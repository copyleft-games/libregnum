/* test-editor-prefab-link.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Linked prefab instances (v0.8.1): instantiate linked, automatic override
 * marking through the editor setters, sync propagation (values, added and
 * removed children) that keeps overrides, apply to source, revert, one
 * level of nesting, undo/redo of every operation (sync as one group),
 * .rlevel round trip of the `prefab:` block, 0.8.0 files loading unlinked
 * plus relink, and the runtime ignoring the block.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <libregnum.h>

#define EPS (1e-4)

/* ==========================================================================
 * Fixture: a temp dir holding crate.rprefab
 *
 *   Crate   (cube, component health {max: 100})
 *   +- Lid     location (0, 1, 0)
 *   +- Handle
 * ========================================================================== */

typedef struct
{
	gchar     *dir;
	gchar     *path;   /* crate.rprefab */
	LrgEditor *editor;
} Fixture;

static LrgNode *
make_crate (void)
{
	LrgNode                     *root = lrg_node_new ("Crate");
	g_autoptr(LrgNodeVisual)     vis = lrg_node_visual_new (LRG_NODE_VISUAL_PRIMITIVE);
	g_autoptr(LrgComponentDesc)  health = lrg_component_desc_new ("health");
	g_autoptr(LrgNode)           lid = lrg_node_new ("Lid");
	g_autoptr(LrgNode)           handle = lrg_node_new ("Handle");

	lrg_node_visual_set_primitive (vis, LRG_PRIMITIVE_CUBE);
	lrg_node_set_visual (root, vis);
	lrg_component_desc_set_int (health, "max", 100);
	lrg_node_add_component (root, health);
	lrg_node_set_location_xyz (lid, 0.0f, 1.0f, 0.0f);
	lrg_node_add_child (root, lid);
	lrg_node_add_child (root, handle);
	return root;
}

static void
fixture_setup (Fixture       *f,
               gconstpointer  data)
{
	g_autoptr(LrgNode) crate = make_crate ();
	g_autoptr(GError)  error = NULL;

	f->dir = g_dir_make_tmp ("lrg-prefab-link-XXXXXX", &error);
	g_assert_no_error (error);
	f->path = g_build_filename (f->dir, "crate.rprefab", NULL);
	g_assert_true (lrg_prefab_save (crate, f->path, &error));
	g_assert_no_error (error);
	f->editor = lrg_editor_new ();
}

