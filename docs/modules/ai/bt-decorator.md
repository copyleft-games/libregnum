# LrgBTDecorator

A derivable base class for decorator nodes that wrap a single child and modify its behavior. Also includes concrete implementations of Inverter, Repeater, Succeeder, and Failer decorators.

## Type Information

- **Type Name**: `LrgBTDecorator`
- **Type ID**: `LRG_TYPE_BT_DECORATOR`
- **Category**: Derivable GObject (subclass of LrgBTNode)
- **Header**: `lrg-bt-decorator.h`

## Description

Decorator nodes are wrappers that take a single child and modify how it behaves. Common uses include:

- Inverting results (negate condition)
- Repeating execution
- Forcing success or failure
- Adding conditions or side effects

## Child Management

### lrg_bt_decorator_get_child()

```c
LrgBTNode *
lrg_bt_decorator_get_child (LrgBTDecorator *self)
```

Gets the child node.

**Returns:** (transfer none) (nullable) The child node or NULL

### lrg_bt_decorator_set_child()

```c
void
lrg_bt_decorator_set_child (LrgBTDecorator *self,
                            LrgBTNode      *child)
```

Sets the child node.

**Parameters:**
- `child`: (nullable) (transfer none) The child node (NULL to clear)

**Example:**
```c
LrgBTInverter *inverter = lrg_bt_inverter_new(NULL);
LrgBTCondition *condition = lrg_bt_condition_new_simple(my_condition);
lrg_bt_decorator_set_child(LRG_BT_DECORATOR(inverter), LRG_BT_NODE(condition));
```

## Inverter Decorator

Inverts the result of a child: SUCCESS becomes FAILURE and vice versa. RUNNING remains RUNNING.

### lrg_bt_inverter_new()

```c
LrgBTInverter *
lrg_bt_inverter_new (LrgBTNode *child)
```

Creates a new inverter decorator.

**Parameters:**
- `child`: (nullable) (transfer none) The child node

**Returns:** (transfer full) A new `LrgBTInverter`

### Behavior

```
Inverter: Flip success and failure
├── Child → SUCCESS → Result: FAILURE
├── Child → FAILURE → Result: SUCCESS
└── Child → RUNNING → Result: RUNNING
```

### Example

```c
/* Check if target is NOT visible */
LrgBTCondition *target_visible = lrg_bt_condition_new_simple(is_target_visible);
LrgBTInverter *target_not_visible = lrg_bt_inverter_new(LRG_BT_NODE(target_visible));

/* Now succeeds when target is NOT visible */
```

## Succeeder Decorator

Always returns SUCCESS regardless of child result. Used to allow fallthrough or ignore failures.

### lrg_bt_succeeder_new()

```c
LrgBTSucceeder *
lrg_bt_succeeder_new (LrgBTNode *child)
```

Creates a new succeeder decorator.

**Parameters:**
- `child`: (nullable) (transfer none) The child node

**Returns:** (transfer full) A new `LrgBTSucceeder`

### Behavior

```
Succeeder: Always return SUCCESS
├── Child → SUCCESS → Result: SUCCESS
├── Child → FAILURE → Result: SUCCESS
└── Child → RUNNING → Result: RUNNING
```

### Example

```c
/* Try to attack but continue regardless of result */
LrgBTAction *attack = lrg_bt_action_new_simple(attack_action);
LrgBTSucceeder *guaranteed_success = lrg_bt_succeeder_new(LRG_BT_NODE(attack));
```

## Failer Decorator

Always returns FAILURE regardless of child result. Used to force behavior tree into failure state.

### lrg_bt_failer_new()

```c
LrgBTFailer *
lrg_bt_failer_new (LrgBTNode *child)
```

Creates a new failer decorator.

**Parameters:**
- `child`: (nullable) (transfer none) The child node

**Returns:** (transfer full) A new `LrgBTFailer`

### Behavior

```
Failer: Always return FAILURE
├── Child → SUCCESS → Result: FAILURE
├── Child → FAILURE → Result: FAILURE
└── Child → RUNNING → Result: RUNNING
```

### Example

```c
/* Run animation but don't consider it a success */
LrgBTAction *animate = lrg_bt_action_new_simple(animation_action);
LrgBTFailer *animation_fail = lrg_bt_failer_new(LRG_BT_NODE(animate));
```

## Repeater Decorator

Repeats a child node a specified number of times.

### lrg_bt_repeater_new()

```c
LrgBTRepeater *
lrg_bt_repeater_new (LrgBTNode *child,
                     guint      count)
```

Creates a new repeater decorator.

**Parameters:**
- `child`: (nullable) (transfer none) The child node
- `count`: Number of times to repeat (0 = infinite)

**Returns:** (transfer full) A new `LrgBTRepeater`

### Behavior

```
Repeater(3): Run child 3 times
├── Tick 1: Child → Running → Result: RUNNING
├── Tick 2: Child → Success → Retry
├── Tick 3: Child → Success → Retry
├── Tick 4: Child → Success → Result: SUCCESS
└── Tick 5+: No execution until reset
```

### lrg_bt_repeater_get_count()

