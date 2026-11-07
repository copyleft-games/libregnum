/* lrg-editor-dialogs.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The editor's modal dialogs on LrgDialog and LrgFileDialog: unsaved
 * changes (Save / Don't Save / Cancel), open and save level (.rlevel
 * filter), open project (folder), import scene, save and instantiate
 * prefab, new project, new script (Lua, Python, GJS, crispy C and Emacs
 * Lisp templates; the file opens where the script-editor setting says),
 * crash recovery, about (version, license, links), the live searchable
 * shortcut table (LrgEditorPreferences' keymap, read-only) and the
 * editable project settings (name, default level, game module, run
 * command, scripts folder and the asset folder list, written to
 * project.ryaml through LrgProject).
 *
 * One dialog is open at a time. Its id goes into
 * LrgEditorViewState:dialog while it shows (ui.state "dialog" reports the
 * same strings as 0.8.0: "confirm", "file", "new-project", "new-script",
 * "recover", "about", "shortcuts", "project-settings", plus
 * "preferences" from lrg-editor-preferences.c), which also takes the
 * "editor" action group off the canvas so shortcuts do not fire behind
 * it. Escape answers LRG_DIALOG_RESPONSE_CANCEL. Other files that show
 * dialogs (preferences, the code panel's prompts) follow the same
 * protocol: the dialog is object data "lrg-editor-dialog" on the app,
 * so editor_dialog_close_all() closes them too.
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
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-button.h"
#include "lrg-editor-code-panel.h"
#include "lrg-editor-preferences.h"
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
	FILE_INSTANTIATE_PREFAB,
	FILE_SELECT_FOLDER       /* generic: editor_dialog_select_folder() */
} FileMode;

typedef struct
{
	LrgEditorApp     *self;     /* weak */
	FileMode          mode;
	LrgFileDialog    *dialog;
	gchar            *node_guid;

	/* FILE_SELECT_FOLDER: called with the folder (NULL when cancelled) */
	EditorFolderFunc  folder_func;
	gpointer          folder_data;
	GDestroyNotify    folder_destroy;
} FileRequest;

static void
file_request_free (FileRequest *req)
{
	g_clear_weak_pointer (&req->self);
	g_clear_object (&req->dialog);
	g_free (req->node_guid);
	if (req->folder_destroy != NULL)
		req->folder_destroy (req->folder_data);
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
	case FILE_SELECT_FOLDER:
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
	if (req->mode == FILE_SELECT_FOLDER)
	{
		/* The caller handles both outcomes (NULL: cancelled) */
		if (file != NULL)
			path = g_file_get_path (file);
		if (req->folder_func != NULL)
			req->folder_func (self, path, req->folder_data);
		file_request_free (req);
		return;
	}
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
run_file_dialog_full (LrgEditorApp     *self,
                      FileMode          mode,
                      const gchar      *title,
                      const gchar      *dir,
                      const gchar      *suffix,
                      const gchar      *name,
                      EditorFolderFunc  folder_func,
                      gpointer          folder_data,
                      GDestroyNotify    folder_destroy)
{
	FileRequest            *req = g_new0 (FileRequest, 1);
	g_autoptr(GFile)        folder = g_file_new_for_path (dir);
	g_autoptr(GCancellable) cancel = g_cancellable_new ();

	editor_dialog_close_all (self);
	req->mode = mode;
	req->folder_func = folder_func;
	req->folder_data = folder_data;
	req->folder_destroy = folder_destroy;
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
	case FILE_SELECT_FOLDER:
		lrg_file_dialog_select_folder (req->dialog, LRG_WIDGET (self->canvas), cancel, on_file_chosen, req);
		break;
	default:
		lrg_file_dialog_open (req->dialog, LRG_WIDGET (self->canvas), cancel, on_file_chosen, req);
		break;
	}
}

static void
run_file_dialog (LrgEditorApp *self,
                 FileMode      mode,
                 const gchar  *title,
                 const gchar  *dir,
                 const gchar  *suffix,
                 const gchar  *name)
{
	run_file_dialog_full (self, mode, title, dir, suffix, name, NULL, NULL, NULL);
}

