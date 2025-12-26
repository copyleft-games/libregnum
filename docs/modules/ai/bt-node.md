# LrgBTNode

A derivable base class for all behavior tree nodes, providing the interface for execution, state management, and resetting.

## Type Information

- **Type Name**: `LrgBTNode`
- **Type ID**: `LRG_TYPE_BT_NODE`
- **Category**: Derivable GObject (base class for BT nodes)
- **Header**: `lrg-bt-node.h`

## Description

`LrgBTNode` is the foundation of the behavior tree system. All concrete node types (actions, conditions, composites, decorators) inherit from this base class. It provides:

- **Execution**: The `tick()` method for processing
- **Status Tracking**: Current node status
- **State Management**: Reset and abort operations
- **Naming**: Optional names for debugging

## Node Status

All nodes return one of three statuses when executed:

```c
typedef enum
{
    LRG_BT_STATUS_SUCCESS = 0,   /* Task completed successfully */
    LRG_BT_STATUS_FAILURE = 1,   /* Task failed */
    LRG_BT_STATUS_RUNNING = 2    /* Task is still running */
} LrgBTStatus;
```

## Properties and Getters

### lrg_bt_node_get_name()

```c
const gchar *
lrg_bt_node_get_name (LrgBTNode *self)
```

Gets the node's name for debugging purposes.

**Returns:** (transfer none) (nullable) The node name or NULL

### lrg_bt_node_set_name()

```c
void
lrg_bt_node_set_name (LrgBTNode   *self,
                      const gchar *name)
```

Sets the node's name.

**Parameters:**
- `name`: (nullable) Name for debugging (NULL to clear)

**Example:**
```c
LrgBTAction *action = lrg_bt_action_new_simple(my_function);
lrg_bt_node_set_name(LRG_BT_NODE(action), "attack_enemy");
```

### lrg_bt_node_get_status()

```c
LrgBTStatus
lrg_bt_node_get_status (LrgBTNode *self)
```

Gets the current status of the node.

**Returns:** The node's status

**Note:** This is the status from the last execution.

## Execution

### lrg_bt_node_tick()

```c
LrgBTStatus
lrg_bt_node_tick (LrgBTNode     *self,
                  LrgBlackboard *blackboard,
                  gfloat         delta_time)
```

Executes one tick of the node.

**Parameters:**
- `blackboard`: The shared blackboard for state
- `delta_time`: Time elapsed since last tick (in seconds)

**Returns:** The result status

**Behavior:** Calls the virtual `tick()` method on the node class

**Example:**
```c
LrgBlackboard *bb = lrg_blackboard_new();
LrgBTAction *action = lrg_bt_action_new_simple(my_func);

LrgBTStatus status = lrg_bt_node_tick(LRG_BT_NODE(action), bb, 0.016f);

g_object_unref(action);
g_object_unref(bb);
```

## State Management

### lrg_bt_node_reset()

```c
void
lrg_bt_node_reset (LrgBTNode *self)
```

Resets the node to its initial state.

**Effect:**
- For simple actions: clears running flag
- For composites: resets child index tracking
- For decorators: propagates to child
- Status becomes undefined until next tick

**Use Case:** Starting a new execution cycle or switching behaviors

**Example:**
```c
LrgBTStatus status = lrg_bt_node_tick(node, bb, 0.016f);
if (status == LRG_BT_STATUS_RUNNING) {
    /* Continue next frame without reset */
} else {
    /* Done, prepare for next execution */
    lrg_bt_node_reset(node);
}
```

### lrg_bt_node_abort()

```c
void
lrg_bt_node_abort (LrgBTNode *self)
```

Aborts a currently running node.

**Effect:**
- For running actions: triggers cleanup
- For composites: aborts all children
- For decorators: aborts child

**Use Case:** Interrupting behavior (e.g., being hit while attacking)

**Example:**
```c
if (was_interrupted) {
    lrg_bt_node_abort(node);
    lrg_bt_node_reset(node);
}
```

### lrg_bt_node_is_running()

```c
gboolean
lrg_bt_node_is_running (LrgBTNode *self)
```

Checks if the node is currently running.

**Returns:** `TRUE` if status is `LRG_BT_STATUS_RUNNING`

**Example:**
```c
if (lrg_bt_node_is_running(node)) {
    g_print("Still executing...\n");
}
```

## Node Class Structure

