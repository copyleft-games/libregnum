/* lrg-editor-dialogs.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Modal dialogs, the command palette and quick open.
 *
 * One modal is open at a time (self->dialog). Dialogs are immediate-mode
 * popups: editor_dialog_open() prepares the dialog's state and the next
 * editor_dialogs_draw() opens the popup; when the popup is closed by any
 * means (buttons, Escape, the close box) the dialog state goes back to
 * EDITOR_DIALOG_NONE.
 *
 * Preferences are edited live: every change is applied at once and the
 * file is written when the dialog closes, so nothing is lost and nothing
 * needs an "Apply" button.
 */

#include "lrg-editor-app-private.h"
#include "../lrg-asset-database.h"
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

#define PALETTE_WIDTH      (640.0f)
#define PALETTE_ROWS       (14)
#define QUICK_OPEN_LIMIT   (20000)

static const gchar *dialog_titles[] = {
	NULL,
	"Preferences###dlg-prefs",
	"Project Settings###dlg-project",
	"###dlg-file",
	"Unsaved Changes###dlg-confirm",
	"New Project###dlg-new-project",
	"New Script###dlg-new-script",
	"About###dlg-about",
	"Keyboard Shortcuts###dlg-shortcuts",
	"Recover Autosave###dlg-recover"
};

static const gchar *script_languages[] = { "Lua", "Python", "JavaScript (GJS)", "C (crispy)", "Emacs Lisp" };
static const LrgScriptLanguage script_language_values[] = {
	LRG_SCRIPT_LANGUAGE_LUA, LRG_SCRIPT_LANGUAGE_PYTHON, LRG_SCRIPT_LANGUAGE_GJS,
	LRG_SCRIPT_LANGUAGE_CRISPY, LRG_SCRIPT_LANGUAGE_ELISP
};

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static void
close_dialog (LrgEditorApp *self)
{
	if (self->dialog == EDITOR_DIALOG_PREFERENCES)
	{
		g_autoptr(GError) error = NULL;

		if (!lrg_editor_app_save_settings (self, &error))
			editor_app_notify (self, 3, "Could not save preferences: %s", error->message);
	}
	g_clear_pointer (&self->capture_action, g_free);
	self->dialog = EDITOR_DIALOG_NONE;
	lrg_gui_close_current_popup (self->gui);
}

/* Opens/positions the popup for the current dialog. Returns TRUE while it
 * is showing; the caller then draws the body and calls lrg_gui_end_popup(). */
static gboolean
begin_dialog (LrgEditorApp *self,
              const gchar  *title,
              gfloat        width,
              gfloat        height)
{
	gboolean open = TRUE;

	if (!lrg_gui_is_popup_open (self->gui, title))
	{
		if (!self->dialog_just_opened)
		{
			/* Closed from elsewhere (e.g. a click outside). */
			g_clear_pointer (&self->capture_action, g_free);
			self->dialog = EDITOR_DIALOG_NONE;
			return FALSE;
		}
		lrg_gui_open_popup (self->gui, title);
	}

	width = MIN (width, self->width - 40.0f);
	if (height > 0.0f)
		height = MIN (height, self->height - 80.0f);
	lrg_gui_set_next_popup_size (self->gui, width, height);
	lrg_gui_set_next_popup_pos (self->gui, roundf ((self->width - width) * 0.5f),
	                            roundf (MAX (40.0f, (self->height - (height > 0.0f ? height : 300.0f)) * 0.35f)));
	if (!lrg_gui_begin_popup_modal (self->gui, title, &open))
		return FALSE;
	self->dialog_just_opened = FALSE;

	if (!open)
	{
		/* The close box or Escape. */
		lrg_gui_end_popup (self->gui);
		if (self->dialog == EDITOR_DIALOG_CONFIRM)
			editor_app_confirm_choice (self, 2);
		if (self->dialog == EDITOR_DIALOG_PREFERENCES)
		{
			g_autoptr(GError) error = NULL;

			if (!lrg_editor_app_save_settings (self, &error))
				editor_app_notify (self, 3, "Could not save preferences: %s", error->message);
		}
		g_clear_pointer (&self->capture_action, g_free);
		self->dialog = EDITOR_DIALOG_NONE;
		return FALSE;
	}
	return TRUE;
}

/* Moves the cursor so that a row of buttons with @labels ends at the
 * right edge of the region. */
static void
buttons_right (LrgEditorApp        *self,
               const gchar * const *labels)
{
	LrgGuiStyle *style = lrg_gui_context_get_style (self->gui);
	gfloat       total = 0.0f, avail, x, y;
	guint        i;

	for (i = 0; labels[i] != NULL; i++)
		total += lrg_gui_calc_text_width (self->gui, labels[i]) + style->frame_padding_x * 2.0f +
		         style->item_spacing_x;
	lrg_gui_get_content_avail (self->gui, &avail, NULL);
	lrg_gui_get_cursor (self->gui, &x, &y);
	lrg_gui_set_cursor (self->gui, x + MAX (0.0f, avail - total), y);
}

/* Settings writes go through here so dock edits are not reverted when
 * editor_app_apply_settings() reloads the stored layout. */
static void
apply_prefs (LrgEditorApp *self)
{
	g_autofree gchar *layout = lrg_gui_dock_to_string (self->dock);

	g_object_set (self->settings, "layout", layout, NULL);
	editor_app_apply_settings (self);
}

static void
set_string_pref (LrgEditorApp *self,
                 const gchar  *prop,
                 GString      *buf)
{
	g_object_set (self->settings, prop, buf->len > 0 ? buf->str : NULL, NULL);
	apply_prefs (self);
}

static void
load_string_pref (LrgEditorApp *self,
                  const gchar  *prop,
                  GString      *buf)
{
	g_autofree gchar *value = NULL;

	g_object_get (self->settings, prop, &value, NULL);
	g_string_assign (buf, value != NULL ? value : "");
}

static gboolean
bool_pref (LrgEditorApp *self,
           const gchar  *label,
           const gchar  *prop)
{
	gboolean          value = FALSE;
	g_autofree gchar *id = g_strdup_printf ("##%s", prop);

	g_object_get (self->settings, prop, &value, NULL);
	lrg_gui_property_label (self->gui, label);
	if (lrg_gui_checkbox (self->gui, id, &value))
	{
		g_object_set (self->settings, prop, value, NULL);
		apply_prefs (self);
		return TRUE;
	}
	return FALSE;
}

static void
double_pref (LrgEditorApp *self,
             const gchar  *label,
             const gchar  *prop,
             gfloat        speed,
             gfloat        min,
             gfloat        max,
             const gchar  *format)
{
	gdouble           value = 0.0;
	gfloat            v;
	g_autofree gchar *id = g_strdup_printf ("##%s", prop);

	g_object_get (self->settings, prop, &value, NULL);
	v = (gfloat) value;
	lrg_gui_property_label (self->gui, label);
	if (lrg_gui_drag_float (self->gui, id, &v, speed, min, max, format))
	{
		g_object_set (self->settings, prop, (gdouble) v, NULL);
		apply_prefs (self);
	}
}

static void
uint_pref (LrgEditorApp *self,
           const gchar  *label,
           const gchar  *prop,
           gint          min,
           gint          max)
{
	guint             value = 0;
	gint              v;
	g_autofree gchar *id = g_strdup_printf ("##%s", prop);

	g_object_get (self->settings, prop, &value, NULL);
	v = (gint) value;
	lrg_gui_property_label (self->gui, label);
	if (lrg_gui_drag_int (self->gui, id, &v, 0.2f, min, max))
	{
		g_object_set (self->settings, prop, (guint) MAX (v, 0), NULL);
		apply_prefs (self);
	}
}

/* A combo over a fixed list of string values. */
static void
choice_pref (LrgEditorApp        *self,
             const gchar         *label,
             const gchar         *prop,
             const gchar * const *values,
             const gchar * const *labels,
             gint                 n)
{
	g_autofree gchar *value = NULL;
	g_autofree gchar *id = g_strdup_printf ("##%s", prop);
	gint              current = 0, i;

	g_object_get (self->settings, prop, &value, NULL);
	for (i = 0; i < n; i++)
		if (g_strcmp0 (values[i], value) == 0)
			current = i;
	lrg_gui_property_label (self->gui, label);
	if (lrg_gui_combo (self->gui, id, &current, labels, n))
	{
		g_object_set (self->settings, prop, values[current], NULL);
		apply_prefs (self);
	}
}

/* ==========================================================================
 * Opening
 * ========================================================================== */

