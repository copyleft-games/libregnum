/* lrg-cad-manager.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Singleton service for CAD documents in scenes.
 */

#include "lrg-cad-manager.h"

#include <gio/gio.h>
#include <string.h>

#ifdef LRG_BUILD_EDITOR
#include "../editor/lrg-node.h"
#include "../editor/lrg-node-visual.h"
#endif

typedef struct
{
	CadDocument  *document;     /* ref */
	GFileMonitor *monitor;      /* ref, nullable */
} LrgCadManagerEntry;

struct _LrgCadManager
{
	GObject parent_instance;

	GHashTable *documents;      /* abs path -> LrgCadManagerEntry* */
	GHashTable *bakes;          /* cache key -> LrgCadBakeResult* */
};

enum
{
	SIGNAL_DOCUMENT_CHANGED,
	N_SIGNALS
};

static guint lrg_cad_manager_signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (LrgCadManager, lrg_cad_manager, G_TYPE_OBJECT)

static void
lrg_cad_manager_entry_free (gpointer data)
{
	LrgCadManagerEntry *entry = data;

	g_clear_object (&entry->document);
	if (entry->monitor != NULL)
	{
		g_file_monitor_cancel (entry->monitor);
		g_clear_object (&entry->monitor);
	}
	g_free (entry);
}

static void
lrg_cad_manager_finalize (GObject *object)
{
	LrgCadManager *self = LRG_CAD_MANAGER (object);

	g_hash_table_unref (self->documents);
	g_hash_table_unref (self->bakes);

	G_OBJECT_CLASS (lrg_cad_manager_parent_class)->finalize (object);
}

static void
lrg_cad_manager_class_init (LrgCadManagerClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_cad_manager_finalize;

	/**
	 * LrgCadManager::document-changed:
	 * @self: the manager
	 * @path: the part source path that changed on disk
	 *
	 * Emitted when a monitored part source changes; caches for it
	 * have already been dropped, so listeners re-bake.
	 */
	lrg_cad_manager_signals[SIGNAL_DOCUMENT_CHANGED] =
		g_signal_new ("document-changed", LRG_TYPE_CAD_MANAGER,
		              G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
lrg_cad_manager_init (LrgCadManager *self)
{
	self->documents = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                         g_free,
	                                         lrg_cad_manager_entry_free);
	self->bakes = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                     g_free, g_object_unref);
}

LrgCadManager *
lrg_cad_manager_get_default (void)
{
	static LrgCadManager *instance;

	if (g_once_init_enter_pointer (&instance))
	{
		LrgCadManager *self = g_object_new (LRG_TYPE_CAD_MANAGER, NULL);

		g_once_init_leave_pointer (&instance, self);
	}

	return instance;
}

static void
lrg_cad_manager_drop_bakes_for (LrgCadManager *self,
                                const gchar   *abs_path)
{
	GHashTableIter iter;
	gpointer key;
	GList *doomed = NULL;
	GList *l;

	g_hash_table_iter_init (&iter, self->bakes);
	while (g_hash_table_iter_next (&iter, &key, NULL))
	{
		if (g_str_has_prefix ((const gchar *) key, abs_path))
			doomed = g_list_prepend (doomed, g_strdup (key));
	}
	for (l = doomed; l != NULL; l = l->next)
		g_hash_table_remove (self->bakes, l->data);
	g_list_free_full (doomed, g_free);
}

static void
lrg_cad_manager_on_file_changed (GFileMonitor      *monitor,
                                 GFile             *file,
                                 GFile             *other,
                                 GFileMonitorEvent  event,
                                 gpointer           user_data)
{
	LrgCadManager *self = LRG_CAD_MANAGER (user_data);
	gchar *path;

	if (event != G_FILE_MONITOR_EVENT_CHANGED
	    && event != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT
	    && event != G_FILE_MONITOR_EVENT_CREATED)
		return;

	path = g_file_get_path (file);
	if (path == NULL)
		return;

	lrg_cad_manager_invalidate (self, path);
	g_signal_emit (self,
	               lrg_cad_manager_signals[SIGNAL_DOCUMENT_CHANGED], 0,
	               path);
	g_free (path);
}

