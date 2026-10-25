/* test-editor-core.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the editor core added for the full editor application:
 * node tree helpers, snapshot/group/index-preserving commands, dirty
 * tracking, history navigation, clipboard, duplication, keep-world moves,
 * and the headless math/picking module.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#define EPS (1e-4)

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static LrgNode *
make_cube (const gchar *name,
           gfloat       x,
           gfloat       y,
           gfloat       z)
{
	LrgNode                  *node = lrg_node_new (name);
	g_autoptr(LrgNodeVisual)  vis = lrg_node_visual_new (LRG_NODE_VISUAL_PRIMITIVE);

	lrg_node_visual_set_primitive (vis, LRG_PRIMITIVE_CUBE);
	lrg_node_set_visual (node, vis);
	lrg_node_set_location_xyz (node, x, y, z);
	return node;
}

/* Adds a cube under @parent (NULL = root) through the editor. */
static LrgNode *
add_cube (LrgEditor   *editor,
          const gchar *name,
          LrgNode     *parent,
          gfloat       x)
{
	g_autoptr(LrgNode) node = make_cube (name, x, 0.0f, 0.0f);

	lrg_editor_add_node (editor, node, parent);
	return node;  /* borrowed: the level holds a reference */
}

static LrgNode *
root_of (LrgEditor *editor)
{
	return lrg_level_get_root (lrg_editor_get_level (editor));
}

static const gchar *
child_name (LrgNode *parent,
            guint    i)
{
	return lrg_node_get_name (g_ptr_array_index (lrg_node_get_children (parent), i));
}

/* ==========================================================================
 * Node helpers
 * ========================================================================== */

static void
test_node_insert_index_path (void)
{
	g_autoptr(LrgNode) root = lrg_node_new ("root");
	g_autoptr(LrgNode) a = lrg_node_new ("a");
	g_autoptr(LrgNode) b = lrg_node_new ("b");
	g_autoptr(LrgNode) c = lrg_node_new ("c");
	g_autofree gchar  *path = NULL;

	lrg_node_add_child (root, a);
	lrg_node_add_child (root, c);
	lrg_node_insert_child (root, b, 1);

	g_assert_cmpstr (child_name (root, 0), ==, "a");
	g_assert_cmpstr (child_name (root, 1), ==, "b");
	g_assert_cmpstr (child_name (root, 2), ==, "c");
	g_assert_cmpint (lrg_node_get_child_index (root, c), ==, 2);
	g_assert_cmpint (lrg_node_get_child_index (a, c), ==, -1);

	g_assert_true (lrg_node_is_ancestor_of (root, c));
	g_assert_true (lrg_node_is_ancestor_of (c, c));
	g_assert_false (lrg_node_is_ancestor_of (c, root));
	g_assert_cmpuint (lrg_node_get_depth (c), ==, 1);

	/* The parentless top node is skipped in paths unless it is the target. */
	path = lrg_node_get_path (c);
	g_assert_cmpstr (path, ==, "c");
	g_assert_true (lrg_node_find_by_path (root, "/b/") == b);
	g_assert_null (lrg_node_find_by_path (root, "b/missing"));
}

static void
test_node_copy_state_and_equal (void)
{
	g_autoptr(LrgNode)          a = make_cube ("crate", 1.0f, 2.0f, 3.0f);
	g_autoptr(LrgNode)          b = lrg_node_new (NULL);
	g_autoptr(LrgComponentDesc) desc = lrg_component_desc_new ("health");
	g_autoptr(LrgScriptBinding) script = lrg_script_binding_new (LRG_SCRIPT_LANGUAGE_LUA, "crate.lua");
	g_autofree gchar           *guid_b = NULL;

	lrg_component_desc_set_int (desc, "max", 10);
	lrg_node_add_component (a, desc);
	lrg_node_add_script (a, script);
	lrg_node_set_locked (a, TRUE);

	guid_b = g_strdup (lrg_node_get_guid (b));
	g_assert_false (lrg_node_state_equal (a, b));

	lrg_node_copy_state (b, a);
	g_assert_true (lrg_node_state_equal (a, b));
	g_assert_cmpstr (lrg_node_get_guid (b), ==, guid_b);

	/* Deep copy: editing the copy must not alias the source. */
	lrg_component_desc_set_int (g_ptr_array_index (lrg_node_get_components (b), 0), "max", 99);
	g_assert_false (lrg_node_state_equal (a, b));
	lrg_node_visual_set_primitive (lrg_node_get_visual (b), LRG_PRIMITIVE_UV_SPHERE);
	g_assert_cmpint (lrg_node_visual_get_primitive (lrg_node_get_visual (a)), ==, LRG_PRIMITIVE_CUBE);
}

static void
test_node_component_script_editing (void)
{
	g_autoptr(LrgNode)          n = lrg_node_new ("n");
	g_autoptr(LrgComponentDesc) c1 = lrg_component_desc_new ("one");
	g_autoptr(LrgComponentDesc) c2 = lrg_component_desc_new ("two");
	g_autoptr(LrgScriptBinding) s1 = lrg_script_binding_new (LRG_SCRIPT_LANGUAGE_PYTHON, "a.py");

	lrg_node_add_component (n, c2);
	lrg_node_insert_component (n, c1, 0);
	g_assert_true (lrg_node_find_component (n, "two") == c2);
	g_assert_true (g_ptr_array_index (lrg_node_get_components (n), 0) == c1);
	g_assert_true (lrg_node_remove_component_at (n, 0));
	g_assert_false (lrg_node_remove_component_at (n, 5));
	g_assert_null (lrg_node_find_component (n, "one"));

	lrg_node_insert_script (n, s1, -1);
	g_assert_cmpuint (lrg_node_get_scripts (n)->len, ==, 1);
	g_assert_true (lrg_node_remove_script_at (n, 0));
	g_assert_cmpuint (lrg_node_get_scripts (n)->len, ==, 0);
}

/* ==========================================================================
 * Commands, history and dirty tracking
 * ========================================================================== */

