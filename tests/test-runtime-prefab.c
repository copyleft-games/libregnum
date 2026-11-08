/* test-runtime-prefab.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Runtime prefab references and prefab variants (0.9.0, milestone M1):
 *
 *  - reference storage: a level saved with linked instances stored as
 *    references loads back to exactly the tree the expanded form gives
 *    (guids, values, overrides, removed/added/moved children, order);
 *  - lrg_prefab_instantiate_reference() rebuilds the expanded instance
 *    from lrg_prefab_make_reference() in memory;
 *  - a prefab edit reaches a reference-stored level without re-saving it;
 *  - level and project `store_prefab_instances` settings;
 *  - variants: base chain resolution, propagation base -> variant ->
 *    instances, cycle and depth errors, lrg_prefab_get_variant_chain(),
 *    lrg_prefab_derive_variant();
 *  - with the editor built: Create Variant (asset and instance) and undo.
 *
 * Everything except the last group runs on a BUILD_EDITOR=0 library.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
#include <libregnum.h>

/* ==========================================================================
 * Fixture: a temp dir holding crate.rprefab
 *
 *   Crate    (cube, component health {max: 100})
 *   +- Lid       location (0, 1, 0)
 *   |  +- Knob
 *   +- Handle
 *   +- Hinge
 * ========================================================================== */

typedef struct
{
	gchar *dir;
	gchar *path;   /* crate.rprefab */
} Fixture;

static LrgNode *
make_crate (void)
{
	LrgNode                     *root = lrg_node_new ("Crate");
	g_autoptr(LrgNodeVisual)     vis = lrg_node_visual_new (LRG_NODE_VISUAL_PRIMITIVE);
	g_autoptr(LrgComponentDesc)  health = lrg_component_desc_new ("health");
	g_autoptr(LrgNode)           lid = lrg_node_new ("Lid");
	g_autoptr(LrgNode)           knob = lrg_node_new ("Knob");
	g_autoptr(LrgNode)           handle = lrg_node_new ("Handle");
	g_autoptr(LrgNode)           hinge = lrg_node_new ("Hinge");

	lrg_node_visual_set_primitive (vis, LRG_PRIMITIVE_CUBE);
	lrg_node_set_visual (root, vis);
	lrg_component_desc_set_int (health, "max", 100);
	lrg_node_add_component (root, health);
	lrg_node_set_location_xyz (lid, 0.0f, 1.0f, 0.0f);
	lrg_node_add_child (lid, knob);
	lrg_node_add_child (root, lid);
	lrg_node_add_child (root, handle);
	lrg_node_add_child (root, hinge);
	return root;
}

static void
fixture_setup (Fixture       *f,
               gconstpointer  data)
{
	g_autoptr(LrgNode) crate = make_crate ();
	g_autoptr(GError)  error = NULL;

	f->dir = g_dir_make_tmp ("lrg-runtime-prefab-XXXXXX", &error);
	g_assert_no_error (error);
	f->path = g_build_filename (f->dir, "crate.rprefab", NULL);
	g_assert_true (lrg_prefab_save (crate, f->path, &error));
	g_assert_no_error (error);
}