CadDocument *
lrg_cad_manager_load (LrgCadManager  *self,
                      const gchar    *path,
                      GError        **error)
{
	LrgCadManagerEntry *entry;
	gchar *abs_path;
	CadDocument *document;

	g_return_val_if_fail (LRG_IS_CAD_MANAGER (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	/* Built-in frontends self-register lazily; make sure they exist
	 * before extension dispatch. */
	cad_frontend_sexp_get_default ();
#ifdef CAD_HAVE_CRISPY
	cad_frontend_crispy_get_default ();
#endif

	abs_path = g_canonicalize_filename (path, NULL);
	entry = g_hash_table_lookup (self->documents, abs_path);
	if (entry != NULL)
	{
		g_free (abs_path);
		return entry->document;
	}

	document = cad_document_new_from_file (abs_path, error);
	if (document == NULL)
	{
		g_free (abs_path);
		return NULL;
	}

	entry = g_new0 (LrgCadManagerEntry, 1);
	entry->document = document;
	{
		GFile *file = g_file_new_for_path (abs_path);

		entry->monitor = g_file_monitor_file (file,
		                                      G_FILE_MONITOR_NONE,
		                                      NULL, NULL);
		g_object_unref (file);
		if (entry->monitor != NULL)
			g_signal_connect (entry->monitor, "changed",
			                  G_CALLBACK (lrg_cad_manager_on_file_changed),
			                  self);
	}
	g_hash_table_replace (self->documents, abs_path, entry);

	return document;
}

static gchar *
lrg_cad_manager_bake_key (const gchar *abs_path,
                          GHashTable  *overrides,
                          gdouble      deflection,
                          const gchar *part)
{
	GString *key = g_string_new (abs_path);

	g_string_append_printf (key, "|d=%.9g|p=%s", deflection,
	                        part != NULL ? part : "");

	if (overrides != NULL)
	{
		GList *names = g_hash_table_get_keys (overrides);
		GList *l;

		names = g_list_sort (names, (GCompareFunc) g_strcmp0);
		for (l = names; l != NULL; l = l->next)
		{
			const gdouble *value =
				g_hash_table_lookup (overrides, l->data);

			g_string_append_printf (key, "|%s=%.12g",
			                        (const gchar *) l->data,
			                        value != NULL ? *value : 0.0);
		}
		g_list_free (names);
	}

	return g_string_free (key, FALSE);
}

LrgCadBakeResult *
lrg_cad_manager_bake (LrgCadManager  *self,
                      const gchar    *path,
                      GHashTable     *overrides,
                      gdouble         deflection,
                      const gchar    *part,
                      GError        **error)
{
	CadDocument *document;
	gchar *abs_path;
	gchar *key;
	LrgCadBakeResult *result;

	g_return_val_if_fail (LRG_IS_CAD_MANAGER (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	document = lrg_cad_manager_load (self, path, error);
	if (document == NULL)
		return NULL;

	abs_path = g_canonicalize_filename (path, NULL);
	key = lrg_cad_manager_bake_key (abs_path, overrides, deflection,
	                                part);
	g_free (abs_path);

	result = g_hash_table_lookup (self->bakes, key);
	if (result != NULL)
	{
		g_free (key);
		return result;
	}

	result = lrg_cad_bake (document, overrides, deflection, part, error);
	if (result == NULL)
	{
		g_free (key);
		return NULL;
	}

	g_hash_table_replace (self->bakes, key, result);

	return result;
}

void
lrg_cad_manager_invalidate (LrgCadManager *self,
                            const gchar   *path)
{
	gchar *abs_path;

	g_return_if_fail (LRG_IS_CAD_MANAGER (self));
	g_return_if_fail (path != NULL);

	abs_path = g_canonicalize_filename (path, NULL);
	g_hash_table_remove (self->documents, abs_path);
	lrg_cad_manager_drop_bakes_for (self, abs_path);
	g_free (abs_path);
}

#ifdef LRG_BUILD_EDITOR
GHashTable *
lrg_cad_manager_overrides_for_node (LrgNode *node)
{
	LrgNodeVisual *visual;
	GHashTable *overrides = NULL;
	GList *names;
	GList *l;

	g_return_val_if_fail (LRG_IS_NODE (node), NULL);

	visual = lrg_node_get_visual (node);
	if (visual == NULL)
		return NULL;

	names = lrg_node_visual_get_param_names (visual);
	for (l = names; l != NULL; l = l->next)
	{
		const gchar *name = l->data;
		gdouble *value;

		if (!g_str_has_prefix (name, "cad:"))
			continue;
		if (g_strcmp0 (name, "cad:part") == 0
		    || g_strcmp0 (name, "cad:deflection") == 0)
			continue;

		if (overrides == NULL)
			overrides = g_hash_table_new_full (g_str_hash,
			                                   g_str_equal,
			                                   g_free, g_free);
		value = g_new (gdouble, 1);
		*value = lrg_node_visual_get_param_double (visual, name, 0.0);
		g_hash_table_replace (overrides,
		                      g_strdup (name + strlen ("cad:")),
		                      value);
	}
	g_list_free (names);

	return overrides;
}
#endif
