/* test-level-runtime.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The runtime level module (src/level/) used the way a shipped game uses
 * it: hand-written .rlevel and .rprefab files are loaded and instantiated
 * into an LrgWorld with only lrg_level_*, lrg_prefab_*, lrg_node_* and
 * lrg_level_instantiate().  No LrgEditor, selection, command or project
 * object is created, and nothing here needs the editor headers, so this
 * file also builds and runs against a BUILD_EDITOR=0 library
 * (tests/check-level-noeditor.sh does exactly that).
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <libregnum.h>

#define EPS (1e-5f)

/* ==========================================================================
 * A tiny registry component, as a game would register its own
 * ========================================================================== */

#define TEST_TYPE_HEALTH (test_health_get_type ())
G_DECLARE_FINAL_TYPE (TestHealth, test_health, TEST, HEALTH, LrgComponent)

struct _TestHealth
{
	LrgComponent parent_instance;
	gint         hp;
};

G_DEFINE_FINAL_TYPE (TestHealth, test_health, LRG_TYPE_COMPONENT)

enum
{
	TH_PROP_0,
	TH_PROP_HP,
	TH_N_PROPS
};

static GParamSpec *th_props[TH_N_PROPS];

static void
test_health_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
	TestHealth *self = TEST_HEALTH (object);

	if (prop_id == TH_PROP_HP)
		g_value_set_int (value, self->hp);
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
test_health_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
	TestHealth *self = TEST_HEALTH (object);

	if (prop_id == TH_PROP_HP)
		self->hp = g_value_get_int (value);
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
test_health_class_init (TestHealthClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = test_health_get_property;
	object_class->set_property = test_health_set_property;

	th_props[TH_PROP_HP] =
		g_param_spec_int ("hp", "HP", "Hit points",
		                  0, G_MAXINT, 0,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                  G_PARAM_EXPLICIT_NOTIFY);
	g_object_class_install_properties (object_class, TH_N_PROPS, th_props);
}

static void
test_health_init (TestHealth *self)
{
	self->hp = 0;
}

/* ==========================================================================
 * Fixture: a temp project with crate.rprefab and room.rlevel
 * ========================================================================== */

/*
 * The prefab: a "Crate" root (cube, health component) with a "Lid" child.
 * A .rprefab is an ordinary .rlevel document whose first top-level node is
 * the prefab root.
 */
static const gchar *PREFAB_YAML =
	"type: level\n"
	"name: prefab\n"
	"nodes:\n"
	"  - name: Crate\n"
	"    guid: aaaaaaaa-0000-0000-0000-000000000001\n"
	"    transform:\n"
	"      location: [0.0, 0.0, 0.0]\n"
	"      rotation: [0.0, 0.0, 0.0]\n"
	"      scale: [1.0, 1.0, 1.0]\n"
	"    visual:\n"
	"      kind: primitive\n"
	"      primitive: primitive_cube\n"
	"    components:\n"
	"      - type: test-health\n"
	"        props:\n"
	"          hp: 25\n"
	"    children:\n"
	"      - name: Lid\n"
	"        guid: aaaaaaaa-0000-0000-0000-000000000002\n"
	"        transform:\n"
	"          location: [0.0, 0.5, 0.0]\n"
	"          rotation: [0.0, 0.0, 0.0]\n"
	"          scale: [1.0, 0.1, 1.0]\n";

/*
 * The level: a rotated "Room" group with a "Door" child (the child's world
 * position depends on the parent rotation), and a "Guard" with a health
 * component, plus a timeline track on the door.
 */