```c
guint
lrg_bt_repeater_get_count (LrgBTRepeater *self)
```

Gets the repeat count.

**Returns:** Repeat count (0 = infinite)

### lrg_bt_repeater_set_count()

```c
void
lrg_bt_repeater_set_count (LrgBTRepeater *self,
                           guint          count)
```

Sets the repeat count.

**Parameters:**
- `count`: Repeat count (0 = infinite)

### Example

```c
/* Attack 3 times then stop */
LrgBTAction *attack = lrg_bt_action_new_simple(attack_action);
LrgBTRepeater *triple_attack = lrg_bt_repeater_new(LRG_BT_NODE(attack), 3);

/* Attack infinitely */
LrgBTRepeater *infinite = lrg_bt_repeater_new(LRG_BT_NODE(attack), 0);
```

## Complete Example

```c
#include <glib.h>
#include <libregnum.h>

static gboolean
is_healthy(LrgBlackboard *bb, gpointer ud) {
    (void)ud;
    gint health = lrg_blackboard_get_int(bb, "health", 100);
    return health > 50;
}

static LrgBTStatus
heal_action(LrgBlackboard *bb, gfloat dt, gpointer ud) {
    (void)dt;(void)ud;
    gint health = lrg_blackboard_get_int(bb, "health", 100);
    health += 10;
    if (health > 100) health = 100;
    lrg_blackboard_set_int(bb, "health", health);
    g_print("Healed to %d\n", health);
    return LRG_BT_STATUS_SUCCESS;
}

int main(void) {
    g_autoptr(LrgBlackboard) bb = lrg_blackboard_new();
    lrg_blackboard_set_int(bb, "health", 20);

    /* Build tree:
     * If NOT healthy, heal 3 times
     * Sequence
     *   ├── Inverter(is_healthy) - succeeds if health <= 50
     *   └── Repeater(heal, 3) - heal up to 3 times
     */
    g_autoptr(LrgBTCondition) healthy = lrg_bt_condition_new_simple(is_healthy);
    g_autoptr(LrgBTInverter) not_healthy = lrg_bt_inverter_new(LRG_BT_NODE(healthy));

    g_autoptr(LrgBTAction) heal = lrg_bt_action_new_simple(heal_action);
    g_autoptr(LrgBTRepeater) heal_3x = lrg_bt_repeater_new(LRG_BT_NODE(heal), 3);

    g_autoptr(LrgBTSequence) heal_seq = lrg_bt_sequence_new();
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(heal_seq), LRG_BT_NODE(not_healthy));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(heal_seq), LRG_BT_NODE(heal_3x));

    /* Execute sequence multiple times */
    g_print("Initial health: %d\n", lrg_blackboard_get_int(bb, "health", 0));

    for (gint i = 0; i < 5; i++) {
        g_print("\n=== Tick %d ===\n", i + 1);
        LrgBTStatus status = lrg_bt_node_tick(LRG_BT_NODE(heal_seq), bb, 0.016f);
        g_print("Status: %d, Health: %d\n", status,
                lrg_blackboard_get_int(bb, "health", 0));

        /* Reset for next tick */
        if (status != LRG_BT_STATUS_RUNNING) {
            lrg_bt_node_reset(LRG_BT_NODE(heal_seq));
        }
    }

    return 0;
}
```

## Decorator Patterns

### Negate Condition

Use inverter to test the opposite:

```c
LrgBTCondition *target_visible = /* ... */;
LrgBTInverter *target_hidden = lrg_bt_inverter_new(LRG_BT_NODE(target_visible));
```

### Force Success

Allow failure without affecting parent:

```c
LrgBTAction *risky_action = /* ... */;
LrgBTSucceeder *safe_risky = lrg_bt_succeeder_new(LRG_BT_NODE(risky_action));
```

### Repeat Action

Execute action multiple times:

```c
LrgBTAction *attack = /* ... */;
LrgBTRepeater *multi_attack = lrg_bt_repeater_new(LRG_BT_NODE(attack), 5);
```

### Conditional Repetition

Repeat until condition is met:

```c
/* Keep attacking while target is visible */
LrgBTSelector *attack_until_gone = lrg_bt_selector_new();
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(attack_until_gone),
                           repeater_attack);
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(attack_until_gone),
                           idle_action);
```

## Stacking Decorators

You can nest decorators:

```c
/* Attack 3 times, but always consider it successful */
LrgBTAction *attack = /* ... */;
LrgBTRepeater *repeat = lrg_bt_repeater_new(LRG_BT_NODE(attack), 3);
LrgBTSucceeder *guaranteed = lrg_bt_succeeder_new(LRG_BT_NODE(repeat));
```

## Status Propagation

Most decorators propagate RUNNING status unchanged:

```
Inverter(Running) → Running
Repeater(Running) → Running
Succeeder(Running) → Running
Failer(Running) → Running
```

This is important for multi-frame operations like animations or waits.

## Related Types

- [LrgBTNode](bt-node.md) - Base class
- [LrgBTComposite](bt-composite.md) - Composite nodes
- [LrgBTLeaf](bt-leaf.md) - Leaf nodes
