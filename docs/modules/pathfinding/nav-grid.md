# LrgNavGrid

A derivable GObject that represents a grid-based navigation surface. It manages a collection of navigation cells and provides methods for querying walkability and movement options.

## Type Information

- **Type Name**: `LrgNavGrid`
- **Type ID**: `LRG_TYPE_NAV_GRID`
- **Category**: Derivable GObject (supports subclassing)
- **Header**: `lrg-nav-grid.h`

## Description

`LrgNavGrid` is the core navigation data structure for pathfinding. It stores a 2D grid of navigation cells and provides:

- **Grid Management**: Width/height queries, bounds checking
- **Cell Access**: Get/set cells, modify costs and flags
- **Movement Configuration**: Control diagonal movement and corner cutting
- **Batch Operations**: Fill rectangular areas with specific properties
- **Neighbor Queries**: Get valid neighboring cells for A* exploration

## Creating Grids

### lrg_nav_grid_new()

```c
LrgNavGrid *
lrg_nav_grid_new (guint width,
                  guint height)
```

Creates a new navigation grid with all cells walkable and cost 1.0.

**Parameters:**
- `width`: Grid width in cells
- `height`: Grid height in cells

**Returns:** (transfer full) A new `LrgNavGrid`

**Example:**
```c
g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new(100, 100);
```

## Grid Dimensions

### lrg_nav_grid_get_width()

```c
guint
lrg_nav_grid_get_width (LrgNavGrid *self)
```

Gets the grid width in cells. **Returns:** Width in cells

### lrg_nav_grid_get_height()

```c
guint
lrg_nav_grid_get_height (LrgNavGrid *self)
```

Gets the grid height in cells. **Returns:** Height in cells

## Bounds Checking

### lrg_nav_grid_is_valid()

```c
gboolean
lrg_nav_grid_is_valid (LrgNavGrid *self,
                       gint        x,
                       gint        y)
```

Checks if coordinates are within grid bounds.

**Parameters:**
- `x`: X coordinate
- `y`: Y coordinate

**Returns:** `TRUE` if coordinates are valid (0 <= x < width and 0 <= y < height)

**Example:**
```c
if (lrg_nav_grid_is_valid(grid, 50, 50)) {
    g_print("Coordinates are within grid\n");
}
```

## Cell Access

### lrg_nav_grid_get_cell()

```c
LrgNavCell *
lrg_nav_grid_get_cell (LrgNavGrid *self,
                       gint        x,
                       gint        y)
```

Gets the navigation cell at specified position.

**Returns:** (transfer none) (nullable) The cell or NULL if invalid

**Example:**
```c
LrgNavCell *cell = lrg_nav_grid_get_cell(grid, 25, 30);
if (cell != NULL) {
    gfloat cost = lrg_nav_cell_get_cost(cell);
}
```

## Cell Cost Management

### lrg_nav_grid_get_cell_cost()

```c
gfloat
lrg_nav_grid_get_cell_cost (LrgNavGrid *self,
                            gint        x,
                            gint        y)
```

Gets the movement cost for a cell.

**Returns:** Movement cost, or `G_MAXFLOAT` if coordinates invalid

### lrg_nav_grid_set_cell_cost()

```c
void
lrg_nav_grid_set_cell_cost (LrgNavGrid *self,
                            gint        x,
                            gint        y,
                            gfloat      cost)
```

Sets the movement cost for a cell.

**Parameters:**
- `cost`: Cost multiplier (1.0 = normal, higher = slower)

**Example:**
```c
/* Make swamp terrain slower to traverse */
for (guint y = 10; y < 20; y++) {
    for (guint x = 10; x < 20; x++) {
        lrg_nav_grid_set_cell_cost(grid, x, y, 2.0f);
    }
}
```

## Cell Flags

### lrg_nav_grid_get_cell_flags()

```c
LrgNavCellFlags
lrg_nav_grid_get_cell_flags (LrgNavGrid *self,
                             gint        x,
                             gint        y)
```

Gets navigation flags for a cell. **Returns:** Cell flags

### lrg_nav_grid_set_cell_flags()

```c
void
lrg_nav_grid_set_cell_flags (LrgNavGrid      *self,
                             gint             x,
                             gint             y,
                             LrgNavCellFlags  flags)
```

Sets navigation flags for a cell.

## Walkability

### lrg_nav_grid_is_walkable()

```c
gboolean
lrg_nav_grid_is_walkable (LrgNavGrid *self,
                          gint        x,
                          gint        y)
```

Checks if a cell is walkable.

**Returns:** `TRUE` if valid and not blocked

### lrg_nav_grid_set_blocked()

```c
void
lrg_nav_grid_set_blocked (LrgNavGrid *self,
                          gint        x,
                          gint        y,
                          gboolean    blocked)
```

Sets whether a cell is blocked.

**Parameters:**
- `blocked`: `TRUE` to block the cell, `FALSE` to unblock

**Example:**
```c
/* Create a wall */
for (gint y = 0; y < 50; y++) {
    lrg_nav_grid_set_blocked(grid, 25, y, TRUE);
}
```

## Diagonal Movement

### lrg_nav_grid_get_allow_diagonal()

```c
gboolean
lrg_nav_grid_get_allow_diagonal (LrgNavGrid *self)
```

Gets whether diagonal movement is allowed. **Returns:** `TRUE` if diagonal allowed (default)

