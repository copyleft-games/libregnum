# Pathfinding Examples

Complete working examples using the Pathfinding module.

## Example 1: Basic Path Finding

This example demonstrates creating a grid, adding obstacles, and finding a path.

```c
#include <glib.h>
#include <libregnum.h>

int main(void) {
    g_autoptr(GError) error = NULL;

    /* Create a 20x20 navigation grid */
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new(20, 20);

    /* Configure movement: allow diagonals without corner cutting */
    lrg_nav_grid_set_allow_diagonal(grid, TRUE);
    lrg_nav_grid_set_cut_corners(grid, FALSE);

    /* Create a vertical wall from (10,0) to (10,19) */
    for (gint i = 0; i < 20; i++) {
        lrg_nav_grid_set_blocked(grid, 10, i, TRUE);
    }

    /* Create pathfinder */
    g_autoptr(LrgPathfinder) pathfinder = lrg_pathfinder_new(grid);

    /* Find path from (2,10) to (18,10) - must go around wall */
    LrgPath *path = lrg_pathfinder_find_path(pathfinder, 2, 10, 18, 10, &error);

    if (path != NULL) {
        guint length = lrg_path_get_length(path);
        gfloat cost = lrg_path_get_total_cost(path);
        guint nodes = lrg_pathfinder_get_last_nodes_explored(pathfinder);

        g_print("Path found!\n");
        g_print("  Length: %u waypoints\n", length);
        g_print("  Cost: %.2f\n", cost);
        g_print("  Nodes explored: %u\n", nodes);

        /* Print waypoints */
        g_print("  Waypoints:\n");
        for (guint i = 0; i < length; i++) {
            gint x, y;
            if (lrg_path_get_point(path, i, &x, &y)) {
                g_print("    [%u] (%d, %d)\n", i, x, y);
            }
        }

        lrg_path_free(path);
    } else {
        g_warning("No path found: %s", error->message);
        g_error_free(error);
        return 1;
    }

    return 0;
}
```

## Example 2: Different Heuristics

Comparing different heuristic functions.

```c
#include <glib.h>
#include <libregnum.h>

int main(void) {
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new(50, 50);
    g_autoptr(LrgPathfinder) pf = lrg_pathfinder_new(grid);

    /* Create some obstacles */
    for (gint i = 0; i < 40; i++) {
        lrg_nav_grid_set_blocked(grid, 25, i, TRUE);
    }

    /* Test different heuristics */
    const struct {
        const gchar *name;
        LrgHeuristicFunc heuristic;
    } heuristics[] = {
        { "Manhattan", lrg_heuristic_manhattan },
        { "Euclidean", lrg_heuristic_euclidean },
        { "Chebyshev", lrg_heuristic_chebyshev },
        { "Octile", lrg_heuristic_octile }
    };

    for (guint h = 0; h < G_N_ELEMENTS(heuristics); h++) {
        lrg_pathfinder_set_heuristic(pf, heuristics[h].heuristic, NULL, NULL);

        LrgPath *path = lrg_pathfinder_find_path(pf, 10, 25, 40, 25, NULL);

        if (path != NULL) {
            guint nodes = lrg_pathfinder_get_last_nodes_explored(pf);
            g_print("%s: %u nodes explored, path length: %u\n",
                    heuristics[h].name, nodes, lrg_path_get_length(path));
            lrg_path_free(path);
        }
    }

    return 0;
}
```

## Example 3: Terrain with Varying Costs

Create terrain with different movement costs.

```c
#include <glib.h>
#include <libregnum.h>

int main(void) {
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new(30, 30);
    g_autoptr(LrgPathfinder) pf = lrg_pathfinder_new(grid);

    /* Mark grass terrain (fast) */
    lrg_nav_grid_fill_rect(grid, 0, 0, 30, 10, LRG_NAV_CELL_NONE, 1.0f);

    /* Mark swamp terrain (slow) */
    lrg_nav_grid_fill_rect(grid, 0, 10, 30, 10, LRG_NAV_CELL_NONE, 3.0f);

    /* Mark mountain terrain (slowest) */
    lrg_nav_grid_fill_rect(grid, 0, 20, 30, 10, LRG_NAV_CELL_NONE, 5.0f);

    /* Path from top to bottom will prefer going through swamp rather than mountains */
    LrgPath *path = lrg_pathfinder_find_path(pf, 15, 0, 15, 29, NULL);

    if (path != NULL) {
        g_print("Cost-aware path found\n");
        g_print("  Path cost: %.2f\n", lrg_path_get_total_cost(path));
        g_print("  Waypoints: %u\n", lrg_path_get_length(path));

        /* Analyze which terrains are used */
        gint swamp_count = 0, mountain_count = 0;
        for (guint i = 0; i < lrg_path_get_length(path); i++) {
            gint x, y;
            if (lrg_path_get_point(path, i, &x, &y)) {
                if (y >= 10 && y < 20) swamp_count++;
                if (y >= 20) mountain_count++;
            }
        }

        g_print("  Swamp waypoints: %d\n", swamp_count);
        g_print("  Mountain waypoints: %d\n", mountain_count);

        lrg_path_free(path);
    }

    return 0;
}
```

