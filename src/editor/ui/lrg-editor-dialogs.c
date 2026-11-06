/* lrg-editor-dialogs.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The editor's modal dialogs on LrgDialog and LrgFileDialog: unsaved
 * changes (Save / Don't Save / Cancel), open and save level (.rlevel
 * filter), open project (folder), import scene, save and instantiate
 * prefab, new project, new script, crash recovery, about, the shortcut
 * list and a project settings summary.
 *
 * One dialog is open at a time. Its id goes into
 * LrgEditorViewState:dialog while it shows (ui.state "dialog" reports the
 * same strings as 0.8.0: "confirm", "file", "new-project", "new-script",
 * "recover", "about", "shortcuts", "project-settings"), which also takes
 * the "editor" action group off the canvas so shortcuts do not fire
 * behind it. Escape answers LRG_DIALOG_RESPONSE_CANCEL.
 *
 * The preferences editor, command palette and quick open come in Wave 4
 * (their actions show a notification until then).
 */

#include "config.h"

#include "lrg-editor-app-private.h"
#include "../../ui/lrg-dialog.h"
#include "../../ui/lrg-file-dialog.h"
#include "../../ui/lrg-file-browser.h"
#include "../../ui/lrg-text-input.h"
#include "../../ui/lrg-combo-box.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-vbox.h"
#include <string.h>

#define DIALOG_KEY "lrg-editor-dialog"

/* Responses of the editor's own buttons (LrgDialog's are <= 0) */
enum
{
	RESPONSE_PRIMARY = 1,
	RESPONSE_SECONDARY = 2,
	RESPONSE_TERTIARY = 3
};

typedef void (*DialogDone) (LrgEditorApp *self, LrgDialog *dialog, gint response);

/* ==========================================================================
 * Bookkeeping
 * ========================================================================== */

static void
dialog_finished (LrgEditorApp *self)
{
	g_object_set_data (G_OBJECT (self), DIALOG_KEY, NULL);
	lrg_editor_view_state_set_dialog (self->view, NULL);
}

static void
on_response (LrgDialog *dialog,
             gint       response,
             gpointer   user_data)
{
	LrgEditorApp *self = LRG_EDITOR_APP (user_data);
	DialogDone    done = (DialogDone) g_object_get_data (G_OBJECT (dialog), "lrg-editor-done");
	g_autoptr(LrgDialog) keep = g_object_ref (dialog);

	/* Clear first: the handler may open the next dialog */
	if (g_object_get_data (G_OBJECT (self), DIALOG_KEY) == dialog)
		dialog_finished (self);
	if (done != NULL)
		done (self, dialog, response);
}

/* Closes whatever dialog is open (quit, dispose, tests). */
void
editor_dialog_close_all (LrgEditorApp *self)
{
	GObject      *current = g_object_get_data (G_OBJECT (self), DIALOG_KEY);
	GCancellable *cancel = g_object_get_data (G_OBJECT (self), "lrg-editor-file-cancel");

	if (cancel != NULL)
		g_cancellable_cancel (cancel);
	if (current != NULL && LRG_IS_DIALOG (current))
	{
		g_signal_handlers_disconnect_by_data (current, self);
		lrg_dialog_close (LRG_DIALOG (current));
	}
	if (self->view != NULL)
		dialog_finished (self);
}

/* Shows @dialog as @id; @done gets the response. */
static void
present (LrgEditorApp *self,
         LrgDialog    *dialog,
         const gchar  *id,
         DialogDone    done)
{
	editor_dialog_close_all (self);
	g_object_set_data (G_OBJECT (dialog), "lrg-editor-done", (gpointer) done);
	g_signal_connect (dialog, "response", G_CALLBACK (on_response), self);
	g_object_set_data_full (G_OBJECT (self), DIALOG_KEY, g_object_ref_sink (dialog), g_object_unref);
	lrg_editor_view_state_set_dialog (self->view, id);
	editor_window_set_menus_enabled (self->window, FALSE);
	lrg_dialog_present (dialog, self->canvas);
}

static LrgTextInput *
add_field (LrgVBox     *box,
           const gchar *label,
           const gchar *value,
           const gchar *name)
{
	g_autoptr(LrgLabel)     l = lrg_label_new (label);
	g_autoptr(LrgTextInput) input = lrg_text_input_new ();

	lrg_text_input_set_text (input, value != NULL ? value : "");
	lrg_widget_set_name (LRG_WIDGET (input), name);
	lrg_widget_set_min_size (LRG_WIDGET (input), 360.0f, 0.0f);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (l));
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (input));
	return input;
}

