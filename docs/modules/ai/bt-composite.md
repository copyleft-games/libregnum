# LrgBTComposite

A derivable base class for composite nodes that contain and control multiple child nodes. Also includes concrete implementations of Sequence, Selector, and Parallel nodes.

## Type Information

- **Type Name**: `LrgBTComposite`
- **Type ID**: `LRG_TYPE_BT_COMPOSITE`
- **Category**: Derivable GObject (subclass of LrgBTNode)
- **Header**: `lrg-bt-composite.h`

## Description

Composite nodes are parent nodes that execute and control multiple children. They are the logic flow nodes of the behavior tree, determining when and how children execute.

## Child Management

### lrg_bt_composite_add_child()

```c
void
lrg_bt_composite_add_child (LrgBTComposite *self,
                            LrgBTNode      *child)
```

Adds a child node to the composite.

**Parameters:**
- `child`: (transfer none) The child node to add

**Example:**
```c
LrgBTSequence *sequence = lrg_bt_sequence_new();
LrgBTAction *action1 = lrg_bt_action_new_simple(func1);
LrgBTAction *action2 = lrg_bt_action_new_simple(func2);

lrg_bt_composite_add_child(LRG_BT_COMPOSITE(sequence), LRG_BT_NODE(action1));
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(sequence), LRG_BT_NODE(action2));
```

### lrg_bt_composite_remove_child()

```c
gboolean
lrg_bt_composite_remove_child (LrgBTComposite *self,
                               LrgBTNode      *child)
```

Removes a child node.

**Returns:** `TRUE` if child was removed

**Example:**
```c
gboolean removed = lrg_bt_composite_remove_child(
    LRG_BT_COMPOSITE(sequence), LRG_BT_NODE(action));
```

### lrg_bt_composite_get_children()

```c
GPtrArray *
lrg_bt_composite_get_children (LrgBTComposite *self)
```

Gets the array of child nodes.

**Returns:** (transfer none) (element-type LrgBTNode) Array of children

**Example:**
```c
GPtrArray *children = lrg_bt_composite_get_children(LRG_BT_COMPOSITE(sequence));
for (guint i = 0; i < children->len; i++) {
    LrgBTNode *child = g_ptr_array_index(children, i);
    const gchar *name = lrg_bt_node_get_name(child);
    g_print("  Child %u: %s\n", i, name ? name : "unnamed");
}
```

### lrg_bt_composite_get_child_count()

```c
guint
lrg_bt_composite_get_child_count (LrgBTComposite *self)
```

Gets the number of children.

**Returns:** Number of child nodes

**Example:**
```c
if (lrg_bt_composite_get_child_count(LRG_BT_COMPOSITE(sequence)) == 0) {
    g_warning("Sequence has no children!");
}
```

### lrg_bt_composite_clear_children()

```c
void
lrg_bt_composite_clear_children (LrgBTComposite *self)
```

Removes all child nodes.

**Example:**
```c
lrg_bt_composite_clear_children(LRG_BT_COMPOSITE(sequence));
```

## Sequence Node

A sequence runs children in order until one fails. Returns SUCCESS only if all children succeed.

### lrg_bt_sequence_new()

```c
LrgBTSequence *
lrg_bt_sequence_new (void)
```

Creates a new sequence node.

**Returns:** (transfer full) A new `LrgBTSequence`

### Execution Logic

```
Sequence: Run children in order until one fails
├── Child 1 → Success ✓
├── Child 2 → Success ✓
├── Child 3 → FAILURE ✗ (stop here, return FAILURE)
└── Child 4 → (not executed)
```

### Example

```c
/* "If can move AND can attack, then move and attack" */
g_autoptr(LrgBTSequence) seq = lrg_bt_sequence_new();
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(seq),
                           LRG_BT_NODE(can_move_cond));
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(seq),
                           LRG_BT_NODE(can_attack_cond));
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(seq),
                           LRG_BT_NODE(attack_action));
```

## Selector Node

A selector runs children in order until one succeeds. Returns FAILURE only if all children fail.

### lrg_bt_selector_new()

```c
LrgBTSelector *
lrg_bt_selector_new (void)
```

Creates a new selector node.

**Returns:** (transfer full) A new `LrgBTSelector`

### Execution Logic

```
Selector: Run children in order until one succeeds
├── Child 1 → FAILURE ✗
├── Child 2 → SUCCESS ✓ (stop here, return SUCCESS)
├── Child 3 → (not executed)
└── Child 4 → (not executed)
```

### Example

```c
/* "Attack if can, otherwise flee, otherwise stand idle" */
g_autoptr(LrgBTSelector) sel = lrg_bt_selector_new();
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(sel),
                           LRG_BT_NODE(attack_action));
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(sel),
                           LRG_BT_NODE(flee_action));
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(sel),
                           LRG_BT_NODE(idle_action));
```

## Parallel Node

