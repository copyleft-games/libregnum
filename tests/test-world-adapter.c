/* test-world-adapter.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the LrgWorldAdapter interface and LrgTiledWorld: refusals
 * from an adapter that implements nothing, places, walls from layers,
 * solid tiles and obstacle objects, spawning, walking to places and
 * seats, unreachable targets, removal, the camera, and a failed load
 * leaving the old world alone.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>
#include <math.h>

/* ==========================================================================
 * An adapter that implements nothing
 * ========================================================================== */

#define TEST_TYPE_EMPTY_WORLD (test_empty_world_get_type ())
G_DECLARE_FINAL_TYPE (TestEmptyWorld, test_empty_world, TEST, EMPTY_WORLD, GObject)

struct _TestEmptyWorld
{
	GObject parent_instance;
};

static void
test_empty_world_iface_init (LrgWorldAdapterInterface *iface)
{
}

G_DEFINE_FINAL_TYPE_WITH_CODE (TestEmptyWorld, test_empty_world, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_WORLD_ADAPTER,
                                                      test_empty_world_iface_init))

static void
test_empty_world_class_init (TestEmptyWorldClass *klass)
{
}

static void
test_empty_world_init (TestEmptyWorld *self)
{
}

static void
test_unimplemented_refuses (void)
{
	g_autoptr(TestEmptyWorld) world = g_object_new (TEST_TYPE_EMPTY_WORLD, NULL);
	LrgWorldAdapter *adapter = LRG_WORLD_ADAPTER (world);
	g_autoptr(GError) error = NULL;
	gfloat x = 7.0f;

	g_assert_false (lrg_world_adapter_load (adapter, "x.tmj", &error));
	g_assert_error (error, LRG_WORLD_ADAPTER_ERROR, LRG_WORLD_ADAPTER_ERROR_NOT_SUPPORTED);
	g_clear_error (&error);
	g_assert_cmpuint (lrg_world_adapter_spawn_actor (adapter, "a", NULL, &error), ==, 0);
	g_assert_error (error, LRG_WORLD_ADAPTER_ERROR, LRG_WORLD_ADAPTER_ERROR_NOT_SUPPORTED);
	g_clear_error (&error);
	g_assert_false (lrg_world_adapter_move_actor (adapter, 1, 0.0f, 0.0f, 0.0f, &error));
	g_assert_error (error, LRG_WORLD_ADAPTER_ERROR, LRG_WORLD_ADAPTER_ERROR_NOT_SUPPORTED);
	g_clear_error (&error);
	g_assert_false (lrg_world_adapter_resolve_place (adapter, "desk", &x, NULL, NULL));
	g_assert_cmpfloat (x, ==, 7.0f);
	g_assert_false (lrg_world_adapter_move_actor_to_place (adapter, 1, "desk", &error));
	g_assert_error (error, LRG_WORLD_ADAPTER_ERROR, LRG_WORLD_ADAPTER_ERROR_UNKNOWN_PLACE);
	g_assert_false (lrg_world_adapter_remove_actor (adapter, 1));
	g_assert_false (lrg_world_adapter_get_actor_position (adapter, 1, NULL, NULL, NULL));
	lrg_world_adapter_set_camera (adapter, LRG_WORLD_CAMERA_OVERVIEW);
	lrg_world_adapter_set_camera_focus (adapter, 1);
	lrg_world_adapter_update (adapter, 0.1f);
	lrg_world_adapter_draw (adapter);
}

/* ==========================================================================
 * LrgTiledWorld on the office fixture
 * ========================================================================== */

static gchar *
office_path (void)
{
	return g_test_build_filename (G_TEST_DIST, "fixtures", "tiled", "office.tmj", NULL);
}

static void
on_arrived (LrgWorldAdapter *adapter,
            guint            handle,
            gpointer         user_data)
{
	g_ptr_array_add (user_data, GUINT_TO_POINTER (handle));
}

