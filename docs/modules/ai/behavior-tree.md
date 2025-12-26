# LrgBehaviorTree

A final GObject that serves as the top-level container and executor for a behavior tree.

## Type Information

- **Type Name**: `LrgBehaviorTree`
- **Type ID**: `LRG_TYPE_BEHAVIOR_TREE`
- **Category**: Final GObject (cannot be subclassed)
- **Header**: `lrg-behavior-tree.h`

## Description

`LrgBehaviorTree` is the main entry point for using behavior trees. It manages:

- **Root Node**: The top-level behavior tree node
- **Blackboard**: Shared data store for all nodes
- **Execution**: Ticking the tree each frame
- **State**: Tracking running/completion status

## Creating Trees

### lrg_behavior_tree_new()

```c
LrgBehaviorTree *
lrg_behavior_tree_new (void)
```

Creates a new empty behavior tree without a root node.

**Returns:** (transfer full) A new `LrgBehaviorTree`

**Example:**
```c
g_autoptr(LrgBehaviorTree) tree = lrg_behavior_tree_new();
```

### lrg_behavior_tree_new_with_root()

```c
LrgBehaviorTree *
lrg_behavior_tree_new_with_root (LrgBTNode *root)
```

Creates a new behavior tree with a root node.

**Parameters:**
- `root`: (transfer none) The root node

**Returns:** (transfer full) A new `LrgBehaviorTree`

**Example:**
```c
LrgBTSelector *root = lrg_bt_selector_new();
g_autoptr(LrgBehaviorTree) tree = lrg_behavior_tree_new_with_root(LRG_BT_NODE(root));
```

## Root Node Management

### lrg_behavior_tree_get_root()

```c
LrgBTNode *
lrg_behavior_tree_get_root (LrgBehaviorTree *self)
```

Gets the root node of the tree.

**Returns:** (transfer none) (nullable) The root node or NULL

### lrg_behavior_tree_set_root()

```c
void
lrg_behavior_tree_set_root (LrgBehaviorTree *self,
                            LrgBTNode       *root)
```

Sets the root node.

**Parameters:**
- `root`: (nullable) (transfer none) The root node (NULL to clear)

**Example:**
```c
LrgBTSequence *sequence = lrg_bt_sequence_new();
/* ... populate sequence ... */
lrg_behavior_tree_set_root(tree, LRG_BT_NODE(sequence));
```

## Blackboard Access

### lrg_behavior_tree_get_blackboard()

```c
LrgBlackboard *
lrg_behavior_tree_get_blackboard (LrgBehaviorTree *self)
```

Gets the shared blackboard for this tree.

**Returns:** (transfer none) The blackboard

**Example:**
```c
LrgBlackboard *bb = lrg_behavior_tree_get_blackboard(tree);
lrg_blackboard_set_int(bb, "health", 100);
```

## Tree Execution

### lrg_behavior_tree_tick()

```c
LrgBTStatus
lrg_behavior_tree_tick (LrgBehaviorTree *self,
                        gfloat           delta_time)
```

Executes one tick of the behavior tree.

**Parameters:**
- `delta_time`: Time elapsed since last tick (in seconds)

**Returns:** The tree's status after this tick

**Status Values:**
- `LRG_BT_STATUS_SUCCESS` - Tree completed successfully
- `LRG_BT_STATUS_FAILURE` - Tree failed
- `LRG_BT_STATUS_RUNNING` - Tree still running

**Example:**
```c
/* In game loop */
gfloat delta_time = 0.016f;  /* ~60 FPS */
LrgBTStatus status = lrg_behavior_tree_tick(tree, delta_time);

switch (status) {
    case LRG_BT_STATUS_SUCCESS:
        g_print("Behavior completed successfully\n");
        break;
    case LRG_BT_STATUS_FAILURE:
        g_print("Behavior failed\n");
        break;
    case LRG_BT_STATUS_RUNNING:
        g_print("Behavior still running\n");
        break;
}
```

## Tree State Management

### lrg_behavior_tree_get_status()

```c
LrgBTStatus
lrg_behavior_tree_get_status (LrgBehaviorTree *self)
```

Gets the current status of the tree.

**Returns:** Current tree status

**Note:** This is the status from the last tick.

### lrg_behavior_tree_is_running()

```c
gboolean
lrg_behavior_tree_is_running (LrgBehaviorTree *self)
```

Checks if the tree is currently running.

**Returns:** `TRUE` if status is `LRG_BT_STATUS_RUNNING`

**Example:**
```c
if (lrg_behavior_tree_is_running(tree)) {
    g_print("Still executing...\n");
}
```

### lrg_behavior_tree_reset()

```c
void
lrg_behavior_tree_reset (LrgBehaviorTree *self)
```

Resets the tree to its initial state.

**Effect:**
- Clears all running state
- Resets all nodes
- Status becomes undefined until next tick

**Use Case:** When starting a new behavior sequence

**Example:**
```c
/* Finished current behavior, start new one */
lrg_behavior_tree_reset(tree);
lrg_behavior_tree_set_root(tree, new_root);
```

### lrg_behavior_tree_abort()

```c
void
lrg_behavior_tree_abort (LrgBehaviorTree *self)
```

Aborts any running nodes in the tree.

**Effect:**
- Stops all currently running nodes
- Calls abort on all nodes in the tree
- May trigger cleanup in running nodes

**Use Case:** Interrupted behavior or emergency stop

**Example:**
```c
/* Player entered combat, abort patrol behavior */
lrg_behavior_tree_abort(old_tree);
lrg_behavior_tree_set_root(combat_tree, combat_root);
```

## Complete Example: Simple Enemy AI

