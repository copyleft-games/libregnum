/* test-pathfinding.c
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the Pathfinding module.
 */

#include <glib.h>
#include <math.h>
#include <libregnum.h>

/* ========================================================================== */
/* LrgNavCell Tests                                                           */
/* ========================================================================== */

static void
test_nav_cell_new (void)
{
    g_autoptr(LrgNavCell) cell = lrg_nav_cell_new (5, 10, 1.5f, LRG_NAV_CELL_NONE);

    g_assert_nonnull (cell);
    g_assert_cmpint (lrg_nav_cell_get_x (cell), ==, 5);
    g_assert_cmpint (lrg_nav_cell_get_y (cell), ==, 10);
    g_assert_cmpfloat (lrg_nav_cell_get_cost (cell), ==, 1.5f);
    g_assert_cmpuint (lrg_nav_cell_get_flags (cell), ==, LRG_NAV_CELL_NONE);
}

static void
test_nav_cell_copy (void)
{
    g_autoptr(LrgNavCell) original = lrg_nav_cell_new (3, 7, 2.0f, LRG_NAV_CELL_BLOCKED);
    g_autoptr(LrgNavCell) copy = lrg_nav_cell_copy (original);

    g_assert_nonnull (copy);
    g_assert_true (copy != original);
    g_assert_cmpint (lrg_nav_cell_get_x (copy), ==, 3);
    g_assert_cmpint (lrg_nav_cell_get_y (copy), ==, 7);
    g_assert_cmpfloat (lrg_nav_cell_get_cost (copy), ==, 2.0f);
    g_assert_cmpuint (lrg_nav_cell_get_flags (copy), ==, LRG_NAV_CELL_BLOCKED);
}

static void
test_nav_cell_flags (void)
{
    g_autoptr(LrgNavCell) cell = lrg_nav_cell_new (0, 0, 1.0f, LRG_NAV_CELL_NONE);

    g_assert_true (lrg_nav_cell_is_walkable (cell));
    g_assert_false (lrg_nav_cell_has_flag (cell, LRG_NAV_CELL_BLOCKED));

    lrg_nav_cell_set_flags (cell, LRG_NAV_CELL_BLOCKED);
    g_assert_false (lrg_nav_cell_is_walkable (cell));
    g_assert_true (lrg_nav_cell_has_flag (cell, LRG_NAV_CELL_BLOCKED));
}

static void
test_nav_cell_cost (void)
{
    g_autoptr(LrgNavCell) cell = lrg_nav_cell_new (0, 0, 1.0f, LRG_NAV_CELL_NONE);

    g_assert_cmpfloat (lrg_nav_cell_get_cost (cell), ==, 1.0f);

    lrg_nav_cell_set_cost (cell, 3.5f);
    g_assert_cmpfloat (lrg_nav_cell_get_cost (cell), ==, 3.5f);
}

/* ========================================================================== */
/* LrgPath Tests                                                              */
/* ========================================================================== */

static void
test_path_new (void)
{
    g_autoptr(LrgPath) path = lrg_path_new ();

    g_assert_nonnull (path);
    g_assert_true (lrg_path_is_empty (path));
    g_assert_cmpuint (lrg_path_get_length (path), ==, 0);
}

static void
test_path_append_prepend (void)
{
    g_autoptr(LrgPath) path = lrg_path_new ();
    gint x, y;

    lrg_path_append (path, 0, 0);
    lrg_path_append (path, 1, 1);
    lrg_path_prepend (path, -1, -1);

    g_assert_cmpuint (lrg_path_get_length (path), ==, 3);

    g_assert_true (lrg_path_get_start (path, &x, &y));
    g_assert_cmpint (x, ==, -1);
    g_assert_cmpint (y, ==, -1);

    g_assert_true (lrg_path_get_end (path, &x, &y));
    g_assert_cmpint (x, ==, 1);
    g_assert_cmpint (y, ==, 1);
}

static void
test_path_get_point (void)
{
    g_autoptr(LrgPath) path = lrg_path_new ();
    gint x, y;

    lrg_path_append (path, 5, 10);
    lrg_path_append (path, 15, 20);

    g_assert_true (lrg_path_get_point (path, 0, &x, &y));
    g_assert_cmpint (x, ==, 5);
    g_assert_cmpint (y, ==, 10);

    g_assert_true (lrg_path_get_point (path, 1, &x, &y));
    g_assert_cmpint (x, ==, 15);
    g_assert_cmpint (y, ==, 20);

    g_assert_false (lrg_path_get_point (path, 2, &x, &y));
}