static guint
run_until_arrived (LrgWorldAdapter *adapter,
                   GPtrArray       *arrivals,
                   guint            want)
{
	guint steps = 0;

	while (arrivals->len < want && steps < 5000)
	{
		lrg_world_adapter_update (adapter, 1.0f / 60.0f);
		steps++;
	}
	return steps;
}

static void
test_office (void)
{
	g_autoptr(LrgTiledWorld) world = lrg_tiled_world_new ();
	LrgWorldAdapter *adapter = LRG_WORLD_ADAPTER (world);
	g_autofree gchar *path = office_path ();
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) arrivals = g_ptr_array_new ();
	LrgNavGrid *grid;
	guint kudu;
	guint oryx;
	gfloat x;
	gfloat y;
	gfloat z;

	g_signal_connect (adapter, "actor-arrived", G_CALLBACK (on_arrived), arrivals);

	/* nothing loaded */
	g_assert_null (lrg_tiled_world_get_map (world));
	g_assert_false (lrg_world_adapter_resolve_place (adapter, "meeting", NULL, NULL, NULL));
	g_assert_cmpuint (lrg_world_adapter_spawn_actor (adapter, "kudu", NULL, &error), ==, 0);
	g_assert_error (error, LRG_WORLD_ADAPTER_ERROR, LRG_WORLD_ADAPTER_ERROR_NOT_LOADED);
	g_clear_error (&error);

	g_assert_true (lrg_world_adapter_load (adapter, path, &error));
	g_assert_no_error (error);
	g_assert_nonnull (lrg_tiled_world_get_map (world));

	/* places are named objects, at their centres */
	g_assert_true (lrg_world_adapter_resolve_place (adapter, "meeting", &x, &y, &z));
	g_assert_cmpfloat (x, ==, 64.0f);
	g_assert_cmpfloat (y, ==, 56.0f);
	g_assert_cmpfloat (z, ==, 0.0f);
	g_assert_false (lrg_world_adapter_resolve_place (adapter, "nowhere", NULL, NULL, NULL));

	/* the "walls" layer blocks, the doorway in it does not */
	grid = lrg_tiled_world_get_nav_grid (world);
	g_assert_cmpuint (lrg_nav_grid_get_width (grid), ==, 10);
	g_assert_false (lrg_nav_grid_is_walkable (grid, 0, 0));
	g_assert_false (lrg_nav_grid_is_walkable (grid, 9, 3));
	g_assert_true (lrg_nav_grid_is_walkable (grid, 4, 7));
	g_assert_true (lrg_nav_grid_is_walkable (grid, 3, 3));

	/* no spawn object: actors start in the middle */
	kudu = lrg_world_adapter_spawn_actor (adapter, "kudu", "#ff8800", &error);
	g_assert_no_error (error);
	g_assert_cmpuint (kudu, >, 0);
	g_assert_true (lrg_world_adapter_get_actor_position (adapter, kudu, &x, &y, NULL));
	g_assert_cmpfloat (x, ==, 80.0f);
	g_assert_cmpfloat (y, ==, 64.0f);
	g_assert_cmpuint (lrg_world_adapter_spawn_actor (adapter, "kudu", NULL, &error), ==, 0);
	g_assert_error (error, LRG_WORLD_ADAPTER_ERROR, LRG_WORLD_ADAPTER_ERROR_DUPLICATE_ACTOR);
	g_clear_error (&error);
	oryx = lrg_world_adapter_spawn_actor (adapter, "oryx", "ibex", &error);
	g_assert_cmpuint (oryx, !=, kudu);
	g_assert_cmpuint (lrg_tiled_world_find_actor (world, "oryx"), ==, oryx);
	g_assert_cmpuint (lrg_tiled_world_find_actor (world, "nobody"), ==, 0);
	g_assert_cmpuint (lrg_tiled_world_get_n_actors (world), ==, 2);

	/* walk to a desk */
	g_assert_true (lrg_world_adapter_move_actor_to_place (adapter, kudu, "desk-1", &error));
	g_assert_no_error (error);
	g_assert_cmpint (lrg_actor_mover_get_state (lrg_tiled_world_get_actor_mover (world, kudu)),
	                 ==, LRG_ACTOR_MOVER_MOVING);
	run_until_arrived (adapter, arrivals, 1);
	g_assert_cmpuint (arrivals->len, ==, 1);
	g_assert_cmpuint (GPOINTER_TO_UINT (g_ptr_array_index (arrivals, 0)), ==, kudu);
	lrg_world_adapter_get_actor_position (adapter, kudu, &x, &y, NULL);
	g_assert_cmpfloat (x, ==, 120.0f);
	g_assert_cmpfloat (y, ==, 40.0f);
	g_assert_cmpint (lrg_actor_mover_get_state (lrg_tiled_world_get_actor_mover (world, kudu)),
	                 ==, LRG_ACTOR_MOVER_IDLE);

	/* errors */
	g_assert_false (lrg_world_adapter_move_actor_to_place (adapter, kudu, "nowhere", &error));
	g_assert_error (error, LRG_WORLD_ADAPTER_ERROR, LRG_WORLD_ADAPTER_ERROR_UNKNOWN_PLACE);
	g_clear_error (&error);
	g_assert_false (lrg_world_adapter_move_actor (adapter, 999, 1.0f, 1.0f, 0.0f, &error));
	g_assert_error (error, LRG_WORLD_ADAPTER_ERROR, LRG_WORLD_ADAPTER_ERROR_UNKNOWN_ACTOR);
	g_clear_error (&error);
	/* outside the map */
	g_assert_false (lrg_world_adapter_move_actor (adapter, kudu, -100.0f, 10.0f, 0.0f, &error));
	g_assert_error (error, LRG_WORLD_ADAPTER_ERROR, LRG_WORLD_ADAPTER_ERROR_UNREACHABLE);
	g_clear_error (&error);

	/* camera: the overview fits the 160x128 map into 320x240 */
	lrg_tiled_world_set_viewport (world, 320, 240);
	lrg_world_adapter_set_camera (adapter, LRG_WORLD_CAMERA_OVERVIEW);
	g_assert_cmpfloat_with_epsilon (lrg_camera2d_get_zoom (lrg_tiled_world_get_camera (world)),
	                                1.875f, 1e-5f);
	lrg_world_adapter_set_camera_focus (adapter, kudu);
	lrg_world_adapter_set_camera (adapter, LRG_WORLD_CAMERA_FOLLOW);
	{
		g_autoptr(GrlVector2) target = lrg_camera2d_get_target (lrg_tiled_world_get_camera (world));

		g_assert_cmpfloat (target->x, ==, 120.0f);
		g_assert_cmpfloat (target->y, ==, 40.0f);
	}
	lrg_world_adapter_draw (adapter);    /* headless: nothing, harmlessly */

	/* removal */
	g_assert_true (lrg_world_adapter_remove_actor (adapter, kudu));
	g_assert_false (lrg_world_adapter_remove_actor (adapter, kudu));
	g_assert_false (lrg_world_adapter_get_actor_position (adapter, kudu, NULL, NULL, NULL));
	g_assert_null (lrg_tiled_world_get_actor_mover (world, kudu));
	g_assert_cmpuint (lrg_tiled_world_get_n_actors (world), ==, 1);

	/* a load that fails leaves the world as it was */
	g_assert_false (lrg_world_adapter_load (adapter, "/nonexistent/map.tmj", &error));
	g_assert_error (error, LRG_WORLD_ADAPTER_ERROR, LRG_WORLD_ADAPTER_ERROR_LOAD);
	g_assert_cmpuint (lrg_tiled_world_get_n_actors (world), ==, 1);
	g_assert_true (lrg_world_adapter_resolve_place (adapter, "meeting", NULL, NULL, NULL));
}

