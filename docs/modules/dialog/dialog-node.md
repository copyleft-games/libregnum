# LrgDialogNode - Dialog Tree Node

A single point in a conversation with speaker information, text content, response options, and conditions/effects. Nodes can be linked together to form branching conversations.

## Type

- **Derivable GObject Class** - Can be subclassed for custom behaviors
- **Type ID** - `LRG_TYPE_DIALOG_NODE`
- **GIR Name** - `Libregnum.DialogNode`

## Virtual Methods

### get_display_text()
```c
const gchar * (*get_display_text) (LrgDialogNode *self);
```

Gets the display text, potentially localized. Subclasses can override to provide localization.

### evaluate_conditions()
```c
gboolean (*evaluate_conditions) (LrgDialogNode *self,
                                 GHashTable    *context);
```

Checks if node conditions are met. Can be overridden for custom condition logic.

### apply_effects()
```c
void (*apply_effects) (LrgDialogNode *self,
                       GHashTable    *context);
```

Applies node effects to context. Can be overridden for custom effect handling.

## Construction

### lrg_dialog_node_new()
```c
LrgDialogNode * lrg_dialog_node_new (const gchar *id);
```

Creates a new dialog node.

**Parameters:**
- `id` - Unique identifier for the node

**Returns:** A new `LrgDialogNode` (transfer full)

## Identification

### lrg_dialog_node_get_id()
```c
const gchar * lrg_dialog_node_get_id (LrgDialogNode *self);
```

Gets the node identifier.

**Parameters:**
- `self` - An `LrgDialogNode`

**Returns:** The node ID (transfer none)

## Speaker

### lrg_dialog_node_get_speaker()
```c
const gchar * lrg_dialog_node_get_speaker (LrgDialogNode *self);
```

Gets the speaker for this node (NPC name, etc).

**Parameters:**
- `self` - An `LrgDialogNode`

**Returns:** The speaker name (transfer none, nullable)

### lrg_dialog_node_set_speaker()
```c
void lrg_dialog_node_set_speaker (LrgDialogNode *self,
                                  const gchar   *speaker);
```

Sets the speaker for this node.

**Parameters:**
- `self` - An `LrgDialogNode`
- `speaker` - Speaker name (nullable)

## Text Content

### lrg_dialog_node_get_text()
```c
const gchar * lrg_dialog_node_get_text (LrgDialogNode *self);
```

Gets the raw text content.

**Parameters:**
- `self` - An `LrgDialogNode`

**Returns:** The node text (transfer none, nullable)

### lrg_dialog_node_set_text()
```c
void lrg_dialog_node_set_text (LrgDialogNode *self,
                               const gchar   *text);
```

Sets the text content.

**Parameters:**
- `self` - An `LrgDialogNode`
- `text` - Text content (nullable)

### lrg_dialog_node_get_display_text()
```c
const gchar * lrg_dialog_node_get_display_text (LrgDialogNode *self);
```

Gets the display text, potentially localized. Subclasses can override this to provide localization.

**Parameters:**
- `self` - An `LrgDialogNode`

**Returns:** The display text (transfer none, nullable)

## Navigation

### lrg_dialog_node_get_next_node_id()
```c
const gchar * lrg_dialog_node_get_next_node_id (LrgDialogNode *self);
```

Gets the default next node ID for auto-advance.

**Parameters:**
- `self` - An `LrgDialogNode`

**Returns:** The next node ID (transfer none, nullable)

### lrg_dialog_node_set_next_node_id()
```c
void lrg_dialog_node_set_next_node_id (LrgDialogNode *self,
                                       const gchar   *next_node_id);
```

Sets the default next node ID.

**Parameters:**
- `self` - An `LrgDialogNode`
- `next_node_id` - Next node ID (nullable)

## Responses

### lrg_dialog_node_add_response()
```c
void lrg_dialog_node_add_response (LrgDialogNode     *self,
                                   LrgDialogResponse *response);
```

Adds a response option to this node.

**Parameters:**
- `self` - An `LrgDialogNode`
- `response` - Response to add (transfer full)

### lrg_dialog_node_get_responses()
```c
GPtrArray * lrg_dialog_node_get_responses (LrgDialogNode *self);
```

Gets all responses for this node.

**Parameters:**
- `self` - An `LrgDialogNode`

**Returns:** Array of `LrgDialogResponse` (element-type, transfer none)

