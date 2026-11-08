/* lrg-editor-plugin.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-editor-plugin.h"

#include <gmodule.h>
#include <string.h>

typedef struct
{
	gchar *id;
	gchar *label;
	gchar *category;
} PluginAction;

typedef struct
{
	gchar *id;
	gchar *title;
} PluginPanel;

struct _LrgEditorPluginManager
{
	GObject    parent_instance;
	GPtrArray *modules;      /* GModule *, not closed: plugins stay resident */
	GPtrArray *actions;      /* PluginAction * */
	GPtrArray *panels;       /* PluginPanel * */
	GPtrArray *inspectors;   /* gchar * */
};

G_DEFINE_FINAL_TYPE (LrgEditorPluginManager, lrg_editor_plugin_manager, G_TYPE_OBJECT)

static void
action_free (gpointer data)
{
	PluginAction *action = (PluginAction *) data;

	g_free (action->id);
	g_free (action->label);
	g_free (action->category);
	g_free (action);
}

static void
panel_free (gpointer data)
{
	PluginPanel *panel = (PluginPanel *) data;

	g_free (panel->id);
	g_free (panel->title);
	g_free (panel);
}

static void
api_add_action (gpointer     manager,
                const gchar *id,
                const gchar *label,
                const gchar *category)
{
	lrg_editor_plugin_manager_add_action (manager, id, label, category);
}

static void
api_add_panel (gpointer     manager,
               const gchar *id,
               const gchar *title)
{
	lrg_editor_plugin_manager_add_panel (manager, id, title);
}

static void
api_add_inspector (gpointer     manager,
                   const gchar *type_name)
{
	lrg_editor_plugin_manager_add_inspector (manager, type_name);
}

static const LrgEditorPluginApi plugin_api = {
	api_add_action,
	api_add_panel,
	api_add_inspector
};

static gchar *
yaml_scalar (const gchar *text,
             const gchar *key)
{
	const gchar *line;
	gsize        klen;

	klen = strlen (key);
	line = text;
	while (line != NULL && *line != '\0')
	{
		const gchar *end;
		const gchar *value;

		end = strchr (line, '\n');
		if (strncmp (line, key, klen) == 0 && line[klen] == ':')
		{
			value = line + klen + 1;
			while (*value == ' ' || *value == '\t')
				value++;
			if (*value == '"' || *value == '\'')
			{
				gchar        q = *value;
				const gchar *stop;

				value++;
				stop = strchr (value, q);
				if (stop == NULL)
					return g_strdup (value);
				return g_strndup (value, (gsize) (stop - value));
			}
			if (end == NULL)
				return g_strdup (value);
			return g_strndup (value, (gsize) (end - value));
		}
		line = (end == NULL) ? NULL : end + 1;
	}
	return NULL;
}

static void
lrg_editor_plugin_manager_dispose (GObject *object)
{
	LrgEditorPluginManager *self = LRG_EDITOR_PLUGIN_MANAGER (object);

	/* Recorded registrations are plain strings, so the modules can close
	 * with the manager. Keep the manager alive while a plugin callback runs. */
	g_clear_pointer (&self->modules, g_ptr_array_unref);
	g_clear_pointer (&self->actions, g_ptr_array_unref);
	g_clear_pointer (&self->panels, g_ptr_array_unref);
	g_clear_pointer (&self->inspectors, g_ptr_array_unref);
	G_OBJECT_CLASS (lrg_editor_plugin_manager_parent_class)->dispose (object);
}

static void
lrg_editor_plugin_manager_class_init (LrgEditorPluginManagerClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_plugin_manager_dispose;
}

static void
close_module (gpointer data)
{
	if (data != NULL)
		g_module_close ((GModule *) data);
}

static void
lrg_editor_plugin_manager_init (LrgEditorPluginManager *self)
{
	self->modules = g_ptr_array_new_with_free_func (close_module);
	self->actions = g_ptr_array_new_with_free_func (action_free);
	self->panels = g_ptr_array_new_with_free_func (panel_free);
	self->inspectors = g_ptr_array_new_with_free_func (g_free);
}

GQuark
lrg_editor_plugin_error_quark (void)
{
	return g_quark_from_static_string ("lrg-editor-plugin-error");
}

LrgEditorPluginManager *
lrg_editor_plugin_manager_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_PLUGIN_MANAGER, NULL);
}

