/* test-editor-node-model.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorNodeModel: incremental sync of the per-parent stores (add,
 * remove, reparent, reorder, rename, undo, level replacement), the
 * name / GUID filter, the parent function and a tree view bound to it.
 * Headless: no window, no canvas.
 */

#include <glib.h>
#include <libregnum.h>

/* Counts items-changed emissions and remembers the last one. */
typedef struct
{
	guint n;
	guint position;
	guint removed;
	guint added;
} Changes;

static void
on_items_changed (GListModel *model,
                  guint       position,
                  guint       removed,
                  guint       added,
                  Changes    *c)
{
	c->n++;
	c->position = position;
	c->removed = removed;
	c->added = added;
}

/* The item at @i of @model (borrowed: the store keeps a reference). */
static LrgNode *
item_at (GListModel *model,
         guint       i)
{
	g_autoptr(GObject) item = g_list_model_get_item (model, i);

	return LRG_NODE (item);
}

static guint
n_items (GListModel *model)
{
	return g_list_model_get_n_items (model);
}

/* A node added to the editor's level under @parent. */
static LrgNode *
add (LrgEditor   *editor,
     const gchar *name,
     LrgNode     *parent)
{
	g_autoptr(LrgNode) node = lrg_node_new (name);

	lrg_editor_insert_node (editor, node, parent, -1);
	return node;
}

static void
test_initial_and_add (void)
{
	g_autoptr(LrgEditor)          editor = lrg_editor_new ();
	LrgNode                      *a = add (editor, "A", NULL);
	LrgNode                      *b = add (editor, "B", NULL);
	LrgNode                      *a1 = add (editor, "A1", a);
	g_autoptr(LrgEditorNodeModel) model = lrg_editor_node_model_new (editor);
	GListModel                   *root = lrg_editor_node_model_get_root_model (model);
	GListModel                   *kids_a;
	Changes                       c = { 0 };
	LrgNode                      *a2;
	LrgNode                      *c_node;

	g_assert_true (lrg_editor_node_model_get_editor (model) == editor);
	g_assert_true (lrg_editor_node_model_get_level (model) == lrg_editor_get_level (editor));
	g_assert_cmpuint (n_items (root), ==, 2);
	g_assert_true (item_at (root, 0) == a);
	g_assert_true (item_at (root, 1) == b);
	g_assert_true (lrg_editor_node_model_get_children (model, NULL) == root);
	g_assert_true (lrg_editor_node_model_get_children (model, lrg_level_get_root (lrg_editor_get_level (editor))) == root);

	kids_a = lrg_editor_node_model_get_children (model, a);
	g_assert_cmpuint (n_items (kids_a), ==, 1);
	g_assert_true (item_at (kids_a, 0) == a1);
	/* Same object every time; a leaf gets an empty store */
	g_assert_true (lrg_editor_node_model_get_children (model, a) == kids_a);
	g_assert_cmpuint (n_items (lrg_editor_node_model_get_children (model, b)), ==, 0);

	/* An addition is one insertion in the parent's store */
	g_signal_connect (kids_a, "items-changed", G_CALLBACK (on_items_changed), &c);
	a2 = add (editor, "A2", a);
	g_assert_cmpuint (c.n, ==, 1);
	g_assert_cmpuint (c.position, ==, 1);
	g_assert_cmpuint (c.removed, ==, 0);
	g_assert_cmpuint (c.added, ==, 1);
	g_assert_true (item_at (kids_a, 1) == a2);

	/* A leaf's empty store fills */
	c_node = add (editor, "B1", b);
	g_assert_cmpuint (n_items (lrg_editor_node_model_get_children (model, b)), ==, 1);
	g_assert_true (item_at (lrg_editor_node_model_get_children (model, b), 0) == c_node);

	/* A level without an editor follows level signals too */
	{
		g_autoptr(LrgLevel)           level = lrg_level_new ("L");
		g_autoptr(LrgNode)            n = lrg_node_new ("N");
		g_autoptr(LrgEditorNodeModel) lm = lrg_editor_node_model_new_for_level (level);

		g_assert_null (lrg_editor_node_model_get_editor (lm));
		g_assert_cmpuint (n_items (lrg_editor_node_model_get_root_model (lm)), ==, 0);
		lrg_level_add_node (level, n, NULL);
		g_assert_cmpuint (n_items (lrg_editor_node_model_get_root_model (lm)), ==, 1);
	}
}

