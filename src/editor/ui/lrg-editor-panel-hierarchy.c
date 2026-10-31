/* lrg-editor-panel-hierarchy.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The Hierarchy (outliner) panel: the level's node tree with filtering,
 * multi-selection (Ctrl toggles, Shift selects a range), inline rename,
 * visibility and lock toggles, drag and drop to reparent/reorder (and to
 * drop assets onto nodes), context menus and keyboard navigation. The
 * selected node is revealed (ancestors opened, scrolled into view) when
 * the selection changes elsewhere: viewport picks, Emacs, the palette.
 */

#include "lrg-editor-app-private.h"
#include "../lrg-node-private.h"
#include <string.h>

static gpointer
ref_copy (gconstpointer src,
          gpointer      data)
{
	return g_object_ref ((gpointer) src);
}

typedef struct
{
	gchar      *needle;    /* casefolded filter, or NULL */
	GHashTable *shown;     /* nodes to show while filtering */
	gfloat      view_y0;   /* rows entirely outside [view_y0, view_y1) */
	gfloat      view_y1;   /* are culled (see draw_row_culled()) */
} Filter;

/* Marks @node (and returns TRUE) if it or a descendant matches. */
static gboolean
mark_matches (LrgNode *node,
              Filter  *f)
{
	GPtrArray *children = lrg_node_get_children (node);
	gboolean   any = FALSE;
	guint      i;

	for (i = 0; i < children->len; i++)
		if (mark_matches (g_ptr_array_index (children, i), f))
			any = TRUE;

	if (!any)
	{
		const gchar      *name = lrg_node_get_name (node);
		g_autofree gchar *folded = (name != NULL) ? g_utf8_casefold (name, -1) : NULL;

		any = (folded != NULL && strstr (folded, f->needle) != NULL) ||
		      g_strcmp0 (lrg_node_get_guid (node), f->needle) == 0;
	}

	if (any)
		g_hash_table_add (f->shown, node);
	return any;
}

/*
 * editor_app_count_nodes:
 *
 * lrg_level_count_nodes() of the edited level, walked only when the node
 * tree changed (the status bar, the outliner header and the viewport ask
 * every frame).
 */
guint
editor_app_count_nodes (LrgEditorApp *self)
{
	LrgLevel *level = lrg_editor_get_level (self->editor);
	LrgNode  *root = lrg_level_get_root (level);
	guint     serial = lrg_node_get_tree_serial ();

	if (self->node_count_serial != serial || self->node_count_root != root)
	{
		self->node_count = lrg_level_count_nodes (level);
		self->node_count_serial = serial;
		self->node_count_root = root;
	}
	return self->node_count;
}

/*
 * filter_shown:
 *
 * The set of nodes to show for @needle (matches and their ancestors),
 * rebuilt only when the filter text or the node tree changed.
 */
static GHashTable *
filter_shown (LrgEditorApp *self,
              LrgNode      *root,
              gchar        *needle)
{
	guint serial = lrg_node_get_tree_serial ();

	if (self->hierarchy_shown == NULL)
		self->hierarchy_shown = g_hash_table_new (g_direct_hash, g_direct_equal);

	if (self->hierarchy_shown_serial != serial || self->hierarchy_shown_root != root ||
	    g_strcmp0 (self->hierarchy_shown_needle, needle) != 0)
	{
		Filter f;

		g_hash_table_remove_all (self->hierarchy_shown);
		f.needle = needle;
		f.shown = self->hierarchy_shown;
		if (needle != NULL)
			mark_matches (root, &f);
		g_free (self->hierarchy_shown_needle);
		self->hierarchy_shown_needle = g_strdup (needle);
		self->hierarchy_shown_serial = serial;
		self->hierarchy_shown_root = root;
	}
	return self->hierarchy_shown;
}

static gboolean
is_selected (LrgEditorApp *self,
             LrgNode      *node)
{
	return lrg_editor_selection_contains (lrg_editor_get_selection (self->editor), node);
}

