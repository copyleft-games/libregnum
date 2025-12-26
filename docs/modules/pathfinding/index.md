# Pathfinding Module

The Pathfinding module provides A* pathfinding algorithms for grid-based navigation in games. It supports custom movement costs, diagonal movement, and path smoothing.

## Overview

The module is built around four core components:

- **LrgNavCell**: A boxed type representing a single navigable cell with position, cost, and flags
- **LrgNavGrid**: A grid data structure containing navigation cells with configurable movement options
- **LrgPathfinder**: The A* algorithm implementation for finding optimal paths
- **LrgPath**: A boxed type representing a sequence of waypoints from start to goal

## Key Features

- **A* Algorithm**: Optimal pathfinding with customizable heuristics (Manhattan, Euclidean, Chebyshev, Octile)
- **Flexible Movement**: Support for cardinal-only (4-directional) and diagonal (8-directional) movement
- **Custom Costs**: Different movement costs per cell for terrain variation
- **Path Smoothing**: Optional path smoothing for smoother animations
- **Reachability Checks**: Quick checks to determine if a path exists without computing it
- **Performance Metrics**: Track nodes explored for debugging and optimization

## Basic Usage

```c
#include <libregnum.h>

/* Create and configure the navigation grid */
LrgNavGrid *grid = lrg_nav_grid_new(50, 50);
lrg_nav_grid_set_allow_diagonal(grid, TRUE);

/* Block some cells to represent obstacles */
lrg_nav_grid_set_blocked(grid, 25, 10, TRUE);
lrg_nav_grid_set_blocked(grid, 25, 11, TRUE);
lrg_nav_grid_set_blocked(grid, 25, 12, TRUE);

/* Create a pathfinder for this grid */
LrgPathfinder *pathfinder = lrg_pathfinder_new(grid);

/* Find a path */
GError *error = NULL;
LrgPath *path = lrg_pathfinder_find_path(pathfinder, 10, 10, 40, 40, &error);

if (path != NULL) {
    g_print("Path found with %u waypoints\n", lrg_path_get_length(path));
    lrg_path_free(path);
} else {
    g_print("No path found: %s\n", error->message);
    g_error_free(error);
}

g_object_unref(pathfinder);
g_object_unref(grid);
```

## Module Structure

```
pathfinding/
├── lrg-nav-cell.h/.c       # Navigation cell boxed type
├── lrg-nav-grid.h/.c       # Navigation grid GObject
├── lrg-path.h/.c           # Path result boxed type
└── lrg-pathfinder.h/.c     # A* pathfinder GObject
```

## Documentation

- [LrgNavCell](nav-cell.md) - Navigation cell representation
- [LrgNavGrid](nav-grid.md) - Grid data structure and configuration
- [LrgPath](path.md) - Path result and waypoint management
- [LrgPathfinder](pathfinder.md) - A* pathfinding algorithm

## Examples

For complete working examples, see the [Pathfinding Examples](../examples/pathfinding.md).