static void
test_remove_and_undo (void)
{
	g_autoptr(LrgEditor)          editor = lrg_editor_new ();
	LrgNode                      *a = add (editor, "A", NULL);
	LrgNode                      *a1 = add (editor, "A1", a);
	LrgNode                      *a2 = add (editor, "A2", a);
	g_autoptr(LrgEditorNodeModel) model = lrg_editor_node_model_new (editor);
	GListModel                   *kids_a = lrg_editor_node_model_get_children (model, a);
	GListModel                   *root = lrg_editor_node_model_get_root_model (model);
	Changes                       c = { 0 };
	guint                         pos = 99;

	g_assert_true (lrg_editor_node_model_get_position (model, a2, &pos) == kids_a);
	g_assert_cmpuint (pos, ==, 1);

	g_signal_connect (kids_a, "items-changed", G_CALLBACK (on_items_changed), &c);
	g_assert_true (lrg_editor_delete_node (editor, a1));
	g_assert_cmpuint (c.n, ==, 1);
	g_assert_cmpuint (c.position, ==, 0);
	g_assert_cmpuint (c.removed, ==, 1);
	g_assert_cmpuint (c.added, ==, 0);
	g_assert_cmpuint (n_items (kids_a), ==, 1);
	g_assert_true (item_at (kids_a, 0) == a2);
	g_assert_null (lrg_editor_node_model_get_position (model, a1, NULL));

	/* Undo puts it back at its index */
	lrg_editor_undo (editor);
	g_assert_cmpuint (n_items (kids_a), ==, 2);
	g_assert_true (item_at (kids_a, 0) == a1);

	/* Deleting a whole subtree: its store survives detached and is current
	 * again after the undo */
	g_assert_true (lrg_editor_delete_node (editor, a));
	g_assert_cmpuint (n_items (root), ==, 0);
	lrg_editor_undo (editor);
	g_assert_cmpuint (n_items (root), ==, 1);
	g_assert_true (lrg_editor_node_model_get_children (model, a) == kids_a);
	g_assert_cmpuint (n_items (kids_a), ==, 2);
}

static void
test_reparent_reorder (void)
{
	g_autoptr(LrgEditor)          editor = lrg_editor_new ();
	LrgNode                      *a = add (editor, "A", NULL);
	LrgNode                      *b = add (editor, "B", NULL);
	LrgNode                      *c = add (editor, "C", NULL);
	g_autoptr(LrgEditorNodeModel) model = lrg_editor_node_model_new (editor);
	GListModel                   *root = lrg_editor_node_model_get_root_model (model);
	GListModel                   *kids_a = lrg_editor_node_model_get_children (model, a);
	Changes                       rc = { 0 };
	Changes                       ac = { 0 };

	g_signal_connect (root, "items-changed", G_CALLBACK (on_items_changed), &rc);
	g_signal_connect (kids_a, "items-changed", G_CALLBACK (on_items_changed), &ac);

	/* Reparent C under A: the level does not signal moves, the editor's
	 * "changed" does */
	g_assert_true (lrg_editor_move_node (editor, c, a, -1, FALSE));
	g_assert_cmpuint (n_items (root), ==, 2);
	g_assert_cmpuint (n_items (kids_a), ==, 1);
	g_assert_true (item_at (kids_a, 0) == c);
	g_assert_cmpuint (rc.n, >=, 1);
	g_assert_cmpuint (rc.removed, ==, 1);
	g_assert_cmpuint (ac.n, ==, 1);
	g_assert_true (lrg_editor_node_model_get_parent (model, c) == a);
	g_assert_null (lrg_editor_node_model_get_parent (model, a));

	/* Reorder at the top: B before A is one minimal splice */
	rc.n = 0;
	g_assert_true (lrg_editor_move_node (editor, b, NULL, 0, FALSE));
	g_assert_true (item_at (root, 0) == b);
	g_assert_true (item_at (root, 1) == a);
	g_assert_cmpuint (rc.n, ==, 1);
	g_assert_cmpuint (rc.position, ==, 0);
	g_assert_cmpuint (rc.removed, ==, 2);
	g_assert_cmpuint (rc.added, ==, 2);

	/* Undo both */
	lrg_editor_undo (editor);
	lrg_editor_undo (editor);
	g_assert_cmpuint (n_items (root), ==, 3);
	g_assert_true (item_at (root, 2) == c);
	g_assert_cmpuint (n_items (kids_a), ==, 0);

	/* A model over a level alone needs sync() after a direct move */
	{
		g_autoptr(LrgLevel)           level = lrg_level_new ("L");
		g_autoptr(LrgNode)            p = lrg_node_new ("P");
		g_autoptr(LrgNode)            q = lrg_node_new ("Q");
		g_autoptr(LrgEditorNodeModel) lm = NULL;
		GListModel                   *lroot;

		lrg_level_add_node (level, p, NULL);
		lrg_level_add_node (level, q, NULL);
		lm = lrg_editor_node_model_new_for_level (level);
		lroot = lrg_editor_node_model_get_root_model (lm);
		g_object_ref (q);
		lrg_node_remove_child (lrg_level_get_root (level), q);
		lrg_node_add_child (p, q);
		g_object_unref (q);
		g_assert_cmpuint (n_items (lroot), ==, 2);
		lrg_editor_node_model_sync (lm);
		g_assert_cmpuint (n_items (lroot), ==, 1);
		g_assert_true (item_at (lrg_editor_node_model_get_children (lm, p), 0) == q);
	}
}

