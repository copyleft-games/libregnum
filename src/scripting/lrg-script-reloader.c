/* lrg-script-reloader.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScriptReloader: GFileMonitor watches on script files and search
 * paths, debounced reloads into every live context that loaded the file.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SCRIPTING
#define LRG_SCRIPTING_PRIVATE

#include "lrg-script-reloader.h"
#include "../lrg-log.h"

#include <gio/gio.h>
#include <string.h>

/* One installed GFileMonitor.  Directories are not recursive, so each
 * subdirectory gets its own monitor. */
typedef struct
{
	GFileMonitor      *monitor;
	gboolean           is_dir;
	LrgScriptReloader *owner;
} Watch;

typedef struct
{
	LrgScripting      *scripting;
	LrgScriptReloader *owner;
} WatchedScript;

struct _LrgScriptReloader
{
	GObject parent_instance;

	GMutex        lock;
	GMainContext *context;       /* thread-default of the first watch */

	GHashTable *explicit_paths;  /* canonical path -> gboolean is_dir (pointer) */
	GPtrArray  *watched;         /* WatchedScript *, weak on the context */
	gboolean    watch_all;
	gboolean    instance_hook;
	gboolean    path_hook;

	gboolean    enabled;
	guint       debounce_ms;

	GHashTable *monitors;        /* canonical path -> Watch * */
	GHashTable *pending;         /* canonical file -> gint64 * due (monotonic usec) */

	guint       reload_count;
	guint       failure_count;
	gchar      *last_error;
};

G_DEFINE_FINAL_TYPE (LrgScriptReloader, lrg_script_reloader, G_TYPE_OBJECT)