static const gchar *LEVEL_YAML =
	"type: level\n"
	"name: room\n"
	"default_view: 3d\n"
	"nodes:\n"
	"  - name: Room\n"
	"    guid: bbbbbbbb-0000-0000-0000-000000000001\n"
	"    transform:\n"
	"      location: [10.0, 0.0, 0.0]\n"
	"      rotation: [0.0, 1.5707963267948966, 0.0]\n"
	"      scale: [2.0, 2.0, 2.0]\n"
	"    children:\n"
	"      - name: Door\n"
	"        guid: bbbbbbbb-0000-0000-0000-000000000002\n"
	"        transform:\n"
	"          location: [1.0, 0.0, 0.0]\n"
	"          rotation: [0.0, 0.0, 0.0]\n"
	"          scale: [1.0, 1.0, 1.0]\n"
	"  - name: Guard\n"
	"    guid: bbbbbbbb-0000-0000-0000-000000000003\n"
	"    transform:\n"
	"      location: [3.0, 4.0, 0.0]\n"
	"      rotation: [0.0, 0.0, 0.0]\n"
	"      scale: [1.0, 1.0, 1.0]\n"
	"    components:\n"
	"      - type: test-health\n"
	"        props:\n"
	"          hp: 100\n"
	"timelines:\n"
	"  - name: DoorOpen\n"
	"    length: 1.0\n"
	"    loop: false\n"
	"    autoplay: true\n"
	"    tracks:\n"
	"      - node: bbbbbbbb-0000-0000-0000-000000000002\n"
	"        channel: rotation.y\n"
	"        keys:\n"
	"          - time: 0.0\n"
	"            value: 0.0\n"
	"          - time: 1.0\n"
	"            value: 1.5\n";

typedef struct
{
	gchar *dir;
	gchar *prefab_path;
	gchar *level_path;
} Fixture;

static void
fixture_setup (Fixture       *f,
               gconstpointer  data)
{
	g_autoptr(GError) error = NULL;

	(void) data;

	f->dir = g_dir_make_tmp ("lrg-level-runtime-XXXXXX", &error);
	g_assert_no_error (error);
	f->prefab_path = g_build_filename (f->dir, "crate.rprefab", NULL);
	f->level_path = g_build_filename (f->dir, "room.rlevel", NULL);

	g_file_set_contents (f->prefab_path, PREFAB_YAML, -1, &error);
	g_assert_no_error (error);
	g_file_set_contents (f->level_path, LEVEL_YAML, -1, &error);
	g_assert_no_error (error);
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  data)
{
	g_autoptr(GDir) dir = NULL;
	const gchar    *name;

	(void) data;

	/* Remove every file the tests wrote, then the directory. */
	dir = g_dir_open (f->dir, 0, NULL);
	while (dir != NULL && (name = g_dir_read_name (dir)) != NULL)
	{
		g_autofree gchar *path = g_build_filename (f->dir, name, NULL);

		g_unlink (path);
	}
	g_rmdir (f->dir);

	g_free (f->level_path);
	g_free (f->prefab_path);
	g_free (f->dir);
}

/*
 * Returns the default engine with its registry started and the test
 * component registered, or NULL (the caller skips) when unavailable.
 */
static LrgEngine *
get_engine (void)
{
	LrgEngine         *engine;
	LrgRegistry       *registry;
	g_autoptr(GError)  error = NULL;

	engine = lrg_engine_get_default ();
	if (engine == NULL)
		return NULL;

	/* Headless-safe: graphics subsystems are window-guarded. */
	lrg_engine_startup (engine, &error);
	g_assert_no_error (error);

	registry = lrg_engine_get_registry (engine);
	if (registry == NULL)
		return NULL;
	if (!lrg_registry_is_registered (registry, "test-health"))
		lrg_registry_register (registry, "test-health", TEST_TYPE_HEALTH);

	return engine;
}

