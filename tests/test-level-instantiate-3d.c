/* test-level-instantiate-3d.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tests for lrg_level_instantiate_3d() and friends: the level's
 * node tree becomes an LrgEntity3D tree 1:1 by GUID (names, child order,
 * full local TRS with the authored Euler kept, world matrices equal to
 * lrg_node_get_world_matrix()), every visual kind becomes the right
 * component configured from its params, registry components and script
 * bindings are materialised, LrgNodeClass.instantiate_3d runs for custom
 * node kinds, and lrg_level_visual_store_property() inverts the visual
 * mapping. Only runtime (src/level + src/world3d) API is used, so the
 * suite also builds against a BUILD_EDITOR=0 library.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#define EPS (1e-5f)

/* ==========================================================================
 * A registry component with a float, an enum and a string property
 * ========================================================================== */

#define TEST_TYPE_SPIN (test_spin_get_type ())
G_DECLARE_FINAL_TYPE (TestSpin, test_spin, TEST, SPIN, LrgComponent)

struct _TestSpin
{
	LrgComponent      parent_instance;
	gfloat            speed;
	LrgLight3DKind    axis_kind;   /* any enum will do: exercises nicks */
	gchar            *label;
};

G_DEFINE_FINAL_TYPE (TestSpin, test_spin, LRG_TYPE_COMPONENT)

enum
{
	SPIN_PROP_0,
	SPIN_PROP_SPEED,
	SPIN_PROP_KIND,
	SPIN_PROP_LABEL,
	SPIN_N_PROPS
};

static void
test_spin_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
	TestSpin *self = TEST_SPIN (object);

	switch (prop_id)
	{
	case SPIN_PROP_SPEED: g_value_set_float (value, self->speed); break;
	case SPIN_PROP_KIND:  g_value_set_enum (value, (gint) self->axis_kind); break;
	case SPIN_PROP_LABEL: g_value_set_string (value, self->label); break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); break;
	}
}

static void
test_spin_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
	TestSpin *self = TEST_SPIN (object);

	switch (prop_id)
	{
	case SPIN_PROP_SPEED: self->speed = g_value_get_float (value); break;
	case SPIN_PROP_KIND:  self->axis_kind = (LrgLight3DKind) g_value_get_enum (value); break;
	case SPIN_PROP_LABEL: g_free (self->label); self->label = g_value_dup_string (value); break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); break;
	}
}

static void
test_spin_finalize (GObject *object)
{
	g_free (TEST_SPIN (object)->label);
	G_OBJECT_CLASS (test_spin_parent_class)->finalize (object);
}

static void
test_spin_class_init (TestSpinClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS;

	object_class->get_property = test_spin_get_property;
	object_class->set_property = test_spin_set_property;
	object_class->finalize = test_spin_finalize;

	g_object_class_install_property (object_class, SPIN_PROP_SPEED,
		g_param_spec_float ("speed", "Speed", "Radians per second", -100.0f, 100.0f, 0.0f, rw));
	g_object_class_install_property (object_class, SPIN_PROP_KIND,
		g_param_spec_enum ("kind", "Kind", "An enum", LRG_TYPE_LIGHT3D_KIND, LRG_LIGHT3D_POINT, rw));
	g_object_class_install_property (object_class, SPIN_PROP_LABEL,
		g_param_spec_string ("label", "Label", "A string", NULL, rw));
}

static void
test_spin_init (TestSpin *self)
{
	self->axis_kind = LRG_LIGHT3D_POINT;
}

/* ==========================================================================
 * A custom node kind with an instantiate_3d hook
 * ========================================================================== */

#define TEST_TYPE_MARKER_NODE (test_marker_node_get_type ())
G_DECLARE_FINAL_TYPE (TestMarkerNode, test_marker_node, TEST, MARKER_NODE, LrgNode)

struct _TestMarkerNode
{
	LrgNode  parent_instance;
	guint    calls;
	guint    components_seen;  /* components on the entity at hook time */
	guint    children_seen;    /* child entities at hook time */
	gboolean in_world;
};

G_DEFINE_FINAL_TYPE (TestMarkerNode, test_marker_node, LRG_TYPE_NODE)

static void
test_marker_node_instantiate_3d (LrgNode     *node,
                                 LrgEntity3D *entity,
                                 LrgWorld3D  *world)
{
	TestMarkerNode *self = TEST_MARKER_NODE (node);

	self->calls++;
	self->components_seen = lrg_entity3d_get_n_components (entity);
	self->children_seen = lrg_entity3d_get_n_children (entity);
	self->in_world = lrg_entity3d_get_world (entity) == world;
	lrg_entity3d_add_tag (entity, "marker");
}

static void
test_marker_node_class_init (TestMarkerNodeClass *klass)
{
	LRG_NODE_CLASS (klass)->instantiate_3d = test_marker_node_instantiate_3d;
}

static void
test_marker_node_init (TestMarkerNode *self)
{
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static LrgNode *
add_node (LrgLevel    *level,
          LrgNode     *parent,
          const gchar *name,
          const gchar *guid)
{
	g_autoptr(LrgNode) node = lrg_node_new (name);

	lrg_node_set_guid (node, guid);
	if (parent != NULL)
		lrg_node_add_child (parent, node);
	else
		lrg_level_add_node (level, node, NULL);
	return node;   /* owned by the tree */
}

static LrgNodeVisual *
set_visual (LrgNode           *node,
            LrgNodeVisualKind  kind)
{
	g_autoptr(LrgNodeVisual) visual = lrg_node_visual_new (kind);

	lrg_node_set_visual (node, visual);
	return lrg_node_get_visual (node);
}

static void
set_param_string (LrgNodeVisual *visual,
                  const gchar   *name,
                  const gchar   *text)
{
	GValue v = G_VALUE_INIT;

	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, text);
	lrg_node_visual_set_param (visual, name, &v);
	g_value_unset (&v);
}

