/* test-editor-app-hierarchy.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The Hierarchy panel of LrgEditorApp, driven through the canvas like a
 * user: click / Ctrl / Shift selection mirrored into the editor, outside
 * selections revealing rows, inline rename (F2, double click, IPC), the
 * context menu, drag and drop (reparent, reorder, refused cycles, Alt
 * keeping world transforms, one undo step), asset drops, the eye toggle,
 * Ctrl+F filtering, the Live tab during play and the hierarchy.* IPC
 * methods.
 */

#include <glib.h>
#include <libregnum.h>

#ifdef LRG_BUILD_EDITOR_UI

#include "editor-app-fixture.h"

typedef EditorAppFixture Fixture;

#define CTRL  LRG_INPUT_MODIFIER_CTRL
#define ALT   LRG_INPUT_MODIFIER_ALT
#define SHIFT LRG_INPUT_MODIFIER_SHIFT
#define NOMOD LRG_INPUT_MODIFIER_NONE

static LrgEditorHierarchyPanel *
panel (Fixture *f)
{
	LrgEditorHierarchyPanel *p = lrg_editor_hierarchy_panel_find (f->app);

	g_assert_nonnull (p);
	return p;
}

static LrgTreeView *
tree (Fixture *f)
{
	return lrg_editor_hierarchy_panel_get_tree_view (panel (f));
}

/* A top-level (or child) node added through the editor. */
static LrgNode *
add (Fixture     *f,
     const gchar *name,
     LrgNode     *parent)
{
	g_autoptr(LrgNode) node = lrg_node_new (name);

	lrg_editor_insert_node (f->editor, node, parent, -1);
	return node;
}

static LrgEditorSelection *
sel (Fixture *f)
{
	return lrg_editor_get_selection (f->editor);
}

/* A point on @node's row: @fx of the way across (left of the toggles),
 * @fy of the way down. */
static void
row_point (Fixture *f,
           LrgNode *node,
           gfloat   fx,
           gfloat   fy,
           gfloat  *px,
           gfloat  *py)
{
	gfloat x, y, w, h;

	g_assert_true (lrg_tree_view_get_row_rect (tree (f), G_OBJECT (node), &x, &y, &w, &h));
	*px = x + w * fx;
	*py = y + h * fy;
}

/* Clicks a row with @mods held, then lets the double-click window pass. */
static void
click_row (Fixture           *f,
           LrgNode           *node,
           LrgInputModifiers  mods)
{
	gfloat x, y;

	row_point (f, node, 0.3f, 0.5f, &x, &y);
	f->mods = mods;
	efx_click (f, x, y);
	f->mods = NOMOD;
	efx_frames (f, 40);
}

/* Drags @from's row to (@fy of the way down) @to's row. */
static void
drag_row (Fixture           *f,
          LrgNode           *from,
          LrgNode           *to,
          gfloat             fy,
          LrgInputModifiers  mods)
{
	gfloat x0, y0, x1, y1;

	row_point (f, from, 0.3f, 0.5f, &x0, &y0);
	row_point (f, to, 0.3f, fy, &x1, &y1);
	f->mods = mods;
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, x0, y0, x1, y1, 6);
	f->mods = NOMOD;
	efx_frames (f, 40);
}

static void
setup_frames (Fixture *f)
{
	efx_frames (f, 3);
}

/* ==========================================================================
 * Selection
 * ========================================================================== */