/* ==========================================================================
 * Unsaved changes
 * ========================================================================== */

static void
confirm_done (LrgEditorApp *self,
              LrgDialog    *dialog,
              gint          response)
{
	/* 0 save, 1 discard, 2 cancel */
	editor_app_confirm_choice (self, response == RESPONSE_PRIMARY ? 0 : response == RESPONSE_SECONDARY ? 1 : 2);
}

void
editor_dialog_confirm (LrgEditorApp *self,
                       const gchar  *message)
{
	g_autoptr(LrgDialog) dialog = lrg_dialog_new ("Unsaved changes", message);

	lrg_dialog_add_button (dialog, "Cancel", RESPONSE_TERTIARY, FALSE);
	lrg_dialog_add_button (dialog, "Don't Save", RESPONSE_SECONDARY, FALSE);
	lrg_dialog_add_button (dialog, "Save", RESPONSE_PRIMARY, TRUE);
	present (self, dialog, "confirm", confirm_done);
}

/* ==========================================================================
 * File dialogs
 * ========================================================================== */

typedef enum
{
	FILE_OPEN_LEVEL,
	FILE_SAVE_LEVEL_AS,
	FILE_OPEN_PROJECT,
	FILE_IMPORT_SCENE,
	FILE_SAVE_PREFAB,
	FILE_INSTANTIATE_PREFAB
} FileMode;

typedef struct
{
	LrgEditorApp  *self;     /* weak */
	FileMode       mode;
	LrgFileDialog *dialog;
	gchar         *node_guid;
} FileRequest;

static void
file_request_free (FileRequest *req)
{
	g_clear_weak_pointer (&req->self);
	g_clear_object (&req->dialog);
	g_free (req->node_guid);
	g_free (req);
}

static void
on_file_chosen (GObject      *source,
                GAsyncResult *result,
                gpointer      user_data)
{
	FileRequest      *req = user_data;
	LrgEditorApp     *self = req->self;
	g_autoptr(GFile)  file = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *path = NULL;

	switch (req->mode)
	{
	case FILE_SAVE_LEVEL_AS:
	case FILE_SAVE_PREFAB:
		file = lrg_file_dialog_save_finish (req->dialog, result, &error);
		break;
	case FILE_OPEN_PROJECT:
		file = lrg_file_dialog_select_folder_finish (req->dialog, result, &error);
		break;
	default:
		file = lrg_file_dialog_open_finish (req->dialog, result, &error);
		break;
	}

	if (self == NULL)
	{
		file_request_free (req);
		return;
	}
	g_object_set_data (G_OBJECT (self), "lrg-editor-file-cancel", NULL);
	if (g_strcmp0 (lrg_editor_view_state_get_dialog (self->view), "file") == 0)
		dialog_finished (self);
	if (file == NULL || (path = g_file_get_path (file)) == NULL)
	{
		/* Cancelled: an action waiting on a Save As is dropped too */
		if (req->mode == FILE_SAVE_LEVEL_AS)
		{
			self->pending = EDITOR_PENDING_NONE;
			self->quit_requested = FALSE;
		}
		file_request_free (req);
		return;
	}

	switch (req->mode)
	{
	case FILE_OPEN_LEVEL:
		editor_app_request (self, EDITOR_PENDING_OPEN_LEVEL, path);
		break;
	case FILE_SAVE_LEVEL_AS:
		if (!g_str_has_suffix (path, ".rlevel"))
		{
			gchar *with = g_strconcat (path, ".rlevel", NULL);

			g_free (path);
			path = with;
		}
		editor_app_save_as (self, path);
		break;
	case FILE_OPEN_PROJECT:
		editor_app_request (self, EDITOR_PENDING_OPEN_PROJECT, path);
		break;
	case FILE_IMPORT_SCENE:
		{
			g_autoptr(LrgLevel) level = lrg_level_import_scene_file (path, &error);

			if (level == NULL)
				editor_app_notify (self, EDITOR_NOTIFY_ERROR, "Import failed: %s", error->message);
			else
			{
				lrg_editor_set_level (self->editor, level);
				lrg_editor_set_path (self->editor, NULL);
				editor_app_notify (self, EDITOR_NOTIFY_SUCCESS, "Imported %s", path);
			}
		}
		break;
	case FILE_SAVE_PREFAB:
		{
			LrgNode *node = lrg_editor_find_node (self->editor, req->node_guid);

			if (!g_str_has_suffix (path, ".rprefab"))
			{
				gchar *with = g_strconcat (path, ".rprefab", NULL);

				g_free (path);
				path = with;
			}
			if (node == NULL)
				editor_app_notify (self, EDITOR_NOTIFY_WARNING, "The node is gone");
			else if (!lrg_editor_save_prefab (self->editor, node, path, &error))
				editor_app_notify (self, EDITOR_NOTIFY_ERROR, "Save prefab failed: %s", error->message);
			else
				editor_app_notify (self, EDITOR_NOTIFY_SUCCESS, "Saved prefab %s", path);
		}
		break;
	case FILE_INSTANTIATE_PREFAB:
		{
			LrgNode *parent = NULL;
			LrgNode *primary = lrg_editor_selection_get_primary (lrg_editor_get_selection (self->editor));

			if (primary != NULL)
				parent = lrg_node_get_parent (primary);
			if (parent != NULL && lrg_node_get_parent (parent) == NULL)
				parent = NULL;
			if (lrg_editor_instantiate_prefab (self->editor, path, parent, &error) == NULL)
				editor_app_notify (self, EDITOR_NOTIFY_ERROR, "Instantiate failed: %s", error->message);
		}
		break;
	default:
		break;
	}
	file_request_free (req);
}

