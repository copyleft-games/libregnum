# LrgDialogRunner - Dialog Flow Manager

Manages the flow of conversation during gameplay. Tracks the current node, filters available responses based on conditions, and evaluates effects. One runner handles one active conversation at a time.

## Type

- **Final GObject Class** - Cannot be subclassed
- **Type ID** - `LRG_TYPE_DIALOG_RUNNER`
- **GIR Name** - `Libregnum.DialogRunner`

## Construction

### lrg_dialog_runner_new()
```c
LrgDialogRunner * lrg_dialog_runner_new (void);
```

Creates a new dialog runner.

**Parameters:** None

**Returns:** A new `LrgDialogRunner` (transfer full)

## Tree Management

### lrg_dialog_runner_get_tree()
```c
LrgDialogTree * lrg_dialog_runner_get_tree (LrgDialogRunner *self);
```

Gets the current dialog tree.

**Parameters:**
- `self` - An `LrgDialogRunner`

**Returns:** The current tree (transfer none, nullable)

### lrg_dialog_runner_set_tree()
```c
void lrg_dialog_runner_set_tree (LrgDialogRunner *self,
                                 LrgDialogTree   *tree);
```

Sets the dialog tree for this runner.

**Parameters:**
- `self` - An `LrgDialogRunner`
- `tree` - Dialog tree to set (nullable)

## Current Node

### lrg_dialog_runner_get_current_node()
```c
LrgDialogNode * lrg_dialog_runner_get_current_node (LrgDialogRunner *self);
```

Gets the current dialog node.

**Parameters:**
- `self` - An `LrgDialogRunner`

**Returns:** The current node (transfer none, nullable)

## Starting Dialogs

### lrg_dialog_runner_start()
```c
gboolean lrg_dialog_runner_start (LrgDialogRunner  *self,
                                  GError          **error);
```

Starts the dialog from the tree's start node.

**Parameters:**
- `self` - An `LrgDialogRunner`
- `error` - (optional) Return location for error

**Returns:** `TRUE` on success

### lrg_dialog_runner_start_at()
```c
gboolean lrg_dialog_runner_start_at (LrgDialogRunner  *self,
                                     const gchar      *node_id,
                                     GError          **error);
```

Starts the dialog at a specific node.

**Parameters:**
- `self` - An `LrgDialogRunner`
- `node_id` - Node ID to start at
- `error` - (optional) Return location for error

**Returns:** `TRUE` on success

## Navigation

### lrg_dialog_runner_advance()
```c
gboolean lrg_dialog_runner_advance (LrgDialogRunner  *self,
                                    GError          **error);
```

Advances to the next node if auto-advance is set (next_node_id).

**Parameters:**
- `self` - An `LrgDialogRunner`
- `error` - (optional) Return location for error

**Returns:** `TRUE` if advanced, `FALSE` if at a choice or terminal node

### lrg_dialog_runner_select_response()
```c
gboolean lrg_dialog_runner_select_response (LrgDialogRunner  *self,
                                            guint             index,
                                            GError          **error);
```

Selects a response by index and advances to its target node.

**Parameters:**
- `self` - An `LrgDialogRunner`
- `index` - Response index
- `error` - (optional) Return location for error

**Returns:** `TRUE` on success

## Status Queries

### lrg_dialog_runner_is_active()
```c
gboolean lrg_dialog_runner_is_active (LrgDialogRunner *self);
```

Checks if a dialog is currently active.

**Parameters:**
- `self` - An `LrgDialogRunner`

**Returns:** `TRUE` if active

### lrg_dialog_runner_is_at_choice()
```c
gboolean lrg_dialog_runner_is_at_choice (LrgDialogRunner *self);
```

Checks if at a node that requires a response selection.

**Parameters:**
- `self` - An `LrgDialogRunner`

**Returns:** `TRUE` if at a choice node

## Stopping

### lrg_dialog_runner_stop()
```c
void lrg_dialog_runner_stop (LrgDialogRunner *self);
```

Stops the current dialog.

**Parameters:**
- `self` - An `LrgDialogRunner`

## Response Filtering

### lrg_dialog_runner_get_available_responses()
```c
GPtrArray * lrg_dialog_runner_get_available_responses (LrgDialogRunner *self);
```

Gets available responses for the current node. Only returns responses whose conditions pass.

**Parameters:**
- `self` - An `LrgDialogRunner`