void
editor_dialog_open (LrgEditorApp *self,
                    EditorDialog  dialog)
{
	/* A second request replaces the first (the old popup closes). */
	if (self->dialog != EDITOR_DIALOG_NONE && self->dialog != dialog)
		lrg_gui_close_all_popups (self->gui);

	self->dialog = dialog;
	self->dialog_just_opened = TRUE;
	self->dialog_choice = 0;
	self->palette_open = FALSE;
	g_string_truncate (self->dialog_text1, 0);
	g_string_truncate (self->dialog_text2, 0);
	g_string_truncate (self->dialog_text3, 0);
	g_string_truncate (self->dialog_text4, 0);
	g_string_truncate (self->dialog_text5, 0);
	g_clear_pointer (&self->capture_action, g_free);

	switch (dialog)
	{
	case EDITOR_DIALOG_PREFERENCES:
		load_string_pref (self, "external-editor-command", self->dialog_text1);
		load_string_pref (self, "terminal-command", self->dialog_text2);
		load_string_pref (self, "emacs-server-name", self->dialog_text3);
		load_string_pref (self, "font-path", self->dialog_text4);
		load_string_pref (self, "server-socket", self->dialog_text5);
		break;

	case EDITOR_DIALOG_PROJECT_SETTINGS:
		if (self->project == NULL)
		{
			self->dialog = EDITOR_DIALOG_NONE;
			editor_app_notify (self, 2, "No project is open");
			return;
		}
		g_string_assign (self->dialog_text1, lrg_project_get_name (self->project));
		g_string_assign (self->dialog_text2, lrg_project_get_default_level (self->project) != NULL
		                                     ? lrg_project_get_default_level (self->project) : "");
		g_string_assign (self->dialog_text3, lrg_project_get_run_command (self->project) != NULL
		                                     ? lrg_project_get_run_command (self->project) : "");
		g_string_assign (self->dialog_text4, lrg_project_get_game_output (self->project) != NULL
		                                     ? lrg_project_get_game_output (self->project) : "");
		g_string_assign (self->dialog_text5, lrg_project_get_scripts_dir (self->project));
		break;

	case EDITOR_DIALOG_NEW_PROJECT:
		{
			g_autofree gchar *cwd = g_get_current_dir ();
			g_autofree gchar *dir = g_build_filename (cwd, "new-game", NULL);

			g_string_assign (self->dialog_text1, dir);
			g_string_assign (self->dialog_text2, "New Game");
		}
		break;

	case EDITOR_DIALOG_NEW_SCRIPT:
		{
			LrgNode *target = (self->file_target_guid != NULL)
			                  ? lrg_node_find_by_guid (lrg_level_get_root (lrg_editor_get_level (self->editor)),
			                                           self->file_target_guid)
			                  : NULL;
			const gchar *name = (target != NULL && lrg_node_get_name (target) != NULL)
			                    ? lrg_node_get_name (target) : "script";
			GString     *clean = g_string_new (NULL);
			const gchar *p;

			/* A file-name friendly version of the node name. */
			for (p = name; *p != '\0'; p++)
				g_string_append_c (clean, g_ascii_isalnum (*p) ? g_ascii_tolower (*p) : '_');
			g_string_assign (self->dialog_text1, clean->str);
			g_string_free (clean, TRUE);
		}
		break;

	default:
		break;
	}
}

/* ==========================================================================
 * File dialog
 * ========================================================================== */

static void
file_refresh (LrgEditorApp *self)
{
	g_clear_pointer (&self->file_entries, g_ptr_array_unref);
	self->file_entries = editor_list_dir (self->file_dir, self->file_ext,
	                                      self->file_mode == EDITOR_FILE_OPEN_PROJECT);
}

static void
file_set_dir (LrgEditorApp *self,
              const gchar  *dir)
{
	g_autofree gchar *abs = g_canonicalize_filename (dir, NULL);

	g_free (self->file_dir);
	self->file_dir = g_steal_pointer (&abs);
	g_clear_pointer (&self->file_selected, g_free);
	file_refresh (self);
}

void
editor_file_dialog_open (LrgEditorApp   *self,
                         EditorFileMode  mode,
                         const gchar    *dir,
                         const gchar    *ext)
{
	g_autofree gchar *start = NULL;

	editor_dialog_open (self, EDITOR_DIALOG_FILE);
	self->file_mode = mode;
	g_free (self->file_ext);
	self->file_ext = g_strdup (ext);
	g_string_truncate (self->file_name, 0);

	/* Fall back up the tree when the suggested folder does not exist. */
	start = g_strdup (dir != NULL ? dir : editor_app_base_dir (self));
	if (start == NULL)
		start = g_get_current_dir ();
	while (!g_file_test (start, G_FILE_TEST_IS_DIR))
	{
		gchar *parent = g_path_get_dirname (start);

		if (g_strcmp0 (parent, start) == 0)
		{
			g_free (parent);
			break;
		}
		g_free (start);
		start = parent;
	}
	file_set_dir (self, start);

	if (mode == EDITOR_FILE_SAVE_LEVEL_AS && lrg_editor_get_path (self->editor) == NULL)
		g_string_assign (self->file_name, "untitled.rlevel");
	else if (mode == EDITOR_FILE_SAVE_LEVEL_AS)
	{
		g_autofree gchar *base = g_path_get_basename (lrg_editor_get_path (self->editor));

		g_string_assign (self->file_name, base);
	}
}

static const gchar *
file_mode_title (EditorFileMode mode)
{
	switch (mode)
	{
	case EDITOR_FILE_OPEN_LEVEL:    return "Open Level";
	case EDITOR_FILE_SAVE_LEVEL_AS: return "Save Level As";
	case EDITOR_FILE_OPEN_PROJECT:  return "Open Project Folder";
	case EDITOR_FILE_SAVE_PREFAB:   return "Save Selection as Prefab";
	case EDITOR_FILE_IMPORT_SCENE:  return "Import Scene";
	case EDITOR_FILE_PICK_ASSET:    return "Choose Asset";
	case EDITOR_FILE_PICK_SCRIPT:   return "Attach Script";
	default:                        return "Choose File";
	}
}

static gboolean
file_mode_saves (EditorFileMode mode)
{
	return mode == EDITOR_FILE_SAVE_LEVEL_AS || mode == EDITOR_FILE_SAVE_PREFAB;
}

static LrgNode *
file_target (LrgEditorApp *self)
{
	if (self->file_target_guid == NULL)
		return NULL;
	return lrg_node_find_by_guid (lrg_level_get_root (lrg_editor_get_level (self->editor)), self->file_target_guid);
}

/* Carries out the dialog's purpose. Returns TRUE to close it. */
static gboolean
file_accept (LrgEditorApp *self,
             const gchar  *path)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *full = NULL;
	g_autofree gchar *rel = NULL;

	/* Saving appends the expected extension. */
	if (file_mode_saves (self->file_mode) && self->file_ext != NULL && !g_str_has_suffix (path, self->file_ext))
		full = g_strconcat (path, self->file_ext, NULL);
	else
		full = g_strdup (path);
	rel = editor_app_relative_path (self, full);

	switch (self->file_mode)
	{
	case EDITOR_FILE_OPEN_LEVEL:
		self->dialog = EDITOR_DIALOG_NONE;
		lrg_gui_close_current_popup (self->gui);
		editor_app_request (self, EDITOR_PENDING_OPEN_LEVEL, full);
		return FALSE;   /* may have opened the confirm dialog */

	case EDITOR_FILE_SAVE_LEVEL_AS:
		self->dialog = EDITOR_DIALOG_NONE;
		lrg_gui_close_current_popup (self->gui);
		editor_app_save_as (self, full);
		return FALSE;

	case EDITOR_FILE_OPEN_PROJECT:
		self->dialog = EDITOR_DIALOG_NONE;
		lrg_gui_close_current_popup (self->gui);
		editor_app_request (self, EDITOR_PENDING_OPEN_PROJECT, full);
		return FALSE;

	case EDITOR_FILE_SAVE_PREFAB:
		{
			LrgNode *node = lrg_editor_selection_get_primary (lrg_editor_get_selection (self->editor));

			if (node == NULL)
				editor_app_notify (self, 2, "Select a node to save as a prefab");
			else if (!lrg_prefab_save (node, full, &error))
				editor_app_notify (self, 3, "Could not save the prefab: %s", error->message);
			else
			{
				self->assets_dirty = TRUE;
				editor_app_notify (self, 1, "Saved prefab %s", rel);
			}
		}
		return TRUE;

	case EDITOR_FILE_IMPORT_SCENE:
		{
			g_autoptr(LrgLevel) level = lrg_level_import_scene_file (full, &error);

			if (level == NULL)
			{
				editor_app_notify (self, 3, "Import failed: %s", error->message);
				return FALSE;
			}
			lrg_editor_set_level (self->editor, level);
			lrg_editor_set_path (self->editor, NULL);
			lrg_editor_mark_modified (self->editor);
			editor_app_notify (self, 1, "Imported %s (save it as a level to keep it)", rel);
		}
		return TRUE;

	case EDITOR_FILE_PICK_ASSET:
		if (self->file_target_slot == -2)
		{
			/* Instantiate a prefab under the selection's parent. */
			LrgNode *sel = lrg_editor_selection_get_primary (lrg_editor_get_selection (self->editor));

			if (lrg_editor_instantiate_prefab (self->editor, full, sel != NULL ? lrg_node_get_parent (sel) : NULL,
			                                   &error) == NULL)
				editor_app_notify (self, 3, "Prefab: %s", error->message);
			else
				self->reveal_pending = TRUE;
		}
		else
		{
			LrgNode       *node = file_target (self);
			LrgNodeVisual *visual = (node != NULL) ? lrg_node_get_visual (node) : NULL;

			if (visual != NULL)
			{
				g_autoptr(LrgNodeVisual) copy = lrg_node_visual_copy (visual);

				lrg_node_visual_set_asset (copy, rel);
				lrg_editor_set_visual (self->editor, node, copy);
				editor_app_set_status (self, "Asset set to %s", rel);
			}
		}
		return TRUE;

	case EDITOR_FILE_PICK_SCRIPT:
		{
			LrgNode *node = file_target (self);

			if (node != NULL)
			{
				g_autoptr(LrgScriptBinding) b = lrg_script_binding_new (editor_script_language_for_path (rel), rel);

				lrg_editor_add_script (self->editor, node, b);
				editor_app_set_status (self, "Attached %s", rel);
			}
		}
		return TRUE;

	default:
		return TRUE;
	}
}