static gfloat
get_float (gpointer     object,
           const gchar *property)
{
	GValue v = G_VALUE_INIT;
	GValue f = G_VALUE_INIT;
	gfloat out;

	g_assert_nonnull (g_object_class_find_property (G_OBJECT_GET_CLASS (object), property));
	g_value_init (&v, g_object_class_find_property (G_OBJECT_GET_CLASS (object), property)->value_type);
	g_object_get_property (G_OBJECT (object), property, &v);
	g_value_init (&f, G_TYPE_FLOAT);
	g_assert_true (g_value_transform (&v, &f));
	out = g_value_get_float (&f);
	g_value_unset (&v);
	return out;
}

static void
assert_matrix_equal (const gfloat *a,
                     const gfloat *b)
{
	guint i;

	for (i = 0; i < 16; i++)
		g_assert_cmpfloat_with_epsilon (a[i], b[i], 1e-4f);
}

static LrgEntity3D *
entity_for (LrgWorld3D *world,
            LrgNode    *node)
{
	LrgEntity3D *e = lrg_world3d_find_by_guid (world, lrg_node_get_guid (node));

	g_assert_nonnull (e);
	return e;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* The node tree becomes the entity tree: GUIDs, names, order, TRS. */
static void
test_tree_and_transforms (void)
{
	g_autoptr(LrgLevel)   level = lrg_level_new ("tree");
	g_autoptr(LrgWorld3D) world = lrg_world3d_new ();
	g_autoptr(GError)     error = NULL;
	LrgNode              *group, *a, *b, *leaf, *top2;
	LrgEntity3D          *eg, *ea, *eb, *eleaf;
	LrgNode              *all[5];
	gfloat                m1[16], m2[16], v[3];
	guint                 i;

	group = add_node (level, NULL, "Group", "aaaaaaaa-0000-0000-0000-000000000001");
	a = add_node (level, group, "A", "aaaaaaaa-0000-0000-0000-000000000002");
	b = add_node (level, group, "B", "aaaaaaaa-0000-0000-0000-000000000003");
	leaf = add_node (level, b, "Leaf", "aaaaaaaa-0000-0000-0000-000000000004");
	top2 = add_node (level, NULL, "Second", "aaaaaaaa-0000-0000-0000-000000000005");

	/* Non-trivial nested transforms; one rotation above 2 pi so the
	 * authored Euler is not what a decomposition would give back. */
	lrg_node_set_location_xyz (group, 10.0f, -2.0f, 3.0f);
	lrg_node_set_rotation_xyz (group, 0.3f, 0.7f, -0.2f);
	lrg_node_set_scale_xyz (group, 2.0f, 2.0f, 2.0f);
	lrg_node_set_location_xyz (a, 1.0f, 0.0f, 0.0f);
	lrg_node_set_location_xyz (b, 0.0f, 1.5f, -4.0f);
	lrg_node_set_rotation_xyz (b, 0.0f, 7.0f, 0.0f);
	lrg_node_set_scale_xyz (b, 1.0f, 0.5f, 3.0f);
	lrg_node_set_location_xyz (leaf, 0.25f, 0.5f, 0.75f);
	lrg_node_set_rotation_xyz (leaf, -1.0f, 0.1f, 2.5f);
	lrg_node_set_visible (top2, FALSE);

	g_assert_true (lrg_level_instantiate_3d (level, world, NULL, &error));
	g_assert_no_error (error);

	/* 1:1: five entities (plus nothing for the level root) */
	g_assert_cmpuint (lrg_world3d_get_entity_count (world), ==, 5);
	g_assert_cmpuint (lrg_entity3d_get_n_children (lrg_world3d_get_root (world)), ==, 2);

	eg = entity_for (world, group);
	ea = entity_for (world, a);
	eb = entity_for (world, b);
	eleaf = entity_for (world, leaf);
	g_assert_true (lrg_entity3d_get_child (lrg_world3d_get_root (world), 0) == eg);
	g_assert_true (lrg_entity3d_get_child (lrg_world3d_get_root (world), 1) == entity_for (world, top2));
	g_assert_true (lrg_entity3d_get_parent (ea) == eg);
	g_assert_true (lrg_entity3d_get_child (eg, 0) == ea);
	g_assert_true (lrg_entity3d_get_child (eg, 1) == eb);
	g_assert_true (lrg_entity3d_get_parent (eleaf) == eb);
	g_assert_cmpstr (lrg_entity3d_get_name (eleaf), ==, "Leaf");
	g_assert_cmpstr (lrg_entity3d_get_guid (eleaf), ==, "aaaaaaaa-0000-0000-0000-000000000004");

	/* Flags */
	g_assert_true (lrg_entity3d_get_visible (eg));
	g_assert_false (lrg_entity3d_get_visible (entity_for (world, top2)));
	g_assert_true (lrg_entity3d_get_active (entity_for (world, top2)));

	/* Local TRS, authored Euler exactly */
	lrg_entity3d_get_position (eb, v);
	g_assert_cmpfloat (v[0], ==, 0.0f);
	g_assert_cmpfloat (v[1], ==, 1.5f);
	g_assert_cmpfloat (v[2], ==, -4.0f);
	g_assert_true (lrg_entity3d_has_authored_euler (eb));
	lrg_entity3d_get_rotation_euler (eb, v);
	g_assert_cmpfloat (v[1], ==, 7.0f);
	lrg_entity3d_get_scale (eb, v);
	g_assert_cmpfloat (v[0], ==, 1.0f);
	g_assert_cmpfloat (v[1], ==, 0.5f);
	g_assert_cmpfloat (v[2], ==, 3.0f);

	/* World matrices match the editor's composition for every node */
	all[0] = group; all[1] = a; all[2] = b; all[3] = leaf; all[4] = top2;
	for (i = 0; i < G_N_ELEMENTS (all); i++)
	{
		lrg_node_get_world_matrix (all[i], m1);
		lrg_entity3d_get_world_matrix (entity_for (world, all[i]), m2);
		assert_matrix_equal (m1, m2);
		lrg_node_get_local_matrix (all[i], m1);
		lrg_entity3d_get_local_matrix (entity_for (world, all[i]), m2);
		assert_matrix_equal (m1, m2);
	}

	/* Group nodes carry no components */
	g_assert_cmpuint (lrg_entity3d_get_n_components (eg), ==, 0);
}

/* Every visual kind maps to its component, configured from the params. */
static void
test_visual_kinds (void)
{
	g_autoptr(LrgLevel)      level = lrg_level_new ("visuals");
	g_autoptr(LrgWorld3D)    world = lrg_world3d_new ();
	g_autoptr(GError)        error = NULL;
	g_autoptr(LrgMaterial3D) red = lrg_material3d_new_with_color (1.0f, 0.0f, 0.0f, 1.0f);
	LrgNode                 *n;
	LrgNodeVisual           *vis;
	LrgEntity3D             *e;
	LrgComponent            *c;
	gint                     kind = -1;

	/* primitive */
	n = add_node (level, NULL, "Cube", "bbbbbbbb-0000-0000-0000-000000000001");
	vis = set_visual (n, LRG_NODE_VISUAL_PRIMITIVE);
	lrg_node_visual_set_primitive (vis, LRG_PRIMITIVE_UV_SPHERE);
	lrg_material3d_set_roughness (red, 0.25f);
	lrg_node_visual_set_material (vis, red);
	/* mesh asset */
	n = add_node (level, NULL, "Mesh", "bbbbbbbb-0000-0000-0000-000000000002");
	vis = set_visual (n, LRG_NODE_VISUAL_MESH_ASSET);
	lrg_node_visual_set_asset (vis, "models/rock.obj");
	/* lights */
	n = add_node (level, NULL, "Sun", "bbbbbbbb-0000-0000-0000-000000000003");
	vis = set_visual (n, LRG_NODE_VISUAL_LIGHT);
	set_param_string (vis, "light_type", "directional");
	lrg_node_visual_set_param_double (vis, "intensity", 2.5);
	n = add_node (level, NULL, "Lamp", "bbbbbbbb-0000-0000-0000-000000000004");
	vis = set_visual (n, LRG_NODE_VISUAL_LIGHT);
	lrg_node_visual_set_param_double (vis, "range", 7.0);
	lrg_node_visual_set_material (vis, red);
	n = add_node (level, NULL, "Spot", "bbbbbbbb-0000-0000-0000-000000000005");
	vis = set_visual (n, LRG_NODE_VISUAL_LIGHT);
	lrg_node_visual_set_param_double (vis, "spot_angle", 30.0);
	/* camera */
	n = add_node (level, NULL, "Camera", "bbbbbbbb-0000-0000-0000-000000000006");
	vis = set_visual (n, LRG_NODE_VISUAL_CAMERA);
	lrg_node_visual_set_param_double (vis, "fov", 70.0);
	lrg_node_visual_set_param_double (vis, "near", 0.5);
	lrg_node_visual_set_param_double (vis, "far", 250.0);
	/* audio emitter */
	n = add_node (level, NULL, "Hum", "bbbbbbbb-0000-0000-0000-000000000007");
	vis = set_visual (n, LRG_NODE_VISUAL_AUDIO_EMITTER);
	lrg_node_visual_set_asset (vis, "sounds/hum.wav");
	lrg_node_visual_set_param_double (vis, "radius", 12.0);
	/* sprite */
	n = add_node (level, NULL, "Sprite", "bbbbbbbb-0000-0000-0000-000000000008");
	vis = set_visual (n, LRG_NODE_VISUAL_SPRITE);
	lrg_node_visual_set_asset (vis, "textures/tree.png");
	lrg_node_visual_set_param_double (vis, "pixels_per_unit", 100.0);
	/* tilemap */
	n = add_node (level, NULL, "Tiles", "bbbbbbbb-0000-0000-0000-000000000009");
	vis = set_visual (n, LRG_NODE_VISUAL_TILEMAP);
	lrg_node_visual_set_asset (vis, "textures/tiles.png");
	lrg_node_visual_set_param_double (vis, "tile_size", 2.0);
	{
		g_autoptr(LrgNodeTilemap) grid = lrg_node_tilemap_new (4, 3);

		lrg_node_tilemap_set_tile (grid, 0, 1, 2, 5);
		lrg_node_visual_set_tilemap (vis, grid);
	}
	/* none and prefab instance: no component */
	n = add_node (level, NULL, "Empty", "bbbbbbbb-0000-0000-0000-00000000000a");
	set_visual (n, LRG_NODE_VISUAL_NONE);
	n = add_node (level, NULL, "Prefab", "bbbbbbbb-0000-0000-0000-00000000000b");
	set_visual (n, LRG_NODE_VISUAL_PREFAB_INSTANCE);

	g_assert_true (lrg_level_instantiate_3d (level, world, NULL, &error));
	g_assert_no_error (error);

	/* primitive -> mesh renderer with primitive and material */
	e = lrg_world3d_find_by_guid (world, "bbbbbbbb-0000-0000-0000-000000000001");
	c = lrg_entity3d_get_component (e, LRG_TYPE_MESH_RENDERER_COMPONENT);
	g_assert_nonnull (c);
	g_assert_cmpint (lrg_mesh_renderer_component_get_primitive (LRG_MESH_RENDERER_COMPONENT (c)), ==,
	                 LRG_PRIMITIVE_UV_SPHERE);
	{
		gfloat rgba[4];

		lrg_mesh_renderer_component_get_color (LRG_MESH_RENDERER_COMPONENT (c), rgba);
		g_assert_cmpfloat (rgba[0], ==, 1.0f);
		g_assert_cmpfloat (rgba[1], ==, 0.0f);
		g_assert_cmpfloat_with_epsilon (get_float (c, "roughness"), 0.25f, EPS);
	}

	/* mesh asset */
	e = lrg_world3d_find_by_guid (world, "bbbbbbbb-0000-0000-0000-000000000002");
	c = lrg_entity3d_get_component (e, LRG_TYPE_MESH_RENDERER_COMPONENT);
	g_assert_nonnull (c);
	{
		gfloat rgba[4];

		/* no material: the editor's grey, not the renderer's white */
		lrg_mesh_renderer_component_get_color (LRG_MESH_RENDERER_COMPONENT (c), rgba);
		g_assert_cmpfloat_with_epsilon (rgba[0], 0.75f, EPS);
		g_assert_cmpfloat_with_epsilon (rgba[2], 0.78f, EPS);
	}
	g_assert_cmpstr (lrg_mesh_renderer_component_get_mesh_asset (LRG_MESH_RENDERER_COMPONENT (c)), ==,
	                 "models/rock.obj");

	/* lights: light_type param, the default point light, spot from the angle */
	e = lrg_world3d_find_by_guid (world, "bbbbbbbb-0000-0000-0000-000000000003");
	c = lrg_entity3d_get_component (e, LRG_TYPE_LIGHT_COMPONENT);
	g_assert_nonnull (c);
	g_assert_cmpint (lrg_light_component_get_kind (LRG_LIGHT_COMPONENT (c)), ==, LRG_LIGHT3D_DIRECTIONAL);
	g_assert_cmpfloat_with_epsilon (lrg_light_component_get_intensity (LRG_LIGHT_COMPONENT (c)), 2.5f, EPS);
	e = lrg_world3d_find_by_guid (world, "bbbbbbbb-0000-0000-0000-000000000004");
	c = lrg_entity3d_get_component (e, LRG_TYPE_LIGHT_COMPONENT);
	g_assert_cmpint (lrg_light_component_get_kind (LRG_LIGHT_COMPONENT (c)), ==, LRG_LIGHT3D_POINT);
	g_assert_cmpfloat_with_epsilon (lrg_light_component_get_range (LRG_LIGHT_COMPONENT (c)), 7.0f, EPS);
	g_assert_cmpfloat_with_epsilon (get_float (c, "color-g"), 0.0f, EPS);
	e = lrg_world3d_find_by_guid (world, "bbbbbbbb-0000-0000-0000-000000000005");
	c = lrg_entity3d_get_component (e, LRG_TYPE_LIGHT_COMPONENT);
	g_object_get (c, "kind", &kind, NULL);
	g_assert_cmpint (kind, ==, LRG_LIGHT3D_SPOT);
	/* spot_angle is the full cone; outer-angle a half-angle */
	g_assert_cmpfloat_with_epsilon (get_float (c, "outer-angle"), 15.0f, EPS);

	/* camera */
	e = lrg_world3d_find_by_guid (world, "bbbbbbbb-0000-0000-0000-000000000006");
	c = lrg_entity3d_get_component (e, LRG_TYPE_CAMERA_COMPONENT);
	g_assert_nonnull (c);
	g_assert_cmpfloat_with_epsilon (lrg_camera_component_get_fov (LRG_CAMERA_COMPONENT (c)), 70.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_camera_component_get_near (LRG_CAMERA_COMPONENT (c)), 0.5f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_camera_component_get_far (LRG_CAMERA_COMPONENT (c)), 250.0f, EPS);

	/* audio emitter */
	e = lrg_world3d_find_by_guid (world, "bbbbbbbb-0000-0000-0000-000000000007");
	c = lrg_entity3d_get_component (e, LRG_TYPE_AUDIO_SOURCE_COMPONENT);
	g_assert_nonnull (c);
	g_assert_cmpstr (lrg_audio_source_component_get_sound_path (LRG_AUDIO_SOURCE_COMPONENT (c)), ==,
	                 "sounds/hum.wav");
	g_assert_cmpfloat_with_epsilon (get_float (c, "max-distance"), 12.0f, EPS);

	/* sprite and tilemap */
	e = lrg_world3d_find_by_guid (world, "bbbbbbbb-0000-0000-0000-000000000008");
	g_assert_nonnull (lrg_entity3d_get_component (e, LRG_TYPE_BILLBOARD_COMPONENT));
	{
		g_autofree gchar *tex = NULL;

		g_object_get (lrg_entity3d_get_component (e, LRG_TYPE_BILLBOARD_COMPONENT), "texture-path", &tex, NULL);
		g_assert_cmpstr (tex, ==, "textures/tree.png");
	}
	{
		GObject  *bb = G_OBJECT (lrg_entity3d_get_component (e, LRG_TYPE_BILLBOARD_COMPONENT));
		gint      facing = -1;
		gboolean  keep = FALSE;
		gfloat    ppu_default;

		/* the editor's aspect-fit quad; pixels_per_unit is ignored */
		g_object_get (bb, "facing", &facing, "preserve-aspect", &keep, NULL);
		g_assert_cmpint (facing, ==, LRG_BILLBOARD_FACING_FIXED);
		g_assert_true (keep);
		ppu_default = G_PARAM_SPEC_FLOAT (g_object_class_find_property (G_OBJECT_GET_CLASS (bb),
		                                                                "pixels-per-unit"))->default_value;
		g_assert_cmpfloat_with_epsilon (get_float (bb, "pixels-per-unit"), ppu_default, EPS);
	}
	e = lrg_world3d_find_by_guid (world, "bbbbbbbb-0000-0000-0000-000000000009");
	c = lrg_entity3d_get_component (e, LRG_TYPE_TILEMAP_COMPONENT);
	g_assert_nonnull (c);
	g_assert_cmpfloat_with_epsilon (lrg_tilemap_component_get_tile_size (LRG_TILEMAP_COMPONENT (c)), 2.0f, EPS);
	g_assert_cmpstr (lrg_tilemap_component_get_tileset_path (LRG_TILEMAP_COMPONENT (c)), ==, "textures/tiles.png");
	{
		LrgNodeTilemap *cells = lrg_tilemap_component_get_node_tilemap (LRG_TILEMAP_COMPONENT (c));

		/* a copy of the level's grid, with the painted cell */
		g_assert_nonnull (cells);
		g_assert_true (cells != lrg_node_visual_get_tilemap (vis));
		g_assert_cmpuint (lrg_node_tilemap_get_width (cells), ==, 4);
		g_assert_cmpuint (lrg_node_tilemap_get_tile (cells, 0, 1, 2), ==, 5);
	}

	/* none / prefab instance */
	e = lrg_world3d_find_by_guid (world, "bbbbbbbb-0000-0000-0000-00000000000a");
	g_assert_cmpuint (lrg_entity3d_get_n_components (e), ==, 0);
	e = lrg_world3d_find_by_guid (world, "bbbbbbbb-0000-0000-0000-00000000000b");
	g_assert_cmpuint (lrg_entity3d_get_n_components (e), ==, 0);

	/* The component types are the documented ones */
	g_assert_true (lrg_level_visual_get_component_type (LRG_NODE_VISUAL_SPRITE) == LRG_TYPE_BILLBOARD_COMPONENT);
	g_assert_true (lrg_level_visual_get_component_type (LRG_NODE_VISUAL_NONE) == G_TYPE_INVALID);
	g_assert_true (lrg_level_visual_get_component_type (LRG_NODE_VISUAL_CAD_PART) == G_TYPE_INVALID);
}

/* Registry components and scripts, in order after the visual's. */
static void
test_components_and_scripts (void)
{
	g_autoptr(LrgLevel)          level = lrg_level_new ("comps");
	g_autoptr(LrgWorld3D)        world = lrg_world3d_new ();
	g_autoptr(GError)            error = NULL;
	g_autoptr(LrgComponentDesc)  desc = lrg_component_desc_new ("test-spin");
	g_autoptr(LrgComponentDesc)  unknown = lrg_component_desc_new ("no-such-type");
	g_autoptr(LrgScriptBinding)  on = lrg_script_binding_new (LRG_SCRIPT_LANGUAGE_LUA, "scripts/on.lua");
	g_autoptr(LrgScriptBinding)  off = lrg_script_binding_new (LRG_SCRIPT_LANGUAGE_LUA, "scripts/off.lua");
	LrgNode                     *n;
	LrgEntity3D                 *e;
	GPtrArray                   *comps;
	TestSpin                    *spin;

	n = add_node (level, NULL, "Spinner", "cccccccc-0000-0000-0000-000000000001");
	set_visual (n, LRG_NODE_VISUAL_PRIMITIVE);
	lrg_component_desc_set_double (desc, "speed", 1.5);
	lrg_component_desc_set_string (desc, "kind", "spot");     /* enum nick */
	lrg_component_desc_set_string (desc, "label", "hello");
	lrg_node_add_component (n, desc);
	lrg_node_add_component (n, unknown);                     /* skipped */
	lrg_script_binding_set_enabled (off, FALSE);
	lrg_node_add_script (n, on);
	lrg_node_add_script (n, off);

	g_assert_true (lrg_level_instantiate_3d (level, world, NULL, &error));
	g_assert_no_error (error);

	e = entity_for (world, n);
	comps = lrg_entity3d_get_components (e);
	g_assert_cmpuint (comps->len, ==, 3);
	g_assert_true (LRG_IS_MESH_RENDERER_COMPONENT (g_ptr_array_index (comps, 0)));
	g_assert_true (TEST_IS_SPIN (g_ptr_array_index (comps, 1)));
	g_assert_true (LRG_IS_SCRIPT_COMPONENT (g_ptr_array_index (comps, 2)));

	spin = TEST_SPIN (g_ptr_array_index (comps, 1));
	g_assert_cmpfloat_with_epsilon (spin->speed, 1.5f, EPS);
	g_assert_cmpint (spin->axis_kind, ==, LRG_LIGHT3D_SPOT);
	g_assert_cmpstr (spin->label, ==, "hello");
	g_assert_true (lrg_component_get_entity (LRG_COMPONENT (spin)) == e);
	g_assert_cmpstr (lrg_script_component_get_script (g_ptr_array_index (comps, 2)), ==, "scripts/on.lua");
}

/* LrgNodeClass.instantiate_3d runs after the node's own components, in
 * the world, before the children. */
static void
test_custom_vfunc (void)
{
	g_autoptr(LrgLevel)        level = lrg_level_new ("custom");
	g_autoptr(LrgWorld3D)      world = lrg_world3d_new ();
	g_autoptr(GError)          error = NULL;
	g_autoptr(TestMarkerNode)  marker = g_object_new (TEST_TYPE_MARKER_NODE, "name", "Marker", NULL);
	LrgEntity3D               *e;

	lrg_node_set_guid (LRG_NODE (marker), "dddddddd-0000-0000-0000-000000000001");
	set_visual (LRG_NODE (marker), LRG_NODE_VISUAL_CAMERA);
	lrg_level_add_node (level, LRG_NODE (marker), NULL);
	add_node (level, LRG_NODE (marker), "Child", "dddddddd-0000-0000-0000-000000000002");

	g_assert_true (lrg_level_instantiate_3d (level, world, NULL, &error));
	g_assert_cmpuint (marker->calls, ==, 1);
	g_assert_cmpuint (marker->components_seen, ==, 1);
	g_assert_cmpuint (marker->children_seen, ==, 0);
	g_assert_true (marker->in_world);
	e = lrg_world3d_find_by_guid (world, "dddddddd-0000-0000-0000-000000000001");
	g_assert_true (lrg_entity3d_has_tag (e, "marker"));
	g_assert_cmpuint (lrg_entity3d_get_n_children (e), ==, 1);

	/* A plain node has no hook: the dispatcher is a no-op */
	{
		g_autoptr(LrgNode)     plain = lrg_node_new ("plain");
		g_autoptr(LrgEntity3D) ent = lrg_entity3d_new ();

		lrg_node_instantiate_3d (plain, ent, world);
		g_assert_cmpuint (lrg_entity3d_get_n_components (ent), ==, 0);
	}
}

/* Instantiating twice: the second copy gets fresh GUIDs. */
static void
test_guid_conflicts (void)
{
	g_autoptr(LrgLevel)   level = lrg_level_new ("twice");
	g_autoptr(LrgWorld3D) world = lrg_world3d_new ();
	g_autoptr(GError)     error = NULL;
	LrgNode              *n;
	LrgEntity3D          *first, *second;

	n = add_node (level, NULL, "Thing", "eeeeeeee-0000-0000-0000-000000000001");
	add_node (level, n, "Part", "eeeeeeee-0000-0000-0000-000000000002");

	g_assert_true (lrg_level_instantiate_3d (level, world, NULL, &error));
	g_assert_true (lrg_level_instantiate_3d (level, world, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_world3d_get_entity_count (world), ==, 4);

	first = lrg_entity3d_get_child (lrg_world3d_get_root (world), 0);
	second = lrg_entity3d_get_child (lrg_world3d_get_root (world), 1);
	g_assert_cmpstr (lrg_entity3d_get_guid (first), ==, "eeeeeeee-0000-0000-0000-000000000001");
	g_assert_nonnull (lrg_entity3d_get_guid (second));
	g_assert_cmpstr (lrg_entity3d_get_guid (second), !=, "eeeeeeee-0000-0000-0000-000000000001");
	g_assert_true (lrg_world3d_find_by_guid (world, lrg_entity3d_get_guid (second)) == second);
	g_assert_cmpstr (lrg_entity3d_get_name (second), ==, "Thing");
}

/* lrg_level_instantiate_node_3d() under a parent (runtime prefab spawn). */
static void
test_instantiate_node (void)
{
	g_autoptr(LrgWorld3D)  world = lrg_world3d_new ();
	g_autoptr(LrgEntity3D) holder = lrg_entity3d_new_with_name ("holder");
	g_autoptr(LrgNode)     crate = lrg_node_new ("Crate");
	g_autoptr(LrgNode)     lid = lrg_node_new ("Lid");
	g_autoptr(LrgEntity3D) loose = lrg_entity3d_new ();
	g_autoptr(GError)      error = NULL;
	LrgEntity3D           *e;
	gfloat                 v[3];

	lrg_world3d_add_entity (world, holder);
	lrg_node_set_location_xyz (crate, 4.0f, 0.0f, 0.0f);
	lrg_node_add_child (crate, lid);
	set_visual (crate, LRG_NODE_VISUAL_PRIMITIVE);

	e = lrg_level_instantiate_node_3d (crate, holder, world, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (e);
	g_assert_true (lrg_entity3d_get_parent (e) == holder);
	g_assert_cmpuint (lrg_entity3d_get_n_children (e), ==, 1);
	g_assert_cmpstr (lrg_entity3d_get_guid (e), ==, lrg_node_get_guid (crate));
	lrg_entity3d_get_position (e, v);
	g_assert_cmpfloat (v[0], ==, 4.0f);

	/* A parent outside the world is an error */
	g_assert_null (lrg_level_instantiate_node_3d (crate, loose, world, NULL, &error));
	g_assert_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_FAILED);
}

/* lrg_level_load_world3d() reads a .rlevel and builds the world. */
static void
test_load_world3d (void)
{
	static const gchar *yaml =
		"type: level\n"
		"name: loaded\n"
		"default_view: 3d\n"
		"nodes:\n"
		"  - name: Sun\n"
		"    guid: ffffffff-0000-0000-0000-000000000001\n"
		"    transform:\n"
		"      rotation: [-0.9, 0.6, 0.0]\n"
		"    visual:\n"
		"      kind: light\n"
		"      params:\n"
		"        light_type: directional\n"
		"  - name: Box\n"
		"    guid: ffffffff-0000-0000-0000-000000000002\n"
		"    transform:\n"
		"      location: [1.0, 2.0, 3.0]\n"
		"    visual:\n"
		"      kind: primitive\n"
		"      primitive: cube\n"
		"    components:\n"
		"      - type: test-spin\n"
		"        props:\n"
		"          speed: 2.0\n"
		"    children:\n"
		"      - name: Lid\n"
		"        guid: ffffffff-0000-0000-0000-000000000003\n";
	g_autoptr(LrgWorld3D) world = lrg_world3d_new ();
	g_autoptr(LrgLevel)   level = NULL;
	g_autoptr(GError)     error = NULL;
	g_autofree gchar     *dir = g_dir_make_tmp ("lrg-inst3d-XXXXXX", NULL);
	g_autofree gchar     *path = g_build_filename (dir, "loaded.rlevel", NULL);
	LrgEntity3D          *box;
	LrgComponent         *light;

	g_assert_true (g_file_set_contents (path, yaml, -1, NULL));
	level = lrg_level_load_world3d (path, world, &error);
	g_assert_no_error (error);
	g_assert_nonnull (level);
	g_assert_cmpstr (lrg_level_get_name (level), ==, "loaded");
	g_assert_cmpuint (lrg_world3d_get_entity_count (world), ==, 3);

	box = lrg_world3d_find_by_guid (world, "ffffffff-0000-0000-0000-000000000002");
	g_assert_nonnull (box);
	g_assert_nonnull (lrg_entity3d_get_component (box, TEST_TYPE_SPIN));
	g_assert_nonnull (lrg_entity3d_find_child (box, "Lid", FALSE));
	light = lrg_entity3d_get_component (lrg_world3d_find_by_guid (world, "ffffffff-0000-0000-0000-000000000001"),
	                                    LRG_TYPE_LIGHT_COMPONENT);
	g_assert_cmpint (lrg_light_component_get_kind (LRG_LIGHT_COMPONENT (light)), ==, LRG_LIGHT3D_DIRECTIONAL);

	/* A missing file is an error */
	g_clear_object (&level);
	level = lrg_level_load_world3d ("/nonexistent/x.rlevel", world, &error);
	g_assert_null (level);
	g_assert_nonnull (error);

	g_unlink (path);
	g_rmdir (dir);
}

/* store_property() is the inverse of create_component(). */
static void
test_store_property (void)
{
	g_autoptr(LrgNodeVisual) light = lrg_node_visual_new (LRG_NODE_VISUAL_LIGHT);
	g_autoptr(LrgNodeVisual) prim = lrg_node_visual_new (LRG_NODE_VISUAL_PRIMITIVE);
	g_autoptr(LrgComponent)  comp = NULL;
	g_autoptr(LrgComponent)  again = NULL;
	GValue                   v = G_VALUE_INIT;
	const GValue            *stored;

	/* light: intensity is a param, kind goes to light_type as a nick,
	 * colour into the material */
	comp = lrg_level_visual_create_component (light);
	g_assert_true (LRG_IS_LIGHT_COMPONENT (comp));
	lrg_light_component_set_intensity (LRG_LIGHT_COMPONENT (comp), 4.0f);
	lrg_light_component_set_kind (LRG_LIGHT_COMPONENT (comp), LRG_LIGHT3D_DIRECTIONAL);

	g_value_init (&v, G_TYPE_FLOAT);
	g_object_get_property (G_OBJECT (comp), "intensity", &v);
	g_assert_true (lrg_level_visual_store_property (light, "intensity", &v));
	g_value_unset (&v);
	g_assert_cmpfloat_with_epsilon (lrg_node_visual_get_param_double (light, "intensity", 0.0), 4.0, 1e-6);

	g_value_init (&v, LRG_TYPE_LIGHT3D_KIND);
	g_object_get_property (G_OBJECT (comp), "kind", &v);
	g_assert_true (lrg_level_visual_store_property (light, "kind", &v));
	g_value_unset (&v);
	stored = lrg_node_visual_get_param (light, "light_type");
	g_assert_nonnull (stored);
	g_assert_cmpstr (g_value_get_string (stored), ==, "directional");

	g_value_init (&v, G_TYPE_FLOAT);
	g_value_set_float (&v, 0.5f);
	g_assert_true (lrg_level_visual_store_property (light, "color-g", &v));
	g_value_unset (&v);
	g_assert_nonnull (lrg_node_visual_get_material (light));

	/* outer-angle goes back as the full cone */
	g_value_init (&v, G_TYPE_FLOAT);
	g_value_set_float (&v, 20.0f);
	g_assert_true (lrg_level_visual_store_property (light, "outer-angle", &v));
	g_value_unset (&v);
	g_assert_cmpfloat_with_epsilon (lrg_node_visual_get_param_double (light, "spot_angle", 0.0), 40.0, 1e-5);
	lrg_node_visual_remove_param (light, "spot_angle");

	/* Re-creating gives the same values back */
	again = lrg_level_visual_create_component (light);
	g_assert_cmpfloat_with_epsilon (lrg_light_component_get_intensity (LRG_LIGHT_COMPONENT (again)), 4.0f, EPS);
	g_assert_cmpint (lrg_light_component_get_kind (LRG_LIGHT_COMPONENT (again)), ==, LRG_LIGHT3D_DIRECTIONAL);
	g_assert_cmpfloat_with_epsilon (get_float (again, "color-g"), 0.5f, EPS);

	/* Unknown properties and LrgComponent's own are not stored */
	g_value_init (&v, G_TYPE_BOOLEAN);
	g_value_set_boolean (&v, FALSE);
	g_assert_false (lrg_level_visual_store_property (light, "enabled", &v));
	g_assert_false (lrg_level_visual_store_property (light, "no-such-property", &v));
	g_value_unset (&v);

	/* primitive: the shape */
	g_value_init (&v, LRG_TYPE_PRIMITIVE_TYPE);
	g_value_set_enum (&v, LRG_PRIMITIVE_TORUS);
	g_assert_true (lrg_level_visual_store_property (prim, "primitive", &v));
	g_value_unset (&v);
	g_assert_cmpint (lrg_node_visual_get_primitive (prim), ==, LRG_PRIMITIVE_TORUS);

	/* No component for a group visual */
	{
		g_autoptr(LrgNodeVisual) none = lrg_node_visual_new (LRG_NODE_VISUAL_NONE);

		g_assert_null (lrg_level_visual_create_component (none));
	}
}

/* A level mixer is loaded only when named. A bad file fails and leaves
 * the manager's mixer alone. No name leaves it alone too. */
static void
test_level_audio_mixer (void)
{
	static const gchar *good_mixer =
		"version: 1\n"
		"buses:\n"
		"  - name: sfx\n"
		"    volume: -6\n"
		"  - name: weapons\n"
		"    parent: sfx\n"
		"    volume: -3\n"
		"    sends:\n"
		"      - target: fx\n"
		"        level: -12\n"
		"  - name: fx\n"
		"    volume: 0\n";
	static const gchar *plain =
		"type: level\n"
		"name: plain\n"
		"default_view: 3d\n"
		"nodes: []\n";
	static const gchar *named =
		"type: level\n"
		"name: mixed\n"
		"default_view: 3d\n"
		"audio:\n"
		"  mixer: mix.rmixer\n"
		"nodes: []\n";
	static const gchar *broken_level =
		"type: level\n"
		"name: broken\n"
		"default_view: 3d\n"
		"audio:\n"
		"  mixer: bad.rmixer\n"
		"nodes: []\n";
	static const gchar *missing_level =
		"type: level\n"
		"name: missing\n"
		"default_view: 3d\n"
		"audio:\n"
		"  mixer: no-such.rmixer\n"
		"nodes: []\n";
	static const gchar *bad_block =
		"type: level\n"
		"name: bad-block\n"
		"default_view: 3d\n"
		"audio:\n"
		"  volume: 1\n"
		"nodes: []\n";
	LrgAudioManager          *mgr = lrg_audio_manager_get_default ();
	LrgAudioMixer            *previous = lrg_audio_manager_get_mixer (mgr);
	g_autoptr(LrgAudioMixer)  held = previous != NULL ? g_object_ref (previous) : NULL;
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(LrgLevel)       round = NULL;
	g_autoptr(LrgWorld3D)     world = lrg_world3d_new ();
	g_autoptr(LrgLevel)       level = NULL;
	g_autoptr(GError)         error = NULL;
	g_autofree gchar         *dir = g_dir_make_tmp ("lrg-level-mixer-XXXXXX", NULL);
	g_autofree gchar         *mix_path = NULL;
	g_autofree gchar         *bad_path = NULL;
	g_autofree gchar         *level_path = NULL;
	g_autofree gchar         *round_path = NULL;
	LrgAudioMixer            *current;

	g_assert_nonnull (dir);
	mix_path = g_build_filename (dir, "mix.rmixer", NULL);
	bad_path = g_build_filename (dir, "bad.rmixer", NULL);
	level_path = g_build_filename (dir, "level.rlevel", NULL);
	round_path = g_build_filename (dir, "round.rlevel", NULL);
	g_assert_true (g_file_set_contents (mix_path, good_mixer, -1, &error));
	g_assert_no_error (error);
	g_assert_true (g_file_set_contents (bad_path, "buses: 3\n", -1, &error));
	g_assert_no_error (error);

	/* Saving the path round-trips as audio.mixer. */
	{
		g_autoptr(LrgLevel) src = lrg_level_new ("round");

		lrg_level_set_audio_mixer (src, "mix.rmixer");
		g_assert_true (lrg_level_serializer_save_to_file (ser, src, round_path, &error));
		g_assert_no_error (error);
	}
	round = lrg_level_serializer_load_from_file (ser, round_path, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_level_get_audio_mixer (round), ==, "mix.rmixer");

	/* No mixer named: the manager's mixer stays. */
	g_assert_true (g_file_set_contents (level_path, plain, -1, &error));
	g_assert_no_error (error);
	level = lrg_level_load_world3d (level_path, world, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_audio_manager_get_mixer (mgr) == held);
	g_clear_object (&level);

	/* A good .rmixer replaces it and keeps the fx bus. */
	g_assert_true (g_file_set_contents (level_path, named, -1, &error));
	g_assert_no_error (error);
	level = lrg_level_load_world3d (level_path, world, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_level_get_audio_mixer (level), ==, "mix.rmixer");
	current = lrg_audio_manager_get_mixer (mgr);
	g_assert_nonnull (current);
	g_assert_true (current != held);
	g_assert_true (lrg_audio_mixer_has_bus (current, "fx"));
	g_clear_object (&level);

	/* A parse error does not replace that mixer. */
	g_assert_true (g_file_set_contents (level_path, broken_level, -1, &error));
	g_assert_no_error (error);
	level = lrg_level_load_world3d (level_path, world, &error);
	g_assert_null (level);
	g_assert_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_FAILED);
	g_assert_true (lrg_audio_manager_get_mixer (mgr) == current);
	g_clear_error (&error);

	/* A missing file does not replace it either. */
	g_assert_true (g_file_set_contents (level_path, missing_level, -1, &error));
	g_assert_no_error (error);
	level = lrg_level_load_world3d (level_path, world, &error);
	g_assert_null (level);
	g_assert_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_FAILED);
	g_assert_true (lrg_audio_manager_get_mixer (mgr) == current);
	g_clear_error (&error);

	/* An unknown key fails the load before instantiate. */
	g_assert_true (g_file_set_contents (level_path, bad_block, -1, &error));
	g_assert_no_error (error);
	level = lrg_level_serializer_load_from_file (ser, level_path, &error);
	g_assert_null (level);
	g_assert_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE);
	g_clear_error (&error);

	lrg_audio_manager_set_mixer (mgr, held);
	g_unlink (mix_path);
	g_unlink (bad_path);
	g_unlink (level_path);
	g_unlink (round_path);
	g_rmdir (dir);
}