/* Selects the rows between the anchor and @node (display order). */
static void
select_range (LrgEditorApp *self,
              LrgNode      *node)
{
	LrgNode             *anchor = (self->range_anchor != NULL)
	                              ? lrg_editor_find_node (self->editor, self->range_anchor) : NULL;
	g_autoptr(GPtrArray) range = g_ptr_array_new ();
	gint                 a = -1, b = -1;
	guint                i;

	for (i = 0; i < self->visible_rows->len; i++)
	{
		if (g_ptr_array_index (self->visible_rows, i) == anchor)
			a = (gint) i;
		if (g_ptr_array_index (self->visible_rows, i) == node)
			b = (gint) i;
	}
	if (a < 0 || b < 0)
	{
		lrg_editor_select (self->editor, node, FALSE);
		return;
	}

	for (i = (guint) MIN (a, b); i <= (guint) MAX (a, b); i++)
		g_ptr_array_add (range, g_ptr_array_index (self->visible_rows, i));
	/* Keep @node primary. */
	g_ptr_array_remove (range, node);
	g_ptr_array_add (range, node);
	lrg_editor_select_nodes (self->editor, range, FALSE);
}

/* Moves the dragged node (or the selection containing it) under/next to
 * @target according to @where: -1 before, 0 into, 1 after. */
static void
drop_nodes (LrgEditorApp *self,
            const gchar  *guid,
            LrgNode      *target,
            gint          where)
{
	LrgNode             *dragged = lrg_editor_find_node (self->editor, guid);
	g_autoptr(GPtrArray) moving = NULL;
	LrgNode             *parent;
	gint                 index;
	guint                i;

	if (dragged == NULL)
		return;

	if (is_selected (self, dragged))
		moving = lrg_editor_get_selected_roots (self->editor);
	else
	{
		moving = g_ptr_array_new ();
		g_ptr_array_add (moving, dragged);
	}

	if (where == 0)
	{
		parent = target;
		index = -1;
	}
	else
	{
		parent = lrg_node_get_parent (target);
		index = lrg_node_get_child_index (parent, target) + (where > 0 ? 1 : 0);
	}

	/* Refuse cycles up front so the whole drop is all-or-nothing. */
	for (i = 0; i < moving->len; i++)
		if (lrg_node_is_ancestor_of (g_ptr_array_index (moving, i), parent))
		{
			editor_app_set_status (self, "A node cannot be moved into itself");
			return;
		}

	lrg_editor_begin_group (self->editor, moving->len > 1 ? "Move Nodes" : NULL);
	for (i = 0; i < moving->len; i++)
	{
		LrgNode *n = g_ptr_array_index (moving, i);
		gint     at = index;

		/* Moving down within the same parent shifts the target index. */
		if (at >= 0 && lrg_node_get_parent (n) == parent &&
		    lrg_node_get_child_index (parent, n) < at)
			at--;
		lrg_editor_move_node (self->editor, n, parent, at >= 0 ? at + (gint) i : -1, TRUE);
	}
	lrg_editor_end_group (self->editor);
	self->reveal_pending = TRUE;
}

/* Assets dropped on a node: prefabs become children, scripts attach,
 * models/sprites become child nodes. */
static void
drop_asset (LrgEditorApp *self,
            const gchar  *path,
            LrgNode      *target)
{
	LrgAssetType      type = lrg_asset_database_classify (path);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *base = g_path_get_basename (path);

	switch (type)
	{
	case LRG_ASSET_TYPE_PREFAB:
		{
			g_autofree gchar *abs = editor_app_project_path (self, path);

			if (lrg_editor_instantiate_prefab (self->editor, abs, target, &error) == NULL)
				editor_app_notify (self, 3, "Prefab: %s", error->message);
		}
		break;
	case LRG_ASSET_TYPE_SCRIPT:
		if (target != NULL)
		{
			g_autoptr(LrgScriptBinding) b = NULL;
			LrgScriptLanguage           lang = editor_script_language_for_path (path);

			b = lrg_script_binding_new (lang, path);
			lrg_editor_add_script (self->editor, target, b);
			editor_app_set_status (self, "Attached %s to %s", base, lrg_node_get_name (target));
		}
		break;
	case LRG_ASSET_TYPE_MODEL:
	case LRG_ASSET_TYPE_TEXTURE:
		{
			LrgNode *n = editor_app_create_node (self, base,
			                                     type == LRG_ASSET_TYPE_MODEL ? LRG_NODE_VISUAL_MESH_ASSET
			                                                                  : LRG_NODE_VISUAL_SPRITE,
			                                     0, target);
			g_autoptr(LrgNode) before = lrg_editor_snapshot_node (n);

			lrg_node_visual_set_asset (lrg_node_get_visual (n), path);
			lrg_editor_commit_node_state (self->editor, n, before, "Set Asset", TRUE);
		}
		break;
	default:
		editor_app_set_status (self, "%s cannot be dropped here", base);
		break;
	}
}