static void
draw_file_dialog (LrgEditorApp *self)
{
	g_autofree gchar *title = g_strdup_printf ("%s###dlg-file", file_mode_title (self->file_mode));
	gboolean          saves = file_mode_saves (self->file_mode);
	gboolean          dirs_only = self->file_mode == EDITOR_FILE_OPEN_PROJECT;
	const gchar      *activated = NULL;
	gfloat            avail_h;
	guint             i;

	if (!begin_dialog (self, title, 720.0f, 520.0f))
		return;

	/* Location bar: up, home, project, path. */
	if (lrg_gui_button (self->gui, "Up"))
	{
		g_autofree gchar *parent = g_path_get_dirname (self->file_dir);

		file_set_dir (self, parent);
	}
	lrg_gui_same_line (self->gui, -1.0f);
	if (lrg_gui_button (self->gui, "Home"))
		file_set_dir (self, g_get_home_dir ());
	if (self->project_dir != NULL)
	{
		lrg_gui_same_line (self->gui, -1.0f);
		if (lrg_gui_button (self->gui, "Project"))
			file_set_dir (self, self->project_dir);
	}
	lrg_gui_same_line (self->gui, -1.0f);
	if (lrg_gui_button (self->gui, "Refresh"))
		file_refresh (self);
	lrg_gui_same_line (self->gui, -1.0f);
	lrg_gui_text_disabled (self->gui, self->file_dir);

	/* Listing. */
	lrg_gui_get_content_avail (self->gui, NULL, &avail_h);
	lrg_gui_begin_child (self->gui, "##file-list", 0.0f, MAX (80.0f, avail_h - lrg_gui_get_frame_height (self->gui) * 2.6f),
	                     LRG_GUI_REGION_SCROLL_Y | LRG_GUI_REGION_BORDER);
	for (i = 0; self->file_entries != NULL && i < self->file_entries->len; i++)
	{
		EditorFileEntry  *e = g_ptr_array_index (self->file_entries, i);
		g_autofree gchar *label = g_strdup_printf ("%s  %s%s##f%u", editor_asset_glyph (e->type, e->is_dir),
		                                           e->name, e->is_dir ? "/" : "", i);
		gboolean          selected = g_strcmp0 (self->file_selected, e->path) == 0;

		if (lrg_gui_selectable (self->gui, label, selected, LRG_GUI_SELECTABLE_DONT_CLOSE_POPUP))
		{
			g_free (self->file_selected);
			self->file_selected = g_strdup (e->path);
			if (!e->is_dir || dirs_only)
				g_string_assign (self->file_name, e->name);
		}
		if (lrg_gui_is_item_double_clicked (self->gui))
			activated = e->path;
		if (!e->is_dir)
		{
			g_autofree gchar *size = g_format_size ((guint64) e->size);

			lrg_gui_set_item_tooltip (self->gui, size);
		}
	}
	if (self->file_entries == NULL || self->file_entries->len == 0)
		lrg_gui_text_disabled (self->gui, self->file_ext != NULL ? "(no matching files)" : "(empty)");
	lrg_gui_end_region (self->gui);

	/* Double click: enter folders, accept files. */
	if (activated != NULL)
	{
		g_autofree gchar *path = g_strdup (activated);

		if (g_file_test (path, G_FILE_TEST_IS_DIR))
			file_set_dir (self, path);
		else if (file_accept (self, path) && self->dialog == EDITOR_DIALOG_FILE)
			close_dialog (self);
		if (self->dialog != EDITOR_DIALOG_FILE)
		{
			lrg_gui_end_popup (self->gui);
			return;
		}
	}

	/* Name field and buttons. */
	lrg_gui_property_label (self->gui, dirs_only ? "Folder" : (saves ? "File name" : "File"));
	{
		gboolean enter = lrg_gui_input_text (self->gui, "##file-name", saves ? "name" : NULL, self->file_name,
		                                     LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE);
		const gchar *labels[] = { "Cancel", dirs_only ? "Open Folder" : (saves ? "Save" : "Open"), NULL };
		gboolean     ok;

		buttons_right (self, labels);
		if (lrg_gui_button (self->gui, "Cancel"))
		{
			close_dialog (self);
			lrg_gui_end_popup (self->gui);
			return;
		}
		lrg_gui_same_line (self->gui, -1.0f);
		ok = lrg_gui_button (self->gui, labels[1]) || enter;
		if (ok)
		{
			g_autofree gchar *path = NULL;

			if (dirs_only)
				path = (self->file_selected != NULL && self->file_name->len > 0)
				       ? g_strdup (self->file_selected) : g_strdup (self->file_dir);
			else if (self->file_name->len > 0)
				path = g_path_is_absolute (self->file_name->str)
				       ? g_strdup (self->file_name->str)
				       : g_build_filename (self->file_dir, self->file_name->str, NULL);

			if (path == NULL)
				editor_app_set_status (self, "Choose a file first");
			else if (!dirs_only && g_file_test (path, G_FILE_TEST_IS_DIR))
			{
				file_set_dir (self, path);
				g_string_truncate (self->file_name, 0);
			}
			else if (!saves && !dirs_only && !g_file_test (path, G_FILE_TEST_EXISTS))
				editor_app_notify (self, 2, "%s does not exist", path);
			else if (file_accept (self, path) && self->dialog == EDITOR_DIALOG_FILE)
				close_dialog (self);
		}
	}

	lrg_gui_end_popup (self->gui);
}

/* ==========================================================================
 * Preferences
 * ========================================================================== */

static void
prefs_general (LrgEditorApp *self)
{
	static const gchar *themes[] = { "mocha", "macchiato", "frappe", "latte" };
	static const gchar *theme_labels[] = { "Mocha (dark)", "Macchiato (dark)", "Frappe (dark)", "Latte (light)" };
	gint                font_size = 15;
	gint                fs;

	lrg_gui_separator_text (self->gui, "Appearance");
	choice_pref (self, "Theme", "theme", themes, theme_labels, G_N_ELEMENTS (themes));
	double_pref (self, "UI scale", "ui-scale", 0.01f, 0.5f, 3.0f, "%.2f");
	g_object_get (self->settings, "font-size", &font_size, NULL);
	fs = font_size;
	lrg_gui_property_label (self->gui, "Font size");
	if (lrg_gui_drag_int (self->gui, "##font-size", &fs, 0.1f, 8, 40))
	{
		g_object_set (self->settings, "font-size", fs, NULL);
		apply_prefs (self);
	}
	lrg_gui_property_label (self->gui, "Font file");
	if (lrg_gui_input_text (self->gui, "##font-path", "system default", self->dialog_text4,
	                        LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE))
		set_string_pref (self, "font-path", self->dialog_text4);

	lrg_gui_separator_text (self->gui, "Documents");
	uint_pref (self, "Autosave (s, 0 = off)", "autosave-interval", 0, 3600);
	uint_pref (self, "Undo history limit", "history-limit", 10, 100000);
	bool_pref (self, "Reload on external change", "reload-on-external-change");
	bool_pref (self, "Confirm unsaved changes", "confirm-on-close");
	bool_pref (self, "Restore last session", "restore-last-session");
}

