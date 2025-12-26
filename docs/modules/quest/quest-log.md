# LrgQuestLog - Quest Journal Manager

Container for managing active, completed, and failed quests. Tracks quest instances and supports quest tracking for HUD display.

## Type

- **Final GObject Class** - Cannot be subclassed
- **Type ID** - `LRG_TYPE_QUEST_LOG`
- **GIR Name** - `Libregnum.QuestLog`

## Construction

### lrg_quest_log_new()
```c
LrgQuestLog * lrg_quest_log_new (void);
```

Creates a new quest log.

**Parameters:** None

**Returns:** A new `LrgQuestLog` (transfer full)

## Quest Management

### lrg_quest_log_start_quest()
```c
LrgQuestInstance * lrg_quest_log_start_quest (LrgQuestLog *self,
                                              LrgQuestDef *quest_def);
```

Starts a new quest from a definition. Creates an instance and adds it to the active quests.

**Parameters:**
- `self` - An `LrgQuestLog`
- `quest_def` - Quest definition to start

**Returns:** The quest instance (transfer none, nullable) - `NULL` on failure

### lrg_quest_log_get_quest()
```c
LrgQuestInstance * lrg_quest_log_get_quest (LrgQuestLog *self,
                                            const gchar *quest_id);
```

Gets a quest instance by its definition ID.

**Parameters:**
- `self` - An `LrgQuestLog`
- `quest_id` - Quest definition ID

**Returns:** The quest instance (transfer none, nullable)

## Quest Lists

### lrg_quest_log_get_active_quests()
```c
GList * lrg_quest_log_get_active_quests (LrgQuestLog *self);
```

Gets all active quests.

**Parameters:**
- `self` - An `LrgQuestLog`

**Returns:** List of active quest instances (element-type, transfer container)

### lrg_quest_log_get_completed_quests()
```c
GList * lrg_quest_log_get_completed_quests (LrgQuestLog *self);
```

Gets all completed quests.

**Parameters:**
- `self` - An `LrgQuestLog`

**Returns:** List of completed quest instances (element-type, transfer container)

## Quest Status Checks

### lrg_quest_log_is_quest_completed()
```c
gboolean lrg_quest_log_is_quest_completed (LrgQuestLog *self,
                                           const gchar *quest_id);
```

Checks if a quest has been completed.

**Parameters:**
- `self` - An `LrgQuestLog`
- `quest_id` - Quest definition ID

**Returns:** `TRUE` if completed

### lrg_quest_log_is_quest_active()
```c
gboolean lrg_quest_log_is_quest_active (LrgQuestLog *self,
                                        const gchar *quest_id);
```

Checks if a quest is currently active.

**Parameters:**
- `self` - An `LrgQuestLog`
- `quest_id` - Quest definition ID

**Returns:** `TRUE` if active

## Quest Tracking (HUD Display)

### lrg_quest_log_get_tracked_quest()
```c
LrgQuestInstance * lrg_quest_log_get_tracked_quest (LrgQuestLog *self);
```

Gets the currently tracked quest for HUD display.

**Parameters:**
- `self` - An `LrgQuestLog`

**Returns:** The tracked quest (transfer none, nullable)

### lrg_quest_log_set_tracked_quest()
```c
void lrg_quest_log_set_tracked_quest (LrgQuestLog      *self,
                                      LrgQuestInstance *quest);
```

Sets the currently tracked quest.

**Parameters:**
- `self` - An `LrgQuestLog`
- `quest` - Quest to track (nullable)

### lrg_quest_log_track_quest()
```c
gboolean lrg_quest_log_track_quest (LrgQuestLog *self,
                                    const gchar *quest_id);
```

Tracks a quest by its ID.

**Parameters:**
- `self` - An `LrgQuestLog`
- `quest_id` - Quest definition ID to track

**Returns:** `TRUE` if quest was found and tracked

## Quest Abandonment

### lrg_quest_log_abandon_quest()
```c
gboolean lrg_quest_log_abandon_quest (LrgQuestLog *self,
                                      const gchar *quest_id);
```

Abandons an active quest.

**Parameters:**
- `self` - An `LrgQuestLog`
- `quest_id` - Quest definition ID

**Returns:** `TRUE` if quest was abandoned

## Counts

### lrg_quest_log_get_active_count()
```c
guint lrg_quest_log_get_active_count (LrgQuestLog *self);
```

Gets the number of active quests.

**Parameters:**
- `self` - An `LrgQuestLog`

**Returns:** Active quest count

### lrg_quest_log_get_completed_count()
```c
guint lrg_quest_log_get_completed_count (LrgQuestLog *self);
```

Gets the number of completed quests.

**Parameters:**
- `self` - An `LrgQuestLog`

**Returns:** Completed quest count

## Example

```c
/* Create quest log */
g_autoptr(LrgQuestLog) journal = lrg_quest_log_new();

/* NPC offers a quest */
g_autoptr(LrgQuestDef) dragon_quest = load_quest_def("slay_dragon");
LrgQuestInstance *instance = lrg_quest_log_start_quest(journal, dragon_quest);
if (instance) {
    g_print("Quest accepted: %s\n", lrg_quest_def_get_name(dragon_quest));
}

/* Player tracks this quest for HUD */
lrg_quest_log_track_quest(journal, "slay_dragon");

/* Later, display tracked quest on HUD */
LrgQuestInstance *tracked = lrg_quest_log_get_tracked_quest(journal);
if (tracked) {
    LrgQuestObjective *obj = lrg_quest_instance_get_current_objective(tracked);
    if (obj) {
        g_print("Objective: %s\n", lrg_quest_objective_get_description(obj));
    }
}

/* Check if quest is active */
if (lrg_quest_log_is_quest_active(journal, "collect_herbs")) {
    g_print("Herb gathering is still in progress\n");
}

/* Player completes a quest */
LrgQuestInstance *completed_quest = lrg_quest_log_get_quest(journal, "collect_herbs");
if (completed_quest) {
    lrg_quest_instance_complete(completed_quest);

    if (lrg_quest_log_is_quest_completed(journal, "collect_herbs")) {
        g_print("Herb quest finished!\n");
    }
}

/* View all active quests */
GList *active = lrg_quest_log_get_active_quests(journal);
g_print("Active quests (%d):\n", lrg_quest_log_get_active_count(journal));
for (GList *l = active; l != NULL; l = l->next) {
    LrgQuestInstance *q = l->data;
    LrgQuestDef *def = lrg_quest_instance_get_quest_def(q);
    g_print("  - %s\n", lrg_quest_def_get_name(def));
}
g_list_free(active);

/* View all completed quests */
GList *completed = lrg_quest_log_get_completed_quests(journal);
g_print("Completed quests (%d):\n", lrg_quest_log_get_completed_count(journal));
for (GList *l = completed; l != NULL; l = l->next) {
    LrgQuestInstance *q = l->data;
    LrgQuestDef *def = lrg_quest_instance_get_quest_def(q);
    g_print("  - %s\n", lrg_quest_def_get_name(def));
}
g_list_free(completed);

/* Player gives up */
lrg_quest_log_abandon_quest(journal, "boring_fetch_quest");
```
