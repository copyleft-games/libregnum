# LrgPathfinder

A final GObject implementing the A* pathfinding algorithm for finding optimal paths on a navigation grid.

## Type Information

- **Type Name**: `LrgPathfinder`
- **Type ID**: `LRG_TYPE_PATHFINDER`
- **Category**: Final GObject (cannot be subclassed)
- **Header**: `lrg-pathfinder.h`

## Description

`LrgPathfinder` implements the A* algorithm for finding optimal paths from a start position to a goal position on a navigation grid. Key features:

- **A* Algorithm**: Optimal pathfinding with customizable heuristics
- **Multiple Heuristics**: Built-in Manhattan, Euclidean, Chebyshev, and Octile distance
- **Custom Heuristics**: Support for application-specific distance calculations
- **Path Smoothing**: Optional post-processing to smooth paths
- **Reachability Checks**: Fast checks to determine if a path exists
- **Performance Metrics**: Track nodes explored for optimization

## Creating a Pathfinder

### lrg_pathfinder_new()

```c
LrgPathfinder *
lrg_pathfinder_new (LrgNavGrid *grid)
```

Creates a new pathfinder for the given grid.

**Parameters:**
- `grid`: The navigation grid (can be NULL, but must be set before pathfinding)

**Returns:** (transfer full) A new `LrgPathfinder`

**Example:**
```c
g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new(100, 100);
g_autoptr(LrgPathfinder) pf = lrg_pathfinder_new(grid);
```

## Grid Management

### lrg_pathfinder_get_grid()

```c
LrgNavGrid *
lrg_pathfinder_get_grid (LrgPathfinder *self)
```

Gets the navigation grid.

**Returns:** (transfer none) The navigation grid

### lrg_pathfinder_set_grid()

```c
void
lrg_pathfinder_set_grid (LrgPathfinder *self,
                         LrgNavGrid    *grid)
```

Sets or changes the navigation grid.

**Example:**
```c
g_autoptr(LrgNavGrid) new_grid = lrg_nav_grid_new(200, 200);
lrg_pathfinder_set_grid(pathfinder, new_grid);
```

## Finding Paths

### lrg_pathfinder_find_path()

```c
LrgPath *
lrg_pathfinder_find_path (LrgPathfinder  *self,
                          gint            start_x,
                          gint            start_y,
                          gint            end_x,
                          gint            end_y,
                          GError        **error)
```

Finds a path from start to end using A*.

**Parameters:**
- `start_x`, `start_y`: Starting position
- `end_x`, `end_y`: Goal position
- `error`: (nullable) Return location for error

**Returns:** (transfer full) (nullable) The path, or NULL on error

**Error Codes:** (from `LRG_PATHFINDING_ERROR`)
- `LRG_PATHFINDING_ERROR_NO_GRID` - No grid set on pathfinder
- `LRG_PATHFINDING_ERROR_INVALID_START` - Start position invalid or blocked
- `LRG_PATHFINDING_ERROR_INVALID_GOAL` - Goal position invalid or blocked
- `LRG_PATHFINDING_ERROR_NO_PATH` - No path exists between start and goal

**Example:**
```c
GError *error = NULL;
LrgPath *path = lrg_pathfinder_find_path(pathfinder, 10, 10, 90, 90, &error);

if (path != NULL) {
    g_print("Found path with %u waypoints\n", lrg_path_get_length(path));
    lrg_path_free(path);
} else {
    g_warning("Pathfinding failed: %s", error->message);
    g_error_free(error);
}
```

## Reachability Checks

### lrg_pathfinder_is_reachable()

```c
gboolean
lrg_pathfinder_is_reachable (LrgPathfinder *self,
                             gint           start_x,
                             gint           start_y,
                             gint           end_x,
                             gint           end_y)
```

Quickly checks if a path exists without computing the full path.

**Returns:** `TRUE` if a path exists

**Use Case:** Fast queries before expensive pathfinding

**Example:**
```c
if (lrg_pathfinder_is_reachable(pathfinder, 10, 10, 90, 90)) {
    /* Safe to call find_path */
    LrgPath *path = lrg_pathfinder_find_path(pathfinder, 10, 10, 90, 90, NULL);
}
```

## Path Smoothing

### lrg_pathfinder_get_smoothing()

```c
LrgPathSmoothingMode
lrg_pathfinder_get_smoothing (LrgPathfinder *self)
```

Gets the path smoothing mode.

**Returns:** The smoothing mode

### lrg_pathfinder_set_smoothing()

```c
void
lrg_pathfinder_set_smoothing (LrgPathfinder        *self,
                              LrgPathSmoothingMode  mode)
```

Sets the path smoothing mode.

**Smoothing Modes:** (from `lrg-enums.h`)
- `LRG_PATH_SMOOTHING_NONE` - No smoothing (default)
- `LRG_PATH_SMOOTHING_RAYCASTING` - Line-of-sight smoothing

**Example:**
```c
/* Enable path smoothing for smoother movement */
lrg_pathfinder_set_smoothing(pathfinder, LRG_PATH_SMOOTHING_RAYCASTING);
```

## Iteration Control

### lrg_pathfinder_get_max_iterations()

```c
guint
lrg_pathfinder_get_max_iterations (LrgPathfinder *self)
```

Gets the maximum iterations limit.

**Returns:** Maximum iterations (0 = unlimited)

### lrg_pathfinder_set_max_iterations()

```c
void
lrg_pathfinder_set_max_iterations (LrgPathfinder *self,
                                   guint          max_iterations)
```

Sets the maximum number of iterations before giving up.

**Use Case:** Prevent excessive computation on large grids with blocked goals

**Parameters:**
- `max_iterations`: Maximum (0 = unlimited)