static void
prefs_external_editor (LrgEditorApp *self)
{
	LrgGuiStyle         *style = lrg_gui_context_get_style (self->gui);
	const gchar * const *presets = lrg_external_editor_list_presets ();
	g_autofree gchar    *current = NULL;
	g_autofree gchar    *source = NULL;
	g_autofree gchar    *resolved = NULL;
	g_autofree gchar    *preview = NULL;
	guint                i;

	g_object_get (self->settings, "external-editor", &current, NULL);

	lrg_gui_text_wrapped (self->gui,
	                      "Code, scripts and data files open in your editor. \"auto\" follows $VISUAL / $EDITOR; "
	                      "choosing Emacs enables the first-party integration (libregnum.el connects to this "
	                      "editor and opens files in the running Emacs).");
	lrg_gui_spacing (self->gui);

	/* Preset picker. */
	lrg_gui_property_label (self->gui, "Editor");
	if (lrg_gui_begin_combo (self->gui, "##external-editor",
	                         lrg_external_editor_preset_get_label (current != NULL ? current : "auto")))
	{
		for (i = 0; presets[i] != NULL; i++)
		{
			const gchar      *label = lrg_external_editor_preset_get_label (presets[i]);
			g_autofree gchar *row = g_strdup_printf ("%s##%s", label != NULL ? label : presets[i], presets[i]);

			if (lrg_gui_selectable (self->gui, row, g_strcmp0 (presets[i], current) == 0, LRG_GUI_SELECTABLE_NONE))
			{
				g_object_set (self->settings, "external-editor", presets[i], NULL);
				apply_prefs (self);
			}
		}
		lrg_gui_end_combo (self->gui);
	}

	/* What "auto" (or the choice) resolves to right now. */
	resolved = lrg_external_editor_resolve_preset (self->external, &source);
	{
		g_autofree gchar *line = g_strdup_printf ("Resolves to: %s%s%s%s",
		                                          lrg_external_editor_preset_get_label (resolved) != NULL
		                                          ? lrg_external_editor_preset_get_label (resolved) : resolved,
		                                          source != NULL ? "  (from " : "", source != NULL ? source : "",
		                                          source != NULL ? ")" : "");

		lrg_gui_property_label (self->gui, "");
		lrg_gui_text_disabled (self->gui, line);
	}

	lrg_gui_property_label (self->gui, "Custom command");
	if (lrg_gui_input_text (self->gui, "##external-editor-command", "codium --goto {file}:{line}:{col}",
	                        self->dialog_text1, LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE | LRG_GUI_INPUT_TEXT_MONOSPACE))
		set_string_pref (self, "external-editor-command", self->dialog_text1);
	lrg_gui_set_item_tooltip (self->gui,
	                          "Used by the \"custom\" preset. Placeholders: {file} {line} {col} {project} {dir}. "
	                          "Split like a shell command; a template without {file} gets the file appended.");

	{
		static const gchar *modes[] = { "auto", "always", "never" };
		static const gchar *mode_labels[] = { "Auto (terminal editors only)", "Always", "Never" };

		choice_pref (self, "Run in a terminal", "editor-in-terminal", modes, mode_labels, 3);
	}
	lrg_gui_property_label (self->gui, "Terminal");
	if (lrg_gui_input_text (self->gui, "##terminal-command", "auto: $TERMINAL, gst, foot, kitty...",
	                        self->dialog_text2, LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE | LRG_GUI_INPUT_TEXT_MONOSPACE))
		set_string_pref (self, "terminal-command", self->dialog_text2);

	lrg_gui_separator_text (self->gui, "Emacs");
	bool_pref (self, "Use a connected Emacs", "emacs-integration");
	lrg_gui_property_label (self->gui, "Server name (-s)");
	if (lrg_gui_input_text (self->gui, "##emacs-server-name", "default", self->dialog_text3,
	                        LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE))
		set_string_pref (self, "emacs-server-name", self->dialog_text3);
	lrg_gui_property_label (self->gui, "Status");
	if (lrg_editor_server_has_capability (self->server, "open-file"))
		lrg_gui_text_colored (self->gui, style->colors[LRG_GUI_COL_SUCCESS], "Emacs is connected (libregnum.el)");
	else if (lrg_editor_server_get_socket_path (self->server) != NULL)
		lrg_gui_text_disabled (self->gui, "Not connected - M-x libregnum-connect in Emacs");
	else
		lrg_gui_text_colored (self->gui, style->colors[LRG_GUI_COL_WARNING], "The editor server is off");

	/* Show the exact command a file would open with. */
	lrg_gui_separator_text (self->gui, "Preview");
	{
		g_autoptr(GError) error = NULL;
		g_auto(GStrv)     argv = lrg_external_editor_build_argv (self->external, "/path/to/script.lua", 12, 4, &error);

		preview = (argv != NULL) ? g_strjoinv (" ", argv) : g_strdup (error != NULL ? error->message : "");
	}
	lrg_gui_text_mono (self->gui, style->colors[LRG_GUI_COL_TEXT_DISABLED], preview);
	if (lrg_gui_button (self->gui, "Test: open the preferences file"))
	{
		g_autofree gchar *path = (self->settings_path != NULL) ? g_strdup (self->settings_path)
		                                                       : lrg_editor_settings_get_default_path ();
		g_autoptr(GError) error = NULL;

		if (!lrg_editor_app_save_settings (self, &error))
			editor_app_notify (self, 3, "%s", error->message);
		editor_app_open_file (self, path, 1, 1);
	}
}

static void
prefs_viewport (LrgEditorApp *self)
{
	lrg_gui_separator_text (self->gui, "Snapping");
	bool_pref (self, "Snap by default", "snap-enabled");
	double_pref (self, "Move step (m)", "snap-translate", 0.01f, 0.001f, 1000.0f, "%.3f");
	double_pref (self, "Rotate step (deg)", "snap-rotate-degrees", 0.5f, 0.1f, 180.0f, "%.1f");
	double_pref (self, "Scale step", "snap-scale", 0.01f, 0.001f, 100.0f, "%.3f");

	lrg_gui_separator_text (self->gui, "Grid");
	if (bool_pref (self, "Show grid", "grid-visible"))
		g_object_get (self->settings, "grid-visible", &self->show_grid, NULL);
	double_pref (self, "Cell size (m)", "grid-size", 0.01f, 0.01f, 1000.0f, "%.2f");
	uint_pref (self, "Half extent (cells)", "grid-extent", 1, 200);

	lrg_gui_separator_text (self->gui, "Camera");
	{
		static const gchar *schemes[] = { "default", "blender", "trackpad" };
		static const gchar *labels[] = {
			"Default (right drag fly, middle pan, Alt+left orbit)",
			"Blender (middle orbit, Shift+middle pan, Ctrl+middle zoom)",
			"Trackpad (scroll orbits, Shift+scroll pans, Ctrl+scroll zooms)"
		};

		choice_pref (self, "Navigation", "camera-navigation", schemes, labels, 3);
	}
	double_pref (self, "Field of view", "camera-fov", 0.5f, 10.0f, 150.0f, "%.0f deg");
	double_pref (self, "Fly speed (m/s)", "camera-fly-speed", 0.1f, 0.1f, 500.0f, "%.1f");
	double_pref (self, "Orbit speed", "camera-orbit-speed", 0.01f, 0.01f, 5.0f, "%.2f");
	bool_pref (self, "Invert Y", "camera-invert-y");
	{
		gdouble fov = 50.0;

		g_object_get (self->settings, "camera-fov", &fov, NULL);
		self->view.fovy = (gfloat) fov;
	}
}

/* Captures the next key press for the binding being edited. */
static void
capture_key (LrgEditorApp *self)
{
	const LrgGuiInput *in = self->cur_input;
	guint              i;

	if (self->capture_action == NULL || in == NULL)
		return;

	for (i = 0; i < in->n_keys; i++)
	{
		const LrgGuiKeyEvent *ev = &in->keys[i];
		g_autofree gchar     *text = NULL;

		if (ev->key == LRG_GUI_KEY_ESCAPE && ev->mods == 0)
		{
			g_clear_pointer (&self->capture_action, g_free);
			return;
		}
		if ((ev->key == LRG_GUI_KEY_BACKSPACE || ev->key == LRG_GUI_KEY_DELETE) && ev->mods == 0)
		{
			lrg_editor_settings_set_keybinding (self->settings, self->capture_action, "");
			g_clear_pointer (&self->capture_action, g_free);
			return;
		}

		text = lrg_editor_shortcut_format (ev->mods, ev->key);
		if (text == NULL)
			continue;
		lrg_editor_settings_set_keybinding (self->settings, self->capture_action, text);
		editor_app_set_status (self, "%s bound to %s",
		                       lrg_editor_actions_get_label (self->actions, self->capture_action), text);
		g_clear_pointer (&self->capture_action, g_free);
		return;
	}
}

