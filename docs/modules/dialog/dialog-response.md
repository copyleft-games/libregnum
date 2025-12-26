# LrgDialogResponse - Dialog Response Option

A player choice option within a dialog node. Each response points to a next node and can have conditions that determine visibility and effects that execute when selected.

## Type

- **Boxed Type** - Reference-counted value type
- **Type ID** - `LRG_TYPE_DIALOG_RESPONSE`
- **GIR Name** - `Libregnum.DialogResponse`

## Construction

### lrg_dialog_response_new()
```c
LrgDialogResponse * lrg_dialog_response_new (const gchar *id,
                                             const gchar *text,
                                             const gchar *next_node_id);
```

Creates a new dialog response.

**Parameters:**
- `id` - Unique identifier for the response
- `text` - Display text shown to player
- `next_node_id` - (nullable) ID of the node to transition to

**Returns:** A new `LrgDialogResponse` (transfer full)

### lrg_dialog_response_copy()
```c
LrgDialogResponse * lrg_dialog_response_copy (const LrgDialogResponse *self);
```

Creates a deep copy of the response.

**Parameters:**
- `self` - An `LrgDialogResponse`

**Returns:** A copy of @self (transfer full)

### lrg_dialog_response_free()
```c
void lrg_dialog_response_free (LrgDialogResponse *self);
```

Frees the response and all associated data.

**Parameters:**
- `self` - An `LrgDialogResponse`

## Identification

### lrg_dialog_response_get_id()
```c
const gchar * lrg_dialog_response_get_id (const LrgDialogResponse *self);
```

Gets the response identifier.

**Parameters:**
- `self` - An `LrgDialogResponse`

**Returns:** The response ID (transfer none)

## Text

### lrg_dialog_response_get_text()
```c
const gchar * lrg_dialog_response_get_text (const LrgDialogResponse *self);
```

Gets the display text.

**Parameters:**
- `self` - An `LrgDialogResponse`

**Returns:** The response text (transfer none)

### lrg_dialog_response_set_text()
```c
void lrg_dialog_response_set_text (LrgDialogResponse *self,
                                   const gchar       *text);
```

Sets the display text.

**Parameters:**
- `self` - An `LrgDialogResponse`
- `text` - New display text

## Navigation

### lrg_dialog_response_get_next_node_id()
```c
const gchar * lrg_dialog_response_get_next_node_id (const LrgDialogResponse *self);
```

Gets the next node ID to transition to.

**Parameters:**
- `self` - An `LrgDialogResponse`

**Returns:** The next node ID (transfer none, nullable)

### lrg_dialog_response_set_next_node_id()
```c
void lrg_dialog_response_set_next_node_id (LrgDialogResponse *self,
                                           const gchar       *next_node_id);
```

Sets the next node ID.

**Parameters:**
- `self` - An `LrgDialogResponse`
- `next_node_id` - Next node ID (nullable)

## Conditions

### lrg_dialog_response_add_condition()
```c
void lrg_dialog_response_add_condition (LrgDialogResponse *self,
                                        const gchar       *condition);
```

Adds a condition that must be true for this response to appear.

**Parameters:**
- `self` - An `LrgDialogResponse`
- `condition` - Condition expression string (e.g., "has_sword", "level >= 10")

### lrg_dialog_response_get_conditions()
```c
GPtrArray * lrg_dialog_response_get_conditions (const LrgDialogResponse *self);
```

Gets all conditions for this response.

**Parameters:**
- `self` - An `LrgDialogResponse`

**Returns:** Array of condition strings (element-type utf8, transfer none)

## Effects

### lrg_dialog_response_add_effect()
```c
void lrg_dialog_response_add_effect (LrgDialogResponse *self,
                                     const gchar       *effect);
```

Adds an effect that triggers when this response is selected.

**Parameters:**
- `self` - An `LrgDialogResponse`
- `effect` - Effect expression string (e.g., "accept_quest", "add_gold:100")

### lrg_dialog_response_get_effects()
```c
GPtrArray * lrg_dialog_response_get_effects (const LrgDialogResponse *self);
```

Gets all effects for this response.

**Parameters:**
- `self` - An `LrgDialogResponse`

**Returns:** Array of effect strings (element-type utf8, transfer none)

## Memory Management

Responses use automatic pointer cleanup:

```c
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgDialogResponse, lrg_dialog_response_free)
```

## Example

```c
/* Create a response option */
g_autoptr(LrgDialogResponse) accept = lrg_dialog_response_new(
    "accept_quest",
    "I'll help you with the dragon",
    "quest_details"
);

/* Add condition - only show if player level >= 10 */
lrg_dialog_response_add_condition(accept, "level >= 10");

/* Add effect - accept the quest */
lrg_dialog_response_add_effect(accept, "start_quest:dragon");

/* Another response - only for lower levels */
g_autoptr(LrgDialogResponse) too_weak = lrg_dialog_response_new(
    "too_weak",
    "I'm not strong enough yet",
    "comeback_later"
);
lrg_dialog_response_add_condition(too_weak, "level < 10");
lrg_dialog_response_add_effect(too_weak, "increment_counter:rejection_count");

/* Get response properties */
g_print("Response: %s\n", lrg_dialog_response_get_text(accept));
g_print("Goes to: %s\n", lrg_dialog_response_get_next_node_id(accept));

/* Check conditions */
GPtrArray *conditions = lrg_dialog_response_get_conditions(accept);
for (guint i = 0; i < conditions->len; i++) {
    const gchar *cond = g_ptr_array_index(conditions, i);
    g_print("Condition: %s\n", cond);
}

/* Copy a response */
g_autoptr(LrgDialogResponse) copy = lrg_dialog_response_copy(accept);
g_print("Copy goes to: %s\n", lrg_dialog_response_get_next_node_id(copy));
```