static gchar *
levels_dir (LrgEditorApp *self,
            const gchar  *sub)
{
	if (self->project_dir != NULL)
	{
		g_autofree gchar *dir = g_build_filename (self->project_dir, sub, NULL);

		if (g_file_test (dir, G_FILE_TEST_IS_DIR))
			return g_steal_pointer (&dir);
		return g_strdup (self->project_dir);
	}
	if (lrg_editor_get_path (self->editor) != NULL)
		return g_path_get_dirname (lrg_editor_get_path (self->editor));
	return g_get_current_dir ();
}

static void
run_file_dialog (LrgEditorApp *self,
                 FileMode      mode,
                 const gchar  *title,
                 const gchar  *dir,
                 const gchar  *suffix,
                 const gchar  *name)
{
	FileRequest            *req = g_new0 (FileRequest, 1);
	g_autoptr(GFile)        folder = g_file_new_for_path (dir);
	g_autoptr(GCancellable) cancel = g_cancellable_new ();

	editor_dialog_close_all (self);
	req->mode = mode;
	g_set_weak_pointer (&req->self, self);
	req->dialog = lrg_file_dialog_new ();
	lrg_file_dialog_set_title (req->dialog, title);
	lrg_file_dialog_set_prefer_native (req->dialog, FALSE);
	lrg_file_dialog_set_modal (req->dialog, TRUE);
	lrg_file_dialog_set_initial_folder (req->dialog, folder);
	if (name != NULL)
		lrg_file_dialog_set_initial_name (req->dialog, name);
	if (suffix != NULL)
	{
		g_autoptr(GListStore)    filters = g_list_store_new (LRG_TYPE_FILE_FILTER);
		g_autoptr(LrgFileFilter) filter = lrg_file_filter_new (suffix);

		lrg_file_filter_add_suffix (filter, suffix);
		g_list_store_append (filters, filter);
		lrg_file_dialog_set_filters (req->dialog, G_LIST_MODEL (filters));
	}
	if (mode == FILE_SAVE_PREFAB)
	{
		LrgNode *node = lrg_editor_selection_get_primary (lrg_editor_get_selection (self->editor));

		req->node_guid = g_strdup (node != NULL ? lrg_node_get_guid (node) : NULL);
	}

	g_object_set_data_full (G_OBJECT (self), "lrg-editor-file-cancel", g_object_ref (cancel), g_object_unref);
	lrg_editor_view_state_set_dialog (self->view, "file");
	editor_window_set_menus_enabled (self->window, FALSE);
	switch (mode)
	{
	case FILE_SAVE_LEVEL_AS:
	case FILE_SAVE_PREFAB:
		lrg_file_dialog_save (req->dialog, LRG_WIDGET (self->canvas), cancel, on_file_chosen, req);
		break;
	case FILE_OPEN_PROJECT:
		lrg_file_dialog_select_folder (req->dialog, LRG_WIDGET (self->canvas), cancel, on_file_chosen, req);
		break;
	default:
		lrg_file_dialog_open (req->dialog, LRG_WIDGET (self->canvas), cancel, on_file_chosen, req);
		break;
	}
}