static void
test_delete_undo_restores_index (void)
{
	g_autoptr(LrgEditor) ed = lrg_editor_new ();
	LrgNode             *b;

	add_cube (ed, "a", NULL, 0);
	b = add_cube (ed, "b", NULL, 1);
	add_cube (ed, "c", NULL, 2);

	g_assert_true (lrg_editor_delete_node (ed, b));
	g_assert_cmpuint (lrg_node_get_n_children (root_of (ed)), ==, 2);

	lrg_editor_undo (ed);
	g_assert_cmpstr (child_name (root_of (ed), 1), ==, "b");
}

static void
test_dirty_tracking (void)
{
	g_autoptr(LrgEditor) ed = lrg_editor_new ();
	g_autofree gchar    *dir = g_dir_make_tmp ("lrg-editor-XXXXXX", NULL);
	g_autofree gchar    *path = g_build_filename (dir, "t.rlevel", NULL);
	g_autoptr(GError)    error = NULL;
	LrgNode             *a;
	gfloat               loc[3] = { 5.0f, 0.0f, 0.0f };

	g_assert_false (lrg_editor_is_dirty (ed));
	a = add_cube (ed, "a", NULL, 0);
	g_assert_true (lrg_editor_is_dirty (ed));

	g_assert_true (lrg_editor_save_level (ed, path, &error));
	g_assert_no_error (error);
	g_assert_false (lrg_editor_is_dirty (ed));
	g_assert_cmpstr (lrg_editor_get_path (ed), ==, path);

	/* Undo past the save point is dirty; redo back is clean again. */
	lrg_editor_undo (ed);
	g_assert_true (lrg_editor_is_dirty (ed));
	lrg_editor_redo (ed);
	g_assert_false (lrg_editor_is_dirty (ed));

	/* A merge into the saved command makes the document dirty. */
	lrg_editor_transform_node (ed, a, loc, NULL, NULL, FALSE);
	g_assert_true (lrg_editor_save (ed, &error));
	loc[0] = 6.0f;
	lrg_editor_transform_node (ed, a, loc, NULL, NULL, TRUE);
	g_assert_true (lrg_editor_is_dirty (ed));
	g_assert_cmpuint (lrg_editor_get_undo_count (ed), ==, 2);

	g_assert_true (lrg_editor_reload (ed, &error));
	g_assert_no_error (error);
	g_assert_false (lrg_editor_is_dirty (ed));
	g_assert_cmpfloat_with_epsilon (
		lrg_node_get_location (g_ptr_array_index (lrg_node_get_children (root_of (ed)), 0))->x,
		5.0f, EPS);

	g_unlink (path);
	g_rmdir (dir);
}

static void
test_group_is_one_step (void)
{
	g_autoptr(LrgEditor) ed = lrg_editor_new ();
	g_autofree gchar    *desc = NULL;

	lrg_editor_begin_group (ed, "Build Room");
	lrg_editor_begin_group (ed, "inner");   /* nested groups fold in */
	add_cube (ed, "wall1", NULL, 0);
	add_cube (ed, "wall2", NULL, 1);
	lrg_editor_end_group (ed);
	add_cube (ed, "floor", NULL, 2);
	lrg_editor_end_group (ed);

	g_assert_cmpuint (lrg_editor_get_undo_count (ed), ==, 1);
	desc = lrg_editor_command_describe (lrg_editor_get_undo_command (ed, 0));
	g_assert_cmpstr (desc, ==, "Build Room");

	lrg_editor_undo (ed);
	g_assert_cmpuint (lrg_node_get_n_children (root_of (ed)), ==, 0);
	lrg_editor_redo (ed);
	g_assert_cmpuint (lrg_node_get_n_children (root_of (ed)), ==, 3);
	g_assert_cmpstr (child_name (root_of (ed), 2), ==, "floor");

	/* An empty group records nothing. */
	lrg_editor_begin_group (ed, "nothing");
	lrg_editor_end_group (ed);
	g_assert_cmpuint (lrg_editor_get_undo_count (ed), ==, 1);
}

static void
test_history_jump_and_limit (void)
{
	g_autoptr(LrgEditor) ed = lrg_editor_new ();
	guint                i;

	for (i = 0; i < 5; i++)
	{
		g_autofree gchar *n = g_strdup_printf ("n%u", i);

		add_cube (ed, n, NULL, (gfloat) i);
	}

	lrg_editor_jump_to_history (ed, 2);
	g_assert_cmpuint (lrg_editor_get_undo_count (ed), ==, 2);
	g_assert_cmpuint (lrg_editor_get_redo_count (ed), ==, 3);
	g_assert_cmpuint (lrg_node_get_n_children (root_of (ed)), ==, 2);
	{
		g_autofree gchar *next = lrg_editor_command_describe (lrg_editor_get_redo_command (ed, 0));

		g_assert_cmpstr (next, ==, "Add n2");
	}

	lrg_editor_jump_to_history (ed, 5);
	g_assert_cmpuint (lrg_node_get_n_children (root_of (ed)), ==, 5);

	lrg_editor_set_history_limit (ed, 3);
	g_assert_cmpuint (lrg_editor_get_undo_count (ed), ==, 3);
}

static void
test_snapshot_commit_and_components (void)
{
	g_autoptr(LrgEditor)        ed = lrg_editor_new ();
	g_autoptr(LrgComponentDesc) desc = lrg_component_desc_new ("health");
	GValue                      v = G_VALUE_INIT;
	LrgNode                    *a;
	LrgComponentDesc           *live;

	a = add_cube (ed, "a", NULL, 0);
	lrg_editor_add_component (ed, a, desc);
	g_assert_cmpuint (lrg_node_get_components (a)->len, ==, 1);

	/* Consecutive edits of one key merge into one step (a drag). */
	g_value_init (&v, G_TYPE_DOUBLE);
	g_value_set_double (&v, 1.0);
	g_assert_true (lrg_editor_set_component_value (ed, a, 0, "hp", &v, TRUE));
	g_value_set_double (&v, 2.0);
	g_assert_true (lrg_editor_set_component_value (ed, a, 0, "hp", &v, TRUE));
	g_value_unset (&v);
	g_assert_cmpuint (lrg_editor_get_undo_count (ed), ==, 3);

	live = g_ptr_array_index (lrg_node_get_components (a), 0);
	g_assert_cmpfloat_with_epsilon (g_value_get_double (lrg_component_desc_get_value (live, "hp")), 2.0, EPS);

	lrg_editor_undo (ed);   /* undo the whole drag */
	live = g_ptr_array_index (lrg_node_get_components (a), 0);
	g_assert_false (lrg_component_desc_has (live, "hp"));

	lrg_editor_undo (ed);   /* undo add component */
	g_assert_cmpuint (lrg_node_get_components (a)->len, ==, 0);

	/* A commit with no change records nothing. */
	{
		g_autoptr(LrgNode) snap = lrg_editor_snapshot_node (a);

		g_assert_false (lrg_editor_commit_node_state (ed, a, snap, "noop", FALSE));
	}
}

