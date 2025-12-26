# Dialog Module

The Dialog module provides a complete dialogue system for building branching conversations with NPCs. It includes a node-based editor-friendly format, condition evaluation, and dynamic response filtering.

## Overview

The Dialog module consists of four core classes:

- **LrgDialogNode** - A single point in a conversation with text, speaker, and response options
- **LrgDialogResponse** - A player choice that leads to another node
- **LrgDialogTree** - A complete dialogue tree containing interconnected nodes
- **LrgDialogRunner** - Manages conversation flow and state during gameplay

## Key Features

- **Node-based structure** - Non-linear conversations with multiple branches
- **Response filtering** - Show/hide responses based on game state conditions
- **Node effects** - Trigger actions when entering a node (inventory changes, flags)
- **Context variables** - Pass game state into conditions and effects
- **Terminal nodes** - End conversations gracefully
- **Multiple next nodes** - Support both auto-advance and response choices
- **Condition expressions** - Evaluate conditions based on game state
- **Extensible** - Derivable node class for custom behaviors

## Quick Start

```c
/* Create a dialog tree */
g_autoptr(LrgDialogTree) tree = lrg_dialog_tree_new("npc_greet");
lrg_dialog_tree_set_start_node_id(tree, "start");

/* Create nodes */
g_autoptr(LrgDialogNode) start = lrg_dialog_node_new("start");
lrg_dialog_node_set_speaker(start, "NPC");
lrg_dialog_node_set_text(start, "Hello, traveler!");

g_autoptr(LrgDialogNode) response_node = lrg_dialog_node_new("response");
lrg_dialog_node_set_speaker(response_node, "NPC");
lrg_dialog_node_set_text(response_node, "You look strong. Can you help me?");

/* Add responses */
g_autoptr(LrgDialogResponse) yes = lrg_dialog_response_new(
    "yes", "I'll help!", "help_quest"
);
lrg_dialog_node_add_response(response_node, yes);

g_autoptr(LrgDialogResponse) no = lrg_dialog_response_new(
    "no", "Sorry, I'm busy", "end"
);
lrg_dialog_node_add_response(response_node, no);

/* Add nodes to tree */
lrg_dialog_tree_add_node(tree, start);
lrg_dialog_tree_add_node(tree, response_node);

/* Run the dialog */
g_autoptr(LrgDialogRunner) runner = lrg_dialog_runner_new();
lrg_dialog_runner_set_tree(runner, tree);
lrg_dialog_runner_start(runner, NULL);

/* In your UI loop */
while (lrg_dialog_runner_is_active(runner)) {
    /* Display current node text */
    LrgDialogNode *node = lrg_dialog_runner_get_current_node(runner);
    if (node) {
        g_print("%s: %s\n",
                lrg_dialog_node_get_speaker(node),
                lrg_dialog_node_get_display_text(node));
    }

    if (lrg_dialog_runner_is_at_choice(runner)) {
        /* Show responses */
        GPtrArray *responses = lrg_dialog_runner_get_available_responses(runner);
        for (guint i = 0; i < responses->len; i++) {
            LrgDialogResponse *resp = g_ptr_array_index(responses, i);
            g_print("[%u] %s\n", i, lrg_dialog_response_get_text(resp));
        }

        /* Get player input and select response */
        guint choice = get_player_input();
        lrg_dialog_runner_select_response(runner, choice, NULL);
    } else if (lrg_dialog_runner_advance(runner, NULL)) {
        /* Auto-advanced to next node */
        continue;
    } else {
        /* Terminal node or choice */
        break;
    }
}
```

## Conditions and Effects

Dialogues can check game state and modify it:

```c
/* Node only appears if player hasn't talked to NPC */
lrg_dialog_node_add_condition(node, "!has_talked_to_npc");

/* Response only available if player has quest item */
lrg_dialog_response_add_condition(response, "has_quest_item");

/* When node is entered, set a flag */
lrg_dialog_node_add_effect(node, "set_talked_to_npc");

/* When response is selected, give reward */
lrg_dialog_response_add_effect(response, "add_gold:100");
```

## Variable Context

Pass game state to the runner for condition evaluation:

```c
/* Set initial variables */
lrg_dialog_runner_set_variable(runner, "player_level", "10");
lrg_dialog_runner_set_variable(runner, "has_sword", "true");
lrg_dialog_runner_set_variable(runner, "gold", "500");

/* Check variable */
const gchar *value = lrg_dialog_runner_get_variable(runner, "player_level");
```

## API Reference

See the individual class documentation:

- [LrgDialogNode](dialog-node.md) - Single dialogue point with responses
- [LrgDialogResponse](dialog-response.md) - Player choice option
- [LrgDialogTree](dialog-tree.md) - Complete dialogue structure
- [LrgDialogRunner](dialog-runner.md) - Conversation flow manager
