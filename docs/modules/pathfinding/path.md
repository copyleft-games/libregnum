# LrgPath

A boxed type representing a path result containing a sequence of waypoints from start to goal position.

## Type Information

- **Type Name**: `LrgPath`
- **Type ID**: `LRG_TYPE_PATH`
- **Category**: Boxed Type
- **Header**: `lrg-path.h`

## Description

`LrgPath` is the result type returned by pathfinding operations. It contains:

- **Waypoints**: An ordered sequence of (x, y) coordinates from start to goal
- **Total Cost**: The combined movement cost of the entire path
- **Position Info**: Quick access to start, end, and any waypoint

## Creating Paths

### lrg_path_new()

```c
LrgPath *
lrg_path_new (void)
```

Creates a new empty path.

**Returns:** (transfer full) A new `LrgPath`

**Example:**
```c
g_autoptr(LrgPath) path = lrg_path_new();
```

## Copying and Freeing

### lrg_path_copy()

```c
LrgPath *
lrg_path_copy (const LrgPath *self)
```

Creates a deep copy of a path.

**Returns:** (transfer full) A copy of `self`

### lrg_path_free()

```c
void
lrg_path_free (LrgPath *self)
```

Frees a path and its waypoint data.

## Adding Waypoints

### lrg_path_append()

```c
void
lrg_path_append (LrgPath *self,
                 gint     x,
                 gint     y)
```

Appends a waypoint to the end of the path.

**Parameters:**
- `x`: X coordinate
- `y`: Y coordinate

**Example:**
```c
lrg_path_append(path, 10, 10);
lrg_path_append(path, 15, 15);
lrg_path_append(path, 20, 20);
```

### lrg_path_prepend()

```c
void
lrg_path_prepend (LrgPath *self,
                  gint     x,
                  gint     y)
```

Prepends a waypoint to the beginning of the path.

**Example:**
```c
g_autoptr(LrgPath) path = lrg_path_new();
lrg_path_append(path, 20, 20);
lrg_path_prepend(path, 10, 10);  /* Now starts at (10,10) */
```

## Path Length

### lrg_path_get_length()

```c
guint
lrg_path_get_length (const LrgPath *self)
```

Gets the number of waypoints in the path.

**Returns:** Number of waypoints

### lrg_path_is_empty()

```c
gboolean
lrg_path_is_empty (const LrgPath *self)
```

Checks if the path is empty.

**Returns:** `TRUE` if path contains no waypoints

**Equivalent to:** `lrg_path_get_length(path) == 0`

## Waypoint Access

### lrg_path_get_point()

```c
gboolean
lrg_path_get_point (const LrgPath *self,
                    guint          index,
                    gint          *x,
                    gint          *y)
```

Gets a waypoint at the specified index.

**Parameters:**
- `index`: Waypoint index (0 = first)
- `x`: (out) X coordinate
- `y`: (out) Y coordinate

**Returns:** `TRUE` if index is valid

**Example:**
```c
gint x, y;
if (lrg_path_get_point(path, 5, &x, &y)) {
    g_print("Waypoint 5: (%d, %d)\n", x, y);
}
```

### lrg_path_get_start()

```c
gboolean
lrg_path_get_start (const LrgPath *self,
                    gint          *x,
                    gint          *y)
```

Gets the starting waypoint.

**Parameters:**
- `x`: (out) X coordinate
- `y`: (out) Y coordinate

**Returns:** `TRUE` if path is not empty

**Example:**
```c
gint start_x, start_y;
if (lrg_path_get_start(path, &start_x, &start_y)) {
    g_print("Start: (%d, %d)\n", start_x, start_y);
}
```

### lrg_path_get_end()

```c
gboolean
lrg_path_get_end (const LrgPath *self,
                  gint          *x,
                  gint          *y)
```

Gets the ending waypoint.

**Parameters:**
- `x`: (out) X coordinate
- `y`: (out) Y coordinate

**Returns:** `TRUE` if path is not empty