static void
test_path_reverse (void)
{
    g_autoptr(LrgPath) path = lrg_path_new ();
    gint x, y;

    lrg_path_append (path, 0, 0);
    lrg_path_append (path, 1, 1);
    lrg_path_append (path, 2, 2);

    lrg_path_reverse (path);

    g_assert_true (lrg_path_get_point (path, 0, &x, &y));
    g_assert_cmpint (x, ==, 2);
    g_assert_cmpint (y, ==, 2);

    g_assert_true (lrg_path_get_point (path, 2, &x, &y));
    g_assert_cmpint (x, ==, 0);
    g_assert_cmpint (y, ==, 0);
}

static void
test_path_copy (void)
{
    g_autoptr(LrgPath) original = lrg_path_new ();
    g_autoptr(LrgPath) copy = NULL;
    gint x, y;

    lrg_path_append (original, 1, 2);
    lrg_path_append (original, 3, 4);
    lrg_path_set_total_cost (original, 5.5f);

    copy = lrg_path_copy (original);

    g_assert_nonnull (copy);
    g_assert_cmpuint (lrg_path_get_length (copy), ==, 2);
    g_assert_cmpfloat (lrg_path_get_total_cost (copy), ==, 5.5f);

    g_assert_true (lrg_path_get_point (copy, 0, &x, &y));
    g_assert_cmpint (x, ==, 1);
    g_assert_cmpint (y, ==, 2);
}

/* ========================================================================== */
/* LrgNavGrid Tests                                                           */
/* ========================================================================== */

static void
test_nav_grid_new (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (10, 15);

    g_assert_nonnull (grid);
    g_assert_cmpuint (lrg_nav_grid_get_width (grid), ==, 10);
    g_assert_cmpuint (lrg_nav_grid_get_height (grid), ==, 15);
}

static void
test_nav_grid_is_valid (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (10, 10);

    g_assert_true (lrg_nav_grid_is_valid (grid, 0, 0));
    g_assert_true (lrg_nav_grid_is_valid (grid, 9, 9));
    g_assert_true (lrg_nav_grid_is_valid (grid, 5, 5));

    g_assert_false (lrg_nav_grid_is_valid (grid, -1, 0));
    g_assert_false (lrg_nav_grid_is_valid (grid, 0, -1));
    g_assert_false (lrg_nav_grid_is_valid (grid, 10, 0));
    g_assert_false (lrg_nav_grid_is_valid (grid, 0, 10));
}

static void
test_nav_grid_get_cell (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (5, 5);
    LrgNavCell *cell;

    cell = lrg_nav_grid_get_cell (grid, 2, 3);
    g_assert_nonnull (cell);
    g_assert_cmpint (lrg_nav_cell_get_x (cell), ==, 2);
    g_assert_cmpint (lrg_nav_cell_get_y (cell), ==, 3);

    cell = lrg_nav_grid_get_cell (grid, 100, 100);
    g_assert_null (cell);
}

static void
test_nav_grid_blocked (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (5, 5);

    g_assert_true (lrg_nav_grid_is_walkable (grid, 2, 2));

    lrg_nav_grid_set_blocked (grid, 2, 2, TRUE);
    g_assert_false (lrg_nav_grid_is_walkable (grid, 2, 2));

    lrg_nav_grid_set_blocked (grid, 2, 2, FALSE);
    g_assert_true (lrg_nav_grid_is_walkable (grid, 2, 2));
}

static void
test_nav_grid_cell_cost (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (5, 5);

    g_assert_cmpfloat (lrg_nav_grid_get_cell_cost (grid, 2, 2), ==, 1.0f);

    lrg_nav_grid_set_cell_cost (grid, 2, 2, 3.0f);
    g_assert_cmpfloat (lrg_nav_grid_get_cell_cost (grid, 2, 2), ==, 3.0f);
}

static void
test_nav_grid_diagonal (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (5, 5);

    g_assert_true (lrg_nav_grid_get_allow_diagonal (grid));
    g_assert_false (lrg_nav_grid_get_cut_corners (grid));

    lrg_nav_grid_set_allow_diagonal (grid, FALSE);
    g_assert_false (lrg_nav_grid_get_allow_diagonal (grid));

    lrg_nav_grid_set_cut_corners (grid, TRUE);
    g_assert_true (lrg_nav_grid_get_cut_corners (grid));
}

static void
test_nav_grid_fill_rect (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (10, 10);

    lrg_nav_grid_fill_rect (grid, 2, 2, 3, 3, LRG_NAV_CELL_BLOCKED, 2.0f);

    g_assert_false (lrg_nav_grid_is_walkable (grid, 2, 2));
    g_assert_false (lrg_nav_grid_is_walkable (grid, 4, 4));
    g_assert_true (lrg_nav_grid_is_walkable (grid, 1, 1));
    g_assert_true (lrg_nav_grid_is_walkable (grid, 5, 5));
}

/* ========================================================================== */
/* LrgPathfinder Tests                                                        */
/* ========================================================================== */