/*
 * editor_dialog_select_folder:
 *
 * A folder chooser (the editor's LrgFileDialog, like Open Project) for
 * callers outside this file, e.g. the Sources panel's "Export to...".
 * @func runs once with the chosen folder, or %NULL when cancelled;
 * @destroy frees @data afterwards.
 */
void
editor_dialog_select_folder (LrgEditorApp     *self,
                             const gchar      *title,
                             const gchar      *dir,
                             EditorFolderFunc  func,
                             gpointer          data,
                             GDestroyNotify    destroy)
{
	g_autofree gchar *start = (dir != NULL) ? g_strdup (dir)
	                         : (self->project_dir != NULL) ? g_strdup (self->project_dir) : g_get_current_dir ();

	run_file_dialog_full (self, FILE_SELECT_FOLDER, title, start, NULL, NULL, func, data, destroy);
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
	g_autofree gchar *folder = NULL;
	const gchar      *title;

	if (response != RESPONSE_PRIMARY)
		return;
	folder = g_strstrip (g_strdup (lrg_text_input_get_text (dir) != NULL ? lrg_text_input_get_text (dir) : ""));
	title = lrg_text_input_get_text (name);
	if (*folder == '\0')
	{
		editor_app_notify (self, EDITOR_NOTIFY_WARNING, "Choose a folder for the project");
		return;
	}
	if (lrg_editor_is_dirty (self->editor))
	{
		editor_app_notify (self, EDITOR_NOTIFY_WARNING, "Save or discard the open level first");
		return;
	}
	if (!lrg_editor_app_new_project (self, folder, title != NULL && *title != '\0' ? title : "New Game", &error))
		editor_app_notify (self, EDITOR_NOTIFY_ERROR, "Could not create the project: %s", error->message);
}

void
editor_dialog_new_project (LrgEditorApp *self)
{
	g_autoptr(LrgDialog) dialog = lrg_dialog_new ("New Project",
	                                              "Creates the folder with levels/, scripts/, assets/ and "
	                                              "prefabs/, a project.ryaml manifest and a starter level.");
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

/* The languages of the New Script dialog, in 0.8.0 order; the
 * templates themselves come from editor_app_create_script(). */
static const gchar * const script_languages[] = { "Lua", "Python", "JavaScript (GJS)", "C (crispy)", "Emacs Lisp" };
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
	if (lrg_text_input_get_text (name) == NULL || *lrg_text_input_get_text (name) == '\0')
	{
		editor_app_notify (self, EDITOR_NOTIFY_WARNING, "Give the script a name");
		return;
	}
	i = CLAMP (lrg_combo_box_get_selected (lang), 0, (gint) G_N_ELEMENTS (script_language_ids) - 1);
	rel = editor_app_create_script (self, lrg_text_input_get_text (name), script_language_ids[i], &error);
	if (rel == NULL)
	{
		editor_app_notify (self, EDITOR_NOTIFY_ERROR, "Could not create the script: %s", error->message);
		return;
	}

	/* Attach it to the node the dialog was opened for */
	node = (guid != NULL) ? lrg_editor_find_node (self->editor, guid) : NULL;
	if (node != NULL)
	{
		g_autoptr(LrgScriptBinding) binding = lrg_script_binding_new (script_language_ids[i], rel);

		lrg_editor_add_script (self->editor, node, binding);
	}

	/* Code panel or external editor, as script-editor says */
	lrg_editor_code_panel_open_script (self, rel, 1);
}

/* Enter in the name field creates the script (the field eats Enter) */
static void
on_field_submitted (LrgTextInput *input,
                    gpointer      user_data)
{
	lrg_dialog_respond (LRG_DIALOG (user_data), RESPONSE_PRIMARY);
}