**Returns:** Array of `LrgDialogResponse` (element-type, transfer container)

## Context Variables

### lrg_dialog_runner_get_context()
```c
GHashTable * lrg_dialog_runner_get_context (LrgDialogRunner *self);
```

Gets the variable context for conditions and effects.

**Parameters:**
- `self` - An `LrgDialogRunner`

**Returns:** The context hash table (transfer none)

### lrg_dialog_runner_set_variable()
```c
void lrg_dialog_runner_set_variable (LrgDialogRunner *self,
                                     const gchar     *key,
                                     const gchar     *value);
```

Sets a variable in the context.

**Parameters:**
- `self` - An `LrgDialogRunner`
- `key` - Variable name
- `value` - Variable value

### lrg_dialog_runner_get_variable()
```c
const gchar * lrg_dialog_runner_get_variable (LrgDialogRunner *self,
                                              const gchar     *key);
```

Gets a variable from the context.

**Parameters:**
- `self` - An `LrgDialogRunner`
- `key` - Variable name

**Returns:** Variable value (transfer none, nullable)

## Example

```c
/* Create and setup runner */
g_autoptr(LrgDialogRunner) runner = lrg_dialog_runner_new();
lrg_dialog_runner_set_tree(runner, tree);

/* Set game state variables */
lrg_dialog_runner_set_variable(runner, "player_level", "10");
lrg_dialog_runner_set_variable(runner, "has_sword", "true");
lrg_dialog_runner_set_variable(runner, "gold", "500");

/* Start dialog */
if (!lrg_dialog_runner_start(runner, NULL)) {
    g_warning("Failed to start dialog\n");
    return;
}

/* Main dialog loop */
while (lrg_dialog_runner_is_active(runner)) {
    LrgDialogNode *node = lrg_dialog_runner_get_current_node(runner);
    if (!node) break;

    /* Display node text */
    const gchar *speaker = lrg_dialog_node_get_speaker(node);
    const gchar *text = lrg_dialog_node_get_display_text(node);

    if (speaker) {
        g_print("%s: %s\n", speaker, text);
    } else {
        g_print("%s\n", text);
    }

    /* Check if this is a choice node */
    if (lrg_dialog_runner_is_at_choice(runner)) {
        /* Get available responses */
        GPtrArray *responses = lrg_dialog_runner_get_available_responses(runner);

        /* Display responses */
        for (guint i = 0; i < responses->len; i++) {
            LrgDialogResponse *resp = g_ptr_array_index(responses, i);
            g_print("[%u] %s\n", i, lrg_dialog_response_get_text(resp));
        }

        /* Get player choice */
        guint choice = get_player_input();

        /* Select response */
        if (!lrg_dialog_runner_select_response(runner, choice, NULL)) {
            g_warning("Failed to select response\n");
            break;
        }
    } else {
        /* Try auto-advance */
        if (!lrg_dialog_runner_advance(runner, NULL)) {
            /* Terminal node - dialog ends */
            break;
        }
    }
}

lrg_dialog_runner_stop(runner);
g_print("Dialog ended\n");
```

## Game Loop Integration

```c
/* Setup */
g_autoptr(LrgDialogRunner) dialog = lrg_dialog_runner_new();
gboolean dialog_active = FALSE;

/* Game loop */
while (game_running) {
    if (!dialog_active && player_activated_npc) {
        LrgDialogTree *npc_dialog = get_npc_dialog_tree(npc);
        lrg_dialog_runner_set_tree(dialog, npc_dialog);
        lrg_dialog_runner_start(dialog, NULL);
        dialog_active = TRUE;
    }

    if (dialog_active) {
        if (lrg_dialog_runner_is_at_choice(dialog)) {
            /* Show UI choices, handle player input */
            if (player_selected_response) {
                guint choice = player_response_index;
                lrg_dialog_runner_select_response(dialog, choice, NULL);
            }
        } else {
            /* Auto-advance on button press */
            if (player_pressed_continue) {
                if (!lrg_dialog_runner_advance(dialog, NULL)) {
                    dialog_active = FALSE;
                }
            }
        }

        /* Draw current dialog node */
        LrgDialogNode *node = lrg_dialog_runner_get_current_node(dialog);
        if (node) {
            draw_dialog_ui(node, dialog);
        }
    }

    render_frame();
}
```