static void
test_visual_and_script_edits (void)
{
	g_autoptr(LrgEditor)        ed = lrg_editor_new ();
	g_autoptr(LrgNodeVisual)    light = lrg_node_visual_new (LRG_NODE_VISUAL_LIGHT);
	g_autoptr(LrgScriptBinding) script = lrg_script_binding_new (LRG_SCRIPT_LANGUAGE_LUA, "a.lua");
	LrgNode                    *a;

	a = add_cube (ed, "a", NULL, 0);
	lrg_editor_set_visual (ed, a, light);
	g_assert_cmpint (lrg_node_visual_get_kind (lrg_node_get_visual (a)), ==, LRG_NODE_VISUAL_LIGHT);
	g_assert_false (lrg_node_get_visual (a) == light);   /* copied */

	lrg_editor_add_script (ed, a, script);
	g_assert_cmpuint (lrg_node_get_scripts (a)->len, ==, 1);
	g_assert_true (lrg_editor_remove_script (ed, a, 0));
	g_assert_cmpuint (lrg_node_get_scripts (a)->len, ==, 0);

	lrg_editor_undo (ed);
	lrg_editor_undo (ed);
	lrg_editor_undo (ed);
	g_assert_cmpint (lrg_node_visual_get_kind (lrg_node_get_visual (a)), ==, LRG_NODE_VISUAL_PRIMITIVE);
}

static void
test_object_property_coercion (void)
{
	g_autoptr(LrgEditor) ed = lrg_editor_new ();
	GValue               v = G_VALUE_INIT;
	LrgNode             *a;

	a = add_cube (ed, "a", NULL, 0);

	/* A string "true" is not transformable to boolean by GLib; an int is. */
	g_value_init (&v, G_TYPE_INT);
	g_value_set_int (&v, 0);
	g_assert_true (lrg_editor_set_object_property (ed, G_OBJECT (a), "visible", &v, FALSE));
	g_value_unset (&v);
	g_assert_false (lrg_node_get_visible (a));

	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "x");
	g_assert_false (lrg_editor_set_object_property (ed, G_OBJECT (a), "no-such-prop", &v, FALSE));
	g_value_unset (&v);

	lrg_editor_rename_node (ed, a, "renamed");
	g_assert_cmpstr (lrg_node_get_name (a), ==, "renamed");
	lrg_editor_undo (ed);
	g_assert_cmpstr (lrg_node_get_name (a), ==, "a");
}

/* ==========================================================================
 * Selection-aware structure operations
 * ========================================================================== */

static void
test_selection_pruned_on_remove (void)
{
	g_autoptr(LrgEditor) ed = lrg_editor_new ();
	LrgNode             *parent;
	LrgNode             *child;

	parent = add_cube (ed, "parent", NULL, 0);
	child = add_cube (ed, "child", parent, 0);
	lrg_editor_select (ed, child, FALSE);

	/* Undoing the add of the selected child deselects it. */
	lrg_editor_undo (ed);
	g_assert_cmpuint (lrg_editor_selection_get_count (lrg_editor_get_selection (ed)), ==, 0);
	lrg_editor_redo (ed);

	/* Deleting an ancestor deselects descendants. */
	lrg_editor_select (ed, child, FALSE);
	lrg_editor_delete_node (ed, parent);
	g_assert_cmpuint (lrg_editor_selection_get_count (lrg_editor_get_selection (ed)), ==, 0);
}

static void
test_selected_roots_and_delete_selection (void)
{
	g_autoptr(LrgEditor) ed = lrg_editor_new ();
	g_autoptr(GPtrArray) roots = NULL;
	LrgNode             *a, *b, *c, *a1;

	a = add_cube (ed, "a", NULL, 0);
	a1 = add_cube (ed, "a1", a, 0);
	b = add_cube (ed, "b", NULL, 1);
	c = add_cube (ed, "c", NULL, 2);

	lrg_editor_select (ed, c, FALSE);
	lrg_editor_select (ed, a1, TRUE);
	lrg_editor_select (ed, a, TRUE);
	roots = lrg_editor_get_selected_roots (ed);
	g_assert_cmpuint (roots->len, ==, 2);
	g_assert_true (g_ptr_array_index (roots, 0) == a);   /* tree order */
	g_assert_true (g_ptr_array_index (roots, 1) == c);

	g_assert_cmpuint (lrg_editor_delete_selection (ed), ==, 2);
	g_assert_cmpuint (lrg_node_get_n_children (root_of (ed)), ==, 1);
	g_assert_cmpuint (lrg_editor_get_undo_count (ed), ==, 5);

	lrg_editor_undo (ed);
	g_assert_cmpstr (child_name (root_of (ed), 0), ==, "a");
	g_assert_cmpstr (child_name (root_of (ed), 2), ==, "c");
	g_assert_true (lrg_node_get_parent (a1) == a);
	(void) b;
}