enum
{
	SIGNAL_CHANGED,
	SIGNAL_RELOADED,
	SIGNAL_RELOAD_FAILED,
	N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

static void          reloader_rebuild (LrgScriptReloader *self);
static void          reloader_path_hook (LrgScripting *scripting,
                                         const gchar  *path,
                                         gpointer      user_data);
static void          reloader_instance_hook (LrgScripting *scripting,
                                             gpointer      user_data);
static void          on_file_changed (GFileMonitor      *monitor,
                                      GFile             *file,
                                      GFile             *other_file,
                                      GFileMonitorEvent  event,
                                      gpointer           user_data);

/* ==========================================================================
 * Monitors
 * ========================================================================== */

static void
watch_free (gpointer data)
{
	Watch *watch = (Watch *) data;

	if (watch->monitor != NULL)
	{
		g_signal_handlers_disconnect_by_data (watch->monitor, watch->owner);
		g_file_monitor_cancel (watch->monitor);
		g_object_unref (watch->monitor);
	}
	g_free (watch);
}

static void
pending_free (gpointer data)
{
	g_free (data);
}

static gchar *
canon_or_null (const gchar *path)
{
	if (path == NULL || path[0] == '\0')
		return NULL;
	return g_canonicalize_filename (path, NULL);
}

/* Subdirectories of @root, not including @root.  Caller owns the list. */
static GPtrArray *
list_subdirs (const gchar *root)
{
	GPtrArray *found = g_ptr_array_new_with_free_func (g_free);
	GQueue *queue = g_queue_new ();

	g_queue_push_tail (queue, g_strdup (root));
	while (!g_queue_is_empty (queue))
	{
		gchar *dir = (gchar *) g_queue_pop_head (queue);
		GDir *handle;
		const gchar *name;

		handle = g_dir_open (dir, 0, NULL);
		if (handle != NULL)
		{
			while ((name = g_dir_read_name (handle)) != NULL)
			{
				gchar *child = g_build_filename (dir, name, NULL);

				if (g_file_test (child, G_FILE_TEST_IS_DIR))
				{
					g_ptr_array_add (found, g_strdup (child));
					g_queue_push_tail (queue, child);
				}
				else
					g_free (child);
			}
			g_dir_close (handle);
		}
		g_free (dir);
	}
	g_queue_free (queue);
	return found;
}

static void
ensure_context (LrgScriptReloader *self)
{
	if (self->context == NULL)
		self->context = g_main_context_ref_thread_default ();
}

/*
 * Install a monitor for @canon.  The reloader lock is NOT held: creating
 * a monitor can dispatch, and the callback takes the lock.
 */
static gboolean
ensure_monitor (LrgScriptReloader  *self,
                const gchar        *canon,
                gboolean            is_dir,
                GError            **error)
{
	GFile *file;
	GFileMonitor *monitor;
	GError *local = NULL;
	Watch *watch;
	gboolean pushed = FALSE;
	gboolean exists;

	g_mutex_lock (&self->lock);
	exists = g_hash_table_contains (self->monitors, canon);
	ensure_context (self);
	g_mutex_unlock (&self->lock);
	if (exists)
		return TRUE;

	if (g_main_context_get_thread_default () != self->context)
	{
		g_main_context_push_thread_default (self->context);
		pushed = TRUE;
	}

	file = g_file_new_for_path (canon);
	monitor = g_file_monitor (file,
	                          G_FILE_MONITOR_WATCH_MOVES,
	                          NULL,
	                          &local);
	g_object_unref (file);

	if (pushed)
		g_main_context_pop_thread_default (self->context);

	if (monitor == NULL)
	{
		g_propagate_error (error, local);
		return FALSE;
	}

	/* GIO's default rate limit is 800 ms, which hides a save from the
	 * next frame.  The reloader debounces itself. */
	g_file_monitor_set_rate_limit (monitor, 0);

	watch = g_new0 (Watch, 1);
	watch->monitor = monitor;
	watch->is_dir = is_dir;
	watch->owner = self;

	g_mutex_lock (&self->lock);
	if (g_hash_table_contains (self->monitors, canon))
	{
		g_mutex_unlock (&self->lock);
		g_file_monitor_cancel (monitor);
		g_object_unref (monitor);
		g_free (watch);
		return TRUE;
	}
	g_signal_connect (monitor, "changed", G_CALLBACK (on_file_changed), self);
	g_hash_table_insert (self->monitors, g_strdup (canon), watch);
	g_mutex_unlock (&self->lock);
	return TRUE;
}

static void
queue_reload (LrgScriptReloader *self,
              const gchar       *path)
{
	gint64 *due;
	gint64 when;

	when = g_get_monotonic_time () + (gint64) self->debounce_ms * 1000;
	due = (gint64 *) g_hash_table_lookup (self->pending, path);
	if (due != NULL)
	{
		*due = when;
		return;
	}
	due = g_new (gint64, 1);
	*due = when;
	g_hash_table_insert (self->pending, g_strdup (path), due);
}

static void
on_file_changed (GFileMonitor      *monitor,
                 GFile             *file,
                 GFile             *other_file,
                 GFileMonitorEvent  event,
                 gpointer           user_data)
{
	LrgScriptReloader *self = (LrgScriptReloader *) user_data;
	g_autofree gchar *path = NULL;
	g_autofree gchar *other = NULL;
	gboolean is_dir = FALSE;
	gboolean enabled;
	gchar *emit_path = NULL;

	(void) monitor;

	if (event == G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED ||
	    event == G_FILE_MONITOR_EVENT_PRE_UNMOUNT ||
	    event == G_FILE_MONITOR_EVENT_UNMOUNTED)
		return;

	path = g_file_get_path (file);
	if (path == NULL)
		return;

	if (event == G_FILE_MONITOR_EVENT_CREATED &&
	    g_file_test (path, G_FILE_TEST_IS_DIR))
		is_dir = TRUE;

	g_mutex_lock (&self->lock);
	enabled = self->enabled;
	if (event != G_FILE_MONITOR_EVENT_DELETED && enabled)
		queue_reload (self, path);
	if (other_file != NULL &&
	    (event == G_FILE_MONITOR_EVENT_RENAMED ||
	     event == G_FILE_MONITOR_EVENT_MOVED_OUT))
	{
		other = g_file_get_path (other_file);
		if (other != NULL && enabled)
			queue_reload (self, other);
	}
	emit_path = g_strdup (path);
	g_mutex_unlock (&self->lock);

	g_signal_emit (self, signals[SIGNAL_CHANGED], 0, emit_path);
	if (other != NULL)
		g_signal_emit (self, signals[SIGNAL_CHANGED], 0, other);
	g_free (emit_path);

	/* A new subdirectory of a watched directory needs its own monitor
	 * (GFileMonitor does not recurse). */
	if (is_dir)
	{
		g_autoptr(GError) error = NULL;

		if (!ensure_monitor (self, path, TRUE, &error))
			lrg_debug (LRG_LOG_DOMAIN_SCRIPTING,
			           "script reloader: %s", error->message);
	}
}

/* Add @canon and, when it is a directory, every subdirectory. */
static gboolean
watch_tree (LrgScriptReloader  *self,
            const gchar        *canon,
            gboolean            is_dir,
            GError            **error)
{
	if (!ensure_monitor (self, canon, is_dir, error))
		return FALSE;

	if (is_dir)
	{
		g_autoptr(GPtrArray) subs = list_subdirs (canon);
		guint i;

		for (i = 0; i < subs->len; i++)
		{
			g_autoptr(GError) local = NULL;
			const gchar *sub = (const gchar *) g_ptr_array_index (subs, i);

			if (!ensure_monitor (self, sub, TRUE, &local))
				lrg_debug (LRG_LOG_DOMAIN_SCRIPTING,
				           "script reloader: %s", local->message);
		}
	}
	return TRUE;
}

static gboolean
scripting_is_watched (LrgScriptReloader *self,
                      LrgScripting      *scripting)
{
	guint i;

	if (self->watch_all)
		return TRUE;
	for (i = 0; i < self->watched->len; i++)
	{
		WatchedScript *entry = (WatchedScript *) g_ptr_array_index (self->watched, i);

		if (entry->scripting == scripting)
			return TRUE;
	}
	return FALSE;
}

/* Paths this reloader should be monitoring right now.  Lock is held.
 * Returns a hash of canonical path -> GINT_TO_POINTER(is_dir). */
static GHashTable *
collect_wanted_locked (LrgScriptReloader *self)
{
	GHashTable *wanted;
	GHashTableIter iter;
	gpointer key;
	gpointer value;
	guint i;

	wanted = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	g_hash_table_iter_init (&iter, self->explicit_paths);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		gboolean is_dir = GPOINTER_TO_INT (value);
		g_autoptr(GPtrArray) subs = NULL;

		g_hash_table_insert (wanted, g_strdup ((const gchar *) key),
		                     GINT_TO_POINTER (is_dir));
		if (!is_dir)
			continue;
		subs = list_subdirs ((const gchar *) key);
		for (i = 0; i < subs->len; i++)
			g_hash_table_insert (wanted,
			                     g_strdup ((const gchar *) g_ptr_array_index (subs, i)),
			                     GINT_TO_POINTER (TRUE));
	}