## Example 4: Cardinal Only Movement

Using 4-directional movement instead of 8-directional.

```c
#include <glib.h>
#include <libregnum.h>

int main(void) {
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new(20, 20);
    g_autoptr(LrgPathfinder) pf_diagonal = lrg_pathfinder_new(grid);
    g_autoptr(LrgPathfinder) pf_cardinal = lrg_pathfinder_new(grid);

    /* Find path with diagonal movement allowed */
    LrgPath *path_diagonal = lrg_pathfinder_find_path(
        pf_diagonal, 0, 0, 19, 19, NULL);

    /* Find path with cardinal movement only */
    lrg_nav_grid_set_allow_diagonal(grid, FALSE);
    LrgPath *path_cardinal = lrg_pathfinder_find_path(
        pf_cardinal, 0, 0, 19, 19, NULL);

    if (path_diagonal != NULL && path_cardinal != NULL) {
        g_print("Diagonal movement:\n");
        g_print("  Length: %u waypoints\n", lrg_path_get_length(path_diagonal));
        g_print("  Cost: %.2f\n", lrg_path_get_total_cost(path_diagonal));

        g_print("Cardinal movement:\n");
        g_print("  Length: %u waypoints\n", lrg_path_get_length(path_cardinal));
        g_print("  Cost: %.2f\n", lrg_path_get_total_cost(path_cardinal));

        lrg_path_free(path_diagonal);
        lrg_path_free(path_cardinal);
    }

    return 0;
}
```

## Example 5: Path Smoothing

Apply path smoothing for smoother movement.

```c
#include <glib.h>
#include <libregnum.h>

int main(void) {
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new(30, 30);
    g_autoptr(LrgPathfinder) pf = lrg_pathfinder_new(grid);

    /* Add some obstacles */
    lrg_nav_grid_set_blocked(grid, 15, 10, TRUE);
    lrg_nav_grid_set_blocked(grid, 15, 11, TRUE);
    lrg_nav_grid_set_blocked(grid, 15, 12, TRUE);

    /* Compare with and without smoothing */
    LrgPath *path_unsmoothed = lrg_pathfinder_find_path(
        pf, 10, 5, 20, 20, NULL);

    lrg_pathfinder_set_smoothing(pf, LRG_PATH_SMOOTHING_RAYCASTING);
    LrgPath *path_smoothed = lrg_pathfinder_find_path(
        pf, 10, 5, 20, 20, NULL);

    if (path_unsmoothed != NULL && path_smoothed != NULL) {
        g_print("Unsmoothed path: %u waypoints\n",
                lrg_path_get_length(path_unsmoothed));
        g_print("Smoothed path: %u waypoints\n",
                lrg_path_get_length(path_smoothed));

        lrg_path_free(path_unsmoothed);
        lrg_path_free(path_smoothed);
    }

    return 0;
}
```

## Example 6: Reachability Check and Multiple Paths

Use reachability checking before pathfinding.

```c
#include <glib.h>
#include <libregnum.h>

int main(void) {
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new(40, 40);
    g_autoptr(LrgPathfinder) pf = lrg_pathfinder_new(grid);

    /* Create two separate areas with a wall between */
    for (gint i = 0; i < 40; i++) {
        lrg_nav_grid_set_blocked(grid, 20, i, TRUE);
    }

    /* Check reachability */
    gboolean left_to_right = lrg_pathfinder_is_reachable(pf, 5, 20, 35, 20);
    gboolean left_to_left = lrg_pathfinder_is_reachable(pf, 5, 10, 5, 30);
    gboolean right_to_right = lrg_pathfinder_is_reachable(pf, 35, 10, 35, 30);

    g_print("Can reach from left to right: %s\n", left_to_right ? "yes" : "no");
    g_print("Can move within left side: %s\n", left_to_left ? "yes" : "no");
    g_print("Can move within right side: %s\n", right_to_right ? "yes" : "no");

    /* Find paths for reachable areas */
    if (left_to_left) {
        LrgPath *path = lrg_pathfinder_find_path(pf, 5, 10, 5, 30, NULL);
        if (path != NULL) {
            g_print("\nLeft side path found: %u waypoints\n",
                    lrg_path_get_length(path));
            lrg_path_free(path);
        }
    }

    return 0;
}
```

## Example 7: Game Loop Integration

Typical usage in a game loop.

