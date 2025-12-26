# LrgBTLeaf

Final GObject implementations of terminal behavior tree nodes: Action, Condition, and Wait. These are the leaf nodes that perform actual work.

## Type Information

- **Category**: Final GObject (subclass of LrgBTNode)
- **Header**: `lrg-bt-leaf.h`

## Description

Leaf nodes are terminal nodes that perform the actual work in a behavior tree. They don't have children and return concrete results.

## Action Node

Action nodes execute functions that perform game logic.

### Type Information

- **Type Name**: `LrgBTAction`
- **Type ID**: `LRG_TYPE_BT_ACTION`

### Action Function Signature

```c
typedef LrgBTStatus (*LrgBTActionFunc) (LrgBlackboard *blackboard,
                                        gfloat         delta_time,
                                        gpointer       user_data)
```

**Parameters:**
- `blackboard`: Shared data store (can be NULL if not needed)
- `delta_time`: Time elapsed since last tick
- `user_data`: Custom data passed to action

**Returns:** Status of the action

### Creating Actions

#### lrg_bt_action_new()

```c
LrgBTAction *
lrg_bt_action_new (LrgBTActionFunc  func,
                   gpointer         user_data,
                   GDestroyNotify   destroy)
```

Creates an action with user data.

**Parameters:**
- `func`: Action function
- `user_data`: Custom data for function
- `destroy`: Cleanup function for user_data

### lrg_bt_action_new_simple()

```c
LrgBTAction *
lrg_bt_action_new_simple (LrgBTActionFunc func)
```

Creates an action without user data.

**Example:**
```c
static LrgBTStatus
move_to_player(LrgBlackboard *bb, gfloat dt, gpointer ud)
{
    (void)ud;

    gint player_x = lrg_blackboard_get_int(bb, "player_x", 0);
    gint my_x = lrg_blackboard_get_int(bb, "my_x", 0);

    if (my_x < player_x) my_x++;
    else if (my_x > player_x) my_x--;

    lrg_blackboard_set_int(bb, "my_x", my_x);

    if (my_x == player_x) return LRG_BT_STATUS_SUCCESS;
    return LRG_BT_STATUS_RUNNING;
}

LrgBTAction *move = lrg_bt_action_new_simple(move_to_player);
```

### Action with User Data

```c
typedef struct {
    gint max_iterations;
    gint current_iteration;
} ActionData;

static LrgBTStatus
move_limited(LrgBlackboard *bb, gfloat dt, gpointer ud)
{
    ActionData *data = (ActionData *)ud;

    if (data->current_iteration >= data->max_iterations) {
        return LRG_BT_STATUS_SUCCESS;
    }

    /* Do work */
    data->current_iteration++;

    return LRG_BT_STATUS_RUNNING;
}

ActionData *data = g_new(ActionData, 1);
data->max_iterations = 10;
data->current_iteration = 0;

LrgBTAction *move = lrg_bt_action_new(move_limited, data, g_free);
```

## Condition Node

Condition nodes test boolean conditions and return SUCCESS or FAILURE.

### Type Information

- **Type Name**: `LrgBTCondition`
- **Type ID**: `LRG_TYPE_BT_CONDITION`

### Condition Function Signature

```c
typedef gboolean (*LrgBTConditionFunc) (LrgBlackboard *blackboard,
                                        gpointer       user_data)
```

**Parameters:**
- `blackboard`: Shared data store
- `user_data`: Custom data passed to condition

**Returns:** `TRUE` for success, `FALSE` for failure

### Creating Conditions

#### lrg_bt_condition_new()

```c
LrgBTCondition *
lrg_bt_condition_new (LrgBTConditionFunc  func,
                      gpointer            user_data,
                      GDestroyNotify      destroy)
```

Creates a condition with user data.

### lrg_bt_condition_new_simple()

```c
LrgBTCondition *
lrg_bt_condition_new_simple (LrgBTConditionFunc func)
```

Creates a condition without user data.

