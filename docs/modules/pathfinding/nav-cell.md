# LrgNavCell

A boxed type representing a single cell in a navigation grid. Cells contain position, movement cost, and optional flags.

## Type Information

- **Type Name**: `LrgNavCell`
- **Type ID**: `LRG_TYPE_NAV_CELL`
- **Category**: Boxed Type
- **Header**: `lrg-nav-cell.h`

## Description

`LrgNavCell` represents an individual tile in a pathfinding grid. It stores:

- **Position**: X and Y coordinates
- **Cost**: Movement cost multiplier (1.0 = normal, higher = slower)
- **Flags**: Navigation state flags (e.g., blocked, restricted)
- **User Data**: Optional custom data attached to the cell

## Creating Cells

### lrg_nav_cell_new()

```c
LrgNavCell *
lrg_nav_cell_new (gint            x,
                  gint            y,
                  gfloat          cost,
                  LrgNavCellFlags flags)
```

Creates a new navigation cell.

**Parameters:**
- `x`: X coordinate in the grid
- `y`: Y coordinate in the grid
- `cost`: Movement cost (1.0 = normal, higher = slower). Use 1.0 for standard cells.
- `flags`: Navigation flags. Common values:
  - `LRG_NAV_CELL_NONE`: No flags, fully walkable
  - `LRG_NAV_CELL_BLOCKED`: Cell is blocked and not walkable

**Returns:** (transfer full) A new `LrgNavCell`

**Example:**
```c
g_autoptr(LrgNavCell) cell = lrg_nav_cell_new(5, 10, 1.0f, LRG_NAV_CELL_NONE);
```

## Copying and Freeing

### lrg_nav_cell_copy()

```c
LrgNavCell *
lrg_nav_cell_copy (const LrgNavCell *self)
```

Creates a deep copy of a navigation cell, including all properties and user data.

**Returns:** (transfer full) A new `LrgNavCell` copy

### lrg_nav_cell_free()

```c
void
lrg_nav_cell_free (LrgNavCell *self)
```

Frees a navigation cell and calls the destroy function on any user data.

## Position Access

### lrg_nav_cell_get_x()

```c
gint
lrg_nav_cell_get_x (const LrgNavCell *self)
```

Gets the X coordinate. **Returns:** The X coordinate

### lrg_nav_cell_get_y()

```c
gint
lrg_nav_cell_get_y (const LrgNavCell *self)
```

Gets the Y coordinate. **Returns:** The Y coordinate

## Cost Management

### lrg_nav_cell_get_cost()

```c
gfloat
lrg_nav_cell_get_cost (const LrgNavCell *self)
```

Gets the movement cost. **Returns:** The movement cost multiplier

### lrg_nav_cell_set_cost()

```c
void
lrg_nav_cell_set_cost (LrgNavCell *self,
                       gfloat      cost)
```

Sets the movement cost. Higher costs increase the path cost for traversing this cell.

**Parameters:**
- `cost`: New movement cost (typically >= 1.0)

**Example:**
```c
lrg_nav_cell_set_cost(cell, 2.0f);  /* Slow cell - double cost */
```

## Flag Management

### lrg_nav_cell_get_flags()

```c
LrgNavCellFlags
lrg_nav_cell_get_flags (const LrgNavCell *self)
```

Gets all navigation flags. **Returns:** The navigation flags bitfield

### lrg_nav_cell_set_flags()

```c
void
lrg_nav_cell_set_flags (LrgNavCell      *self,
                        LrgNavCellFlags  flags)
```

Sets the navigation flags (replaces all flags).

**Parameters:**
- `flags`: New flags (use bitwise OR to combine multiple flags)

### lrg_nav_cell_has_flag()

```c
gboolean
lrg_nav_cell_has_flag (const LrgNavCell *self,
                       LrgNavCellFlags   flag)
```

Checks if a specific flag is set. **Returns:** `TRUE` if flag is set

**Example:**
```c
if (lrg_nav_cell_has_flag(cell, LRG_NAV_CELL_BLOCKED)) {
    g_print("Cell is blocked\n");
}
```

### lrg_nav_cell_is_walkable()

```c
gboolean
lrg_nav_cell_is_walkable (const LrgNavCell *self)
```

Checks if the cell is walkable (not blocked).

**Returns:** `TRUE` if walkable

**Equivalent to:** `!lrg_nav_cell_has_flag(cell, LRG_NAV_CELL_BLOCKED)`

## User Data

### lrg_nav_cell_get_user_data()

```c
gpointer
lrg_nav_cell_get_user_data (const LrgNavCell *self)
```

Gets user-defined data. **Returns:** (transfer none) (nullable) User data or NULL

### lrg_nav_cell_set_user_data()

```c
void
lrg_nav_cell_set_user_data (LrgNavCell     *self,
                            gpointer        user_data,
                            GDestroyNotify  destroy)
```

Attaches user-defined data to the cell.

**Parameters:**
- `user_data`: Data to attach (can be NULL)
- `destroy`: Function to clean up user_data when cell is freed (can be NULL)

**Example:**
```c
typedef struct {
    gchar *terrain_type;
    guint visibility;
} CellData;

CellData *data = g_new(CellData, 1);
data->terrain_type = g_strdup("grass");
data->visibility = 100;

lrg_nav_cell_set_user_data(cell, data, g_free);
```

## Complete Example

```c
#include <glib.h>
#include <libregnum.h>

int main(void) {
    /* Create a cell at (10, 20) with normal cost */
    g_autoptr(LrgNavCell) cell = lrg_nav_cell_new(10, 20, 1.0f, LRG_NAV_CELL_NONE);

    /* Check initial state */
    g_assert_cmpint(lrg_nav_cell_get_x(cell), ==, 10);
    g_assert_cmpint(lrg_nav_cell_get_y(cell), ==, 20);
    g_assert_true(lrg_nav_cell_is_walkable(cell));

    /* Mark as blocked */
    lrg_nav_cell_set_flags(cell, LRG_NAV_CELL_BLOCKED);
    g_assert_false(lrg_nav_cell_is_walkable(cell));

    /* Increase cost */
    lrg_nav_cell_set_cost(cell, 3.5f);
    g_assert_cmpfloat(lrg_nav_cell_get_cost(cell), ==, 3.5f);

    return 0;
}
```

## Navigation Flags

The following flags are defined in `lrg-enums.h`:

| Flag | Meaning |
|------|---------|
| `LRG_NAV_CELL_NONE` | No flags set - normal walkable cell |
| `LRG_NAV_CELL_BLOCKED` | Cell is blocked and not walkable |

## Related Types

- [LrgNavGrid](nav-grid.md) - Contains and manages nav cells
- [LrgPath](path.md) - Contains waypoints (which are path points, not cells)