static void
remove_tree (const gchar *dir)
{
	g_autoptr(GDir) d = g_dir_open (dir, 0, NULL);
	const gchar    *name;

	while (d != NULL && (name = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar *p = g_build_filename (dir, name, NULL);

		if (g_file_test (p, G_FILE_TEST_IS_DIR))
			remove_tree (p);
		else
			g_remove (p);
	}
	g_rmdir (dir);
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  data)
{
	remove_tree (f->dir);
	g_free (f->dir);
	g_free (f->path);
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static LrgNode *
child (LrgNode     *node,
       const gchar *path)
{
	return lrg_node_find_by_path (node, path);
}

static LrgNode *
load_resolved (const gchar *path)
{
	g_autoptr(GError) error = NULL;
	LrgNode          *prefab = lrg_prefab_load_resolved (path, NULL, &error);

	g_assert_no_error (error);
	g_assert_nonnull (prefab);
	return prefab;
}

/* Replaces the link's override set by what the instance actually differs
 * in (what the editor's setters would have marked one by one). */
static void
mark_all (LrgNode *instance,
          LrgNode *prefab)
{
	g_auto(GStrv) keys = lrg_prefab_compute_overrides (instance, prefab);
	guint         i;

	for (i = 0; keys[i] != NULL; i++)
		lrg_prefab_link_add_override (lrg_node_get_prefab_link (instance), keys[i]);
}

/* An instance of crate.rprefab with every kind of override:
 *   - root scale (2,2,2), health max 250, a new component "loot"
 *   - Lid moved to (0, 3, 0)
 *   - Handle removed
 *   - "Sticker" added under Lid (between nothing: Lid's only other child
 *     is Knob, so it comes first to test order)
 *   - Hinge moved under Lid on purpose */
static LrgNode *
make_edited_instance (Fixture *f,
                      LrgNode *prefab)
{
	LrgNode                     *inst = lrg_prefab_instantiate_linked (prefab, f->path, "asset-guid-1");
	g_autoptr(LrgNode)           sticker = lrg_node_new ("Sticker");
	g_autoptr(LrgComponentDesc)  loot = lrg_component_desc_new ("loot");
	LrgNode                     *lid = child (inst, "Lid");
	LrgNode                     *hinge = child (inst, "Hinge");
	g_autofree gchar            *moved = NULL;

	lrg_node_set_name (inst, "Crate 1");
	lrg_node_set_location_xyz (inst, 5.0f, 0.0f, -2.0f);
	lrg_node_set_scale_xyz (inst, 2.0f, 2.0f, 2.0f);
	lrg_component_desc_set_int (lrg_node_find_component (inst, "health"), "max", 250);
	lrg_component_desc_set_int (loot, "gold", 7);
	lrg_node_add_component (inst, loot);
	lrg_node_set_location_xyz (lid, 0.0f, 3.0f, 0.0f);
	lrg_node_remove_child (inst, child (inst, "Handle"));
	lrg_node_visual_set_primitive (lrg_node_get_visual (inst), LRG_PRIMITIVE_CUBE);
	lrg_node_insert_child (lid, sticker, 0);

	/* Purposeful move: detach Hinge, put it under Lid, mark the move. */
	g_object_ref (hinge);
	lrg_node_remove_child (inst, hinge);
	lrg_node_add_child (lid, hinge);
	g_object_unref (hinge);

	mark_all (inst, prefab);
	moved = g_strdup_printf ("%s:children/+%s", lrg_node_get_guid (lid), lrg_node_get_guid (hinge));
	lrg_prefab_link_add_override (lrg_node_get_prefab_link (inst), moved);
	return inst;
}

/* Serializes @root (inside a throw-away level) in expanded storage: the
 * canonical text two trees are compared by. */
static gchar *
expanded_text (LrgNode *root)
{
	g_autoptr(LrgLevel)           level = lrg_level_new ("cmp");
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(LrgNode)            copy = lrg_prefab_copy (root, TRUE);

	lrg_level_add_node (level, copy, NULL);
	lrg_level_serializer_set_prefab_storage (ser, LRG_PREFAB_STORAGE_EXPANDED);
	return lrg_level_serializer_save_to_data (ser, level, NULL);
}

static gchar *
level_text (LrgLevel         *level,
            LrgPrefabStorage  storage)
{
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();

	lrg_level_serializer_set_prefab_storage (ser, storage);
	return lrg_level_serializer_save_to_data (ser, level, NULL);
}

static LrgLevel *
load_level (const gchar *path)
{
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(GError)             error = NULL;
	LrgLevel                     *level = lrg_level_serializer_load_from_file (ser, path, &error);

	g_assert_no_error (error);
	g_assert_nonnull (level);
	return level;
}

static void
save_level (LrgLevel         *level,
            const gchar      *path,
            LrgPrefabStorage  storage)
{
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(GError)             error = NULL;

	lrg_level_serializer_set_prefab_storage (ser, storage);
	g_assert_true (lrg_level_serializer_save_to_file (ser, level, path, &error));
	g_assert_no_error (error);
}

static LrgNode *
first_node (LrgLevel *level)
{
	return g_ptr_array_index (lrg_node_get_children (lrg_level_get_root (level)), 0);
}

static gint
int_value (LrgNode     *node,
           const gchar *component,
           const gchar *key)
{
	LrgComponentDesc *desc = lrg_node_find_component (node, component);
	const GValue     *v = (desc != NULL) ? lrg_component_desc_get_value (desc, key) : NULL;
	GValue            out = G_VALUE_INIT;
	gint              result;

	if (v == NULL)
		return -1;
	g_value_init (&out, G_TYPE_INT);
	if (!g_value_transform (v, &out))
		return -2;
	result = g_value_get_int (&out);
	g_value_unset (&out);
	return result;
}

static gboolean
file_contains (const gchar *path,
               const gchar *needle)
{
	g_autofree gchar *text = NULL;

	g_assert_true (g_file_get_contents (path, &text, NULL, NULL));
	return strstr (text, needle) != NULL;
}

/* ==========================================================================
 * Reference storage
 * ========================================================================== */

/* In memory: make_reference + instantiate_reference reproduce the
 * expanded instance exactly. */
static void
test_reference_in_memory (Fixture       *f,
                          gconstpointer  data)
{
	g_autoptr(LrgNode) prefab = load_resolved (f->path);
	g_autoptr(LrgNode) inst = make_edited_instance (f, prefab);
	g_autoptr(LrgNode) sparse = lrg_prefab_make_reference (inst);
	g_autoptr(LrgNode) full = NULL;
	g_autoptr(GError)  error = NULL;
	g_autofree gchar  *want = NULL;
	g_autofree gchar  *got = NULL;

	g_assert_nonnull (sparse);
	/* Knob is not edited, but Lid is: Lid keeps all its children (Knob as
	 * a placeholder) so the order survives. */
	g_assert_nonnull (child (sparse, "Lid/Sticker"));
	g_assert_nonnull (child (sparse, "Lid/Knob"));
	g_assert_null (child (sparse, "Handle"));

	full = lrg_prefab_instantiate_reference (sparse, prefab, &error);
	g_assert_no_error (error);
	g_assert_nonnull (full);

	want = expanded_text (inst);
	got = expanded_text (full);
	g_assert_cmpstr (got, ==, want);

	/* Guids are stable: derived for source nodes, kept for added ones. */
	g_assert_cmpstr (lrg_node_get_guid (child (full, "Lid/Knob")), ==,
	                 lrg_node_get_guid (child (inst, "Lid/Knob")));
	{
		g_autofree gchar *derived = lrg_prefab_derive_guid (lrg_node_get_guid (inst),
		                                                    lrg_node_get_guid (child (prefab, "Lid/Knob")));

		g_assert_cmpstr (lrg_node_get_guid (child (full, "Lid/Knob")), ==, derived);
	}
	g_assert_cmpstr (lrg_node_get_guid (child (full, "Lid/Sticker")), ==,
	                 lrg_node_get_guid (child (inst, "Lid/Sticker")));
	g_assert_true (lrg_prefab_link_equal (lrg_node_get_prefab_link (full), lrg_node_get_prefab_link (inst)));

	/* Not a reference: an error, no crash. */
	{
		g_autoptr(GError)  e2 = NULL;
		g_autoptr(LrgNode) plain = lrg_node_new ("plain");
		LrgNode           *none = lrg_prefab_instantiate_reference (plain, prefab, &e2);

		g_assert_null (none);
		g_assert_error (e2, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	}
}

/* Through files: a reference-stored level loads to the same tree as the
 * expanded one, and the file really is sparse. */
static void
test_reference_round_trip (Fixture       *f,
                           gconstpointer  data)
{
	g_autoptr(LrgNode)  prefab = load_resolved (f->path);
	g_autoptr(LrgNode)  inst = make_edited_instance (f, prefab);
	g_autoptr(LrgLevel) level = lrg_level_new ("level");
	g_autofree gchar   *exp_path = g_build_filename (f->dir, "expanded.rlevel", NULL);
	g_autofree gchar   *ref_path = g_build_filename (f->dir, "reference.rlevel", NULL);
	g_autoptr(LrgLevel) from_exp = NULL;
	g_autoptr(LrgLevel) from_ref = NULL;
	g_autofree gchar   *a = NULL;
	g_autofree gchar   *b = NULL;

	lrg_level_add_node (level, inst, NULL);
	save_level (level, exp_path, LRG_PREFAB_STORAGE_EXPANDED);
	save_level (level, ref_path, LRG_PREFAB_STORAGE_REFERENCE);

	/* The reference file: the storage marker, no untouched Knob data,
	 * no removed Handle. */
	g_assert_true (file_contains (ref_path, "storage: reference"));
	g_assert_false (file_contains (exp_path, "storage: reference"));
	g_assert_false (file_contains (ref_path, "Handle"));

	from_exp = load_level (exp_path);
	from_ref = load_level (ref_path);
	a = level_text (from_exp, LRG_PREFAB_STORAGE_EXPANDED);
	b = level_text (from_ref, LRG_PREFAB_STORAGE_EXPANDED);
	g_assert_cmpstr (b, ==, a);

	/* What runtime code sees: the full subtree with overridden values. */
	{
		LrgNode *root = first_node (from_ref);

		g_assert_cmpstr (lrg_node_get_name (root), ==, "Crate 1");
		g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (root)->x, 2.0f, 1e-5);
		g_assert_cmpint (int_value (root, "health", "max"), ==, 250);
		g_assert_cmpint (int_value (root, "loot", "gold"), ==, 7);
		g_assert_cmpfloat_with_epsilon (lrg_node_get_location (child (root, "Lid"))->y, 3.0f, 1e-5);
		g_assert_nonnull (child (root, "Lid/Knob"));
		g_assert_nonnull (child (root, "Lid/Hinge"));
		g_assert_null (child (root, "Hinge"));
		g_assert_null (child (root, "Handle"));
		g_assert_cmpint (lrg_node_get_child_index (child (root, "Lid"), child (root, "Lid/Sticker")), ==, 0);
	}

	/* Saving the loaded reference level again gives the same file. */
	{
		g_autofree gchar *again = level_text (from_ref, LRG_PREFAB_STORAGE_REFERENCE);
		g_autofree gchar *first = level_text (level, LRG_PREFAB_STORAGE_REFERENCE);

		g_assert_cmpstr (again, ==, first);
	}
}

/* Edits a prefab file in place (guids kept). */
static void
edit_prefab (const gchar *path,
             void       (*edit) (LrgNode *prefab))
{
	g_autoptr(GError)  error = NULL;
	g_autoptr(LrgNode) prefab = lrg_prefab_load (path, &error);

	g_assert_no_error (error);
	edit (prefab);
	g_assert_true (lrg_prefab_save (prefab, path, &error));
	g_assert_no_error (error);
}

static void
edit_crate (LrgNode *prefab)
{
	g_autoptr(LrgNode) bolt = lrg_node_new ("Bolt");

	/* Not overridden by the instance: these must show up. */
	lrg_node_set_scale_xyz (child (prefab, "Lid/Knob"), 4.0f, 4.0f, 4.0f);
	lrg_node_add_child (prefab, bolt);
	/* Overridden by the instance: must not show up. */
	lrg_component_desc_set_int (lrg_node_find_component (prefab, "health"), "max", 999);
	lrg_node_set_location_xyz (child (prefab, "Lid"), 0.0f, 9.0f, 0.0f);
}

/* The point of references: a prefab edit reaches the level unsaved. */
static void
test_reference_propagation (Fixture       *f,
                            gconstpointer  data)
{
	g_autoptr(LrgNode)  prefab = load_resolved (f->path);
	g_autoptr(LrgNode)  inst = make_edited_instance (f, prefab);
	g_autoptr(LrgLevel) level = lrg_level_new ("level");
	g_autofree gchar   *ref_path = g_build_filename (f->dir, "reference.rlevel", NULL);
	g_autofree gchar   *before = NULL;
	g_autoptr(LrgLevel) loaded = NULL;
	LrgNode            *root;

	lrg_level_add_node (level, inst, NULL);
	save_level (level, ref_path, LRG_PREFAB_STORAGE_REFERENCE);
	g_assert_true (g_file_get_contents (ref_path, &before, NULL, NULL));

	edit_prefab (f->path, edit_crate);
	loaded = load_level (ref_path);
	root = first_node (loaded);

	g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (child (root, "Lid/Knob"))->x, 4.0f, 1e-5);
	g_assert_nonnull (child (root, "Bolt"));
	g_assert_cmpint (int_value (root, "health", "max"), ==, 250);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (child (root, "Lid"))->y, 3.0f, 1e-5);

	/* The new node got the guid an editor sync would give it. */
	{
		g_autoptr(LrgNode) edited = load_resolved (f->path);
		g_autofree gchar  *derived = lrg_prefab_derive_guid (lrg_node_get_guid (root),
		                                                     lrg_node_get_guid (child (edited, "Bolt")));
		g_autoptr(LrgNode) synced = lrg_prefab_copy (inst, TRUE);

		g_assert_cmpstr (lrg_node_get_guid (child (root, "Bolt")), ==, derived);
		/* ... and the same tree an editor sync of the expanded instance
		 * produces. */
		lrg_prefab_sync_instance (synced, edited);
		{
			g_autofree gchar *want = expanded_text (synced);
			g_autofree gchar *got = expanded_text (root);

			g_assert_cmpstr (got, ==, want);
		}
	}

	/* The level file was not touched. */
	{
		g_autofree gchar *after = NULL;

		g_assert_true (g_file_get_contents (ref_path, &after, NULL, NULL));
		g_assert_cmpstr (after, ==, before);
	}
}

