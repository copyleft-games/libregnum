/* test-actor-mover.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for LrgActorMover: straight moves, waypoint chains, grid routes
 * around walls, blocked start and target cells, re-pathing when the way
 * closes, failures, facing and its sectors, turn rate, the x/z plane,
 * animation names and their signal.
 */

#include <glib.h>
#include <libregnum.h>
#include <math.h>

#define CELL (16.0f)

typedef struct
{
	guint      arrived;
	guint      failed;
	GPtrArray *anims;
} Counters;

static void
on_arrived (LrgActorMover *mover,
            gpointer       user_data)
{
	((Counters *) user_data)->arrived++;
}

static void
on_failed (LrgActorMover *mover,
           gpointer       user_data)
{
	((Counters *) user_data)->failed++;
}

static void
on_anim (LrgActorMover *mover,
         const gchar   *name,
         gpointer       user_data)
{
	g_ptr_array_add (((Counters *) user_data)->anims, g_strdup (name));
}

static void
watch (LrgActorMover *mover,
       Counters      *c)
{
	c->arrived = 0;
	c->failed = 0;
	c->anims = g_ptr_array_new_with_free_func (g_free);
	g_signal_connect (mover, "arrived", G_CALLBACK (on_arrived), c);
	g_signal_connect (mover, "path-failed", G_CALLBACK (on_failed), c);
	g_signal_connect (mover, "animation-changed", G_CALLBACK (on_anim), c);
}

/* Runs until arrival (or a bound), asserting after every step that the
 * route ahead is on walkable cells, except the final exact target. */
static guint
run_to_arrival (LrgActorMover *mover,
                LrgNavGrid    *grid,
                Counters      *c,
                guint          max_steps)
{
	guint steps = 0;

	while (c->arrived == 0 && c->failed == 0 && steps < max_steps)
	{
		guint n;
		guint i;

		lrg_actor_mover_update (mover, 1.0f / 60.0f);
		steps++;
		n = lrg_actor_mover_get_n_waypoints (mover);

		for (i = 0; grid != NULL && i + 1 < n; i++)
		{
			gfloat x;
			gfloat y;
			gfloat z;
			gint col;
			gint row;

			lrg_actor_mover_get_waypoint (mover, i, &x, &y, &z);
			lrg_actor_mover_world_to_cell (mover, x, y, z, &col, &row);
			g_assert_true (lrg_nav_grid_is_walkable (grid, col, row));
		}
	}
	return steps;
}