static void
remove_tree (const gchar *dir)
{
	g_autoptr(GDir) d = g_dir_open (dir, 0, NULL);
	const gchar    *name;

	while (d != NULL && (name = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar *p = g_build_filename (dir, name, NULL);

		g_remove (p);
	}
	g_rmdir (dir);
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  data)
{
	g_clear_object (&f->editor);
	remove_tree (f->dir);
	g_free (f->dir);
	g_free (f->path);
}

static LrgNode *
child (LrgNode     *node,
       const gchar *name)
{
	return lrg_node_find_by_path (node, name);
}

static LrgPrefabLink *
link_of (LrgNode *node)
{
	return lrg_node_get_prefab_link (node);
}

static gboolean
has_key (LrgNode     *instance,
         const gchar *key)
{
	return lrg_prefab_link_contains_override (link_of (instance), key);
}

static gchar *
key_for (LrgNode     *node,
         const gchar *path)
{
	return g_strdup_printf ("%s:%s", lrg_node_get_guid (node), path);
}

static gint
health_max (LrgNode *node)
{
	LrgComponentDesc *desc = lrg_node_find_component (node, "health");
	const GValue     *v = (desc != NULL) ? lrg_component_desc_get_value (desc, "max") : NULL;

	GValue            out = G_VALUE_INIT;
	gint              result;

	/* Values read back from YAML may come as another integer type. */
	if (v == NULL)
		return -1;
	g_value_init (&out, G_TYPE_INT);
	if (!g_value_transform (v, &out))
		return -2;
	result = g_value_get_int (&out);
	g_value_unset (&out);
	return result;
}

static LrgNode *
instantiate (Fixture *f)
{
	g_autoptr(GError) error = NULL;
	LrgNode          *inst = lrg_editor_instantiate_prefab (f->editor, f->path, NULL, &error);

	g_assert_no_error (error);
	g_assert_nonnull (inst);
	return inst;
}

/* Loads the prefab, lets @edit change it, saves it back (same guids). */
static void
edit_prefab_file (Fixture *f,
                  void   (*edit) (LrgNode *prefab))
{
	g_autoptr(GError)  error = NULL;
	g_autoptr(LrgNode) prefab = lrg_prefab_load (f->path, &error);

	g_assert_no_error (error);
	edit (prefab);
	g_assert_true (lrg_prefab_save (prefab, f->path, &error));
	g_assert_no_error (error);
}

/* ==========================================================================
 * Instantiate linked
 * ========================================================================== */

static void
test_instantiate_linked (Fixture       *f,
                         gconstpointer  data)
{
	g_autoptr(GError)  error = NULL;
	g_autoptr(LrgNode) prefab = lrg_prefab_load (f->path, &error);
	LrgNode           *inst = instantiate (f);
	LrgNode           *inst2 = instantiate (f);
	LrgPrefabLink     *link = link_of (inst);
	const GValue      *marker;

	g_assert_nonnull (link);
	g_assert_cmpstr (lrg_prefab_link_get_source_path (link), ==, f->path);
	g_assert_cmpuint (lrg_prefab_link_get_map_size (link), ==, 3);
	g_assert_cmpuint (lrg_prefab_link_get_n_overrides (link), ==, 0);

	/* Every node mirrors its source node, with its own guid. */
	g_assert_cmpstr (lrg_node_get_prefab_source_guid (inst), ==, lrg_node_get_guid (prefab));
	g_assert_cmpstr (lrg_node_get_prefab_source_guid (child (inst, "Lid")), ==,
	                 lrg_node_get_guid (child (prefab, "Lid")));
	g_assert_cmpstr (lrg_node_get_guid (child (inst, "Lid")), !=,
	                 lrg_node_get_guid (child (prefab, "Lid")));
	g_assert_cmpstr (lrg_node_get_guid (child (inst, "Lid")), !=,
	                 lrg_node_get_guid (child (inst2, "Lid")));
	g_assert_true (lrg_prefab_find_owner (child (inst, "Handle")) == inst);

	/* 0.8.0 marker kept for old tools. */
	marker = lrg_node_visual_get_param (lrg_node_get_visual (inst), "prefab:source");
	g_assert_nonnull (marker);
	g_assert_cmpstr (g_value_get_string (marker), ==, f->path);

	/* Nothing differs from the source. */
	{
		g_auto(GStrv) keys = lrg_prefab_compute_overrides (inst, prefab);

		g_assert_cmpuint (g_strv_length (keys), ==, 0);
	}

	/* Undo removes the instance like any insert. */
	lrg_editor_undo (f->editor);
	lrg_editor_undo (f->editor);
	g_assert_cmpuint (lrg_level_count_nodes (lrg_editor_get_level (f->editor)), ==, 0);
}

/* ==========================================================================
 * Override marking through the normal editor setters
 * ========================================================================== */

static void
test_override_marking (Fixture       *f,
                       gconstpointer  data)
{
	LrgNode          *inst = instantiate (f);
	LrgNode          *lid = child (inst, "Lid");
	LrgNode          *handle = child (inst, "Handle");
	const gfloat      loc[3] = { 0.0f, 3.0f, 0.0f };
	const gfloat      loc2[3] = { 0.0f, 4.0f, 0.0f };
	const gfloat      root_loc[3] = { 10.0f, 0.0f, 0.0f };
	const gfloat      scl[3] = { 2.0f, 2.0f, 2.0f };
	g_autofree gchar *lid_loc = key_for (lid, "transform/location");
	g_autofree gchar *lid_name = key_for (lid, "name");
	g_autofree gchar *handle_src = g_strdup (lrg_node_get_prefab_source_guid (handle));
	g_autofree gchar *removed = g_strconcat ("children/-", handle_src, NULL);
	g_autoptr(LrgNode) extra = lrg_node_new ("Extra");
	g_autofree gchar *added = g_strconcat ("children/+", lrg_node_get_guid (extra), NULL);
	GValue            v = G_VALUE_INIT;
	guint             undo_before;

	/* Moving the instance root is never an override. */
	lrg_editor_transform_node (f->editor, inst, root_loc, NULL, NULL, FALSE);
	g_assert_cmpuint (lrg_prefab_link_get_n_overrides (link_of (inst)), ==, 0);

	/* A drag (merged transforms) is one step and one key. */
	undo_before = lrg_editor_get_undo_count (f->editor);
	lrg_editor_transform_node (f->editor, lid, loc, NULL, NULL, TRUE);
	lrg_editor_transform_node (f->editor, lid, loc2, NULL, NULL, TRUE);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo_before + 1);
	g_assert_true (has_key (inst, lid_loc));
	g_assert_true (lrg_prefab_is_overridden (lid, "transform/location"));
	lrg_editor_undo (f->editor);
	g_assert_false (has_key (inst, lid_loc));
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (lid)->y, 1.0f, EPS);
	lrg_editor_redo (f->editor);
	g_assert_true (has_key (inst, lid_loc));

	/* Root scale is a plain root key. */
	lrg_editor_transform_node (f->editor, inst, NULL, NULL, scl, FALSE);
	g_assert_true (has_key (inst, "transform/scale"));

	/* Rename (set-property path) and visibility. */
	lrg_editor_rename_node (f->editor, lid, "Top");
	g_assert_true (has_key (inst, lid_name));
	g_value_init (&v, G_TYPE_BOOLEAN);
	g_value_set_boolean (&v, FALSE);
	lrg_editor_set_node_property (f->editor, inst, "visible", &v);
	g_value_unset (&v);
	g_assert_true (has_key (inst, "visible"));

	/* Inspector-style snapshot edit of a component value. */
	g_value_init (&v, G_TYPE_INT);
	g_value_set_int (&v, 250);
	g_assert_true (lrg_editor_set_component_value (f->editor, inst, 0, "max", &v, FALSE));
	g_value_unset (&v);
	g_assert_true (has_key (inst, "components/health/max"));
	lrg_editor_undo (f->editor);
	g_assert_false (has_key (inst, "components/health/max"));
	lrg_editor_redo (f->editor);

	/* Structural edits. */
	lrg_editor_add_node (f->editor, extra, inst);
	g_assert_true (has_key (inst, added));
	g_assert_true (lrg_editor_delete_node (f->editor, handle));
	g_assert_true (has_key (inst, removed));
	lrg_editor_undo (f->editor);
	g_assert_false (has_key (inst, removed));
	g_assert_nonnull (child (inst, "Handle"));

	/* Deleting the added child takes its "+" key away; undo restores it. */
	g_assert_true (lrg_editor_delete_node (f->editor, extra));
	g_assert_false (has_key (inst, added));
	lrg_editor_undo (f->editor);
	g_assert_true (has_key (inst, added));

	/* Explicit toggles. */
	g_assert_true (lrg_editor_prefab_set_override (f->editor, handle, "is_2d", TRUE));
	g_assert_true (lrg_prefab_is_overridden (handle, "is_2d"));
	g_assert_false (lrg_editor_prefab_set_override (f->editor, handle, "is_2d", TRUE));
	lrg_editor_undo (f->editor);
	g_assert_false (lrg_prefab_is_overridden (handle, "is_2d"));

	/* Covered paths: "visual" covers "visual/asset". */
	g_assert_true (lrg_prefab_link_add_override (link_of (inst), "visual"));
	g_assert_true (lrg_prefab_link_has_override (link_of (inst), "visual/asset"));
	g_assert_false (lrg_prefab_link_has_override (link_of (inst), "visuals"));
}

