# LrgQuestObjective - Quest Objective/Stage

A single objective within a quest. Represents a goal like killing enemies, collecting items, or reaching locations. Tracks current and target counts for progress.

## Type

- **Boxed Type** - Reference-counted value type
- **Type ID** - `LRG_TYPE_QUEST_OBJECTIVE`
- **GIR Name** - `Libregnum.QuestObjective`

## Objective Types

- `LRG_QUEST_OBJECTIVE_TYPE_KILL` - Kill a number of enemies
- `LRG_QUEST_OBJECTIVE_TYPE_COLLECT` - Collect a number of items
- `LRG_QUEST_OBJECTIVE_TYPE_REACH` - Reach a location
- `LRG_QUEST_OBJECTIVE_TYPE_TALK` - Talk to an NPC
- `LRG_QUEST_OBJECTIVE_TYPE_DELIVER` - Deliver an item
- Custom types can be added by games

## Construction

### lrg_quest_objective_new()
```c
LrgQuestObjective * lrg_quest_objective_new (const gchar           *id,
                                             const gchar           *description,
                                             LrgQuestObjectiveType  type);
```

Creates a new quest objective.

**Parameters:**
- `id` - Unique identifier for the objective
- `description` - Human-readable description
- `type` - Objective type

**Returns:** A new `LrgQuestObjective` (transfer full)

### lrg_quest_objective_copy()
```c
LrgQuestObjective * lrg_quest_objective_copy (const LrgQuestObjective *self);
```

Creates a deep copy of the objective.

**Parameters:**
- `self` - An `LrgQuestObjective`

**Returns:** A copy of @self (transfer full)

### lrg_quest_objective_free()
```c
void lrg_quest_objective_free (LrgQuestObjective *self);
```

Frees the objective.

**Parameters:**
- `self` - An `LrgQuestObjective`

## Identification

### lrg_quest_objective_get_id()
```c
const gchar * lrg_quest_objective_get_id (const LrgQuestObjective *self);
```

Gets the objective ID.

**Parameters:**
- `self` - An `LrgQuestObjective`

**Returns:** The objective ID (transfer none)

## Description

### lrg_quest_objective_get_description()
```c
const gchar * lrg_quest_objective_get_description (const LrgQuestObjective *self);
```

Gets the objective description.

**Parameters:**
- `self` - An `LrgQuestObjective`

**Returns:** The description (transfer none)

### lrg_quest_objective_set_description()
```c
void lrg_quest_objective_set_description (LrgQuestObjective *self,
                                          const gchar       *description);
```

Sets the objective description.

**Parameters:**
- `self` - An `LrgQuestObjective`
- `description` - New description

## Type

### lrg_quest_objective_get_objective_type()
```c
LrgQuestObjectiveType lrg_quest_objective_get_objective_type (const LrgQuestObjective *self);
```

Gets the objective type.

**Parameters:**
- `self` - An `LrgQuestObjective`

**Returns:** The objective type

## Target

### lrg_quest_objective_get_target_id()
```c
const gchar * lrg_quest_objective_get_target_id (const LrgQuestObjective *self);
```

Gets the target entity/item ID.

**Parameters:**
- `self` - An `LrgQuestObjective`

**Returns:** The target ID (transfer none, nullable)

### lrg_quest_objective_set_target_id()
```c
void lrg_quest_objective_set_target_id (LrgQuestObjective *self,
                                        const gchar       *target_id);
```

Sets the target ID.

**Parameters:**
- `self` - An `LrgQuestObjective`
- `target_id` - Target entity/item ID (nullable)

## Progress Tracking

### lrg_quest_objective_get_target_count()
```c
guint lrg_quest_objective_get_target_count (const LrgQuestObjective *self);
```

Gets the required count to complete the objective.

**Parameters:**
- `self` - An `LrgQuestObjective`

**Returns:** The target count

### lrg_quest_objective_set_target_count()
```c
void lrg_quest_objective_set_target_count (LrgQuestObjective *self,
                                           guint              count);
```

Sets the target count.

**Parameters:**
- `self` - An `LrgQuestObjective`
- `count` - Required count

