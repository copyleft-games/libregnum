# LrgBlackboard

A final GObject that serves as a shared key-value data store for behavior tree communication and state management.

## Type Information

- **Type Name**: `LrgBlackboard`
- **Type ID**: `LRG_TYPE_BLACKBOARD`
- **Category**: Final GObject (cannot be subclassed)
- **Header**: `lrg-blackboard.h`

## Description

`LrgBlackboard` is a type-safe dictionary that stores arbitrary data accessible to all nodes in a behavior tree. Key features:

- **Type Safety**: Separate methods for different data types
- **Default Values**: Return sensible defaults when keys don't exist
- **Reference Counting**: Automatically manages GObject references
- **Key Management**: Query and manipulate stored keys
- **Multiple Types**: int, float, bool, string, object, pointer

## Creating a Blackboard

### lrg_blackboard_new()

```c
LrgBlackboard *
lrg_blackboard_new (void)
```

Creates a new empty blackboard.

**Returns:** (transfer full) A new `LrgBlackboard`

**Example:**
```c
g_autoptr(LrgBlackboard) bb = lrg_blackboard_new();
```

## Integer Values

### lrg_blackboard_set_int()

```c
void
lrg_blackboard_set_int (LrgBlackboard *self,
                        const gchar   *key,
                        gint           value)
```

Sets an integer value.

**Parameters:**
- `key`: Key name
- `value`: Integer value

**Example:**
```c
lrg_blackboard_set_int(bb, "health", 100);
lrg_blackboard_set_int(bb, "ammunition", 30);
```

### lrg_blackboard_get_int()

```c
gint
lrg_blackboard_get_int (LrgBlackboard *self,
                        const gchar   *key,
                        gint           default_value)
```

Gets an integer value.

**Parameters:**
- `key`: Key name
- `default_value`: Value to return if key doesn't exist

**Returns:** Integer value or default if not found

**Example:**
```c
gint health = lrg_blackboard_get_int(bb, "health", 100);
gint ammo = lrg_blackboard_get_int(bb, "ammunition", 0);
```

## Float Values

### lrg_blackboard_set_float()

```c
void
lrg_blackboard_set_float (LrgBlackboard *self,
                          const gchar   *key,
                          gfloat         value)
```

Sets a float value.

**Example:**
```c
lrg_blackboard_set_float(bb, "speed", 5.5f);
lrg_blackboard_set_float(bb, "patrol_radius", 25.0f);
```

### lrg_blackboard_get_float()

```c
gfloat
lrg_blackboard_get_float (LrgBlackboard *self,
                          const gchar   *key,
                          gfloat         default_value)
```

Gets a float value.

**Parameters:**
- `key`: Key name
- `default_value`: Default if key not found

**Returns:** Float value or default

**Example:**
```c
gfloat speed = lrg_blackboard_get_float(bb, "speed", 3.0f);
```

## Boolean Values

### lrg_blackboard_set_bool()

```c
void
lrg_blackboard_set_bool (LrgBlackboard *self,
                         const gchar   *key,
                         gboolean       value)
```

Sets a boolean value.

**Example:**
```c
lrg_blackboard_set_bool(bb, "in_combat", TRUE);
lrg_blackboard_set_bool(bb, "can_see_player", FALSE);
```

### lrg_blackboard_get_bool()

```c
gboolean
lrg_blackboard_get_bool (LrgBlackboard *self,
                         const gchar   *key,
                         gboolean       default_value)
```

Gets a boolean value.

**Example:**
```c
if (lrg_blackboard_get_bool(bb, "in_combat", FALSE)) {
    /* Execute combat behavior */
}
```

## String Values

### lrg_blackboard_set_string()

```c
void
lrg_blackboard_set_string (LrgBlackboard *self,
                           const gchar   *key,
                           const gchar   *value)
```

Sets a string value. Pass NULL to unset.

**Example:**
```c
lrg_blackboard_set_string(bb, "target_name", "enemy1");
lrg_blackboard_set_string(bb, "current_state", "patrolling");
```

### lrg_blackboard_get_string()

```c
const gchar *
lrg_blackboard_get_string (LrgBlackboard *self,
                           const gchar   *key)
```

Gets a string value.

**Returns:** (transfer none) (nullable) String or NULL if not found

**Example:**
```c
const gchar *target = lrg_blackboard_get_string(bb, "target_name");
if (target != NULL) {
    g_print("Target: %s\n", target);
}
```

## Object Values

### lrg_blackboard_set_object()

```c
void
lrg_blackboard_set_object (LrgBlackboard *self,
                           const gchar   *key,
                           GObject       *object)
```

Stores a GObject reference. The blackboard takes a reference.

**Example:**
```c
LrgNavGrid *grid = /* ... */;
lrg_blackboard_set_object(bb, "nav_grid", G_OBJECT(grid));
```

### lrg_blackboard_get_object()

```c
GObject *
lrg_blackboard_get_object (LrgBlackboard *self,
                           const gchar   *key)
```

Gets a GObject reference.

**Returns:** (transfer none) (nullable) Object or NULL if not found

**Example:**
```c
GObject *obj = lrg_blackboard_get_object(bb, "nav_grid");
if (obj != NULL) {
    LrgNavGrid *grid = LRG_NAV_GRID(obj);
    /* Use grid */
}
```