void
editor_dialog_open_level (LrgEditorApp *self)
{
	g_autofree gchar *dir = levels_dir (self, "levels");

	run_file_dialog (self, FILE_OPEN_LEVEL, "Open Level", dir, ".rlevel", NULL);
}

void
editor_dialog_save_level_as (LrgEditorApp *self)
{
	g_autofree gchar *dir = levels_dir (self, "levels");
	const gchar      *path = lrg_editor_get_path (self->editor);
	g_autofree gchar *name = (path != NULL) ? g_path_get_basename (path) : g_strdup ("untitled.rlevel");

	run_file_dialog (self, FILE_SAVE_LEVEL_AS, "Save Level As", dir, ".rlevel", name);
}

void
editor_dialog_open_project (LrgEditorApp *self)
{
	g_autofree gchar *dir = (self->project_dir != NULL) ? g_path_get_dirname (self->project_dir)
	                                                    : g_get_current_dir ();

	run_file_dialog (self, FILE_OPEN_PROJECT, "Open Project", dir, NULL, NULL);
}

void
editor_dialog_import_scene (LrgEditorApp *self)
{
	g_autofree gchar *dir = levels_dir (self, "levels");

	run_file_dialog (self, FILE_IMPORT_SCENE, "Import Scene (Blender YAML)", dir, ".yaml", NULL);
}

void
editor_dialog_save_prefab (LrgEditorApp *self)
{
	g_autofree gchar *dir = levels_dir (self, "prefabs");
	LrgNode          *node = lrg_editor_selection_get_primary (lrg_editor_get_selection (self->editor));
	g_autofree gchar *name = NULL;

	if (node == NULL)
		return;
	name = g_strconcat (lrg_node_get_name (node) != NULL ? lrg_node_get_name (node) : "prefab", ".rprefab", NULL);
	run_file_dialog (self, FILE_SAVE_PREFAB, "Save as Prefab", dir, ".rprefab", name);
}

void
editor_dialog_instantiate_prefab (LrgEditorApp *self)
{
	g_autofree gchar *dir = levels_dir (self, "prefabs");

	run_file_dialog (self, FILE_INSTANTIATE_PREFAB, "Instantiate Prefab", dir, ".rprefab", NULL);
}

/* ==========================================================================
 * New project, new script
 * ========================================================================== */

static void
new_project_done (LrgEditorApp *self,
                  LrgDialog    *dialog,
                  gint          response)
{
	LrgTextInput     *name = g_object_get_data (G_OBJECT (dialog), "name");
	LrgTextInput     *dir = g_object_get_data (G_OBJECT (dialog), "dir");
	g_autoptr(GError) error = NULL;
	const gchar      *folder;

	if (response != RESPONSE_PRIMARY)
		return;
	folder = lrg_text_input_get_text (dir);
	if (folder == NULL || *folder == '\0')
	{
		editor_app_notify (self, EDITOR_NOTIFY_WARNING, "Choose a folder for the project");
		return;
	}
	if (!lrg_editor_app_new_project (self, folder, lrg_text_input_get_text (name), &error))
		editor_app_notify (self, EDITOR_NOTIFY_ERROR, "New project failed: %s", error->message);
}

void
editor_dialog_new_project (LrgEditorApp *self)
{
	g_autoptr(LrgDialog) dialog = lrg_dialog_new ("New Project", NULL);
	g_autoptr(LrgVBox)   box = lrg_vbox_new ();
	g_autofree gchar    *cwd = g_get_current_dir ();
	g_autofree gchar    *dir = g_build_filename (cwd, "new-game", NULL);
	LrgTextInput        *name;
	LrgTextInput        *folder;

	lrg_container_set_spacing (LRG_CONTAINER (box), 6.0f);
	name = add_field (box, "Name", "New Game", "project-name");
	folder = add_field (box, "Folder", dir, "project-folder");
	g_object_set_data (G_OBJECT (dialog), "name", name);
	g_object_set_data (G_OBJECT (dialog), "dir", folder);
	lrg_dialog_set_content (dialog, LRG_WIDGET (box));
	lrg_dialog_add_button (dialog, "Cancel", LRG_DIALOG_RESPONSE_CANCEL, FALSE);
	lrg_dialog_add_button (dialog, "Create", RESPONSE_PRIMARY, TRUE);
	present (self, dialog, "new-project", new_project_done);
}