static void
test_click_selection (Fixture       *f,
                      gconstpointer  data)
{
	LrgNode *n0 = add (f, "N0", NULL);
	LrgNode *n1 = add (f, "N1", NULL);
	LrgNode *n2 = add (f, "N2", NULL);
	LrgNode *n3 = add (f, "N3", NULL);

	lrg_editor_select (f->editor, NULL, FALSE);
	setup_frames (f);
	g_assert_cmpuint (lrg_tree_view_get_n_visible (tree (f)), ==, 4);

	/* Plain click: exactly that node, in the editor and the tree */
	click_row (f, n0, NOMOD);
	g_assert_cmpuint (lrg_editor_selection_get_count (sel (f)), ==, 1);
	g_assert_true (lrg_editor_selection_get_primary (sel (f)) == n0);
	g_assert_true (lrg_tree_view_is_item_selected (tree (f), G_OBJECT (n0)));

	/* Ctrl adds, the clicked node becomes primary */
	click_row (f, n2, CTRL);
	g_assert_cmpuint (lrg_editor_selection_get_count (sel (f)), ==, 2);
	g_assert_true (lrg_editor_selection_contains (sel (f), n0));
	g_assert_true (lrg_editor_selection_get_primary (sel (f)) == n2);

	/* Ctrl again removes */
	click_row (f, n0, CTRL);
	g_assert_cmpuint (lrg_editor_selection_get_count (sel (f)), ==, 1);
	g_assert_false (lrg_editor_selection_contains (sel (f), n0));

	/* Shift: the range from the anchor (N2) */
	click_row (f, n1, NOMOD);
	click_row (f, n3, SHIFT);
	g_assert_cmpuint (lrg_editor_selection_get_count (sel (f)), ==, 3);
	g_assert_true (lrg_editor_selection_contains (sel (f), n1));
	g_assert_true (lrg_editor_selection_contains (sel (f), n2));
	g_assert_true (lrg_editor_selection_contains (sel (f), n3));
	g_assert_true (lrg_editor_selection_get_primary (sel (f)) == n3);

	/* A plain click (no drag) on a row of the multi-selection narrows to it
	 * on release; the press alone keeps the selection for dragging */
	{
		gfloat x, y;

		row_point (f, n2, 0.3f, 0.5f, &x, &y);
		efx_move (f, x, y);
		efx_button (f, x, y, GRL_MOUSE_BUTTON_LEFT, TRUE);
		g_assert_cmpuint (lrg_editor_selection_get_count (sel (f)), ==, 3);
		efx_button (f, x, y, GRL_MOUSE_BUTTON_LEFT, FALSE);
		g_assert_cmpuint (lrg_editor_selection_get_count (sel (f)), ==, 1);
		g_assert_true (lrg_editor_selection_get_primary (sel (f)) == n2);
		g_assert_true (lrg_tree_view_is_item_selected (tree (f), G_OBJECT (n2)));
		g_assert_cmpuint (lrg_tree_view_get_n_selected (tree (f)), ==, 1);
		efx_frames (f, 40);
	}

	/* A click below the rows clears */
	{
		gfloat x, y, w, h;

		efx_rect_of (LRG_WIDGET (tree (f)), &x, &y, &w, &h);
		efx_click (f, x + w * 0.3f, y + h - 10.0f);
		g_assert_cmpuint (lrg_editor_selection_get_count (sel (f)), ==, 0);
		g_assert_cmpuint (lrg_tree_view_get_n_selected (tree (f)), ==, 0);
	}
}

static void
test_external_select_reveals (Fixture       *f,
                              gconstpointer  data)
{
	LrgNode *a = add (f, "A", NULL);
	LrgNode *b = add (f, "B", a);
	LrgNode *c = add (f, "C", b);
	LrgNode *last = NULL;
	gint     i;

	/* Plenty of rows so the last one is scrolled away */
	for (i = 0; i < 80; i++)
	{
		g_autofree gchar *name = g_strdup_printf ("Filler %02d", i);

		last = add (f, name, NULL);
	}
	lrg_editor_select (f->editor, NULL, FALSE);
	lrg_tree_view_collapse_all (tree (f));
	lrg_tree_view_scroll_to (tree (f), 0, FALSE);
	setup_frames (f);
	g_assert_cmpuint (lrg_tree_view_get_position (tree (f), G_OBJECT (c)), ==, LRG_TREE_VIEW_INVALID_POSITION);

	/* Selecting from elsewhere (viewport pick, Emacs) opens the path */
	lrg_editor_select (f->editor, c, FALSE);
	efx_frames (f, 2);
	g_assert_true (lrg_tree_view_is_expanded (tree (f), G_OBJECT (a)));
	g_assert_true (lrg_tree_view_is_expanded (tree (f), G_OBJECT (b)));
	g_assert_true (lrg_tree_view_is_item_selected (tree (f), G_OBJECT (c)));
	g_assert_cmpuint (lrg_tree_view_get_n_selected (tree (f)), ==, 1);

	/* ... and scrolls to it */
	g_assert_cmpfloat (lrg_tree_view_get_scroll_offset (tree (f)), ==, 0.0f);
	lrg_editor_select (f->editor, last, FALSE);
	efx_frames (f, 60);
	g_assert_cmpfloat (lrg_tree_view_get_scroll_offset (tree (f)), >, 0.0f);
	{
		gfloat tx, ty, tw, th, rx, ry, rw, rh;

		efx_rect_of (LRG_WIDGET (tree (f)), &tx, &ty, &tw, &th);
		g_assert_true (lrg_tree_view_get_row_rect (tree (f), G_OBJECT (last), &rx, &ry, &rw, &rh));
		g_assert_cmpfloat (ry, >=, ty - 0.5f);
		g_assert_cmpfloat (ry + rh, <=, ty + th + 0.5f);
	}
}

/* ==========================================================================
 * Rename
 * ========================================================================== */