## Pointer Values

### lrg_blackboard_set_pointer()

```c
void
lrg_blackboard_set_pointer (LrgBlackboard  *self,
                            const gchar    *key,
                            gpointer        pointer,
                            GDestroyNotify  destroy)
```

Stores an arbitrary pointer with optional cleanup.

**Parameters:**
- `key`: Key name
- `pointer`: Pointer to store (NULL to unset)
- `destroy`: Cleanup function (NULL if none needed)

**Example:**
```c
typedef struct {
    gint x, y;
    gfloat health;
} Enemy;

Enemy *enemy = g_new(Enemy, 1);
enemy->x = 50;
enemy->y = 50;
enemy->health = 100.0f;

lrg_blackboard_set_pointer(bb, "target_enemy", enemy, g_free);
```

### lrg_blackboard_get_pointer()

```c
gpointer
lrg_blackboard_get_pointer (LrgBlackboard *self,
                            const gchar   *key)
```

Gets a pointer value.

**Returns:** (nullable) Pointer or NULL if not found

**Example:**
```c
Enemy *target = lrg_blackboard_get_pointer(bb, "target_enemy");
if (target != NULL) {
    g_print("Target at (%d, %d)\n", target->x, target->y);
}
```

## Key Management

### lrg_blackboard_has_key()

```c
gboolean
lrg_blackboard_has_key (LrgBlackboard *self,
                        const gchar   *key)
```

Checks if a key exists.

**Returns:** `TRUE` if key is present

**Example:**
```c
if (lrg_blackboard_has_key(bb, "target_enemy")) {
    /* We have a target */
}
```

### lrg_blackboard_remove()

```c
gboolean
lrg_blackboard_remove (LrgBlackboard *self,
                       const gchar   *key)
```

Removes a key and its value.

**Returns:** `TRUE` if key was removed, `FALSE` if not found

**Example:**
```c
if (lrg_blackboard_remove(bb, "target_enemy")) {
    g_print("Target removed\n");
}
```

### lrg_blackboard_get_keys()

```c
GList *
lrg_blackboard_get_keys (LrgBlackboard *self)
```

Gets all keys in the blackboard.

**Returns:** (transfer container) (element-type utf8) List of key names

**Note:** Must call `g_list_free()` on the returned list

**Example:**
```c
GList *keys = lrg_blackboard_get_keys(bb);
for (GList *iter = keys; iter != NULL; iter = iter->next) {
    const gchar *key = (const gchar *)iter->data;
    g_print("Key: %s\n", key);
}
g_list_free(keys);
```

## Clear and Reset

### lrg_blackboard_clear()

```c
void
lrg_blackboard_clear (LrgBlackboard *self)
```

Removes all entries from the blackboard.

**Example:**
```c
/* Reset for new level */
lrg_blackboard_clear(bb);
```

## Complete Example

```c
#include <glib.h>
#include <libregnum.h>

int main(void) {
    g_autoptr(LrgBlackboard) bb = lrg_blackboard_new();

    /* Store various types of data */
    lrg_blackboard_set_int(bb, "health", 100);
    lrg_blackboard_set_float(bb, "speed", 5.5f);
    lrg_blackboard_set_bool(bb, "in_combat", TRUE);
    lrg_blackboard_set_string(bb, "state", "attacking");

    /* Retrieve and verify */
    g_assert_cmpint(lrg_blackboard_get_int(bb, "health", 0), ==, 100);
    g_assert_cmpfloat_with_epsilon(
        lrg_blackboard_get_float(bb, "speed", 0.0f), 5.5f, 0.001f);
    g_assert_true(lrg_blackboard_get_bool(bb, "in_combat", FALSE));
    g_assert_cmpstr(lrg_blackboard_get_string(bb, "state"), ==, "attacking");

    /* Check keys */
    g_assert_true(lrg_blackboard_has_key(bb, "health"));
    g_assert_false(lrg_blackboard_has_key(bb, "nonexistent"));

    /* Get all keys */
    GList *keys = lrg_blackboard_get_keys(bb);
    g_print("Blackboard contains %u keys\n", g_list_length(keys));
    g_list_free(keys);

    /* Remove a key */
    g_assert_true(lrg_blackboard_remove(bb, "health"));
    g_assert_false(lrg_blackboard_has_key(bb, "health"));

    /* Clear all */
    lrg_blackboard_clear(bb);
    g_assert_cmpuint(g_list_length(lrg_blackboard_get_keys(bb)), ==, 0);

    return 0;
}
```

## Usage in Behavior Trees

The blackboard is typically obtained from a behavior tree and used to share state:

```c
LrgBehaviorTree *tree = lrg_behavior_tree_new();
LrgBlackboard *bb = lrg_behavior_tree_get_blackboard(tree);

/* Set initial state */
lrg_blackboard_set_int(bb, "health", 100);
lrg_blackboard_set_bool(bb, "player_visible", FALSE);

/* Nodes access the same blackboard during execution */
```

## Related Types

- [LrgBehaviorTree](behavior-tree.md) - Contains the blackboard
- [LrgBTNode](bt-node.md) - Nodes receive blackboard parameter
- [LrgBTLeaf](bt-leaf.md) - Leaf nodes read/write blackboard
