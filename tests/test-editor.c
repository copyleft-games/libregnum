/* test-editor.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the editor / level authoring module: LrgComponentDesc,
 * LrgScriptBinding, LrgNodeVisual, LrgNode, LrgLevel, the .rlevel serializer,
 * the scene importer, and the bake / instantiate bridges.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <libregnum.h>

#define EPS 0.0001

/* ==========================================================================
 * A trivial component type for the instantiate test
 * ========================================================================== */

#define TEST_TYPE_COMP (test_comp_get_type ())
G_DECLARE_FINAL_TYPE (TestComp, test_comp, TEST, COMP, LrgComponent)

struct _TestComp
{
	LrgComponent parent_instance;
	gint         value;
};

G_DEFINE_FINAL_TYPE (TestComp, test_comp, LRG_TYPE_COMPONENT)

enum
{
	TC_PROP_0,
	TC_PROP_VALUE,
	TC_N_PROPS
};

static GParamSpec *tc_props[TC_N_PROPS];

static void
test_comp_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	TestComp *self = TEST_COMP (object);

	if (prop_id == TC_PROP_VALUE)
		g_value_set_int (value, self->value);
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
test_comp_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	TestComp *self = TEST_COMP (object);

	if (prop_id == TC_PROP_VALUE)
		self->value = g_value_get_int (value);
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
test_comp_class_init (TestCompClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = test_comp_get_property;
	object_class->set_property = test_comp_set_property;

	tc_props[TC_PROP_VALUE] =
		g_param_spec_int ("value", "Value", "Test value",
		                  G_MININT, G_MAXINT, 0,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_properties (object_class, TC_N_PROPS, tc_props);
}

static void
test_comp_init (TestComp *self)
{
	self->value = 0;
}

/* ==========================================================================
 * LrgComponentDesc
 * ========================================================================== */

static void
test_component_desc (void)
{
	g_autoptr(LrgComponentDesc) desc = lrg_component_desc_new ("health");
	const GValue              *v;

	g_assert_nonnull (desc);
	g_assert_cmpstr (lrg_component_desc_get_type_name (desc), ==, "health");

	lrg_component_desc_set_string (desc, "name", "hero");
	lrg_component_desc_set_int (desc, "max", 100);
	lrg_component_desc_set_double (desc, "speed", 2.5);
	lrg_component_desc_set_boolean (desc, "active", TRUE);

	g_assert_true (lrg_component_desc_has (desc, "name"));
	g_assert_false (lrg_component_desc_has (desc, "missing"));

	v = lrg_component_desc_get_value (desc, "name");
	g_assert_nonnull (v);
	g_assert_true (G_VALUE_HOLDS_STRING (v));
	g_assert_cmpstr (g_value_get_string (v), ==, "hero");

	v = lrg_component_desc_get_value (desc, "max");
	g_assert_true (G_VALUE_HOLDS_INT (v));
	g_assert_cmpint (g_value_get_int (v), ==, 100);

	v = lrg_component_desc_get_value (desc, "speed");
	g_assert_true (G_VALUE_HOLDS_DOUBLE (v));
	g_assert_cmpfloat (g_value_get_double (v), ==, 2.5);

	v = lrg_component_desc_get_value (desc, "active");
	g_assert_true (G_VALUE_HOLDS_BOOLEAN (v));
	g_assert_true (g_value_get_boolean (v));

	g_assert_null (lrg_component_desc_get_value (desc, "missing"));

	/* Overwrite keeps a single entry. */
	lrg_component_desc_set_int (desc, "max", 200);
	g_assert_cmpint (g_value_get_int (lrg_component_desc_get_value (desc, "max")), ==, 200);
}

/* ==========================================================================
 * LrgScriptBinding
 * ========================================================================== */

static void
test_script_binding (void)
{
	g_autoptr(LrgScriptBinding) b = lrg_script_binding_new (LRG_SCRIPT_LANGUAGE_LUA,
	                                                        "scripts/player.lua");

	g_assert_cmpint (lrg_script_binding_get_language (b), ==, LRG_SCRIPT_LANGUAGE_LUA);
	g_assert_cmpstr (lrg_script_binding_get_script (b), ==, "scripts/player.lua");
	g_assert_true (lrg_script_binding_get_enabled (b));

	lrg_script_binding_set_language (b, LRG_SCRIPT_LANGUAGE_CRISPY);
	lrg_script_binding_set_script (b, "scripts/enemy.c");
	lrg_script_binding_set_enabled (b, FALSE);

	g_assert_cmpint (lrg_script_binding_get_language (b), ==, LRG_SCRIPT_LANGUAGE_CRISPY);
	g_assert_cmpstr (lrg_script_binding_get_script (b), ==, "scripts/enemy.c");
	g_assert_false (lrg_script_binding_get_enabled (b));
}

/* ==========================================================================
 * LrgNodeVisual
 * ========================================================================== */

static void
test_node_visual (void)
{
	g_autoptr(LrgNodeVisual) v = lrg_node_visual_new (LRG_NODE_VISUAL_PRIMITIVE);
	g_autoptr(LrgMaterial3D) mat = lrg_material3d_new ();

	g_assert_cmpint (lrg_node_visual_get_kind (v), ==, LRG_NODE_VISUAL_PRIMITIVE);

	lrg_node_visual_set_primitive (v, LRG_PRIMITIVE_UV_SPHERE);
	g_assert_cmpint (lrg_node_visual_get_primitive (v), ==, LRG_PRIMITIVE_UV_SPHERE);

	lrg_node_visual_set_asset (v, "models/x.glb");
	g_assert_cmpstr (lrg_node_visual_get_asset (v), ==, "models/x.glb");

	lrg_material3d_set_color (mat, 0.25f, 0.5f, 0.75f, 1.0f);
	lrg_node_visual_set_material (v, mat);
	g_assert_true (lrg_node_visual_get_material (v) == mat);

	lrg_node_visual_set_param_double (v, "intensity", 1.5);
	g_assert_cmpfloat (lrg_node_visual_get_param_double (v, "intensity", 0.0), ==, 1.5);
	g_assert_cmpfloat (lrg_node_visual_get_param_double (v, "missing", -1.0), ==, -1.0);
}

/* ==========================================================================
 * LrgNode
 * ========================================================================== */

static void
test_node_basic (void)
{
	g_autoptr(LrgNode) node = lrg_node_new ("Player");
	GrlVector3        *loc;

	g_assert_cmpstr (lrg_node_get_name (node), ==, "Player");
	g_assert_nonnull (lrg_node_get_guid (node));
	g_assert_cmpuint (strlen (lrg_node_get_guid (node)), >, 0);

	/* Identity transform by default. */
	loc = lrg_node_get_location (node);
	g_assert_cmpfloat (loc->x, ==, 0.0f);
	g_assert_cmpfloat (lrg_node_get_scale (node)->x, ==, 1.0f);

	lrg_node_set_location_xyz (node, 1.5f, -2.0f, 3.25f);
	loc = lrg_node_get_location (node);
	g_assert_cmpfloat (loc->x, ==, 1.5f);
	g_assert_cmpfloat (loc->y, ==, -2.0f);
	g_assert_cmpfloat (loc->z, ==, 3.25f);

	g_assert_true (lrg_node_get_visible (node));
	lrg_node_set_visible (node, FALSE);
	g_assert_false (lrg_node_get_visible (node));

	lrg_node_set_is_2d (node, TRUE);
	g_assert_true (lrg_node_get_is_2d (node));
}

static void
test_node_hierarchy (void)
{
	g_autoptr(LrgNode) root = lrg_node_new ("root");
	LrgNode           *a = lrg_node_new ("a");
	LrgNode           *b = lrg_node_new ("b");
	const gchar       *a_guid;

	lrg_node_add_child (root, a);
	lrg_node_add_child (root, b);
	g_object_unref (a);   /* root holds a ref now */
	g_object_unref (b);

	g_assert_cmpuint (lrg_node_get_n_children (root), ==, 2);
	g_assert_true (lrg_node_get_parent (a) == root);

	a_guid = lrg_node_get_guid (a);
	g_assert_true (lrg_node_find_by_guid (root, a_guid) == a);
	g_assert_null (lrg_node_find_by_guid (root, "no-such-guid"));

	g_assert_true (lrg_node_remove_child (root, a));
	g_assert_cmpuint (lrg_node_get_n_children (root), ==, 1);
	g_assert_true (lrg_node_get_parent (b) == root);
}

static void
test_node_payloads (void)
{
	g_autoptr(LrgNode)         node = lrg_node_new ("n");
	g_autoptr(LrgNodeVisual)   v = lrg_node_visual_new (LRG_NODE_VISUAL_SPRITE);
	g_autoptr(LrgComponentDesc) c = lrg_component_desc_new ("health");
	g_autoptr(LrgScriptBinding) s = lrg_script_binding_new (LRG_SCRIPT_LANGUAGE_PYTHON, "s.py");

	lrg_node_set_visual (node, v);
	g_assert_true (lrg_node_get_visual (node) == v);

	lrg_node_add_component (node, c);
	lrg_node_add_script (node, s);
	g_assert_cmpuint (lrg_node_get_components (node)->len, ==, 1);
	g_assert_cmpuint (lrg_node_get_scripts (node)->len, ==, 1);

	/* instantiate vfunc default is a no-op; must not crash with NULL object. */
	lrg_node_instantiate (node, NULL);
}

/* ==========================================================================
 * LrgLevel
 * ========================================================================== */

typedef struct
{
	guint added;
	guint removed;
	guint changed;
} SignalCounts;

static void
on_node_added (LrgLevel *level, LrgNode *node, gpointer data)
{
	((SignalCounts *) data)->added++;
}

static void
on_node_removed (LrgLevel *level, LrgNode *node, gpointer data)
{
	((SignalCounts *) data)->removed++;
}

static void
on_node_changed (LrgLevel *level, LrgNode *node, gpointer data)
{
	((SignalCounts *) data)->changed++;
}

static void
test_level (void)
{
	g_autoptr(LrgLevel) level = lrg_level_new ("Level 1");
	LrgNode            *a = lrg_node_new ("a");
	LrgNode            *child = lrg_node_new ("child");
	SignalCounts        counts = { 0, 0, 0 };
	const gchar        *a_guid;

	g_assert_cmpstr (lrg_level_get_name (level), ==, "Level 1");
	g_assert_nonnull (lrg_level_get_root (level));
	g_assert_false (lrg_level_get_default_2d (level));

	g_signal_connect (level, "node-added", G_CALLBACK (on_node_added), &counts);
	g_signal_connect (level, "node-removed", G_CALLBACK (on_node_removed), &counts);
	g_signal_connect (level, "node-changed", G_CALLBACK (on_node_changed), &counts);

	lrg_level_add_node (level, a, NULL);          /* under root */
	lrg_level_add_node (level, child, a);         /* under a */
	g_object_unref (a);
	g_object_unref (child);

	g_assert_cmpuint (counts.added, ==, 2);
	g_assert_cmpuint (lrg_node_get_n_children (lrg_level_get_root (level)), ==, 1);

	a_guid = lrg_node_get_guid (a);
	g_assert_true (lrg_level_find_node (level, a_guid) == a);
	g_assert_true (lrg_level_find_node (level, lrg_node_get_guid (child)) == child);

	lrg_level_notify_node_changed (level, a);
	g_assert_cmpuint (counts.changed, ==, 1);

	g_assert_true (lrg_level_remove_node (level, a));
	g_assert_cmpuint (counts.removed, ==, 1);
	g_assert_cmpuint (lrg_node_get_n_children (lrg_level_get_root (level)), ==, 0);

	lrg_level_set_default_2d (level, TRUE);
	g_assert_true (lrg_level_get_default_2d (level));
}

/* ==========================================================================
 * Serializer round-trip
 * ========================================================================== */

static LrgLevel *
build_sample_level (void)
{
	LrgLevel                  *level = lrg_level_new ("Sample");
	LrgNode                   *parent = lrg_node_new ("Parent");
	LrgNode                   *child = lrg_node_new ("Child");
	LrgNodeVisual             *visual = lrg_node_visual_new (LRG_NODE_VISUAL_PRIMITIVE);
	LrgMaterial3D             *mat = lrg_material3d_new ();
	LrgComponentDesc          *desc = lrg_component_desc_new ("health");
	LrgScriptBinding          *script = lrg_script_binding_new (LRG_SCRIPT_LANGUAGE_LUA, "p.lua");

	lrg_node_set_location_xyz (parent, 1.0f, 2.0f, 3.0f);
	lrg_node_set_rotation_xyz (parent, 0.1f, 0.2f, 0.3f);
	lrg_node_set_scale_xyz (parent, 2.0f, 2.0f, 2.0f);
	lrg_node_set_is_2d (parent, TRUE);

	lrg_node_visual_set_primitive (visual, LRG_PRIMITIVE_UV_SPHERE);
	lrg_material3d_set_color (mat, 0.2f, 0.4f, 0.6f, 1.0f);
	lrg_material3d_set_roughness (mat, 0.7f);
	lrg_node_visual_set_material (visual, mat);
	lrg_node_visual_set_param_double (visual, "radius", 1.25);
	lrg_node_set_visual (child, visual);

	lrg_component_desc_set_int (desc, "max", 100);
	lrg_component_desc_set_string (desc, "label", "hero");
	lrg_component_desc_set_boolean (desc, "active", TRUE);
	lrg_node_add_component (child, desc);
	lrg_node_add_script (child, script);

	lrg_level_add_node (level, parent, NULL);
	lrg_level_add_node (level, child, parent);

	g_object_unref (parent);
	g_object_unref (child);
	g_object_unref (visual);
	g_object_unref (mat);
	g_object_unref (desc);
	g_object_unref (script);

	return level;
}

static void
test_serializer_roundtrip (void)
{
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(LrgLevel)           level = build_sample_level ();
	g_autoptr(LrgLevel)           loaded = NULL;
	g_autofree gchar             *data = NULL;
	g_autoptr(GError)             error = NULL;
	gsize                         length = 0;
	LrgNode                      *root, *parent, *child;
	LrgNodeVisual                *visual;
	LrgMaterial3D                *mat;
	GrlVector3                   *loc;
	gfloat                        r, g, b, a;
	const gchar                  *parent_guid;

	data = lrg_level_serializer_save_to_data (ser, level, &length);
	g_assert_nonnull (data);
	g_assert_cmpuint (length, >, 0);

	loaded = lrg_level_serializer_load_from_data (ser, data, (gssize) length, &error);
	g_assert_no_error (error);
	g_assert_nonnull (loaded);

	g_assert_cmpstr (lrg_level_get_name (loaded), ==, "Sample");

	root = lrg_level_get_root (loaded);
	g_assert_cmpuint (lrg_node_get_n_children (root), ==, 1);

	parent = g_ptr_array_index (lrg_node_get_children (root), 0);
	g_assert_cmpstr (lrg_node_get_name (parent), ==, "Parent");
	g_assert_true (lrg_node_get_is_2d (parent));

	loc = lrg_node_get_location (parent);
	g_assert_cmpfloat_with_epsilon (loc->x, 1.0f, EPS);
	g_assert_cmpfloat_with_epsilon (loc->y, 2.0f, EPS);
	g_assert_cmpfloat_with_epsilon (loc->z, 3.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (parent)->x, 2.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_rotation (parent)->y, 0.2f, EPS);

	/* guid preserved across round-trip */
	parent_guid = lrg_node_get_guid (g_ptr_array_index (lrg_node_get_children (lrg_level_get_root (level)), 0));
	g_assert_true (lrg_level_find_node (loaded, parent_guid) == parent);

	g_assert_cmpuint (lrg_node_get_n_children (parent), ==, 1);
	child = g_ptr_array_index (lrg_node_get_children (parent), 0);
	g_assert_cmpstr (lrg_node_get_name (child), ==, "Child");

	visual = lrg_node_get_visual (child);
	g_assert_nonnull (visual);
	g_assert_cmpint (lrg_node_visual_get_kind (visual), ==, LRG_NODE_VISUAL_PRIMITIVE);
	g_assert_cmpint (lrg_node_visual_get_primitive (visual), ==, LRG_PRIMITIVE_UV_SPHERE);
	g_assert_cmpfloat_with_epsilon (lrg_node_visual_get_param_double (visual, "radius", 0.0), 1.25, EPS);

	mat = lrg_node_visual_get_material (visual);
	g_assert_nonnull (mat);
	lrg_material3d_get_color (mat, &r, &g, &b, &a);
	g_assert_cmpfloat_with_epsilon (r, 0.2f, EPS);
	g_assert_cmpfloat_with_epsilon (g, 0.4f, EPS);
	g_assert_cmpfloat_with_epsilon (b, 0.6f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_material3d_get_roughness (mat), 0.7f, EPS);

	/* component: integers come back as int64 (typed YAML scalar heuristic) */
	g_assert_cmpuint (lrg_node_get_components (child)->len, ==, 1);
	{
		LrgComponentDesc *cd = g_ptr_array_index (lrg_node_get_components (child), 0);
		const GValue     *gv;

		g_assert_cmpstr (lrg_component_desc_get_type_name (cd), ==, "health");

		gv = lrg_component_desc_get_value (cd, "max");
		g_assert_nonnull (gv);
		g_assert_true (G_VALUE_HOLDS_INT64 (gv));
		g_assert_cmpint (g_value_get_int64 (gv), ==, 100);

		gv = lrg_component_desc_get_value (cd, "label");
		g_assert_true (G_VALUE_HOLDS_STRING (gv));
		g_assert_cmpstr (g_value_get_string (gv), ==, "hero");

		gv = lrg_component_desc_get_value (cd, "active");
		g_assert_true (G_VALUE_HOLDS_BOOLEAN (gv));
		g_assert_true (g_value_get_boolean (gv));
	}

	/* script binding */
	g_assert_cmpuint (lrg_node_get_scripts (child)->len, ==, 1);
	{
		LrgScriptBinding *sb = g_ptr_array_index (lrg_node_get_scripts (child), 0);

		g_assert_cmpint (lrg_script_binding_get_language (sb), ==, LRG_SCRIPT_LANGUAGE_LUA);
		g_assert_cmpstr (lrg_script_binding_get_script (sb), ==, "p.lua");
		g_assert_true (lrg_script_binding_get_enabled (sb));
	}
}

static void
test_serializer_empty (void)
{
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(LrgLevel)           level = lrg_level_new ("Empty");
	g_autoptr(LrgLevel)           loaded = NULL;
	g_autofree gchar             *data = NULL;
	g_autoptr(GError)             error = NULL;
	gsize                         length = 0;

	data = lrg_level_serializer_save_to_data (ser, level, &length);
	g_assert_nonnull (data);

	loaded = lrg_level_serializer_load_from_data (ser, data, (gssize) length, &error);
	g_assert_no_error (error);
	g_assert_nonnull (loaded);
	g_assert_cmpstr (lrg_level_get_name (loaded), ==, "Empty");
	g_assert_cmpuint (lrg_node_get_n_children (lrg_level_get_root (loaded)), ==, 0);
}

static void
test_serializer_file (void)
{
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(LrgLevel)           level = build_sample_level ();
	g_autoptr(LrgLevel)           loaded = NULL;
	g_autoptr(GError)             error = NULL;
	g_autofree gchar             *dir = NULL;
	g_autofree gchar             *path = NULL;

	dir = g_dir_make_tmp ("lrg-editor-XXXXXX", &error);
	g_assert_no_error (error);
	path = g_build_filename (dir, "sample.rlevel", NULL);

	g_assert_true (lrg_level_serializer_save_to_file (ser, level, path, &error));
	g_assert_no_error (error);
	g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));

	loaded = lrg_level_serializer_load_from_file (ser, path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (loaded);
	g_assert_cmpuint (lrg_node_get_n_children (lrg_level_get_root (loaded)), ==, 1);

	g_remove (path);
	g_rmdir (dir);
}