	{
		g_autoptr(GPtrArray) extra = NULL;
		GPtrArray *sources;

		if (self->watch_all)
			extra = lrg_scripting_list_instances ();
		sources = self->watch_all ? extra : NULL;

		if (sources == NULL)
		{
			sources = g_ptr_array_new ();
			for (i = 0; i < self->watched->len; i++)
			{
				WatchedScript *entry = (WatchedScript *) g_ptr_array_index (self->watched, i);

				g_ptr_array_add (sources, entry->scripting);
			}
		}

		for (i = 0; i < sources->len; i++)
		{
			LrgScripting *scripting = (LrgScripting *) g_ptr_array_index (sources, i);
			g_auto(GStrv) files = lrg_scripting_get_loaded_files (scripting);
			g_auto(GStrv) paths = lrg_scripting_get_search_paths (scripting);
			gchar **cursor;

			for (cursor = files; cursor != NULL && *cursor != NULL; cursor++)
				g_hash_table_replace (wanted, g_strdup (*cursor),
				                      GINT_TO_POINTER (FALSE));
			for (cursor = paths; cursor != NULL && *cursor != NULL; cursor++)
			{
				g_autoptr(GPtrArray) subs = NULL;
				guint s;

				g_hash_table_replace (wanted, g_strdup (*cursor),
				                      GINT_TO_POINTER (TRUE));
				subs = list_subdirs (*cursor);
				for (s = 0; s < subs->len; s++)
					g_hash_table_replace (wanted,
					                      g_strdup ((const gchar *) g_ptr_array_index (subs, s)),
					                      GINT_TO_POINTER (TRUE));
			}
		}

		if (!self->watch_all)
			g_ptr_array_unref (sources);
	}

	return wanted;
}

static void
reloader_rebuild (LrgScriptReloader *self)
{
	GHashTable *wanted;
	GHashTableIter iter;
	gpointer key;
	gpointer value;
	GPtrArray *drop;
	guint i;

	g_mutex_lock (&self->lock);
	wanted = collect_wanted_locked (self);
	drop = g_ptr_array_new_with_free_func (g_free);
	g_hash_table_iter_init (&iter, self->monitors);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		if (!g_hash_table_contains (wanted, key))
			g_ptr_array_add (drop, g_strdup ((const gchar *) key));
	}
	for (i = 0; i < drop->len; i++)
		g_hash_table_remove (self->monitors, g_ptr_array_index (drop, i));
	g_mutex_unlock (&self->lock);

	g_hash_table_iter_init (&iter, wanted);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		g_autoptr(GError) error = NULL;

		if (!ensure_monitor (self, (const gchar *) key,
		                     GPOINTER_TO_INT (value), &error))
			lrg_debug (LRG_LOG_DOMAIN_SCRIPTING,
			           "script reloader: %s", error->message);
	}

	g_hash_table_unref (wanted);
	g_ptr_array_unref (drop);
}

