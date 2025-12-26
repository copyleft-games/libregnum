# Dialog System Example

Complete example showing how to build an NPC dialogue system using the Dialog module.

## Setting Up the Dialog System

```c
/* Create and configure the runner */
g_autoptr(LrgDialogRunner) dialog_runner = lrg_dialog_runner_new();

/* Set game state variables that conditions might check */
g_autoptr(GHashTable) game_vars = g_hash_table_new(g_str_hash, g_str_equal);
lrg_dialog_runner_set_variable(dialog_runner, "player_level", "10");
lrg_dialog_runner_set_variable(dialog_runner, "has_sword", "true");
lrg_dialog_runner_set_variable(dialog_runner, "completed_quest_forest", "true");
```

## Building a Dialog Tree

```c
/* Create the tree */
g_autoptr(LrgDialogTree) npc_dialog = lrg_dialog_tree_new("merchant_greeting");
lrg_dialog_tree_set_start_node_id(npc_dialog, "greeting");

/* Initial greeting node */
g_autoptr(LrgDialogNode) greeting = lrg_dialog_node_new("greeting");
lrg_dialog_node_set_speaker(greeting, "Merchant");
lrg_dialog_node_set_text(greeting, "Welcome to my shop, traveler!");
/* Auto-advance to choices node */
lrg_dialog_node_set_next_node_id(greeting, "choices");
lrg_dialog_tree_add_node(npc_dialog, greeting);

/* Main interaction node with choices */
g_autoptr(LrgDialogNode) choices = lrg_dialog_node_new("choices");
lrg_dialog_node_set_speaker(choices, "Merchant");
lrg_dialog_node_set_text(choices, "What can I do for you?");

/* Response 1: Browse goods */
g_autoptr(LrgDialogResponse) browse = lrg_dialog_response_new(
    "browse",
    "Show me your wares",
    "shopping"
);
lrg_dialog_node_add_response(choices, browse);

/* Response 2: Ask about rumor (only if level >= 15) */
g_autoptr(LrgDialogResponse) rumor = lrg_dialog_response_new(
    "rumor",
    "Any rumors about monsters?",
    "monster_rumor"
);
lrg_dialog_response_add_condition(rumor, "level >= 15");
lrg_dialog_node_add_response(choices, rumor);

/* Response 3: Leave */
g_autoptr(LrgDialogResponse) leave = lrg_dialog_response_new(
    "leave",
    "I'll come back later",
    "farewell"
);
lrg_dialog_node_add_response(choices, leave);

lrg_dialog_tree_add_node(npc_dialog, choices);

/* Shopping node */
g_autoptr(LrgDialogNode) shopping = lrg_dialog_node_new("shopping");
lrg_dialog_node_set_speaker(shopping, "Merchant");
lrg_dialog_node_set_text(shopping, "Here's what I have in stock...");
lrg_dialog_node_add_effect(shopping, "open_shop_ui");
lrg_dialog_tree_add_node(npc_dialog, shopping);

/* Monster rumor (level-gated) */
g_autoptr(LrgDialogNode) rumor_node = lrg_dialog_node_new("monster_rumor");
lrg_dialog_node_set_speaker(rumor_node, "Merchant");
lrg_dialog_node_set_text(rumor_node,
    "There's a beast terrorizing the northern woods. Someone should do something...");
lrg_dialog_node_add_effect(rumor_node, "start_quest:hunt_beast");
lrg_dialog_tree_add_node(npc_dialog, rumor_node);

/* Farewell node (terminal) */
g_autoptr(LrgDialogNode) farewell = lrg_dialog_node_new("farewell");
lrg_dialog_node_set_speaker(farewell, "Merchant");
lrg_dialog_node_set_text(farewell, "Come back soon!");
lrg_dialog_tree_add_node(npc_dialog, farewell);

/* Validate the tree */
g_autoptr(GError) error = NULL;
if (!lrg_dialog_tree_validate(npc_dialog, &error)) {
    g_warning("Dialog tree invalid: %s", error->message);
    return;
}

lrg_dialog_runner_set_tree(dialog_runner, npc_dialog);
```

## Running Dialog in Game Loop