/* Level setting, project setting, serializer override precedence. */
static void
test_storage_settings (Fixture       *f,
                       gconstpointer  data)
{
	g_autoptr(LrgNode)  prefab = load_resolved (f->path);
	g_autoptr(LrgLevel) level = lrg_level_new ("level");
	LrgNode            *inst = lrg_prefab_instantiate_linked (prefab, f->path, NULL);
	g_autofree gchar   *path = g_build_filename (f->dir, "sub", "settings.rlevel", NULL);
	g_autofree gchar   *sub = g_build_filename (f->dir, "sub", NULL);
	g_autofree gchar   *manifest = g_build_filename (f->dir, "project.ryaml", NULL);
	g_autoptr(GError)   error = NULL;

	g_assert_cmpint (g_mkdir_with_parents (sub, 0755), ==, 0);
	lrg_level_add_node (level, inst, NULL);
	g_object_unref (inst);

	g_assert_cmpint (lrg_prefab_storage_from_string ("Reference"), ==, LRG_PREFAB_STORAGE_REFERENCE);
	g_assert_cmpint (lrg_prefab_storage_from_string ("nonsense"), ==, LRG_PREFAB_STORAGE_DEFAULT);
	g_assert_cmpstr (lrg_prefab_storage_to_string (LRG_PREFAB_STORAGE_EXPANDED), ==, "expanded");

	/* 1. Nothing set: expanded (the 0.8.x format). */
	save_level (level, path, LRG_PREFAB_STORAGE_DEFAULT);
	g_assert_false (file_contains (path, "storage: reference"));

	/* 2. Project setting, discovered above the level's directory. */
	g_assert_true (g_file_set_contents (manifest,
	                                    "project:\n  name: t\n  store_prefab_instances: reference\n",
	                                    -1, &error));
	g_assert_no_error (error);
	save_level (level, path, LRG_PREFAB_STORAGE_DEFAULT);
	g_assert_true (file_contains (path, "storage: reference"));
	g_assert_false (file_contains (path, "store_prefab_instances"));

	/* 3. The level setting beats the project, and is saved and loaded. */
	lrg_prefab_level_set_storage (level, LRG_PREFAB_STORAGE_EXPANDED);
	save_level (level, path, LRG_PREFAB_STORAGE_DEFAULT);
	g_assert_false (file_contains (path, "storage: reference"));
	g_assert_true (file_contains (path, "store_prefab_instances: expanded"));
	{
		g_autoptr(LrgLevel) back = load_level (path);

		g_assert_cmpint (lrg_prefab_level_get_storage (back), ==, LRG_PREFAB_STORAGE_EXPANDED);
	}

	/* 4. The serializer property beats both. */
	save_level (level, path, LRG_PREFAB_STORAGE_REFERENCE);
	g_assert_true (file_contains (path, "storage: reference"));

	/* A project-relative source path resolves through the project root. */
	{
		g_autoptr(LrgLevel)           rel = lrg_level_new ("rel");
		LrgNode                      *ri = lrg_prefab_instantiate_linked (prefab, "crate.rprefab", NULL);
		g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
		g_autoptr(LrgLevel)           back = NULL;

		lrg_node_set_scale_xyz (ri, 3.0f, 3.0f, 3.0f);
		lrg_prefab_mark_override (ri, "transform/scale");
		lrg_level_add_node (rel, ri, NULL);
		g_object_unref (ri);
		save_level (rel, path, LRG_PREFAB_STORAGE_REFERENCE);
		back = lrg_level_serializer_load_from_file (ser, path, &error);
		g_assert_no_error (error);
		g_assert_nonnull (child (first_node (back), "Lid/Knob"));
		g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (first_node (back))->x, 3.0f, 1e-5);
	}
}