static void
prefs_keybindings (LrgEditorApp *self)
{
	LrgGuiStyle         *style = lrg_gui_context_get_style (self->gui);
	g_autoptr(GPtrArray) conflicts = lrg_editor_actions_find_conflicts (self->actions);
	GList               *ids, *l;
	const gchar         *category = NULL;
	gfloat               avail_h;
	guint                i;

	capture_key (self);

	lrg_gui_text_wrapped (self->gui, "Click a shortcut, then press the new keys (Escape cancels, Backspace "
	                      "unbinds). Several alternatives can be written as \"Ctrl+Y|Ctrl+Shift+Z\" in "
	                      "editor.yaml under keybindings:.");
	lrg_gui_set_next_item_width (self->gui, -1.0f);
	lrg_gui_input_text (self->gui, "##kb-filter", "Filter actions...", self->palette_query, LRG_GUI_INPUT_TEXT_NONE);

	for (i = 0; conflicts != NULL && i < conflicts->len; i++)
		lrg_gui_text_colored (self->gui, style->colors[LRG_GUI_COL_WARNING], g_ptr_array_index (conflicts, i));

	lrg_gui_get_content_avail (self->gui, NULL, &avail_h);
	lrg_gui_begin_child (self->gui, "##kb-list", 0.0f, MAX (60.0f, avail_h), LRG_GUI_REGION_SCROLL_Y);
	ids = (self->palette_query->len > 0) ? lrg_editor_actions_search (self->actions, self->palette_query->str)
	                               : lrg_editor_actions_list (self->actions);
	for (l = ids; l != NULL; l = l->next)
	{
		const gchar      *id = l->data;
		const gchar      *cat = lrg_editor_actions_get_category (self->actions, id);
		const gchar      *sc = lrg_editor_actions_get_shortcut (self->actions, id);
		const gchar      *def = lrg_editor_actions_get_default_shortcut (self->actions, id);
		gboolean          capturing = g_strcmp0 (self->capture_action, id) == 0;
		g_autofree gchar *button = NULL;

		if (self->palette_query->len == 0 && g_strcmp0 (cat, category) != 0)
		{
			lrg_gui_separator_text (self->gui, cat != NULL ? cat : "Other");
			category = cat;
		}

		lrg_gui_push_id (self->gui, id);
		lrg_gui_property_label (self->gui, lrg_editor_actions_get_label (self->actions, id));
		button = g_strdup_printf ("%s##bind", capturing ? "press keys..." : (sc != NULL ? sc : "-"));
		lrg_gui_set_next_item_width (self->gui, 200.0f);
		if (lrg_gui_button_sized (self->gui, button, 200.0f, 0.0f))
		{
			g_free (self->capture_action);
			self->capture_action = g_strdup (id);
		}
		lrg_gui_set_item_tooltip (self->gui, id);
		if (g_strcmp0 (sc, def) != 0)
		{
			lrg_gui_same_line (self->gui, -1.0f);
			if (lrg_gui_button (self->gui, "Reset"))
				lrg_editor_settings_set_keybinding (self->settings, id, NULL);
			lrg_gui_set_item_tooltip (self->gui, def != NULL ? def : "(unbound by default)");
		}
		lrg_gui_pop_id (self->gui);
	}
	g_list_free (ids);
	lrg_gui_end_region (self->gui);
}

static void
prefs_server (LrgEditorApp *self)
{
	LrgGuiStyle *style = lrg_gui_context_get_style (self->gui);
	const gchar *sock = lrg_editor_server_get_socket_path (self->server);

	lrg_gui_text_wrapped (self->gui, "The editor serves a JSON-lines protocol on a Unix socket. Emacs "
	                      "(libregnum.el), scripts and other tools use it to inspect and edit the open level, "
	                      "run actions and receive events. See docs/editor/ipc-protocol.org.");
	lrg_gui_spacing (self->gui);
	bool_pref (self, "Start the server", "server-enabled");
	lrg_gui_property_label (self->gui, "Socket path");
	if (lrg_gui_input_text (self->gui, "##server-socket", "$XDG_RUNTIME_DIR/libregnum/editor-PID.sock",
	                        self->dialog_text5, LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE | LRG_GUI_INPUT_TEXT_MONOSPACE))
		set_string_pref (self, "server-socket", self->dialog_text5);

	lrg_gui_property_label (self->gui, "Listening on");
	if (sock != NULL)
	{
		lrg_gui_text_mono (self->gui, style->colors[LRG_GUI_COL_TEXT], sock);
		lrg_gui_property_label (self->gui, "Clients");
		lrg_gui_textf (self->gui, "%u connected", lrg_editor_server_get_n_clients (self->server));
		lrg_gui_property_label (self->gui, "");
		if (lrg_gui_button (self->gui, "Copy socket path"))
			lrg_gui_context_set_clipboard (self->gui, sock);
	}
	else
	{
		lrg_gui_text_disabled (self->gui, "not running");
		lrg_gui_property_label (self->gui, "");
		if (lrg_gui_button (self->gui, "Start now"))
		{
			g_autoptr(GError) error = NULL;

			if (!lrg_editor_app_start_server (self, NULL, &error))
				editor_app_notify (self, 3, "Server: %s", error->message);
		}
	}
}

static void
draw_preferences (LrgEditorApp *self)
{
	static const gchar *tabs[] = { "General", "External Editor", "Viewport", "Keybindings", "Server" };
	gfloat              avail_h;
	gint                i;

	if (!begin_dialog (self, dialog_titles[EDITOR_DIALOG_PREFERENCES], 760.0f, 600.0f))
		return;

	if (lrg_gui_begin_tab_bar (self->gui, "##prefs-tabs"))
	{
		for (i = 0; i < (gint) G_N_ELEMENTS (tabs); i++)
			if (lrg_gui_tab_item (self->gui, tabs[i], NULL))
				self->prefs_tab = i;
		lrg_gui_end_tab_bar (self->gui);
	}

	lrg_gui_get_content_avail (self->gui, NULL, &avail_h);
	lrg_gui_begin_child (self->gui, "##prefs-body", 0.0f,
	                     MAX (80.0f, avail_h - lrg_gui_get_frame_height (self->gui) * 1.4f),
	                     self->prefs_tab == 3 ? LRG_GUI_REGION_NONE : LRG_GUI_REGION_SCROLL_Y);
	switch (self->prefs_tab)
	{
	case 1:  prefs_external_editor (self); break;
	case 2:  prefs_viewport (self); break;
	case 3:  prefs_keybindings (self); break;
	case 4:  prefs_server (self); break;
	default: prefs_general (self); break;
	}
	lrg_gui_end_region (self->gui);

	{
		const gchar *labels[] = { "Reset All", "Edit editor.yaml", "Close", NULL };

		buttons_right (self, labels);
		if (lrg_gui_button (self->gui, "Reset All"))
		{
			g_autofree gchar *layout = lrg_gui_dock_to_string (self->dock);

			lrg_editor_settings_reset (self->settings);
			g_object_set (self->settings, "layout", layout, NULL);
			editor_app_apply_settings (self);
			editor_dialog_open (self, EDITOR_DIALOG_PREFERENCES);
			self->dialog_just_opened = FALSE;
			editor_app_notify (self, 0, "Preferences reset to defaults");
		}
		lrg_gui_same_line (self->gui, -1.0f);
		if (lrg_gui_button (self->gui, "Edit editor.yaml"))
		{
			g_autofree gchar *path = (self->settings_path != NULL) ? g_strdup (self->settings_path)
			                                                       : lrg_editor_settings_get_default_path ();
			g_autoptr(GError) error = NULL;

			if (!lrg_editor_app_save_settings (self, &error))
				editor_app_notify (self, 3, "%s", error->message);
			editor_app_open_file (self, path, 1, 1);
		}
		lrg_gui_same_line (self->gui, -1.0f);
		if (lrg_gui_button (self->gui, "Close"))
			close_dialog (self);
	}

	lrg_gui_end_popup (self->gui);
}

/* ==========================================================================
 * Project dialogs
 * ========================================================================== */