```c
/* Start the dialogue */
if (!lrg_dialog_runner_start(dialog_runner, NULL)) {
    g_warning("Failed to start dialogue");
    return;
}

gboolean dialog_active = TRUE;

while (dialog_active && game_running) {
    /* Get current node */
    LrgDialogNode *current = lrg_dialog_runner_get_current_node(dialog_runner);
    if (!current) break;

    /* Display node text */
    const gchar *speaker = lrg_dialog_node_get_speaker(current);
    const gchar *text = lrg_dialog_node_get_display_text(current);

    if (speaker) {
        g_print("%s: %s\n", speaker, text);
    } else {
        g_print("%s\n", text);
    }

    /* Check if at a choice */
    if (lrg_dialog_runner_is_at_choice(dialog_runner)) {
        /* Get available responses (filtered by conditions) */
        g_autoptr(GPtrArray) available =
            lrg_dialog_runner_get_available_responses(dialog_runner);

        g_print("\nAvailable responses:\n");
        for (guint i = 0; i < available->len; i++) {
            LrgDialogResponse *resp = g_ptr_array_index(available, i);
            g_print("[%u] %s\n", i, lrg_dialog_response_get_text(resp));
        }

        /* Get player input (simplified) */
        guint choice = get_player_input();

        if (choice < available->len) {
            /* Select response */
            if (!lrg_dialog_runner_select_response(dialog_runner, choice, NULL)) {
                g_warning("Failed to select response");
                break;
            }
        }
    } else {
        /* Auto-advance or terminal node */
        if (!lrg_dialog_runner_advance(dialog_runner, NULL)) {
            /* Dialog ended */
            dialog_active = FALSE;
        } else {
            /* Wait for player to continue */
            wait_for_continue_button();
        }
    }
}

g_print("Dialogue ended\n");
lrg_dialog_runner_stop(dialog_runner);
```

## Complex Conditions Example

```c
/* Create a more complex dialogue requiring game state checks */
g_autoptr(LrgDialogNode) quest_followup = lrg_dialog_node_new("quest_followup");
lrg_dialog_node_set_speaker(quest_followup, "Guard Captain");

/* Check multiple conditions */
lrg_dialog_node_add_condition(quest_followup, "completed_quest_forest");
lrg_dialog_node_add_condition(quest_followup, "player_level >= 20");
lrg_dialog_node_add_condition(quest_followup, "!rejected_captain_quest");

lrg_dialog_node_set_text(quest_followup,
    "Impressive! I have another task for someone of your skill...");

g_autoptr(LrgDialogResponse) accept = lrg_dialog_response_new(
    "accept",
    "I'm ready for the challenge",
    "new_quest"
);
lrg_dialog_response_add_effect(accept, "start_quest:dragon_hunt");
lrg_dialog_response_add_effect(accept, "increment_reputation:guards");
lrg_dialog_node_add_response(quest_followup, accept);

g_autoptr(LrgDialogResponse) decline = lrg_dialog_response_new(
    "decline",
    "Maybe later",
    "farewell"
);
lrg_dialog_response_add_effect(decline, "set_flag:rejected_captain_quest");
lrg_dialog_node_add_response(quest_followup, decline);
```

## Handling Conditions and Effects

For more advanced condition evaluation, subclass `LrgDialogNode`:

```c
struct _GameDialogNode {
    LrgDialogNode parent;
    GameState *game_state;  /* Reference to game state */
};

static gboolean
game_dialog_node_evaluate_conditions(LrgDialogNode *self, GHashTable *context) {
    GameDialogNode *node = GAME_DIALOG_NODE(self);

    /* Get node conditions */
    GPtrArray *conditions = lrg_dialog_node_get_conditions(self);
    for (guint i = 0; i < conditions->len; i++) {
        const gchar *cond = g_ptr_array_index(conditions, i);

        /* Evaluate custom condition logic */
        if (g_str_has_prefix(cond, "has_item:")) {
            const gchar *item_id = cond + 9;
            if (!inventory_has_item(node->game_state->inventory, item_id)) {
                return FALSE;  /* Condition failed */
            }
        } else if (g_str_has_prefix(cond, "skill_level:")) {
            const gchar *skill_level = cond + 12;
            if (node->game_state->skill_points < atoi(skill_level)) {
                return FALSE;
            }
        }
        /* ... handle other conditions ... */
    }

    return TRUE;
}

static void
game_dialog_node_apply_effects(LrgDialogNode *self, GHashTable *context) {
    GameDialogNode *node = GAME_DIALOG_NODE(self);

    /* Get node effects */
    GPtrArray *effects = lrg_dialog_node_get_effects(self);
    for (guint i = 0; i < effects->len; i++) {
        const gchar *effect = g_ptr_array_index(effects, i);

        /* Apply custom effects */
        if (g_str_has_prefix(effect, "give_item:")) {
            const gchar *item_id = effect + 10;
            inventory_add_item(node->game_state->inventory, item_id, 1);
        } else if (g_str_has_prefix(effect, "add_gold:")) {
            gint gold = atoi(effect + 9);
            node->game_state->gold += gold;
        }
        /* ... handle other effects ... */
    }
}
```

## Complete Example

See `/var/home/zach/Source/Projects/libregnum/tests/test-dialog.c` for full integration tests showing real usage patterns.