/* A missing prefab keeps the reference instead of failing the load. */
static void
test_reference_missing_source (Fixture       *f,
                               gconstpointer  data)
{
	g_autoptr(LrgNode)  prefab = load_resolved (f->path);
	g_autoptr(LrgLevel) level = lrg_level_new ("level");
	LrgNode            *inst = lrg_prefab_instantiate_linked (prefab, f->path, NULL);
	g_autofree gchar   *path = g_build_filename (f->dir, "missing.rlevel", NULL);
	g_autoptr(LrgLevel) back = NULL;

	lrg_level_add_node (level, inst, NULL);
	g_object_unref (inst);
	save_level (level, path, LRG_PREFAB_STORAGE_REFERENCE);
	g_assert_cmpint (g_remove (f->path), ==, 0);

	back = load_level (path);
	g_assert_nonnull (lrg_node_get_prefab_link (first_node (back)));
	g_assert_null (child (first_node (back), "Lid"));
}

/* ==========================================================================
 * Variants
 * ========================================================================== */

static gchar *
make_variant_file (Fixture     *f,
                   const gchar *base_path,
                   const gchar *name,
                   gfloat       scale)
{
	g_autoptr(LrgNode) base = load_resolved (base_path);
	g_autoptr(LrgNode) variant = lrg_prefab_create_variant (base, base_path, NULL);
	g_autoptr(GError)  error = NULL;
	gchar             *path = g_build_filename (f->dir, name, NULL);

	lrg_node_set_scale_xyz (variant, scale, scale, scale);
	lrg_prefab_mark_override (variant, "transform/scale");
	g_assert_true (lrg_prefab_save (variant, path, &error));
	g_assert_no_error (error);
	return path;
}