static void
draw_project_settings (LrgEditorApp *self)
{
	LrgGuiStyle *style = lrg_gui_context_get_style (self->gui);
	GPtrArray   *dirs;
	guint        i;

	if (self->project == NULL)
	{
		self->dialog = EDITOR_DIALOG_NONE;
		return;
	}
	if (!begin_dialog (self, dialog_titles[EDITOR_DIALOG_PROJECT_SETTINGS], 640.0f, 0.0f))
		return;

	lrg_gui_property_label (self->gui, "Root");
	lrg_gui_text_mono (self->gui, style->colors[LRG_GUI_COL_TEXT_DISABLED], lrg_project_get_root (self->project));
	lrg_gui_property_label (self->gui, "Name");
	lrg_gui_input_text (self->gui, "##p-name", NULL, self->dialog_text1, LRG_GUI_INPUT_TEXT_NONE);
	lrg_gui_property_label (self->gui, "Default level");
	lrg_gui_input_text (self->gui, "##p-level", "levels/main.rlevel", self->dialog_text2, LRG_GUI_INPUT_TEXT_NONE);
	if (lrg_editor_get_path (self->editor) != NULL)
	{
		lrg_gui_same_line (self->gui, -1.0f);
		if (lrg_gui_button (self->gui, "Use current"))
		{
			g_autofree gchar *rel = lrg_project_relativize (self->project, lrg_editor_get_path (self->editor));

			g_string_assign (self->dialog_text2, rel);
		}
	}
	lrg_gui_property_label (self->gui, "Game module");
	lrg_gui_input_text (self->gui, "##p-module", "build/libmygame.so", self->dialog_text4, LRG_GUI_INPUT_TEXT_NONE);
	lrg_gui_property_label (self->gui, "Run command");
	lrg_gui_input_text (self->gui, "##p-run", "lrgldr {module} -- --level {level}", self->dialog_text3,
	                    LRG_GUI_INPUT_TEXT_MONOSPACE);
	lrg_gui_set_item_tooltip (self->gui, "Placeholders: {project} {level} {module}. Empty uses the default.");
	lrg_gui_property_label (self->gui, "Scripts folder");
	lrg_gui_input_text (self->gui, "##p-scripts", "scripts", self->dialog_text5, LRG_GUI_INPUT_TEXT_NONE);

	lrg_gui_separator_text (self->gui, "Asset folders");
	dirs = lrg_project_get_asset_dirs (self->project);
	for (i = 0; dirs != NULL && i < dirs->len; i++)
		lrg_gui_text (self->gui, g_ptr_array_index (dirs, i));
	lrg_gui_text_disabled (self->gui, "Edit asset_dirs in project.ryaml to change these.");

	lrg_gui_spacing (self->gui);
	{
		const gchar *labels[] = { "Cancel", "Save", NULL };

		buttons_right (self, labels);
		if (lrg_gui_button (self->gui, "Cancel"))
			close_dialog (self);
		lrg_gui_same_line (self->gui, -1.0f);
		if (lrg_gui_button (self->gui, "Save"))
		{
			g_autoptr(GError) error = NULL;

			lrg_project_set_name (self->project, self->dialog_text1->len > 0 ? self->dialog_text1->str : "Game");
			lrg_project_set_default_level (self->project, self->dialog_text2->len > 0 ? self->dialog_text2->str : NULL);
			lrg_project_set_run_command (self->project, self->dialog_text3->len > 0 ? self->dialog_text3->str : NULL);
			lrg_project_set_game_output (self->project, self->dialog_text4->len > 0 ? self->dialog_text4->str : NULL);
			lrg_project_set_scripts_dir (self->project, self->dialog_text5->len > 0 ? self->dialog_text5->str : NULL);
			if (!lrg_project_save (self->project, &error))
				editor_app_notify (self, 3, "Could not save the project: %s", error->message);
			else
			{
				editor_app_notify (self, 1, "Saved project settings");
				close_dialog (self);
			}
		}
	}

	lrg_gui_end_popup (self->gui);
}

static void
draw_new_project (LrgEditorApp *self)
{
	if (!begin_dialog (self, dialog_titles[EDITOR_DIALOG_NEW_PROJECT], 600.0f, 0.0f))
		return;

	lrg_gui_text_wrapped (self->gui, "Creates the folder with levels/, scripts/, assets/ and prefabs/, a "
	                      "project.ryaml manifest and a starter level.");
	lrg_gui_property_label (self->gui, "Folder");
	lrg_gui_input_text (self->gui, "##np-dir", NULL, self->dialog_text1, LRG_GUI_INPUT_TEXT_MONOSPACE);
	lrg_gui_property_label (self->gui, "Name");
	lrg_gui_input_text (self->gui, "##np-name", NULL, self->dialog_text2, LRG_GUI_INPUT_TEXT_NONE);

	{
		const gchar *labels[] = { "Cancel", "Create", NULL };

		buttons_right (self, labels);
		if (lrg_gui_button (self->gui, "Cancel"))
			close_dialog (self);
		lrg_gui_same_line (self->gui, -1.0f);
		if (lrg_gui_button (self->gui, "Create") && self->dialog_text1->len > 0)
		{
			g_autoptr(GError) error = NULL;
			g_autofree gchar *dir = g_strdup (self->dialog_text1->str);
			g_autofree gchar *name = g_strdup (self->dialog_text2->str);

			close_dialog (self);
			if (lrg_editor_is_dirty (self->editor))
				editor_app_notify (self, 2, "Save or discard the open level first");
			else if (!lrg_editor_app_new_project (self, dir, name, &error))
				editor_app_notify (self, 3, "Could not create the project: %s", error->message);
		}
	}

	lrg_gui_end_popup (self->gui);
}

static void
draw_new_script (LrgEditorApp *self)
{
	LrgNode  *target = file_target (self);
	gint      lang = self->dialog_choice;
	gboolean  first = self->dialog_just_opened;

	if (!begin_dialog (self, dialog_titles[EDITOR_DIALOG_NEW_SCRIPT], 520.0f, 0.0f))
		return;

	if (target != NULL)
		lrg_gui_textf (self->gui, "Attach to: %s", lrg_node_get_name (target));
	lrg_gui_property_label (self->gui, "Name");
	if (first)
		lrg_gui_request_focus (self->gui, "##ns-name");
	{
		gboolean enter = lrg_gui_input_text (self->gui, "##ns-name", "script", self->dialog_text1,
		                                     LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE);
		const gchar *labels[] = { "Cancel", "Create", NULL };

		lrg_gui_property_label (self->gui, "Language");
		if (lrg_gui_combo (self->gui, "##ns-lang", &lang, script_languages, G_N_ELEMENTS (script_languages)))
			self->dialog_choice = lang;

		buttons_right (self, labels);
		if (lrg_gui_button (self->gui, "Cancel"))
			close_dialog (self);
		lrg_gui_same_line (self->gui, -1.0f);
		if ((lrg_gui_button (self->gui, "Create") || enter) && self->dialog_text1->len > 0)
		{
			g_autoptr(GError) error = NULL;
			g_autofree gchar *rel = editor_app_create_script (self, self->dialog_text1->str,
			                                                  script_language_values[CLAMP (lang, 0, 4)], &error);

			if (rel == NULL)
				editor_app_notify (self, 3, "Could not create the script: %s", error->message);
			else
			{
				if (target != NULL)
				{
					g_autoptr(LrgScriptBinding) b = lrg_script_binding_new (script_language_values[CLAMP (lang, 0, 4)],
					                                                        rel);

					lrg_editor_add_script (self->editor, target, b);
				}
				editor_app_open_file (self, rel, 1, 1);
				close_dialog (self);
			}
		}
	}

	lrg_gui_end_popup (self->gui);
}

/* ==========================================================================
 * Small dialogs
 * ========================================================================== */

static void
draw_confirm (LrgEditorApp *self)
{
	if (!begin_dialog (self, dialog_titles[EDITOR_DIALOG_CONFIRM], 480.0f, 0.0f))
		return;

	lrg_gui_text_wrapped (self->gui, self->confirm_message != NULL ? self->confirm_message : "Unsaved changes.");
	lrg_gui_text_disabled (self->gui, "Save them before continuing?");
	lrg_gui_spacing (self->gui);
	{
		const gchar *labels[] = { "Cancel", "Don't Save", "Save", NULL };
		gint         choice = -1;

		buttons_right (self, labels);
		if (lrg_gui_button (self->gui, "Cancel"))
			choice = 2;
		lrg_gui_same_line (self->gui, -1.0f);
		if (lrg_gui_button (self->gui, "Don't Save"))
			choice = 1;
		lrg_gui_same_line (self->gui, -1.0f);
		if (lrg_gui_button (self->gui, "Save") || lrg_gui_is_key_pressed (self->gui, LRG_GUI_KEY_ENTER, FALSE))
			choice = 0;

		if (choice >= 0)
		{
			/* Close first: the choice may open the Save As dialog. */
			self->dialog = EDITOR_DIALOG_NONE;
			lrg_gui_close_current_popup (self->gui);
			lrg_gui_end_popup (self->gui);
			editor_app_confirm_choice (self, choice);
			return;
		}
	}

	lrg_gui_end_popup (self->gui);
}