/* ==========================================================================
 * Seats, spawn points and obstacles
 * ========================================================================== */

/* 8x6 cells of 10 px: a walled room, a spawn, a seat facing down (90
 * degrees) and a sofa obstacle boxing in a second seat */
static const gchar room_tmj[] =
	"{\"type\":\"map\",\"width\":8,\"height\":6,\"tilewidth\":10,\"tileheight\":10,"
	"\"orientation\":\"orthogonal\",\"tilesets\":[],\"layers\":["
	"{\"type\":\"tilelayer\",\"name\":\"bounds\",\"width\":8,\"height\":6,"
	" \"properties\":[{\"name\":\"collision\",\"type\":\"bool\",\"value\":true}],"
	" \"data\":[1,1,1,1,1,1,1,1, 1,0,0,0,0,0,0,1, 1,0,0,0,0,0,0,1,"
	"          1,0,0,0,0,0,0,1, 1,0,0,0,0,0,0,1, 1,1,1,1,1,1,1,1]},"
	"{\"type\":\"objectgroup\",\"name\":\"zones\",\"objects\":["
	" {\"id\":1,\"name\":\"door\",\"type\":\"spawn\",\"x\":15,\"y\":15,\"point\":true},"
	" {\"id\":2,\"name\":\"chair\",\"type\":\"seat\",\"x\":65,\"y\":15,\"point\":true,"
	"  \"properties\":[{\"name\":\"facing\",\"type\":\"float\",\"value\":90}]},"
	" {\"id\":3,\"name\":\"sofa\",\"type\":\"obstacle\",\"x\":40,\"y\":30,\"width\":30,\"height\":20},"
	" {\"id\":4,\"name\":\"boxed\",\"type\":\"seat\",\"x\":65,\"y\":45,\"point\":true},"
	" {\"id\":5,\"name\":\"wall-block\",\"type\":\"\",\"x\":50,\"y\":40,\"width\":10,\"height\":10,"
	"  \"properties\":[{\"name\":\"solid\",\"type\":\"bool\",\"value\":true}]}"
	"]}]}";