static void
test_serializer_invalid (void)
{
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(GError)             error = NULL;
	LrgLevel                     *loaded;

	/* A bare scalar is valid YAML but not a level mapping. */
	loaded = lrg_level_serializer_load_from_data (ser, "42", -1, &error);
	g_assert_null (loaded);
	g_assert_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE);

	/* Missing file. */
	g_clear_error (&error);
	loaded = lrg_level_serializer_load_from_file (ser, "/nonexistent/does-not-exist.rlevel", &error);
	g_assert_null (loaded);
	g_assert_nonnull (error);
}

/* ==========================================================================
 * Scene import
 * ========================================================================== */

static void
test_scene_import (void)
{
	g_autoptr(LrgScene)       scene = lrg_scene_new ("imported");
	LrgSceneEntity           *entity = lrg_scene_entity_new ("ent");
	LrgSceneObject           *object = lrg_scene_object_new ("obj", LRG_PRIMITIVE_UV_SPHERE);
	LrgMaterial3D            *mat = lrg_material3d_new ();
	g_autoptr(LrgLevel)       level = NULL;
	LrgNode                  *ent_node, *obj_node;
	LrgNodeVisual            *visual;
	gfloat                    r, g, b, a;

	lrg_scene_entity_set_location_xyz (entity, 1.0f, 2.0f, 3.0f);
	lrg_material3d_set_color (mat, 0.1f, 0.2f, 0.3f, 1.0f);
	lrg_scene_object_set_material (object, mat);
	lrg_scene_entity_add_object (entity, object);
	lrg_scene_add_entity (scene, entity);
	g_object_unref (object);
	g_object_unref (entity);
	g_object_unref (mat);

	level = lrg_level_from_scene (scene);
	g_assert_nonnull (level);
	g_assert_cmpuint (lrg_node_get_n_children (lrg_level_get_root (level)), ==, 1);

	ent_node = g_ptr_array_index (lrg_node_get_children (lrg_level_get_root (level)), 0);
	g_assert_cmpstr (lrg_node_get_name (ent_node), ==, "ent");
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (ent_node)->x, 1.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (ent_node)->z, 3.0f, EPS);
	g_assert_cmpuint (lrg_node_get_n_children (ent_node), ==, 1);

	obj_node = g_ptr_array_index (lrg_node_get_children (ent_node), 0);
	visual = lrg_node_get_visual (obj_node);
	g_assert_nonnull (visual);
	g_assert_cmpint (lrg_node_visual_get_kind (visual), ==, LRG_NODE_VISUAL_PRIMITIVE);
	g_assert_cmpint (lrg_node_visual_get_primitive (visual), ==, LRG_PRIMITIVE_UV_SPHERE);
	lrg_material3d_get_color (lrg_node_visual_get_material (visual), &r, &g, &b, &a);
	g_assert_cmpfloat_with_epsilon (r, 0.1f, EPS);
	g_assert_cmpfloat_with_epsilon (g, 0.2f, EPS);
}