static const gchar * const script_languages[] = { "Lua", "Python", "JavaScript (GJS)", "C (crispy)", "Elisp" };
static const LrgScriptLanguage script_language_ids[] = {
	LRG_SCRIPT_LANGUAGE_LUA, LRG_SCRIPT_LANGUAGE_PYTHON, LRG_SCRIPT_LANGUAGE_GJS,
	LRG_SCRIPT_LANGUAGE_CRISPY, LRG_SCRIPT_LANGUAGE_ELISP
};

static void
new_script_done (LrgEditorApp *self,
                 LrgDialog    *dialog,
                 gint          response)
{
	LrgTextInput     *name = g_object_get_data (G_OBJECT (dialog), "name");
	LrgComboBox      *lang = g_object_get_data (G_OBJECT (dialog), "lang");
	const gchar      *guid = g_object_get_data (G_OBJECT (dialog), "guid");
	g_autoptr(GError) error = NULL;
	g_autofree gchar *rel = NULL;
	gint              i;
	LrgNode          *node;

	if (response != RESPONSE_PRIMARY)
		return;
	i = CLAMP (lrg_combo_box_get_selected (lang), 0, (gint) G_N_ELEMENTS (script_language_ids) - 1);
	rel = editor_app_create_script (self, lrg_text_input_get_text (name), script_language_ids[i], &error);
	if (rel == NULL)
	{
		editor_app_notify (self, EDITOR_NOTIFY_ERROR, "New script failed: %s", error->message);
		return;
	}

	/* Attach it to the node the dialog was opened for */
	node = (guid != NULL) ? lrg_editor_find_node (self->editor, guid) : NULL;
	if (node != NULL)
	{
		g_autoptr(LrgScriptBinding) binding = lrg_script_binding_new (script_language_ids[i], rel);

		lrg_editor_add_script (self->editor, node, binding);
	}
	editor_app_open_file (self, rel, 1, 1);
}

void
editor_dialog_new_script (LrgEditorApp *self,
                          const gchar  *node_guid)
{
	g_autoptr(LrgDialog)   dialog = lrg_dialog_new ("New Script", NULL);
	g_autoptr(LrgVBox)     box = lrg_vbox_new ();
	g_autoptr(LrgComboBox) lang = lrg_combo_box_new ();
	LrgNode               *node = (node_guid != NULL) ? lrg_editor_find_node (self->editor, node_guid) : NULL;
	LrgTextInput          *name;
	guint                  i;

	lrg_container_set_spacing (LRG_CONTAINER (box), 6.0f);
	name = add_field (box, "Name", node != NULL && lrg_node_get_name (node) != NULL ? lrg_node_get_name (node)
	                                                                                 : "script", "script-name");
	for (i = 0; i < G_N_ELEMENTS (script_languages); i++)
		lrg_combo_box_append (lang, script_languages[i]);
	lrg_combo_box_set_selected (lang, 0);
	lrg_widget_set_name (LRG_WIDGET (lang), "script-language");
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (lang));
	g_object_set_data (G_OBJECT (dialog), "name", name);
	g_object_set_data (G_OBJECT (dialog), "lang", lang);
	g_object_set_data_full (G_OBJECT (dialog), "guid", g_strdup (node_guid), g_free);
	lrg_dialog_set_content (dialog, LRG_WIDGET (box));
	lrg_dialog_add_button (dialog, "Cancel", LRG_DIALOG_RESPONSE_CANCEL, FALSE);
	lrg_dialog_add_button (dialog, "Create", RESPONSE_PRIMARY, TRUE);
	present (self, dialog, "new-script", new_script_done);
}

/* ==========================================================================
 * Recovery, about, shortcuts, project settings
 * ========================================================================== */

static void
recover_done (LrgEditorApp *self,
              LrgDialog    *dialog,
              gint          response)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *path = g_strdup (self->recovery_file);

	if (path == NULL)
		return;
	if (response == RESPONSE_PRIMARY)
	{
		/* Restore: load the autosave but keep the document's real path */
		g_autofree gchar *orig = g_strdup (lrg_editor_get_path (self->editor));

		if (!lrg_editor_load_level (self->editor, path, &error))
			editor_app_notify (self, EDITOR_NOTIFY_ERROR, "Could not restore: %s", error->message);
		else
		{
			lrg_editor_set_path (self->editor, orig);
			lrg_editor_mark_modified (self->editor);
			editor_app_notify (self, EDITOR_NOTIFY_SUCCESS, "Restored the autosave; save to keep it");
		}
	}
	else if (response == RESPONSE_SECONDARY)
	{
		g_autoptr(GFile) file = g_file_new_for_path (path);

		if (!g_file_trash (file, NULL, &error))
			editor_app_notify (self, EDITOR_NOTIFY_WARNING, "Could not move the autosave to the trash: %s",
			                   error->message);
	}
	g_clear_pointer (&self->recovery_file, g_free);
}