/* ==========================================================================
 * Scripting hooks
 * ========================================================================== */

static void
on_scripting_gone (gpointer data,
                   GObject *where_the_object_was)
{
	LrgScriptReloader *self = (LrgScriptReloader *) data;
	guint i;

	g_mutex_lock (&self->lock);
	for (i = 0; i < self->watched->len; i++)
	{
		WatchedScript *entry = (WatchedScript *) g_ptr_array_index (self->watched, i);

		if ((GObject *) entry->scripting == where_the_object_was)
		{
			g_ptr_array_remove_index (self->watched, i);
			break;
		}
	}
	g_mutex_unlock (&self->lock);
	reloader_rebuild (self);
}

static void
reloader_path_hook (LrgScripting *scripting,
                    const gchar  *path,
                    gpointer      user_data)
{
	LrgScriptReloader *self = (LrgScriptReloader *) user_data;
	gboolean watch = FALSE;
	gboolean is_dir;

	g_mutex_lock (&self->lock);
	watch = scripting_is_watched (self, scripting);
	g_mutex_unlock (&self->lock);
	if (!watch || path == NULL)
		return;

	is_dir = g_file_test (path, G_FILE_TEST_IS_DIR);
	{
		g_autoptr(GError) error = NULL;

		if (!watch_tree (self, path, is_dir, &error))
			lrg_debug (LRG_LOG_DOMAIN_SCRIPTING,
			           "script reloader: %s", error->message);
	}
}

static void
watch_scripting_paths (LrgScriptReloader *self,
                       LrgScripting      *scripting)
{
	g_auto(GStrv) files = lrg_scripting_get_loaded_files (scripting);
	g_auto(GStrv) paths = lrg_scripting_get_search_paths (scripting);
	gchar **cursor;

	for (cursor = files; cursor != NULL && *cursor != NULL; cursor++)
	{
		g_autoptr(GError) error = NULL;

		if (!ensure_monitor (self, *cursor, FALSE, &error))
			lrg_debug (LRG_LOG_DOMAIN_SCRIPTING,
			           "script reloader: %s", error->message);
	}
	for (cursor = paths; cursor != NULL && *cursor != NULL; cursor++)
	{
		g_autoptr(GError) error = NULL;

		if (!watch_tree (self, *cursor, TRUE, &error))
			lrg_debug (LRG_LOG_DOMAIN_SCRIPTING,
			           "script reloader: %s", error->message);
	}
}

static void
reloader_instance_hook (LrgScripting *scripting,
                        gpointer      user_data)
{
	LrgScriptReloader *self = (LrgScriptReloader *) user_data;

	if (!self->watch_all)
		return;
	watch_scripting_paths (self, scripting);
}

static void
ensure_path_hook (LrgScriptReloader *self)
{
	if (self->path_hook)
		return;
	lrg_scripting_priv_add_path_hook (reloader_path_hook, self);
	self->path_hook = TRUE;
}

/* ==========================================================================
 * Reload
 * ========================================================================== */