static void
test_rename_rebinds (void)
{
	g_autoptr(LrgEditor)          editor = lrg_editor_new ();
	LrgNode                      *a = add (editor, "A", NULL);
	LrgNode                      *b = add (editor, "B", NULL);
	g_autoptr(LrgEditorNodeModel) model = lrg_editor_node_model_new (editor);
	GListModel                   *root = lrg_editor_node_model_get_root_model (model);
	Changes                       c = { 0 };

	g_signal_connect (root, "items-changed", G_CALLBACK (on_items_changed), &c);
	lrg_editor_rename_node (editor, b, "Bee");
	g_assert_cmpuint (c.n, >=, 1);
	g_assert_cmpuint (c.position, ==, 1);
	g_assert_cmpuint (c.removed, ==, 1);
	g_assert_cmpuint (c.added, ==, 1);
	g_assert_true (item_at (root, 1) == b);

	/* Visibility (a row toggle) also re-binds; node-changed too */
	c.n = 0;
	lrg_node_set_visible (a, FALSE);
	g_assert_cmpuint (c.n, ==, 1);
	g_assert_cmpuint (c.position, ==, 0);
	c.n = 0;
	lrg_level_notify_node_changed (lrg_editor_get_level (editor), a);
	g_assert_cmpuint (c.n, ==, 1);

	/* Transforms do not */
	c.n = 0;
	lrg_node_set_location_xyz (a, 1.0f, 2.0f, 3.0f);
	g_assert_cmpuint (c.n, ==, 0);
}

static void
on_notify (GObject    *object,
           GParamSpec *pspec,
           guint      *count)
{
	(*count)++;
}

static void
test_level_replace (void)
{
	g_autoptr(LrgEditor)          editor = lrg_editor_new ();
	g_autoptr(LrgEditorNodeModel) model = NULL;
	g_autoptr(GListModel)         old_root = NULL;
	guint                         notified = 0;
	LrgNode                      *n;

	add (editor, "A", NULL);
	model = lrg_editor_node_model_new (editor);
	/* Keep the old store alive so the comparison is meaningful */
	old_root = g_object_ref (lrg_editor_node_model_get_root_model (model));
	g_assert_cmpuint (n_items (old_root), ==, 1);
	g_signal_connect (model, "notify::root-model", G_CALLBACK (on_notify), &notified);

	lrg_editor_new_level (editor, "Second");
	g_assert_cmpuint (notified, ==, 1);
	g_assert_true (lrg_editor_node_model_get_root_model (model) != old_root);
	g_assert_true (lrg_editor_node_model_get_level (model) == lrg_editor_get_level (editor));
	g_assert_cmpuint (n_items (lrg_editor_node_model_get_root_model (model)), ==, 0);

	/* The new level is followed */
	n = add (editor, "X", NULL);
	g_assert_cmpuint (n_items (lrg_editor_node_model_get_root_model (model)), ==, 1);
	g_assert_true (item_at (lrg_editor_node_model_get_root_model (model), 0) == n);
}