The `LrgBTNodeClass` defines virtual methods for customization:

```c
struct _LrgBTNodeClass
{
    GObjectClass parent_class;

    /* Virtual methods */
    LrgBTStatus (*tick)   (LrgBTNode     *self,
                           LrgBlackboard *blackboard,
                           gfloat         delta_time);

    void        (*reset)  (LrgBTNode     *self);

    void        (*abort)  (LrgBTNode     *self);

    gpointer _reserved[8];  /* ABI stability */
};
```

When subclassing, override these methods to implement custom node behavior.

## Complete Example: Using Different Node Types

```c
#include <glib.h>
#include <libregnum.h>

int main(void) {
    g_autoptr(LrgBlackboard) bb = lrg_blackboard_new();

    /* Create different node types */
    g_autoptr(LrgBTAction) action = lrg_bt_action_new_simple(some_action);
    g_autoptr(LrgBTCondition) condition = lrg_bt_condition_new_simple(some_condition);
    g_autoptr(LrgBTWait) wait = lrg_bt_wait_new(2.0f);

    /* Set names for debugging */
    lrg_bt_node_set_name(LRG_BT_NODE(action), "attack");
    lrg_bt_node_set_name(LRG_BT_NODE(condition), "check_target");
    lrg_bt_node_set_name(LRG_BT_NODE(wait), "cooldown");

    /* Execute nodes */
    g_print("=== Executing nodes ===\n");

    LrgBTStatus status = lrg_bt_node_tick(LRG_BT_NODE(action), bb, 0.016f);
    g_print("%s: %d\n", lrg_bt_node_get_name(LRG_BT_NODE(action)), status);

    status = lrg_bt_node_tick(LRG_BT_NODE(condition), bb, 0.016f);
    g_print("%s: %d\n", lrg_bt_node_get_name(LRG_BT_NODE(condition)), status);

    status = lrg_bt_node_tick(LRG_BT_NODE(wait), bb, 0.016f);
    g_print("%s: %d (running)\n", lrg_bt_node_get_name(LRG_BT_NODE(wait)), status);

    /* Check running status */
    if (lrg_bt_node_is_running(LRG_BT_NODE(wait))) {
        g_print("Wait node is still running\n");
    }

    /* Abort running node */
    lrg_bt_node_abort(LRG_BT_NODE(wait));
    g_print("Aborted wait node\n");

    /* Reset for next cycle */
    lrg_bt_node_reset(LRG_BT_NODE(wait));
    g_print("Reset wait node\n");

    return 0;
}
```

## Node Lifecycle

Typical node lifecycle in a tree:

```
1. Creation
   ├── lrg_bt_*_new() creates node
   └── lrg_bt_node_set_name() optionally names it

2. Execution Cycle (repeated)
   ├── lrg_bt_node_tick() executes node
   │   ├── if returns RUNNING: continue next frame
   │   └── if returns SUCCESS/FAILURE: process result
   └── lrg_bt_node_reset() resets state

3. Interruption (optional)
   ├── lrg_bt_node_abort() stops running node
   └── lrg_bt_node_reset() clears state

4. Destruction
   └── g_object_unref() or g_autoptr cleanup
```

## Status Flow Semantics

| Status | Meaning | Next Action |
|--------|---------|-------------|
| `SUCCESS` | Completed successfully | Can move to next node or end |
| `FAILURE` | Could not complete | Handle failure, retry, or switch |
| `RUNNING` | Still executing | Keep ticking, don't reset |

## Best Practices

1. **Set Names**: Always name nodes for debugging
   ```c
   lrg_bt_node_set_name(node, "descriptive_name");
   ```

2. **Handle Status Properly**: Check return status and reset appropriately
   ```c
   LrgBTStatus status = lrg_bt_node_tick(node, bb, dt);
   if (status != LRG_BT_STATUS_RUNNING) {
       lrg_bt_node_reset(node);
   }
   ```

3. **Abort on Interruption**: Stop running nodes cleanly
   ```c
   if (interrupted) {
       lrg_bt_node_abort(node);
       lrg_bt_node_reset(node);
   }
   ```

## Related Types

- [LrgBTComposite](bt-composite.md) - Parent nodes
- [LrgBTDecorator](bt-decorator.md) - Wrapper nodes
- [LrgBTLeaf](bt-leaf.md) - Terminal nodes
- [LrgBlackboard](blackboard.md) - State sharing