/* ==========================================================================
 * Bake (level -> scene)
 * ========================================================================== */

static void
test_bake_to_scene (void)
{
	g_autoptr(LrgLevel)      level = lrg_level_new ("baked");
	LrgNode                 *node = lrg_node_new ("cube");
	LrgNodeVisual           *visual = lrg_node_visual_new (LRG_NODE_VISUAL_PRIMITIVE);
	LrgNode                 *empty = lrg_node_new ("group");   /* no visual: not baked */
	g_autoptr(LrgScene)      scene = NULL;
	LrgSceneEntity          *entity;

	lrg_node_visual_set_primitive (visual, LRG_PRIMITIVE_CUBE);
	lrg_node_set_visual (node, visual);
	lrg_node_set_location_xyz (node, 5.0f, 0.0f, 0.0f);

	lrg_level_add_node (level, node, NULL);
	lrg_level_add_node (level, empty, NULL);
	g_object_unref (node);
	g_object_unref (visual);
	g_object_unref (empty);

	scene = lrg_level_to_scene (level);
	g_assert_nonnull (scene);
	/* Only the primitive node bakes into an entity. */
	g_assert_cmpuint (lrg_scene_get_entity_count (scene), ==, 1);

	entity = lrg_scene_get_entity (scene, lrg_node_get_guid (
	             g_ptr_array_index (lrg_node_get_children (lrg_level_get_root (level)), 0)));
	g_assert_nonnull (entity);
	g_assert_cmpfloat_with_epsilon (lrg_scene_entity_get_location (entity)->x, 5.0f, EPS);
	g_assert_cmpuint (lrg_scene_entity_get_object_count (entity), ==, 1);
}