### Examples

**Simple Condition:**
```c
static gboolean
is_player_visible(LrgBlackboard *bb, gpointer ud)
{
    (void)ud;
    return lrg_blackboard_get_bool(bb, "player_visible", FALSE);
}

LrgBTCondition *condition = lrg_bt_condition_new_simple(is_player_visible);
```

**Condition with Threshold:**
```c
static gboolean
health_below_threshold(LrgBlackboard *bb, gpointer ud)
{
    gint threshold = GPOINTER_TO_INT(ud);
    gint health = lrg_blackboard_get_int(bb, "health", 100);
    return health < threshold;
}

LrgBTCondition *low_health = lrg_bt_condition_new(
    health_below_threshold,
    GINT_TO_POINTER(30),  /* 30% health threshold */
    NULL);
```

**Timestamp Condition:**
```c
static gboolean
cooldown_ready(LrgBlackboard *bb, gpointer ud)
{
    gint64 last_time = lrg_blackboard_get_int(bb, "last_attack_time", 0);
    gint64 now = g_get_monotonic_time() / 1000000;  /* seconds */
    return (now - last_time) >= 2;  /* 2 second cooldown */
}

LrgBTCondition *ready = lrg_bt_condition_new_simple(cooldown_ready);
```

## Wait Node

Wait nodes pause for a specified duration.

### Type Information

- **Type Name**: `LrgBTWait`
- **Type ID**: `LRG_TYPE_BT_WAIT`

### Creating Wait Nodes

#### lrg_bt_wait_new()

```c
LrgBTWait *
lrg_bt_wait_new (gfloat duration)
```

Creates a wait node for the specified duration.

**Parameters:**
- `duration`: Duration to wait in seconds

**Returns:** (transfer full) A new `LrgBTWait`

### Duration Management

#### lrg_bt_wait_get_duration()

```c
gfloat
lrg_bt_wait_get_duration (LrgBTWait *self)
```

Gets the wait duration. **Returns:** Duration in seconds

#### lrg_bt_wait_set_duration()

```c
void
lrg_bt_wait_set_duration (LrgBTWait *self,
                          gfloat     duration)
```

Sets the wait duration.

### Behavior

```
Wait(2.0): Wait for 2 seconds
├── Tick 1 (0.5s): Elapsed 0.5s < 2.0s → RUNNING
├── Tick 2 (0.5s): Elapsed 1.0s < 2.0s → RUNNING
├── Tick 3 (0.8s): Elapsed 1.8s < 2.0s → RUNNING
├── Tick 4 (0.3s): Elapsed 2.1s >= 2.0s → SUCCESS
└── Reset needed before reuse
```

### Example

```c
/* Wait 3 seconds then continue */
LrgBTWait *wait = lrg_bt_wait_new(3.0f);

/* In a sequence */
g_autoptr(LrgBTSequence) seq = lrg_bt_sequence_new();
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(seq), LRG_BT_NODE(wait));
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(seq), LRG_BT_NODE(next_action));
```

## Complete Example