static void
test_pathfinder_new (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (10, 10);
    g_autoptr(LrgPathfinder) pathfinder = lrg_pathfinder_new (grid);

    g_assert_nonnull (pathfinder);
    g_assert_true (lrg_pathfinder_get_grid (pathfinder) == grid);
}

static void
test_pathfinder_simple_path (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (10, 10);
    g_autoptr(LrgPathfinder) pathfinder = lrg_pathfinder_new (grid);
    g_autoptr(LrgPath) path = NULL;
    GError *error = NULL;
    gint x, y;

    /* Find path from (0,0) to (5,5) */
    path = lrg_pathfinder_find_path (pathfinder, 0, 0, 5, 5, &error);

    g_assert_no_error (error);
    g_assert_nonnull (path);
    g_assert_false (lrg_path_is_empty (path));

    /* Path should start at (0,0) */
    g_assert_true (lrg_path_get_start (path, &x, &y));
    g_assert_cmpint (x, ==, 0);
    g_assert_cmpint (y, ==, 0);

    /* Path should end at (5,5) */
    g_assert_true (lrg_path_get_end (path, &x, &y));
    g_assert_cmpint (x, ==, 5);
    g_assert_cmpint (y, ==, 5);
}

static void
test_pathfinder_smoothing_preserves_cost (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (5, 1);
    g_autoptr(LrgPathfinder) pathfinder = lrg_pathfinder_new (grid);
    g_autoptr(LrgPath) path = NULL;
    g_autoptr(LrgPath) smoothed = NULL;
    g_autoptr(GError) error = NULL;
    gint x, y;

    lrg_nav_grid_set_cell_cost (grid, 2, 0, 3.0f);
    path = lrg_pathfinder_find_path (pathfinder, 0, 0, 4, 0, &error);
    g_assert_no_error (error);
    g_assert_nonnull (path);
    g_assert_cmpfloat (lrg_path_get_total_cost (path), ==, 6.0f);

    lrg_pathfinder_set_smoothing (pathfinder, LRG_PATH_SMOOTHING_SIMPLE);
    smoothed = lrg_pathfinder_find_path (pathfinder, 0, 0, 4, 0, &error);
    g_assert_no_error (error);
    g_assert_nonnull (smoothed);
    g_assert_cmpuint (lrg_path_get_length (smoothed), ==, 2);
    g_assert_cmpfloat (lrg_path_get_total_cost (smoothed), ==,
                       lrg_path_get_total_cost (path));
    g_assert_true (lrg_path_get_start (smoothed, &x, &y));
    g_assert_cmpint (x, ==, 0);
    g_assert_cmpint (y, ==, 0);
    g_assert_true (lrg_path_get_end (smoothed, &x, &y));
    g_assert_cmpint (x, ==, 4);
    g_assert_cmpint (y, ==, 0);
}

static void
test_pathfinder_weighted_optimal (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (3, 2);
    g_autoptr(LrgPathfinder) finder = lrg_pathfinder_new (grid);
    g_autoptr(LrgPath) path = NULL;
    gint x;

    lrg_nav_grid_set_allow_diagonal (grid, FALSE);
    for (x = 0; x < 3; x++)
        lrg_nav_grid_set_cell_cost (grid, x, 1, 0.1f);
    path = lrg_pathfinder_find_path (finder, 0, 0, 2, 0, NULL);
    g_assert_nonnull (path);
    g_assert_cmpfloat_with_epsilon (lrg_path_get_total_cost (path), 1.3f, 0.0001f);

    /* Recompute the bound after mutable cell costs change, including zero. */
    g_clear_pointer (&path, lrg_path_free);
    for (x = 0; x < 3; x++)
        lrg_nav_grid_set_cell_cost (grid, x, 1, 0.0f);
    path = lrg_pathfinder_find_path (finder, 0, 0, 2, 0, NULL);
    g_assert_nonnull (path);
    g_assert_cmpfloat (lrg_path_get_total_cost (path), ==, 1.0f);
}

static void
test_pathfinder_diagonal_optimal (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (5, 5);
    g_autoptr(LrgPathfinder) finder = lrg_pathfinder_new (grid);
    g_autoptr(LrgPath) path = NULL;
    const gint walls[][2] = { {2, 0}, {4, 1}, {2, 2}, {0, 3}, {4, 3} };
    guint i;

    lrg_nav_grid_set_allow_diagonal (grid, TRUE);
    for (i = 0; i < G_N_ELEMENTS (walls); i++)
        lrg_nav_grid_set_blocked (grid, walls[i][0], walls[i][1], TRUE);
    path = lrg_pathfinder_find_path (finder, 0, 0, 4, 4, NULL);
    g_assert_nonnull (path);
    g_assert_cmpfloat_with_epsilon (lrg_path_get_total_cost (path),
                                    4.0f + 2.0f * sqrtf (2.0f), 0.0001f);
}