static void
test_duplicate_names_and_placement (void)
{
	g_autoptr(LrgEditor) ed = lrg_editor_new ();
	g_autoptr(GPtrArray) dup = NULL;
	g_autoptr(GPtrArray) dup2 = NULL;
	LrgNode             *crate;

	crate = add_cube (ed, "Crate", NULL, 0);
	add_cube (ed, "Other", NULL, 1);
	add_cube (ed, "Child", crate, 0);

	lrg_editor_select (ed, crate, FALSE);
	dup = lrg_editor_duplicate_selection (ed);
	g_assert_cmpuint (dup->len, ==, 1);
	g_assert_cmpstr (child_name (root_of (ed), 1), ==, "Crate2");
	g_assert_cmpuint (lrg_node_get_n_children (g_ptr_array_index (dup, 0)), ==, 1);
	g_assert_cmpstr (lrg_node_get_guid (g_ptr_array_index (dup, 0)), !=, lrg_node_get_guid (crate));
	g_assert_true (lrg_editor_selection_get_primary (lrg_editor_get_selection (ed)) ==
	               g_ptr_array_index (dup, 0));

	/* Duplicating the copy continues the counter. */
	dup2 = lrg_editor_duplicate_selection (ed);
	g_assert_cmpstr (lrg_node_get_name (g_ptr_array_index (dup2, 0)), ==, "Crate3");

	lrg_editor_undo (ed);
	lrg_editor_undo (ed);
	g_assert_cmpuint (lrg_node_get_n_children (root_of (ed)), ==, 2);
}

static void
test_clipboard_roundtrip (void)
{
	g_autoptr(LrgEditor) ed = lrg_editor_new ();
	g_autoptr(GPtrArray) pasted = NULL;
	g_autoptr(GError)    error = NULL;
	g_autofree gchar    *data = NULL;
	LrgNode             *a, *target;

	a = add_cube (ed, "a", NULL, 0);
	add_cube (ed, "kid", a, 0);
	target = add_cube (ed, "target", NULL, 1);

	lrg_editor_select (ed, a, FALSE);
	data = lrg_editor_cut_selection (ed);
	g_assert_nonnull (data);
	g_assert_nonnull (strstr (data, "kid"));
	g_assert_cmpuint (lrg_node_get_n_children (root_of (ed)), ==, 1);

	pasted = lrg_editor_paste (ed, NULL, target, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (pasted->len, ==, 1);
	g_assert_true (lrg_node_get_parent (g_ptr_array_index (pasted, 0)) == target);
	g_assert_cmpuint (lrg_node_get_n_children (g_ptr_array_index (pasted, 0)), ==, 1);

	lrg_editor_undo (ed);
	g_assert_cmpuint (lrg_node_get_n_children (target), ==, 0);

	/* Garbage is an error and records nothing. */
	{
		guint      before = lrg_editor_get_undo_count (ed);
		GPtrArray *bad;

		bad = lrg_editor_paste (ed, "this: [is: not, a level", NULL, &error);
		g_assert_null (bad);
		g_assert_nonnull (error);
		g_clear_error (&error);
		g_assert_cmpuint (lrg_editor_get_undo_count (ed), ==, before);
	}
}

static void
test_move_reorder_and_keep_world (void)
{
	g_autoptr(LrgEditor) ed = lrg_editor_new ();
	LrgNode             *a, *b, *c, *parent;
	gfloat               before[3], after[3];

	a = add_cube (ed, "a", NULL, 0);
	b = add_cube (ed, "b", NULL, 1);
	c = add_cube (ed, "c", NULL, 2);

	/* Reorder within the same parent. */
	g_assert_true (lrg_editor_move_node (ed, c, NULL, 0, FALSE));
	g_assert_cmpstr (child_name (root_of (ed), 0), ==, "c");
	g_assert_false (lrg_editor_move_node (ed, c, NULL, 0, FALSE));   /* no-op */
	lrg_editor_undo (ed);
	g_assert_cmpstr (child_name (root_of (ed), 2), ==, "c");

	/* Cycles are refused. */
	lrg_editor_move_node (ed, b, a, -1, FALSE);
	g_assert_false (lrg_editor_move_node (ed, a, b, -1, FALSE));

	/* Keep-world: reparent under a rotated, scaled, offset parent. */
	parent = add_cube (ed, "parent", NULL, 3);
	lrg_node_set_location_xyz (parent, 10.0f, 2.0f, -3.0f);
	lrg_node_set_rotation_xyz (parent, 0.3f, 1.1f, -0.4f);
	lrg_node_set_scale_xyz (parent, 2.0f, 2.0f, 2.0f);
	lrg_node_set_location_xyz (c, 1.0f, 4.0f, 7.0f);
	lrg_node_set_rotation_xyz (c, 0.2f, 0.0f, 0.1f);

	lrg_editor_node_get_world_position (c, before);
	g_assert_true (lrg_editor_move_node (ed, c, parent, -1, TRUE));
	lrg_editor_node_get_world_position (c, after);
	g_assert_cmpfloat_with_epsilon (before[0], after[0], 1e-3);
	g_assert_cmpfloat_with_epsilon (before[1], after[1], 1e-3);
	g_assert_cmpfloat_with_epsilon (before[2], after[2], 1e-3);

	/* The move + transform is a single undo step. */
	lrg_editor_undo (ed);
	g_assert_true (lrg_node_get_parent (c) == root_of (ed));
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (c)->y, 4.0f, EPS);
}

static void
test_find_nodes (void)
{
	g_autoptr(LrgEditor) ed = lrg_editor_new ();
	g_autoptr(GPtrArray) found = NULL;
	LrgNode             *house, *door;

	house = add_cube (ed, "House", NULL, 0);
	door = add_cube (ed, "FrontDoor", house, 0);
	add_cube (ed, "BackDoor", house, 1);

	found = lrg_editor_find_nodes (ed, "door");
	g_assert_cmpuint (found->len, ==, 2);
	g_assert_true (lrg_editor_find_node (ed, "House/FrontDoor") == door);
	g_assert_true (lrg_editor_find_node (ed, lrg_node_get_guid (door)) == door);
	g_assert_null (lrg_editor_find_node (ed, "nope"));
}

static void
test_prefab_instantiate_records_source (void)
{
	g_autoptr(LrgEditor) ed = lrg_editor_new ();
	g_autoptr(LrgNode)   src = make_cube ("Barrel", 0, 0, 0);
	g_autoptr(GError)    error = NULL;
	g_autofree gchar    *dir = g_dir_make_tmp ("lrg-prefab-XXXXXX", NULL);
	g_autofree gchar    *path = g_build_filename (dir, "barrel.rprefab", NULL);
	LrgNode             *inst1, *inst2;
	const GValue        *v;

	g_assert_true (lrg_prefab_save (src, path, &error));
	inst1 = lrg_editor_instantiate_prefab (ed, path, NULL, &error);
	g_assert_no_error (error);
	inst2 = lrg_editor_instantiate_prefab (ed, path, NULL, &error);
	g_assert_cmpstr (lrg_node_get_name (inst1), ==, "Barrel");
	g_assert_cmpstr (lrg_node_get_name (inst2), ==, "Barrel2");

	v = lrg_node_visual_get_param (lrg_node_get_visual (inst1), "prefab:source");
	g_assert_nonnull (v);
	g_assert_cmpstr (g_value_get_string (v), ==, path);

	g_unlink (path);
	g_rmdir (dir);
}

