# Quest Module

The Quest module provides a complete quest system with definitions, progress tracking, and quest log management. Supports multi-stage quests with completion conditions and rewards.

## Overview

The Quest module consists of four core classes:

- **LrgQuestDef** - Blueprint for a quest with stages, rewards, and prerequisites
- **LrgQuestObjective** - A single goal within a quest (kill, collect, reach)
- **LrgQuestInstance** - Active quest tracking player progress
- **LrgQuestLog** - Container for active and completed quests

## Key Features

- **Quest stages** - Multi-stage quests with sequential progress
- **Objective types** - Kill, collect items, reach locations, custom types
- **Progress tracking** - Track current count toward goal
- **Completion detection** - Automatic detection when objectives complete
- **Quest rewards** - Gold, experience, items
- **Prerequisites** - Require other quests to be completed first
- **Quest state** - Active, complete, failed, abandoned
- **Quest log** - Track active, completed, and failed quests
- **Tracked quests** - Display current objective in HUD

## Quick Start

```c
/* Create a quest definition */
g_autoptr(LrgQuestDef) quest = lrg_quest_def_new("dragon_slayer");
lrg_quest_def_set_name(quest, "Slay the Dragon");
lrg_quest_def_set_description(quest, "The kingdom needs a hero!");
lrg_quest_def_set_giver_npc(quest, "king");

/* Add reward */
lrg_quest_def_set_reward_gold(quest, 1000);
lrg_quest_def_set_reward_xp(quest, 5000);
lrg_quest_def_add_reward_item(quest, "dragon_scales", 5);

/* Add stages */
g_autoptr(LrgQuestObjective) kill_dragon = lrg_quest_objective_new(
    "kill_dragon",
    "Slay the dragon",
    LRG_QUEST_OBJECTIVE_TYPE_KILL
);
lrg_quest_objective_set_target_id(kill_dragon, "dragon_boss");
lrg_quest_objective_set_target_count(kill_dragon, 1);
lrg_quest_def_add_stage(quest, kill_dragon);

/* Create quest instance */
g_autoptr(LrgQuestInstance) instance = lrg_quest_instance_new(quest);

/* Track progress */
lrg_quest_instance_update_progress(
    instance,
    LRG_QUEST_OBJECTIVE_TYPE_KILL,
    "dragon_boss",
    1
);

/* Check if complete */
if (lrg_quest_instance_is_complete(instance)) {
    lrg_quest_instance_complete(instance);
    apply_quest_rewards(instance);
}
```

## Objective Types

- **KILL** - Kill a certain number of enemies
- **COLLECT** - Collect a certain number of items
- **REACH** - Reach a specific location
- **TALK** - Talk to an NPC
- **DELIVER** - Deliver an item

## Quest States

- **ACTIVE** - Quest is currently in progress
- **COMPLETE** - Quest finished successfully
- **FAILED** - Quest failed (optional)
- **ABANDONED** - Player gave up on the quest

## API Reference

See the individual class documentation:

- [LrgQuestDef](quest-def.md) - Quest blueprint and definition
- [LrgQuestObjective](quest-objective.md) - Single quest objective
- [LrgQuestInstance](quest-instance.md) - Active quest with progress
- [LrgQuestLog](quest-log.md) - Quest management and tracking
