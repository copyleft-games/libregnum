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

	return g_test_run ();
}