/* ==========================================================================
 * Sync propagation
 * ========================================================================== */

static void
edit_for_sync (LrgNode *prefab)
{
	LrgNode            *lid = child (prefab, "Lid");
	LrgNode            *handle = child (prefab, "Handle");
	g_autoptr(LrgNode)  spout = lrg_node_new ("Spout");

	lrg_node_set_location_xyz (lid, 0.0f, 5.0f, 0.0f);
	lrg_node_set_scale_xyz (lid, 3.0f, 3.0f, 3.0f);
	lrg_component_desc_set_int (lrg_node_find_component (prefab, "health"), "max", 200);
	lrg_node_add_child (prefab, spout);
	lrg_node_remove_child (prefab, handle);
}

typedef struct
{
	gchar *path;
	guint  n;
	guint  count;
} SyncedData;

static void
on_synced (LrgEditor   *editor,
           const gchar *path,
           guint        n,
           gpointer     user_data)
{
	SyncedData *d = user_data;

	g_free (d->path);
	d->path = g_strdup (path);
	d->n = n;
	d->count++;
}

static void
test_sync (Fixture       *f,
           gconstpointer  data)
{
	LrgNode          *a = instantiate (f);
	LrgNode          *b = instantiate (f);
	LrgNode          *a_lid = child (a, "Lid");
	LrgNode          *a_handle = child (a, "Handle");
	LrgNode          *b_lid = child (b, "Lid");
	const gfloat      loc[3] = { 0.0f, 9.0f, 0.0f };
	SyncedData        d = { NULL, 0, 0 };
	g_autofree gchar *a_lid_guid = g_strdup (lrg_node_get_guid (a_lid));
	g_autofree gchar *a_handle_added = NULL;
	guint             undo_before;
	GValue            v = G_VALUE_INIT;

	/* A: override the lid location and edit the handle (so it survives
	 * deletion in the source). B: delete the lid on purpose. */
	lrg_editor_transform_node (f->editor, a_lid, loc, NULL, NULL, FALSE);
	g_value_init (&v, G_TYPE_BOOLEAN);
	g_value_set_boolean (&v, FALSE);
	lrg_editor_set_node_property (f->editor, a_handle, "visible", &v);
	g_value_unset (&v);
	g_assert_true (lrg_editor_delete_node (f->editor, b_lid));

	g_signal_connect (f->editor, "prefab-synced", G_CALLBACK (on_synced), &d);
	edit_prefab_file (f, edit_for_sync);

	undo_before = lrg_editor_get_undo_count (f->editor);
	g_assert_cmpuint (lrg_editor_prefab_changed (f->editor, f->path), ==, 2);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo_before + 1);
	g_assert_cmpuint (d.count, ==, 1);
	g_assert_cmpuint (d.n, ==, 2);
	g_assert_cmpstr (d.path, ==, f->path);

	/* A: overridden location kept, scale and component propagated, new
	 * child added, mapped node kept its identity. */
	g_assert_true (child (a, "Lid") == a_lid);
	g_assert_cmpstr (lrg_node_get_guid (a_lid), ==, a_lid_guid);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (a_lid)->y, 9.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (a_lid)->x, 3.0f, EPS);
	g_assert_cmpint (health_max (a), ==, 200);
	g_assert_nonnull (child (a, "Spout"));
	g_assert_nonnull (lrg_node_get_prefab_source_guid (child (a, "Spout")));
	/* The edited handle survives as an instance-added child. */
	g_assert_true (child (a, "Handle") == a_handle);
	g_assert_null (lrg_node_get_prefab_source_guid (a_handle));
	a_handle_added = g_strconcat ("children/+", lrg_node_get_guid (a_handle), NULL);
	g_assert_true (has_key (a, a_handle_added));

	/* B: everything propagated, the deleted lid stays deleted, the
	 * unedited handle went away. */
	g_assert_null (child (b, "Lid"));
	g_assert_null (child (b, "Handle"));
	g_assert_nonnull (child (b, "Spout"));
	g_assert_cmpint (health_max (b), ==, 200);

	/* Nothing left to do: a second notification records nothing. */
	g_assert_cmpuint (lrg_editor_prefab_changed (f->editor, f->path), ==, 0);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo_before + 1);

	/* One undo reverts both instances. */
	lrg_editor_undo (f->editor);
	g_assert_cmpint (health_max (a), ==, 100);
	g_assert_cmpint (health_max (b), ==, 100);
	g_assert_null (child (a, "Spout"));
	g_assert_null (child (b, "Spout"));
	g_assert_nonnull (child (b, "Handle"));
	g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (a_lid)->x, 1.0f, EPS);
	g_assert_nonnull (lrg_node_get_prefab_source_guid (a_handle));
	g_assert_false (has_key (a, a_handle_added));

	lrg_editor_redo (f->editor);
	g_assert_cmpint (health_max (b), ==, 200);
	g_assert_nonnull (child (a, "Spout"));
	g_assert_null (child (b, "Handle"));

	g_signal_handlers_disconnect_by_func (f->editor, on_synced, &d);
	g_free (d.path);
}