static void
test_pathfinder_same_start_end (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (10, 10);
    g_autoptr(LrgPathfinder) pathfinder = lrg_pathfinder_new (grid);
    g_autoptr(LrgPath) path = NULL;
    GError *error = NULL;

    path = lrg_pathfinder_find_path (pathfinder, 5, 5, 5, 5, &error);

    g_assert_no_error (error);
    g_assert_nonnull (path);
    g_assert_cmpuint (lrg_path_get_length (path), ==, 1);
    g_assert_cmpfloat (lrg_path_get_total_cost (path), ==, 0.0f);
}

static void
test_pathfinder_blocked_path (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (5, 5);
    g_autoptr(LrgPathfinder) pathfinder = lrg_pathfinder_new (grid);
    g_autoptr(LrgPath) path = NULL;
    GError *error = NULL;
    gint i;

    /* Create a wall blocking the path */
    for (i = 0; i < 5; i++)
        lrg_nav_grid_set_blocked (grid, 2, i, TRUE);

    path = lrg_pathfinder_find_path (pathfinder, 0, 2, 4, 2, &error);

    g_assert_error (error, LRG_PATHFINDING_ERROR, LRG_PATHFINDING_ERROR_NO_PATH);
    g_assert_null (path);
    g_clear_error (&error);
}

static void
test_pathfinder_around_obstacle (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (10, 10);
    g_autoptr(LrgPathfinder) pathfinder = lrg_pathfinder_new (grid);
    g_autoptr(LrgPath) path = NULL;
    GError *error = NULL;
    gint x, y;
    gint i;

    /* Create partial wall */
    for (i = 0; i < 8; i++)
        lrg_nav_grid_set_blocked (grid, 5, i, TRUE);

    path = lrg_pathfinder_find_path (pathfinder, 0, 5, 9, 5, &error);

    g_assert_no_error (error);
    g_assert_nonnull (path);

    /* Verify start and end */
    g_assert_true (lrg_path_get_start (path, &x, &y));
    g_assert_cmpint (x, ==, 0);
    g_assert_cmpint (y, ==, 5);

    g_assert_true (lrg_path_get_end (path, &x, &y));
    g_assert_cmpint (x, ==, 9);
    g_assert_cmpint (y, ==, 5);
}

static void
test_pathfinder_invalid_positions (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (10, 10);
    g_autoptr(LrgPathfinder) pathfinder = lrg_pathfinder_new (grid);
    g_autoptr(LrgPath) path = NULL;
    GError *error = NULL;

    /* Invalid start */
    path = lrg_pathfinder_find_path (pathfinder, -1, 0, 5, 5, &error);
    g_assert_error (error, LRG_PATHFINDING_ERROR, LRG_PATHFINDING_ERROR_INVALID_START);
    g_assert_null (path);
    g_clear_error (&error);

    /* Invalid goal */
    path = lrg_pathfinder_find_path (pathfinder, 0, 0, 100, 100, &error);
    g_assert_error (error, LRG_PATHFINDING_ERROR, LRG_PATHFINDING_ERROR_INVALID_GOAL);
    g_assert_null (path);
    g_clear_error (&error);
}

static void
test_pathfinder_blocked_start_end (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (10, 10);
    g_autoptr(LrgPathfinder) pathfinder = lrg_pathfinder_new (grid);
    g_autoptr(LrgPath) path = NULL;
    GError *error = NULL;

    /* Block start */
    lrg_nav_grid_set_blocked (grid, 0, 0, TRUE);
    path = lrg_pathfinder_find_path (pathfinder, 0, 0, 5, 5, &error);
    g_assert_error (error, LRG_PATHFINDING_ERROR, LRG_PATHFINDING_ERROR_INVALID_START);
    g_assert_null (path);
    g_clear_error (&error);

    /* Unblock start, block end */
    lrg_nav_grid_set_blocked (grid, 0, 0, FALSE);
    lrg_nav_grid_set_blocked (grid, 5, 5, TRUE);
    path = lrg_pathfinder_find_path (pathfinder, 0, 0, 5, 5, &error);
    g_assert_error (error, LRG_PATHFINDING_ERROR, LRG_PATHFINDING_ERROR_INVALID_GOAL);
    g_assert_null (path);
    g_clear_error (&error);
}

static void
test_pathfinder_no_grid (void)
{
    g_autoptr(LrgPathfinder) pathfinder = lrg_pathfinder_new (NULL);
    g_autoptr(LrgPath) path = NULL;
    GError *error = NULL;

    path = lrg_pathfinder_find_path (pathfinder, 0, 0, 5, 5, &error);
    g_assert_error (error, LRG_PATHFINDING_ERROR, LRG_PATHFINDING_ERROR_NO_GRID);
    g_assert_null (path);
    g_clear_error (&error);
}