void
editor_dialog_new_script (LrgEditorApp *self,
                          const gchar  *node_guid)
{
	LrgNode               *node = (node_guid != NULL) ? lrg_editor_find_node (self->editor, node_guid) : NULL;
	g_autofree gchar      *body = (node != NULL) ? g_strdup_printf ("Attach to: %s", lrg_node_get_name (node))
	                                             : g_strdup ("Creates the file in the project's scripts folder.");
	g_autoptr(LrgDialog)   dialog = lrg_dialog_new ("New Script", body);
	g_autoptr(LrgVBox)     box = lrg_vbox_new ();
	g_autoptr(LrgComboBox) lang = lrg_combo_box_new ();
	g_autoptr(LrgLabel)    lang_label = lrg_label_new ("Language");
	LrgTextInput          *name;
	guint                  i;

	lrg_container_set_spacing (LRG_CONTAINER (box), 6.0f);
	name = add_field (box, "Name", node != NULL && lrg_node_get_name (node) != NULL ? lrg_node_get_name (node)
	                                                                                 : "script", "script-name");
	g_signal_connect_object (name, "submitted", G_CALLBACK (on_field_submitted), dialog, 0);
	for (i = 0; i < G_N_ELEMENTS (script_languages); i++)
		lrg_combo_box_append (lang, script_languages[i]);
	lrg_combo_box_set_selected (lang, 0);
	lrg_widget_set_name (LRG_WIDGET (lang), "script-language");
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (lang_label));
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
 * Recovery, about, shortcuts
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
	if (response == LRG_DIALOG_RESPONSE_CANCEL)
		return;   /* Later: keep the autosave and the offer */
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
	g_autofree gchar    *body = g_strdup_printf ("An autosave newer than the level on disk was found; the editor "
	                                             "may have closed without saving.\n%s", self->recovery_file);
	g_autoptr(LrgDialog) dialog = lrg_dialog_new ("Recover Autosave", body);

	lrg_dialog_add_button (dialog, "Later", LRG_DIALOG_RESPONSE_CANCEL, FALSE);
	lrg_dialog_add_button (dialog, "Discard Autosave", RESPONSE_SECONDARY, FALSE);
	lrg_dialog_add_button (dialog, "Restore", RESPONSE_PRIMARY, TRUE);
	present (self, dialog, "recover", recover_done);
}

/* About: the docs and source links open with the desktop's handler */
static void
open_uri (LrgEditorApp *self,
          const gchar  *uri)
{
	g_autoptr(GError) error = NULL;

	if (!g_app_info_launch_default_for_uri (uri, NULL, &error))
		editor_app_notify (self, EDITOR_NOTIFY_WARNING, "Could not open %s: %s", uri, error->message);
}

static void
on_about_link (LrgButton *button,
               gpointer   user_data)
{
	LrgEditorApp *self = LRG_EDITOR_APP (user_data);
	const gchar  *what = g_object_get_data (G_OBJECT (button), "link");

	if (g_strcmp0 (what, "docs") == 0)
		lrg_editor_actions_activate (self->actions, "help.docs");
	else
		open_uri (self, what);
}

static void
about_link (LrgEditorApp *self,
            LrgHBox      *row,
            const gchar  *label,
            const gchar  *name,
            const gchar  *link)
{
	g_autoptr(LrgButton) button = lrg_button_new (label);

	lrg_widget_set_name (LRG_WIDGET (button), name);
	g_object_set_data_full (G_OBJECT (button), "link", g_strdup (link), g_free);
	g_signal_connect_object (button, "clicked", G_CALLBACK (on_about_link), self, 0);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (button));
}

void
editor_dialog_about (LrgEditorApp *self)
{
	const gchar         *sock = lrg_editor_server_get_socket_path (self->server);
	g_autofree gchar    *body = g_strdup_printf ("Version %s\n\nThe level, scene and world editor of the "
	                                             "libregnum engine, built with the engine's own retained UI "
	                                             "toolkit.\n\nCopyright 2025-2026 Zach Podbielniak\nFree software "
	                                             "under the GNU Affero General Public License, version 3 or later "
	                                             "(AGPL-3.0-or-later).%s%s", LRG_VERSION_STRING,
	                                             sock != NULL ? "\n\nIPC socket: " : "", sock != NULL ? sock : "");
	g_autoptr(LrgDialog) dialog = lrg_dialog_new ("About the libregnum editor", body);
	g_autoptr(LrgHBox)   links = lrg_hbox_new ();

	lrg_container_set_spacing (LRG_CONTAINER (links), 8.0f);
	about_link (self, links, "Documentation", "about-docs", "docs");
	about_link (self, links, "Source code", "about-source", "https://gitlab.com/zachpodbielniak/libregnum");
	about_link (self, links, "License", "about-license", "https://www.gnu.org/licenses/agpl-3.0.html");
	lrg_dialog_set_content (dialog, LRG_WIDGET (links));
	lrg_dialog_add_button (dialog, "Close", LRG_DIALOG_RESPONSE_CLOSE, TRUE);
	present (self, dialog, "about", NULL);
}