```c
#include <glib.h>
#include <libregnum.h>

/* Action: patrol */
static LrgBTStatus
patrol_action(LrgBlackboard *bb, gfloat delta_time, gpointer user_data)
{
    (void)delta_time;
    (void)user_data;

    gint patrol_x = lrg_blackboard_get_int(bb, "patrol_x", 0);
    patrol_x += 1;
    if (patrol_x > 50) patrol_x = 0;

    lrg_blackboard_set_int(bb, "patrol_x", patrol_x);
    g_print("Patrolling at x=%d\n", patrol_x);

    return LRG_BT_STATUS_SUCCESS;
}

/* Condition: player visible? */
static gboolean
is_player_visible(LrgBlackboard *bb, gpointer user_data)
{
    (void)user_data;
    return lrg_blackboard_get_bool(bb, "player_visible", FALSE);
}

/* Action: chase player */
static LrgBTStatus
chase_action(LrgBlackboard *bb, gfloat delta_time, gpointer user_data)
{
    (void)delta_time;
    (void)user_data;

    gint player_x = lrg_blackboard_get_int(bb, "player_x", 50);
    gint enemy_x = lrg_blackboard_get_int(bb, "enemy_x", 0);

    if (enemy_x < player_x) enemy_x++;
    else if (enemy_x > player_x) enemy_x--;

    lrg_blackboard_set_int(bb, "enemy_x", enemy_x);
    g_print("Chasing player at x=%d\n", enemy_x);

    return LRG_BT_STATUS_RUNNING;
}

int main(void) {
    /* Create tree */
    g_autoptr(LrgBehaviorTree) tree = lrg_behavior_tree_new();
    LrgBlackboard *bb = lrg_behavior_tree_get_blackboard(tree);

    /* Initialize blackboard */
    lrg_blackboard_set_int(bb, "enemy_x", 25);
    lrg_blackboard_set_int(bb, "patrol_x", 0);
    lrg_blackboard_set_bool(bb, "player_visible", FALSE);

    /* Build tree: selector with attack sequence and patrol fallback
     * Selector (if...else)
     *   ├── Sequence (must see player AND chase)
     *   │     ├── Condition: is_player_visible
     *   │     └── Action: chase
     *   └── Action: patrol (fallback)
     */
    g_autoptr(LrgBTSelector) root = lrg_bt_selector_new();
    g_autoptr(LrgBTSequence) chase_seq = lrg_bt_sequence_new();
    g_autoptr(LrgBTCondition) see_player = lrg_bt_condition_new_simple(is_player_visible);
    g_autoptr(LrgBTAction) chase = lrg_bt_action_new_simple(chase_action);
    g_autoptr(LrgBTAction) patrol = lrg_bt_action_new_simple(patrol_action);

    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(chase_seq), LRG_BT_NODE(see_player));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(chase_seq), LRG_BT_NODE(chase));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(root), LRG_BT_NODE(chase_seq));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(root), LRG_BT_NODE(patrol));

    lrg_behavior_tree_set_root(tree, LRG_BT_NODE(root));

    /* Simulate game loop */
    g_print("=== Phase 1: Patrol (player not visible) ===\n");
    for (gint i = 0; i < 3; i++) {
        LrgBTStatus status = lrg_behavior_tree_tick(tree, 0.016f);
        g_print("Status: %d\n", status);
        lrg_behavior_tree_reset(tree);
    }

    g_print("\n=== Phase 2: Chase (player visible) ===\n");
    lrg_blackboard_set_bool(bb, "player_visible", TRUE);
    lrg_blackboard_set_int(bb, "player_x", 50);

    for (gint i = 0; i < 3; i++) {
        LrgBTStatus status = lrg_behavior_tree_tick(tree, 0.016f);
        gint enemy_x = lrg_blackboard_get_int(bb, "enemy_x", 0);
        g_print("Status: %d, Enemy at x=%d\n", status, enemy_x);
        lrg_behavior_tree_reset(tree);
    }

    return 0;
}
```

## Game Loop Integration

Typical usage in a game loop:

```c
/* Initialization */
LrgBehaviorTree *ai_tree = lrg_behavior_tree_new();
lrg_behavior_tree_set_root(ai_tree, root_node);

/* Game loop */
while (running) {
    /* ... other game logic ... */

    /* Update AI */
    LrgBlackboard *bb = lrg_behavior_tree_get_blackboard(ai_tree);
    lrg_blackboard_set_bool(bb, "player_visible", player_in_view);
    lrg_blackboard_set_int(bb, "player_x", player_position_x);

    LrgBTStatus status = lrg_behavior_tree_tick(ai_tree, delta_time);

    /* Process tree result if needed */
    if (status == LRG_BT_STATUS_SUCCESS) {
        /* Behavior completed, can switch to new behavior */
        lrg_behavior_tree_reset(ai_tree);
        lrg_behavior_tree_set_root(ai_tree, next_behavior);
    }

    /* ... render ... */
}

g_object_unref(ai_tree);
```

## Status Handling

Each tick returns a status that indicates the tree's state:

| Status | Meaning | Action |
|--------|---------|--------|
| `SUCCESS` | Tree completed | Consider behavior done, may switch trees |
| `FAILURE` | Tree failed | Handle failure case, may retry or switch |
| `RUNNING` | Tree still executing | Continue ticking next frame |

## Related Types

- [LrgBlackboard](blackboard.md) - Shared data storage
- [LrgBTNode](bt-node.md) - Base node class
- [LrgBTComposite](bt-composite.md) - Composite node types
- [LrgBTDecorator](bt-decorator.md) - Decorator node types
- [LrgBTLeaf](bt-leaf.md) - Leaf node types