static void
test_pathfinder_is_reachable (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (10, 10);
    g_autoptr(LrgPathfinder) pathfinder = lrg_pathfinder_new (grid);
    gint i;

    g_assert_true (lrg_pathfinder_is_reachable (pathfinder, 0, 0, 9, 9));

    /* Block with wall */
    for (i = 0; i < 10; i++)
        lrg_nav_grid_set_blocked (grid, 5, i, TRUE);

    g_assert_false (lrg_pathfinder_is_reachable (pathfinder, 0, 0, 9, 9));
}

static void
test_pathfinder_nodes_explored (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (10, 10);
    g_autoptr(LrgPathfinder) pathfinder = lrg_pathfinder_new (grid);
    g_autoptr(LrgPath) path = NULL;

    path = lrg_pathfinder_find_path (pathfinder, 0, 0, 5, 5, NULL);

    g_assert_nonnull (path);
    g_assert_cmpuint (lrg_pathfinder_get_last_nodes_explored (pathfinder), >, 0);
}

static void
test_pathfinder_cardinal_only (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (5, 5);
    g_autoptr(LrgPathfinder) pathfinder = lrg_pathfinder_new (grid);
    g_autoptr(LrgPath) path = NULL;
    guint i;
    gint x1, y1, x2, y2;

    lrg_nav_grid_set_allow_diagonal (grid, FALSE);

    path = lrg_pathfinder_find_path (pathfinder, 0, 0, 2, 2, NULL);

    g_assert_nonnull (path);

    /* Verify no diagonal moves */
    for (i = 1; i < lrg_path_get_length (path); i++)
    {
        gint dx, dy;

        lrg_path_get_point (path, i - 1, &x1, &y1);
        lrg_path_get_point (path, i, &x2, &y2);

        dx = abs (x2 - x1);
        dy = abs (y2 - y1);

        /* Cardinal moves only: either dx=1,dy=0 or dx=0,dy=1 */
        g_assert_true ((dx == 1 && dy == 0) || (dx == 0 && dy == 1));
    }
}

/* ========================================================================== */
/* Heuristic Tests                                                            */
/* ========================================================================== */