static void
shortcuts_done (LrgEditorApp *self,
                LrgDialog    *dialog,
                gint          response)
{
	/* Close this one first: a popup opened while it is still up would be
	 * stacked above it and closed along with it */
	if (response != RESPONSE_PRIMARY)
		return;
	lrg_dialog_close (dialog);
	lrg_editor_preferences_present (self, "keybindings");
}

void
editor_dialog_shortcuts (LrgEditorApp *self)
{
	g_autoptr(LrgDialog) dialog = lrg_dialog_new ("Keyboard Shortcuts", NULL);
	g_autoptr(LrgWidget) keymap = lrg_editor_preferences_create_keymap (self, FALSE);

	/* A live, searchable table (it follows rebinding) */
	lrg_widget_set_min_size (keymap, MIN (720.0f, MAX (self->width - 160.0f, 400.0f)),
	                         MIN (560.0f, MAX (self->height - 260.0f, 260.0f)));
	lrg_dialog_set_content (dialog, keymap);
	lrg_dialog_add_button (dialog, "Edit Keybindings...", RESPONSE_PRIMARY, FALSE);
	lrg_dialog_add_button (dialog, "Close", LRG_DIALOG_RESPONSE_CLOSE, TRUE);
	present (self, dialog, "shortcuts", shortcuts_done);
}

/* ==========================================================================
 * Project settings
 * ========================================================================== */

/* The asset folder list is edited in a GPtrArray on the dialog and only
 * written to the project on Save. */
static void project_dirs_rebuild (LrgDialog *dialog);

static void
on_dir_remove (LrgButton *button,
               gpointer   user_data)
{
	LrgDialog *dialog = LRG_DIALOG (user_data);
	GPtrArray *dirs = g_object_get_data (G_OBJECT (dialog), "dirs");
	guint      index = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (button), "index"));

	if (index < dirs->len)
		g_ptr_array_remove_index (dirs, index);
	project_dirs_rebuild (dialog);
}

static void
on_dir_add (LrgButton *button,
            gpointer   user_data)
{
	LrgDialog        *dialog = LRG_DIALOG (user_data);
	GPtrArray        *dirs = g_object_get_data (G_OBJECT (dialog), "dirs");
	LrgTextInput     *input = g_object_get_data (G_OBJECT (dialog), "dir-input");
	g_autofree gchar *dir = g_strstrip (g_strdup (lrg_text_input_get_text (input) != NULL
	                                              ? lrg_text_input_get_text (input) : ""));
	guint             i;

	if (*dir == '\0')
		return;
	for (i = 0; i < dirs->len; i++)
		if (g_strcmp0 (g_ptr_array_index (dirs, i), dir) == 0)
			return;
	g_ptr_array_add (dirs, g_steal_pointer (&dir));
	lrg_text_input_set_text (input, "");
	project_dirs_rebuild (dialog);
}

static void
on_dir_submitted (LrgTextInput *input,
                  gpointer      user_data)
{
	on_dir_add (NULL, user_data);
}

static void
project_dirs_rebuild (LrgDialog *dialog)
{
	LrgVBox   *list = g_object_get_data (G_OBJECT (dialog), "dir-list");
	GPtrArray *dirs = g_object_get_data (G_OBJECT (dialog), "dirs");
	guint      i;

	lrg_container_remove_all (LRG_CONTAINER (list));
	for (i = 0; i < dirs->len; i++)
	{
		g_autoptr(LrgHBox)   row = lrg_hbox_new ();
		g_autoptr(LrgLabel)  label = lrg_label_new (g_ptr_array_index (dirs, i));
		g_autoptr(LrgButton) remove = lrg_button_new ("Remove");
		g_autofree gchar    *name = g_strdup_printf ("project-dir-remove:%u", i);

		lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
		lrg_widget_set_fill_weight (LRG_WIDGET (label), 1.0f);
		lrg_widget_set_valign (LRG_WIDGET (label), LRG_ALIGN_CENTER);
		lrg_widget_set_name (LRG_WIDGET (remove), name);
		g_object_set_data (G_OBJECT (remove), "index", GUINT_TO_POINTER (i));
		g_signal_connect_object (remove, "clicked", G_CALLBACK (on_dir_remove), dialog, 0);
		lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (label));
		lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (remove));
		lrg_container_add_child (LRG_CONTAINER (list), LRG_WIDGET (row));
	}
	if (dirs->len == 0)
	{
		g_autoptr(LrgLabel) none = lrg_label_new ("No asset folders: the whole project is scanned");

		lrg_widget_add_class (LRG_WIDGET (none), "dim-label");
		lrg_container_add_child (LRG_CONTAINER (list), LRG_WIDGET (none));
	}
	lrg_widget_queue_layout (LRG_WIDGET (dialog));
}

