# LrgQuestDef - Quest Definition

A blueprint for a quest containing stages (objectives), rewards, and prerequisites. Quest definitions are templates that can be instantiated multiple times to track individual player progress.

## Type

- **Derivable GObject Class** - Can be subclassed for custom quest behavior
- **Type ID** - `LRG_TYPE_QUEST_DEF`
- **GIR Name** - `Libregnum.QuestDef`

## Virtual Methods

### check_prerequisites()
```c
gboolean (*check_prerequisites) (LrgQuestDef *self,
                                 gpointer     player);
```

Checks if quest prerequisites are met. Can be overridden for custom logic.

### grant_rewards()
```c
void (*grant_rewards) (LrgQuestDef *self,
                       gpointer     player);
```

Grants quest rewards to the player. Can be overridden for custom reward logic.

## Construction

### lrg_quest_def_new()
```c
LrgQuestDef * lrg_quest_def_new (const gchar *id);
```

Creates a new quest definition.

**Parameters:**
- `id` - Unique identifier for the quest

**Returns:** A new `LrgQuestDef` (transfer full)

## Identification

### lrg_quest_def_get_id()
```c
const gchar * lrg_quest_def_get_id (LrgQuestDef *self);
```

Gets the quest ID.

**Parameters:**
- `self` - An `LrgQuestDef`

**Returns:** The quest ID (transfer none)

## Quest Info

### lrg_quest_def_get_name()
```c
const gchar * lrg_quest_def_get_name (LrgQuestDef *self);
```

Gets the quest name.

**Parameters:**
- `self` - An `LrgQuestDef`

**Returns:** The quest name (transfer none, nullable)

### lrg_quest_def_set_name()
```c
void lrg_quest_def_set_name (LrgQuestDef *self,
                             const gchar *name);
```

Sets the quest name.

**Parameters:**
- `self` - An `LrgQuestDef`
- `name` - Quest name (nullable)

### lrg_quest_def_get_description()
```c
const gchar * lrg_quest_def_get_description (LrgQuestDef *self);
```

Gets the quest description.

**Parameters:**
- `self` - An `LrgQuestDef`

**Returns:** The description (transfer none, nullable)

### lrg_quest_def_set_description()
```c
void lrg_quest_def_set_description (LrgQuestDef *self,
                                    const gchar *description);
```

Sets the quest description.

**Parameters:**
- `self` - An `LrgQuestDef`
- `description` - Quest description (nullable)

### lrg_quest_def_get_giver_npc()
```c
const gchar * lrg_quest_def_get_giver_npc (LrgQuestDef *self);
```

Gets the quest giver NPC ID.

**Parameters:**
- `self` - An `LrgQuestDef`

**Returns:** The giver NPC ID (transfer none, nullable)

### lrg_quest_def_set_giver_npc()
```c
void lrg_quest_def_set_giver_npc (LrgQuestDef *self,
                                  const gchar *npc_id);
```

Sets the quest giver NPC.

**Parameters:**
- `self` - An `LrgQuestDef`
- `npc_id` - NPC ID (nullable)

## Stages/Objectives

### lrg_quest_def_add_stage()
```c
void lrg_quest_def_add_stage (LrgQuestDef       *self,
                              LrgQuestObjective *objective);
```

Adds a stage (objective) to the quest.

**Parameters:**
- `self` - An `LrgQuestDef`
- `objective` - Objective to add as a stage (transfer full)

### lrg_quest_def_get_stages()
```c
GPtrArray * lrg_quest_def_get_stages (LrgQuestDef *self);
```

Gets all quest stages.

**Parameters:**
- `self` - An `LrgQuestDef`

**Returns:** Array of `LrgQuestObjective` (element-type, transfer none)

### lrg_quest_def_get_stage_count()
```c
guint lrg_quest_def_get_stage_count (LrgQuestDef *self);
```

Gets the number of stages.

**Parameters:**
- `self` - An `LrgQuestDef`

**Returns:** Stage count

### lrg_quest_def_get_stage()
```c
LrgQuestObjective * lrg_quest_def_get_stage (LrgQuestDef *self,
                                             guint        index);
```

Gets a stage by index.

**Parameters:**
- `self` - An `LrgQuestDef`
- `index` - Stage index

**Returns:** The stage objective (transfer none, nullable)

## Prerequisites