/* ==========================================================================
 * Math
 * ========================================================================== */

static void
test_math_compose_decompose (void)
{
	const gfloat loc[3] = { 1.5f, -2.0f, 3.25f };
	const gfloat rot[3] = { 0.4f, -0.7f, 1.2f };
	const gfloat scl[3] = { 2.0f, 0.5f, 3.0f };
	gfloat       m[16], inv[16], id[16];
	gfloat       l[3], r[3], s[3];
	gint         i;

	lrg_editor_math_compose (loc, rot, scl, m);
	g_assert_true (lrg_editor_math_decompose (m, l, r, s));
	for (i = 0; i < 3; i++)
	{
		g_assert_cmpfloat_with_epsilon (l[i], loc[i], EPS);
		g_assert_cmpfloat_with_epsilon (r[i], rot[i], EPS);
		g_assert_cmpfloat_with_epsilon (s[i], scl[i], EPS);
	}

	g_assert_true (lrg_editor_math_invert (m, inv));
	lrg_editor_math_multiply (m, inv, id);
	for (i = 0; i < 16; i++)
		g_assert_cmpfloat_with_epsilon (id[i], (i % 5 == 0) ? 1.0f : 0.0f, 1e-4);

	/* Rotating +X by 90 degrees about Z gives +Y. */
	{
		const gfloat z90[3] = { 0.0f, 0.0f, (gfloat) G_PI_2 };
		const gfloat zero[3] = { 0.0f, 0.0f, 0.0f };
		const gfloat one[3] = { 1.0f, 1.0f, 1.0f };
		const gfloat px[3] = { 1.0f, 0.0f, 0.0f };
		gfloat       out[3];

		lrg_editor_math_compose (zero, z90, one, m);
		lrg_editor_math_transform_point (m, px, out);
		g_assert_cmpfloat_with_epsilon (out[0], 0.0f, EPS);
		g_assert_cmpfloat_with_epsilon (out[1], 1.0f, EPS);
	}
}

static void
test_math_world_matrix_hierarchy (void)
{
	g_autoptr(LrgNode) parent = lrg_node_new ("p");
	g_autoptr(LrgNode) child = lrg_node_new ("c");
	gfloat             pos[3];

	lrg_node_set_location_xyz (parent, 10.0f, 0.0f, 0.0f);
	lrg_node_set_rotation_xyz (parent, 0.0f, (gfloat) G_PI_2, 0.0f);  /* +X -> -Z */
	lrg_node_set_scale_xyz (parent, 2.0f, 2.0f, 2.0f);
	lrg_node_set_location_xyz (child, 1.0f, 0.0f, 0.0f);
	lrg_node_add_child (parent, child);

	lrg_editor_node_get_world_position (child, pos);
	g_assert_cmpfloat_with_epsilon (pos[0], 10.0f, EPS);
	g_assert_cmpfloat_with_epsilon (pos[1], 0.0f, EPS);
	g_assert_cmpfloat_with_epsilon (pos[2], -2.0f, EPS);
}

static void
test_view_project_and_ray (void)
{
	LrgEditorView view;
	const gfloat  origin[3] = { 0.0f, 0.0f, 0.0f };
	gfloat        sx, sy, depth;
	gfloat        ro[3], rd[3];
	gfloat        t;
	const gfloat  normal[3] = { 0.0f, 1.0f, 0.0f };

	lrg_editor_view_init_default (&view, 800.0f, 600.0f);

	/* The target projects to the viewport centre. */
	g_assert_true (lrg_editor_view_project (&view, origin, &sx, &sy, &depth));
	g_assert_cmpfloat_with_epsilon (sx, 400.0f, 1e-2);
	g_assert_cmpfloat_with_epsilon (sy, 300.0f, 1e-2);
	g_assert_cmpfloat (depth, >, 0.0f);

	/* The ray through a projected point passes through it. */
	{
		const gfloat p[3] = { 1.0f, 0.0f, -2.0f };
		gfloat       hit[3];
		gint         i;

		lrg_editor_view_project (&view, p, &sx, &sy, NULL);
		lrg_editor_view_get_ray (&view, sx, sy, ro, rd);
		g_assert_true (lrg_editor_ray_plane (ro, rd, origin, normal, &t));
		for (i = 0; i < 3; i++)
			hit[i] = ro[i] + rd[i] * t;
		g_assert_cmpfloat_with_epsilon (hit[0], 1.0f, 1e-3);
		g_assert_cmpfloat_with_epsilon (hit[2], -2.0f, 1e-3);
	}

	/* Same for orthographic. */
	view.orthographic = TRUE;
	{
		const gfloat p[3] = { -3.0f, 0.0f, 1.0f };
		gfloat       hit[3];
		gint         i;

		lrg_editor_view_project (&view, p, &sx, &sy, NULL);
		lrg_editor_view_get_ray (&view, sx, sy, ro, rd);
		g_assert_true (lrg_editor_ray_plane (ro, rd, origin, normal, &t));
		for (i = 0; i < 3; i++)
			hit[i] = ro[i] + rd[i] * t;
		g_assert_cmpfloat_with_epsilon (hit[0], -3.0f, 1e-3);
		g_assert_cmpfloat_with_epsilon (hit[2], 1.0f, 1e-3);
	}
}