static void
test_straight (void)
{
	g_autoptr(LrgActorMover) mover = lrg_actor_mover_new ();
	Counters c;
	gfloat x;
	gfloat y;

	watch (mover, &c);
	g_assert_cmpstr (lrg_actor_mover_get_animation (mover), ==, "idle");
	lrg_actor_mover_set_speed (mover, 10.0f);
	g_assert_cmpfloat (lrg_actor_mover_get_speed (mover), ==, 10.0f);

	g_assert_true (lrg_actor_mover_move_to (mover, 30.0f, 40.0f, 0.0f, FALSE));
	g_assert_cmpint (lrg_actor_mover_get_state (mover), ==, LRG_ACTOR_MOVER_MOVING);
	g_assert_cmpstr (lrg_actor_mover_get_animation (mover), ==, "walk");
	g_assert_cmpfloat_with_epsilon (lrg_actor_mover_get_remaining_distance (mover), 50.0f, 1e-4f);

	lrg_actor_mover_update (mover, 2.0f);
	lrg_actor_mover_get_position (mover, &x, &y, NULL);
	g_assert_cmpfloat_with_epsilon (x, 12.0f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (y, 16.0f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (lrg_actor_mover_get_facing (mover), atan2f (40.0f, 30.0f), 1e-5f);
	g_assert_cmpuint (c.arrived, ==, 0);

	lrg_actor_mover_update (mover, 3.0f);
	lrg_actor_mover_get_position (mover, &x, &y, NULL);
	g_assert_cmpfloat (x, ==, 30.0f);
	g_assert_cmpfloat (y, ==, 40.0f);
	g_assert_cmpuint (c.arrived, ==, 1);
	g_assert_cmpint (lrg_actor_mover_get_state (mover), ==, LRG_ACTOR_MOVER_IDLE);
	g_assert_cmpuint (c.anims->len, ==, 2);
	g_assert_cmpstr (g_ptr_array_index (c.anims, 0), ==, "walk");
	g_assert_cmpstr (g_ptr_array_index (c.anims, 1), ==, "idle");

	/* nothing more happens when idle */
	lrg_actor_mover_update (mover, 1.0f);
	g_assert_cmpuint (c.arrived, ==, 1);
	g_ptr_array_unref (c.anims);
}

static void
test_waypoint_chain (void)
{
	g_autoptr(LrgActorMover) mover = lrg_actor_mover_new ();
	const gfloat route[] = { 10.0f, 0.0f, 0.0f, 10.0f, 10.0f, 0.0f, 0.0f, 10.0f, 2.0f };
	Counters c;
	gfloat x;
	gfloat y;
	gfloat z;

	watch (mover, &c);
	lrg_actor_mover_set_speed (mover, 100.0f);
	lrg_actor_mover_set_animation_names (mover, "run", NULL, "rest");
	lrg_actor_mover_follow_waypoints (mover, route, G_N_ELEMENTS (route), TRUE);
	g_assert_cmpuint (lrg_actor_mover_get_n_waypoints (mover), ==, 3);
	g_assert_cmpstr (lrg_actor_mover_get_animation (mover), ==, "run");

	/* 25 units: past two waypoints, 5 along the third leg */
	lrg_actor_mover_update (mover, 0.25f);
	lrg_actor_mover_get_position (mover, &x, &y, &z);
	g_assert_cmpuint (lrg_actor_mover_get_n_waypoints (mover), ==, 1);
	g_assert_cmpfloat_with_epsilon (x, 10.0f - 5.0f * 10.0f / sqrtf (104.0f), 1e-4f);
	g_assert_cmpfloat_with_epsilon (z, 5.0f * 2.0f / sqrtf (104.0f), 1e-4f);

	lrg_actor_mover_update (mover, 1.0f);
	g_assert_cmpuint (c.arrived, ==, 1);
	g_assert_cmpint (lrg_actor_mover_get_state (mover), ==, LRG_ACTOR_MOVER_SITTING);
	g_assert_cmpstr (lrg_actor_mover_get_animation (mover), ==, "rest");
	lrg_actor_mover_get_position (mover, &x, &y, &z);
	g_assert_cmpfloat (z, ==, 2.0f);

	lrg_actor_mover_stop (mover);
	g_assert_cmpstr (lrg_actor_mover_get_animation (mover), ==, "idle");
	lrg_actor_mover_sit (mover);
	g_assert_cmpint (lrg_actor_mover_get_state (mover), ==, LRG_ACTOR_MOVER_SITTING);
	g_ptr_array_unref (c.anims);
}

/* A 10x10 grid with a wall down column 5, open only at row 9. */
static LrgNavGrid *
walled_grid (void)
{
	LrgNavGrid *grid = lrg_nav_grid_new (10, 10);
	gint r;

	for (r = 0; r < 9; r++)
		lrg_nav_grid_set_blocked (grid, 5, r, TRUE);
	return grid;
}

static void
test_grid_route (void)
{
	g_autoptr(LrgNavGrid) grid = walled_grid ();
	g_autoptr(LrgPathfinder) pf = lrg_pathfinder_new (grid);
	g_autoptr(LrgActorMover) mover = lrg_actor_mover_new ();
	Counters c;
	gfloat x;
	gfloat y;
	gint col;
	gint row;

	watch (mover, &c);
	lrg_actor_mover_set_grid (mover, pf, CELL, CELL, 0.0f, 0.0f);
	lrg_actor_mover_set_speed (mover, 160.0f);
	lrg_actor_mover_set_position (mover, 24.0f, 24.0f, 0.0f);

	g_assert_true (lrg_actor_mover_move_to (mover, 140.0f, 20.0f, 0.0f, TRUE));
	g_assert_cmpuint (lrg_actor_mover_get_n_waypoints (mover), >, 2);
	/* the last waypoint is the exact target, not a cell centre */
	lrg_actor_mover_get_waypoint (mover, lrg_actor_mover_get_n_waypoints (mover) - 1, &x, &y, NULL);
	g_assert_cmpfloat (x, ==, 140.0f);
	g_assert_cmpfloat (y, ==, 20.0f);
	/* the way round goes through the gap at row 9 */
	g_assert_cmpfloat (lrg_actor_mover_get_remaining_distance (mover), >, 200.0f);

	g_assert_cmpuint (run_to_arrival (mover, grid, &c, 2000), <, 2000);
	g_assert_cmpuint (c.arrived, ==, 1);
	g_assert_cmpint (lrg_actor_mover_get_state (mover), ==, LRG_ACTOR_MOVER_SITTING);
	lrg_actor_mover_get_position (mover, &x, &y, NULL);
	g_assert_cmpfloat (x, ==, 140.0f);
	g_assert_cmpfloat (y, ==, 20.0f);
	lrg_actor_mover_world_to_cell (mover, x, y, 0.0f, &col, &row);
	g_assert_cmpint (col, ==, 8);
	g_assert_cmpint (row, ==, 1);
	g_ptr_array_unref (c.anims);
}

static void
test_blocked_start_and_target (void)
{
	g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (10, 10);
	g_autoptr(LrgPathfinder) pf = lrg_pathfinder_new (grid);
	g_autoptr(LrgActorMover) mover = lrg_actor_mover_new ();
	Counters c;
	gfloat x;
	gfloat y;
	gint col;
	gint row;

	/* a desk at (2,2) the actor sits at, a chair at (7,2) it walks to */
	lrg_nav_grid_set_blocked (grid, 2, 2, TRUE);
	lrg_nav_grid_set_blocked (grid, 7, 2, TRUE);

	watch (mover, &c);
	lrg_actor_mover_set_grid (mover, pf, CELL, CELL, 0.0f, 0.0f);
	lrg_actor_mover_set_position (mover, 40.0f, 40.0f, 0.0f);
	g_assert_true (lrg_actor_mover_move_to (mover, 120.0f, 40.0f, 0.0f, FALSE));

	/* leaves through the neighbour nearest the target: east */
	lrg_actor_mover_get_waypoint (mover, 0, &x, &y, NULL);
	lrg_actor_mover_world_to_cell (mover, x, y, 0.0f, &col, &row);
	g_assert_cmpint (col, ==, 3);
	g_assert_cmpint (row, ==, 2);
	/* enters the chair's cell from its west neighbour */
	lrg_actor_mover_get_waypoint (mover, lrg_actor_mover_get_n_waypoints (mover) - 2, &x, &y, NULL);
	lrg_actor_mover_world_to_cell (mover, x, y, 0.0f, &col, &row);
	g_assert_cmpint (col, ==, 6);
	g_assert_cmpint (row, ==, 2);

	run_to_arrival (mover, grid, &c, 2000);
	g_assert_cmpuint (c.arrived, ==, 1);
	lrg_actor_mover_get_position (mover, &x, &y, NULL);
	g_assert_cmpfloat (x, ==, 120.0f);
	g_ptr_array_unref (c.anims);
}

static void
test_repath_when_blocked (void)
{
	g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (10, 5);
	g_autoptr(LrgPathfinder) pf = lrg_pathfinder_new (grid);
	g_autoptr(LrgActorMover) mover = lrg_actor_mover_new ();
	Counters c;
	gfloat x;
	guint i;
	gint r;

	watch (mover, &c);
	lrg_actor_mover_set_grid (mover, pf, CELL, CELL, 0.0f, 0.0f);
	lrg_actor_mover_set_speed (mover, 32.0f);
	lrg_actor_mover_set_position (mover, 8.0f, 8.0f, 0.0f);
	g_assert_true (lrg_actor_mover_move_to (mover, 152.0f, 8.0f, 0.0f, FALSE));

	for (i = 0; i < 20; i++)
		lrg_actor_mover_update (mover, 1.0f / 60.0f);
	lrg_actor_mover_get_position (mover, &x, NULL, NULL);
	g_assert_cmpfloat (x, <, 40.0f);

	/* a door closes ahead: the route bends round it */
	lrg_nav_grid_set_blocked (grid, 4, 0, TRUE);
	lrg_nav_grid_set_blocked (grid, 4, 1, TRUE);
	run_to_arrival (mover, grid, &c, 4000);
	g_assert_cmpuint (c.arrived, ==, 1);
	g_assert_cmpuint (c.failed, ==, 0);

	/* walled off completely while walking: path-failed and blocked */
	c.arrived = 0;
	lrg_actor_mover_set_position (mover, 8.0f, 8.0f, 0.0f);
	g_assert_true (lrg_actor_mover_move_to (mover, 152.0f, 8.0f, 0.0f, FALSE));
	lrg_actor_mover_update (mover, 1.0f / 60.0f);
	for (r = 0; r < 5; r++)
		lrg_nav_grid_set_blocked (grid, 4, r, TRUE);
	run_to_arrival (mover, grid, &c, 4000);
	g_assert_cmpuint (c.failed, ==, 1);
	g_assert_cmpint (lrg_actor_mover_get_state (mover), ==, LRG_ACTOR_MOVER_BLOCKED);
	g_assert_cmpstr (lrg_actor_mover_get_animation (mover), ==, "idle");
	g_assert_cmpuint (lrg_actor_mover_get_n_waypoints (mover), ==, 0);

	/* and a move that cannot start fails at once */
	g_assert_false (lrg_actor_mover_move_to (mover, 152.0f, 8.0f, 0.0f, FALSE));
	g_assert_cmpuint (c.failed, ==, 2);
	/* outside the grid */
	g_assert_false (lrg_actor_mover_move_to (mover, -50.0f, 8.0f, 0.0f, FALSE));
	g_ptr_array_unref (c.anims);
}

static void
test_repath_interval (void)
{
	g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (10, 3);
	g_autoptr(LrgPathfinder) pf = lrg_pathfinder_new (grid);
	g_autoptr(LrgActorMover) mover = lrg_actor_mover_new ();
	Counters c;

	watch (mover, &c);
	lrg_actor_mover_set_grid (mover, pf, CELL, CELL, 0.0f, 0.0f);
	lrg_actor_mover_set_repath_interval (mover, 0.1f);
	lrg_actor_mover_set_position (mover, 8.0f, 24.0f, 0.0f);
	g_assert_true (lrg_actor_mover_move_to (mover, 152.0f, 24.0f, 0.0f, FALSE));
	g_assert_true (lrg_actor_mover_repath (mover));
	run_to_arrival (mover, grid, &c, 4000);
	g_assert_cmpuint (c.arrived, ==, 1);
	/* without a grid target there is nothing to re-path */
	g_assert_true (lrg_actor_mover_repath (mover));
	g_ptr_array_unref (c.anims);
}

static void
test_follow_path (void)
{
	g_autoptr(LrgActorMover) mover = lrg_actor_mover_new ();
	g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (4, 4);
	g_autoptr(LrgPathfinder) pf = lrg_pathfinder_new (grid);
	g_autoptr(LrgPath) path = lrg_path_new ();
	gfloat x;
	gfloat y;
	gfloat z;

	lrg_actor_mover_set_grid (mover, pf, 10.0f, 10.0f, 100.0f, 200.0f);
	lrg_actor_mover_set_position (mover, 105.0f, 205.0f, 7.0f);
	lrg_path_append (path, 1, 0);
	lrg_path_append (path, 1, 2);
	lrg_actor_mover_follow_path (mover, path, FALSE);
	g_assert_cmpuint (lrg_actor_mover_get_n_waypoints (mover), ==, 2);
	lrg_actor_mover_get_waypoint (mover, 1, &x, &y, &z);
	g_assert_cmpfloat (x, ==, 115.0f);
	g_assert_cmpfloat (y, ==, 225.0f);
	g_assert_cmpfloat (z, ==, 7.0f);
	g_assert_false (lrg_actor_mover_get_waypoint (mover, 2, NULL, NULL, NULL));
}

static void
test_facing (void)
{
	g_autoptr(LrgActorMover) mover = lrg_actor_mover_new ();

	lrg_actor_mover_set_facing (mover, 0.0f);
	g_assert_cmpuint (lrg_actor_mover_get_facing_sector (mover, 4), ==, 0);
	lrg_actor_mover_set_facing (mover, (gfloat) G_PI_2);
	g_assert_cmpuint (lrg_actor_mover_get_facing_sector (mover, 4), ==, 1);
	lrg_actor_mover_set_facing (mover, (gfloat) G_PI);
	g_assert_cmpuint (lrg_actor_mover_get_facing_sector (mover, 4), ==, 2);
	lrg_actor_mover_set_facing (mover, -(gfloat) G_PI_2);
	g_assert_cmpuint (lrg_actor_mover_get_facing_sector (mover, 4), ==, 3);
	lrg_actor_mover_set_facing (mover, -0.3f);
	g_assert_cmpuint (lrg_actor_mover_get_facing_sector (mover, 4), ==, 0);
	g_assert_cmpuint (lrg_actor_mover_get_facing_sector (mover, 8), ==, 0);
	lrg_actor_mover_set_facing (mover, (gfloat) G_PI / 4.0f);
	g_assert_cmpuint (lrg_actor_mover_get_facing_sector (mover, 8), ==, 1);
	/* angles wrap into (-pi, pi] */
	lrg_actor_mover_set_facing (mover, 3.0f * (gfloat) G_PI);
	g_assert_cmpfloat_with_epsilon (lrg_actor_mover_get_facing (mover), (gfloat) G_PI, 1e-5f);

	/* a turn rate turns gradually */
	lrg_actor_mover_set_facing (mover, 0.0f);
	lrg_actor_mover_set_turn_speed (mover, (gfloat) G_PI);
	lrg_actor_mover_set_speed (mover, 1.0f);
	lrg_actor_mover_move_to (mover, -100.0f, 0.0f, 0.0f, FALSE);
	lrg_actor_mover_update (mover, 0.5f);
	g_assert_cmpfloat_with_epsilon (fabsf (lrg_actor_mover_get_facing (mover)), (gfloat) G_PI_2, 1e-4f);
	lrg_actor_mover_update (mover, 0.5f);
	g_assert_cmpfloat_with_epsilon (fabsf (lrg_actor_mover_get_facing (mover)), (gfloat) G_PI, 1e-4f);
}

static void
test_xz_plane (void)
{
	g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (8, 8);
	g_autoptr(LrgPathfinder) pf = lrg_pathfinder_new (grid);
	g_autoptr(LrgActorMover) mover = lrg_actor_mover_new ();
	g_autoptr(LrgNavAgent) agent = lrg_nav_agent_new ();
	Counters c;
	gfloat x;
	gfloat y;
	gfloat z;
	guint i;
	gint col;
	gint row;

	watch (mover, &c);
	lrg_actor_mover_set_plane (mover, LRG_ACTOR_MOVER_PLANE_XZ);
	g_assert_cmpint (lrg_actor_mover_get_plane (mover), ==, LRG_ACTOR_MOVER_PLANE_XZ);
	lrg_actor_mover_set_grid (mover, pf, 2.0f, 2.0f, -8.0f, -8.0f);
	lrg_actor_mover_set_speed (mover, 4.0f);
	lrg_actor_mover_set_position (mover, -7.0f, 3.0f, -7.0f);
	lrg_actor_mover_world_to_cell (mover, -7.0f, 99.0f, 5.0f, &col, &row);
	g_assert_cmpint (col, ==, 0);
	g_assert_cmpint (row, ==, 6);

	g_assert_true (lrg_actor_mover_move_to (mover, 7.0f, 3.0f, -7.0f, FALSE));
	for (i = 0; i < lrg_actor_mover_get_n_waypoints (mover); i++)
	{
		lrg_actor_mover_get_waypoint (mover, i, NULL, &y, NULL);
		g_assert_cmpfloat (y, ==, 3.0f);
	}
	lrg_actor_mover_update (mover, 0.5f);
	/* facing +x on the x/z plane is 0 */
	g_assert_cmpfloat_with_epsilon (lrg_actor_mover_get_facing (mover), 0.0f, 1e-5f);
	run_to_arrival (mover, grid, &c, 2000);
	lrg_actor_mover_get_position (mover, &x, &y, &z);
	g_assert_cmpfloat (x, ==, 7.0f);
	g_assert_cmpfloat (y, ==, 3.0f);

	/* an agent with no path gives nothing to follow */
	g_assert_false (lrg_actor_mover_follow_nav_agent (mover, agent, FALSE));
	g_ptr_array_unref (c.anims);
}

static void
test_deterministic (void)
{
	g_autoptr(LrgNavGrid) grid = walled_grid ();
	g_autoptr(LrgPathfinder) pf = lrg_pathfinder_new (grid);
	g_autoptr(LrgActorMover) a = lrg_actor_mover_new ();
	g_autoptr(LrgActorMover) b = lrg_actor_mover_new ();
	guint i;

	lrg_actor_mover_set_grid (a, pf, CELL, CELL, 0.0f, 0.0f);
	lrg_actor_mover_set_grid (b, pf, CELL, CELL, 0.0f, 0.0f);
	lrg_actor_mover_set_position (a, 24.0f, 24.0f, 0.0f);
	lrg_actor_mover_set_position (b, 24.0f, 24.0f, 0.0f);
	lrg_actor_mover_move_to (a, 140.0f, 20.0f, 0.0f, FALSE);
	lrg_actor_mover_move_to (b, 140.0f, 20.0f, 0.0f, FALSE);
	for (i = 0; i < 300; i++)
	{
		gfloat ax;
		gfloat ay;
		gfloat bx;
		gfloat by;

		lrg_actor_mover_update (a, 1.0f / 60.0f);
		lrg_actor_mover_update (b, 1.0f / 60.0f);
		lrg_actor_mover_get_position (a, &ax, &ay, NULL);
		lrg_actor_mover_get_position (b, &bx, &by, NULL);
		g_assert_cmpfloat (ax, ==, bx);
		g_assert_cmpfloat (ay, ==, by);
	}
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/actor-mover/straight", test_straight);
	g_test_add_func ("/actor-mover/waypoint-chain", test_waypoint_chain);
	g_test_add_func ("/actor-mover/grid-route", test_grid_route);
	g_test_add_func ("/actor-mover/blocked-start-and-target", test_blocked_start_and_target);
	g_test_add_func ("/actor-mover/repath-when-blocked", test_repath_when_blocked);
	g_test_add_func ("/actor-mover/repath-interval", test_repath_interval);
	g_test_add_func ("/actor-mover/follow-path", test_follow_path);
	g_test_add_func ("/actor-mover/facing", test_facing);
	g_test_add_func ("/actor-mover/xz-plane", test_xz_plane);
	g_test_add_func ("/actor-mover/deterministic", test_deterministic);

	return g_test_run ();
}