static void
test_rename_f2 (Fixture       *f,
                gconstpointer  data)
{
	LrgNode *a = add (f, "Alpha", NULL);
	LrgNode *b = add (f, "Beta", NULL);
	gfloat   x, y;

	setup_frames (f);
	click_row (f, a, NOMOD);

	/* F2 (the node.rename action) opens the field over the row */
	efx_key (f, GRL_KEY_F2, NOMOD);
	g_assert_true (lrg_tree_view_get_editing_item (tree (f)) == G_OBJECT (a));
	efx_type (f, "Renamed");
	efx_key (f, GRL_KEY_ENTER, NOMOD);
	g_assert_null (lrg_tree_view_get_editing_item (tree (f)));
	g_assert_cmpstr (lrg_node_get_name (a), ==, "Renamed");

	/* One undo step, and the row follows the name */
	lrg_editor_undo (f->editor);
	g_assert_cmpstr (lrg_node_get_name (a), ==, "Alpha");

	/* Escape cancels */
	g_assert_true (lrg_editor_app_activate_action (f->app, "node.rename"));
	efx_frames (f, 2);
	g_assert_true (lrg_tree_view_get_editing_item (tree (f)) == G_OBJECT (a));
	efx_type (f, "Nope");
	efx_key (f, GRL_KEY_ESCAPE, NOMOD);
	g_assert_null (lrg_tree_view_get_editing_item (tree (f)));
	g_assert_cmpstr (lrg_node_get_name (a), ==, "Alpha");

	/* Double click renames too */
	efx_frames (f, 40);
	row_point (f, b, 0.3f, 0.5f, &x, &y);
	efx_click (f, x, y);
	efx_click (f, x, y);
	g_assert_true (lrg_tree_view_get_editing_item (tree (f)) == G_OBJECT (b));
	efx_key (f, GRL_KEY_ESCAPE, NOMOD);
}

/* ==========================================================================
 * Context menu
 * ========================================================================== */

/* Index of the menu entry labelled @label, or -1. */
static gint
menu_entry (LrgMenu     *menu,
            const gchar *label)
{
	guint i;

	for (i = 0; i < lrg_menu_get_n_entries (menu); i++)
		if (g_strcmp0 (lrg_menu_get_entry_label (menu, i), label) == 0)
			return (gint) i;
	return -1;
}

static void
test_context_menu_duplicate (Fixture       *f,
                             gconstpointer  data)
{
	LrgNode *a = add (f, "Alpha", NULL);
	LrgNode *b = add (f, "Beta", NULL);
	LrgMenu *menu;
	gfloat   x, y, mx, my, mw, mh;
	gint     idx;
	guint    before;

	lrg_editor_select (f->editor, a, FALSE);
	setup_frames (f);
	before = lrg_level_count_nodes (lrg_editor_get_level (f->editor));

	/* Right click on an unselected row selects it and opens the menu */
	row_point (f, b, 0.3f, 0.5f, &x, &y);
	efx_move (f, x, y);
	efx_button (f, x, y, GRL_MOUSE_BUTTON_RIGHT, TRUE);
	efx_button (f, x, y, GRL_MOUSE_BUTTON_RIGHT, FALSE);
	efx_frames (f, 10);
	menu = lrg_editor_hierarchy_panel_get_context_menu (panel (f));
	g_assert_nonnull (menu);
	g_assert_true (lrg_menu_get_open (menu));
	g_assert_true (lrg_editor_selection_get_primary (sel (f)) == b);
	g_assert_cmpuint (lrg_editor_selection_get_count (sel (f)), ==, 1);

	/* Its entries (no prefab section for a plain node) */
	g_assert_cmpint (menu_entry (menu, "Rename"), >=, 0);
	g_assert_cmpint (menu_entry (menu, "Copy GUID"), >=, 0);
	g_assert_cmpint (menu_entry (menu, "Create Child"), >=, 0);
	g_assert_cmpint (menu_entry (menu, "Revert All Overrides"), <, 0);

	/* Click "Duplicate" */
	idx = menu_entry (menu, "Duplicate");
	g_assert_cmpint (idx, >=, 0);
	g_assert_true (lrg_menu_get_entry_enabled (menu, (guint) idx));
	g_assert_true (lrg_menu_get_item_rect (menu, (guint) idx, &mx, &my, &mw, &mh));
	efx_click (f, mx + mw * 0.5f, my + mh * 0.5f);
	efx_frames (f, 2);
	g_assert_false (lrg_menu_get_open (menu));
	g_assert_cmpuint (lrg_level_count_nodes (lrg_editor_get_level (f->editor)), ==, before + 1);
	/* The copy is selected and has a row */
	g_assert_true (lrg_editor_selection_get_primary (sel (f)) != b);
	g_assert_true (lrg_tree_view_is_item_selected (tree (f), G_OBJECT (lrg_editor_selection_get_primary (sel (f)))));

	/* Copy GUID through the menu actions */
	lrg_editor_hierarchy_panel_popup_context_menu (panel (f), a, 200.0f, 200.0f);
	efx_frames (f, 2);
	idx = menu_entry (menu, "Copy GUID");
	g_assert_true (lrg_menu_activate_entry (menu, (guint) idx));
	g_assert_cmpstr (g_object_get_data (G_OBJECT (f->app), "lrg-editor-clipboard"), ==, lrg_node_get_guid (a));

	/* Empty area: create at the root */
	{
		gfloat tx, ty, tw, th;

		efx_rect_of (LRG_WIDGET (tree (f)), &tx, &ty, &tw, &th);
		efx_move (f, tx + tw * 0.4f, ty + th - 20.0f);
		efx_button (f, tx + tw * 0.4f, ty + th - 20.0f, GRL_MOUSE_BUTTON_RIGHT, TRUE);
		efx_button (f, tx + tw * 0.4f, ty + th - 20.0f, GRL_MOUSE_BUTTON_RIGHT, FALSE);
		efx_frames (f, 4);
		g_assert_true (lrg_menu_get_open (menu));
		g_assert_cmpint (menu_entry (menu, "Create"), >=, 0);
		g_assert_cmpint (menu_entry (menu, "Duplicate"), <, 0);
		lrg_menu_close (menu);
	}
}