static void
node_context_menu (LrgEditorApp *self,
                   LrgNode      *node)
{
	if (lrg_gui_menu_item (self->gui, "Rename", "F2", FALSE, TRUE))
		lrg_editor_app_activate_action (self, "node.rename");
	editor_app_menu_item_action (self, "edit.duplicate");
	editor_app_menu_item_action (self, "edit.delete");
	lrg_gui_separator (self->gui);
	editor_app_menu_item_action (self, "edit.cut");
	editor_app_menu_item_action (self, "edit.copy");
	if (lrg_gui_menu_item (self->gui, "Paste as Child", NULL, FALSE, TRUE))
	{
		g_autofree gchar *text = lrg_gui_context_get_clipboard (self->gui);
		g_autoptr(GError) error = NULL;
		g_autoptr(GPtrArray) created = NULL;

		if (text == NULL || strstr (text, "nodes:") == NULL)
			g_clear_pointer (&text, g_free);
		created = lrg_editor_paste (self->editor, text, node, &error);
		if (created == NULL)
			editor_app_notify (self, 2, "Paste: %s", error->message);
	}
	lrg_gui_separator (self->gui);
	if (lrg_gui_begin_menu (self->gui, "Create Child", TRUE))
	{
		editor_app_menu_create (self, node);
		lrg_gui_end_menu (self->gui);
	}
	editor_app_menu_item_action (self, "node.group");
	editor_app_menu_item_action (self, "node.ungroup");
	editor_app_menu_item_action (self, "node.save-prefab");
	lrg_gui_separator (self->gui);
	editor_app_menu_item_action (self, "view.focus");
	editor_app_menu_item_action (self, "node.toggle-visible");
	editor_app_menu_item_action (self, "node.toggle-lock");
	editor_app_menu_item_action (self, "node.attach-script");
	editor_app_menu_item_action (self, "node.open-scripts");
	lrg_gui_separator (self->gui);
	if (lrg_gui_menu_item (self->gui, "Copy GUID", NULL, FALSE, TRUE))
		lrg_gui_context_set_clipboard (self->gui, lrg_node_get_guid (node));
	if (lrg_gui_menu_item (self->gui, "Copy Path", NULL, FALSE, TRUE))
	{
		g_autofree gchar *path = lrg_node_get_path (node);

		lrg_gui_context_set_clipboard (self->gui, path);
	}
}

/* Eye and lock toggles at the right edge of a row (submitted before the
 * row so their clicks win). */
static void
row_toggles (LrgEditorApp *self,
             LrgNode      *node,
             gfloat        x_right,
             gfloat        y,
             gfloat        h)
{
	LrgGuiStyle    *style = lrg_gui_context_get_style (self->gui);
	LrgGuiDrawList *dl = lrg_gui_get_draw_list (self->gui);
	LrgGuiRect      r;
	gfloat          cx, cy;

	/* Visibility. */
	lrg_gui_set_cursor (self->gui, x_right - h * 2.0f, y);
	if (lrg_gui_invisible_button (self->gui, "##vis", h, h))
	{
		GValue v = G_VALUE_INIT;

		g_value_init (&v, G_TYPE_BOOLEAN);
		g_value_set_boolean (&v, !lrg_node_get_visible (node));
		lrg_editor_set_object_property (self->editor, G_OBJECT (node), "visible", &v, FALSE);
		g_value_unset (&v);
	}
	lrg_gui_set_item_tooltip (self->gui, "Toggle visibility (H)");
	lrg_gui_get_item_rect (self->gui, &r);
	cx = r.x + r.w * 0.5f;
	cy = r.y + r.h * 0.5f;
	if (lrg_node_get_visible (node))
		lrg_gui_draw_list_circle (dl, cx, cy, h * 0.18f, style->colors[LRG_GUI_COL_TEXT_DISABLED], TRUE);
	else
		lrg_gui_draw_list_circle (dl, cx, cy, h * 0.18f, style->colors[LRG_GUI_COL_TEXT_DISABLED], FALSE);
	if (lrg_gui_is_item_hovered (self->gui))
		lrg_gui_draw_list_circle (dl, cx, cy, h * 0.32f, style->colors[LRG_GUI_COL_ACCENT], FALSE);

	/* Lock. */
	lrg_gui_set_cursor (self->gui, x_right - h, y);
	if (lrg_gui_invisible_button (self->gui, "##lock", h, h))
	{
		GValue v = G_VALUE_INIT;

		g_value_init (&v, G_TYPE_BOOLEAN);
		g_value_set_boolean (&v, !lrg_node_get_locked (node));
		lrg_editor_set_object_property (self->editor, G_OBJECT (node), "locked", &v, FALSE);
		g_value_unset (&v);
	}
	lrg_gui_set_item_tooltip (self->gui, "Lock against viewport picking (Ctrl+L)");
	lrg_gui_get_item_rect (self->gui, &r);
	if (lrg_node_get_locked (node) || lrg_gui_is_item_hovered (self->gui))
	{
		LrgGuiRect body = { r.x + r.w * 0.3f, r.y + r.h * 0.45f, r.w * 0.4f, r.h * 0.32f };

		lrg_gui_draw_list_rect_filled (dl, &body,
		                               lrg_node_get_locked (node) ? style->colors[LRG_GUI_COL_WARNING]
		                                                          : style->colors[LRG_GUI_COL_TEXT_DISABLED],
		                               1.0f);
		lrg_gui_draw_list_circle (dl, r.x + r.w * 0.5f, body.y, r.w * 0.14f,
		                          lrg_node_get_locked (node) ? style->colors[LRG_GUI_COL_WARNING]
		                                                     : style->colors[LRG_GUI_COL_TEXT_DISABLED],
		                          FALSE);
	}
}