int
main (int   argc,
      char *argv[])
{
	g_autoptr(GError) error = NULL;
	LrgEngine        *engine;

	g_test_init (&argc, &argv, NULL);

	/* The registry is created during startup (headless-safe). */
	engine = lrg_engine_get_default ();
	lrg_engine_startup (engine, &error);
	g_assert_no_error (error);
	lrg_registry_register (lrg_engine_get_registry (engine), "test-spin", TEST_TYPE_SPIN);

	g_test_add_func ("/level/instantiate-3d/tree-and-transforms", test_tree_and_transforms);
	g_test_add_func ("/level/instantiate-3d/visual-kinds", test_visual_kinds);
	g_test_add_func ("/level/instantiate-3d/components-and-scripts", test_components_and_scripts);
	g_test_add_func ("/level/instantiate-3d/custom-vfunc", test_custom_vfunc);
	g_test_add_func ("/level/instantiate-3d/guid-conflicts", test_guid_conflicts);
	g_test_add_func ("/level/instantiate-3d/instantiate-node", test_instantiate_node);
	g_test_add_func ("/level/instantiate-3d/load-world3d", test_load_world3d);
	g_test_add_func ("/level/instantiate-3d/audio-mixer", test_level_audio_mixer);
	g_test_add_func ("/level/instantiate-3d/store-property", test_store_property);

	return g_test_run ();
}
