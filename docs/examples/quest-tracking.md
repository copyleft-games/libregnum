# Quest System Example

Complete example showing how to integrate the Quest module into a game.

## Setting Up the Quest System

```c
/* Create the quest log */
g_autoptr(LrgQuestLog) journal = lrg_quest_log_new();

/* Define some quests */
g_autoptr(LrgQuestDef) slay_dragon = lrg_quest_def_new("slay_dragon");
lrg_quest_def_set_name(slay_dragon, "Slay the Dragon");
lrg_quest_def_set_description(slay_dragon, "A great dragon terrorizes the kingdom");
lrg_quest_def_set_giver_npc(slay_dragon, "king");

/* Add objective: kill the dragon */
g_autoptr(LrgQuestObjective) kill = lrg_quest_objective_new(
    "kill_dragon",
    "Defeat the dragon",
    LRG_QUEST_OBJECTIVE_TYPE_KILL
);
lrg_quest_objective_set_target_id(kill, "dragon_boss");
lrg_quest_objective_set_target_count(kill, 1);
lrg_quest_def_add_stage(slay_dragon, kill);

/* Add rewards */
lrg_quest_def_set_reward_gold(slay_dragon, 10000);
lrg_quest_def_set_reward_xp(slay_dragon, 50000);
lrg_quest_def_add_reward_item(slay_dragon, "dragon_scales", 5);

/* Player accepts quest from NPC */
LrgQuestInstance *active = lrg_quest_log_start_quest(journal, slay_dragon);
if (!active) {
    g_warning("Failed to start quest\n");
    return;
}

g_print("Started quest: %s\n", lrg_quest_def_get_name(slay_dragon));
```

## Multi-Stage Quest Example

```c
/* Create a more complex quest */
g_autoptr(LrgQuestDef) artifact_hunt = lrg_quest_def_new("artifact_hunt");
lrg_quest_def_set_name(artifact_hunt, "Find the Sacred Artifact");
lrg_quest_def_set_description(artifact_hunt, "Locate three artifact pieces");

/* Stage 1: Explore ancient ruin */
g_autoptr(LrgQuestObjective) explore = lrg_quest_objective_new(
    "explore_ruin",
    "Explore the ancient ruin",
    LRG_QUEST_OBJECTIVE_TYPE_REACH
);
lrg_quest_objective_set_location(explore, "ancient_ruin");
lrg_quest_def_add_stage(artifact_hunt, explore);

/* Stage 2: Collect artifact pieces */
g_autoptr(LrgQuestObjective) collect = lrg_quest_objective_new(
    "collect_pieces",
    "Collect artifact pieces",
    LRG_QUEST_OBJECTIVE_TYPE_COLLECT
);
lrg_quest_objective_set_target_id(collect, "artifact_piece");
lrg_quest_objective_set_target_count(collect, 3);
lrg_quest_def_add_stage(artifact_hunt, collect);

/* Stage 3: Return to quest giver */
g_autoptr(LrgQuestObjective) return_obj = lrg_quest_objective_new(
    "return",
    "Return to the sage",
    LRG_QUEST_OBJECTIVE_TYPE_TALK
);
lrg_quest_objective_set_target_id(return_obj, "sage");
lrg_quest_def_add_stage(artifact_hunt, return_obj);

/* Start the quest */
LrgQuestInstance *instance = lrg_quest_log_start_quest(journal, artifact_hunt);

/* Track it for HUD display */
lrg_quest_log_track_quest(journal, "artifact_hunt");
```

## Tracking Quest Progress in Game Loop

```c
/* In your game update loop */
void game_update(Game *game, float delta) {
    LrgQuestLog *journal = game->quest_log;

    /* Check if player reached a location */
    if (player_reached_location(game, "ancient_ruin")) {
        LrgQuestInstance *quest = lrg_quest_log_get_quest(journal, "artifact_hunt");
        if (quest) {
            lrg_quest_instance_update_progress(
                quest,
                LRG_QUEST_OBJECTIVE_TYPE_REACH,
                NULL,
                1
            );
            g_print("Reached ancient ruin!\n");
        }
    }

    /* Check if player collected an item */
    if (player_collected_item(game, "artifact_piece")) {
        for (GList *l = lrg_quest_log_get_active_quests(journal); l; l = l->next) {
            LrgQuestInstance *q = l->data;
            lrg_quest_instance_update_progress(
                q,
                LRG_QUEST_OBJECTIVE_TYPE_COLLECT,
                "artifact_piece",
                1
            );

            /* Check if objective just completed */
            LrgQuestObjective *obj = lrg_quest_instance_get_current_objective(q);
            if (obj && lrg_quest_objective_is_complete(obj)) {
                g_print("Stage complete!\n");
                lrg_quest_instance_advance_stage(q);
            }
        }
    }

    /* Check if player killed an enemy */
    if (player_killed_enemy(game, "dragon_boss")) {
        LrgQuestInstance *quest = lrg_quest_log_get_quest(journal, "slay_dragon");
        if (quest) {
            lrg_quest_instance_update_progress(
                quest,
                LRG_QUEST_OBJECTIVE_TYPE_KILL,
                "dragon_boss",
                1
            );

            if (lrg_quest_instance_is_complete(quest)) {
                lrg_quest_instance_complete(quest);
                apply_quest_rewards(game, quest);
                g_print("Quest complete!\n");
            }
        }
    }
}
```