/* A short tag after the name describing the node's content. */
static const gchar *
kind_tag (LrgNode *node)
{
	LrgNodeVisual *v = lrg_node_get_visual (node);

	if (v == NULL)
		return (lrg_node_get_n_children (node) > 0) ? NULL : "empty";

	switch (lrg_node_visual_get_kind (v))
	{
	case LRG_NODE_VISUAL_LIGHT:           return "light";
	case LRG_NODE_VISUAL_CAMERA:          return "camera";
	case LRG_NODE_VISUAL_AUDIO_EMITTER:   return "audio";
	case LRG_NODE_VISUAL_SPRITE:          return "sprite";
	case LRG_NODE_VISUAL_TILEMAP:         return "tilemap";
	case LRG_NODE_VISUAL_MESH_ASSET:      return "mesh";
	case LRG_NODE_VISUAL_PREFAB_INSTANCE: return "prefab";
	case LRG_NODE_VISUAL_CAD_PART:        return "cad";
	default:                              return NULL;
	}
}

/* Starts a drag of @node (or of the selection containing it) when its
 * row is held and moved: call right after the row's tree node. */
static void
row_drag_source (LrgEditorApp *self,
                 LrgNode      *node,
                 gboolean      selected)
{
	const gchar *name = lrg_node_get_name (node);

	if (lrg_gui_begin_drag_source (self->gui))
	{
		guint             n = selected ? lrg_editor_selection_get_count (lrg_editor_get_selection (self->editor)) : 1;
		g_autofree gchar *preview = (n > 1) ? g_strdup_printf ("%u nodes", n)
		                                    : g_strdup (name != NULL ? name : "node");

		lrg_gui_set_drag_payload (self->gui, "node", lrg_node_get_guid (node), preview);
		lrg_gui_end_drag_source (self->gui);
	}
}

/*
 * draw_row_culled:
 *
 * A row outside the view: submits only its tree node (labelled by GUID
 * alone, which is all the tree node's identity depends on) so layout,
 * open state, the ID scope of its children and a held row (still active,
 * maybe starting a drag) behave exactly as for a drawn row, and skips the
 * toggles, label formatting, measuring, drop targets and context menu
 * that only matter under the pointer.
 */
static gboolean
draw_row_culled (LrgEditorApp    *self,
                 LrgNode         *node,
                 LrgGuiTreeFlags  flags,
                 gfloat           avail,
                 gfloat           h,
                 gboolean         selected)
{
	const gchar      *guid = lrg_node_get_guid (node);
	gchar             buf[96];
	g_autofree gchar *heap = NULL;
	const gchar      *label;
	gboolean          open;

	if (strlen (guid) + 4 <= sizeof buf)
	{
		g_snprintf (buf, sizeof buf, "###%s", guid);
		label = buf;
	}
	else
		label = heap = g_strconcat ("###", guid, NULL);

	lrg_gui_set_next_item_width (self->gui, MAX (40.0f, avail - h * 2.0f));
	open = lrg_gui_tree_node (self->gui, label, flags);
	if (lrg_gui_is_item_active (self->gui))
		row_drag_source (self, node, selected);
	return open;
}