static void
test_prefab_menu (Fixture       *f,
                  gconstpointer  data)
{
	LrgNode           *crate = add (f, "Crate", NULL);
	g_autofree gchar  *path = g_build_filename (f->dir, "crate.rprefab", NULL);
	g_autoptr(GError)  error = NULL;
	LrgNode           *inst;
	LrgMenu           *menu;
	gint               idx;

	add (f, "Lid", crate);
	g_assert_true (lrg_editor_save_prefab (f->editor, crate, path, &error));
	g_assert_no_error (error);
	inst = lrg_editor_instantiate_prefab (f->editor, path, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (lrg_node_get_prefab_link (inst));
	setup_frames (f);

	/* A linked instance gets the Prefab section; no overrides yet */
	lrg_editor_hierarchy_panel_popup_context_menu (panel (f), inst, 200.0f, 200.0f);
	efx_frames (f, 2);
	menu = lrg_editor_hierarchy_panel_get_context_menu (panel (f));
	g_assert_cmpint (menu_entry (menu, "Select Source Prefab"), >=, 0);
	idx = menu_entry (menu, "Revert All Overrides");
	g_assert_cmpint (idx, >=, 0);
	g_assert_false (lrg_menu_get_entry_enabled (menu, (guint) idx));
	g_assert_cmpint (menu_entry (menu, "Relink"), <, 0);

	/* Select Source copies the prefab path */
	g_assert_true (lrg_menu_activate_entry (menu, (guint) menu_entry (menu, "Select Source Prefab")));
	g_assert_nonnull (strstr (g_object_get_data (G_OBJECT (f->app), "lrg-editor-clipboard"), "crate.rprefab"));

	/* An override enables Revert All, which clears it (one undo step) */
	g_assert_true (lrg_editor_prefab_set_override (f->editor, inst, "location", TRUE));
	g_assert_cmpuint (lrg_prefab_link_get_n_overrides (lrg_node_get_prefab_link (inst)), >, 0);
	lrg_editor_hierarchy_panel_popup_context_menu (panel (f), inst, 200.0f, 200.0f);
	efx_frames (f, 2);
	idx = menu_entry (menu, "Revert All Overrides");
	g_assert_true (lrg_menu_get_entry_enabled (menu, (guint) idx));
	g_assert_true (lrg_menu_activate_entry (menu, (guint) idx));
	g_assert_cmpuint (lrg_prefab_link_get_n_overrides (lrg_node_get_prefab_link (inst)), ==, 0);

	/* Unlink; a child of an instance offers Select Instance Root first */
	lrg_editor_hierarchy_panel_popup_context_menu (panel (f), inst, 200.0f, 200.0f);
	efx_frames (f, 2);
	g_assert_true (lrg_menu_activate_entry (menu, (guint) menu_entry (menu, "Unlink")));
	g_assert_null (lrg_node_get_prefab_link (inst));
}

/* ==========================================================================
 * Drag and drop
 * ========================================================================== */

static void
test_dnd_reparent (Fixture       *f,
                   gconstpointer  data)
{
	LrgNode *a = add (f, "Alpha", NULL);
	LrgNode *b = add (f, "Beta", NULL);
	LrgNode *c = add (f, "Gamma", NULL);
	guint    undo;

	lrg_editor_select (f->editor, NULL, FALSE);
	setup_frames (f);
	undo = lrg_editor_get_undo_count (f->editor);

	/* Beta onto the middle of Alpha: becomes its child, one undo step */
	drag_row (f, b, a, 0.5f, NOMOD);
	g_assert_true (lrg_node_get_parent (b) == a);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo + 1);
	g_assert_true (lrg_tree_view_is_expanded (tree (f), G_OBJECT (a)));
	g_assert_cmpuint (lrg_tree_view_get_depth (tree (f), lrg_tree_view_get_position (tree (f), G_OBJECT (b))), ==, 1);

	/* A multi-selection moves together, still one step */
	{
		LrgNode *d = add (f, "Delta", NULL);

		lrg_editor_select (f->editor, b, FALSE);
		lrg_editor_select (f->editor, c, TRUE);
		efx_frames (f, 2);
		undo = lrg_editor_get_undo_count (f->editor);
		drag_row (f, c, d, 0.5f, NOMOD);
		g_assert_true (lrg_node_get_parent (b) == d);
		g_assert_true (lrg_node_get_parent (c) == d);
		g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo + 1);
		lrg_editor_undo (f->editor);
		efx_frames (f, 2);
		g_assert_true (lrg_node_get_parent (b) == a);
		g_assert_true (lrg_node_get_parent (c) == lrg_level_get_root (lrg_editor_get_level (f->editor)));
	}

	/* Undo Delta's creation, then the first move */
	lrg_editor_undo (f->editor);
	lrg_editor_undo (f->editor);
	efx_frames (f, 2);
	g_assert_true (lrg_node_get_parent (b) == lrg_level_get_root (lrg_editor_get_level (f->editor)));
}