### lrg_nav_grid_set_allow_diagonal()

```c
void
lrg_nav_grid_set_allow_diagonal (LrgNavGrid *self,
                                 gboolean    allow)
```

Sets whether diagonal movement is allowed.

**Parameters:**
- `allow`: `TRUE` for 8-directional movement, `FALSE` for 4-directional

**Example:**
```c
/* Use only cardinal directions (north/south/east/west) */
lrg_nav_grid_set_allow_diagonal(grid, FALSE);
```

## Corner Cutting

### lrg_nav_grid_get_cut_corners()

```c
gboolean
lrg_nav_grid_get_cut_corners (LrgNavGrid *self)
```

Gets whether corner cutting is allowed.

**Returns:** `TRUE` if corner cutting allowed (default: `FALSE`)

### lrg_nav_grid_set_cut_corners()

```c
void
lrg_nav_grid_set_cut_corners (LrgNavGrid *self,
                              gboolean    allow)
```

Sets whether corner cutting is allowed.

**Context:** When moving diagonally between two blocked cells:
- `TRUE`: Diagonal move is allowed
- `FALSE`: Blocked diagonal move requires both adjacent cells to be walkable

**Example:**
```c
/* Allow diagonal moves even if corners are blocked */
lrg_nav_grid_set_allow_diagonal(grid, TRUE);
lrg_nav_grid_set_cut_corners(grid, TRUE);
```

## Neighbor Queries

### lrg_nav_grid_get_neighbors()

```c
GList *
lrg_nav_grid_get_neighbors (LrgNavGrid *self,
                            gint        x,
                            gint        y)
```

Gets all walkable neighbors of a cell.

**Returns:** (transfer full) (element-type LrgNavCell) List of neighbor cells

**Note:** This is primarily used internally by the pathfinder.

**Example:**
```c
GList *neighbors = lrg_nav_grid_get_neighbors(grid, 25, 25);
for (GList *iter = neighbors; iter != NULL; iter = iter->next) {
    LrgNavCell *neighbor = (LrgNavCell *)iter->data;
    g_print("  Neighbor at (%d, %d)\n",
            lrg_nav_cell_get_x(neighbor),
            lrg_nav_cell_get_y(neighbor));
    lrg_nav_cell_free(neighbor);
}
g_list_free(neighbors);
```

## Batch Operations

### lrg_nav_grid_fill_rect()

```c
void
lrg_nav_grid_fill_rect (LrgNavGrid      *self,
                        gint             x,
                        gint             y,
                        guint            width,
                        guint            height,
                        LrgNavCellFlags  flags,
                        gfloat           cost)
```

Fills a rectangular area with specified flags and cost.

**Parameters:**
- `x`, `y`: Top-left corner
- `width`, `height`: Rectangle dimensions
- `flags`: Flags to set on all cells
- `cost`: Cost to set on all cells

**Example:**
```c
/* Mark a building as blocked */
lrg_nav_grid_fill_rect(grid, 30, 30, 10, 10, LRG_NAV_CELL_BLOCKED, 1.0f);

/* Mark swamp terrain */
lrg_nav_grid_fill_rect(grid, 50, 50, 20, 20, LRG_NAV_CELL_NONE, 2.0f);
```

### lrg_nav_grid_clear()

```c
void
lrg_nav_grid_clear (LrgNavGrid *self)
```

Resets all cells to default (walkable, cost 1.0).

**Example:**
```c
/* Reset grid for a new level */
lrg_nav_grid_clear(grid);
```

## Complete Example

```c
#include <glib.h>
#include <libregnum.h>

int main(void) {
    /* Create a 50x50 grid */
    g_autoptr(LrgNavGrid) grid = lrg_nav_grid_new(50, 50);

    /* Configure movement */
    lrg_nav_grid_set_allow_diagonal(grid, TRUE);
    lrg_nav_grid_set_cut_corners(grid, FALSE);

    /* Create a wall obstacle */
    for (gint i = 0; i < 40; i++) {
        lrg_nav_grid_set_blocked(grid, 25, i, TRUE);
    }

    /* Create swamp terrain with increased cost */
    lrg_nav_grid_fill_rect(grid, 5, 5, 15, 15, LRG_NAV_CELL_NONE, 3.0f);

    /* Verify grid setup */
    g_assert_cmpuint(lrg_nav_grid_get_width(grid), ==, 50);
    g_assert_cmpuint(lrg_nav_grid_get_height(grid), ==, 50);
    g_assert_true(lrg_nav_grid_is_valid(grid, 25, 20));
    g_assert_false(lrg_nav_grid_is_walkable(grid, 25, 20));
    g_assert_cmpfloat(lrg_nav_grid_get_cell_cost(grid, 10, 10), ==, 3.0f);

    return 0;
}
```

## Subclassing

`LrgNavGrid` is a derivable type, allowing you to create custom grid implementations. Override these virtual methods:

- `get_cell_cost(LrgNavGrid *self, gint x, gint y)` - Custom cost calculation
- `is_cell_walkable(LrgNavGrid *self, gint x, gint y)` - Custom walkability logic
- `get_neighbors(LrgNavGrid *self, gint x, gint y)` - Custom neighbor generation

## Related Types

- [LrgNavCell](nav-cell.md) - Individual cells
- [LrgPathfinder](pathfinder.md) - Uses grid for pathfinding