/* A row inside the view, with everything. Returns whether it is open. */
static gboolean
draw_row_full (LrgEditorApp    *self,
               LrgNode         *node,
               LrgGuiTreeFlags  flags,
               LrgNode         *reveal,
               gfloat           x,
               gfloat           y,
               gfloat           avail,
               gfloat           h,
               gboolean         selected,
               gboolean         renaming)
{
	LrgGuiStyle      *style = lrg_gui_context_get_style (self->gui);
	const gchar      *name = lrg_node_get_name (node);
	const gchar      *guid = lrg_node_get_guid (node);
	gboolean          has_kids = lrg_node_get_n_children (node) > 0;
	g_autofree gchar *label = NULL;
	LrgGuiRect        row;
	gfloat            tag_w = 0.0f;
	gboolean          open;

	row_toggles (self, node, x + avail, y, h);

	/* Inline rename editor, submitted before the row. */
	if (renaming)
	{
		lrg_gui_set_cursor (self->gui, x + style->font_size + 2.0f, y);
		lrg_gui_set_next_item_width (self->gui, MAX (60.0f, avail - h * 2.0f - style->font_size - 4.0f));
		if (self->rename_focus)
		{
			lrg_gui_set_keyboard_focus_here (self->gui);
			self->rename_focus = FALSE;
		}
		if (lrg_gui_input_text (self->gui, "##rename", NULL, self->rename_buf,
		                        LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE | LRG_GUI_INPUT_TEXT_AUTO_SELECT_ALL))
		{
			if (self->rename_buf->len > 0)
				lrg_editor_rename_node (self->editor, node, self->rename_buf->str);
			g_clear_pointer (&self->rename_guid, g_free);
		}
		else if (!lrg_gui_context_wants_keyboard (self->gui) && !self->rename_focus &&
		         !lrg_gui_is_item_activated (self->gui) &&
		         lrg_gui_context_get_frame (self->gui) > 2)
		{
			/* Focus left without Enter (Escape or click away): cancel. */
			g_clear_pointer (&self->rename_guid, g_free);
		}
	}

	lrg_gui_set_cursor (self->gui, x, y);
	lrg_gui_set_next_item_width (self->gui, MAX (40.0f, avail - h * 2.0f));
	if (renaming)
		label = g_strdup_printf ("###%s", guid);
	else
	{
		const gchar *tag = kind_tag (node);

		label = g_strdup_printf ("%s###%s", (name != NULL && *name != '\0') ? name : "(unnamed)", guid);
		if (tag != NULL)
			tag_w = lrg_gui_calc_text_width (self->gui, tag);
	}
	open = lrg_gui_tree_node (self->gui, label, flags);
	lrg_gui_get_item_rect (self->gui, &row);

	/* Kind tag and script marker, right-aligned before the toggles. */
	if (!renaming)
	{
		const gchar *tag = kind_tag (node);
		gfloat       tx = row.x + row.w - tag_w - 6.0f;

		if (tag != NULL && tx > row.x + lrg_gui_calc_text_width (self->gui, label) + style->font_size * 2.0f)
			lrg_gui_draw_list_text (lrg_gui_get_draw_list (self->gui), tx,
			                        row.y + (row.h - style->font_size) * 0.5f, tag, -1,
			                        style->font_size, style->colors[LRG_GUI_COL_TEXT_DISABLED], FALSE);
		if (lrg_node_get_scripts (node)->len > 0)
			lrg_gui_draw_list_circle (lrg_gui_get_draw_list (self->gui), row.x + row.w - tag_w - 14.0f,
			                          row.y + row.h * 0.5f, 3.0f, style->colors[LRG_GUI_COL_SUCCESS], TRUE);
	}

	if (node == reveal && self->reveal_pending)
	{
		lrg_gui_scroll_to_item (self->gui, -1.0f);
		self->reveal_pending = FALSE;
	}

	/* Selection. */
	if (lrg_gui_is_item_clicked (self->gui, LRG_GUI_MOUSE_LEFT) && !renaming)
	{
		LrgGuiModifiers mods = lrg_gui_get_mods (self->gui);

		if (mods & LRG_GUI_MOD_SHIFT)
			select_range (self, node);
		else if (mods & LRG_GUI_MOD_CTRL)
			lrg_editor_toggle_selected (self->editor, node);
		else if (!selected)
			lrg_editor_select (self->editor, node, FALSE);
		if (!(mods & LRG_GUI_MOD_SHIFT))
		{
			g_free (self->range_anchor);
			self->range_anchor = g_strdup (guid);
		}
	}
	/* Clicking an already-selected row (without dragging) narrows to it. */
	if (selected && !renaming && lrg_gui_is_item_deactivated (self->gui) &&
	    !(lrg_gui_get_mods (self->gui) & (LRG_GUI_MOD_CTRL | LRG_GUI_MOD_SHIFT)) &&
	    lrg_gui_get_drag_payload (self->gui, NULL) == NULL && lrg_gui_is_item_hovered (self->gui))
		lrg_editor_select (self->editor, node, FALSE);

	if (lrg_gui_is_item_double_clicked (self->gui) && !has_kids)
		editor_app_frame_selection (self);

	if (lrg_gui_is_item_clicked (self->gui, LRG_GUI_MOUSE_RIGHT) && !selected)
		lrg_editor_select (self->editor, node, FALSE);

	/* Drag source. */
	row_drag_source (self, node, selected);

	/* Drop targets: before / into / after by pointer height. */
	{
		const gchar *dropped;
		gfloat       my;

		lrg_gui_get_mouse (self->gui, NULL, &my);
		if ((dropped = lrg_gui_accept_drop (self->gui, "node")) != NULL)
		{
			gfloat rel = (my - row.y) / MAX (1.0f, row.h);
			gint   where = (rel < 0.25f) ? -1 : (rel > 0.75f ? 1 : 0);
			g_autofree gchar *copy = g_strdup (dropped);

			drop_nodes (self, copy, node, where);
		}
		else if ((dropped = lrg_gui_accept_drop (self->gui, "asset")) != NULL)
		{
			g_autofree gchar *copy = g_strdup (dropped);

			drop_asset (self, copy, node);
		}

		/* Insertion line feedback while hovering with a node. */
		{
			const gchar *type = NULL;

			if (lrg_gui_get_drag_payload (self->gui, &type) != NULL && g_strcmp0 (type, "node") == 0 &&
			    my >= row.y && my < row.y + row.h && lrg_gui_is_rect_hovered (self->gui, &row))
			{
				gfloat rel = (my - row.y) / MAX (1.0f, row.h);

				if (rel < 0.25f || rel > 0.75f)
				{
					gfloat ly = (rel < 0.25f) ? row.y : row.y + row.h;

					lrg_gui_draw_list_line (lrg_gui_get_draw_list (self->gui), row.x, ly, row.x + row.w, ly,
					                        style->colors[LRG_GUI_COL_DROP_TARGET], 2.0f);
				}
			}
		}
	}

	/* Context menu. */
	if (lrg_gui_begin_popup_context_item (self->gui, "##node-menu"))
	{
		node_context_menu (self, node);
		lrg_gui_end_popup (self->gui);
	}

	return open;
}

