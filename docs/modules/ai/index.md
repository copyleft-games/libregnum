# AI Module

The AI module provides a flexible behavior tree framework for implementing game AI decision-making systems. It supports both simple and complex AI behaviors through a hierarchical composition of nodes.

## Overview

The module is built around six core components:

- **LrgBlackboard**: Shared data store for behavior tree state and communication
- **LrgBehaviorTree**: The top-level behavior tree container and executor
- **LrgBTNode**: Base class for all behavior tree nodes
- **LrgBTComposite**: Parent nodes that contain children (Sequence, Selector, Parallel)
- **LrgBTDecorator**: Wrapper nodes that modify child behavior (Inverter, Repeater, etc.)
- **LrgBTLeaf**: Terminal nodes that perform actions (Action, Condition, Wait)

## Key Concepts

### Status Flow

Each node execution returns one of three statuses:

- **SUCCESS**: Task completed successfully
- **FAILURE**: Task failed
- **RUNNING**: Task is ongoing, needs more ticks

### Behavior Tree Execution

Trees are executed by repeatedly calling `tick()` with a delta time. Each tick:

1. Updates the root node
2. Root recursively updates its children
3. Composite nodes control child execution based on their logic
4. Leaf nodes perform actual work

### Blackboard

The blackboard is a shared key-value store accessible to all nodes:

- **Type-Safe**: Separate getters/setters for int, float, bool, string, object, pointer
- **Default Values**: Return defaults when keys don't exist
- **Ownership**: Handles object reference counting

## Basic Usage

```c
#include <libregnum.h>

/* Action function */
static LrgBTStatus
move_action(LrgBlackboard *blackboard, gfloat delta_time, gpointer user_data)
{
    (void)blackboard;
    (void)delta_time;
    (void)user_data;

    g_print("Moving...\n");
    return LRG_BT_STATUS_SUCCESS;
}

/* Condition function */
static gboolean
is_enemy_visible(LrgBlackboard *blackboard, gpointer user_data)
{
    (void)user_data;
    gint enemies_seen = lrg_blackboard_get_int(blackboard, "enemies_seen", 0);
    return enemies_seen > 0;
}

int main(void) {
    /* Create behavior tree */
    LrgBehaviorTree *tree = lrg_behavior_tree_new();
    LrgBlackboard *bb = lrg_behavior_tree_get_blackboard(tree);

    /* Create a simple sequence: check for enemy, then move */
    LrgBTSequence *sequence = lrg_bt_sequence_new();
    LrgBTCondition *condition = lrg_bt_condition_new_simple(is_enemy_visible);
    LrgBTAction *action = lrg_bt_action_new_simple(move_action);

    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(sequence), LRG_BT_NODE(condition));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(sequence), LRG_BT_NODE(action));

    lrg_behavior_tree_set_root(tree, LRG_BT_NODE(sequence));

    /* Execute tree */
    lrg_blackboard_set_int(bb, "enemies_seen", 1);
    LrgBTStatus status = lrg_behavior_tree_tick(tree, 0.016f);

    g_print("Tree status: %d\n", status);

    g_object_unref(sequence);
    g_object_unref(condition);
    g_object_unref(action);
    g_object_unref(tree);

    return 0;
}
```

## Module Structure

```
ai/
├── lrg-blackboard.h/.c           # Shared data store
├── lrg-behavior-tree.h/.c        # Tree container
├── lrg-bt-node.h/.c              # Base node class
├── lrg-bt-composite.h/.c         # Parent nodes
├── lrg-bt-decorator.h/.c         # Wrapper nodes
└── lrg-bt-leaf.h/.c              # Terminal nodes
```

## Node Types

### Composite Nodes

Control the execution of multiple children:

- **Sequence**: Runs children in order until one fails
- **Selector**: Runs children in order until one succeeds
- **Parallel**: Runs all children simultaneously

### Leaf Nodes

Perform actual work:

- **Action**: Executes a function that performs game logic
- **Condition**: Tests a boolean condition
- **Wait**: Pauses for a specified duration

### Decorator Nodes

Modify child behavior:

- **Inverter**: Inverts SUCCESS to FAILURE and vice versa
- **Succeeder**: Always returns SUCCESS regardless of child result
- **Failer**: Always returns FAILURE regardless of child result
- **Repeater**: Repeats child N times

## Documentation

- [LrgBlackboard](blackboard.md) - Shared data storage
- [LrgBehaviorTree](behavior-tree.md) - Tree container and executor
- [LrgBTNode](bt-node.md) - Base node class
- [LrgBTComposite](bt-composite.md) - Composite nodes (Sequence, Selector, Parallel)
- [LrgBTDecorator](bt-decorator.md) - Decorator nodes (Inverter, Repeater, etc.)
- [LrgBTLeaf](bt-leaf.md) - Leaf nodes (Action, Condition, Wait)

## Examples

For complete working examples, see the [AI Examples](../examples/behavior-trees.md).

## Common Patterns

### Fallback Pattern (Selector)

Try multiple options in priority order:

```c
LrgBTSelector *selector = lrg_bt_selector_new();
lrg_bt_composite_add_child(selector, attack_action);      /* Try attack first */
lrg_bt_composite_add_child(selector, run_action);         /* Fallback to run */
```

### Guard Pattern (Sequence + Condition)

Only execute action if condition is true:

```c
LrgBTSequence *sequence = lrg_bt_sequence_new();
lrg_bt_composite_add_child(sequence, enemy_in_range_cond);
lrg_bt_composite_add_child(sequence, attack_action);
```

### Repeat Until Success (Repeater)

Keep trying an action until it succeeds:

```c
LrgBTRepeater *repeater = lrg_bt_repeater_new(action, 0);  /* 0 = infinite */
```

### Negate Condition (Inverter)

Flip a condition's result:

```c
LrgBTInverter *inverter = lrg_bt_inverter_new(enemy_visible_cond);
/* Now succeeds when enemy is NOT visible */
```