static void
on_use_current_level (LrgButton *button,
                      gpointer   user_data)
{
	LrgDialog        *dialog = LRG_DIALOG (user_data);
	LrgEditorApp     *self = g_object_get_data (G_OBJECT (dialog), "app");
	LrgTextInput     *level = g_object_get_data (G_OBJECT (dialog), "level");
	g_autofree gchar *rel = NULL;

	if (self == NULL || self->project == NULL || lrg_editor_get_path (self->editor) == NULL)
		return;
	rel = lrg_project_relativize (self->project, lrg_editor_get_path (self->editor));
	lrg_text_input_set_text (level, rel);
}

/* "" -> NULL for the optional project fields */
static const gchar *
field_or_null (LrgDialog   *dialog,
               const gchar *key)
{
	LrgTextInput *input = g_object_get_data (G_OBJECT (dialog), key);
	const gchar  *text = lrg_text_input_get_text (input);

	return (text != NULL && *text != '\0') ? text : NULL;
}

static void
project_settings_done (LrgEditorApp *self,
                       LrgDialog    *dialog,
                       gint          response)
{
	GPtrArray        *dirs = g_object_get_data (G_OBJECT (dialog), "dirs");
	g_autoptr(GError) error = NULL;
	guint             i;

	if (response != RESPONSE_PRIMARY || self->project == NULL)
		return;
	lrg_project_set_name (self->project, field_or_null (dialog, "name") != NULL ? field_or_null (dialog, "name")
	                                                                           : "Game");
	lrg_project_set_default_level (self->project, field_or_null (dialog, "level"));
	lrg_project_set_game_output (self->project, field_or_null (dialog, "module"));
	lrg_project_set_run_command (self->project, field_or_null (dialog, "run"));
	lrg_project_set_scripts_dir (self->project, field_or_null (dialog, "scripts"));
	lrg_project_clear_asset_dirs (self->project);
	for (i = 0; i < dirs->len; i++)
		lrg_project_add_asset_dir (self->project, g_ptr_array_index (dirs, i));
	if (!lrg_project_save (self->project, &error))
		editor_app_notify (self, EDITOR_NOTIFY_ERROR, "Could not save the project: %s", error->message);
	else
		editor_app_notify (self, EDITOR_NOTIFY_SUCCESS, "Saved project settings");
}