static void
test_filter (void)
{
	g_autoptr(LrgEditor)          editor = lrg_editor_new ();
	LrgNode                      *lights = add (editor, "Lights", NULL);
	LrgNode                      *sun = add (editor, "Sun", lights);
	LrgNode                      *lamp = add (editor, "Lamp", lights);
	LrgNode                      *ground = add (editor, "Ground", NULL);
	LrgNode                      *props = add (editor, "Props", NULL);
	g_autoptr(LrgEditorNodeModel) model = lrg_editor_node_model_new (editor);
	GListModel                   *root = lrg_editor_node_model_get_root_model (model);
	GListModel                   *kids = lrg_editor_node_model_get_children (model, lights);
	g_autofree gchar             *guid_part = NULL;

	g_assert_null (lrg_editor_node_model_get_filter (model));
	g_assert_cmpuint (lrg_editor_node_model_get_n_matches (model), ==, 5);

	/* "SUN" (case-insensitive) shows Sun and its ancestor Lights only */
	lrg_editor_node_model_set_filter (model, "SUN");
	g_assert_cmpstr (lrg_editor_node_model_get_filter (model), ==, "SUN");
	g_assert_cmpuint (n_items (root), ==, 1);
	g_assert_true (item_at (root, 0) == lights);
	g_assert_cmpuint (n_items (kids), ==, 1);
	g_assert_true (item_at (kids, 0) == sun);
	g_assert_true (lrg_editor_node_model_is_match (model, sun));
	g_assert_false (lrg_editor_node_model_is_match (model, lights));
	g_assert_true (lrg_editor_node_model_is_shown (model, lights));
	g_assert_false (lrg_editor_node_model_is_shown (model, lamp));
	g_assert_cmpuint (lrg_editor_node_model_get_n_matches (model), ==, 1);

	/* Renaming into a match shows the node; out of it hides it */
	lrg_editor_rename_node (editor, ground, "Sunny Ground");
	g_assert_cmpuint (n_items (root), ==, 2);
	g_assert_true (item_at (root, 1) == ground);
	lrg_editor_rename_node (editor, sun, "Moon");
	g_assert_cmpuint (n_items (kids), ==, 0);
	g_assert_cmpuint (n_items (root), ==, 1);

	/* A matching addition appears with its ancestors */
	add (editor, "Sun 2", props);
	g_assert_cmpuint (n_items (root), ==, 2);
	g_assert_true (item_at (root, 1) == props);

	/* GUID substring */
	guid_part = g_strndup (lrg_node_get_guid (lamp), 8);
	lrg_editor_node_model_set_filter (model, guid_part);
	g_assert_true (lrg_editor_node_model_is_match (model, lamp));
	g_assert_cmpuint (n_items (kids), ==, 1);
	g_assert_true (item_at (kids, 0) == lamp);

	/* Clearing restores everything ("" is no filter) */
	lrg_editor_node_model_set_filter (model, "");
	g_assert_null (lrg_editor_node_model_get_filter (model));
	g_assert_cmpuint (n_items (root), ==, 3);
	g_assert_cmpuint (n_items (kids), ==, 2);
	g_assert_true (lrg_editor_node_model_is_match (model, lights));
}

static void
test_parent_func_tree_view (void)
{
	g_autoptr(LrgEditor)          editor = lrg_editor_new ();
	LrgNode                      *a = add (editor, "A", NULL);
	LrgNode                      *b = add (editor, "B", a);
	LrgNode                      *c = add (editor, "C", b);
	g_autoptr(LrgEditorNodeModel) model = lrg_editor_node_model_new (editor);
	g_autoptr(LrgTreeView)        view = lrg_tree_view_new ();

	g_assert_true (lrg_editor_node_model_get_parent (model, c) == b);
	g_assert_true (lrg_editor_node_model_get_parent (model, b) == a);
	g_assert_null (lrg_editor_node_model_get_parent (model, a));

	lrg_editor_node_model_bind_tree_view (model, view);
	g_assert_true (lrg_tree_view_get_model (view) == lrg_editor_node_model_get_root_model (model));
	g_assert_cmpuint (lrg_tree_view_get_n_visible (view), ==, 1);

	/* Reveal walks the parent function: A and B expand */
	g_assert_true (lrg_tree_view_reveal (view, G_OBJECT (c)));
	g_assert_true (lrg_tree_view_is_expanded (view, G_OBJECT (a)));
	g_assert_true (lrg_tree_view_is_expanded (view, G_OBJECT (b)));
	g_assert_cmpuint (lrg_tree_view_get_n_visible (view), ==, 3);
	g_assert_cmpuint (lrg_tree_view_get_position (view, G_OBJECT (c)), ==, 2);

	/* Structural edits flow into the view */
	add (editor, "D", NULL);
	g_assert_cmpuint (lrg_tree_view_get_n_visible (view), ==, 4);
	g_assert_true (lrg_editor_move_node (editor, c, NULL, -1, FALSE));
	g_assert_cmpuint (lrg_tree_view_get_position (view, G_OBJECT (c)), ==, 3);

	/* A replaced level re-roots the bound view */
	lrg_editor_new_level (editor, NULL);
	g_assert_true (lrg_tree_view_get_model (view) == lrg_editor_node_model_get_root_model (model));
	g_assert_cmpuint (lrg_tree_view_get_n_visible (view), ==, 0);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/editor/node-model/initial-and-add", test_initial_and_add);
	g_test_add_func ("/editor/node-model/remove-and-undo", test_remove_and_undo);
	g_test_add_func ("/editor/node-model/reparent-reorder", test_reparent_reorder);
	g_test_add_func ("/editor/node-model/rename", test_rename_rebinds);
	g_test_add_func ("/editor/node-model/level-replace", test_level_replace);
	g_test_add_func ("/editor/node-model/filter", test_filter);
	g_test_add_func ("/editor/node-model/parent-func", test_parent_func_tree_view);

	return g_test_run ();
}