**Example:**
```c
/* Limit to 10000 iterations to prevent performance hiccups */
lrg_pathfinder_set_max_iterations(pathfinder, 10000);
```

## Heuristic Functions

### lrg_pathfinder_set_heuristic()

```c
void
lrg_pathfinder_set_heuristic (LrgPathfinder    *self,
                              LrgHeuristicFunc  func,
                              gpointer          user_data,
                              GDestroyNotify    destroy)
```

Sets a custom heuristic function. If NULL, uses Manhattan distance.

**Parameters:**
- `func`: (nullable) Custom heuristic function
- `user_data`: Arbitrary data for the function
- `destroy`: Cleanup function for user_data

**Heuristic Function Signature:**
```c
typedef gfloat (*LrgHeuristicFunc) (gint     x1,
                                    gint     y1,
                                    gint     x2,
                                    gint     y2,
                                    gpointer user_data);
```

Returns estimated cost from (x1,y1) to (x2,y2).

### Built-in Heuristics

#### lrg_heuristic_manhattan()

```c
gfloat
lrg_heuristic_manhattan (gint     x1,
                         gint     y1,
                         gint     x2,
                         gint     y2,
                         gpointer user_data)
```

Manhattan (taxicab) distance heuristic.

**Formula:** `|x2-x1| + |y2-y1|`

**Best for:** Cardinal-only movement (no diagonals)

#### lrg_heuristic_euclidean()

```c
gfloat
lrg_heuristic_euclidean (gint     x1,
                         gint     y1,
                         gint     x2,
                         gint     y2,
                         gpointer user_data)
```

Euclidean distance heuristic.

**Formula:** `sqrt((x2-x1)^2 + (y2-y1)^2)`

**Best for:** Continuous movement

#### lrg_heuristic_chebyshev()

```c
gfloat
lrg_heuristic_chebyshev (gint     x1,
                         gint     y1,
                         gint     x2,
                         gint     y2,
                         gpointer user_data)
```

Chebyshev distance heuristic (diagonal cost = 1).

**Formula:** `max(|x2-x1|, |y2-y1|)`

**Best for:** Chess king movement (equal diagonal/cardinal cost)

#### lrg_heuristic_octile()

```c
gfloat
lrg_heuristic_octile (gint     x1,
                      gint     y1,
                      gint     x2,
                      gint     y2,
                      gpointer user_data)
```

Octile distance heuristic (diagonal cost = sqrt(2)).

**Formula:** Approximate diagonal cost

**Best for:** Most game movement (8-directional with realistic costs)

**Example:**
```c
/* Use Euclidean distance for better path quality */
lrg_pathfinder_set_heuristic(pathfinder,
                             lrg_heuristic_euclidean,
                             NULL,
                             NULL);
```

## Performance Metrics

### lrg_pathfinder_get_last_nodes_explored()

```c
guint
lrg_pathfinder_get_last_nodes_explored (LrgPathfinder *self)
```

Gets the number of nodes explored in the last pathfinding operation.

**Returns:** Nodes explored count

**Use Case:** Performance profiling and optimization

**Example:**
```c
LrgPath *path = lrg_pathfinder_find_path(pathfinder, 10, 10, 90, 90, NULL);
if (path != NULL) {
    guint nodes = lrg_pathfinder_get_last_nodes_explored(pathfinder);
    g_print("Explored %u nodes\n", nodes);
}
```

## Complete Example

```c
#include <glib.h>
#include <libregnum.h>

int main(void) {
    g_autoptr(GError) error = NULL;

    /* Create grid with obstacles */
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new(50, 50);
    lrg_nav_grid_set_allow_diagonal(grid, TRUE);

    /* Create wall obstacle */
    for (gint i = 0; i < 40; i++) {
        lrg_nav_grid_set_blocked(grid, 25, i, TRUE);
    }

    /* Create pathfinder */
    g_autoptr(LrgPathfinder) pf = lrg_pathfinder_new(grid);
    lrg_pathfinder_set_smoothing(pf, LRG_PATH_SMOOTHING_RAYCASTING);
    lrg_pathfinder_set_max_iterations(pf, 5000);

    /* Check if path exists before pathfinding */
    if (!lrg_pathfinder_is_reachable(pf, 10, 25, 40, 25)) {
        g_print("Goal is not reachable\n");
        return 1;
    }

    /* Find path */
    g_autoptr(LrgPath) path = lrg_pathfinder_find_path(pf, 10, 25, 40, 25, &error);
    if (path == NULL) {
        g_warning("Pathfinding failed: %s", error->message);
        return 1;
    }

    /* Analyze result */
    guint length = lrg_path_get_length(path);
    guint nodes = lrg_pathfinder_get_last_nodes_explored(pf);
    gfloat cost = lrg_path_get_total_cost(path);

    g_print("Path found: %u waypoints, %u nodes explored, cost: %.2f\n",
            length, nodes, cost);

    /* Print waypoints */
    for (guint i = 0; i < length; i++) {
        gint x, y;
        if (lrg_path_get_point(path, i, &x, &y)) {
            g_print("  [%u] (%d, %d)\n", i, x, y);
        }
    }

    return 0;
}
```

## Algorithm Details

The A* algorithm works by:

1. **Open Set**: Maintains candidates for exploration
2. **Closed Set**: Tracks already-explored nodes
3. **Cost Calculation**: `f(node) = g(node) + h(node)` where:
   - `g(node)` = actual cost from start to node
   - `h(node)` = heuristic estimate from node to goal
4. **Expansion**: Always expands the node with lowest f-cost
5. **Termination**: Stops when goal is reached or open set is exhausted

## Related Types

- [LrgNavGrid](nav-grid.md) - Navigation grid
- [LrgPath](path.md) - Path result
- [LrgNavCell](nav-cell.md) - Individual cells