/* ==========================================================================
 * Instantiate (level -> world)
 * ========================================================================== */

static void
test_instantiate (void)
{
	LrgEngine                  *engine;
	LrgRegistry                *registry;
	g_autoptr(LrgLevel)         level = NULL;
	g_autoptr(LrgWorld)         world = NULL;
	g_autoptr(GError)           error = NULL;
	LrgNode                    *node;
	LrgComponentDesc           *desc;
	GList                      *objects;
	LrgGameObject              *go;
	LrgComponent               *comp;
	gint                        value = 0;

	engine = lrg_engine_get_default ();
	if (engine == NULL)
	{
		g_test_skip ("No default engine");
		return;
	}

	/* The registry is created during startup (headless-safe: graphics
	 * subsystems are window-guarded). */
	lrg_engine_startup (engine, &error);
	g_assert_no_error (error);

	registry = lrg_engine_get_registry (engine);
	if (registry == NULL)
	{
		g_test_skip ("No registry");
		return;
	}

	lrg_registry_register (registry, "test-comp", TEST_TYPE_COMP);

	level = lrg_level_new ("inst");
	node = lrg_node_new ("entity");
	lrg_node_set_location_xyz (node, 3.0f, 4.0f, 0.0f);
	desc = lrg_component_desc_new ("test-comp");
	lrg_component_desc_set_int (desc, "value", 42);
	lrg_node_add_component (node, desc);
	lrg_level_add_node (level, node, NULL);
	g_object_unref (node);
	g_object_unref (desc);

	world = lrg_world_new ();
	g_assert_true (lrg_level_instantiate (level, world, engine, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_world_get_object_count (world), ==, 1);

	objects = lrg_world_get_objects (world);
	g_assert_nonnull (objects);
	go = objects->data;
	comp = lrg_game_object_get_component (go, TEST_TYPE_COMP);
	g_assert_nonnull (comp);
	g_object_get (comp, "value", &value, NULL);
	g_assert_cmpint (value, ==, 42);

	g_list_free (objects);  /* transfer-container list from lrg_world_get_objects */
}

/* ==========================================================================
 * LrgEditor core
 * ========================================================================== */

static void
test_editor_basic (void)
{
	g_autoptr(LrgEditor) editor = lrg_editor_new ();

	g_assert_nonnull (editor);
	g_assert_nonnull (lrg_editor_get_level (editor));
	g_assert_nonnull (lrg_editor_get_selection (editor));
	g_assert_false (lrg_editor_can_undo (editor));
	g_assert_false (lrg_editor_can_redo (editor));
	g_assert_cmpint (lrg_editor_get_tool (editor), ==, LRG_EDITOR_TOOL_SELECT);

	lrg_editor_set_tool (editor, LRG_EDITOR_TOOL_ROTATE);
	g_assert_cmpint (lrg_editor_get_tool (editor), ==, LRG_EDITOR_TOOL_ROTATE);

	lrg_editor_set_snap (editor, 0.5, 0.1, 0.25);
	{
		gdouble t = 0, r = 0, s = 0;
		lrg_editor_get_snap (editor, &t, &r, &s);
		g_assert_cmpfloat (t, ==, 0.5);
		g_assert_cmpfloat (r, ==, 0.1);
		g_assert_cmpfloat (s, ==, 0.25);
	}
}

static void
test_editor_add_undo_redo (void)
{
	g_autoptr(LrgEditor) editor = lrg_editor_new ();
	g_autoptr(LrgNode)   node = lrg_node_new ("n");
	LrgNode             *root = lrg_level_get_root (lrg_editor_get_level (editor));

	lrg_editor_add_node (editor, node, NULL);
	g_assert_cmpuint (lrg_node_get_n_children (root), ==, 1);
	g_assert_true (lrg_editor_selection_get_primary (lrg_editor_get_selection (editor)) == node);
	g_assert_true (lrg_editor_can_undo (editor));
	g_assert_false (lrg_editor_can_redo (editor));

	lrg_editor_undo (editor);
	g_assert_cmpuint (lrg_node_get_n_children (root), ==, 0);
	g_assert_false (lrg_editor_can_undo (editor));
	g_assert_true (lrg_editor_can_redo (editor));

	lrg_editor_redo (editor);
	g_assert_cmpuint (lrg_node_get_n_children (root), ==, 1);
	g_assert_true (lrg_editor_can_undo (editor));
}

static void
test_editor_delete_undo (void)
{
	g_autoptr(LrgEditor) editor = lrg_editor_new ();
	g_autoptr(LrgNode)   node = lrg_node_new ("n");
	LrgNode             *root = lrg_level_get_root (lrg_editor_get_level (editor));

	lrg_editor_add_node (editor, node, NULL);
	g_assert_cmpuint (lrg_node_get_n_children (root), ==, 1);

	g_assert_true (lrg_editor_delete_node (editor, node));
	g_assert_cmpuint (lrg_node_get_n_children (root), ==, 0);
	g_assert_false (lrg_editor_selection_contains (lrg_editor_get_selection (editor), node));

	lrg_editor_undo (editor);   /* undo delete */
	g_assert_cmpuint (lrg_node_get_n_children (root), ==, 1);
}

static void
test_editor_transform_undo (void)
{
	g_autoptr(LrgEditor) editor = lrg_editor_new ();
	g_autoptr(LrgNode)   node = lrg_node_new ("n");
	const gfloat         loc[3] = { 5.0f, 6.0f, 7.0f };
	const gfloat         rot[3] = { 0.0f, 0.0f, 0.0f };
	const gfloat         scl[3] = { 2.0f, 2.0f, 2.0f };

	lrg_editor_add_node (editor, node, NULL);
	lrg_editor_set_node_transform (editor, node, loc, rot, scl);

	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (node)->x, 5.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (node)->x, 2.0f, EPS);

	lrg_editor_undo (editor);   /* undo transform */
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (node)->x, 0.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (node)->x, 1.0f, EPS);
}

static void
test_editor_property_undo (void)
{
	g_autoptr(LrgEditor) editor = lrg_editor_new ();
	g_autoptr(LrgNode)   node = lrg_node_new ("Original");
	GValue               v = G_VALUE_INIT;

	lrg_editor_add_node (editor, node, NULL);

	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "Renamed");
	lrg_editor_set_node_property (editor, node, "name", &v);
	g_value_unset (&v);

	g_assert_cmpstr (lrg_node_get_name (node), ==, "Renamed");

	lrg_editor_undo (editor);
	g_assert_cmpstr (lrg_node_get_name (node), ==, "Original");

	lrg_editor_redo (editor);
	g_assert_cmpstr (lrg_node_get_name (node), ==, "Renamed");
}