/* A sync that creates a child keeps that child's guid stable. */
static void
test_sync_stable_guids (Fixture       *f,
                        gconstpointer  data)
{
	g_autoptr(GError)  error = NULL;
	LrgNode           *a = instantiate (f);
	g_autoptr(LrgNode) p1 = NULL;
	g_autoptr(LrgNode) p2 = NULL;
	g_autoptr(LrgNode) c1 = NULL;
	g_autoptr(LrgNode) c2 = NULL;

	edit_prefab_file (f, edit_for_sync);
	p1 = lrg_prefab_load (f->path, &error);
	p2 = lrg_prefab_load (f->path, &error);
	c1 = lrg_prefab_clone (a);
	c2 = lrg_prefab_clone (a);
	/* Same instance root guid + same source guid -> same child guid. */
	lrg_node_set_guid (c2, lrg_node_get_guid (c1));
	{
		g_autoptr(LrgPrefabLink) l = lrg_prefab_link_copy (link_of (c1));

		lrg_node_set_prefab_link (c2, l);
	}
	g_assert_true (lrg_prefab_sync_instance (c1, p1));
	g_assert_true (lrg_prefab_sync_instance (c2, p2));
	g_assert_cmpstr (lrg_node_get_guid (child (c1, "Spout")), ==,
	                 lrg_node_get_guid (child (c2, "Spout")));
}

/* ==========================================================================
 * Apply to source
 * ========================================================================== */

static void
test_apply (Fixture       *f,
            gconstpointer  data)
{
	g_autoptr(GError)  error = NULL;
	LrgNode           *a = instantiate (f);
	LrgNode           *b = instantiate (f);
	const gfloat       scl[3] = { 2.0f, 2.0f, 2.0f };
	g_autoptr(LrgNode) extra = lrg_node_new ("Extra");
	g_autoptr(LrgNode) prefab = NULL;
	g_autofree gchar  *before = NULL;
	g_autofree gchar  *now = NULL;
	guint              undo_before;

	g_assert_true (g_file_get_contents (f->path, &before, NULL, NULL));

	lrg_editor_transform_node (f->editor, a, NULL, NULL, scl, FALSE);
	lrg_editor_add_node (f->editor, extra, a);
	g_assert_cmpuint (lrg_prefab_link_get_n_overrides (link_of (a)), ==, 2);

	undo_before = lrg_editor_get_undo_count (f->editor);
	g_assert_true (lrg_editor_prefab_apply (f->editor, a, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo_before + 1);

	/* The instance has no overrides left and now mirrors the new child. */
	g_assert_cmpuint (lrg_prefab_link_get_n_overrides (link_of (a)), ==, 0);
	g_assert_nonnull (lrg_node_get_prefab_source_guid (extra));

	/* The file has the change; the other instance got it too. */
	prefab = lrg_prefab_load (f->path, &error);
	g_assert_no_error (error);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (prefab)->x, 2.0f, EPS);
	g_assert_nonnull (child (prefab, "Extra"));
	g_assert_null (lrg_node_visual_get_param (lrg_node_get_visual (prefab), "prefab:source"));
	g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (b)->x, 2.0f, EPS);
	g_assert_nonnull (child (b, "Extra"));

	/* Undo restores the file and both instances. */
	lrg_editor_undo (f->editor);
	g_assert_true (g_file_get_contents (f->path, &now, NULL, NULL));
	g_assert_cmpstr (now, ==, before);
	g_assert_null (child (b, "Extra"));
	g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (b)->x, 1.0f, EPS);
	g_assert_cmpuint (lrg_prefab_link_get_n_overrides (link_of (a)), ==, 2);
	g_assert_null (lrg_node_get_prefab_source_guid (extra));

	lrg_editor_redo (f->editor);
	g_assert_nonnull (child (b, "Extra"));
}