static void
test_heuristics (void)
{
    gfloat manhattan, euclidean, chebyshev, octile;

    manhattan = lrg_heuristic_manhattan (0, 0, 3, 4, NULL);
    g_assert_cmpfloat (manhattan, ==, 7.0f);

    euclidean = lrg_heuristic_euclidean (0, 0, 3, 4, NULL);
    g_assert_cmpfloat_with_epsilon (euclidean, 5.0f, 0.001f);

    chebyshev = lrg_heuristic_chebyshev (0, 0, 3, 4, NULL);
    g_assert_cmpfloat (chebyshev, ==, 4.0f);

    octile = lrg_heuristic_octile (0, 0, 3, 4, NULL);
    g_assert_cmpfloat (octile, >, chebyshev);
    g_assert_cmpfloat (octile, <, manhattan);
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

/* This lower bound is admissible but drops abruptly along vertical edges. */
static gfloat
inconsistent_heuristic (gint     x1,
                        gint     y1,
                        gint     x2,
                        gint     y2,
                        gpointer data)
{
    return y1 == 1 ? (gfloat)(4 - x1) : 0.0f;
}

static void
test_pathfinder_reopen (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (4, 2);
    g_autoptr(LrgPathfinder) finder = lrg_pathfinder_new (grid);
    g_autoptr(LrgPath) path = NULL;

    lrg_nav_grid_set_allow_diagonal (grid, FALSE);
    lrg_nav_grid_set_blocked (grid, 3, 1, TRUE);
    lrg_nav_grid_set_cell_cost (grid, 1, 0, 3.5f);
    lrg_pathfinder_set_heuristic (finder, inconsistent_heuristic, NULL, NULL);
    path = lrg_pathfinder_find_path (finder, 0, 0, 3, 0, NULL);
    g_assert_nonnull (path);
    /* The upper route costs 5.5; the lower route must reopen (2, 0). */
    g_assert_cmpfloat (lrg_path_get_total_cost (path), ==, 5.0f);
    g_assert_cmpuint (lrg_path_get_length (path), ==, 6);
}

static void
test_pathfinder_iteration_limit (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (3, 1);
    g_autoptr(LrgPathfinder) finder = lrg_pathfinder_new (grid);
    g_autoptr(LrgPath) path = NULL;
    g_autoptr(GError) error = NULL;

    lrg_pathfinder_set_max_iterations (finder, 2);
    path = lrg_pathfinder_find_path (finder, 0, 0, 2, 0, &error);
    g_assert_null (path);
    g_assert_error (error, LRG_PATHFINDING_ERROR, LRG_PATHFINDING_ERROR_ITERATION_LIMIT);
    g_assert_cmpuint (lrg_pathfinder_get_last_nodes_explored (finder), ==, 2);
    g_clear_error (&error);

    /* Reaching the goal on the final permitted expansion succeeds. */
    lrg_pathfinder_set_max_iterations (finder, 3);
    path = lrg_pathfinder_find_path (finder, 0, 0, 2, 0, &error);
    g_assert_no_error (error);
    g_assert_nonnull (path);
    g_clear_pointer (&path, lrg_path_free);

    /* Exhausting the open set exactly at the limit proves no path exists. */
    lrg_nav_grid_set_blocked (grid, 1, 0, TRUE);
    lrg_pathfinder_set_max_iterations (finder, 1);
    path = lrg_pathfinder_find_path (finder, 0, 0, 2, 0, &error);
    g_assert_null (path);
    g_assert_error (error, LRG_PATHFINDING_ERROR, LRG_PATHFINDING_ERROR_NO_PATH);
    g_clear_error (&error);

    path = lrg_pathfinder_find_path (finder, 0, 0, 0, 0, &error);
    g_assert_no_error (error);
    g_assert_nonnull (path);
    g_assert_cmpuint (lrg_pathfinder_get_last_nodes_explored (finder), ==, 0);
}

static void
test_pathfinding_error_enum (void)
{
    GEnumClass *klass = g_type_class_ref (LRG_TYPE_PATHFINDING_ERROR);
    gint code;

    for (code = LRG_PATHFINDING_ERROR_FAILED;
         code <= LRG_PATHFINDING_ERROR_ITERATION_LIMIT; code++)
        g_assert_nonnull (g_enum_get_value (klass, code));
    g_assert_cmpint (g_enum_get_value_by_nick (klass, "iteration-limit")->value,
                    ==, LRG_PATHFINDING_ERROR_ITERATION_LIMIT);
    g_type_class_unref (klass);
}

static void
test_heuristics_large_coordinates (void)
{
    LrgHeuristicFunc funcs[] = { lrg_heuristic_manhattan, lrg_heuristic_euclidean,
                                lrg_heuristic_chebyshev, lrg_heuristic_octile };
    gdouble span = (gdouble)G_MAXINT - G_MININT;
    guint i;

    for (i = 0; i < G_N_ELEMENTS (funcs); i++)
    {
        gfloat forward = funcs[i] (G_MININT, 0, G_MAXINT, 0, NULL);
        gfloat reverse = funcs[i] (G_MAXINT, 0, G_MININT, 0, NULL);

        g_assert_cmpfloat_with_epsilon (forward, (gfloat)span, 1.0f);
        g_assert_cmpfloat (forward, ==, reverse);
        g_assert_cmpfloat (funcs[i] (G_MAXINT, G_MININT, G_MAXINT, G_MININT, NULL), ==, 0.0f);
    }
    g_assert_cmpfloat_with_epsilon (lrg_heuristic_euclidean (0, 0, 60000, 80000, NULL),
                                    100000.0f, 0.01f);
    g_assert_cmpfloat_with_epsilon (lrg_heuristic_manhattan (G_MININT, G_MININT,
                                    G_MAXINT, G_MAXINT, NULL), (gfloat)(2 * span), 1.0f);
    g_assert_cmpfloat_with_epsilon (lrg_heuristic_euclidean (G_MININT, G_MININT,
                                    G_MAXINT, G_MAXINT, NULL), (gfloat)(sqrt (2.0) * span), 1024.0f);
}

/* Independent O(V^2) Dijkstra oracle: no engine queue, heuristic, or neighbors. */
static gfloat
reference_cost (LrgNavGrid *grid)
{
    gfloat distances[64];
    gboolean visited[64] = { FALSE };
    const gint dx[] = { -1, 1, 0, 0 };
    const gint dy[] = { 0, 0, -1, 1 };
    guint i;

    for (i = 0; i < 64; i++)
        distances[i] = G_MAXFLOAT;
    distances[0] = 0.0f;
    for (;;)
    {
        gint best = -1;
        guint direction;

        for (i = 0; i < 64; i++)
            if (!visited[i] && distances[i] < G_MAXFLOAT &&
                (best < 0 || distances[i] < distances[best]))
                best = i;
        if (best < 0)
            return G_MAXFLOAT;
        if (best == 63)
            return distances[best];
        visited[best] = TRUE;
        for (direction = 0; direction < 4; direction++)
        {
            gint x = best % 8 + dx[direction];
            gint y = best / 8 + dy[direction];
            gfloat cost;

            if (x < 0 || x >= 8 || y < 0 || y >= 8 ||
                !lrg_nav_grid_is_walkable (grid, x, y))
                continue;
            cost = distances[best] + lrg_nav_grid_get_cell_cost (grid, x, y);
            if (cost < distances[y * 8 + x])
                distances[y * 8 + x] = cost;
        }
    }
}

static void
test_pathfinder_reference_maps (void)
{
    GRand *random = g_rand_new_with_seed (0x51a7);
    guint map;

    for (map = 0; map < 100; map++)
    {
        g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (8, 8);
        g_autoptr(LrgPathfinder) finder = lrg_pathfinder_new (grid);
        g_autoptr(LrgPath) path = NULL;
        g_autoptr(GError) error = NULL;
        gfloat expected;
        gfloat actual = 0.0f;
        gint previous_x = 0, previous_y = 0;
        guint i;

        lrg_nav_grid_set_allow_diagonal (grid, FALSE);
        for (i = 0; i < 64; i++)
        {
            lrg_nav_grid_set_cell_cost (grid, i % 8, i / 8,
                                       g_rand_int_range (random, 0, 20) * 0.25f);
            if (i != 0 && i != 63 && g_rand_int_range (random, 0, 5) == 0)
                lrg_nav_grid_set_blocked (grid, i % 8, i / 8, TRUE);
        }
        expected = reference_cost (grid);
        path = lrg_pathfinder_find_path (finder, 0, 0, 7, 7, &error);
        if (expected == G_MAXFLOAT)
        {
            g_assert_null (path);
            g_assert_error (error, LRG_PATHFINDING_ERROR, LRG_PATHFINDING_ERROR_NO_PATH);
            continue;
        }
        g_assert_no_error (error);
        g_assert_nonnull (path);
        g_assert_cmpfloat_with_epsilon (lrg_path_get_total_cost (path), expected, 0.0001f);
        for (i = 0; i < lrg_path_get_length (path); i++)
        {
            gint x, y;

            g_assert_true (lrg_path_get_point (path, i, &x, &y));
            g_assert_true (lrg_nav_grid_is_walkable (grid, x, y));
            if (i == 0)
            {
                g_assert_cmpint (x, ==, 0);
                g_assert_cmpint (y, ==, 0);
            }
            else
            {
                g_assert_cmpint (abs (x - previous_x) + abs (y - previous_y), ==, 1);
                actual += lrg_nav_grid_get_cell_cost (grid, x, y);
            }
            previous_x = x;
            previous_y = y;
        }
        g_assert_cmpint (previous_x, ==, 7);
        g_assert_cmpint (previous_y, ==, 7);
        g_assert_cmpfloat_with_epsilon (actual, expected, 0.0001f);
    }
    g_rand_free (random);
}

static void
test_nav_grid_clipped_rectangles (void)
{
    const struct { gint x, y; guint width, height; } cases[] = {
        { -2, -1, 4, 3 }, { 2, 2, G_MAXUINT, G_MAXUINT },
        { G_MININT, G_MININT, G_MAXUINT, G_MAXUINT },
        { G_MAXINT, G_MAXINT, G_MAXUINT, G_MAXUINT },
        { -10, -10, 2, 2 }, { 0, 0, 0, G_MAXUINT },
        { G_MININT, 0, 1, 3 }
    };
    guint i;

    for (i = 0; i < G_N_ELEMENTS (cases); i++)
    {
        g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (4, 3);
        gint x, y;

        lrg_nav_grid_fill_rect (grid, cases[i].x, cases[i].y,
                                cases[i].width, cases[i].height,
                                LRG_NAV_CELL_BLOCKED, 2.5f);
        for (y = 0; y < 3; y++)
            for (x = 0; x < 4; x++)
            {
                gboolean inside = (gint64)x >= cases[i].x &&
                    (gint64)y >= cases[i].y &&
                    (gint64)x < (gint64)cases[i].x + cases[i].width &&
                    (gint64)y < (gint64)cases[i].y + cases[i].height;

                g_assert_cmpint (lrg_nav_grid_is_walkable (grid, x, y), ==, !inside);
                g_assert_cmpfloat (lrg_nav_grid_get_cell_cost (grid, x, y), ==,
                                   inside ? 2.5f : 1.0f);
            }
    }
}

static void
test_nav_grid_invalid_fill_cost (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (2, 2);
    const gfloat costs[] = { -1.0f, NAN };
    guint i;
    gint x, y;

    for (i = 0; i < G_N_ELEMENTS (costs); i++)
    {
        g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*cost >= 0.0f*");
        lrg_nav_grid_fill_rect (grid, 0, 0, 2, 2, LRG_NAV_CELL_BLOCKED, costs[i]);
        g_test_assert_expected_messages ();
        for (y = 0; y < 2; y++)
            for (x = 0; x < 2; x++)
            {
                g_assert_true (lrg_nav_grid_is_walkable (grid, x, y));
                g_assert_cmpfloat (lrg_nav_grid_get_cell_cost (grid, x, y), ==, 1.0f);
            }
    }
}

static void
test_nav_grid_invalid_neighbor_origin (void)
{
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new (3, 3);
    const gint origins[][2] = { {-1, 0}, {0, -1}, {3, 0}, {0, 3},
        {G_MININT, 0}, {0, G_MININT}, {G_MAXINT, 0}, {0, G_MAXINT} };
    guint i;
    GList *neighbors;

    for (i = 0; i < G_N_ELEMENTS (origins); i++)
        g_assert_null (lrg_nav_grid_get_neighbors (grid, origins[i][0], origins[i][1]));
    neighbors = lrg_nav_grid_get_neighbors (grid, 0, 0);
    g_assert_cmpuint (g_list_length (neighbors), ==, 3);
    g_list_free_full (neighbors, (GDestroyNotify)lrg_nav_cell_free);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* NavCell tests */
    g_test_add_func ("/pathfinding/nav-cell/new", test_nav_cell_new);
    g_test_add_func ("/pathfinding/nav-cell/copy", test_nav_cell_copy);
    g_test_add_func ("/pathfinding/nav-cell/flags", test_nav_cell_flags);
    g_test_add_func ("/pathfinding/nav-cell/cost", test_nav_cell_cost);

    /* Path tests */
    g_test_add_func ("/pathfinding/path/new", test_path_new);
    g_test_add_func ("/pathfinding/path/append-prepend", test_path_append_prepend);
    g_test_add_func ("/pathfinding/path/get-point", test_path_get_point);
    g_test_add_func ("/pathfinding/path/reverse", test_path_reverse);
    g_test_add_func ("/pathfinding/path/copy", test_path_copy);

    /* NavGrid tests */
    g_test_add_func ("/pathfinding/nav-grid/new", test_nav_grid_new);
    g_test_add_func ("/pathfinding/nav-grid/is-valid", test_nav_grid_is_valid);
    g_test_add_func ("/pathfinding/nav-grid/get-cell", test_nav_grid_get_cell);
    g_test_add_func ("/pathfinding/nav-grid/blocked", test_nav_grid_blocked);
    g_test_add_func ("/pathfinding/nav-grid/cell-cost", test_nav_grid_cell_cost);
    g_test_add_func ("/pathfinding/nav-grid/diagonal", test_nav_grid_diagonal);
    g_test_add_func ("/pathfinding/nav-grid/fill-rect", test_nav_grid_fill_rect);

    /* Pathfinder tests */
    g_test_add_func ("/pathfinding/pathfinder/weighted-optimal", test_pathfinder_weighted_optimal);
    g_test_add_func ("/pathfinding/pathfinder/diagonal-optimal", test_pathfinder_diagonal_optimal);
    g_test_add_func ("/pathfinding/pathfinder/new", test_pathfinder_new);
    g_test_add_func ("/pathfinding/pathfinder/simple-path", test_pathfinder_simple_path);
    g_test_add_func ("/pathfinding/pathfinder/smoothing-preserves-cost", test_pathfinder_smoothing_preserves_cost);
    g_test_add_func ("/pathfinding/pathfinder/same-start-end", test_pathfinder_same_start_end);
    g_test_add_func ("/pathfinding/pathfinder/blocked-path", test_pathfinder_blocked_path);
    g_test_add_func ("/pathfinding/pathfinder/around-obstacle", test_pathfinder_around_obstacle);
    g_test_add_func ("/pathfinding/pathfinder/invalid-positions", test_pathfinder_invalid_positions);
    g_test_add_func ("/pathfinding/pathfinder/blocked-start-end", test_pathfinder_blocked_start_end);
    g_test_add_func ("/pathfinding/pathfinder/no-grid", test_pathfinder_no_grid);
    g_test_add_func ("/pathfinding/pathfinder/is-reachable", test_pathfinder_is_reachable);
    g_test_add_func ("/pathfinding/pathfinder/nodes-explored", test_pathfinder_nodes_explored);
    g_test_add_func ("/pathfinding/pathfinder/cardinal-only", test_pathfinder_cardinal_only);

    g_test_add_func ("/pathfinding/pathfinder/reopen", test_pathfinder_reopen);
    g_test_add_func ("/pathfinding/pathfinder/iteration-limit", test_pathfinder_iteration_limit);
    g_test_add_func ("/pathfinding/pathfinder/reference-maps", test_pathfinder_reference_maps);
    g_test_add_func ("/pathfinding/error-enum", test_pathfinding_error_enum);
    g_test_add_func ("/pathfinding/heuristics-large", test_heuristics_large_coordinates);

    g_test_add_func ("/pathfinding/nav-grid/clipped-rectangles", test_nav_grid_clipped_rectangles);
    g_test_add_func ("/pathfinding/nav-grid/invalid-fill-cost", test_nav_grid_invalid_fill_cost);
    g_test_add_func ("/pathfinding/nav-grid/invalid-neighbor-origin", test_nav_grid_invalid_neighbor_origin);

    /* Heuristic tests */
    g_test_add_func ("/pathfinding/heuristics", test_heuristics);

    return g_test_run ();
}