void
editor_dialog_recover (LrgEditorApp *self)
{
	g_autofree gchar    *body = g_strdup_printf ("An autosave newer than the level exists:\n%s",
	                                             self->recovery_file);
	g_autoptr(LrgDialog) dialog = lrg_dialog_new ("Recover unsaved changes?", body);

	lrg_dialog_add_button (dialog, "Later", LRG_DIALOG_RESPONSE_CANCEL, FALSE);
	lrg_dialog_add_button (dialog, "Discard", RESPONSE_SECONDARY, FALSE);
	lrg_dialog_add_button (dialog, "Restore", RESPONSE_PRIMARY, TRUE);
	present (self, dialog, "recover", recover_done);
}

void
editor_dialog_about (LrgEditorApp *self)
{
	g_autofree gchar    *body = g_strdup_printf ("libregnum %s\n\nThe level, scene and world editor of the "
	                                             "libregnum engine, built with the engine's own UI toolkit.\n"
	                                             "AGPL-3.0-or-later", LRG_VERSION_STRING);
	g_autoptr(LrgDialog) dialog = lrg_dialog_new ("About the libregnum editor", body);

	lrg_dialog_add_button (dialog, "Close", LRG_DIALOG_RESPONSE_CLOSE, TRUE);
	present (self, dialog, "about", NULL);
}

void
editor_dialog_shortcuts (LrgEditorApp *self)
{
	GString             *text = g_string_new (NULL);
	GList               *ids = lrg_editor_actions_list (self->actions);
	GList               *l;
	g_autoptr(LrgDialog) dialog = NULL;

	/* "Category  Label  Ctrl+S" for every bound action */
	for (l = ids; l != NULL; l = l->next)
	{
		g_auto(GStrv)     accels = lrg_editor_actions_get_accels (self->actions, l->data);
		g_autofree gchar *label = NULL;

		if (accels == NULL || accels[0] == NULL || accels[0][0] == '\0')
			continue;
		label = lrg_editor_accel_to_label (accels[0]);
		g_string_append_printf (text, "%-10s %-34s %s\n", lrg_editor_actions_get_category (self->actions, l->data),
		                        lrg_editor_actions_get_label (self->actions, l->data), label);
	}
	g_list_free (ids);

	dialog = lrg_dialog_new ("Keyboard Shortcuts", text->str);
	g_string_free (text, TRUE);
	lrg_dialog_add_button (dialog, "Close", LRG_DIALOG_RESPONSE_CLOSE, TRUE);
	present (self, dialog, "shortcuts", NULL);
}

/* A read-only summary until the editable form lands with the
 * preferences rework (Wave 4). */
void
editor_dialog_project_settings (LrgEditorApp *self)
{
	g_autofree gchar    *body = NULL;
	g_autoptr(LrgDialog) dialog = NULL;

	if (self->project == NULL)
		return;
	body = g_strdup_printf ("Name: %s\nRoot: %s\nDefault level: %s\nGame module: %s\nRun command: %s\n"
	                        "Scripts: %s\n\nEdit project.ryaml to change these (an editor form comes in Wave 4).",
	                        lrg_project_get_name (self->project), self->project_dir,
	                        lrg_project_get_default_level (self->project) != NULL
	                        ? lrg_project_get_default_level (self->project) : "-",
	                        lrg_project_get_game_output (self->project) != NULL
	                        ? lrg_project_get_game_output (self->project) : "-",
	                        lrg_project_get_run_command (self->project) != NULL
	                        ? lrg_project_get_run_command (self->project) : "(default)",
	                        lrg_project_get_scripts_dir (self->project));
	dialog = lrg_dialog_new ("Project Settings", body);
	lrg_dialog_add_button (dialog, "Close", LRG_DIALOG_RESPONSE_CLOSE, TRUE);
	present (self, dialog, "project-settings", NULL);
}