### lrg_quest_objective_get_current_count()
```c
guint lrg_quest_objective_get_current_count (const LrgQuestObjective *self);
```

Gets the current progress count.

**Parameters:**
- `self` - An `LrgQuestObjective`

**Returns:** The current count

### lrg_quest_objective_set_current_count()
```c
void lrg_quest_objective_set_current_count (LrgQuestObjective *self,
                                            guint              count);
```

Sets the current progress count.

**Parameters:**
- `self` - An `LrgQuestObjective`
- `count` - Current count

### lrg_quest_objective_increment()
```c
guint lrg_quest_objective_increment (LrgQuestObjective *self,
                                     guint              amount);
```

Increments the current count.

**Parameters:**
- `self` - An `LrgQuestObjective`
- `amount` - Amount to add

**Returns:** The new current count

## Location (for REACH objectives)

### lrg_quest_objective_get_location()
```c
const gchar * lrg_quest_objective_get_location (const LrgQuestObjective *self);
```

Gets the target location for REACH objectives.

**Parameters:**
- `self` - An `LrgQuestObjective`

**Returns:** The location ID (transfer none, nullable)

### lrg_quest_objective_set_location()
```c
void lrg_quest_objective_set_location (LrgQuestObjective *self,
                                       const gchar       *location);
```

Sets the target location.

**Parameters:**
- `self` - An `LrgQuestObjective`
- `location` - Location ID (nullable)

## Completion

### lrg_quest_objective_is_complete()
```c
gboolean lrg_quest_objective_is_complete (const LrgQuestObjective *self);
```

Checks if the objective is complete. Automatically true when current >= target.

**Parameters:**
- `self` - An `LrgQuestObjective`

**Returns:** `TRUE` if complete

### lrg_quest_objective_set_complete()
```c
void lrg_quest_objective_set_complete (LrgQuestObjective *self,
                                       gboolean           complete);
```

Manually sets the completion state.

**Parameters:**
- `self` - An `LrgQuestObjective`
- `complete` - Completion state

### lrg_quest_objective_get_progress()
```c
gdouble lrg_quest_objective_get_progress (const LrgQuestObjective *self);
```

Gets the completion percentage (0.0 to 1.0).

**Parameters:**
- `self` - An `LrgQuestObjective`

**Returns:** Progress as a fraction

## Memory Management

Objectives use automatic pointer cleanup:

```c
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgQuestObjective, lrg_quest_objective_free)
```

## Example

```c
/* Create a collect objective */
g_autoptr(LrgQuestObjective) collect = lrg_quest_objective_new(
    "collect_mushrooms",
    "Find red mushrooms in the forest",
    LRG_QUEST_OBJECTIVE_TYPE_COLLECT
);

lrg_quest_objective_set_target_id(collect, "red_mushroom");
lrg_quest_objective_set_target_count(collect, 10);

/* Track progress */
lrg_quest_objective_increment(collect, 3);  /* Player found 3 mushrooms */
lrg_quest_objective_increment(collect, 2);  /* Found 2 more */

g_print("Progress: %u/%u\n",
        lrg_quest_objective_get_current_count(collect),
        lrg_quest_objective_get_target_count(collect));

g_print("Percentage: %.1f%%\n",
        lrg_quest_objective_get_progress(collect) * 100.0);

if (lrg_quest_objective_is_complete(collect)) {
    g_print("Objective complete!\n");
}

/* Kill objective */
g_autoptr(LrgQuestObjective) kill = lrg_quest_objective_new(
    "kill_goblins",
    "Eliminate goblin invaders",
    LRG_QUEST_OBJECTIVE_TYPE_KILL
);
lrg_quest_objective_set_target_id(kill, "goblin_warrior");
lrg_quest_objective_set_target_count(kill, 5);

/* Reach objective */
g_autoptr(LrgQuestObjective) reach = lrg_quest_objective_new(
    "reach_temple",
    "Get to the ancient temple",
    LRG_QUEST_OBJECTIVE_TYPE_REACH
);
lrg_quest_objective_set_location(reach, "temple_entrance");
```