**Example:**
```c
gint end_x, end_y;
if (lrg_path_get_end(path, &end_x, &end_y)) {
    g_print("End: (%d, %d)\n", end_x, end_y);
}
```

## Path Manipulation

### lrg_path_reverse()

```c
void
lrg_path_reverse (LrgPath *self)
```

Reverses the path in place (start becomes end, vice versa).

**Example:**
```c
/* Original: (0,0) -> (1,1) -> (2,2) */
lrg_path_reverse(path);
/* After: (2,2) -> (1,1) -> (0,0) */
```

### lrg_path_clear()

```c
void
lrg_path_clear (LrgPath *self)
```

Removes all waypoints from the path.

## Cost Management

### lrg_path_get_total_cost()

```c
gfloat
lrg_path_get_total_cost (const LrgPath *self)
```

Gets the total movement cost of the path.

**Returns:** Total path cost

### lrg_path_set_total_cost()

```c
void
lrg_path_set_total_cost (LrgPath *self,
                         gfloat   cost)
```

Sets the total path cost.

**Note:** This is typically set by the pathfinder after path computation.

## Iteration

### lrg_path_foreach()

```c
void
lrg_path_foreach (const LrgPath      *self,
                  LrgPathForeachFunc  func,
                  gpointer            user_data)
```

Iterates over all waypoints in the path.

**Parameters:**
- `func`: Callback function called for each waypoint
- `user_data`: Arbitrary user data passed to callback

**Callback Signature:**
```c
typedef void (*LrgPathForeachFunc) (gint     x,
                                    gint     y,
                                    guint    index,
                                    gpointer user_data);
```

**Example:**
```c
static void
print_waypoint(gint x, gint y, guint index, gpointer user_data)
{
    (void)user_data;
    g_print("Waypoint %u: (%d, %d)\n", index, x, y);
}

/* Print all waypoints */
lrg_path_foreach(path, print_waypoint, NULL);
```

## Complete Example

```c
#include <glib.h>
#include <libregnum.h>

static void
print_waypoint(gint x, gint y, guint index, gpointer user_data)
{
    (void)user_data;
    g_print("  [%u] (%d, %d)\n", index, x, y);
}

int main(void) {
    /* Create and populate a path */
    g_autoptr(LrgPath) path = lrg_path_new();
    lrg_path_append(path, 0, 0);
    lrg_path_append(path, 5, 5);
    lrg_path_append(path, 10, 10);
    lrg_path_set_total_cost(path, 14.14f);

    /* Check basic info */
    g_assert_cmpuint(lrg_path_get_length(path), ==, 3);
    g_assert_false(lrg_path_is_empty(path));
    g_assert_cmpfloat(lrg_path_get_total_cost(path), ==, 14.14f);

    /* Access endpoints */
    gint start_x, start_y;
    lrg_path_get_start(path, &start_x, &start_y);
    g_assert_cmpint(start_x, ==, 0);
    g_assert_cmpint(start_y, ==, 0);

    gint end_x, end_y;
    lrg_path_get_end(path, &end_x, &end_y);
    g_assert_cmpint(end_x, ==, 10);
    g_assert_cmpint(end_y, ==, 10);

    /* Print waypoints */
    g_print("Path with %u waypoints (cost: %.2f):\n",
            lrg_path_get_length(path),
            lrg_path_get_total_cost(path));
    lrg_path_foreach(path, print_waypoint, NULL);

    /* Test reverse */
    lrg_path_reverse(path);
    gint rev_start_x, rev_start_y;
    lrg_path_get_start(path, &rev_start_x, &rev_start_y);
    g_assert_cmpint(rev_start_x, ==, 10);
    g_assert_cmpint(rev_start_y, ==, 10);

    return 0;
}
```

## Path Points

The `LrgPathPoint` structure represents a single waypoint:

```c
typedef struct
{
    gint x;
    gint y;
} LrgPathPoint;
```

Internal use only - access waypoints via the public API functions.

## Related Types

- [LrgPathfinder](pathfinder.md) - Generates paths
- [LrgNavGrid](nav-grid.md) - Navigation grid for pathfinding