/* Apply of selected keys only. */
static void
test_apply_some (Fixture       *f,
                 gconstpointer  data)
{
	g_autoptr(GError)  error = NULL;
	LrgNode           *a = instantiate (f);
	LrgNode           *lid = child (a, "Lid");
	const gfloat       loc[3] = { 0.0f, 7.0f, 0.0f };
	const gfloat       scl[3] = { 2.0f, 2.0f, 2.0f };
	g_autofree gchar  *lid_loc = key_for (lid, "transform/location");
	const gchar       *keys[2] = { NULL, NULL };
	g_autoptr(LrgNode) prefab = NULL;

	lrg_editor_transform_node (f->editor, lid, loc, NULL, NULL, FALSE);
	lrg_editor_transform_node (f->editor, a, NULL, NULL, scl, FALSE);
	keys[0] = lid_loc;
	g_assert_true (lrg_editor_prefab_apply (f->editor, a, keys, &error));
	g_assert_no_error (error);

	prefab = lrg_prefab_load (f->path, &error);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (child (prefab, "Lid"))->y, 7.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (prefab)->x, 1.0f, EPS);
	g_assert_false (has_key (a, lid_loc));
	g_assert_true (has_key (a, "transform/scale"));
	g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (a)->x, 2.0f, EPS);
}

/* ==========================================================================
 * Revert
 * ========================================================================== */

static void
test_revert (Fixture       *f,
             gconstpointer  data)
{
	g_autoptr(GError)  error = NULL;
	LrgNode           *a = instantiate (f);
	LrgNode           *lid = child (a, "Lid");
	LrgNode           *handle = child (a, "Handle");
	const gfloat       loc[3] = { 0.0f, 7.0f, 0.0f };
	g_autofree gchar  *lid_loc = key_for (lid, "transform/location");
	g_autofree gchar  *removed = g_strconcat ("children/-", lrg_node_get_prefab_source_guid (handle), NULL);
	GValue             v = G_VALUE_INIT;

	lrg_editor_transform_node (f->editor, lid, loc, NULL, NULL, FALSE);
	g_value_init (&v, G_TYPE_BOOLEAN);
	g_value_set_boolean (&v, FALSE);
	lrg_editor_set_node_property (f->editor, a, "visible", &v);
	g_value_unset (&v);
	g_assert_true (lrg_editor_delete_node (f->editor, handle));

	/* One key. */
	g_assert_true (lrg_editor_prefab_revert (f->editor, a, lid_loc, &error));
	g_assert_no_error (error);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (lid)->y, 1.0f, EPS);
	g_assert_false (has_key (a, lid_loc));
	g_assert_true (has_key (a, "visible"));
	lrg_editor_undo (f->editor);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (lid)->y, 7.0f, EPS);
	g_assert_true (has_key (a, lid_loc));

	/* A removed child comes back with its old guid. */
	g_assert_true (lrg_editor_prefab_revert (f->editor, a, removed, &error));
	g_assert_nonnull (child (a, "Handle"));
	g_assert_cmpstr (lrg_node_get_guid (child (a, "Handle")), ==, lrg_node_get_guid (handle));
	g_assert_cmpint (lrg_node_get_child_index (a, child (a, "Handle")), ==, 1);

	/* Everything. */
	g_assert_true (lrg_editor_prefab_revert (f->editor, a, NULL, &error));
	g_assert_cmpuint (lrg_prefab_link_get_n_overrides (link_of (a)), ==, 0);
	g_assert_true (lrg_node_get_visible (a));
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (lid)->y, 1.0f, EPS);
	lrg_editor_undo (f->editor);
	g_assert_false (lrg_node_get_visible (a));
	g_assert_true (has_key (a, "visible"));
}

/* ==========================================================================
 * Unlink, duplicate, paste
 * ========================================================================== */

