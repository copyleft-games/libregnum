/* lrg-editor-panel-assets.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The Assets panel: a file browser rooted at the project. Breadcrumbs,
 * search, a type filter, list or grid view, double-click to open (levels
 * and prefabs in the editor, code in the external editor, media in the
 * desktop application), drag files into the viewport/hierarchy/inspector,
 * and context menus to rename, duplicate, trash, reveal and create
 * folders, levels and scripts. The directory is watched and rescanned
 * when files change.
 */

#include "lrg-editor-app-private.h"
#include "../lrg-asset-database.h"
#include <glib/gstdio.h>
#include <string.h>

/* ==========================================================================
 * Directory listing
 * ========================================================================== */

void
editor_file_entry_free (gpointer data)
{
	EditorFileEntry *e = data;

	g_free (e->name);
	g_free (e->path);
	g_free (e);
}

static gint
compare_entries (gconstpointer a,
                 gconstpointer b)
{
	const EditorFileEntry *x = *(EditorFileEntry * const *) a;
	const EditorFileEntry *y = *(EditorFileEntry * const *) b;

	/* Folders first, then case-insensitive natural order. */
	if (x->is_dir != y->is_dir)
		return x->is_dir ? -1 : 1;
	return g_utf8_collate (x->name, y->name);
}

GPtrArray *
editor_list_dir (const gchar *dir,
                 const gchar *ext,
                 gboolean     dirs_only)
{
	g_autoptr(GDir) d = NULL;
	GPtrArray      *out = g_ptr_array_new_with_free_func (editor_file_entry_free);
	const gchar    *name;

	if (dir == NULL || (d = g_dir_open (dir, 0, NULL)) == NULL)
		return out;

	while ((name = g_dir_read_name (d)) != NULL)
	{
		EditorFileEntry *e;
		GStatBuf         st;
		g_autofree gchar *path = NULL;

		/* Hidden files and the editor's own state stay out of sight. */
		if (name[0] == '.')
			continue;

		path = g_build_filename (dir, name, NULL);
		if (g_stat (path, &st) != 0)
			continue;

		e = g_new0 (EditorFileEntry, 1);
		e->name = g_strdup (name);
		e->path = g_steal_pointer (&path);
		e->is_dir = S_ISDIR (st.st_mode);
		e->size = st.st_size;
		e->mtime = st.st_mtime;
		e->type = e->is_dir ? LRG_ASSET_TYPE_UNKNOWN : (gint) lrg_asset_database_classify (e->path);

		if ((dirs_only && !e->is_dir) ||
		    (!e->is_dir && ext != NULL && !g_str_has_suffix (name, ext)))
		{
			editor_file_entry_free (e);
			continue;
		}
		g_ptr_array_add (out, e);
	}

	g_ptr_array_sort (out, compare_entries);
	return out;
}

const gchar *
editor_asset_glyph (gint     type,
                    gboolean is_dir)
{
	/* Letters, not symbols: the default UI font (e.g. Noto Sans) has no
	 * geometric-shape glyphs and would draw them as "?". */
	if (is_dir)
		return "+";

	switch (type)
	{
	case LRG_ASSET_TYPE_LEVEL:   return "L";
	case LRG_ASSET_TYPE_PREFAB:  return "P";
	case LRG_ASSET_TYPE_SCRIPT:  return "S";
	case LRG_ASSET_TYPE_TEXTURE: return "T";
	case LRG_ASSET_TYPE_MODEL:   return "M";
	case LRG_ASSET_TYPE_AUDIO:   return "A";
	case LRG_ASSET_TYPE_FONT:    return "F";
	default:                     return "\xc2\xb7";   /* middle dot (Latin-1) */
	}
}

static LrgGuiStyleColor
type_color (gint     type,
            gboolean is_dir)
{
	if (is_dir)
		return LRG_GUI_COL_WARNING;
	switch (type)
	{
	case LRG_ASSET_TYPE_LEVEL:
	case LRG_ASSET_TYPE_PREFAB:
		return LRG_GUI_COL_ACCENT;
	case LRG_ASSET_TYPE_SCRIPT:
		return LRG_GUI_COL_SUCCESS;
	case LRG_ASSET_TYPE_TEXTURE:
	case LRG_ASSET_TYPE_MODEL:
		return LRG_GUI_COL_AXIS_Z;
	case LRG_ASSET_TYPE_AUDIO:
		return LRG_GUI_COL_AXIS_X;
	default:
		return LRG_GUI_COL_TEXT_DISABLED;
	}
}