static void
test_dnd_reorder (Fixture       *f,
                  gconstpointer  data)
{
	LrgNode *a = add (f, "Alpha", NULL);
	LrgNode *b = add (f, "Beta", NULL);
	LrgNode *c = add (f, "Gamma", NULL);
	LrgNode *root = lrg_level_get_root (lrg_editor_get_level (f->editor));

	lrg_editor_select (f->editor, NULL, FALSE);
	setup_frames (f);

	/* Gamma onto the top quarter of Alpha: before it */
	drag_row (f, c, a, 0.1f, NOMOD);
	g_assert_true (lrg_node_get_parent (c) == root);
	g_assert_cmpint (lrg_node_get_child_index (root, c), ==, 0);
	g_assert_cmpint (lrg_node_get_child_index (root, a), ==, 1);
	g_assert_cmpuint (lrg_tree_view_get_position (tree (f), G_OBJECT (c)), ==, 0);

	/* Gamma onto the bottom quarter of Beta: after it (moving down) */
	drag_row (f, c, b, 0.9f, NOMOD);
	g_assert_cmpint (lrg_node_get_child_index (root, a), ==, 0);
	g_assert_cmpint (lrg_node_get_child_index (root, b), ==, 1);
	g_assert_cmpint (lrg_node_get_child_index (root, c), ==, 2);
}

static void
test_dnd_refuses_own_subtree (Fixture       *f,
                              gconstpointer  data)
{
	LrgNode *a = add (f, "Alpha", NULL);
	LrgNode *b = add (f, "Beta", a);
	guint    undo;

	lrg_editor_select (f->editor, NULL, FALSE);
	setup_frames (f);
	lrg_tree_view_expand (tree (f), G_OBJECT (a));
	efx_frames (f, 2);
	undo = lrg_editor_get_undo_count (f->editor);

	/* Alpha into its own child: refused while hovering, nothing happens */
	{
		gfloat x0, y0, x1, y1;

		row_point (f, a, 0.3f, 0.5f, &x0, &y0);
		row_point (f, b, 0.3f, 0.5f, &x1, &y1);
		efx_move (f, x0, y0);
		efx_button (f, x0, y0, GRL_MOUSE_BUTTON_LEFT, TRUE);
		efx_move (f, x0, (y0 + y1) * 0.5f);
		efx_move (f, x1, y1);
		efx_move (f, x1 + 1.0f, y1);
		g_assert_true (lrg_canvas_is_dragging (lrg_editor_app_get_canvas (f->app)));
		g_assert_cmpint (lrg_canvas_get_drag_action (lrg_editor_app_get_canvas (f->app)), ==, LRG_DRAG_ACTION_NONE);
		g_assert_null (lrg_tree_view_get_drop_indicator (tree (f), NULL));
		efx_button (f, x1, y1, GRL_MOUSE_BUTTON_LEFT, FALSE);
	}
	efx_frames (f, 4);
	g_assert_true (lrg_node_get_parent (a) == lrg_level_get_root (lrg_editor_get_level (f->editor)));
	g_assert_true (lrg_node_get_parent (b) == a);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo);

	/* Dropping onto itself is refused the same way */
	drag_row (f, a, a, 0.5f, NOMOD);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo);
}