static void
test_editor_reparent_undo (void)
{
	g_autoptr(LrgEditor) editor = lrg_editor_new ();
	g_autoptr(LrgNode)   a = lrg_node_new ("a");
	g_autoptr(LrgNode)   b = lrg_node_new ("b");
	LrgNode             *root = lrg_level_get_root (lrg_editor_get_level (editor));

	lrg_editor_add_node (editor, a, NULL);
	lrg_editor_add_node (editor, b, NULL);
	g_assert_cmpuint (lrg_node_get_n_children (root), ==, 2);

	g_assert_true (lrg_editor_reparent_node (editor, b, a));
	g_assert_cmpuint (lrg_node_get_n_children (root), ==, 1);
	g_assert_cmpuint (lrg_node_get_n_children (a), ==, 1);
	g_assert_true (lrg_node_get_parent (b) == a);

	lrg_editor_undo (editor);   /* undo reparent */
	g_assert_cmpuint (lrg_node_get_n_children (root), ==, 2);
	g_assert_true (lrg_node_get_parent (b) == root);
}

static void
test_editor_selection (void)
{
	g_autoptr(LrgEditorSelection) sel = lrg_editor_selection_new ();
	g_autoptr(LrgNode)            a = lrg_node_new ("a");
	g_autoptr(LrgNode)            b = lrg_node_new ("b");

	g_assert_cmpuint (lrg_editor_selection_get_count (sel), ==, 0);
	g_assert_null (lrg_editor_selection_get_primary (sel));

	lrg_editor_selection_set (sel, a);
	g_assert_cmpuint (lrg_editor_selection_get_count (sel), ==, 1);
	g_assert_true (lrg_editor_selection_get_primary (sel) == a);
	g_assert_true (lrg_editor_selection_contains (sel, a));

	lrg_editor_selection_add (sel, b);
	g_assert_cmpuint (lrg_editor_selection_get_count (sel), ==, 2);
	g_assert_true (lrg_editor_selection_get_primary (sel) == b);

	g_assert_true (lrg_editor_selection_remove (sel, b));
	g_assert_cmpuint (lrg_editor_selection_get_count (sel), ==, 1);

	lrg_editor_selection_clear (sel);
	g_assert_cmpuint (lrg_editor_selection_get_count (sel), ==, 0);
}

static void
test_command_merge (void)
{
	g_autoptr(LrgNode)   node = lrg_node_new ("n");
	const gfloat         t0[9] = { 0,0,0, 0,0,0, 1,1,1 };
	const gfloat         t1[9] = { 1,0,0, 0,0,0, 1,1,1 };
	const gfloat         t2[9] = { 2,0,0, 0,0,0, 1,1,1 };
	g_autoptr(LrgEditorCommand) c1 = lrg_editor_command_new_transform (node, t0, t1);
	g_autoptr(LrgEditorCommand) c2 = lrg_editor_command_new_transform (node, t1, t2);
	g_autoptr(LrgNode)   other = lrg_node_new ("other");
	g_autoptr(LrgEditorCommand) c3 = lrg_editor_command_new_transform (other, t0, t1);

	g_assert_cmpint (lrg_editor_command_get_kind (c1), ==, LRG_EDITOR_CMD_TRANSFORM);

	/* Same node merges; different node does not. */
	g_assert_true (lrg_editor_command_merge (c1, c2));
	g_assert_false (lrg_editor_command_merge (c1, c3));

	/* After merging c2 into c1, applying c1 yields c2's "after". */
	lrg_editor_command_apply (c1);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (node)->x, 2.0f, EPS);
}