## Displaying Quest Information in HUD

```c
void draw_quest_hud(Game *game) {
    LrgQuestLog *journal = game->quest_log;

    /* Draw tracked quest */
    LrgQuestInstance *tracked = lrg_quest_log_get_tracked_quest(journal);
    if (!tracked) return;

    LrgQuestDef *def = lrg_quest_instance_get_quest_def(tracked);
    const gchar *name = lrg_quest_def_get_name(def);

    /* Draw quest title */
    g_print("Quest: %s\n", name);

    /* Draw current objective */
    LrgQuestObjective *obj = lrg_quest_instance_get_current_objective(tracked);
    if (obj) {
        const gchar *desc = lrg_quest_objective_get_description(obj);
        guint current = lrg_quest_objective_get_current_count(obj);
        guint target = lrg_quest_objective_get_target_count(obj);

        g_print("  %s (%u/%u)\n", desc, current, target);

        /* Draw progress bar */
        gdouble progress = lrg_quest_objective_get_progress(obj);
        draw_progress_bar(progress);
    } else {
        /* Quest complete */
        g_print("  [COMPLETE]\n");
    }

    /* Draw overall progress */
    gdouble overall = lrg_quest_instance_get_progress(tracked);
    g_print("Overall: %.0f%%\n", overall * 100.0);
}

void draw_quest_log(Game *game) {
    LrgQuestLog *journal = game->quest_log;

    /* Draw active quests */
    g_print("Active Quests (%u):\n", lrg_quest_log_get_active_count(journal));

    GList *active = lrg_quest_log_get_active_quests(journal);
    for (GList *l = active; l; l = l->next) {
        LrgQuestInstance *q = l->data;
        LrgQuestDef *def = lrg_quest_instance_get_quest_def(q);
        gdouble progress = lrg_quest_instance_get_progress(q);

        g_print("  - %s [%.0f%%]\n",
                lrg_quest_def_get_name(def),
                progress * 100.0);
    }
    g_list_free(active);

    /* Draw completed quests */
    g_print("\nCompleted Quests (%u):\n", lrg_quest_log_get_completed_count(journal));

    GList *completed = lrg_quest_log_get_completed_quests(journal);
    for (GList *l = completed; l; l = l->next) {
        LrgQuestInstance *q = l->data;
        LrgQuestDef *def = lrg_quest_instance_get_quest_def(q);

        g_print("  - %s\n", lrg_quest_def_get_name(def));
    }
    g_list_free(completed);
}
```

## Quest Giver NPC Interaction

```c
void interact_with_npc(Game *game, NPC *npc) {
    LrgQuestLog *journal = game->quest_log;
    const gchar *npc_id = npc->id;

    /* Get quests given by this NPC */
    g_autoptr(GPtrArray) available_quests = get_quests_from_npc(npc_id);

    for (guint i = 0; i < available_quests->len; i++) {
        LrgQuestDef *quest_def = g_ptr_array_index(available_quests, i);
        const gchar *quest_id = lrg_quest_def_get_id(quest_def);

        /* Check if already completed */
        if (lrg_quest_log_is_quest_completed(journal, quest_id)) {
            g_print("[COMPLETED] %s\n", lrg_quest_def_get_name(quest_def));
            continue;
        }

        /* Check if already active */
        if (lrg_quest_log_is_quest_active(journal, quest_id)) {
            g_print("[ACTIVE] %s\n", lrg_quest_def_get_name(quest_def));
            continue;
        }

        /* Check prerequisites */
        if (!lrg_quest_def_check_prerequisites(quest_def, game->player)) {
            g_print("[LOCKED] %s (prerequisites not met)\n",
                    lrg_quest_def_get_name(quest_def));
            continue;
        }

        /* Quest available to accept */
        g_print("[AVAILABLE] %s\n", lrg_quest_def_get_name(quest_def));
        g_print("  %s\n", lrg_quest_def_get_description(quest_def));

        if (player_wants_quest()) {
            lrg_quest_log_start_quest(journal, quest_def);
            g_print("Quest accepted!\n");
        }
    }
}
```

## See Also

- [Dialog System Example](dialog-system.md) - For quest-related dialogue
- [Save System Example](save-load.md) - For persisting quest progress