static void
test_view_navigation (void)
{
	LrgEditorView view;
	gfloat        dist_before, dist_after;
	const gfloat  center[3] = { 5.0f, 1.0f, 5.0f };
	const gfloat  top[3] = { 0.0f, 1.0f, 0.0f };
	gfloat        f[16];

	lrg_editor_view_init_default (&view, 800.0f, 600.0f);
	dist_before = sqrtf (8 * 8 + 6 * 6 + 8 * 8);

	lrg_editor_view_orbit (&view, 0.5f, 0.2f);
	dist_after = sqrtf (view.position[0] * view.position[0] +
	                    view.position[1] * view.position[1] +
	                    view.position[2] * view.position[2]);
	g_assert_cmpfloat_with_epsilon (dist_before, dist_after, 1e-3);

	/* Pitch is clamped short of the pole. */
	lrg_editor_view_orbit (&view, 0.0f, 10.0f);
	g_assert_cmpfloat (view.position[1], <, dist_after);

	lrg_editor_view_dolly (&view, 0.5f);
	g_assert_cmpfloat (fabsf (view.position[1]), <, dist_after);

	lrg_editor_view_frame (&view, center, 1.0f);
	g_assert_cmpfloat_with_epsilon (view.target[0], 5.0f, EPS);

	/* A top view produces a valid (non-NaN) basis. */
	lrg_editor_view_align (&view, top);
	lrg_editor_view_get_view_matrix (&view, f);
	g_assert_false (isnan (f[0]));
	g_assert_cmpfloat_with_epsilon (view.position[0], 5.0f, EPS);

	/* Fly forward moves both camera and target. */
	{
		gfloat old_target_y = view.target[1];

		lrg_editor_view_fly (&view, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
		g_assert_cmpfloat (view.target[1], <, old_target_y);
	}
}

static void
test_pick_and_box_select (void)
{
	g_autoptr(LrgEditor) ed = lrg_editor_new ();
	g_autoptr(GPtrArray) all = NULL;
	g_autoptr(GPtrArray) boxed = NULL;
	LrgEditorView        view;
	LrgNode             *near, *far, *hidden_parent, *hidden_child, *locked;
	const gfloat         origin[3] = { 0.0f, 0.0f, 10.0f };
	const gfloat         dir[3] = { 0.0f, 0.0f, -1.0f };
	gfloat               d = 0.0f;

	near = add_cube (ed, "near", NULL, 0);
	lrg_node_set_location_xyz (near, 0.0f, 0.0f, 2.0f);
	far = add_cube (ed, "far", NULL, 0);
	lrg_node_set_location_xyz (far, 0.0f, 0.0f, -2.0f);

	g_assert_true (lrg_editor_pick (lrg_editor_get_level (ed), origin, dir, TRUE, &d) == near);
	g_assert_cmpfloat_with_epsilon (d, 7.5f, 1e-3);

	all = lrg_editor_pick_all (lrg_editor_get_level (ed), origin, dir, TRUE);
	g_assert_cmpuint (all->len, ==, 2);
	g_assert_true (g_ptr_array_index (all, 1) == far);

	/* Locked nodes are skipped unless requested. */
	lrg_node_set_locked (near, TRUE);
	locked = near;
	g_assert_true (lrg_editor_pick (lrg_editor_get_level (ed), origin, dir, FALSE, NULL) == far);
	g_assert_true (lrg_editor_pick (lrg_editor_get_level (ed), origin, dir, TRUE, NULL) == locked);
	lrg_node_set_locked (near, FALSE);

	/* An invisible parent hides its children from picking. */
	hidden_parent = add_cube (ed, "hp", NULL, 0);
	lrg_node_set_location_xyz (hidden_parent, 5.0f, 0.0f, 0.0f);
	hidden_child = add_cube (ed, "hc", hidden_parent, 0);
	lrg_node_set_location_xyz (hidden_child, 0.0f, 0.0f, 0.0f);
	lrg_node_set_visible (hidden_parent, FALSE);
	{
		const gfloat o2[3] = { 5.0f, 0.0f, 10.0f };

		g_assert_null (lrg_editor_pick (lrg_editor_get_level (ed), o2, dir, TRUE, NULL));
	}

	/* Rotated and scaled boxes pick by their oriented bounds. */
	lrg_node_set_rotation_xyz (far, 0.0f, (gfloat) G_PI_4, 0.0f);
	lrg_node_set_scale_xyz (far, 4.0f, 1.0f, 1.0f);
	{
		const gfloat o3[3] = { 1.2f, 0.0f, 10.0f };

		lrg_node_set_visible (near, FALSE);
		g_assert_true (lrg_editor_pick (lrg_editor_get_level (ed), o3, dir, TRUE, NULL) == far);
		lrg_node_set_visible (near, TRUE);
	}

	/* Box select around the projected centre catches the origin nodes. */
	lrg_editor_view_init_default (&view, 800.0f, 600.0f);
	boxed = lrg_editor_box_select (lrg_editor_get_level (ed), &view, 300, 200, 500, 400, TRUE);
	g_assert_cmpuint (boxed->len, ==, 2);
}

static void
test_bounds_and_snap (void)
{
	g_autoptr(GPtrArray) nodes = g_ptr_array_new_with_free_func (g_object_unref);
	gfloat               center[3];
	gfloat               radius;

	g_assert_false (lrg_editor_compute_bounds (nodes, center, &radius));
	g_ptr_array_add (nodes, make_cube ("a", -1.0f, 0.0f, 0.0f));
	g_ptr_array_add (nodes, make_cube ("b", 3.0f, 0.0f, 0.0f));
	g_assert_true (lrg_editor_compute_bounds (nodes, center, &radius));
	g_assert_cmpfloat_with_epsilon (center[0], 1.0f, EPS);
	g_assert_cmpfloat (radius, >=, 2.5f);

	g_assert_cmpfloat_with_epsilon (lrg_editor_snap_value (1.26f, 0.25f), 1.25f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_editor_snap_value (1.26f, 0.0f), 1.26f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_editor_snap_value (-0.6f, 0.5f), -0.5f, EPS);
}

/* ==========================================================================
 * Gizmo
 * ========================================================================== */

/* Projects a world point to viewport pixels. */
static void
px_of (const LrgEditorView *view,
       gfloat               x,
       gfloat               y,
       gfloat               z,
       gfloat              *sx,
       gfloat              *sy)
{
	const gfloat p[3] = { x, y, z };

	g_assert_true (lrg_editor_view_project (view, p, sx, sy, NULL));
}

static void
test_gizmo_translate_axis (void)
{
	g_autoptr(LrgEditor)      ed = lrg_editor_new ();
	g_autoptr(LrgEditorGizmo) gz = lrg_editor_gizmo_new (ed);
	LrgEditorView             view;
	LrgNode                  *a;
	gfloat                    size, gx, gy, tx, ty;

	lrg_editor_view_init_default (&view, 800.0f, 600.0f);
	a = add_cube (ed, "a", NULL, 0);
	lrg_editor_select (ed, a, FALSE);

	/* Select tool shows no gizmo. */
	g_assert_false (lrg_editor_gizmo_is_visible (gz));
	lrg_editor_set_tool (ed, LRG_EDITOR_TOOL_TRANSLATE);
	g_assert_true (lrg_editor_gizmo_is_visible (gz));

	size = lrg_editor_gizmo_get_size (gz, &view);
	px_of (&view, size * 0.7f, 0, 0, &gx, &gy);
	g_assert_cmpint (lrg_editor_gizmo_update_hover (gz, &view, gx, gy), ==, LRG_EDITOR_GIZMO_HANDLE_X);

	g_assert_true (lrg_editor_gizmo_begin_drag (gz, &view, gx, gy));
	px_of (&view, size * 0.7f + 2.0f, 0, 0, &tx, &ty);
	lrg_editor_gizmo_drag (gz, &view, tx, ty, FALSE);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (a)->x, 2.0f, 1e-2);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (a)->y, 0.0f, 1e-3);

	/* Holding Ctrl inverts snapping (off -> on with the 1.0 default). */
	px_of (&view, size * 0.7f + 2.3f, 0, 0, &tx, &ty);
	lrg_editor_gizmo_drag (gz, &view, tx, ty, TRUE);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (a)->x, 2.0f, 1e-4);

	g_assert_true (lrg_editor_gizmo_end_drag (gz));
	g_assert_cmpuint (lrg_editor_get_undo_count (ed), ==, 2);
	lrg_editor_undo (ed);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (a)->x, 0.0f, 1e-5);

	/* Cancel restores and records nothing. */
	g_assert_true (lrg_editor_gizmo_begin_drag (gz, &view, gx, gy));
	lrg_editor_gizmo_drag (gz, &view, tx, ty, FALSE);
	lrg_editor_gizmo_cancel_drag (gz);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (a)->x, 0.0f, 1e-5);
	g_assert_cmpuint (lrg_editor_get_undo_count (ed), ==, 1);

	/* A click without motion records nothing either. */
	g_assert_true (lrg_editor_gizmo_begin_drag (gz, &view, gx, gy));
	g_assert_false (lrg_editor_gizmo_end_drag (gz));
}