static void
test_seats_and_obstacles (void)
{
	g_autoptr(LrgTiledWorld) world = lrg_tiled_world_new ();
	LrgWorldAdapter *adapter = LRG_WORLD_ADAPTER (world);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) arrivals = g_ptr_array_new ();
	g_autofree gchar *dir = g_dir_make_tmp ("lrg-world-XXXXXX", &error);
	g_autofree gchar *path = g_build_filename (dir, "room.tmj", NULL);
	LrgActorMover *mover;
	LrgNavGrid *grid;
	guint ibex;
	gfloat x;
	gfloat y;

	g_assert_no_error (error);
	g_assert_true (g_file_set_contents (path, room_tmj, -1, &error));
	g_signal_connect (adapter, "actor-arrived", G_CALLBACK (on_arrived), arrivals);
	g_assert_true (lrg_world_adapter_load (adapter, path, &error));
	g_assert_no_error (error);

	/* obstacles block the cells whose centres they cover */
	grid = lrg_tiled_world_get_nav_grid (world);
	g_assert_false (lrg_nav_grid_is_walkable (grid, 4, 3));
	g_assert_false (lrg_nav_grid_is_walkable (grid, 6, 4));
	g_assert_false (lrg_nav_grid_is_walkable (grid, 5, 4));
	g_assert_true (lrg_nav_grid_is_walkable (grid, 3, 3));

	/* spawned at the spawn point */
	lrg_tiled_world_set_actor_speed (world, 40.0f);
	ibex = lrg_world_adapter_spawn_actor (adapter, "ibex", NULL, &error);
	g_assert_true (lrg_world_adapter_get_actor_position (adapter, ibex, &x, &y, NULL));
	g_assert_cmpfloat (x, ==, 15.0f);
	g_assert_cmpfloat (y, ==, 15.0f);
	mover = lrg_tiled_world_get_actor_mover (world, ibex);
	g_assert_cmpfloat (lrg_actor_mover_get_speed (mover), ==, 40.0f);

	/* a seat: sits on arrival, facing its "facing" */
	g_assert_true (lrg_world_adapter_move_actor_to_place (adapter, ibex, "chair", &error));
	run_until_arrived (adapter, arrivals, 1);
	g_assert_cmpuint (arrivals->len, ==, 1);
	g_assert_cmpint (lrg_actor_mover_get_state (mover), ==, LRG_ACTOR_MOVER_SITTING);
	g_assert_cmpstr (lrg_actor_mover_get_animation (mover), ==, "sit");
	g_assert_cmpfloat_with_epsilon (lrg_actor_mover_get_facing (mover), (gfloat) G_PI_2, 1e-5f);

	/* a seat the sofa and the wall box in: the cell itself is blocked,
	 * and so is every neighbour */
	g_assert_false (lrg_world_adapter_move_actor_to_place (adapter, ibex, "boxed", &error));
	g_assert_error (error, LRG_WORLD_ADAPTER_ERROR, LRG_WORLD_ADAPTER_ERROR_UNREACHABLE);
	g_clear_error (&error);

	/* back to the door: standing, not sitting */
	g_assert_true (lrg_world_adapter_move_actor_to_place (adapter, ibex, "door", &error));
	run_until_arrived (adapter, arrivals, 2);
	g_assert_cmpint (lrg_actor_mover_get_state (mover), ==, LRG_ACTOR_MOVER_IDLE);

	/* a fresh load replaces the actors */
	g_assert_true (lrg_world_adapter_load (adapter, path, &error));
	g_assert_cmpuint (lrg_tiled_world_get_n_actors (world), ==, 0);

	g_unlink (path);
	g_rmdir (dir);
}