A parallel node runs all children simultaneously and returns based on a policy.

### lrg_bt_parallel_new()

```c
LrgBTParallel *
lrg_bt_parallel_new (LrgBTParallelPolicy policy)
```

Creates a new parallel node.

**Parameters:**
- `policy`: Success policy
  - `LRG_BT_PARALLEL_REQUIRE_ONE`: SUCCESS if at least one child succeeds
  - `LRG_BT_PARALLEL_REQUIRE_ALL`: SUCCESS only if all children succeed

**Returns:** (transfer full) A new `LrgBTParallel`

### Execution Logic

```
Parallel (REQUIRE_ONE): Run all, succeed if at least one succeeds
├── Child 1 → Success ✓ (return SUCCESS immediately)
├── Child 2 → Running
└── Child 3 → Failure

Parallel (REQUIRE_ALL): Run all, succeed only if all succeed
├── Child 1 → Success ✓
├── Child 2 → Running (return RUNNING)
└── Child 3 → Failure (return FAILURE)
```

### lrg_bt_parallel_get_policy()

```c
LrgBTParallelPolicy
lrg_bt_parallel_get_policy (LrgBTParallel *self)
```

Gets the success policy.

### lrg_bt_parallel_set_policy()

```c
void
lrg_bt_parallel_set_policy (LrgBTParallel       *self,
                            LrgBTParallelPolicy  policy)
```

Sets the success policy.

### Example

```c
/* Run attack and animation simultaneously */
g_autoptr(LrgBTParallel) par = lrg_bt_parallel_new(LRG_BT_PARALLEL_REQUIRE_ALL);
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(par), LRG_BT_NODE(attack_action));
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(par), LRG_BT_NODE(animate_action));
```

## Complete Example

```c
#include <glib.h>
#include <libregnum.h>

static LrgBTStatus
action1(LrgBlackboard *bb, gfloat dt, gpointer ud) {
    (void)bb;(void)dt;(void)ud;
    g_print("Action 1\n");
    return LRG_BT_STATUS_SUCCESS;
}

static LrgBTStatus
action2(LrgBlackboard *bb, gfloat dt, gpointer ud) {
    (void)bb;(void)dt;(void)ud;
    g_print("Action 2\n");
    return LRG_BT_STATUS_SUCCESS;
}

static LrgBTStatus
action3(LrgBlackboard *bb, gfloat dt, gpointer ud) {
    (void)bb;(void)dt;(void)ud;
    g_print("Action 3 (fallback)\n");
    return LRG_BT_STATUS_SUCCESS;
}

int main(void) {
    g_autoptr(LrgBlackboard) bb = lrg_blackboard_new();

    /* Build complex tree:
     * Selector (choose one branch)
     *   ├── Sequence (try attack)
     *   │     ├── Action 1
     *   │     └── Action 2
     *   └── Action 3 (fallback)
     */
    g_autoptr(LrgBTSelector) root = lrg_bt_selector_new();
    g_autoptr(LrgBTSequence) attack_seq = lrg_bt_sequence_new();
    g_autoptr(LrgBTAction) a1 = lrg_bt_action_new_simple(action1);
    g_autoptr(LrgBTAction) a2 = lrg_bt_action_new_simple(action2);
    g_autoptr(LrgBTAction) a3 = lrg_bt_action_new_simple(action3);

    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(attack_seq), LRG_BT_NODE(a1));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(attack_seq), LRG_BT_NODE(a2));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(root), LRG_BT_NODE(attack_seq));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(root), LRG_BT_NODE(a3));

    /* Print tree structure */
    g_print("Tree has %u top-level children\n",
            lrg_bt_composite_get_child_count(LRG_BT_COMPOSITE(root)));

    /* Execute */
    g_print("\n=== Executing ===\n");
    LrgBTStatus status = lrg_bt_node_tick(LRG_BT_NODE(root), bb, 0.016f);
    g_print("Result: %d\n", status);

    return 0;
}
```

## Tree Construction Patterns

### Guard Pattern

Protect an action with a condition:

```c
LrgBTSequence *guard = lrg_bt_sequence_new();
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(guard), condition);
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(guard), action);
```

### Fallback Pattern

Try options in priority order:

```c
LrgBTSelector *fallback = lrg_bt_selector_new();
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(fallback), preferred_action);
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(fallback), backup_action);
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(fallback), default_action);
```

### Parallel Pattern

Execute multiple actions simultaneously:

```c
LrgBTParallel *multi = lrg_bt_parallel_new(LRG_BT_PARALLEL_REQUIRE_ALL);
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(multi), action1);
lrg_bt_composite_add_child(LRG_BT_COMPOSITE(multi), action2);
```

## Related Types

- [LrgBTNode](bt-node.md) - Base class
- [LrgBTDecorator](bt-decorator.md) - Decorator nodes
- [LrgBTLeaf](bt-leaf.md) - Leaf nodes