static void
test_editor_save_load (void)
{
	g_autoptr(LrgEditor) editor = lrg_editor_new ();
	g_autoptr(LrgNode)   node = lrg_node_new ("persisted");
	g_autoptr(LrgEditor) editor2 = lrg_editor_new ();
	g_autoptr(GError)    error = NULL;
	g_autofree gchar    *dir = NULL;
	g_autofree gchar    *path = NULL;

	dir = g_dir_make_tmp ("lrg-editor2-XXXXXX", &error);
	g_assert_no_error (error);
	path = g_build_filename (dir, "x.rlevel", NULL);

	lrg_editor_add_node (editor, node, NULL);
	g_assert_true (lrg_editor_save_level (editor, path, &error));
	g_assert_no_error (error);

	g_assert_true (lrg_editor_load_level (editor2, path, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_node_get_n_children (lrg_level_get_root (lrg_editor_get_level (editor2))),
	                  ==, 1);
	/* loading clears history */
	g_assert_false (lrg_editor_can_undo (editor2));

	g_remove (path);
	g_rmdir (dir);
}

/* ==========================================================================
 * Scripting manager + script component
 * ========================================================================== */

static void
test_scripting_manager (void)
{
	LrgScriptingManager *m = lrg_scripting_manager_get_default ();
	guint                n = 0;
	g_autofree LrgScriptLanguage *avail = NULL;

	g_assert_nonnull (m);
	g_assert_true (m == lrg_scripting_manager_get_default ());   /* singleton */

	g_assert_false (lrg_scripting_manager_is_available (m, LRG_SCRIPT_LANGUAGE_NONE));

	/* Metadata is defined regardless of availability. */
	g_assert_cmpstr (lrg_scripting_manager_get_display_name (m, LRG_SCRIPT_LANGUAGE_LUA), ==, "Lua");
	g_assert_cmpstr (lrg_scripting_manager_get_display_name (m, LRG_SCRIPT_LANGUAGE_CRISPY), ==, "Crispy");
	g_assert_cmpstr (lrg_scripting_manager_get_extension (m, LRG_SCRIPT_LANGUAGE_LUA), ==, "lua");
	g_assert_cmpstr (lrg_scripting_manager_get_extension (m, LRG_SCRIPT_LANGUAGE_CRISPY), ==, "c");

	/* Unavailable / NONE produce no context. */
	g_assert_null (lrg_scripting_manager_create_context (m, LRG_SCRIPT_LANGUAGE_NONE));
	if (!lrg_scripting_manager_is_available (m, LRG_SCRIPT_LANGUAGE_LUA))
		g_assert_null (lrg_scripting_manager_create_context (m, LRG_SCRIPT_LANGUAGE_LUA));

	/* The available array length matches the count. */
	avail = lrg_scripting_manager_get_available (m, &n);
	g_assert_nonnull (avail);
	g_assert_cmpuint (n, ==, lrg_scripting_manager_get_available_count (m));
}

static void
test_script_component (void)
{
	g_autoptr(LrgScriptComponent) comp = lrg_script_component_new (LRG_SCRIPT_LANGUAGE_NONE, NULL);
	LrgGameObject                *go = lrg_game_object_new ();

	g_assert_cmpint (lrg_script_component_get_language (comp), ==, LRG_SCRIPT_LANGUAGE_NONE);
	g_assert_null (lrg_script_component_get_script (comp));

	lrg_script_component_set_language (comp, LRG_SCRIPT_LANGUAGE_LUA);
	lrg_script_component_set_script (comp, "scripts/x.lua");
	g_assert_cmpint (lrg_script_component_get_language (comp), ==, LRG_SCRIPT_LANGUAGE_LUA);
	g_assert_cmpstr (lrg_script_component_get_script (comp), ==, "scripts/x.lua");

	/* Attaching with an unavailable language is inert, not fatal. */
	lrg_game_object_add_component (go, LRG_COMPONENT (comp));
	lrg_component_update (LRG_COMPONENT (comp), 0.016f);
	if (!lrg_scripting_manager_is_available (lrg_scripting_manager_get_default (),
	                                         LRG_SCRIPT_LANGUAGE_LUA))
		g_assert_null (lrg_script_component_get_context (comp));

	g_object_unref (go);   /* detaches without crashing */
}

/* ==========================================================================
 * Editor host (software host)
 * ========================================================================== */

static void
test_editor_host (void)
{
	g_autoptr(LrgEditorSoftwareHost) host = lrg_editor_software_host_new (NULL);
	LrgEditorHost                   *h = LRG_EDITOR_HOST (host);
	gint                             w = 0, hh = 0;

	g_assert_true (LRG_IS_EDITOR_HOST (h));

	lrg_editor_software_host_set_render_size (host, 1280, 720);
	lrg_editor_host_get_render_size (h, &w, &hh);
	g_assert_cmpint (w, ==, 1280);
	g_assert_cmpint (hh, ==, 720);

	lrg_editor_software_host_set_frame_delta (host, 0.016);
	g_assert_cmpfloat_with_epsilon (lrg_editor_host_get_frame_delta (h), 0.016, EPS);

	lrg_editor_software_host_set_2d_mode (host, TRUE);
	g_assert_true (lrg_editor_host_is_2d_mode (h));

	g_assert_null (lrg_editor_host_get_engine (h));
	g_assert_null (lrg_editor_host_get_viewport_camera (h));
	g_assert_null (lrg_editor_host_get_input_source (h));

	g_assert_cmpuint (lrg_editor_software_host_get_redraw_count (host), ==, 0);
	lrg_editor_host_request_redraw (h);
	lrg_editor_host_request_redraw (h);
	g_assert_cmpuint (lrg_editor_software_host_get_redraw_count (host), ==, 2);

	/* Frame bracket no-ops must not crash. */
	lrg_editor_host_begin_frame (h);
	lrg_editor_host_clear (h, NULL);
	lrg_editor_host_end_frame (h);
}

/* ==========================================================================
 * Asset database + project
 * ========================================================================== */

static void
test_asset_classify (void)
{
	g_assert_cmpint (lrg_asset_database_classify ("a.png"), ==, LRG_ASSET_TYPE_TEXTURE);
	g_assert_cmpint (lrg_asset_database_classify ("dir/B.PNG"), ==, LRG_ASSET_TYPE_TEXTURE);
	g_assert_cmpint (lrg_asset_database_classify ("m.gltf"), ==, LRG_ASSET_TYPE_MODEL);
	g_assert_cmpint (lrg_asset_database_classify ("m.obj"), ==, LRG_ASSET_TYPE_MODEL);
	g_assert_cmpint (lrg_asset_database_classify ("s.wav"), ==, LRG_ASSET_TYPE_AUDIO);
	g_assert_cmpint (lrg_asset_database_classify ("f.ttf"), ==, LRG_ASSET_TYPE_FONT);
	g_assert_cmpint (lrg_asset_database_classify ("x.lua"), ==, LRG_ASSET_TYPE_SCRIPT);
	g_assert_cmpint (lrg_asset_database_classify ("l.rlevel"), ==, LRG_ASSET_TYPE_LEVEL);
	g_assert_cmpint (lrg_asset_database_classify ("p.rprefab"), ==, LRG_ASSET_TYPE_PREFAB);
	g_assert_cmpint (lrg_asset_database_classify ("t.tsx"), ==, LRG_ASSET_TYPE_TILESET);
	g_assert_cmpint (lrg_asset_database_classify ("u.xyz"), ==, LRG_ASSET_TYPE_UNKNOWN);
}

static void
test_asset_database_scan (void)
{
	g_autoptr(LrgAssetDatabase) db = lrg_asset_database_new ();
	g_autoptr(GError)           error = NULL;
	g_autofree gchar           *dir = NULL;
	g_autofree gchar           *assets = NULL;
	g_autofree gchar           *sprites = NULL;
	g_autofree gchar           *png = NULL;
	g_autofree gchar           *gltf = NULL;

	dir = g_dir_make_tmp ("lrg-assets-XXXXXX", &error);
	g_assert_no_error (error);
	assets = g_build_filename (dir, "assets", NULL);
	sprites = g_build_filename (assets, "sprites", NULL);
	g_assert_cmpint (g_mkdir_with_parents (sprites, 0755), ==, 0);

	png = g_build_filename (sprites, "p.png", NULL);
	gltf = g_build_filename (assets, "m.gltf", NULL);
	g_file_set_contents (png, "x", 1, NULL);
	g_file_set_contents (gltf, "x", 1, NULL);

	lrg_asset_database_add_search_dir (db, assets);
	g_assert_true (lrg_asset_database_scan (db, &error));
	g_assert_no_error (error);

	g_assert_cmpuint (lrg_asset_database_get_count (db), ==, 2);
	g_assert_cmpuint (lrg_asset_database_count_of_type (db, LRG_ASSET_TYPE_TEXTURE), ==, 1);
	g_assert_cmpuint (lrg_asset_database_count_of_type (db, LRG_ASSET_TYPE_MODEL), ==, 1);

	/* The nested texture must have a relative path and a guid. */
	{
		guint    i;
		gboolean found = FALSE;

		for (i = 0; i < lrg_asset_database_get_count (db); i++)
		{
			LrgAssetEntry *e = lrg_asset_database_get_entry (db, i);

			if (lrg_asset_entry_get_asset_type (e) == LRG_ASSET_TYPE_TEXTURE)
			{
				g_assert_cmpstr (lrg_asset_entry_get_path (e), ==,
				                 "sprites" G_DIR_SEPARATOR_S "p.png");
				g_assert_cmpstr (lrg_asset_entry_get_name (e), ==, "p.png");
				g_assert_nonnull (lrg_asset_entry_get_guid (e));
				found = TRUE;
			}
		}
		g_assert_true (found);
	}

	g_remove (png);
	g_remove (gltf);
	g_rmdir (sprites);
	g_rmdir (assets);
	g_rmdir (dir);
}

static void
test_project_roundtrip (void)
{
	g_autoptr(GError)     error = NULL;
	g_autofree gchar     *dir = NULL;
	g_autofree gchar     *manifest = NULL;
	g_autoptr(LrgProject) proj = NULL;
	g_autoptr(LrgProject) loaded = NULL;
	g_autoptr(LrgAssetDatabase) db = NULL;

	dir = g_dir_make_tmp ("lrg-proj-XXXXXX", &error);
	g_assert_no_error (error);

	proj = lrg_project_new (dir, "My Game");
	lrg_project_set_default_level (proj, "scenes/main.rlevel");
	lrg_project_set_game_output (proj, "build/game.so");
	lrg_project_add_asset_dir (proj, "assets");
	g_assert_true (lrg_project_save (proj, &error));
	g_assert_no_error (error);

	manifest = g_build_filename (dir, LRG_PROJECT_MANIFEST, NULL);
	g_assert_true (g_file_test (manifest, G_FILE_TEST_EXISTS));

	loaded = lrg_project_open (dir, &error);
	g_assert_no_error (error);
	g_assert_nonnull (loaded);
	g_assert_cmpstr (lrg_project_get_name (loaded), ==, "My Game");
	g_assert_cmpstr (lrg_project_get_default_level (loaded), ==, "scenes/main.rlevel");
	g_assert_cmpstr (lrg_project_get_game_output (loaded), ==, "build/game.so");
	g_assert_cmpuint (lrg_project_get_asset_dirs (loaded)->len, ==, 1);
	g_assert_cmpstr (g_ptr_array_index (lrg_project_get_asset_dirs (loaded), 0), ==, "assets");

	/* The asset database is created over the (resolved) asset dirs. */
	db = lrg_project_create_asset_database (loaded);
	g_assert_nonnull (db);
	g_assert_true (lrg_asset_database_scan (db, &error));   /* dir is empty -> 0 */
	g_assert_cmpuint (lrg_asset_database_get_count (db), ==, 0);

	g_remove (manifest);
	g_rmdir (dir);
}

/* ==========================================================================
 * Prefabs
 * ========================================================================== */

static void
test_prefab_clone (void)
{
	g_autoptr(LrgNode)         node = lrg_node_new ("Door");
	g_autoptr(LrgNodeVisual)   visual = lrg_node_visual_new (LRG_NODE_VISUAL_PRIMITIVE);
	g_autoptr(LrgComponentDesc) desc = lrg_component_desc_new ("openable");
	LrgNode                   *child = lrg_node_new ("handle");
	g_autoptr(LrgNode)         clone = NULL;
	LrgNode                   *clone_child;

	lrg_node_visual_set_primitive (visual, LRG_PRIMITIVE_CUBE);
	lrg_node_set_visual (node, visual);
	lrg_node_set_location_xyz (node, 1.0f, 2.0f, 3.0f);
	lrg_component_desc_set_boolean (desc, "locked", TRUE);
	lrg_node_add_component (node, desc);
	lrg_node_add_child (node, child);
	g_object_unref (child);

	clone = lrg_prefab_clone (node);
	g_assert_nonnull (clone);
	g_assert_cmpstr (lrg_node_get_name (clone), ==, "Door");
	/* fresh guid */
	g_assert_cmpstr (lrg_node_get_guid (clone), !=, lrg_node_get_guid (node));
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (clone)->x, 1.0f, EPS);
	g_assert_nonnull (lrg_node_get_visual (clone));
	g_assert_cmpint (lrg_node_visual_get_primitive (lrg_node_get_visual (clone)), ==, LRG_PRIMITIVE_CUBE);
	g_assert_cmpuint (lrg_node_get_components (clone)->len, ==, 1);
	g_assert_cmpuint (lrg_node_get_n_children (clone), ==, 1);

	clone_child = g_ptr_array_index (lrg_node_get_children (clone), 0);
	g_assert_cmpstr (lrg_node_get_name (clone_child), ==, "handle");
	g_assert_cmpstr (lrg_node_get_guid (clone_child), !=, lrg_node_get_guid (child));
}