static void
test_dnd_alt_keeps_world (Fixture       *f,
                          gconstpointer  data)
{
	LrgNode    *a = add (f, "Alpha", NULL);
	LrgNode    *b = add (f, "Beta", NULL);
	LrgNode    *c = add (f, "Gamma", NULL);
	GrlVector3 *loc;

	lrg_node_set_location_xyz (a, 10.0f, 0.0f, 0.0f);
	lrg_node_set_location_xyz (b, 0.0f, 0.0f, 0.0f);
	lrg_node_set_location_xyz (c, 0.0f, 0.0f, 0.0f);
	lrg_editor_select (f->editor, NULL, FALSE);
	setup_frames (f);

	/* Plain drop: the local transform is kept (the node moves with A) */
	drag_row (f, b, a, 0.5f, NOMOD);
	g_assert_true (lrg_node_get_parent (b) == a);
	loc = lrg_node_get_location (b);
	g_assert_cmpfloat_with_epsilon (loc->x, 0.0f, 0.001f);

	/* Alt drop: the world position is kept */
	drag_row (f, c, a, 0.5f, ALT);
	g_assert_true (lrg_node_get_parent (c) == a);
	loc = lrg_node_get_location (c);
	g_assert_cmpfloat_with_epsilon (loc->x, -10.0f, 0.001f);
}

static void
test_asset_drop (Fixture       *f,
                 gconstpointer  data)
{
	LrgNode                  *a = add (f, "Alpha", NULL);
	LrgCanvas                *canvas = lrg_editor_app_get_canvas (f->app);
	g_autoptr(LrgDragPayload) model = NULL;
	g_autoptr(LrgDragPayload) script = NULL;
	gfloat                    x, y;
	LrgNode                  *made;

	lrg_editor_select (f->editor, NULL, FALSE);
	setup_frames (f);

	/* A model dropped onto a row becomes a mesh node under it */
	model = lrg_drag_payload_new_string ("application/x-lrg-asset", "assets/models/crate.glb",
	                                     LRG_DRAG_ACTION_COPY | LRG_DRAG_ACTION_LINK);
	row_point (f, a, 0.3f, 0.5f, &x, &y);
	g_assert_true (lrg_canvas_begin_drag (canvas, NULL, model, x - 40.0f, y));
	efx_move (f, x, y);
	efx_move (f, x + 1.0f, y);
	efx_click (f, x + 1.0f, y);
	efx_frames (f, 2);
	g_assert_false (lrg_canvas_is_dragging (canvas));
	g_assert_cmpuint (lrg_node_get_n_children (a), ==, 1);
	made = g_ptr_array_index (lrg_node_get_children (a), 0);
	g_assert_cmpstr (lrg_node_get_name (made), ==, "crate.glb");
	g_assert_nonnull (lrg_node_get_visual (made));
	g_assert_cmpint (lrg_node_visual_get_kind (lrg_node_get_visual (made)), ==, LRG_NODE_VISUAL_MESH_ASSET);
	g_assert_cmpstr (lrg_node_visual_get_asset (lrg_node_get_visual (made)), ==, "assets/models/crate.glb");
	g_assert_true (lrg_editor_selection_get_primary (sel (f)) == made);

	/* One undo removes it */
	lrg_editor_undo (f->editor);
	g_assert_cmpuint (lrg_node_get_n_children (a), ==, 0);
	efx_frames (f, 2);

	/* A script attaches to the row's node */
	script = lrg_drag_payload_new_string ("application/x-lrg-asset", "scripts/spin.lua",
	                                      LRG_DRAG_ACTION_COPY | LRG_DRAG_ACTION_LINK);
	row_point (f, a, 0.3f, 0.5f, &x, &y);
	g_assert_true (lrg_canvas_begin_drag (canvas, NULL, script, x - 40.0f, y));
	efx_move (f, x, y);
	efx_move (f, x + 1.0f, y);
	efx_click (f, x + 1.0f, y);
	efx_frames (f, 2);
	g_assert_cmpuint (lrg_node_get_scripts (a)->len, ==, 1);
}

/* ==========================================================================
 * Toggles, filter
 * ========================================================================== */