static void
test_unlink_and_copies (Fixture       *f,
                        gconstpointer  data)
{
	g_autoptr(GError)    error = NULL;
	LrgNode             *a = instantiate (f);
	g_autoptr(GPtrArray) dup = NULL;
	g_autoptr(GPtrArray) pasted = NULL;
	g_autofree gchar    *clip = NULL;
	LrgNode             *d;

	/* Duplicate stays linked, remapped to its own guids. */
	lrg_editor_select (f->editor, a, FALSE);
	dup = lrg_editor_duplicate_selection (f->editor);
	g_assert_cmpuint (dup->len, ==, 1);
	d = g_ptr_array_index (dup, 0);
	g_assert_nonnull (link_of (d));
	g_assert_cmpstr (lrg_node_get_prefab_source_guid (child (d, "Lid")), ==,
	                 lrg_node_get_prefab_source_guid (child (a, "Lid")));

	/* Paste too (through the clipboard YAML). */
	lrg_editor_select (f->editor, a, FALSE);
	clip = lrg_editor_copy_selection (f->editor);
	pasted = lrg_editor_paste (f->editor, clip, NULL, &error);
	g_assert_no_error (error);
	d = g_ptr_array_index (pasted, 0);
	g_assert_cmpstr (lrg_node_get_guid (d), !=, lrg_node_get_guid (a));
	g_assert_cmpstr (lrg_node_get_prefab_source_guid (child (d, "Lid")), ==,
	                 lrg_node_get_prefab_source_guid (child (a, "Lid")));

	g_assert_true (lrg_editor_prefab_unlink (f->editor, a));
	g_assert_null (link_of (a));
	g_assert_null (lrg_node_get_prefab_source_guid (child (a, "Lid")));
	lrg_editor_undo (f->editor);
	g_assert_nonnull (link_of (a));
	g_assert_false (lrg_editor_prefab_unlink (f->editor, child (a, "Lid")));
}

/* ==========================================================================
 * Nesting (one level)
 *
 *   wheel.rprefab: Wheel +- Hub
 *   car.rprefab:   Car +- Wheel (instance of wheel, Hub hidden in the car)
 * ========================================================================== */

static void
test_nested (Fixture       *f,
             gconstpointer  data)
{
	g_autoptr(GError)  error = NULL;
	g_autofree gchar  *wheel_path = g_build_filename (f->dir, "wheel.rprefab", NULL);
	g_autofree gchar  *car_path = g_build_filename (f->dir, "car.rprefab", NULL);
	g_autoptr(LrgNode) wheel = lrg_node_new ("Wheel");
	g_autoptr(LrgNode) hub = lrg_node_new ("Hub");
	g_autoptr(LrgNode) car = lrg_node_new ("Car");
	g_autoptr(LrgNode) wheel_src = NULL;
	g_autoptr(LrgNode) car_wheel = NULL;
	LrgNode           *inst;
	LrgNode           *iw;
	LrgNode           *ihub;

	lrg_node_add_child (wheel, hub);
	g_assert_true (lrg_prefab_save (wheel, wheel_path, &error));

	/* Car contains a linked wheel whose Hub the car hides. */
	wheel_src = lrg_prefab_load (wheel_path, &error);
	car_wheel = lrg_prefab_instantiate_linked (wheel_src, wheel_path, NULL);
	lrg_node_add_child (car, car_wheel);
	lrg_node_set_visible (child (car_wheel, "Hub"), FALSE);
	g_assert_true (lrg_prefab_mark_override (child (car_wheel, "Hub"), "visible"));
	g_assert_true (lrg_prefab_save (car, car_path, &error));
	g_assert_no_error (error);

	/* The car's file carries the nested block. */
	{
		g_autofree gchar *text = NULL;

		g_assert_true (g_file_get_contents (car_path, &text, NULL, NULL));
		g_assert_nonnull (strstr (text, "prefab:"));
		g_assert_nonnull (strstr (text, "wheel.rprefab"));
	}

	inst = lrg_editor_instantiate_prefab (f->editor, car_path, NULL, &error);
	g_assert_no_error (error);
	iw = child (inst, "Wheel");
	ihub = child (iw, "Hub");
	g_assert_nonnull (link_of (iw));
	g_assert_cmpstr (lrg_prefab_link_get_source_path (link_of (iw)), ==, wheel_path);
	/* The nested map is keyed by this instance's guids. */
	g_assert_nonnull (lrg_prefab_link_map_lookup (link_of (iw), lrg_node_get_guid (ihub)));
	g_assert_false (lrg_node_get_visible (ihub));
	/* Level edits belong to the outermost instance. */
	g_assert_true (lrg_prefab_find_owner (ihub) == inst);
	g_assert_true (lrg_prefab_find_owner (iw) == inst);

	/* The wheel source changes: the car instance in the level follows
	 * through the nesting, keeping the car's own override. */
	{
		g_autoptr(LrgNode) w = lrg_prefab_load (wheel_path, &error);

		lrg_node_set_scale_xyz (child (w, "Hub"), 4.0f, 4.0f, 4.0f);
		lrg_node_set_visible (child (w, "Hub"), TRUE);
		g_assert_true (lrg_prefab_save (w, wheel_path, &error));
	}
	g_assert_cmpuint (lrg_editor_prefab_changed (f->editor, wheel_path), ==, 1);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (ihub)->x, 4.0f, EPS);
	g_assert_false (lrg_node_get_visible (ihub));

	/* Editing the nested node in the level is an override of the car
	 * instance, and survives the next sync. */
	{
		const gfloat scl[3] = { 6.0f, 6.0f, 6.0f };

		lrg_editor_transform_node (f->editor, ihub, NULL, NULL, scl, FALSE);
		g_assert_true (lrg_prefab_is_overridden (ihub, "transform/scale"));
		lrg_editor_prefab_changed (f->editor, wheel_path);
		g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (ihub)->x, 6.0f, EPS);
	}

	lrg_editor_undo (f->editor);  /* the scale edit */
	lrg_editor_undo (f->editor);  /* the nested sync */
	g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (ihub)->x, 1.0f, EPS);

	/* A prefab cannot contain itself. */
	{
		g_autoptr(LrgNode) self_ref = lrg_prefab_load (car_path, &error);
		g_autoptr(LrgNode) loop = NULL;
		g_autoptr(LrgNode) loaded = NULL;

		loop = lrg_prefab_instantiate_linked (self_ref, car_path, NULL);
		lrg_node_add_child (self_ref, loop);
		g_assert_true (lrg_prefab_save (self_ref, car_path, &error));
		loaded = lrg_prefab_load_resolved (car_path, NULL, &error);
		g_assert_null (loaded);
		g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	}
}