static void
test_gizmo_translate_plane_and_center (void)
{
	g_autoptr(LrgEditor)      ed = lrg_editor_new ();
	g_autoptr(LrgEditorGizmo) gz = lrg_editor_gizmo_new (ed);
	LrgEditorView             view;
	LrgNode                  *a;
	gfloat                    size, gx, gy, tx, ty, ox, oy;

	lrg_editor_view_init_default (&view, 800.0f, 600.0f);
	a = add_cube (ed, "a", NULL, 0);
	lrg_editor_select (ed, a, FALSE);
	lrg_editor_set_tool (ed, LRG_EDITOR_TOOL_TRANSLATE);
	size = lrg_editor_gizmo_get_size (gz, &view);

	/* Inside the XZ square. */
	px_of (&view, size * 0.32f, 0, size * 0.32f, &gx, &gy);
	g_assert_cmpint (lrg_editor_gizmo_hit_test (gz, &view, gx, gy), ==, LRG_EDITOR_GIZMO_HANDLE_XZ);
	g_assert_true (lrg_editor_gizmo_begin_drag (gz, &view, gx, gy));
	px_of (&view, size * 0.32f + 1.0f, 0, size * 0.32f - 3.0f, &tx, &ty);
	lrg_editor_gizmo_drag (gz, &view, tx, ty, FALSE);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (a)->x, 1.0f, 1e-2);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (a)->y, 0.0f, 1e-3);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (a)->z, -3.0f, 1e-2);
	lrg_editor_gizmo_end_drag (gz);

	/* The centre is reported when on the pivot. */
	px_of (&view, 1.0f, 0.0f, -3.0f, &ox, &oy);
	g_assert_cmpint (lrg_editor_gizmo_hit_test (gz, &view, ox + 2, oy - 1), ==, LRG_EDITOR_GIZMO_HANDLE_CENTER);
	g_assert_cmpint (lrg_editor_gizmo_hit_test (gz, &view, ox + 300, oy + 200), ==, LRG_EDITOR_GIZMO_HANDLE_NONE);
}

static void
test_gizmo_rotate_and_scale (void)
{
	g_autoptr(LrgEditor)      ed = lrg_editor_new ();
	g_autoptr(LrgEditorGizmo) gz = lrg_editor_gizmo_new (ed);
	LrgEditorView             view;
	LrgNode                  *a;
	gfloat                    size, gx, gy, tx, ty;

	lrg_editor_view_init_default (&view, 800.0f, 600.0f);
	a = add_cube (ed, "a", NULL, 0);
	lrg_editor_select (ed, a, FALSE);

	/* Rotate +90 degrees about Y: +X swings to -Z. */
	lrg_editor_set_tool (ed, LRG_EDITOR_TOOL_ROTATE);
	size = lrg_editor_gizmo_get_size (gz, &view);
	px_of (&view, size, 0, 0, &gx, &gy);
	g_assert_true (lrg_editor_gizmo_begin_drag_handle (gz, &view, LRG_EDITOR_GIZMO_HANDLE_Y, gx, gy));
	px_of (&view, 0, 0, -size, &tx, &ty);
	lrg_editor_gizmo_drag (gz, &view, tx, ty, FALSE);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_rotation (a)->y, G_PI_2, 1e-2);
	g_assert_true (lrg_editor_gizmo_end_drag (gz));

	/* Scale the local X axis (now pointing at world -Z) by two. */
	lrg_editor_set_tool (ed, LRG_EDITOR_TOOL_SCALE);
	size = lrg_editor_gizmo_get_size (gz, &view);
	px_of (&view, 0, 0, -size * 0.5f, &gx, &gy);
	g_assert_cmpint (lrg_editor_gizmo_hit_test (gz, &view, gx, gy), ==, LRG_EDITOR_GIZMO_HANDLE_X);
	g_assert_true (lrg_editor_gizmo_begin_drag (gz, &view, gx, gy));
	px_of (&view, 0, 0, -size, &tx, &ty);
	lrg_editor_gizmo_drag (gz, &view, tx, ty, FALSE);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (a)->x, 2.0f, 2e-2);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (a)->y, 1.0f, 1e-5);
	g_assert_true (lrg_editor_gizmo_end_drag (gz));

	g_assert_cmpuint (lrg_editor_get_undo_count (ed), ==, 3);
}