static void
on_dir_changed (GFileMonitor      *monitor,
                GFile             *file,
                GFile             *other,
                GFileMonitorEvent  event,
                gpointer           user_data)
{
	LrgEditorApp *self = LRG_EDITOR_APP (user_data);

	self->assets_dirty = TRUE;
	g_ptr_array_set_size (self->quick_files, 0);
}

void
editor_assets_set_dir (LrgEditorApp *self,
                       const gchar  *dir)
{
	g_autoptr(GFile) file = NULL;

	if (dir == NULL)
		return;

	g_free (self->asset_dir);
	self->asset_dir = g_canonicalize_filename (dir, NULL);
	self->assets_dirty = TRUE;

	if (self->asset_monitor != NULL)
	{
		g_file_monitor_cancel (self->asset_monitor);
		g_signal_handlers_disconnect_by_data (self->asset_monitor, self);
		g_clear_object (&self->asset_monitor);
	}
	file = g_file_new_for_path (self->asset_dir);
	self->asset_monitor = g_file_monitor_directory (file, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
	if (self->asset_monitor != NULL)
		g_signal_connect (self->asset_monitor, "changed", G_CALLBACK (on_dir_changed), self);
}

void
editor_assets_refresh (LrgEditorApp *self)
{
	g_ptr_array_unref (self->asset_entries);
	self->asset_entries = editor_list_dir (self->asset_dir, NULL, FALSE);
	self->assets_dirty = FALSE;
}

/* ==========================================================================
 * Operations
 * ========================================================================== */

static void
trash_entry (LrgEditorApp          *self,
             const EditorFileEntry *e)
{
	g_autoptr(GFile)  file = g_file_new_for_path (e->path);
	g_autoptr(GError) error = NULL;

	/* Trash, never delete: recoverable from the desktop's trash. */
	if (!g_file_trash (file, NULL, &error))
		editor_app_notify (self, 3, "Could not move %s to the trash: %s", e->name, error->message);
	else
		editor_app_notify (self, 0, "Moved %s to the trash", e->name);
	self->assets_dirty = TRUE;
}

static void
duplicate_entry (LrgEditorApp          *self,
                 const EditorFileEntry *e)
{
	g_autoptr(GFile)  src = g_file_new_for_path (e->path);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = g_path_get_dirname (e->path);
	g_autofree gchar *stem = g_strdup (e->name);
	gchar            *dot = strrchr (stem, '.');
	g_autofree gchar *ext = (dot != NULL) ? g_strdup (dot) : g_strdup ("");
	guint             n;

	if (e->is_dir)
		return;
	if (dot != NULL)
		*dot = '\0';

	for (n = 2; n < 1000; n++)
	{
		g_autofree gchar *name = g_strdup_printf ("%s%u%s", stem, n, ext);
		g_autofree gchar *path = g_build_filename (dir, name, NULL);
		g_autoptr(GFile)  dst = NULL;

		if (g_file_test (path, G_FILE_TEST_EXISTS))
			continue;
		dst = g_file_new_for_path (path);
		if (!g_file_copy (src, dst, G_FILE_COPY_NONE, NULL, NULL, NULL, &error))
			editor_app_notify (self, 3, "Duplicate failed: %s", error->message);
		break;
	}
	self->assets_dirty = TRUE;
}

static void
rename_entry (LrgEditorApp *self,
              const gchar  *path,
              const gchar  *new_name)
{
	g_autofree gchar *dir = g_path_get_dirname (path);
	g_autofree gchar *target = g_build_filename (dir, new_name, NULL);

	if (strchr (new_name, G_DIR_SEPARATOR) != NULL || *new_name == '\0')
	{
		editor_app_notify (self, 2, "Invalid name \"%s\"", new_name);
		return;
	}
	if (g_file_test (target, G_FILE_TEST_EXISTS))
	{
		editor_app_notify (self, 2, "%s already exists", new_name);
		return;
	}
	if (g_rename (path, target) != 0)
		editor_app_notify (self, 3, "Rename failed: %s", g_strerror (errno));
	else
	{
		/* Keep the open level's path in sync. */
		if (g_strcmp0 (lrg_editor_get_path (self->editor), path) == 0)
			lrg_editor_set_path (self->editor, target);
		editor_app_set_status (self, "Renamed to %s (references to the old name are not updated)", new_name);
	}
	self->assets_dirty = TRUE;
}

static void
reveal (LrgEditorApp *self,
        const gchar  *path,
        gboolean      is_dir)
{
	g_autofree gchar *dir = is_dir ? g_strdup (path) : g_path_get_dirname (path);
	g_autofree gchar *uri = g_filename_to_uri (dir, NULL, NULL);
	g_autoptr(GError) error = NULL;

	if (uri == NULL || !g_app_info_launch_default_for_uri (uri, NULL, &error))
		editor_app_notify (self, 3, "Cannot open the file manager%s%s",
		                   error != NULL ? ": " : "", error != NULL ? error->message : "");
}

static void
new_folder (LrgEditorApp *self)
{
	guint n;

	for (n = 1; n < 1000; n++)
	{
		g_autofree gchar *name = (n == 1) ? g_strdup ("New Folder") : g_strdup_printf ("New Folder %u", n);
		g_autofree gchar *path = g_build_filename (self->asset_dir, name, NULL);

		if (g_file_test (path, G_FILE_TEST_EXISTS))
			continue;
		if (g_mkdir (path, 0755) == 0)
		{
			g_free (self->asset_rename);
			self->asset_rename = g_strdup (path);
			self->asset_rename_focus = TRUE;
			g_string_assign (self->asset_rename_buf, name);
		}
		break;
	}
	self->assets_dirty = TRUE;
}

static void
new_level_file (LrgEditorApp *self)
{
	guint n;

	for (n = 1; n < 1000; n++)
	{
		g_autofree gchar *name = (n == 1) ? g_strdup ("new_level.rlevel") : g_strdup_printf ("new_level%u.rlevel", n);
		g_autofree gchar *path = g_build_filename (self->asset_dir, name, NULL);
		g_autoptr(GError) error = NULL;

		if (g_file_test (path, G_FILE_TEST_EXISTS))
			continue;
		if (!g_file_set_contents (path, "type: level\nname: new_level\ndefault_view: 3d\nnodes: []\n", -1, &error))
			editor_app_notify (self, 3, "Cannot create a level: %s", error->message);
		else
		{
			g_free (self->asset_rename);
			self->asset_rename = g_strdup (path);
			self->asset_rename_focus = TRUE;
			g_string_assign (self->asset_rename_buf, name);
		}
		break;
	}
	self->assets_dirty = TRUE;
}

static void
entry_menu (LrgEditorApp          *self,
            const EditorFileEntry *e)
{
	if (lrg_gui_menu_item (self->gui, e->is_dir ? "Open Folder" : "Open", "Enter", FALSE, TRUE))
	{
		if (e->is_dir)
			editor_assets_set_dir (self, e->path);
		else
			editor_app_open_asset (self, e->path);
	}
	if (!e->is_dir && lrg_gui_menu_item (self->gui, "Open in External Editor", NULL, FALSE, TRUE))
		editor_app_open_file (self, e->path, 1, 1);
	if (!e->is_dir && (e->type == LRG_ASSET_TYPE_PREFAB || e->type == LRG_ASSET_TYPE_MODEL) &&
	    lrg_gui_menu_item (self->gui, "Place in Scene", NULL, FALSE, TRUE))
	{
		g_autofree gchar *rel = editor_app_relative_path (self, e->path);

		editor_viewport_drop_asset (self, rel, self->viewport_rect.w * 0.5f, self->viewport_rect.h * 0.5f);
	}
	lrg_gui_separator (self->gui);
	if (lrg_gui_menu_item (self->gui, "Rename", "F2", FALSE, TRUE))
	{
		g_free (self->asset_rename);
		self->asset_rename = g_strdup (e->path);
		self->asset_rename_focus = TRUE;
		g_string_assign (self->asset_rename_buf, e->name);
	}
	if (!e->is_dir && lrg_gui_menu_item (self->gui, "Duplicate", NULL, FALSE, TRUE))
		duplicate_entry (self, e);
	if (lrg_gui_menu_item (self->gui, "Move to Trash", "Delete", FALSE, TRUE))
		trash_entry (self, e);
	lrg_gui_separator (self->gui);
	if (lrg_gui_menu_item (self->gui, "Copy Path", NULL, FALSE, TRUE))
	{
		g_autofree gchar *rel = editor_app_relative_path (self, e->path);

		lrg_gui_context_set_clipboard (self->gui, rel);
	}
	if (lrg_gui_menu_item (self->gui, "Show in File Manager", NULL, FALSE, TRUE))
		reveal (self, e->path, e->is_dir);
}

static void
background_menu (LrgEditorApp *self)
{
	if (lrg_gui_menu_item (self->gui, "New Folder", NULL, FALSE, TRUE))
		new_folder (self);
	if (lrg_gui_menu_item (self->gui, "New Level", NULL, FALSE, TRUE))
		new_level_file (self);
	if (lrg_gui_menu_item (self->gui, "New Script...", NULL, FALSE, TRUE))
	{
		g_clear_pointer (&self->file_target_guid, g_free);
		editor_dialog_open (self, EDITOR_DIALOG_NEW_SCRIPT);
	}
	lrg_gui_separator (self->gui);
	if (lrg_gui_menu_item (self->gui, "Refresh", NULL, FALSE, TRUE))
		self->assets_dirty = TRUE;
	if (lrg_gui_menu_item (self->gui, "Show in File Manager", NULL, FALSE, TRUE))
		reveal (self, self->asset_dir, TRUE);
	if (lrg_gui_menu_item (self->gui, "Grid View", NULL, self->asset_grid, TRUE))
		self->asset_grid = !self->asset_grid;
}

/* ==========================================================================
 * Panel
 * ========================================================================== */

static const gchar *type_filters[] = {
	"All", "Textures", "Models", "Audio", "Fonts", "Scripts", "Levels", "Prefabs", "Tilesets", "Scenes", "CAD"
};

static gboolean
entry_visible (LrgEditorApp          *self,
               const EditorFileEntry *e)
{
	if (self->asset_type_filter > 0 && !e->is_dir && e->type != self->asset_type_filter)
		return FALSE;
	if (self->asset_type_filter > 0 && e->is_dir && self->asset_filter->len == 0)
		return TRUE;
	if (self->asset_filter->len > 0 && lrg_editor_fuzzy_score (self->asset_filter->str, e->name) == 0)
		return FALSE;
	return TRUE;
}

/* One entry: selection, open, drag, rename, context menu. */
static void
entry_interactions (LrgEditorApp    *self,
                    EditorFileEntry *e)
{
	if (lrg_gui_is_item_clicked (self->gui, LRG_GUI_MOUSE_LEFT) ||
	    lrg_gui_is_item_clicked (self->gui, LRG_GUI_MOUSE_RIGHT))
	{
		g_free (self->asset_selected);
		self->asset_selected = g_strdup (e->path);
	}
	if (lrg_gui_is_item_double_clicked (self->gui))
	{
		if (e->is_dir)
			editor_assets_set_dir (self, e->path);
		else
			editor_app_open_asset (self, e->path);
	}
	if (!e->is_dir && lrg_gui_begin_drag_source (self->gui))
	{
		g_autofree gchar *rel = editor_app_relative_path (self, e->path);

		lrg_gui_set_drag_payload (self->gui, "asset", rel, e->name);
		lrg_gui_end_drag_source (self->gui);
	}
	{
		g_autofree gchar *tip = g_strdup_printf ("%s\n%" G_GOFFSET_FORMAT " bytes", e->path, e->size);

		lrg_gui_set_item_tooltip (self->gui, tip);
	}
	if (lrg_gui_begin_popup_context_item (self->gui, NULL))
	{
		entry_menu (self, e);
		lrg_gui_end_popup (self->gui);
	}
}

void
editor_panel_assets (LrgEditorApp *self)
{
	LrgGuiStyle *style = lrg_gui_context_get_style (self->gui);
	const gchar *root = (self->project_dir != NULL) ? self->project_dir : NULL;
	gfloat       avail_w;
	guint        i;

	if (self->assets_dirty)
		editor_assets_refresh (self);

	/* Breadcrumbs from the project root to the current directory. */
	{
		const gchar      *start = (root != NULL && g_str_has_prefix (self->asset_dir, root)) ? root : "/";
		g_autofree gchar *rel = g_strdup (self->asset_dir + strlen (start));
		g_auto(GStrv)     parts = g_strsplit (rel, G_DIR_SEPARATOR_S, -1);
		g_autofree gchar *acc = g_strdup (start);
		g_autofree gchar *root_label = (root != NULL) ? g_path_get_basename (root) : g_strdup ("/");

		if (lrg_gui_button (self->gui, root_label))
			editor_assets_set_dir (self, start);
		for (i = 0; parts[i] != NULL; i++)
		{
			gchar *next;

			if (*parts[i] == '\0')
				continue;
			next = g_build_filename (acc, parts[i], NULL);
			g_free (acc);
			acc = next;
			lrg_gui_same_line (self->gui, 2.0f);
			lrg_gui_text_disabled (self->gui, "/");
			lrg_gui_same_line (self->gui, 2.0f);
			lrg_gui_push_id_int (self->gui, i);
			if (lrg_gui_button (self->gui, parts[i]))
				editor_assets_set_dir (self, acc);
			lrg_gui_pop_id (self->gui);
		}
	}

	/* Search, type filter, view mode. */
	lrg_gui_get_content_avail (self->gui, &avail_w, NULL);
	lrg_gui_set_next_item_width (self->gui, avail_w * 0.45f);
	lrg_gui_input_text (self->gui, "##asset-filter", "Search files", self->asset_filter, 0);
	lrg_gui_same_line (self->gui, -1.0f);
	lrg_gui_set_next_item_width (self->gui, 110.0f);
	lrg_gui_combo (self->gui, "##asset-type", &self->asset_type_filter, type_filters, G_N_ELEMENTS (type_filters));
	lrg_gui_same_line (self->gui, -1.0f);
	if (lrg_gui_button (self->gui, self->asset_grid ? "List##view" : "Grid##view"))
		self->asset_grid = !self->asset_grid;
	lrg_gui_same_line (self->gui, -1.0f);
	if (lrg_gui_button (self->gui, "+##asset-new"))
		lrg_gui_open_popup (self->gui, "##asset-new-menu");
	if (lrg_gui_begin_popup (self->gui, "##asset-new-menu"))
	{
		background_menu (self);
		lrg_gui_end_popup (self->gui);
	}

	lrg_gui_begin_child (self->gui, "##asset-list", 0.0f, 0.0f, LRG_GUI_REGION_SCROLL_Y | LRG_GUI_REGION_NO_PADDING);

	/* Up one level (not above the project root). */
	if (root == NULL || g_strcmp0 (self->asset_dir, root) != 0)
	{
		if (lrg_gui_selectable (self->gui, "..", FALSE, 0) || lrg_gui_is_item_double_clicked (self->gui))
		{
			g_autofree gchar *up = g_path_get_dirname (self->asset_dir);

			editor_assets_set_dir (self, up);
		}
		if (lrg_gui_accept_drop (self->gui, "asset") != NULL)
			editor_app_set_status (self, "Moving files between folders: use the file manager");
	}

	if (self->asset_grid)
	{
		gfloat cell = style->font_size * 6.0f;
		gfloat x0, y0, w;
		guint  per_row, col = 0;

		lrg_gui_get_cursor (self->gui, &x0, &y0);
		lrg_gui_get_content_avail (self->gui, &w, NULL);
		per_row = MAX (1, (guint) (w / (cell + style->item_spacing_x)));

		for (i = 0; i < self->asset_entries->len; i++)
		{
			EditorFileEntry *e = g_ptr_array_index (self->asset_entries, i);
			LrgGuiRect       r;
			gboolean         selected = g_strcmp0 (self->asset_selected, e->path) == 0;

			if (!entry_visible (self, e))
				continue;
			if (col > 0)
				lrg_gui_same_line (self->gui, -1.0f);
			lrg_gui_push_id (self->gui, e->name);
			lrg_gui_invisible_button (self->gui, "##cell", cell, cell);
			lrg_gui_get_item_rect (self->gui, &r);
			if (selected || lrg_gui_is_item_hovered (self->gui))
				lrg_gui_draw_list_rect_filled (lrg_gui_get_draw_list (self->gui), &r,
				                               style->colors[selected ? LRG_GUI_COL_HEADER : LRG_GUI_COL_HEADER_HOVERED],
				                               style->rounding);
			{
				LrgGuiDrawList *dl = lrg_gui_get_draw_list (self->gui);
				const gchar    *glyph = editor_asset_glyph (e->type, e->is_dir);
				LrgGuiRect      label = { r.x + 4.0f, r.y + r.h - style->font_size - 6.0f, r.w - 8.0f,
				                          style->font_size + 2.0f };

				lrg_gui_draw_list_text (dl, r.x + r.w * 0.5f - style->font_size, r.y + r.h * 0.25f, glyph, -1,
				                        style->font_size * 2.0f, style->colors[type_color (e->type, e->is_dir)], FALSE);
				lrg_gui_draw_list_push_clip (dl, &label);
				lrg_gui_draw_list_text (dl, label.x, label.y, e->name, -1, style->font_size * 0.9f,
				                        style->colors[LRG_GUI_COL_TEXT], FALSE);
				lrg_gui_draw_list_pop_clip (dl);
			}
			entry_interactions (self, e);
			lrg_gui_pop_id (self->gui);
			col = (col + 1) % per_row;
		}
	}
	else
	{
		for (i = 0; i < self->asset_entries->len; i++)
		{
			EditorFileEntry  *e = g_ptr_array_index (self->asset_entries, i);
			gboolean          selected = g_strcmp0 (self->asset_selected, e->path) == 0;
			g_autofree gchar *label = NULL;
			LrgGuiRect        r;

			if (!entry_visible (self, e))
				continue;

			lrg_gui_push_id (self->gui, e->name);
			if (self->asset_rename != NULL && g_strcmp0 (self->asset_rename, e->path) == 0)
			{
				gboolean requested = self->asset_rename_focus;

				if (requested)
				{
					lrg_gui_set_keyboard_focus_here (self->gui);
					self->asset_rename_focus = FALSE;
				}
				if (lrg_gui_input_text (self->gui, "##asset-rename", NULL, self->asset_rename_buf,
				                        LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE | LRG_GUI_INPUT_TEXT_AUTO_SELECT_ALL))
				{
					if (g_strcmp0 (self->asset_rename_buf->str, e->name) != 0)
						rename_entry (self, e->path, self->asset_rename_buf->str);
					g_clear_pointer (&self->asset_rename, g_free);
				}
				else if (!requested && !lrg_gui_context_wants_keyboard (self->gui))
				{
					/* Escape or a click elsewhere cancels the rename. */
					g_clear_pointer (&self->asset_rename, g_free);
				}
				lrg_gui_pop_id (self->gui);
				continue;
			}

			label = g_strdup_printf ("   %s", e->name);
			lrg_gui_selectable (self->gui, label, selected, 0);
			lrg_gui_get_item_rect (self->gui, &r);
			lrg_gui_draw_list_text (lrg_gui_get_draw_list (self->gui), r.x + style->frame_padding_x,
			                        r.y + style->frame_padding_y, editor_asset_glyph (e->type, e->is_dir), -1,
			                        style->font_size, style->colors[type_color (e->type, e->is_dir)], FALSE);
			entry_interactions (self, e);
			lrg_gui_pop_id (self->gui);
		}
	}

	if (self->asset_entries->len == 0)
		lrg_gui_text_disabled (self->gui, "This folder is empty.");

	/* Keyboard on the selected entry. */
	if (self->asset_selected != NULL && !lrg_gui_context_wants_keyboard (self->gui))
	{
		LrgGuiRect region;
		gfloat     mx, my;

		lrg_gui_get_region_rect (self->gui, &region);
		lrg_gui_get_mouse (self->gui, &mx, &my);
		if (mx >= region.x && mx < region.x + region.w && my >= region.y && my < region.y + region.h)
		{
			for (i = 0; i < self->asset_entries->len; i++)
			{
				EditorFileEntry *e = g_ptr_array_index (self->asset_entries, i);

				if (g_strcmp0 (e->path, self->asset_selected) != 0)
					continue;
				if (lrg_gui_is_key_pressed (self->gui, LRG_GUI_KEY_ENTER, FALSE))
				{
					if (e->is_dir)
						editor_assets_set_dir (self, e->path);
					else
						editor_app_open_asset (self, e->path);
				}
				else if (lrg_gui_is_key_pressed (self->gui, LRG_GUI_KEY_F2, FALSE))
				{
					g_free (self->asset_rename);
					self->asset_rename = g_strdup (e->path);
					self->asset_rename_focus = TRUE;
					g_string_assign (self->asset_rename_buf, e->name);
				}
				break;
			}
		}
	}

	/* Empty area: background menu. */
	{
		gfloat rest;

		lrg_gui_get_content_avail (self->gui, NULL, &rest);
		lrg_gui_invisible_button (self->gui, "##asset-empty", avail_w, MAX (16.0f, rest));
		if (lrg_gui_begin_popup_context_item (self->gui, "##asset-bg-menu"))
		{
			background_menu (self);
			lrg_gui_end_popup (self->gui);
		}
	}

	lrg_gui_end_region (self->gui);
}
