/* test-pathfinding.c
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the Pathfinding module.
 */

#include <glib.h>
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
    g_test_add_func ("/pathfinding/pathfinder/new", test_pathfinder_new);
    g_test_add_func ("/pathfinding/pathfinder/simple-path", test_pathfinder_simple_path);
    g_test_add_func ("/pathfinding/pathfinder/same-start-end", test_pathfinder_same_start_end);
    g_test_add_func ("/pathfinding/pathfinder/blocked-path", test_pathfinder_blocked_path);
    g_test_add_func ("/pathfinding/pathfinder/around-obstacle", test_pathfinder_around_obstacle);
    g_test_add_func ("/pathfinding/pathfinder/invalid-positions", test_pathfinder_invalid_positions);
    g_test_add_func ("/pathfinding/pathfinder/blocked-start-end", test_pathfinder_blocked_start_end);
    g_test_add_func ("/pathfinding/pathfinder/no-grid", test_pathfinder_no_grid);
    g_test_add_func ("/pathfinding/pathfinder/is-reachable", test_pathfinder_is_reachable);
    g_test_add_func ("/pathfinding/pathfinder/nodes-explored", test_pathfinder_nodes_explored);
    g_test_add_func ("/pathfinding/pathfinder/cardinal-only", test_pathfinder_cardinal_only);

    /* Heuristic tests */
    g_test_add_func ("/pathfinding/heuristics", test_heuristics);

    return g_test_run ();
}