void
editor_dialog_project_settings (LrgEditorApp *self)
{
	g_autofree gchar     *body = NULL;
	g_autoptr(LrgDialog)  dialog = NULL;
	g_autoptr(LrgVBox)    box = lrg_vbox_new ();
	g_autoptr(LrgHBox)    level_row = lrg_hbox_new ();
	g_autoptr(LrgVBox)    level_box = lrg_vbox_new ();
	g_autoptr(LrgButton)  use_current = lrg_button_new ("Use current");
	g_autoptr(LrgLabel)   dirs_label = lrg_label_new ("Asset folders");
	g_autoptr(LrgVBox)    dir_list = lrg_vbox_new ();
	g_autoptr(LrgHBox)    add_row = lrg_hbox_new ();
	g_autoptr(LrgTextInput) dir_input = lrg_text_input_new_with_placeholder ("assets/audio");
	g_autoptr(LrgButton)  add = lrg_button_new ("Add");
	GPtrArray            *dirs = g_ptr_array_new_with_free_func (g_free);
	GPtrArray            *pdirs;
	LrgTextInput         *input;
	guint                 i;

	if (self->project == NULL)
	{
		g_ptr_array_unref (dirs);
		return;
	}
	body = g_strdup ("Saved to project.ryaml in the project folder.");
	dialog = lrg_dialog_new ("Project Settings", body);
	g_object_set_data (G_OBJECT (dialog), "app", self);
	lrg_container_set_spacing (LRG_CONTAINER (box), 6.0f);

	input = add_field (box, "Name", lrg_project_get_name (self->project), "project-name");
	g_object_set_data (G_OBJECT (dialog), "name", input);

	/* Default level with "Use current" */
	input = add_field (level_box, "Default level", lrg_project_get_default_level (self->project),
	                   "project-default-level");
	g_object_set_data (G_OBJECT (dialog), "level", input);
	lrg_widget_set_fill_weight (LRG_WIDGET (level_box), 1.0f);
	lrg_widget_set_name (LRG_WIDGET (use_current), "project-use-current");
	lrg_widget_set_valign (LRG_WIDGET (use_current), LRG_ALIGN_END);
	lrg_widget_set_enabled (LRG_WIDGET (use_current), lrg_editor_get_path (self->editor) != NULL);
	g_signal_connect_object (use_current, "clicked", G_CALLBACK (on_use_current_level), dialog, 0);
	lrg_container_set_spacing (LRG_CONTAINER (level_row), 6.0f);
	lrg_container_add_child (LRG_CONTAINER (level_row), LRG_WIDGET (level_box));
	lrg_container_add_child (LRG_CONTAINER (level_row), LRG_WIDGET (use_current));
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (level_row));

	input = add_field (box, "Game module", lrg_project_get_game_output (self->project), "project-game-module");
	lrg_text_input_set_placeholder (input, "build/libmygame.so");
	g_object_set_data (G_OBJECT (dialog), "module", input);
	input = add_field (box, "Run command ({project} {level} {module}; empty uses the default)",
	                   lrg_project_get_run_command (self->project), "project-run-command");
	lrg_text_input_set_placeholder (input, "lrgldr {module} -- --level {level}");
	g_object_set_data (G_OBJECT (dialog), "run", input);
	input = add_field (box, "Scripts folder", lrg_project_get_scripts_dir (self->project), "project-scripts-dir");
	g_object_set_data (G_OBJECT (dialog), "scripts", input);

	/* Asset folders: a list with Remove buttons and an Add row */
	pdirs = lrg_project_get_asset_dirs (self->project);
	for (i = 0; pdirs != NULL && i < pdirs->len; i++)
		g_ptr_array_add (dirs, g_strdup (g_ptr_array_index (pdirs, i)));
	g_object_set_data_full (G_OBJECT (dialog), "dirs", dirs, (GDestroyNotify) g_ptr_array_unref);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (dirs_label));
	lrg_container_set_spacing (LRG_CONTAINER (dir_list), 2.0f);
	lrg_widget_set_name (LRG_WIDGET (dir_list), "project-dirs");
	g_object_set_data (G_OBJECT (dialog), "dir-list", dir_list);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (dir_list));
	lrg_widget_set_name (LRG_WIDGET (dir_input), "project-dir-input");
	lrg_widget_set_fill_weight (LRG_WIDGET (dir_input), 1.0f);
	g_signal_connect_object (dir_input, "submitted", G_CALLBACK (on_dir_submitted), dialog, 0);
	g_object_set_data (G_OBJECT (dialog), "dir-input", dir_input);
	lrg_widget_set_name (LRG_WIDGET (add), "project-dir-add");
	g_signal_connect_object (add, "clicked", G_CALLBACK (on_dir_add), dialog, 0);
	lrg_container_set_spacing (LRG_CONTAINER (add_row), 6.0f);
	lrg_container_add_child (LRG_CONTAINER (add_row), LRG_WIDGET (dir_input));
	lrg_container_add_child (LRG_CONTAINER (add_row), LRG_WIDGET (add));
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (add_row));
	project_dirs_rebuild (dialog);

	lrg_dialog_set_content (dialog, LRG_WIDGET (box));
	lrg_dialog_add_button (dialog, "Cancel", LRG_DIALOG_RESPONSE_CANCEL, FALSE);
	lrg_dialog_add_button (dialog, "Save", RESPONSE_PRIMARY, TRUE);
	present (self, dialog, "project-settings", project_settings_done);
}