/* ==========================================================================
 * Serialization
 * ========================================================================== */

static void
test_roundtrip (Fixture       *f,
                gconstpointer  data)
{
	g_autoptr(GError)             error = NULL;
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(LrgLevel)           loaded = NULL;
	g_autofree gchar             *yaml = NULL;
	LrgNode                      *a = instantiate (f);
	LrgNode                      *la;
	const gfloat                  loc[3] = { 0.0f, 3.0f, 0.0f };
	GValue                        v = G_VALUE_INIT;
	g_auto(GStrv)                 keys = NULL;

	lrg_editor_transform_node (f->editor, child (a, "Lid"), loc, NULL, NULL, FALSE);
	g_value_init (&v, G_TYPE_BOOLEAN);
	g_value_set_boolean (&v, FALSE);
	lrg_editor_set_node_property (f->editor, a, "visible", &v);
	g_value_unset (&v);
	lrg_prefab_link_set_source_guid (link_of (a), "asset-guid-1234");
	keys = lrg_prefab_link_get_overrides (link_of (a));
	g_assert_cmpuint (g_strv_length (keys), ==, 2);

	yaml = lrg_level_serializer_save_to_data (ser, lrg_editor_get_level (f->editor), NULL);
	g_assert_nonnull (strstr (yaml, "prefab:"));
	g_assert_nonnull (strstr (yaml, "overrides:"));

	loaded = lrg_level_serializer_load_from_data (ser, yaml, -1, &error);
	g_assert_no_error (error);
	la = lrg_level_find_node (loaded, lrg_node_get_guid (a));
	g_assert_nonnull (la);
	g_assert_true (lrg_prefab_link_equal (link_of (la), link_of (a)));
	g_assert_cmpstr (lrg_prefab_link_get_source_guid (link_of (la)), ==, "asset-guid-1234");
	g_assert_cmpstr (lrg_node_get_prefab_source_guid (child (la, "Lid")), ==,
	                 lrg_node_get_prefab_source_guid (child (a, "Lid")));

	/* The expanded subtree is still there in full. */
	g_assert_cmpuint (lrg_node_get_n_children (la), ==, 2);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (child (la, "Lid"))->y, 3.0f, EPS);

	/* And the loaded instance syncs like the live one. */
	{
		g_autoptr(LrgNode) prefab = lrg_prefab_load (f->path, &error);

		g_assert_false (lrg_prefab_sync_instance (la, prefab));
	}
}