static void
test_variant_chain (Fixture       *f,
                    gconstpointer  data)
{
	g_autofree gchar   *red = make_variant_file (f, f->path, "red.rprefab", 2.0f);
	g_autofree gchar   *big = make_variant_file (f, red, "big_red.rprefab", 5.0f);
	g_autoptr(LrgNode)  loaded = NULL;
	g_autoptr(LrgNode)  resolved = NULL;
	g_auto(GStrv)       chain = NULL;
	g_auto(GStrv)       deps = NULL;
	g_autoptr(GError)   error = NULL;

	/* The variant root keeps its link through save/load. */
	loaded = lrg_prefab_load (red, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_prefab_is_variant (loaded));
	g_assert_cmpstr (lrg_prefab_link_get_source_path (lrg_node_get_prefab_link (loaded)), ==, f->path);
	g_assert_false (lrg_prefab_is_variant (child (loaded, "Lid")));

	chain = lrg_prefab_get_variant_chain (big, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (g_strv_length (chain), ==, 3);
	g_assert_cmpstr (chain[0], ==, big);
	g_assert_cmpstr (chain[1], ==, red);
	g_assert_cmpstr (chain[2], ==, f->path);

	/* A base edit reaches both variants; variant overrides win. */
	edit_prefab (f->path, edit_crate);
	resolved = lrg_prefab_load_resolved (big, &deps, &error);
	g_assert_no_error (error);
	g_assert_nonnull (child (resolved, "Bolt"));
	g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (child (resolved, "Lid/Knob"))->x, 4.0f, 1e-5);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (resolved)->x, 5.0f, 1e-5);
	g_assert_cmpint (int_value (resolved, "health", "max"), ==, 999);
	g_assert_cmpuint (g_strv_length (deps), ==, 2);
	{
		g_autofree gchar *canon_base = g_canonicalize_filename (f->path, NULL);

		g_assert_true (g_strv_contains ((const gchar * const *) deps, canon_base));
	}

	/* ... and an instance of the variant stored by reference. */
	{
		g_autoptr(LrgNode)  vprefab = load_resolved (big);
		g_autoptr(LrgLevel) level = lrg_level_new ("level");
		LrgNode            *inst = lrg_prefab_instantiate_linked (vprefab, big, NULL);
		g_autofree gchar   *path = g_build_filename (f->dir, "variant.rlevel", NULL);
		g_autoptr(LrgLevel) back = NULL;

		lrg_level_add_node (level, inst, NULL);
		g_object_unref (inst);
		save_level (level, path, LRG_PREFAB_STORAGE_REFERENCE);

		/* Base edited again: the level (two hops away) follows. */
		{
			g_autoptr(GError)  e2 = NULL;
			g_autoptr(LrgNode) base = lrg_prefab_load (f->path, &e2);
			g_autoptr(LrgNode) nut = lrg_node_new ("Nut");

			g_assert_no_error (e2);
			lrg_node_add_child (base, nut);
			g_assert_true (lrg_prefab_save (base, f->path, &e2));
			g_assert_no_error (e2);
		}
		back = load_level (path);
		g_assert_nonnull (child (first_node (back), "Nut"));
		g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (first_node (back))->x, 5.0f, 1e-5);
	}
}