static void
test_prefab_save_load (void)
{
	g_autoptr(LrgNode) node = lrg_node_new ("Crate");
	g_autoptr(LrgNodeVisual) visual = lrg_node_visual_new (LRG_NODE_VISUAL_MESH_ASSET);
	g_autoptr(LrgNode) loaded = NULL;
	g_autoptr(GError)  error = NULL;
	g_autofree gchar  *dir = NULL;
	g_autofree gchar  *path = NULL;

	lrg_node_visual_set_asset (visual, "models/crate.glb");
	lrg_node_set_visual (node, visual);

	dir = g_dir_make_tmp ("lrg-prefab-XXXXXX", &error);
	g_assert_no_error (error);
	path = g_build_filename (dir, "crate.rprefab", NULL);

	g_assert_true (lrg_prefab_save (node, path, &error));
	g_assert_no_error (error);

	loaded = lrg_prefab_load (path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (loaded);
	g_assert_cmpstr (lrg_node_get_name (loaded), ==, "Crate");
	g_assert_null (lrg_node_get_parent (loaded));
	g_assert_cmpint (lrg_node_visual_get_kind (lrg_node_get_visual (loaded)), ==, LRG_NODE_VISUAL_MESH_ASSET);
	g_assert_cmpstr (lrg_node_visual_get_asset (lrg_node_get_visual (loaded)), ==, "models/crate.glb");

	g_remove (path);
	g_rmdir (dir);
}

static LrgNode *
make_visual_node (const gchar *name, LrgNodeVisualKind kind)
{
	LrgNode       *n = lrg_node_new (name);
	LrgNodeVisual *v = lrg_node_visual_new (kind);

	lrg_node_set_visual (n, v);
	g_object_unref (v);
	return n;
}

static void
test_visual_kinds_roundtrip (void)
{
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(LrgLevel)           level = lrg_level_new ("kinds");
	g_autoptr(LrgLevel)           loaded = NULL;
	g_autofree gchar             *data = NULL;
	g_autoptr(GError)             error = NULL;
	gsize                         len = 0;
	LrgNode                      *light = make_visual_node ("L", LRG_NODE_VISUAL_LIGHT);
	LrgNode                      *cam = make_visual_node ("C", LRG_NODE_VISUAL_CAMERA);
	LrgNode                      *audio = make_visual_node ("A", LRG_NODE_VISUAL_AUDIO_EMITTER);
	GPtrArray                    *kids;

	lrg_node_visual_set_param_double (lrg_node_get_visual (light), "intensity", 2.0);
	lrg_node_visual_set_param_double (lrg_node_get_visual (cam), "fov", 60.0);
	lrg_node_visual_set_asset (lrg_node_get_visual (audio), "snd/amb.ogg");
	lrg_node_visual_set_param_double (lrg_node_get_visual (audio), "volume", 0.8);

	lrg_level_add_node (level, light, NULL);
	lrg_level_add_node (level, cam, NULL);
	lrg_level_add_node (level, audio, NULL);
	g_object_unref (light);
	g_object_unref (cam);
	g_object_unref (audio);

	data = lrg_level_serializer_save_to_data (ser, level, &len);
	loaded = lrg_level_serializer_load_from_data (ser, data, (gssize) len, &error);
	g_assert_no_error (error);
	g_assert_nonnull (loaded);

	kids = lrg_node_get_children (lrg_level_get_root (loaded));
	g_assert_cmpuint (kids->len, ==, 3);
	g_assert_cmpint (lrg_node_visual_get_kind (lrg_node_get_visual (g_ptr_array_index (kids, 0))), ==, LRG_NODE_VISUAL_LIGHT);
	g_assert_cmpfloat_with_epsilon (lrg_node_visual_get_param_double (lrg_node_get_visual (g_ptr_array_index (kids, 0)), "intensity", 0.0), 2.0, EPS);
	g_assert_cmpint (lrg_node_visual_get_kind (lrg_node_get_visual (g_ptr_array_index (kids, 1))), ==, LRG_NODE_VISUAL_CAMERA);
	g_assert_cmpfloat_with_epsilon (lrg_node_visual_get_param_double (lrg_node_get_visual (g_ptr_array_index (kids, 1)), "fov", 0.0), 60.0, EPS);
	g_assert_cmpint (lrg_node_visual_get_kind (lrg_node_get_visual (g_ptr_array_index (kids, 2))), ==, LRG_NODE_VISUAL_AUDIO_EMITTER);
	g_assert_cmpstr (lrg_node_visual_get_asset (lrg_node_get_visual (g_ptr_array_index (kids, 2))), ==, "snd/amb.ogg");
}

/* ==========================================================================
 * Edge cases
 * ========================================================================== */

static void
test_edge_deep_nesting_roundtrip (void)
{
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(LrgLevel)           level = lrg_level_new ("deep");
	g_autoptr(LrgLevel)           loaded = NULL;
	g_autofree gchar             *data = NULL;
	g_autoptr(GError)             error = NULL;
	gsize                         len = 0;
	LrgNode                      *parent = NULL;
	LrgNode                      *cursor;
	guint                         depth;

	/* Build a 6-deep chain. */
	for (depth = 0; depth < 6; depth++)
	{
		g_autofree gchar *name = g_strdup_printf ("n%u", depth);
		LrgNode          *n = lrg_node_new (name);

		lrg_level_add_node (level, n, parent);
		parent = n;
		g_object_unref (n);
	}

	data = lrg_level_serializer_save_to_data (ser, level, &len);
	loaded = lrg_level_serializer_load_from_data (ser, data, (gssize) len, &error);
	g_assert_no_error (error);

	/* Walk down the loaded chain and confirm depth + names. */
	cursor = lrg_level_get_root (loaded);
	for (depth = 0; depth < 6; depth++)
	{
		g_autofree gchar *expect = g_strdup_printf ("n%u", depth);

		g_assert_cmpuint (lrg_node_get_n_children (cursor), ==, 1);
		cursor = g_ptr_array_index (lrg_node_get_children (cursor), 0);
		g_assert_cmpstr (lrg_node_get_name (cursor), ==, expect);
	}
	g_assert_cmpuint (lrg_node_get_n_children (cursor), ==, 0);
}

static void
test_edge_no_visual_and_unicode (void)
{
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(LrgLevel)           level = lrg_level_new ("uni");
	g_autoptr(LrgLevel)           loaded = NULL;
	g_autofree gchar             *data = NULL;
	g_autoptr(GError)             error = NULL;
	gsize                         len = 0;
	LrgNode                      *n = lrg_node_new ("naïve—ノード★");
	LrgNode                      *out;

	/* No visual set. */
	lrg_level_add_node (level, n, NULL);
	g_object_unref (n);

	data = lrg_level_serializer_save_to_data (ser, level, &len);
	loaded = lrg_level_serializer_load_from_data (ser, data, (gssize) len, &error);
	g_assert_no_error (error);

	out = g_ptr_array_index (lrg_node_get_children (lrg_level_get_root (loaded)), 0);
	g_assert_cmpstr (lrg_node_get_name (out), ==, "naïve—ノード★");
	g_assert_null (lrg_node_get_visual (out));
}

static void
test_edge_editor_noops (void)
{
	g_autoptr(LrgEditor) editor = lrg_editor_new ();
	g_autoptr(LrgNode)   orphan = lrg_node_new ("orphan");
	g_autoptr(LrgNode)   a = lrg_node_new ("a");

	/* Undo/redo on empty stacks must be safe no-ops. */
	g_assert_false (lrg_editor_can_undo (editor));
	lrg_editor_undo (editor);
	lrg_editor_redo (editor);
	g_assert_false (lrg_editor_can_undo (editor));
	g_assert_false (lrg_editor_can_redo (editor));

	/* Deleting a node with no parent fails gracefully. */
	g_assert_false (lrg_editor_delete_node (editor, orphan));

	/* Reparent to the same parent is rejected. */
	lrg_editor_add_node (editor, a, NULL);
	g_assert_false (lrg_editor_reparent_node (editor, a,
	                                          lrg_level_get_root (lrg_editor_get_level (editor))));

	/* select(NULL) clears. */
	lrg_editor_select (editor, NULL, FALSE);
	g_assert_cmpuint (lrg_editor_selection_get_count (lrg_editor_get_selection (editor)), ==, 0);
}

static void
test_edge_redo_invalidation (void)
{
	g_autoptr(LrgEditor) editor = lrg_editor_new ();
	g_autoptr(LrgNode)   a = lrg_node_new ("a");
	g_autoptr(LrgNode)   b = lrg_node_new ("b");

	lrg_editor_add_node (editor, a, NULL);
	lrg_editor_undo (editor);
	g_assert_true (lrg_editor_can_redo (editor));

	/* A fresh op must clear the redo stack. */
	lrg_editor_add_node (editor, b, NULL);
	g_assert_false (lrg_editor_can_redo (editor));
}

static void
test_edge_asset_missing_dir (void)
{
	g_autoptr(LrgAssetDatabase) db = lrg_asset_database_new ();
	g_autoptr(GError)           error = NULL;

	lrg_asset_database_add_search_dir (db, "/no/such/dir/should/exist");
	g_assert_true (lrg_asset_database_scan (db, &error));   /* tolerant */
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_asset_database_get_count (db), ==, 0);
}