```c
#include <glib.h>
#include <libregnum.h>

typedef struct {
    gint x, y;
    LrgPath *path;
    guint current_waypoint;
    LrgPathfinder *pathfinder;
} Actor;

int main(void) {
    /* Initialize */
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new(100, 100);
    g_autoptr(LrgPathfinder) pf = lrg_pathfinder_new(grid);

    /* Setup actor */
    Actor actor = {
        .x = 10,
        .y = 10,
        .path = NULL,
        .current_waypoint = 0,
        .pathfinder = pf
    };

    /* Find initial path */
    actor.path = lrg_pathfinder_find_path(pf, 10, 10, 90, 90, NULL);

    /* Simulate game loop for 10 frames */
    for (gint frame = 0; frame < 10; frame++) {
        g_print("=== Frame %d ===\n", frame);
        g_print("Actor at (%d, %d)\n", actor.x, actor.y);

        if (actor.path != NULL) {
            /* Get next waypoint */
            gint next_x, next_y;
            if (lrg_path_get_point(actor.path, actor.current_waypoint,
                                  &next_x, &next_y)) {
                g_print("Moving to waypoint %u: (%d, %d)\n",
                        actor.current_waypoint, next_x, next_y);

                /* Simulate movement toward waypoint */
                if (actor.x < next_x) actor.x++;
                else if (actor.x > next_x) actor.x--;
                if (actor.y < next_y) actor.y++;
                else if (actor.y > next_y) actor.y--;

                /* Check if reached waypoint */
                if (actor.x == next_x && actor.y == next_y) {
                    actor.current_waypoint++;
                    if (actor.current_waypoint >= lrg_path_get_length(actor.path)) {
                        g_print("Path complete!\n");
                        lrg_path_free(actor.path);
                        actor.path = NULL;
                    }
                }
            }
        } else {
            g_print("No path\n");
        }
    }

    return 0;
}
```

## Example 8: Complex Obstacle Layout

Handle complex obstacle layouts.

```c
#include <glib.h>
#include <libregnum.h>

int main(void) {
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new(50, 50);
    g_autoptr(LrgPathfinder) pf = lrg_pathfinder_new(grid);

    /* Create a maze-like layout */
    /* Outer walls */
    for (gint i = 0; i < 50; i++) {
        lrg_nav_grid_set_blocked(grid, 0, i, TRUE);
        lrg_nav_grid_set_blocked(grid, 49, i, TRUE);
        lrg_nav_grid_set_blocked(grid, i, 0, TRUE);
        lrg_nav_grid_set_blocked(grid, i, 49, TRUE);
    }

    /* Interior walls */
    lrg_nav_grid_fill_rect(grid, 10, 10, 30, 2, LRG_NAV_CELL_BLOCKED, 1.0f);
    lrg_nav_grid_fill_rect(grid, 10, 30, 30, 2, LRG_NAV_CELL_BLOCKED, 1.0f);
    lrg_nav_grid_fill_rect(grid, 20, 10, 2, 20, LRG_NAV_CELL_BLOCKED, 1.0f);

    /* Find path through maze */
    LrgPath *path = lrg_pathfinder_find_path(pf, 5, 5, 45, 45, NULL);

    if (path != NULL) {
        g_print("Maze path found\n");
        g_print("  Length: %u waypoints\n", lrg_path_get_length(path));
        g_print("  Cost: %.2f\n", lrg_path_get_total_cost(path));
        g_print("  Nodes explored: %u\n",
                lrg_pathfinder_get_last_nodes_explored(pf));

        lrg_path_free(path);
    }

    return 0;
}
```

## Example 9: Custom Grid with Subclassing

Create a custom grid implementation (advanced).

```c
/* This is a conceptual example - actual implementation would involve
   properly subclassing LrgNavGrid and overriding virtual methods */

#include <glib.h>
#include <libregnum.h>

/* Pseudocode showing the concept:

typedef struct {
    LrgNavGrid parent;
    gint *custom_data;
} MyCustomGrid;

static gfloat
my_custom_get_cost(LrgNavGrid *self, gint x, gint y)
{
    MyCustomGrid *grid = (MyCustomGrid *)self;
    /* Custom cost calculation based on grid->custom_data */
    return some_calculated_cost;
}

static gboolean
my_custom_is_walkable(LrgNavGrid *self, gint x, gint y)
{
    /* Custom walkability logic */
    return some_calculated_walkable;
}

/* In main: */
MyCustomGrid *grid = g_object_new(MY_CUSTOM_GRID_TYPE, NULL);
LrgPathfinder *pf = lrg_pathfinder_new(LRG_NAV_GRID(grid));
*/
```

## Compilation

Compile with:

```bash
gcc -Wall -o pathfinding_example example.c `pkg-config --cflags --libs libregnum glib-2.0`
```

Or using your project's Makefile system (as per libregnum's build setup).