static void
test_gizmo_multi_selection_and_locked (void)
{
	g_autoptr(LrgEditor)      ed = lrg_editor_new ();
	g_autoptr(LrgEditorGizmo) gz = lrg_editor_gizmo_new (ed);
	LrgEditorView             view;
	LrgNode                  *parent, *child, *other;
	gfloat                    before_c[3], after_c[3], before_o[3], after_o[3];
	gfloat                    size, gx, gy, tx, ty;

	lrg_editor_view_init_default (&view, 800.0f, 600.0f);
	parent = add_cube (ed, "parent", NULL, 0);
	lrg_node_set_location_xyz (parent, 3.0f, 0.0f, 0.0f);
	lrg_node_set_rotation_xyz (parent, 0.0f, 1.0f, 0.0f);
	lrg_node_set_scale_xyz (parent, 2.0f, 2.0f, 2.0f);
	child = add_cube (ed, "child", parent, 0);
	lrg_node_set_location_xyz (child, 0.5f, 0.0f, 0.0f);
	other = add_cube (ed, "other", NULL, 0);
	lrg_node_set_location_xyz (other, 0.0f, 0.0f, 0.0f);

	/* Primary (pivot) is the last selected: "other" at the origin. */
	lrg_editor_select (ed, child, FALSE);
	lrg_editor_select (ed, other, TRUE);
	lrg_editor_set_tool (ed, LRG_EDITOR_TOOL_TRANSLATE);

	lrg_editor_node_get_world_position (child, before_c);
	lrg_editor_node_get_world_position (other, before_o);

	size = lrg_editor_gizmo_get_size (gz, &view);
	px_of (&view, 0, size * 0.7f, 0, &gx, &gy);
	g_assert_true (lrg_editor_gizmo_begin_drag (gz, &view, gx, gy));
	px_of (&view, 0, size * 0.7f + 1.5f, 0, &tx, &ty);
	lrg_editor_gizmo_drag (gz, &view, tx, ty, FALSE);
	g_assert_true (lrg_editor_gizmo_end_drag (gz));

	lrg_editor_node_get_world_position (child, after_c);
	lrg_editor_node_get_world_position (other, after_o);
	g_assert_cmpfloat_with_epsilon (after_c[1] - before_c[1], 1.5f, 2e-2);
	g_assert_cmpfloat_with_epsilon (after_o[1] - before_o[1], 1.5f, 2e-2);
	g_assert_cmpfloat_with_epsilon (after_c[0], before_c[0], 1e-3);

	/* One undo step for the whole selection. */
	lrg_editor_undo (ed);
	lrg_editor_node_get_world_position (child, after_c);
	g_assert_cmpfloat_with_epsilon (after_c[1], before_c[1], 1e-4);

	/* A locked primary hides the gizmo. */
	lrg_node_set_locked (other, TRUE);
	g_assert_false (lrg_editor_gizmo_is_visible (gz));
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/editor-core/node/insert-index-path", test_node_insert_index_path);
	g_test_add_func ("/editor-core/node/copy-state-equal", test_node_copy_state_and_equal);
	g_test_add_func ("/editor-core/node/component-script-editing", test_node_component_script_editing);

	g_test_add_func ("/editor-core/history/delete-undo-index", test_delete_undo_restores_index);
	g_test_add_func ("/editor-core/history/dirty", test_dirty_tracking);
	g_test_add_func ("/editor-core/history/group", test_group_is_one_step);
	g_test_add_func ("/editor-core/history/jump-limit", test_history_jump_and_limit);
	g_test_add_func ("/editor-core/edit/snapshot-components", test_snapshot_commit_and_components);
	g_test_add_func ("/editor-core/edit/visual-scripts", test_visual_and_script_edits);
	g_test_add_func ("/editor-core/edit/property-coercion", test_object_property_coercion);

	g_test_add_func ("/editor-core/structure/selection-pruned", test_selection_pruned_on_remove);
	g_test_add_func ("/editor-core/structure/delete-selection", test_selected_roots_and_delete_selection);
	g_test_add_func ("/editor-core/structure/duplicate", test_duplicate_names_and_placement);
	g_test_add_func ("/editor-core/structure/clipboard", test_clipboard_roundtrip);
	g_test_add_func ("/editor-core/structure/move-keep-world", test_move_reorder_and_keep_world);
	g_test_add_func ("/editor-core/structure/find", test_find_nodes);
	g_test_add_func ("/editor-core/structure/prefab-source", test_prefab_instantiate_records_source);

	g_test_add_func ("/editor-core/math/compose-decompose", test_math_compose_decompose);
	g_test_add_func ("/editor-core/math/world-hierarchy", test_math_world_matrix_hierarchy);
	g_test_add_func ("/editor-core/math/view-project-ray", test_view_project_and_ray);
	g_test_add_func ("/editor-core/math/view-navigation", test_view_navigation);
	g_test_add_func ("/editor-core/math/pick-box", test_pick_and_box_select);
	g_test_add_func ("/editor-core/math/bounds-snap", test_bounds_and_snap);

	g_test_add_func ("/editor-core/gizmo/translate-axis", test_gizmo_translate_axis);
	g_test_add_func ("/editor-core/gizmo/translate-plane-center", test_gizmo_translate_plane_and_center);
	g_test_add_func ("/editor-core/gizmo/rotate-scale", test_gizmo_rotate_and_scale);
	g_test_add_func ("/editor-core/gizmo/multi-locked", test_gizmo_multi_selection_and_locked);

	return g_test_run ();
}