static void
test_toggles_and_filter (Fixture       *f,
                         gconstpointer  data)
{
	LrgNode      *lights = add (f, "Lights", NULL);
	LrgNode      *sun = add (f, "Sun", lights);
	LrgNode      *ground = add (f, "Ground", NULL);
	LrgTextInput *search;
	gfloat        x, y, w, h;

	lrg_editor_select (f->editor, NULL, FALSE);
	setup_frames (f);

	/* The eye toggle at the trailing edge hides the node (undoable) and
	 * does not select */
	g_assert_true (lrg_tree_view_get_row_rect (tree (f), G_OBJECT (ground), &x, &y, &w, &h));
	{
		LrgWidget *row = lrg_tree_view_get_row_widget (tree (f), lrg_tree_view_get_position (tree (f), G_OBJECT (ground)));
		gfloat     rx, ry, rw, rh;

		g_assert_nonnull (row);
		efx_rect_of (row, &rx, &ry, &rw, &rh);
		/* Eye: second-to-last square of the row */
		efx_click (f, rx + rw - 4.0f - rh * 1.5f, ry + rh * 0.5f);
	}
	g_assert_false (lrg_node_get_visible (ground));
	g_assert_cmpuint (lrg_editor_selection_get_count (sel (f)), ==, 0);
	lrg_editor_undo (f->editor);
	g_assert_true (lrg_node_get_visible (ground));
	efx_frames (f, 40);

	/* Ctrl+F focuses the search field; typing filters */
	efx_key (f, GRL_KEY_F, CTRL);
	search = lrg_editor_hierarchy_panel_get_search_input (panel (f));
	g_assert_true (lrg_canvas_get_focused_widget (lrg_editor_app_get_canvas (f->app)) == LRG_WIDGET (search));
	efx_type (f, "su");
	efx_frames (f, 2);
	g_assert_cmpstr (lrg_editor_node_model_get_filter (lrg_editor_hierarchy_panel_get_node_model (panel (f))), ==, "su");
	g_assert_cmpuint (lrg_tree_view_get_n_visible (tree (f)), ==, 2);   /* Lights (expanded) and Sun */
	g_assert_true (lrg_tree_view_get_item (tree (f), 1) == G_OBJECT (sun));

	/* Cleared from code */
	lrg_editor_hierarchy_panel_set_filter (panel (f), NULL);
	efx_frames (f, 2);
	g_assert_cmpstr (lrg_text_input_get_text (search), ==, "");
	g_assert_cmpuint (lrg_tree_view_get_n_visible (tree (f)), ==, 3);
}

/* ==========================================================================
 * Live tab
 * ========================================================================== */

static void
on_live_selected (LrgEditorHierarchyPanel *p,
                  GObject                 *object,
                  gpointer                 user_data)
{
	GObject **out = user_data;

	*out = object;
	*(out + 1) = GINT_TO_POINTER (GPOINTER_TO_INT (*(out + 1)) + 1);
}

static void
rect_member (JsonObject *o,
             const gchar *name,
             gfloat      *x,
             gfloat      *y,
             gfloat      *w,
             gfloat      *h)
{
	JsonArray *a = json_object_get_array_member (o, name);

	*x = (gfloat) json_array_get_double_element (a, 0);
	*y = (gfloat) json_array_get_double_element (a, 1);
	*w = (gfloat) json_array_get_double_element (a, 2);
	*h = (gfloat) json_array_get_double_element (a, 3);
}

static void
test_live_tab (Fixture       *f,
               gconstpointer  data)
{
	LrgEditorHierarchyPanel *p;
	LrgTreeView             *live;
	GObject                 *got[2] = { NULL, NULL };
	gfloat                   x, y, w, h;

	add (f, "Alpha", NULL);
	add (f, "Beta", NULL);
	setup_frames (f);
	p = panel (f);
	live = lrg_editor_hierarchy_panel_get_live_view (p);
	g_signal_connect (p, "live-selected", G_CALLBACK (on_live_selected), got);

	/* No Live tab while editing */
	g_assert_false (lrg_editor_hierarchy_panel_set_page (p, "live"));
	{
		g_autoptr(JsonNode) s = efx_ipc_result (f, "{'id':1,'method':'hierarchy.state'}");

		g_assert_false (json_object_get_boolean_member (json_node_get_object (s), "live"));
		g_assert_false (json_object_has_member (json_node_get_object (s), "tab-live"));
	}

	/* Play: the tab appears; click it */
	g_assert_true (lrg_editor_app_activate_action (f->app, "play.start"));
	efx_frames (f, 3);
	{
		g_autoptr(JsonNode) s = efx_ipc_result (f, "{'id':2,'method':'hierarchy.state'}");

		g_assert_true (json_object_get_boolean_member (json_node_get_object (s), "live"));
		rect_member (json_node_get_object (s), "tab-live", &x, &y, &w, &h);
	}
	efx_click (f, x + w * 0.5f, y + h * 0.5f);
	efx_frames (f, 2);
	g_assert_cmpstr (lrg_editor_hierarchy_panel_get_page (p), ==, "live");
	g_assert_true (lrg_widget_get_visible (LRG_WIDGET (live)));
	g_assert_false (lrg_widget_get_visible (LRG_WIDGET (tree (f))));
	g_assert_cmpuint (lrg_tree_view_get_n_visible (live), >=, 2);

	/* Selecting a live object emits live-selected */
	{
		GObject *first = lrg_tree_view_get_item (live, 0);
		gfloat   rx, ry, rw, rh;

		g_assert_true (lrg_tree_view_get_row_rect (live, first, &rx, &ry, &rw, &rh));
		efx_click (f, rx + rw * 0.3f, ry + rh * 0.5f);
		g_assert_true (got[0] == first);
		g_assert_true (lrg_editor_hierarchy_panel_get_live_selection (p) == first);
		g_assert_true (LRG_IS_ENTITY3D (first));
	}

	/* Stop: the tab goes away, the live selection is cleared */
	g_assert_true (lrg_editor_app_activate_action (f->app, "play.stop"));
	efx_frames (f, 2);
	g_assert_cmpstr (lrg_editor_hierarchy_panel_get_page (p), ==, "scene");
	g_assert_null (got[0]);
	g_assert_null (lrg_editor_hierarchy_panel_get_live_selection (p));
	g_assert_true (lrg_widget_get_visible (LRG_WIDGET (tree (f))));
}