static LrgLevel *
load_level (const gchar *path)
{
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(GError)             error = NULL;
	LrgLevel                     *level;

	level = lrg_level_serializer_load_from_file (ser, path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (level);
	return level;
}

static gint
health_of (LrgGameObject *go)
{
	LrgComponent *comp;
	gint          hp = -1;

	comp = lrg_game_object_get_component (go, TEST_TYPE_HEALTH);
	if (comp != NULL)
		g_object_get (comp, "hp", &hp, NULL);
	return hp;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* Loading a hand-written .rlevel gives the authored tree and timelines. */
static void
test_load_level (Fixture       *f,
                 gconstpointer  data)
{
	g_autoptr(LrgLevel) level = NULL;
	LrgNode            *door;
	LrgNode            *guard;
	GrlVector3         *rot;

	(void) data;

	level = load_level (f->level_path);
	g_assert_cmpstr (lrg_level_get_name (level), ==, "room");
	g_assert_cmpuint (lrg_level_count_nodes (level), ==, 3);
	g_assert_cmpuint (lrg_level_get_n_timelines (level), ==, 1);

	door = lrg_level_find_node (level, "bbbbbbbb-0000-0000-0000-000000000002");
	g_assert_nonnull (door);
	g_assert_cmpstr (lrg_node_get_name (door), ==, "Door");
	g_assert_cmpstr (lrg_node_get_name (lrg_node_get_parent (door)), ==, "Room");

	guard = lrg_level_find_node (level, "bbbbbbbb-0000-0000-0000-000000000003");
	g_assert_nonnull (guard);
	g_assert_nonnull (lrg_node_find_component (guard, "test-health"));

	rot = lrg_node_get_rotation (lrg_node_get_parent (door));
	g_assert_cmpfloat_with_epsilon (rot->y, (gfloat) G_PI_2, EPS);
}

/*
 * lrg_node_get_world_matrix() composes parent TRS (core lrg-math3d): the
 * Door at local (1, 0, 0) under a Room at x = 10, rotated +90 degrees about
 * Y and scaled 2, ends up at (10, 0, -2).
 */
static void
test_world_matrix (Fixture       *f,
                   gconstpointer  data)
{
	g_autoptr(LrgLevel) level = NULL;
	LrgNode            *door;
	gfloat              world[16];
	gfloat              loc[3], rot[3], scl[3];

	(void) data;

	level = load_level (f->level_path);
	door = lrg_level_find_node (level, "bbbbbbbb-0000-0000-0000-000000000002");
	lrg_node_get_world_matrix (door, world);

	g_assert_true (lrg_math3d_decompose (world, loc, rot, scl));
	g_assert_cmpfloat_with_epsilon (loc[0], 10.0f, EPS);
	g_assert_cmpfloat_with_epsilon (loc[1], 0.0f, EPS);
	g_assert_cmpfloat_with_epsilon (loc[2], -2.0f, EPS);
	g_assert_cmpfloat_with_epsilon (rot[1], (gfloat) G_PI_2, 1e-3f);
	g_assert_cmpfloat_with_epsilon (scl[0], 2.0f, EPS);
	g_assert_cmpfloat_with_epsilon (scl[1], 2.0f, EPS);
	g_assert_cmpfloat_with_epsilon (scl[2], 2.0f, EPS);
}

/*
 * The game loop: load the level, instantiate it into a world, find runtime
 * objects by their node guid, read materialised component properties.
 */
static void
test_instantiate_level (Fixture       *f,
                        gconstpointer  data)
{
	g_autoptr(LrgLevel) level = NULL;
	g_autoptr(LrgWorld) world = NULL;
	g_autoptr(GError)   error = NULL;
	LrgEngine          *engine;
	LrgGameObject      *guard;

	(void) data;

	engine = get_engine ();
	if (engine == NULL)
	{
		g_test_skip ("No default engine / registry");
		return;
	}

	level = load_level (f->level_path);
	world = lrg_world_new ();
	g_assert_true (lrg_level_instantiate (level, world, engine, &error));
	g_assert_no_error (error);

	/* One game object per node, the hierarchy flattened. */
	g_assert_cmpuint (lrg_world_get_object_count (world), ==, 3);

	guard = lrg_world_find_by_tag (world, "bbbbbbbb-0000-0000-0000-000000000003");
	g_assert_nonnull (guard);
	g_assert_cmpfloat_with_epsilon (grl_entity_get_x (GRL_ENTITY (guard)), 3.0f, EPS);
	g_assert_cmpfloat_with_epsilon (grl_entity_get_y (GRL_ENTITY (guard)), 4.0f, EPS);
	g_assert_cmpint (health_of (guard), ==, 100);
}

/*
 * Runtime prefab spawning: load a .rprefab (nested prefabs resolved),
 * place a linked instance into the loaded level, save it, reload it (the
 * instance is stored expanded, with its prefab: block) and instantiate.
 */
static void
test_prefab_spawn (Fixture       *f,
                   gconstpointer  data)
{
	g_autoptr(LrgNode)            prefab = NULL;
	g_autoptr(LrgNode)            crate = NULL;
	g_autoptr(LrgLevel)           level = NULL;
	g_autoptr(LrgLevel)           reloaded = NULL;
	g_autoptr(LrgLevelSerializer) ser = NULL;
	g_autoptr(LrgWorld)           world = NULL;
	g_autoptr(GError)             error = NULL;
	g_auto(GStrv)                 deps = NULL;
	g_autofree gchar             *saved = NULL;
	LrgEngine                    *engine;
	LrgGameObject                *go;
	LrgNode                      *placed;
	LrgNode                      *lid;
	GPtrArray                    *children;
	const gchar                  *crate_guid;

	(void) data;

	prefab = lrg_prefab_load_resolved (f->prefab_path, &deps, &error);
	g_assert_no_error (error);
	g_assert_nonnull (prefab);
	g_assert_cmpstr (lrg_node_get_name (prefab), ==, "Crate");
	g_assert_cmpuint (lrg_node_get_n_children (prefab), ==, 1);
	/* No nested prefabs: the dependency list is empty. */
	g_assert_true (deps == NULL || deps[0] == NULL);

	crate = lrg_prefab_instantiate_linked (prefab, f->prefab_path, NULL);
	g_assert_nonnull (crate);
	g_assert_nonnull (lrg_node_get_prefab_link (crate));
	/* Instances get fresh guids. */
	g_assert_cmpstr (lrg_node_get_guid (crate), !=, "aaaaaaaa-0000-0000-0000-000000000001");
	lrg_node_set_location_xyz (crate, -5.0f, 2.0f, 0.0f);
	crate_guid = lrg_node_get_guid (crate);

	level = load_level (f->level_path);
	lrg_level_add_node (level, crate, NULL);
	g_assert_cmpuint (lrg_level_count_nodes (level), ==, 5);

	/* Save and reload: the expanded instance and its link survive. */
	saved = g_build_filename (f->dir, "room-with-crate.rlevel", NULL);
	ser = lrg_level_serializer_new ();
	g_assert_true (lrg_level_serializer_save_to_file (ser, level, saved, &error));
	g_assert_no_error (error);

	reloaded = load_level (saved);
	g_assert_cmpuint (lrg_level_count_nodes (reloaded), ==, 5);
	placed = lrg_level_find_node (reloaded, crate_guid);
	g_assert_nonnull (placed);
	g_assert_nonnull (lrg_node_get_prefab_link (placed));
	children = lrg_node_get_children (placed);
	g_assert_cmpuint (children->len, ==, 1);
	lid = g_ptr_array_index (children, 0);
	g_assert_cmpstr (lrg_node_get_name (lid), ==, "Lid");

	engine = get_engine ();
	if (engine == NULL)
	{
		g_test_skip ("No default engine / registry");
		return;
	}

	world = lrg_world_new ();
	g_assert_true (lrg_level_instantiate (reloaded, world, engine, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_world_get_object_count (world), ==, 5);

	go = lrg_world_find_by_tag (world, crate_guid);
	g_assert_nonnull (go);
	g_assert_cmpfloat_with_epsilon (grl_entity_get_x (GRL_ENTITY (go)), -5.0f, EPS);
	g_assert_cmpint (health_of (go), ==, 25);
	g_assert_nonnull (lrg_world_find_by_tag (world, lrg_node_get_guid (lid)));
}

/* ==========================================================================
 * Project physics: the physics: block of project.ryaml configures the
 * world a level is loaded into (gravity, step limits, layers, no_collide)
 * ========================================================================== */

/*
 * A Mars-gravity project with its own layers: "ghost" does not collide
 * with "ground". An unknown key (cloth_quality) and another tool's root
 * key must not disturb loading.
 */
static const gchar *PHYSICS_PROJECT_YAML =
	"project:\n"
	"  name: mars\n"
	"  asset_dirs: [assets/]\n"
	"physics:\n"
	"  layers: [default, ground, crate, ghost]\n"
	"  no_collide:\n"
	"    - [ground, ghost]\n"
	"  gravity: [0, -3.7, 0]\n"
	"  fixed_dt: 0.02\n"
	"  max_substeps: 3\n"
	"  iterations: 6\n"
	"  sleep_threshold: 0.001\n"
	"  sleep_substeps: 12\n"
	"  cloth_quality: high\n"
	"tool_data: {keep: me}\n";

/* a box body of @kind on @layer at (x, y, 0) */
static void
add_box_node (LrgLevel           *level,
              const gchar        *name,
              LrgDynamicsBodyKind kind,
              const gchar        *layer,
              gfloat              x,
              gfloat              y,
              gfloat              half_x,
              gfloat              half_y)
{
	g_autoptr(LrgNode)             node = lrg_node_new (name);
	g_autoptr(LrgNodePhysics)      phys = lrg_node_physics_new (kind);
	g_autoptr(LrgNodePhysicsShape) box = lrg_node_physics_shape_new (LRG_COLLIDER3D_BOX);

	lrg_node_physics_shape_set_half_extents (box, half_x, half_y, half_x);
	lrg_node_physics_add_shape (phys, box);
	lrg_node_physics_set_layer (phys, layer);
	if (kind == LRG_DYNAMICS_BODY_DYNAMIC)
		lrg_node_physics_set_mass (phys, 1.0f);
	lrg_node_set_physics (node, phys);
	lrg_node_set_location_xyz (node, x, y, 0.0f);
	lrg_level_add_node (level, node, NULL);
}

/* a level with a floor, a crate above it and a ghost above it */
static LrgLevel *
physics_arena (void)
{
	LrgLevel *level = lrg_level_new ("arena");

	add_box_node (level, "Floor", LRG_DYNAMICS_BODY_STATIC, "ground", 0.0f, -0.5f, 10.0f, 0.5f);
	add_box_node (level, "Crate", LRG_DYNAMICS_BODY_DYNAMIC, "crate", -2.0f, 1.0f, 0.5f, 0.5f);
	add_box_node (level, "Ghost", LRG_DYNAMICS_BODY_DYNAMIC, "ghost", 2.0f, 1.0f, 0.5f, 0.5f);
	return level;
}

static gfloat
entity_y (LrgWorld3D  *world,
          const gchar *name)
{
	LrgEntity3D *e = lrg_world3d_find_by_name (world, name);
	gfloat       p[3];

	g_assert_nonnull (e);
	lrg_entity3d_get_world_position (e, p);
	return p[1];
}

static void
run_world (LrgWorld3D *world,
           gfloat      seconds)
{
	guint i;
	guint n = (guint) ceilf (seconds / (1.0f / 60.0f));

	for (i = 0; i < n; i++)
		lrg_world3d_step (world, 1.0f / 60.0f);
}

static void
test_project_physics (Fixture       *f,
                      gconstpointer  data)
{
	g_autoptr(GError)             error = NULL;
	g_autoptr(LrgLevel)           arena = physics_arena ();
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(LrgLevel)           loaded = NULL;
	g_autoptr(LrgWorld3D)         world = lrg_world3d_new ();
	g_autofree gchar             *project = g_build_filename (f->dir, "project.ryaml", NULL);
	g_autofree gchar             *levels = g_build_filename (f->dir, "levels", NULL);
	g_autofree gchar             *level_path = g_build_filename (levels, "arena.rlevel", NULL);
	g_autofree gchar             *found = NULL;
	LrgDynamicsWorld             *physics;
	LrgPhysicsLayers             *layers;
	LrgPhysicsSettings            settings;
	gint                          ground;
	gint                          ghost;

	(void) data;

	/* the project at the root, the level one directory down */
	g_assert_true (g_file_set_contents (project, PHYSICS_PROJECT_YAML, -1, &error));
	g_assert_cmpint (g_mkdir_with_parents (levels, 0700), ==, 0);
	g_assert_true (lrg_level_serializer_save_to_file (ser, arena, level_path, &error));
	g_assert_no_error (error);
	found = lrg_physics_settings_find_project (level_path);
	g_assert_cmpstr (found, ==, project);

	loaded = lrg_level_load_world3d (level_path, world, &error);
	g_assert_no_error (error);
	g_assert_nonnull (loaded);

	/* the world took the project's block */
	g_assert_nonnull (lrg_world3d_get_physics_settings (world));
	physics = lrg_world3d_get_physics (world);
	g_assert_nonnull (physics);
	lrg_dynamics_world_get_settings (physics, &settings);
	g_assert_cmpfloat (settings.gravity[0], ==, 0.0f);
	g_assert_cmpfloat (settings.gravity[1], ==, -3.7f);
	g_assert_cmpfloat (settings.gravity[2], ==, 0.0f);
	g_assert_cmpuint (settings.iterations, ==, 6);
	g_assert_cmpfloat (settings.sleep_threshold, ==, 0.001f);
	g_assert_cmpuint (settings.sleep_substeps, ==, 12);
	g_assert_cmpfloat (settings.fixed_dt, ==, 0.02f);
	g_assert_cmpuint (settings.max_substeps, ==, 1);   /* one per world fixed update */
	g_assert_cmpfloat (lrg_world3d_get_fixed_step (world), ==, 0.02f);
	g_assert_cmpuint (lrg_world3d_get_max_substeps (world), ==, 3);

	/* the project's layers and matrix, not the default table */
	layers = lrg_dynamics_world_get_layers (physics);
	g_assert_nonnull (layers);
	g_assert_true (layers == lrg_world3d_get_physics_layers (world));
	ground = lrg_physics_layers_find (layers, "ground");
	ghost = lrg_physics_layers_find (layers, "ghost");
	g_assert_cmpint (ground, ==, 1);
	g_assert_cmpint (ghost, ==, 3);
	g_assert_false (lrg_physics_layers_get_collides (layers, (guint) ground, (guint) ghost));
	g_assert_true (lrg_physics_layers_get_collides (layers, (guint) ground, 2u));

	/* simulated: the crate lands on the floor, the ghost falls through;
	 * free fall from rest for 1 s at 3.7 m/s² is 1.85 m */
	run_world (world, 1.0f);
	g_assert_cmpfloat_with_epsilon (entity_y (world, "Ghost"), 1.0f - 1.85f, 0.15f);
	run_world (world, 2.0f);
	g_assert_cmpfloat_with_epsilon (entity_y (world, "Crate"), 0.5f, 0.06f);
	g_assert_cmpfloat (entity_y (world, "Ghost"), <, -3.0f);

	g_unlink (level_path);
	g_rmdir (levels);
}

/* No project.ryaml above the level: the defaults (Earth gravity, the
 * default layer table) */
static void
test_project_physics_none (Fixture       *f,
                           gconstpointer  data)
{
	g_autoptr(GError)             error = NULL;
	g_autoptr(LrgLevel)           arena = physics_arena ();
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(LrgLevel)           loaded = NULL;
	g_autoptr(LrgWorld3D)         world = lrg_world3d_new ();
	g_autoptr(LrgPhysicsLayers)   defaults = lrg_physics_layers_new_default ();
	g_autofree gchar             *level_path = g_build_filename (f->dir, "arena.rlevel", NULL);
	g_autofree gchar             *found = NULL;
	LrgPhysicsSettings            settings;
	LrgPhysicsSettings            expected;

	(void) data;

	g_assert_true (lrg_level_serializer_save_to_file (ser, arena, level_path, &error));
	g_assert_no_error (error);
	found = lrg_physics_settings_find_project (level_path);
	if (found != NULL)
	{
		/* a project.ryaml in a parent of the temp directory */
		g_test_skip ("a project.ryaml exists above the temporary directory");
		return;
	}

	loaded = lrg_level_load_world3d (level_path, world, &error);
	g_assert_no_error (error);
	g_assert_null (lrg_world3d_get_physics_settings (world));
	g_assert_null (lrg_world3d_get_physics_layers (world));
	lrg_dynamics_world_get_settings (lrg_world3d_get_physics (world), &settings);
	lrg_physics_settings_init_default (&expected);
	/* the world runs one physics substep per fixed update of its own */
	expected.max_substeps = 1;
	g_assert_true (lrg_physics_settings_equal (&settings, &expected));
	g_assert_true (lrg_physics_layers_equal (lrg_dynamics_world_get_layers (lrg_world3d_get_physics (world)),
	                                         defaults));
	/* the world's own fixed step is not touched without a project */
	g_assert_cmpfloat (lrg_world3d_get_fixed_step (world), ==, 1.0f / 60.0f);
	g_assert_cmpuint (lrg_world3d_get_max_substeps (world), ==, 8);
}

/* Explicit settings win over the project the level sits in */
static void
test_project_physics_explicit (Fixture       *f,
                               gconstpointer  data)
{
	g_autoptr(GError)             error = NULL;
	g_autoptr(LrgLevel)           arena = physics_arena ();
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(LrgLevel)           loaded = NULL;
	g_autoptr(LrgWorld3D)         world = lrg_world3d_new ();
	g_autofree gchar             *project = g_build_filename (f->dir, "project.ryaml", NULL);
	g_autofree gchar             *level_path = g_build_filename (f->dir, "arena.rlevel", NULL);
	LrgPhysicsSettings            mine;
	LrgPhysicsSettings            got;

	(void) data;

	g_assert_true (g_file_set_contents (project, PHYSICS_PROJECT_YAML, -1, &error));
	g_assert_true (lrg_level_serializer_save_to_file (ser, arena, level_path, &error));
	g_assert_no_error (error);

	lrg_physics_settings_init_default (&mine);
	mine.gravity[1] = -1.62f;
	mine.iterations = 0;
	g_assert_false (lrg_world3d_set_physics_settings (world, &mine, NULL, &error));
	g_assert_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT);
	g_clear_error (&error);
	g_assert_null (lrg_world3d_get_physics_settings (world));
	mine.iterations = 12;
	g_assert_true (lrg_world3d_set_physics_settings (world, &mine, NULL, &error));
	g_assert_no_error (error);

	loaded = lrg_level_load_world3d (level_path, world, &error);
	g_assert_no_error (error);
	lrg_dynamics_world_get_settings (lrg_world3d_get_physics (world), &got);
	g_assert_cmpfloat (got.gravity[1], ==, -1.62f);
	g_assert_cmpuint (got.iterations, ==, 12);
	/* no project layers given: the default table */
	g_assert_cmpint (lrg_physics_layers_find (lrg_dynamics_world_get_layers (lrg_world3d_get_physics (world)),
	                                          "ghost"), ==, -1);

	/* applied to an attached world straight away */
	mine.gravity[1] = -20.0f;
	g_assert_true (lrg_world3d_set_physics_settings (world, &mine, NULL, &error));
	lrg_dynamics_world_get_settings (lrg_world3d_get_physics (world), &got);
	g_assert_cmpfloat (got.gravity[1], ==, -20.0f);
}

/*
 * The runtime module's types must be usable without any editor type having
 * been registered: the program above never touches LrgEditor, and in a
 * BUILD_EDITOR=0 build the editor types do not exist at all.
 */
static void
test_no_editor_types (void)
{
	g_autoptr(LrgLevel) level = lrg_level_new ("plain");

	g_assert_nonnull (level);
#ifdef LRG_BUILD_EDITOR
	g_test_message ("library built with the editor (BUILD_EDITOR=1)");
#else
	g_test_message ("library built without the editor (BUILD_EDITOR=0)");
#endif
	/* Nothing in this program has created an editor object. */
	g_assert_true (g_type_from_name ("LrgEditor") == G_TYPE_INVALID);
	g_assert_true (g_type_from_name ("LrgEditorSelection") == G_TYPE_INVALID);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	/* Runs first, before any test could pull an editor type in. */
	g_test_add_func ("/level-runtime/no-editor-types", test_no_editor_types);
	g_test_add ("/level-runtime/load-level", Fixture, NULL,
	            fixture_setup, test_load_level, fixture_teardown);
	g_test_add ("/level-runtime/world-matrix", Fixture, NULL,
	            fixture_setup, test_world_matrix, fixture_teardown);
	g_test_add ("/level-runtime/instantiate-level", Fixture, NULL,
	            fixture_setup, test_instantiate_level, fixture_teardown);
	g_test_add ("/level-runtime/prefab-spawn", Fixture, NULL,
	            fixture_setup, test_prefab_spawn, fixture_teardown);
	g_test_add ("/level-runtime/project-physics", Fixture, NULL,
	            fixture_setup, test_project_physics, fixture_teardown);
	g_test_add ("/level-runtime/project-physics/none", Fixture, NULL,
	            fixture_setup, test_project_physics_none, fixture_teardown);
	g_test_add ("/level-runtime/project-physics/explicit", Fixture, NULL,
	            fixture_setup, test_project_physics_explicit, fixture_teardown);

	return g_test_run ();
}