/* A 0.8.0 level: an instance with only the prefab:source marker. */
static void
test_legacy_relink (Fixture       *f,
                    gconstpointer  data)
{
	g_autoptr(GError)             error = NULL;
	g_autoptr(LrgNode)            prefab = lrg_prefab_load (f->path, &error);
	g_autoptr(LrgNode)            old = lrg_prefab_instantiate (prefab);
	g_autoptr(LrgLevel)           level = lrg_level_new ("old");
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(LrgLevel)           loaded = NULL;
	g_autofree gchar             *yaml = NULL;
	GValue                        src = G_VALUE_INIT;
	LrgNode                       *inst;
	LrgNode                       *lid;
	g_autofree gchar              *lid_loc = NULL;

	g_value_init (&src, G_TYPE_STRING);
	g_value_set_string (&src, f->path);
	lrg_node_visual_set_param (lrg_node_get_visual (old), "prefab:source", &src);
	g_value_unset (&src);
	lrg_node_set_location_xyz (child (old, "Lid"), 0.0f, 2.0f, 0.0f);
	lrg_level_add_node (level, old, NULL);
	yaml = lrg_level_serializer_save_to_data (ser, level, NULL);
	g_assert_null (strstr (yaml, "prefab:\n"));

	loaded = lrg_level_serializer_load_from_data (ser, yaml, -1, &error);
	g_assert_no_error (error);
	lrg_editor_set_level (f->editor, loaded);
	inst = lrg_level_find_node (loaded, lrg_node_get_guid (old));
	g_assert_null (link_of (inst));  /* loads unlinked */
	g_assert_cmpstr (lrg_prefab_get_marker_source (inst), ==, f->path);

	g_assert_true (lrg_editor_prefab_relink (f->editor, inst, NULL, &error));
	g_assert_no_error (error);
	g_assert_nonnull (link_of (inst));
	g_assert_cmpuint (lrg_prefab_link_get_map_size (link_of (inst)), ==, 3);
	lid = child (inst, "Lid");
	lid_loc = key_for (lid, "transform/location");
	g_assert_true (has_key (inst, lid_loc));
	g_assert_cmpuint (lrg_prefab_link_get_n_overrides (link_of (inst)), ==, 1);
	g_assert_cmpstr (lrg_node_get_prefab_source_guid (lid), ==,
	                 lrg_node_get_guid (child (prefab, "Lid")));

	lrg_editor_undo (f->editor);
	g_assert_null (link_of (inst));
}

/* The runtime ignores the block: same scene / world as an unlinked copy. */
static void
test_runtime_ignores_block (Fixture       *f,
                            gconstpointer  data)
{
	g_autoptr(GError)             error = NULL;
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(LrgLevel)           linked = NULL;
	g_autoptr(LrgLevel)           plain = NULL;
	g_autoptr(LrgScene)           s1 = NULL;
	g_autoptr(LrgScene)           s2 = NULL;
	g_autofree gchar             *yaml = NULL;
	g_autofree gchar             *plain_yaml = NULL;
	LrgNode                      *a = instantiate (f);
	LrgEngine                    *engine;

	(void) a;
	yaml = lrg_level_serializer_save_to_data (ser, lrg_editor_get_level (f->editor), NULL);
	linked = lrg_level_serializer_load_from_data (ser, yaml, -1, &error);
	g_assert_no_error (error);

	/* The same document without the block. */
	lrg_prefab_unlink (lrg_level_find_node (linked, lrg_node_get_guid (a)));
	plain_yaml = lrg_level_serializer_save_to_data (ser, linked, NULL);
	g_assert_null (strstr (plain_yaml, "overrides"));
	g_assert_null (strstr (plain_yaml, "map:"));
	g_clear_object (&linked);
	linked = lrg_level_serializer_load_from_data (ser, yaml, -1, &error);
	plain = lrg_level_serializer_load_from_data (ser, plain_yaml, -1, &error);
	g_assert_no_error (error);

	s1 = lrg_level_to_scene (linked);
	s2 = lrg_level_to_scene (plain);
	g_assert_cmpuint (lrg_scene_get_entity_count (s1), ==, lrg_scene_get_entity_count (s2));
	g_assert_cmpuint (lrg_scene_get_entity_count (s1), >, 0);

	engine = lrg_engine_get_default ();
	if (engine != NULL)
	{
		g_autoptr(LrgWorld) w1 = lrg_world_new ();
		g_autoptr(LrgWorld) w2 = lrg_world_new ();

		lrg_engine_startup (engine, NULL);
		g_assert_true (lrg_level_instantiate (linked, w1, engine, &error));
		g_assert_no_error (error);
		g_assert_true (lrg_level_instantiate (plain, w2, engine, &error));
		g_assert_no_error (error);
		g_assert_cmpuint (lrg_world_get_object_count (w1), ==, lrg_world_get_object_count (w2));
	}
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);

#define ADD(name, fn) \
	g_test_add ("/prefab-link/" name, Fixture, NULL, fixture_setup, fn, fixture_teardown)

	ADD ("instantiate-linked", test_instantiate_linked);
	ADD ("override-marking", test_override_marking);
	ADD ("sync", test_sync);
	ADD ("sync-stable-guids", test_sync_stable_guids);
	ADD ("apply", test_apply);
	ADD ("apply-some", test_apply_some);
	ADD ("revert", test_revert);
	ADD ("unlink-and-copies", test_unlink_and_copies);
	ADD ("nested", test_nested);
	ADD ("roundtrip", test_roundtrip);
	ADD ("legacy-relink", test_legacy_relink);
	ADD ("runtime-ignores-block", test_runtime_ignores_block);

#undef ADD

	return g_test_run ();
}