/* ==========================================================================
 * IPC
 * ========================================================================== */

static void
test_ipc (Fixture       *f,
          gconstpointer  data)
{
	LrgNode          *a = add (f, "Alpha", NULL);
	LrgNode          *b = add (f, "Beta", a);
	g_autofree gchar *req = NULL;
	g_autofree gchar *reply = NULL;
	JsonNode         *r;
	gint64            n;

	lrg_editor_select (f->editor, NULL, FALSE);
	lrg_tree_view_collapse_all (tree (f));
	setup_frames (f);

	/* reveal: expands and selects */
	req = g_strdup_printf ("{'id':1,'method':'hierarchy.reveal','params':{'guid':'%s'}}", lrg_node_get_guid (b));
	r = efx_ipc_result (f, req);
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "revealed"));
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (r), "row"), ==, 1);
	json_node_unref (r);
	g_assert_true (lrg_tree_view_is_expanded (tree (f), G_OBJECT (a)));
	g_assert_true (lrg_editor_selection_get_primary (sel (f)) == b);

	/* rename without a name: inline editing starts */
	g_clear_pointer (&req, g_free);
	req = g_strdup_printf ("{'id':2,'method':'hierarchy.rename','params':{'guid':'%s'}}", lrg_node_get_guid (a));
	r = efx_ipc_result (f, req);
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "editing"));
	json_node_unref (r);
	efx_frames (f, 2);
	g_assert_true (lrg_tree_view_get_editing_item (tree (f)) == G_OBJECT (a));
	efx_key (f, GRL_KEY_ESCAPE, NOMOD);

	/* rename with a name: done, undoable */
	g_clear_pointer (&req, g_free);
	req = g_strdup_printf ("{'id':3,'method':'hierarchy.rename','params':{'guid':'%s','name':'Root A'}}",
	                       lrg_node_get_guid (a));
	r = efx_ipc_result (f, req);
	json_node_unref (r);
	g_assert_cmpstr (lrg_node_get_name (a), ==, "Root A");

	/* filter: returns the match count */
	r = efx_ipc_result (f, "{'id':4,'method':'hierarchy.filter','params':{'text':'beta'}}");
	n = json_node_get_int (r);
	json_node_unref (r);
	g_assert_cmpint (n, ==, 1);

	/* state: rows with rects */
	r = efx_ipc_result (f, "{'id':5,'method':'hierarchy.state'}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "filter"), ==, "beta");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (r), "rows")), ==, 2);
	json_node_unref (r);

	/* errors */
	reply = efx_ipc (f, "{'id':6,'method':'hierarchy.reveal','params':{'guid':'nope'}}");
	g_assert_nonnull (strstr (reply, "not-found"));
}

#endif /* LRG_BUILD_EDITOR_UI */

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

#ifdef LRG_BUILD_EDITOR_UI
#define ADD(path, func) \
	g_test_add ("/editor-app/hierarchy/" path, Fixture, NULL, efx_setup, func, efx_teardown)

	ADD ("click-selection", test_click_selection);
	ADD ("external-select-reveals", test_external_select_reveals);
	ADD ("rename", test_rename_f2);
	ADD ("context-menu", test_context_menu_duplicate);
	ADD ("prefab-menu", test_prefab_menu);
	ADD ("dnd-reparent", test_dnd_reparent);
	ADD ("dnd-reorder", test_dnd_reorder);
	ADD ("dnd-refuse-subtree", test_dnd_refuses_own_subtree);
	ADD ("dnd-alt-keeps-world", test_dnd_alt_keeps_world);
	ADD ("asset-drop", test_asset_drop);
	ADD ("toggles-filter", test_toggles_and_filter);
	ADD ("live-tab", test_live_tab);
	ADD ("ipc", test_ipc);
#undef ADD
#endif

	return g_test_run ();
}