/* ==========================================================================
 * Drawing (needs a display: run under build-aux/xrun.sh)
 * ========================================================================== */

static void
draw_world (guint    frame,
            gfloat   delta,
            gpointer user_data)
{
	lrg_world_adapter_update (LRG_WORLD_ADAPTER (user_data), delta);
	lrg_world_adapter_draw (LRG_WORLD_ADAPTER (user_data));
}

static void
test_draw (void)
{
	g_autoptr(LrgTiledWorld) world = lrg_tiled_world_new ();
	LrgWorldAdapter *adapter = LRG_WORLD_ADAPTER (world);
	g_autofree gchar *path = office_path ();
	g_autofree gchar *png = g_build_filename (g_get_tmp_dir (), "lrg-test-tiled-world.png", NULL);
	g_autoptr(GError) error = NULL;
	g_autoptr(GrlImage) shot = NULL;
	g_autoptr(GrlColor) actor = NULL;
	g_autoptr(GrlColor) floor = NULL;
	const gchar *display = g_getenv ("DISPLAY");

	if (display == NULL || *display == '\0')
	{
		g_test_skip ("no display: run under build-aux/xrun.sh");
		return;
	}
	g_assert_true (lrg_world_adapter_load (adapter, path, &error));
	lrg_world_adapter_spawn_actor (adapter, "kudu", "#ff8800", &error);
	/* the 160x128 map at zoom 1 */
	lrg_tiled_world_set_viewport (world, 160, 128);
	lrg_world_adapter_set_camera (adapter, LRG_WORLD_CAMERA_OVERVIEW);
	if (!lrg_screenshot_after_frames (png, 2, 160, 128, draw_world, world, &error))
	{
		g_test_skip (error->message);
		return;
	}
	shot = grl_image_new_from_file (png);
	g_assert_nonnull (shot);

	/* the actor's disc at the map centre, the floor tile (loaded from the
	 * tileset image beside the map) in an empty cell */
	actor = grl_image_get_pixel (shot, 80, 64);
	g_assert_cmpuint (actor->r, ==, 255);
	g_assert_cmpuint (actor->g, ==, 136);
	g_assert_cmpuint (actor->b, ==, 0);
	floor = grl_image_get_pixel (shot, 43, 43);
	g_assert_cmpuint (floor->r, ==, 200);
	g_assert_cmpuint (floor->g, ==, 200);
	g_assert_cmpuint (floor->b, ==, 200);
	g_unlink (png);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/world-adapter/unimplemented-refuses", test_unimplemented_refuses);
	g_test_add_func ("/world-adapter/tiled/office", test_office);
	g_test_add_func ("/world-adapter/tiled/seats-and-obstacles", test_seats_and_obstacles);
	g_test_add_func ("/world-adapter/tiled/draw", test_draw);

	return g_test_run ();
}