/* Rewrites the root link source of the prefab at @path. */
static void
retarget (const gchar *path,
          const gchar *source)
{
	g_autoptr(GError)  error = NULL;
	g_autoptr(LrgNode) prefab = lrg_prefab_load (path, &error);

	g_assert_no_error (error);
	g_assert_nonnull (lrg_node_get_prefab_link (prefab));
	lrg_prefab_link_set_source_path (lrg_node_get_prefab_link (prefab), source);
	g_assert_true (lrg_prefab_save (prefab, path, &error));
	g_assert_no_error (error);
}

static void
test_variant_cycle_and_depth (Fixture       *f,
                              gconstpointer  data)
{
	g_autofree gchar *a = make_variant_file (f, f->path, "a.rprefab", 2.0f);
	g_autofree gchar *b = make_variant_file (f, a, "b.rprefab", 3.0f);
	g_autoptr(GError) error = NULL;
	LrgNode          *none;
	GStrv             chain;
	gchar            *prev;
	guint             i;

	/* a -> b -> a */
	retarget (a, b);
	none = lrg_prefab_load_resolved (a, NULL, &error);
	g_assert_null (none);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	g_clear_error (&error);
	chain = lrg_prefab_get_variant_chain (b, &error);
	g_assert_null (chain);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	g_clear_error (&error);

	/* Ten variants deep: past the limit of 8. */
	retarget (a, f->path);
	prev = g_strdup (f->path);
	for (i = 0; i < 10; i++)
	{
		g_autofree gchar *name = g_strdup_printf ("deep%u.rprefab", i);
		gchar            *next = make_variant_file (f, i < 7 ? prev : f->path, name, 1.0f);

		if (i >= 7)
			retarget (next, prev);
		g_free (prev);
		prev = next;
	}
	none = lrg_prefab_load_resolved (prev, NULL, &error);
	g_assert_null (none);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	g_free (prev);
}