static void
draw_recover (LrgEditorApp *self)
{
	if (!begin_dialog (self, dialog_titles[EDITOR_DIALOG_RECOVER], 560.0f, 0.0f))
		return;

	lrg_gui_text_wrapped (self->gui, "An autosave newer than the level on disk was found. The editor may "
	                      "have closed without saving.");
	if (self->recovery_file != NULL)
		lrg_gui_text_mono (self->gui, lrg_gui_context_get_style (self->gui)->colors[LRG_GUI_COL_TEXT_DISABLED],
		                   self->recovery_file);
	lrg_gui_spacing (self->gui);
	{
		const gchar *labels[] = { "Later", "Discard Autosave", "Restore", NULL };

		buttons_right (self, labels);
		if (lrg_gui_button (self->gui, "Later"))
			close_dialog (self);
		lrg_gui_same_line (self->gui, -1.0f);
		if (lrg_gui_button (self->gui, "Discard Autosave"))
		{
			if (self->recovery_file != NULL)
			{
				g_autoptr(GFile) file = g_file_new_for_path (self->recovery_file);

				g_file_trash (file, NULL, NULL);
			}
			g_clear_pointer (&self->recovery_file, g_free);
			close_dialog (self);
		}
		lrg_gui_same_line (self->gui, -1.0f);
		if (lrg_gui_button (self->gui, "Restore") && self->recovery_file != NULL)
		{
			g_autoptr(GError) error = NULL;
			g_autofree gchar *orig = g_strdup (lrg_editor_get_path (self->editor));

			/* Load the autosave but keep the document's real path. */
			if (!lrg_editor_load_level (self->editor, self->recovery_file, &error))
				editor_app_notify (self, 3, "Could not restore: %s", error->message);
			else
			{
				lrg_editor_set_path (self->editor, orig);
				lrg_editor_mark_modified (self->editor);
				editor_app_notify (self, 1, "Restored the autosave; save to keep it");
			}
			close_dialog (self);
		}
	}

	lrg_gui_end_popup (self->gui);
}

static void
draw_about (LrgEditorApp *self)
{
	LrgGuiStyle *style = lrg_gui_context_get_style (self->gui);

	if (!begin_dialog (self, dialog_titles[EDITOR_DIALOG_ABOUT], 520.0f, 0.0f))
		return;

	lrg_gui_text_colored (self->gui, style->colors[LRG_GUI_COL_ACCENT], "libregnum editor");
	lrg_gui_textf (self->gui, "Version %d.%d.%d", LRG_VERSION_MAJOR, LRG_VERSION_MINOR, LRG_VERSION_MICRO);
	lrg_gui_text_wrapped (self->gui, "A level, scene and world editor for libregnum, built with libregnum's own "
	                      "immediate-mode GUI.");
	lrg_gui_spacing (self->gui);
	lrg_gui_text_disabled (self->gui, "Copyright 2025 Zach Podbielniak");
	lrg_gui_text_wrapped (self->gui, "This program is free software: you can redistribute it and/or modify it "
	                      "under the terms of the GNU Affero General Public License, version 3 or later.");
	lrg_gui_spacing (self->gui);
	if (lrg_editor_server_get_socket_path (self->server) != NULL)
	{
		lrg_gui_text_disabled (self->gui, "IPC socket:");
		lrg_gui_text_mono (self->gui, style->colors[LRG_GUI_COL_TEXT], lrg_editor_server_get_socket_path (self->server));
	}
	{
		const gchar *labels[] = { "Close", NULL };

		buttons_right (self, labels);
		if (lrg_gui_button (self->gui, "Close"))
			close_dialog (self);
	}
	lrg_gui_end_popup (self->gui);
}

static void
draw_shortcuts (LrgEditorApp *self)
{
	GList       *ids, *l;
	const gchar *category = NULL;
	gfloat       avail_h;

	if (!begin_dialog (self, dialog_titles[EDITOR_DIALOG_SHORTCUTS], 640.0f, 620.0f))
		return;

	lrg_gui_set_next_item_width (self->gui, -1.0f);
	lrg_gui_input_text (self->gui, "##sc-filter", "Filter...", self->dialog_text1, LRG_GUI_INPUT_TEXT_NONE);
	lrg_gui_get_content_avail (self->gui, NULL, &avail_h);
	lrg_gui_begin_child (self->gui, "##sc-list", 0.0f, MAX (60.0f, avail_h - lrg_gui_get_frame_height (self->gui) * 1.4f),
	                     LRG_GUI_REGION_SCROLL_Y);

	/* Viewport navigation is not an action; list it first. */
	if (self->dialog_text1->len == 0)
	{
		static const gchar *nav[][2] = {
			{ "Fly look / move", "Right drag + W A S D Q E (Shift: faster, wheel: speed)" },
			{ "Orbit", "Drag the axis gizmo (bottom left), Alt + left drag, Num4/6/8/2" },
			{ "Look along an axis", "Click an end of the axis gizmo, Num7/1/3 (+Ctrl opposite)" },
			{ "Pan", "Middle drag, Alt + Shift + left drag" },
			{ "Zoom", "Mouse wheel, = and - (numpad + and -)" },
			{ "Other schemes", "Preferences > Viewport > Navigation: Blender, Trackpad" },
			{ "Select / cycle overlapping", "Left click (Shift add, Ctrl toggle)" },
			{ "Box select", "Left drag on empty space" },
			{ "Invert snapping", "Hold Ctrl while dragging a gizmo" },
			{ "Cancel a drag", "Escape or right click" },
		};
		gsize i;

		lrg_gui_separator_text (self->gui, "Viewport");
		for (i = 0; i < G_N_ELEMENTS (nav); i++)
		{
			lrg_gui_property_label (self->gui, nav[i][0]);
			lrg_gui_text_disabled (self->gui, nav[i][1]);
		}
	}

	ids = (self->dialog_text1->len > 0) ? lrg_editor_actions_search (self->actions, self->dialog_text1->str)
	                                    : lrg_editor_actions_list (self->actions);
	for (l = ids; l != NULL; l = l->next)
	{
		const gchar *id = l->data;
		const gchar *cat = lrg_editor_actions_get_category (self->actions, id);
		const gchar *sc = lrg_editor_actions_get_shortcut (self->actions, id);

		if (sc == NULL)
			continue;
		if (self->dialog_text1->len == 0 && g_strcmp0 (cat, category) != 0)
		{
			lrg_gui_separator_text (self->gui, cat != NULL ? cat : "Other");
			category = cat;
		}
		lrg_gui_push_id (self->gui, id);
		lrg_gui_property_label (self->gui, lrg_editor_actions_get_label (self->actions, id));
		lrg_gui_text_mono (self->gui, lrg_gui_context_get_style (self->gui)->colors[LRG_GUI_COL_TEXT], sc);
		lrg_gui_pop_id (self->gui);
	}
	g_list_free (ids);
	lrg_gui_end_region (self->gui);

	{
		const gchar *labels[] = { "Edit Keybindings", "Close", NULL };

		buttons_right (self, labels);
		if (lrg_gui_button (self->gui, "Edit Keybindings"))
		{
			lrg_gui_end_popup (self->gui);
			lrg_gui_close_all_popups (self->gui);
			editor_dialog_open (self, EDITOR_DIALOG_PREFERENCES);
			self->prefs_tab = 3;
			return;
		}
		lrg_gui_same_line (self->gui, -1.0f);
		if (lrg_gui_button (self->gui, "Close"))
			close_dialog (self);
	}
	lrg_gui_end_popup (self->gui);
}

/* ==========================================================================
 * Command palette and quick open
 * ========================================================================== */

static void
collect_files (GPtrArray   *out,
               const gchar *root,
               const gchar *rel)
{
	g_autofree gchar *dir = (rel != NULL) ? g_build_filename (root, rel, NULL) : g_strdup (root);
	g_autoptr(GDir)   d = g_dir_open (dir, 0, NULL);
	const gchar      *name;

	if (d == NULL)
		return;

	while ((name = g_dir_read_name (d)) != NULL && out->len < QUICK_OPEN_LIMIT)
	{
		g_autofree gchar *child_rel = (rel != NULL) ? g_build_filename (rel, name, NULL) : g_strdup (name);
		g_autofree gchar *child = g_build_filename (root, child_rel, NULL);

		/* Skip hidden folders, VCS data and build trees. */
		if (name[0] == '.' || g_strcmp0 (name, "build") == 0 || g_strcmp0 (name, "node_modules") == 0 ||
		    g_strcmp0 (name, "trees") == 0)
			continue;
		if (g_file_test (child, G_FILE_TEST_IS_DIR))
			collect_files (out, root, child_rel);
		else
			g_ptr_array_add (out, g_steal_pointer (&child_rel));
	}
}

void
editor_palette_open (LrgEditorApp *self,
                     gboolean      quick_open)
{
	if (self->dialog != EDITOR_DIALOG_NONE)
		return;

	self->palette_open = TRUE;
	self->quick_open = quick_open;
	self->palette_index = 0;
	self->dialog_just_opened = TRUE;
	g_string_truncate (self->palette_query, 0);

	if (quick_open && self->quick_files->len == 0 && editor_app_base_dir (self) != NULL)
		collect_files (self->quick_files, editor_app_base_dir (self), NULL);
}

typedef struct
{
	gint         score;
	const gchar *text;    /* action id, file or node GUID */
	LrgNode     *node;
} PaletteHit;