static void
test_edge_prefab_independent_instances (void)
{
	g_autoptr(LrgNode) proto = lrg_node_new ("proto");
	g_autoptr(LrgNode) i1 = NULL;
	g_autoptr(LrgNode) i2 = NULL;

	i1 = lrg_prefab_instantiate (proto);
	i2 = lrg_prefab_instantiate (proto);

	/* Independent copies with distinct guids. */
	g_assert_cmpstr (lrg_node_get_guid (i1), !=, lrg_node_get_guid (i2));
	g_assert_cmpstr (lrg_node_get_guid (i1), !=, lrg_node_get_guid (proto));

	lrg_node_set_location_xyz (i1, 9.0f, 0.0f, 0.0f);
	g_assert_cmpfloat (lrg_node_get_location (i2)->x, ==, 0.0f);   /* unaffected */
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/editor/component-desc", test_component_desc);
	g_test_add_func ("/editor/script-binding", test_script_binding);
	g_test_add_func ("/editor/node-visual", test_node_visual);
	g_test_add_func ("/editor/node/basic", test_node_basic);
	g_test_add_func ("/editor/node/hierarchy", test_node_hierarchy);
	g_test_add_func ("/editor/node/payloads", test_node_payloads);
	g_test_add_func ("/editor/level", test_level);
	g_test_add_func ("/editor/serializer/roundtrip", test_serializer_roundtrip);
	g_test_add_func ("/editor/serializer/empty", test_serializer_empty);
	g_test_add_func ("/editor/serializer/file", test_serializer_file);
	g_test_add_func ("/editor/serializer/invalid", test_serializer_invalid);
	g_test_add_func ("/editor/import/scene", test_scene_import);
	g_test_add_func ("/editor/bake/scene", test_bake_to_scene);
	g_test_add_func ("/editor/instantiate/world", test_instantiate);

	g_test_add_func ("/editor/editor/basic", test_editor_basic);
	g_test_add_func ("/editor/editor/add-undo-redo", test_editor_add_undo_redo);
	g_test_add_func ("/editor/editor/delete-undo", test_editor_delete_undo);
	g_test_add_func ("/editor/editor/transform-undo", test_editor_transform_undo);
	g_test_add_func ("/editor/editor/property-undo", test_editor_property_undo);
	g_test_add_func ("/editor/editor/reparent-undo", test_editor_reparent_undo);
	g_test_add_func ("/editor/editor/selection", test_editor_selection);
	g_test_add_func ("/editor/editor/command-merge", test_command_merge);
	g_test_add_func ("/editor/editor/save-load", test_editor_save_load);

	g_test_add_func ("/editor/scripting/manager", test_scripting_manager);
	g_test_add_func ("/editor/scripting/component", test_script_component);

	g_test_add_func ("/editor/host/software", test_editor_host);

	g_test_add_func ("/editor/assets/classify", test_asset_classify);
	g_test_add_func ("/editor/assets/scan", test_asset_database_scan);
	g_test_add_func ("/editor/project/roundtrip", test_project_roundtrip);

	g_test_add_func ("/editor/prefab/clone", test_prefab_clone);
	g_test_add_func ("/editor/prefab/save-load", test_prefab_save_load);
	g_test_add_func ("/editor/visual-kinds/roundtrip", test_visual_kinds_roundtrip);

	g_test_add_func ("/editor/edge/deep-nesting", test_edge_deep_nesting_roundtrip);
	g_test_add_func ("/editor/edge/no-visual-unicode", test_edge_no_visual_and_unicode);
	g_test_add_func ("/editor/edge/editor-noops", test_edge_editor_noops);
	g_test_add_func ("/editor/edge/redo-invalidation", test_edge_redo_invalidation);
	g_test_add_func ("/editor/edge/asset-missing-dir", test_edge_asset_missing_dir);
	g_test_add_func ("/editor/edge/prefab-independent", test_edge_prefab_independent_instances);

	return g_test_run ();
}