static void
draw_node (LrgEditorApp *self,
           LrgNode      *node,
           Filter       *filter,
           LrgNode      *reveal)
{
	LrgGuiStyle      *style = lrg_gui_context_get_style (self->gui);
	GPtrArray        *children = lrg_node_get_children (node);
	const gchar      *guid = lrg_node_get_guid (node);
	gboolean          selected = is_selected (self, node);
	gboolean          renaming = (self->rename_guid != NULL && g_strcmp0 (self->rename_guid, guid) == 0);
	gboolean          has_kids = children->len > 0;
	LrgGuiTreeFlags   flags = LRG_GUI_TREE_OPEN_ON_ARROW;
	gfloat            x, y, avail, h, row_h;
	gboolean          open;
	guint             i;

	if (filter->needle != NULL && !g_hash_table_contains (filter->shown, node))
		return;

	g_ptr_array_add (self->visible_rows, node);
	lrg_gui_push_id (self->gui, guid);

	if (!has_kids)
		flags |= LRG_GUI_TREE_LEAF;
	if (selected)
		flags |= LRG_GUI_TREE_SELECTED;
	if (!lrg_node_get_visible (node))
		flags |= LRG_GUI_TREE_DIMMED;

	/* Open the path to the node being revealed, and everything while
	 * filtering. */
	if ((reveal != NULL && node != reveal && lrg_node_is_ancestor_of (node, reveal)) ||
	    (filter->needle != NULL && has_kids))
		lrg_gui_set_next_item_open (self->gui, TRUE);

	lrg_gui_get_cursor (self->gui, &x, &y);
	lrg_gui_get_content_avail (self->gui, &avail, NULL);
	h = lrg_gui_get_frame_height (self->gui);
	row_h = MAX (h, style->font_size + style->frame_padding_y * 2.0f);

	/* Rows outside the view are culled, except the ones that must run in
	 * full wherever they are: the row being renamed (its text field), the
	 * one being revealed (scrolls to itself) and one whose context menu
	 * is open (the menu is built from its row). */
	if ((y + row_h < filter->view_y0 || y >= filter->view_y1) && !renaming && node != reveal &&
	    !lrg_gui_is_popup_open (self->gui, "##node-menu"))
		open = draw_row_culled (self, node, flags, avail, h, selected);
	else
		open = draw_row_full (self, node, flags, reveal, x, y, avail, h, selected, renaming);

	if (open)
	{
		/* Iterate a snapshot: menus above may restructure the tree. */
		g_autoptr(GPtrArray) kids = g_ptr_array_copy (children, ref_copy, NULL);

		g_ptr_array_set_free_func (kids, g_object_unref);
		for (i = 0; i < kids->len; i++)
			draw_node (self, g_ptr_array_index (kids, i), filter, reveal);
		lrg_gui_tree_pop (self->gui);
	}

	lrg_gui_pop_id (self->gui);
}