static gchar *
format_failure (LrgScripting *scripting,
                const gchar  *path,
                const GError *error)
{
	gchar *file = NULL;
	gint line = 0;
	gchar *text;
	const gchar *message;

	lrg_scripting_get_error_location (scripting, &file, &line);
	if (file == NULL)
		file = g_strdup (path);
	message = (error != NULL && error->message != NULL) ? error->message : "reload failed";
	text = g_strdup_printf ("%s:%d: %s", file, line, message);
	g_free (file);
	return text;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_script_reloader_finalize (GObject *object)
{
	LrgScriptReloader *self = LRG_SCRIPT_RELOADER (object);
	guint i;

	if (self->instance_hook)
		lrg_scripting_priv_remove_instance_hook (reloader_instance_hook, self);
	if (self->path_hook)
		lrg_scripting_priv_remove_path_hook (reloader_path_hook, self);

	for (i = 0; i < self->watched->len; i++)
	{
		WatchedScript *entry = (WatchedScript *) g_ptr_array_index (self->watched, i);

		g_object_weak_unref (G_OBJECT (entry->scripting), on_scripting_gone, self);
	}

	g_clear_pointer (&self->explicit_paths, g_hash_table_unref);
	g_clear_pointer (&self->watched, g_ptr_array_unref);
	g_clear_pointer (&self->monitors, g_hash_table_unref);
	g_clear_pointer (&self->pending, g_hash_table_unref);
	g_clear_pointer (&self->last_error, g_free);
	g_clear_pointer (&self->context, g_main_context_unref);
	g_mutex_clear (&self->lock);

	G_OBJECT_CLASS (lrg_script_reloader_parent_class)->finalize (object);
}

static void
lrg_script_reloader_class_init (LrgScriptReloaderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_script_reloader_finalize;

	/**
	 * LrgScriptReloader::changed:
	 * @reloader: the reloader
	 * @path: (type filename): the file that changed
	 *
	 * A watched file changed.  Emitted even when reloading is disabled.
	 */
	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed",
		              LRG_TYPE_SCRIPT_RELOADER,
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_STRING);

	/**
	 * LrgScriptReloader::reloaded:
	 * @reloader: the reloader
	 * @scripting: the context now running the new code
	 * @path: (type filename): the file
	 *
	 * @scripting reloaded @path.
	 */
	signals[SIGNAL_RELOADED] =
		g_signal_new ("reloaded",
		              LRG_TYPE_SCRIPT_RELOADER,
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 2, LRG_TYPE_SCRIPTING, G_TYPE_STRING);

	/**
	 * LrgScriptReloader::reload-failed:
	 * @reloader: the reloader
	 * @scripting: the context still running the old code
	 * @path: (type filename): the file
	 * @message: "file:line: message"
	 *
	 * The reload failed and the previous code is still running.
	 */
	signals[SIGNAL_RELOAD_FAILED] =
		g_signal_new ("reload-failed",
		              LRG_TYPE_SCRIPT_RELOADER,
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 3,
		              LRG_TYPE_SCRIPTING, G_TYPE_STRING, G_TYPE_STRING);
}

static void
lrg_script_reloader_init (LrgScriptReloader *self)
{
	g_mutex_init (&self->lock);
	self->explicit_paths = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->watched = g_ptr_array_new_with_free_func (g_free);
	self->monitors = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, watch_free);
	self->pending = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, pending_free);
	self->enabled = TRUE;
	self->debounce_ms = 100;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/*
 * lrg_script_reloader_new:
 *
 * Returns: (transfer full): a new #LrgScriptReloader
 */
LrgScriptReloader *
lrg_script_reloader_new (void)
{
	return g_object_new (LRG_TYPE_SCRIPT_RELOADER, NULL);
}

/*
 * lrg_script_reloader_get_default:
 *
 * Returns: (transfer none): the process-wide reloader
 */
LrgScriptReloader *
lrg_script_reloader_get_default (void)
{
	static gsize instance_bits = 0;

	if (g_once_init_enter (&instance_bits))
	{
		LrgScriptReloader *created = lrg_script_reloader_new ();

		g_once_init_leave (&instance_bits, (gsize) created);
	}
	return (LrgScriptReloader *) instance_bits;
}

/*
 * lrg_script_reloader_watch_path:
 * @self: an #LrgScriptReloader
 * @path: (type filename): a file or a directory
 * @error: (nullable): return location for an error
 *
 * Returns: %TRUE when the watch was installed
 */