gboolean
lrg_editor_plugin_manager_load (LrgEditorPluginManager  *self,
                                const gchar             *directory,
                                GError                 **error)
{
	g_autofree gchar *manifest_path = NULL;
	g_autofree gchar *text = NULL;
	g_autofree gchar *name = NULL;
	g_autofree gchar *entry = NULL;
	g_autofree gchar *so_path = NULL;
	GModule          *module;
	LrgEditorPluginInit init;

	g_return_val_if_fail (LRG_IS_EDITOR_PLUGIN_MANAGER (self), FALSE);
	g_return_val_if_fail (directory != NULL, FALSE);

	manifest_path = g_build_filename (directory, "plugin.yaml", NULL);
	if (!g_file_get_contents (manifest_path, &text, NULL, error))
		return FALSE;
	name = yaml_scalar (text, "name");
	entry = yaml_scalar (text, "entry");
	if (name == NULL || entry == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_PLUGIN_ERROR, LRG_EDITOR_PLUGIN_ERROR_FAILED,
		                     "plugin.yaml needs name and entry");
		return FALSE;
	}
	so_path = g_build_filename (directory, entry, NULL);
	module = g_module_open (so_path, G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
	if (module == NULL)
	{
		g_set_error (error, LRG_EDITOR_PLUGIN_ERROR, LRG_EDITOR_PLUGIN_ERROR_FAILED,
		             "Could not open '%s': %s", so_path, g_module_error ());
		return FALSE;
	}
	if (!g_module_symbol (module, "lrg_plugin_init", (gpointer *) &init) || init == NULL)
	{
		g_module_close (module);
		g_set_error_literal (error, LRG_EDITOR_PLUGIN_ERROR, LRG_EDITOR_PLUGIN_ERROR_FAILED,
		                     "Plugin does not export lrg_plugin_init");
		return FALSE;
	}
	init (self, &plugin_api);
	g_ptr_array_add (self->modules, module);
	return TRUE;
}

void
lrg_editor_plugin_manager_add_action (LrgEditorPluginManager *self,
                                      const gchar            *id,
                                      const gchar            *label,
                                      const gchar            *category)
{
	PluginAction *action;

	g_return_if_fail (LRG_IS_EDITOR_PLUGIN_MANAGER (self));
	g_return_if_fail (id != NULL);

	action = g_new0 (PluginAction, 1);
	action->id = g_strdup (id);
	action->label = g_strdup ((label != NULL) ? label : id);
	action->category = g_strdup ((category != NULL) ? category : "Plugin");
	g_ptr_array_add (self->actions, action);
}

void
lrg_editor_plugin_manager_add_panel (LrgEditorPluginManager *self,
                                     const gchar            *id,
                                     const gchar            *title)
{
	PluginPanel *panel;

	g_return_if_fail (LRG_IS_EDITOR_PLUGIN_MANAGER (self));
	g_return_if_fail (id != NULL);

	panel = g_new0 (PluginPanel, 1);
	panel->id = g_strdup (id);
	panel->title = g_strdup ((title != NULL) ? title : id);
	g_ptr_array_add (self->panels, panel);
}

void
lrg_editor_plugin_manager_add_inspector (LrgEditorPluginManager *self,
                                         const gchar            *type_name)
{
	g_return_if_fail (LRG_IS_EDITOR_PLUGIN_MANAGER (self));
	g_return_if_fail (type_name != NULL);
	g_ptr_array_add (self->inspectors, g_strdup (type_name));
}

static GStrv
list_field (GPtrArray *rows,
            gboolean   actions)
{
	GStrv out;
	guint i;

	out = g_new0 (gchar *, rows->len + 1);
	for (i = 0; i < rows->len; i++)
	{
		if (actions)
			out[i] = g_strdup (((PluginAction *) g_ptr_array_index (rows, i))->id);
		else
			out[i] = g_strdup (((PluginPanel *) g_ptr_array_index (rows, i))->id);
	}
	return out;
}

GStrv
lrg_editor_plugin_manager_list_actions (LrgEditorPluginManager *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PLUGIN_MANAGER (self), NULL);
	return list_field (self->actions, TRUE);
}

GStrv
lrg_editor_plugin_manager_list_panels (LrgEditorPluginManager *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PLUGIN_MANAGER (self), NULL);
	return list_field (self->panels, FALSE);
}

GStrv
lrg_editor_plugin_manager_list_inspectors (LrgEditorPluginManager *self)
{
	GStrv out;
	guint i;

	g_return_val_if_fail (LRG_IS_EDITOR_PLUGIN_MANAGER (self), NULL);
	out = g_new0 (gchar *, self->inspectors->len + 1);
	for (i = 0; i < self->inspectors->len; i++)
		out[i] = g_strdup ((const gchar *) g_ptr_array_index (self->inspectors, i));
	return out;
}

const gchar *
lrg_editor_plugin_manager_action_label (LrgEditorPluginManager *self,
                                        const gchar            *id)
{
	guint i;

	g_return_val_if_fail (LRG_IS_EDITOR_PLUGIN_MANAGER (self), NULL);
	g_return_val_if_fail (id != NULL, NULL);
	for (i = 0; i < self->actions->len; i++)
	{
		PluginAction *action = (PluginAction *) g_ptr_array_index (self->actions, i);

		if (g_str_equal (action->id, id))
			return action->label;
	}
	return NULL;
}

const gchar *
lrg_editor_plugin_manager_panel_title (LrgEditorPluginManager *self,
                                       const gchar            *id)
{
	guint i;

	g_return_val_if_fail (LRG_IS_EDITOR_PLUGIN_MANAGER (self), NULL);
	g_return_val_if_fail (id != NULL, NULL);
	for (i = 0; i < self->panels->len; i++)
	{
		PluginPanel *panel = (PluginPanel *) g_ptr_array_index (self->panels, i);

		if (g_str_equal (panel->id, id))
			return panel->title;
	}
	return NULL;
}