void
editor_panel_hierarchy (LrgEditorApp *self)
{
	LrgGuiStyle         *style = lrg_gui_context_get_style (self->gui);
	LrgNode             *root = lrg_level_get_root (lrg_editor_get_level (self->editor));
	g_autoptr(GPtrArray) top = NULL;
	Filter               filter;
	LrgNode             *reveal = NULL;
	gfloat               avail_w, avail_h;
	guint                i;

	/* Filter bar and the create button. */
	lrg_gui_get_content_avail (self->gui, &avail_w, NULL);
	lrg_gui_set_next_item_width (self->gui, avail_w - lrg_gui_get_frame_height (self->gui) - style->item_spacing_x);
	if (g_strcmp0 (self->rename_guid, "##filter") == 0)
	{
		lrg_gui_set_keyboard_focus_here (self->gui);
		g_clear_pointer (&self->rename_guid, g_free);
	}
	lrg_gui_input_text (self->gui, "##hierarchy-filter", "Filter nodes (name or GUID)",
	                    self->hierarchy_filter, LRG_GUI_INPUT_TEXT_NONE);
	lrg_gui_same_line (self->gui, -1.0f);
	if (lrg_gui_button_sized (self->gui, "+##create", lrg_gui_get_frame_height (self->gui), 0.0f))
		lrg_gui_open_popup (self->gui, "##create-menu");
	lrg_gui_set_item_tooltip (self->gui, "Create node");
	if (lrg_gui_begin_popup (self->gui, "##create-menu"))
	{
		editor_app_menu_create (self, lrg_editor_selection_get_primary (lrg_editor_get_selection (self->editor)) != NULL &&
		                              (lrg_gui_get_mods (self->gui) & LRG_GUI_MOD_SHIFT)
		                        ? lrg_editor_selection_get_primary (lrg_editor_get_selection (self->editor))
		                        : NULL);
		lrg_gui_end_popup (self->gui);
	}

	/* Level root line. */
	{
		LrgLevel         *level = lrg_editor_get_level (self->editor);
		g_autofree gchar *head = g_strdup_printf ("%s  (%u)", lrg_level_get_name (level) != NULL
		                                                      ? lrg_level_get_name (level) : "level",
		                                          editor_app_count_nodes (self));

		lrg_gui_text_disabled (self->gui, head);
	}

	filter.needle = (self->hierarchy_filter->len > 0) ? g_utf8_casefold (self->hierarchy_filter->str, -1) : NULL;
	filter.shown = filter_shown (self, root, filter.needle);

	if (self->reveal_pending)
		reveal = lrg_editor_selection_get_primary (lrg_editor_get_selection (self->editor));

	g_ptr_array_set_size (self->visible_rows, 0);

	lrg_gui_get_content_avail (self->gui, NULL, &avail_h);
	lrg_gui_begin_child (self->gui, "##tree", 0.0f, 0.0f, LRG_GUI_REGION_SCROLL_Y | LRG_GUI_REGION_NO_PADDING);
	{
		LrgGuiRect view;

		lrg_gui_get_region_rect (self->gui, &view);
		filter.view_y0 = view.y;
		filter.view_y1 = view.y + view.h;
	}

	top = g_ptr_array_copy (lrg_node_get_children (root), ref_copy, NULL);
	g_ptr_array_set_free_func (top, g_object_unref);
	for (i = 0; i < top->len; i++)
		draw_node (self, g_ptr_array_index (top, i), &filter, reveal);

	if (top->len == 0)
		lrg_gui_text_disabled (self->gui, "Empty level. Use + or the Create menu.");

	/* The rest of the panel: drop to the root, click to deselect,
	 * right-click to create. */
	{
		gfloat rest_h;

		lrg_gui_get_content_avail (self->gui, NULL, &rest_h);
		if (lrg_gui_invisible_button (self->gui, "##tree-empty", 1.0f + avail_w, MAX (24.0f, rest_h)))
			lrg_editor_select (self->editor, NULL, FALSE);
		{
			const gchar *dropped;

			if ((dropped = lrg_gui_accept_drop (self->gui, "node")) != NULL)
			{
				LrgNode *n = lrg_editor_find_node (self->editor, dropped);

				if (n != NULL)
				{
					g_autoptr(GPtrArray) moving = NULL;
					guint                k;

					moving = is_selected (self, n) ? lrg_editor_get_selected_roots (self->editor) : g_ptr_array_new ();
					if (moving->len == 0)
						g_ptr_array_add (moving, n);
					lrg_editor_begin_group (self->editor, "Move to Root");
					for (k = 0; k < moving->len; k++)
						lrg_editor_move_node (self->editor, g_ptr_array_index (moving, k), NULL, -1, TRUE);
					lrg_editor_end_group (self->editor);
				}
			}
			else if ((dropped = lrg_gui_accept_drop (self->gui, "asset")) != NULL)
			{
				g_autofree gchar *copy = g_strdup (dropped);

				drop_asset (self, copy, NULL);
			}
		}
		if (lrg_gui_begin_popup_context_item (self->gui, "##empty-menu"))
		{
			if (lrg_gui_begin_menu (self->gui, "Create", TRUE))
			{
				editor_app_menu_create (self, NULL);
				lrg_gui_end_menu (self->gui);
			}
			editor_app_menu_item_action (self, "edit.paste");
			editor_app_menu_item_action (self, "edit.select-all");
			lrg_gui_end_popup (self->gui);
		}
	}

	/* Up/Down move the selection through the visible rows. */
	{
		LrgGuiRect region;
		gfloat     mx, my;

		lrg_gui_get_region_rect (self->gui, &region);
		lrg_gui_get_mouse (self->gui, &mx, &my);
		if (!lrg_gui_context_wants_keyboard (self->gui) && mx >= region.x && mx < region.x + region.w &&
		    my >= region.y && my < region.y + region.h && self->visible_rows->len > 0)
		{
			gint     dir = 0;
			LrgNode *cur = lrg_editor_selection_get_primary (lrg_editor_get_selection (self->editor));
			gint     idx = -1;

			if (lrg_gui_is_key_pressed (self->gui, LRG_GUI_KEY_DOWN, TRUE))
				dir = 1;
			else if (lrg_gui_is_key_pressed (self->gui, LRG_GUI_KEY_UP, TRUE))
				dir = -1;
			if (dir != 0)
			{
				for (i = 0; i < self->visible_rows->len; i++)
					if (g_ptr_array_index (self->visible_rows, i) == cur)
						idx = (gint) i;
				idx = CLAMP (idx + dir, 0, (gint) self->visible_rows->len - 1);
				lrg_editor_select (self->editor, g_ptr_array_index (self->visible_rows, idx), FALSE);
				self->reveal_pending = TRUE;
			}
		}
	}

	lrg_gui_end_region (self->gui);

	g_free (filter.needle);
}