/* Create Variant from an instance, without the editor. */
static void
test_derive_variant (Fixture       *f,
                     gconstpointer  data)
{
	g_autoptr(LrgNode)  prefab = load_resolved (f->path);
	g_autoptr(LrgNode)  inst = make_edited_instance (f, prefab);
	g_autofree gchar   *vpath = g_build_filename (f->dir, "derived.rprefab", NULL);
	g_autoptr(LrgNode)  variant = NULL;
	g_autoptr(LrgNode)  resolved = NULL;
	g_autoptr(GError)   error = NULL;
	guint               n_before = lrg_prefab_link_get_n_overrides (lrg_node_get_prefab_link (inst));

	variant = lrg_prefab_derive_variant (inst, vpath, NULL);
	g_assert_nonnull (variant);
	g_assert_true (lrg_prefab_is_variant (variant));
	g_assert_cmpuint (lrg_prefab_link_get_n_overrides (lrg_node_get_prefab_link (variant)), ==, n_before);
	g_assert_cmpuint (lrg_prefab_link_get_n_overrides (lrg_node_get_prefab_link (inst)), ==, 0);
	g_assert_cmpstr (lrg_prefab_link_get_source_path (lrg_node_get_prefab_link (inst)), ==, vpath);
	g_assert_cmpstr (lrg_node_get_guid (variant), !=, lrg_node_get_guid (inst));

	g_assert_true (lrg_prefab_save (variant, vpath, &error));
	g_assert_no_error (error);

	/* Syncing the instance against its new (resolved) source changes
	 * nothing: the variant holds exactly what the instance overrode. (The
	 * return value is not checked: values read back from YAML may differ
	 * in GValue integer type only, which a sync rewrites.) */
	resolved = load_resolved (vpath);
	{
		g_autofree gchar *t0 = expanded_text (inst);
		g_autofree gchar *t1 = NULL;

		lrg_prefab_sync_instance (inst, resolved);
		t1 = expanded_text (inst);
		g_assert_cmpstr (t1, ==, t0);
	}
	g_assert_cmpuint (lrg_node_get_children (child (inst, "Lid"))->len, ==, 3);
	g_assert_cmpint (int_value (inst, "health", "max"), ==, 250);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (inst)->x, 2.0f, 1e-5);
	g_assert_null (child (inst, "Handle"));

	/* Not a linked instance: NULL. */
	{
		g_autoptr(LrgNode) plain = lrg_node_new ("plain");

		g_assert_null (lrg_prefab_derive_variant (plain, vpath, NULL));
	}
}

#ifdef LRG_BUILD_EDITOR
/* ==========================================================================
 * Editor: Create Variant and undo
 * ========================================================================== */