static gint
compare_hits (gconstpointer a,
              gconstpointer b)
{
	const PaletteHit *x = a;
	const PaletteHit *y = b;

	if (x->score != y->score)
		return y->score - x->score;
	return g_strcmp0 (x->text, y->text);
}

static gboolean
collect_node_hit (LrgNode  *node,
                  gpointer  user_data)
{
	gpointer    *data = user_data;
	GArray      *hits = data[0];
	const gchar *query = data[1];
	PaletteHit   hit;

	if (lrg_node_get_parent (node) == NULL || lrg_node_get_name (node) == NULL)
		return TRUE;
	hit.score = (*query == '\0') ? 1 : lrg_editor_fuzzy_score (query, lrg_node_get_name (node));
	if (hit.score <= 0)
		return TRUE;
	hit.text = lrg_node_get_guid (node);
	hit.node = node;
	g_array_append_val (hits, hit);
	return TRUE;
}

static void
draw_palette (LrgEditorApp *self)
{
	LrgGuiStyle      *style = lrg_gui_context_get_style (self->gui);
	const gchar      *title = "##palette";
	g_autoptr(GArray) hits = g_array_new (FALSE, FALSE, sizeof (PaletteHit));
	const gchar      *query;
	gboolean          node_mode;
	gboolean          run = FALSE;
	gint              i, n;

	if (!lrg_gui_is_popup_open (self->gui, title))
	{
		if (!self->dialog_just_opened)
		{
			self->palette_open = FALSE;
			return;
		}
		lrg_gui_open_popup (self->gui, title);
	}
	lrg_gui_set_next_popup_size (self->gui, MIN (PALETTE_WIDTH, self->width - 40.0f), 0.0f);
	lrg_gui_set_next_popup_pos (self->gui, roundf ((self->width - MIN (PALETTE_WIDTH, self->width - 40.0f)) * 0.5f),
	                            EDITOR_MENUBAR_H + 8.0f);
	if (!lrg_gui_begin_popup (self->gui, title))
	{
		self->palette_open = FALSE;
		return;
	}

	if (self->dialog_just_opened)
		lrg_gui_request_focus (self->gui, "##palette-query");
	self->dialog_just_opened = FALSE;
	lrg_gui_set_next_item_width (self->gui, -1.0f);
	if (lrg_gui_input_text (self->gui, "##palette-query",
	                        self->quick_open ? "Open file by name..." : "Run a command... (@ jumps to a node)",
	                        self->palette_query, LRG_GUI_INPUT_TEXT_KEEP_FOCUS))
		self->palette_index = 0;

	/* Gather and rank. */
	query = self->palette_query->str;
	node_mode = !self->quick_open && query[0] == '@';
	if (self->quick_open)
	{
		guint k;

		for (k = 0; k < self->quick_files->len; k++)
		{
			PaletteHit hit;

			hit.text = g_ptr_array_index (self->quick_files, k);
			hit.node = NULL;
			hit.score = (*query == '\0') ? 1 : lrg_editor_fuzzy_score (query, hit.text);
			if (hit.score > 0)
				g_array_append_val (hits, hit);
		}
		g_array_sort (hits, compare_hits);
	}
	else if (node_mode)
	{
		gpointer data[2] = { hits, (gpointer) (query + 1) };

		lrg_node_foreach (lrg_level_get_root (lrg_editor_get_level (self->editor)), collect_node_hit, data);
		g_array_sort (hits, compare_hits);
	}
	else
	{
		GList *ids = lrg_editor_actions_search (self->actions, query);
		GList *l;

		/* Already ranked by the registry. */
		for (l = ids; l != NULL; l = l->next)
		{
			PaletteHit hit;

			hit.text = l->data;
			hit.node = NULL;
			hit.score = 1;
			g_array_append_val (hits, hit);
		}
		g_list_free (ids);
	}

	/* Keyboard navigation. */
	n = (gint) hits->len;
	if (lrg_gui_is_key_pressed (self->gui, LRG_GUI_KEY_DOWN, TRUE))
		self->palette_index = (n > 0) ? (self->palette_index + 1) % n : 0;
	if (lrg_gui_is_key_pressed (self->gui, LRG_GUI_KEY_UP, TRUE))
		self->palette_index = (n > 0) ? (self->palette_index + n - 1) % n : 0;
	if (lrg_gui_is_key_pressed (self->gui, LRG_GUI_KEY_ENTER, FALSE))
		run = TRUE;
	if (lrg_gui_is_key_pressed (self->gui, LRG_GUI_KEY_ESCAPE, FALSE))
	{
		self->palette_open = FALSE;
		lrg_gui_close_current_popup (self->gui);
		lrg_gui_end_popup (self->gui);
		return;
	}
	self->palette_index = CLAMP (self->palette_index, 0, MAX (n - 1, 0));

	/* Rows: a window of PALETTE_ROWS around the highlighted one. */
	{
		gint first = MAX (0, MIN (self->palette_index - PALETTE_ROWS / 2, n - PALETTE_ROWS));

		for (i = first; i < n && i < first + PALETTE_ROWS; i++)
		{
			PaletteHit       *hit = &g_array_index (hits, PaletteHit, i);
			g_autofree gchar *label = NULL;
			const gchar      *right = NULL;

			if (self->quick_open)
				label = g_strdup_printf ("%s  %s##q%d", editor_asset_glyph (lrg_asset_database_classify (hit->text), FALSE),
				                         hit->text, i);
			else if (node_mode)
			{
				g_autofree gchar *path = lrg_node_get_path (hit->node);

				label = g_strdup_printf ("%s##n%d", path, i);
			}
			else
			{
				label = g_strdup_printf ("%s: %s##a%d", lrg_editor_actions_get_category (self->actions, hit->text),
				                         lrg_editor_actions_get_label (self->actions, hit->text), i);
				right = lrg_editor_actions_get_shortcut (self->actions, hit->text);
			}

			if (lrg_gui_selectable (self->gui, label, i == self->palette_index,
			                        (!self->quick_open && !node_mode &&
			                         !lrg_editor_actions_is_enabled (self->actions, hit->text))
			                        ? LRG_GUI_SELECTABLE_DISABLED : LRG_GUI_SELECTABLE_NONE))
			{
				self->palette_index = i;
				run = TRUE;
			}
			if (i == self->palette_index)
				lrg_gui_scroll_to_item (self->gui, 0.5f);
			if (right != NULL)
			{
				LrgGuiRect r;
				gfloat     tw = lrg_gui_calc_text_width (self->gui, right);

				lrg_gui_get_item_rect (self->gui, &r);
				lrg_gui_draw_list_text (lrg_gui_get_draw_list (self->gui), r.x + r.w - tw - 8.0f,
				                        r.y + (r.h - style->font_size) * 0.5f, right, -1, style->font_size,
				                        style->colors[LRG_GUI_COL_TEXT_DISABLED], FALSE);
			}
		}
		if (n == 0)
			lrg_gui_text_disabled (self->gui, "No matches");
		else if (n > PALETTE_ROWS)
			lrg_gui_textf (self->gui, "%d more...", n - PALETTE_ROWS);
	}

	if (run && n > 0)
	{
		PaletteHit       *hit = &g_array_index (hits, PaletteHit, self->palette_index);
		g_autofree gchar *text = g_strdup (hit->text);
		LrgNode          *node = hit->node;
		gboolean          quick = self->quick_open;

		self->palette_open = FALSE;
		lrg_gui_close_current_popup (self->gui);
		lrg_gui_end_popup (self->gui);

		if (quick)
			editor_app_open_asset (self, text);
		else if (node != NULL)
		{
			lrg_editor_select (self->editor, node, FALSE);
			editor_app_frame_selection (self);
		}
		else
			lrg_editor_actions_activate (self->actions, text);
		return;
	}

	lrg_gui_end_popup (self->gui);
}

/* ==========================================================================
 * Dispatch
 * ========================================================================== */

void
editor_dialogs_draw (LrgEditorApp *self)
{
	if (self->palette_open)
	{
		draw_palette (self);
		return;
	}

	switch (self->dialog)
	{
	case EDITOR_DIALOG_PREFERENCES:      draw_preferences (self); break;
	case EDITOR_DIALOG_PROJECT_SETTINGS: draw_project_settings (self); break;
	case EDITOR_DIALOG_FILE:             draw_file_dialog (self); break;
	case EDITOR_DIALOG_CONFIRM:          draw_confirm (self); break;
	case EDITOR_DIALOG_NEW_PROJECT:      draw_new_project (self); break;
	case EDITOR_DIALOG_NEW_SCRIPT:       draw_new_script (self); break;
	case EDITOR_DIALOG_ABOUT:            draw_about (self); break;
	case EDITOR_DIALOG_SHORTCUTS:        draw_shortcuts (self); break;
	case EDITOR_DIALOG_RECOVER:          draw_recover (self); break;
	default:                             break;
	}
}