gboolean
lrg_script_reloader_watch_path (LrgScriptReloader  *self,
                                const gchar        *path,
                                GError            **error)
{
	g_autofree gchar *canon = NULL;
	gboolean is_dir;

	g_return_val_if_fail (LRG_IS_SCRIPT_RELOADER (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	canon = canon_or_null (path);
	if (canon == NULL || !g_file_test (canon, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		             "No such path: %s", path);
		return FALSE;
	}
	is_dir = g_file_test (canon, G_FILE_TEST_IS_DIR);

	g_mutex_lock (&self->lock);
	ensure_context (self);
	g_hash_table_replace (self->explicit_paths, g_strdup (canon),
	                      GINT_TO_POINTER (is_dir));
	g_mutex_unlock (&self->lock);

	return watch_tree (self, canon, is_dir, error);
}

/*
 * lrg_script_reloader_unwatch_path:
 * @self: an #LrgScriptReloader
 * @path: (type filename): a path passed to lrg_script_reloader_watch_path()
 */
void
lrg_script_reloader_unwatch_path (LrgScriptReloader *self,
                                  const gchar       *path)
{
	g_autofree gchar *canon = NULL;

	g_return_if_fail (LRG_IS_SCRIPT_RELOADER (self));
	g_return_if_fail (path != NULL);

	canon = canon_or_null (path);
	g_mutex_lock (&self->lock);
	if (canon != NULL)
		g_hash_table_remove (self->explicit_paths, canon);
	g_mutex_unlock (&self->lock);
	reloader_rebuild (self);
}

/*
 * lrg_script_reloader_watch_scripting:
 * @self: an #LrgScriptReloader
 * @scripting: an #LrgScripting
 */
void
lrg_script_reloader_watch_scripting (LrgScriptReloader *self,
                                     LrgScripting      *scripting)
{
	WatchedScript *entry;

	g_return_if_fail (LRG_IS_SCRIPT_RELOADER (self));
	g_return_if_fail (LRG_IS_SCRIPTING (scripting));

	g_mutex_lock (&self->lock);
	if (scripting_is_watched (self, scripting) && !self->watch_all)
	{
		g_mutex_unlock (&self->lock);
		return;
	}
	/* watch-all already covers it; still record an explicit watch so
	 * turning watch-all off keeps this context. */
	{
		guint i;
		gboolean found = FALSE;

		for (i = 0; i < self->watched->len; i++)
		{
			WatchedScript *existing = (WatchedScript *) g_ptr_array_index (self->watched, i);

			if (existing->scripting == scripting)
				found = TRUE;
		}
		if (!found)
		{
			entry = g_new0 (WatchedScript, 1);
			entry->scripting = scripting;
			entry->owner = self;
			g_ptr_array_add (self->watched, entry);
			g_object_weak_ref (G_OBJECT (scripting), on_scripting_gone, self);
		}
	}
	ensure_context (self);
	ensure_path_hook (self);
	g_mutex_unlock (&self->lock);

	watch_scripting_paths (self, scripting);
}

/*
 * lrg_script_reloader_unwatch_scripting:
 * @self: an #LrgScriptReloader
 * @scripting: an #LrgScripting
 */
void
lrg_script_reloader_unwatch_scripting (LrgScriptReloader *self,
                                       LrgScripting      *scripting)
{
	guint i;

	g_return_if_fail (LRG_IS_SCRIPT_RELOADER (self));
	g_return_if_fail (LRG_IS_SCRIPTING (scripting));

	g_mutex_lock (&self->lock);
	for (i = 0; i < self->watched->len; i++)
	{
		WatchedScript *entry = (WatchedScript *) g_ptr_array_index (self->watched, i);

		if (entry->scripting == scripting)
		{
			g_object_weak_unref (G_OBJECT (scripting), on_scripting_gone, self);
			g_ptr_array_remove_index (self->watched, i);
			break;
		}
	}
	g_mutex_unlock (&self->lock);
	reloader_rebuild (self);
}

/*
 * lrg_script_reloader_set_watch_all:
 * @self: an #LrgScriptReloader
 * @watch_all: whether every live context is watched
 */
void
lrg_script_reloader_set_watch_all (LrgScriptReloader *self,
                                   gboolean           watch_all)
{
	g_autoptr(GPtrArray) instances = NULL;
	guint i;

	g_return_if_fail (LRG_IS_SCRIPT_RELOADER (self));

	watch_all = !!watch_all;
	g_mutex_lock (&self->lock);
	if (self->watch_all == watch_all)
	{
		g_mutex_unlock (&self->lock);
		return;
	}
	self->watch_all = watch_all;
	if (watch_all && !self->instance_hook)
	{
		lrg_scripting_priv_add_instance_hook (reloader_instance_hook, self);
		self->instance_hook = TRUE;
	}
	else if (!watch_all && self->instance_hook)
	{
		lrg_scripting_priv_remove_instance_hook (reloader_instance_hook, self);
		self->instance_hook = FALSE;
	}
	if (watch_all)
		ensure_path_hook (self);
	g_mutex_unlock (&self->lock);

	if (watch_all)
	{
		instances = lrg_scripting_list_instances ();
		for (i = 0; i < instances->len; i++)
			watch_scripting_paths (self, (LrgScripting *) g_ptr_array_index (instances, i));
	}
	else
		reloader_rebuild (self);
}

/*
 * lrg_script_reloader_get_watch_all:
 * @self: an #LrgScriptReloader
 *
 * Returns: whether watch-all is on
 */
gboolean
lrg_script_reloader_get_watch_all (LrgScriptReloader *self)
{
	gboolean value;

	g_return_val_if_fail (LRG_IS_SCRIPT_RELOADER (self), FALSE);
	g_mutex_lock (&self->lock);
	value = self->watch_all;
	g_mutex_unlock (&self->lock);
	return value;
}

/*
 * lrg_script_reloader_set_enabled:
 * @self: an #LrgScriptReloader
 * @enabled: whether file changes trigger reloads
 */
void
lrg_script_reloader_set_enabled (LrgScriptReloader *self,
                                 gboolean           enabled)
{
	g_return_if_fail (LRG_IS_SCRIPT_RELOADER (self));
	g_mutex_lock (&self->lock);
	self->enabled = !!enabled;
	g_mutex_unlock (&self->lock);
}

/*
 * lrg_script_reloader_get_enabled:
 * @self: an #LrgScriptReloader
 *
 * Returns: whether reloading is enabled
 */
gboolean
lrg_script_reloader_get_enabled (LrgScriptReloader *self)
{
	gboolean value;

	g_return_val_if_fail (LRG_IS_SCRIPT_RELOADER (self), FALSE);
	g_mutex_lock (&self->lock);
	value = self->enabled;
	g_mutex_unlock (&self->lock);
	return value;
}

/*
 * lrg_script_reloader_set_debounce_ms:
 * @self: an #LrgScriptReloader
 * @debounce_ms: quiet period before a reload, in milliseconds
 */
void
lrg_script_reloader_set_debounce_ms (LrgScriptReloader *self,
                                     guint              debounce_ms)
{
	g_return_if_fail (LRG_IS_SCRIPT_RELOADER (self));
	g_mutex_lock (&self->lock);
	self->debounce_ms = debounce_ms;
	g_mutex_unlock (&self->lock);
}

/*
 * lrg_script_reloader_get_debounce_ms:
 * @self: an #LrgScriptReloader
 *
 * Returns: the debounce window in milliseconds
 */
guint
lrg_script_reloader_get_debounce_ms (LrgScriptReloader *self)
{
	guint value;

	g_return_val_if_fail (LRG_IS_SCRIPT_RELOADER (self), 0);
	g_mutex_lock (&self->lock);
	value = self->debounce_ms;
	g_mutex_unlock (&self->lock);
	return value;
}

/*
 * lrg_script_reloader_reload_file:
 * @self: an #LrgScriptReloader
 * @path: (type filename): a script file
 * @error: (nullable): return location for the first failure
 *
 * Returns: %FALSE when any context failed
 */
gboolean
lrg_script_reloader_reload_file (LrgScriptReloader  *self,
                                 const gchar        *path,
                                 GError            **error)
{
	g_autoptr(GPtrArray) instances = NULL;
	g_autofree gchar *canon = NULL;
	gboolean any_fail = FALSE;
	gboolean any_ok = FALSE;
	guint i;

	g_return_val_if_fail (LRG_IS_SCRIPT_RELOADER (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	canon = canon_or_null (path);
	instances = lrg_scripting_list_instances ();
	for (i = 0; i < instances->len; i++)
	{
		LrgScripting *scripting = (LrgScripting *) g_ptr_array_index (instances, i);
		g_autoptr(GError) local = NULL;

		if (!lrg_scripting_has_loaded_file (scripting, canon != NULL ? canon : path))
			continue;

		if (lrg_scripting_reload_file (scripting, canon != NULL ? canon : path, &local))
		{
			any_ok = TRUE;
			g_mutex_lock (&self->lock);
			self->reload_count++;
			g_mutex_unlock (&self->lock);
			g_signal_emit (self, signals[SIGNAL_RELOADED], 0, scripting,
			               canon != NULL ? canon : path);
		}
		else
		{
			g_autofree gchar *message = format_failure (scripting,
			                                            canon != NULL ? canon : path,
			                                            local);

			any_fail = TRUE;
			g_mutex_lock (&self->lock);
			self->failure_count++;
			g_free (self->last_error);
			self->last_error = g_strdup (message);
			g_mutex_unlock (&self->lock);
			g_signal_emit (self, signals[SIGNAL_RELOAD_FAILED], 0,
			               scripting, canon != NULL ? canon : path, message);
			if (error != NULL && *error == NULL)
				g_propagate_error (error, g_steal_pointer (&local));
		}
	}

	if (!any_fail)
	{
		g_mutex_lock (&self->lock);
		g_clear_pointer (&self->last_error, g_free);
		g_mutex_unlock (&self->lock);
	}

	(void) any_ok;
	return !any_fail;
}

/*
 * lrg_script_reloader_process_pending:
 * @self: an #LrgScriptReloader
 *
 * Returns: the number of files whose reload succeeded
 */
guint
lrg_script_reloader_process_pending (LrgScriptReloader *self)
{
	GMainContext *context;
	GPtrArray *due;
	gint64 now;
	GHashTableIter iter;
	gpointer key;
	gpointer value;
	guint i;
	guint files = 0;

	g_return_val_if_fail (LRG_IS_SCRIPT_RELOADER (self), 0);

	g_mutex_lock (&self->lock);
	context = self->context != NULL ? g_main_context_ref (self->context) : NULL;
	g_mutex_unlock (&self->lock);

	if (context != NULL)
	{
		/* The main loop already owns the context when one is running;
		 * it dispatches the monitors itself.  Otherwise drain them. */
		if (g_main_context_acquire (context))
		{
			while (g_main_context_pending (context))
				g_main_context_iteration (context, FALSE);
			g_main_context_release (context);
		}
		g_main_context_unref (context);
	}

	due = g_ptr_array_new_with_free_func (g_free);
	now = g_get_monotonic_time ();
	g_mutex_lock (&self->lock);
	g_hash_table_iter_init (&iter, self->pending);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		gint64 when = *((gint64 *) value);

		if (when <= now)
		{
			g_ptr_array_add (due, g_strdup ((const gchar *) key));
			g_hash_table_iter_remove (&iter);
		}
	}
	g_mutex_unlock (&self->lock);

	for (i = 0; i < due->len; i++)
	{
		const gchar *path = (const gchar *) g_ptr_array_index (due, i);
		guint before;
		g_autoptr(GError) error = NULL;

		before = lrg_script_reloader_get_reload_count (self);
		if (lrg_script_reloader_reload_file (self, path, &error) &&
		    lrg_script_reloader_get_reload_count (self) > before)
			files++;
	}
	g_ptr_array_unref (due);
	return files;
}

/*
 * lrg_script_reloader_get_reload_count:
 * @self: an #LrgScriptReloader
 *
 * Returns: successful per-context reloads since creation
 */
guint
lrg_script_reloader_get_reload_count (LrgScriptReloader *self)
{
	guint value;

	g_return_val_if_fail (LRG_IS_SCRIPT_RELOADER (self), 0);
	g_mutex_lock (&self->lock);
	value = self->reload_count;
	g_mutex_unlock (&self->lock);
	return value;
}

/*
 * lrg_script_reloader_get_failure_count:
 * @self: an #LrgScriptReloader
 *
 * Returns: failed per-context reloads since creation
 */
guint
lrg_script_reloader_get_failure_count (LrgScriptReloader *self)
{
	guint value;

	g_return_val_if_fail (LRG_IS_SCRIPT_RELOADER (self), 0);
	g_mutex_lock (&self->lock);
	value = self->failure_count;
	g_mutex_unlock (&self->lock);
	return value;
}

/*
 * lrg_script_reloader_dup_last_error:
 * @self: an #LrgScriptReloader
 *
 * Returns: (transfer full) (nullable): "file:line: message" of the most
 *   recent failure, or %NULL after a fully successful reload
 */
gchar *
lrg_script_reloader_dup_last_error (LrgScriptReloader *self)
{
	gchar *copy;

	g_return_val_if_fail (LRG_IS_SCRIPT_RELOADER (self), NULL);
	g_mutex_lock (&self->lock);
	copy = g_strdup (self->last_error);
	g_mutex_unlock (&self->lock);
	return copy;
}

static gint
compare_strings (gconstpointer a,
                 gconstpointer b)
{
	return strcmp (*(gchar * const *) a, *(gchar * const *) b);
}

/*
 * lrg_script_reloader_get_watched_paths:
 * @self: an #LrgScriptReloader
 *
 * Returns: (transfer full) (array zero-terminated=1): canonical watched paths
 */
gchar **
lrg_script_reloader_get_watched_paths (LrgScriptReloader *self)
{
	GStrvBuilder *builder;
	gchar **paths;
	GHashTableIter iter;
	gpointer key;
	guint n;

	g_return_val_if_fail (LRG_IS_SCRIPT_RELOADER (self), NULL);

	builder = g_strv_builder_new ();
	g_mutex_lock (&self->lock);
	g_hash_table_iter_init (&iter, self->monitors);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		g_strv_builder_add (builder, (const gchar *) key);
	g_mutex_unlock (&self->lock);

	paths = g_strv_builder_end (builder);
	g_strv_builder_unref (builder);
	n = g_strv_length (paths);
	if (n > 1)
		qsort (paths, n, sizeof (gchar *), compare_strings);
	return paths;
}
