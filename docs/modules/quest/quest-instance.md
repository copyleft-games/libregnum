# LrgQuestInstance - Active Quest Instance

An active instance of a quest that tracks player progress. Created from a `LrgQuestDef`, it maintains the current state, stage index, and objective progress. One instance per active quest in the player's journal.

## Type

- **Final GObject Class** - Cannot be subclassed
- **Type ID** - `LRG_TYPE_QUEST_INSTANCE`
- **GIR Name** - `Libregnum.QuestInstance`

## Construction

### lrg_quest_instance_new()
```c
LrgQuestInstance * lrg_quest_instance_new (LrgQuestDef *quest_def);
```

Creates a new quest instance from a definition.

**Parameters:**
- `quest_def` - The quest definition

**Returns:** A new `LrgQuestInstance` (transfer full)

## Definition Access

### lrg_quest_instance_get_quest_def()
```c
LrgQuestDef * lrg_quest_instance_get_quest_def (LrgQuestInstance *self);
```

Gets the quest definition.

**Parameters:**
- `self` - An `LrgQuestInstance`

**Returns:** The quest definition (transfer none)

## Quest State

### lrg_quest_instance_get_state()
```c
LrgQuestState lrg_quest_instance_get_state (LrgQuestInstance *self);
```

Gets the current quest state.

**Parameters:**
- `self` - An `LrgQuestInstance`

**Returns:** The quest state (ACTIVE, COMPLETE, FAILED, ABANDONED)

### lrg_quest_instance_set_state()
```c
void lrg_quest_instance_set_state (LrgQuestInstance *self,
                                   LrgQuestState     state);
```

Sets the quest state.

**Parameters:**
- `self` - An `LrgQuestInstance`
- `state` - New state

## Stage Progress

### lrg_quest_instance_get_current_stage()
```c
guint lrg_quest_instance_get_current_stage (LrgQuestInstance *self);
```

Gets the current stage index.

**Parameters:**
- `self` - An `LrgQuestInstance`

**Returns:** Current stage index

### lrg_quest_instance_get_current_objective()
```c
LrgQuestObjective * lrg_quest_instance_get_current_objective (LrgQuestInstance *self);
```

Gets the current stage objective with progress.

**Parameters:**
- `self` - An `LrgQuestInstance`

**Returns:** Current objective (transfer none, nullable) - `NULL` if complete

## Progress Updates

### lrg_quest_instance_update_progress()
```c
gboolean lrg_quest_instance_update_progress (LrgQuestInstance      *self,
                                             LrgQuestObjectiveType  objective_type,
                                             const gchar           *target_id,
                                             guint                  amount);
```

Updates progress for matching objectives. Automatically advances stages when objectives complete.

**Parameters:**
- `self` - An `LrgQuestInstance`
- `objective_type` - Type of objective to update
- `target_id` - (nullable) Target entity/item ID
- `amount` - Amount to add

**Returns:** `TRUE` if progress was updated

### lrg_quest_instance_advance_stage()
```c
gboolean lrg_quest_instance_advance_stage (LrgQuestInstance *self);
```

Advances to the next stage if current objective is complete.

**Parameters:**
- `self` - An `LrgQuestInstance`

**Returns:** `TRUE` if advanced

## Completion/Failure

### lrg_quest_instance_complete()
```c
void lrg_quest_instance_complete (LrgQuestInstance *self);
```

Marks the quest as complete.

**Parameters:**
- `self` - An `LrgQuestInstance`

### lrg_quest_instance_fail()
```c
void lrg_quest_instance_fail (LrgQuestInstance *self);
```

Marks the quest as failed.

**Parameters:**
- `self` - An `LrgQuestInstance`

### lrg_quest_instance_is_complete()
```c
gboolean lrg_quest_instance_is_complete (LrgQuestInstance *self);
```

Checks if the quest is complete.

**Parameters:**
- `self` - An `LrgQuestInstance`

**Returns:** `TRUE` if complete

## Overall Progress

### lrg_quest_instance_get_progress()
```c
gdouble lrg_quest_instance_get_progress (LrgQuestInstance *self);
```

Gets overall quest progress (0.0 to 1.0). Calculated from current stage and objective progress.

**Parameters:**
- `self` - An `LrgQuestInstance`

**Returns:** Progress fraction

## Example

```c
/* Create quest instance from definition */
g_autoptr(LrgQuestDef) def = load_quest_definition("slay_dragon");
g_autoptr(LrgQuestInstance) quest = lrg_quest_instance_new(def);

/* In game loop - track player actions */
if (player_killed_enemy("dragon_boss")) {
    lrg_quest_instance_update_progress(
        quest,
        LRG_QUEST_OBJECTIVE_TYPE_KILL,
        "dragon_boss",
        1
    );
}

if (player_collected_item("dragon_scales")) {
    lrg_quest_instance_update_progress(
        quest,
        LRG_QUEST_OBJECTIVE_TYPE_COLLECT,
        "dragon_scales",
        1
    );
}

/* Check current objective */
LrgQuestObjective *obj = lrg_quest_instance_get_current_objective(quest);
if (obj) {
    g_print("Current objective: %s (%.1f%%)\n",
            lrg_quest_objective_get_description(obj),
            lrg_quest_objective_get_progress(obj) * 100.0);
}

/* Check overall progress */
g_print("Quest progress: %.1f%%\n",
        lrg_quest_instance_get_progress(quest) * 100.0);

/* Check completion */
if (lrg_quest_instance_is_complete(quest)) {
    lrg_quest_instance_complete(quest);
    g_print("Quest complete!\n");
    apply_rewards(quest);
}

/* Or failure */
if (player_gave_up) {
    lrg_quest_instance_fail(quest);
}
```