static void
test_editor_create_variant (Fixture       *f,
                            gconstpointer  data)
{
	g_autoptr(LrgEditor) editor = lrg_editor_new ();
	g_autoptr(GError)    error = NULL;
	g_autofree gchar    *suggested = lrg_editor_prefab_suggest_variant_path (f->path);
	g_autofree gchar    *from_instance = NULL;
	LrgNode             *inst;

	/* From the asset. */
	g_assert_true (g_str_has_suffix (suggested, "crate_variant.rprefab"));
	g_assert_true (lrg_editor_create_prefab_variant (editor, f->path, suggested, &error));
	g_assert_no_error (error);
	g_assert_true (g_file_test (suggested, G_FILE_TEST_IS_REGULAR));
	g_assert_false (lrg_editor_create_prefab_variant (editor, f->path, suggested, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
	g_clear_error (&error);
	lrg_editor_undo (editor);
	g_assert_false (g_file_test (suggested, G_FILE_TEST_EXISTS));
	lrg_editor_redo (editor);
	g_assert_true (g_file_test (suggested, G_FILE_TEST_IS_REGULAR));

	/* From an instance with an override. */
	inst = lrg_editor_instantiate_prefab (editor, f->path, NULL, &error);
	g_assert_no_error (error);
	lrg_node_set_scale_xyz (inst, 3.0f, 3.0f, 3.0f);
	lrg_prefab_mark_override (inst, "transform/scale");
	from_instance = lrg_editor_prefab_suggest_variant_path (f->path);
	g_assert_cmpstr (from_instance, !=, suggested);
	g_assert_true (lrg_editor_prefab_create_variant (editor, inst, from_instance, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_prefab_link_get_source_path (lrg_node_get_prefab_link (inst)), ==, from_instance);
	g_assert_cmpuint (lrg_prefab_link_get_n_overrides (lrg_node_get_prefab_link (inst)), ==, 0);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (inst)->x, 3.0f, 1e-5);

	/* The prefab bar's chain: base > variant. */
	{
		g_auto(GStrv) chain = lrg_prefab_get_variant_chain (from_instance, &error);

		g_assert_no_error (error);
		g_assert_cmpuint (g_strv_length (chain), ==, 2);
	}

	/* One undo restores the link and removes the file. */
	lrg_editor_undo (editor);
	g_assert_false (g_file_test (from_instance, G_FILE_TEST_EXISTS));
	g_assert_cmpstr (lrg_prefab_link_get_source_path (lrg_node_get_prefab_link (inst)), ==, f->path);
	g_assert_cmpuint (lrg_prefab_link_get_n_overrides (lrg_node_get_prefab_link (inst)), ==, 1);
	lrg_editor_redo (editor);
	g_assert_true (g_file_test (from_instance, G_FILE_TEST_IS_REGULAR));
	g_assert_cmpstr (lrg_prefab_link_get_source_path (lrg_node_get_prefab_link (inst)), ==, from_instance);

	/* A base edit saved through the editor reaches the variant instance. */
	{
		g_autoptr(LrgNode) base = lrg_prefab_load (f->path, &error);
		g_autoptr(LrgNode) bolt = lrg_node_new ("Bolt");

		g_assert_no_error (error);
		lrg_node_add_child (base, bolt);
		g_assert_true (lrg_prefab_save (base, f->path, &error));
		g_assert_no_error (error);
		lrg_editor_prefab_changed (editor, f->path);
		g_assert_nonnull (child (inst, "Bolt"));
		g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (inst)->x, 3.0f, 1e-5);
	}
}
#endif

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add ("/runtime-prefab/reference/in-memory", Fixture, NULL,
	            fixture_setup, test_reference_in_memory, fixture_teardown);
	g_test_add ("/runtime-prefab/reference/round-trip", Fixture, NULL,
	            fixture_setup, test_reference_round_trip, fixture_teardown);
	g_test_add ("/runtime-prefab/reference/propagation", Fixture, NULL,
	            fixture_setup, test_reference_propagation, fixture_teardown);
	g_test_add ("/runtime-prefab/reference/settings", Fixture, NULL,
	            fixture_setup, test_storage_settings, fixture_teardown);
	g_test_add ("/runtime-prefab/reference/missing-source", Fixture, NULL,
	            fixture_setup, test_reference_missing_source, fixture_teardown);
	g_test_add ("/runtime-prefab/variant/chain", Fixture, NULL,
	            fixture_setup, test_variant_chain, fixture_teardown);
	g_test_add ("/runtime-prefab/variant/cycle-and-depth", Fixture, NULL,
	            fixture_setup, test_variant_cycle_and_depth, fixture_teardown);
	g_test_add ("/runtime-prefab/variant/derive", Fixture, NULL,
	            fixture_setup, test_derive_variant, fixture_teardown);
#ifdef LRG_BUILD_EDITOR
	g_test_add ("/runtime-prefab/editor/create-variant", Fixture, NULL,
	            fixture_setup, test_editor_create_variant, fixture_teardown);
#endif

	return g_test_run ();
}