### lrg_dialog_node_get_response_count()
```c
guint lrg_dialog_node_get_response_count (LrgDialogNode *self);
```

Gets the number of responses.

**Parameters:**
- `self` - An `LrgDialogNode`

**Returns:** Response count

### lrg_dialog_node_get_response()
```c
LrgDialogResponse * lrg_dialog_node_get_response (LrgDialogNode *self,
                                                  guint          index);
```

Gets a response by index.

**Parameters:**
- `self` - An `LrgDialogNode`
- `index` - Response index

**Returns:** The response (transfer none, nullable) - `NULL` if out of bounds

## Conditions

### lrg_dialog_node_add_condition()
```c
void lrg_dialog_node_add_condition (LrgDialogNode *self,
                                    const gchar   *condition);
```

Adds a condition that must be met to show this node.

**Parameters:**
- `self` - An `LrgDialogNode`
- `condition` - Condition expression (e.g., "has_sword", "level >= 10")

### lrg_dialog_node_get_conditions()
```c
GPtrArray * lrg_dialog_node_get_conditions (LrgDialogNode *self);
```

Gets all conditions.

**Parameters:**
- `self` - An `LrgDialogNode`

**Returns:** Array of condition strings (element-type utf8, transfer none)

### lrg_dialog_node_evaluate_conditions()
```c
gboolean lrg_dialog_node_evaluate_conditions (LrgDialogNode *self,
                                              GHashTable    *context);
```

Evaluates whether all conditions are met.

**Parameters:**
- `self` - An `LrgDialogNode`
- `context` - Variable context for evaluation (nullable)

**Returns:** `TRUE` if all conditions pass

## Effects

### lrg_dialog_node_add_effect()
```c
void lrg_dialog_node_add_effect (LrgDialogNode *self,
                                 const gchar   *effect);
```

Adds an effect to trigger when entering this node.

**Parameters:**
- `self` - An `LrgDialogNode`
- `effect` - Effect expression (e.g., "give_item:sword", "set_flag:defeated_boss")

### lrg_dialog_node_get_effects()
```c
GPtrArray * lrg_dialog_node_get_effects (LrgDialogNode *self);
```

Gets all effects.

**Parameters:**
- `self` - An `LrgDialogNode`

**Returns:** Array of effect strings (element-type utf8, transfer none)

### lrg_dialog_node_apply_effects()
```c
void lrg_dialog_node_apply_effects (LrgDialogNode *self,
                                    GHashTable    *context);
```

Applies all effects to the context.

**Parameters:**
- `self` - An `LrgDialogNode`
- `context` - Variable context for effects (nullable)

## Terminal Nodes

### lrg_dialog_node_is_terminal()
```c
gboolean lrg_dialog_node_is_terminal (LrgDialogNode *self);
```

Checks if this node ends the dialog. A node is terminal if it has no next_node_id and no responses.

**Parameters:**
- `self` - An `LrgDialogNode`

**Returns:** `TRUE` if this is a terminal node

## Example

```c
/* Create a dialog node */
g_autoptr(LrgDialogNode) node = lrg_dialog_node_new("welcome");
lrg_dialog_node_set_speaker(node, "Village Elder");
lrg_dialog_node_set_text(node, "Welcome to our village, stranger!");

/* Add a condition - only show if player level >= 5 */
lrg_dialog_node_add_condition(node, "level >= 5");

/* Add an effect - mark this dialogue as seen */
lrg_dialog_node_add_effect(node, "set_flag:talked_to_elder");

/* Add response options */
g_autoptr(LrgDialogResponse) accept = lrg_dialog_response_new(
    "accept", "I'm honored to help", "quest_start"
);
lrg_dialog_node_add_response(node, accept);

g_autoptr(LrgDialogResponse) decline = lrg_dialog_response_new(
    "decline", "Maybe later", "end"
);
lrg_dialog_node_add_response(node, decline);

/* Evaluate conditions */
g_autoptr(GHashTable) context = g_hash_table_new(g_str_hash, g_str_equal);
g_hash_table_insert(context, "level", "10");
if (lrg_dialog_node_evaluate_conditions(node, context)) {
    g_print("Node conditions met\n");
}

/* Apply effects */
lrg_dialog_node_apply_effects(node, context);

/* Check if terminal */
if (lrg_dialog_node_is_terminal(node)) {
    g_print("This node ends the dialogue\n");
}
```