### lrg_quest_def_add_prerequisite()
```c
void lrg_quest_def_add_prerequisite (LrgQuestDef *self,
                                     const gchar *quest_id);
```

Adds a prerequisite quest that must be completed first.

**Parameters:**
- `self` - An `LrgQuestDef`
- `quest_id` - Prerequisite quest ID

### lrg_quest_def_get_prerequisites()
```c
GPtrArray * lrg_quest_def_get_prerequisites (LrgQuestDef *self);
```

Gets all prerequisite quest IDs.

**Parameters:**
- `self` - An `LrgQuestDef`

**Returns:** Array of quest IDs (element-type utf8, transfer none)

## Rewards

### lrg_quest_def_set_reward_gold()
```c
void lrg_quest_def_set_reward_gold (LrgQuestDef *self,
                                    gint         gold);
```

Sets the gold reward.

**Parameters:**
- `self` - An `LrgQuestDef`
- `gold` - Gold reward amount

### lrg_quest_def_get_reward_gold()
```c
gint lrg_quest_def_get_reward_gold (LrgQuestDef *self);
```

Gets the gold reward.

**Parameters:**
- `self` - An `LrgQuestDef`

**Returns:** Gold amount

### lrg_quest_def_set_reward_xp()
```c
void lrg_quest_def_set_reward_xp (LrgQuestDef *self,
                                  gint         xp);
```

Sets the experience reward.

**Parameters:**
- `self` - An `LrgQuestDef`
- `xp` - Experience reward amount

### lrg_quest_def_get_reward_xp()
```c
gint lrg_quest_def_get_reward_xp (LrgQuestDef *self);
```

Gets the experience reward.

**Parameters:**
- `self` - An `LrgQuestDef`

**Returns:** XP amount

### lrg_quest_def_add_reward_item()
```c
void lrg_quest_def_add_reward_item (LrgQuestDef *self,
                                    const gchar *item_id,
                                    guint        count);
```

Adds an item reward.

**Parameters:**
- `self` - An `LrgQuestDef`
- `item_id` - Item ID to reward
- `count` - Number of items

### lrg_quest_def_get_reward_items()
```c
GHashTable * lrg_quest_def_get_reward_items (LrgQuestDef *self);
```

Gets all item rewards as a hash table (item_id -> count).

**Parameters:**
- `self` - An `LrgQuestDef`

**Returns:** Hash table of item rewards (transfer none)

## Prerequisite Checking and Reward Granting

### lrg_quest_def_check_prerequisites()
```c
gboolean lrg_quest_def_check_prerequisites (LrgQuestDef *self,
                                            gpointer     player);
```

Checks if all prerequisites are met.

**Parameters:**
- `self` - An `LrgQuestDef`
- `player` - (nullable) Player context

**Returns:** `TRUE` if prerequisites are satisfied

### lrg_quest_def_grant_rewards()
```c
void lrg_quest_def_grant_rewards (LrgQuestDef *self,
                                  gpointer     player);
```

Grants all quest rewards.

**Parameters:**
- `self` - An `LrgQuestDef`
- `player` - (nullable) Player to grant rewards to

## Example

```c
/* Create a quest */
g_autoptr(LrgQuestDef) quest = lrg_quest_def_new("collect_herbs");
lrg_quest_def_set_name(quest, "Gather Medicinal Herbs");
lrg_quest_def_set_description(quest, "The herbalist needs rare plants");
lrg_quest_def_set_giver_npc(quest, "herbalist");

/* Add prerequisites */
lrg_quest_def_add_prerequisite(quest, "learn_herbalism");

/* Add objective: collect 5 moonflowers */
g_autoptr(LrgQuestObjective) collect = lrg_quest_objective_new(
    "collect_moonflowers",
    "Collect moonflowers",
    LRG_QUEST_OBJECTIVE_TYPE_COLLECT
);
lrg_quest_objective_set_target_id(collect, "moonflower");
lrg_quest_objective_set_target_count(collect, 5);
lrg_quest_def_add_stage(quest, collect);

/* Add rewards */
lrg_quest_def_set_reward_gold(quest, 500);
lrg_quest_def_set_reward_xp(quest, 1000);
lrg_quest_def_add_reward_item(quest, "herb_bag", 1);

/* Check prerequisites */
if (lrg_quest_def_check_prerequisites(quest, player)) {
    g_print("Can start this quest\n");
}

/* Grant rewards */
lrg_quest_def_grant_rewards(quest, player);
```