```c
#include <glib.h>
#include <libregnum.h>

/* Actions */
static LrgBTStatus
look_around(LrgBlackboard *bb, gfloat dt, gpointer ud)
{
    (void)bb;(void)dt;(void)ud;
    g_print("Looking around...\n");
    return LRG_BT_STATUS_SUCCESS;
}

static LrgBTStatus
attack_player(LrgBlackboard *bb, gfloat dt, gpointer ud)
{
    (void)dt;(void)ud;
    g_print("Attacking!\n");
    gint attacks = lrg_blackboard_get_int(bb, "attacks", 0);
    lrg_blackboard_set_int(bb, "attacks", attacks + 1);
    return LRG_BT_STATUS_SUCCESS;
}

/* Conditions */
static gboolean
is_player_near(LrgBlackboard *bb, gpointer ud)
{
    (void)ud;
    return lrg_blackboard_get_bool(bb, "player_near", FALSE);
}

static gboolean
low_health(LrgBlackboard *bb, gpointer ud)
{
    (void)ud;
    gint health = lrg_blackboard_get_int(bb, "health", 100);
    return health < 30;
}

int main(void) {
    g_autoptr(LrgBlackboard) bb = lrg_blackboard_new();

    /* Initialize state */
    lrg_blackboard_set_int(bb, "health", 50);
    lrg_blackboard_set_bool(bb, "player_near", TRUE);

    /* Build tree:
     * Selector
     *   ├── Sequence (flee if low health)
     *   │     ├── Condition: low_health
     *   │     └── Action: look_around (as placeholder flee)
     *   └── Sequence (attack if near)
     *         ├── Condition: is_player_near
     *         ├── Action: attack_player
     *         └── Wait: cooldown
     */
    g_autoptr(LrgBTSelector) root = lrg_bt_selector_new();

    /* Flee branch */
    g_autoptr(LrgBTSequence) flee = lrg_bt_sequence_new();
    g_autoptr(LrgBTCondition) health_check = lrg_bt_condition_new_simple(low_health);
    g_autoptr(LrgBTAction) flee_action = lrg_bt_action_new_simple(look_around);
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(flee), LRG_BT_NODE(health_check));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(flee), LRG_BT_NODE(flee_action));

    /* Attack branch */
    g_autoptr(LrgBTSequence) attack = lrg_bt_sequence_new();
    g_autoptr(LrgBTCondition) near_check = lrg_bt_condition_new_simple(is_player_near);
    g_autoptr(LrgBTAction) attack_action = lrg_bt_action_new_simple(attack_player);
    g_autoptr(LrgBTWait) cooldown = lrg_bt_wait_new(2.0f);
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(attack), LRG_BT_NODE(near_check));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(attack), LRG_BT_NODE(attack_action));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(attack), LRG_BT_NODE(cooldown));

    /* Assemble root */
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(root), LRG_BT_NODE(flee));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(root), LRG_BT_NODE(attack));

    /* Execute */
    g_print("=== Tick 1: Attack (health=50, player_near=true) ===\n");
    LrgBTStatus status = lrg_bt_node_tick(LRG_BT_NODE(root), bb, 0.016f);
    g_print("Status: %d\n", status);

    g_print("\n=== Tick 2: Continue cooldown ===\n");
    status = lrg_bt_node_tick(LRG_BT_NODE(root), bb, 0.016f);
    g_print("Status: %d\n", status);

    g_print("\n=== Lower health and tick ===\n");
    lrg_blackboard_set_int(bb, "health", 20);
    lrg_bt_node_reset(LRG_BT_NODE(root));
    status = lrg_bt_node_tick(LRG_BT_NODE(root), bb, 0.016f);
    g_print("Status: %d (should flee)\n", status);

    g_print("\nTotal attacks: %d\n", lrg_blackboard_get_int(bb, "attacks", 0));

    return 0;
}
```

## Common Patterns

### Repeating Action Until Success

```c
LrgBTAction *try = lrg_bt_action_new_simple(func);
LrgBTRepeater *retry = lrg_bt_repeater_new(LRG_BT_NODE(try), 0);  /* infinite */
```

### Guard Action with Condition

```c
LrgBTSequence *guarded = lrg_bt_sequence_new();
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(guarded), LRG_BT_NODE(condition));
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(guarded), LRG_BT_NODE(action));
```

### Sequential Actions with Delays

```c
LrgBTSequence *sequence = lrg_bt_sequence_new();
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(sequence), LRG_BT_NODE(action1));
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(sequence), lrg_bt_wait_new(1.0f));
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(sequence), LRG_BT_NODE(action2));
```

## Related Types

- [LrgBTNode](bt-node.md) - Base class
- [LrgBTComposite](bt-composite.md) - Parent nodes
- [LrgBTDecorator](bt-decorator.md) - Wrapper nodes
- [LrgBlackboard](blackboard.md) - State storage
