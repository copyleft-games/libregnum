# Save Module

The Save module provides a complete save/load system with support for multiple save slots, asynchronous operations, and custom object serialization. Games implement the `LrgSaveable` interface to participate in saves.

## Overview

The Save module consists of five core classes:

- **LrgSaveManager** - Singleton manager coordinating all save/load operations
- **LrgSaveGame** - Metadata for a single save slot
- **LrgSaveContext** - Serialization context for reading/writing YAML data
- **LrgSaveable** - Interface for objects that can be saved/loaded
- Save slots tracked by name (e.g., "slot1", "autosave")

## Key Features

- **Multiple save slots** - Support multiple named save files
- **Saveable interface** - Objects opt-in to serialization
- **Context-based serialization** - YAML format for human-readable saves
- **Asynchronous saves** - Non-blocking save/load with libdex futures
- **Version tracking** - Track save format version for compatibility
- **Metadata storage** - Timestamp, playtime, custom data per save
- **Section management** - Organize data by saveable object ID
- **Signal notifications** - Events for save/load start and completion

## Quick Start

```c
/* Implement LrgSaveable interface */
struct _MyGameState {
    GObject parent;
    gint gold;
    gint level;
    gchar *player_name;
};

static const gchar *
my_game_state_get_save_id(LrgSaveable *self) {
    return "game_state";
}

static gboolean
my_game_state_save(LrgSaveable *self, LrgSaveContext *ctx, GError **error) {
    MyGameState *state = MY_GAME_STATE(self);
    lrg_save_context_begin_section(ctx, "game_state");
    lrg_save_context_write_int(ctx, "gold", state->gold);
    lrg_save_context_write_int(ctx, "level", state->level);
    lrg_save_context_write_string(ctx, "player_name", state->player_name);
    lrg_save_context_end_section(ctx);
    return TRUE;
}

static gboolean
my_game_state_load(LrgSaveable *self, LrgSaveContext *ctx, GError **error) {
    MyGameState *state = MY_GAME_STATE(self);
    if (!lrg_save_context_enter_section(ctx, "game_state")) {
        return FALSE;
    }
    state->gold = lrg_save_context_read_int(ctx, "gold", 0);
    state->level = lrg_save_context_read_int(ctx, "level", 1);
    g_free(state->player_name);
    state->player_name = g_strdup(
        lrg_save_context_read_string(ctx, "player_name", "Hero")
    );
    lrg_save_context_leave_section(ctx);
    return TRUE;
}

/* Use the save system */
LrgSaveManager *manager = lrg_save_manager_get_default();
lrg_save_manager_set_save_directory(manager, "~/.local/share/mygame/saves");

/* Register objects */
lrg_save_manager_register(manager, LRG_SAVEABLE(game_state));
lrg_save_manager_register(manager, LRG_SAVEABLE(player_inventory));
lrg_save_manager_register(manager, LRG_SAVEABLE(quest_log));

/* Save to a slot */
if (lrg_save_manager_save(manager, "slot1", NULL)) {
    g_print("Game saved\n");
}

/* Load from a slot */
if (lrg_save_manager_load(manager, "slot1", NULL)) {
    g_print("Game loaded\n");
}
```

## Asynchronous Operations

For non-blocking save/load:

```c
/* Save asynchronously */
DexFuture *future = lrg_save_manager_save_async(manager, "slot1");
dex_future_then(future, on_save_complete, NULL, NULL);

/* Load asynchronously */
DexFuture *future = lrg_save_manager_load_async(manager, "slot1");
dex_future_then(future, on_load_complete, NULL, NULL);
```

## YAML Format

Saves are stored as YAML with sections for each saveable object:

```yaml
version: 1
game_state:
  gold: 1000
  level: 15
  player_name: Hero
inventory:
  items:
    - id: sword
      count: 1
    - id: potion
      count: 5
quest_log:
  active_quests:
    - slay_dragon
    - collect_herbs
  completed_quests:
    - forest_bridge
```

## API Reference

See the individual class documentation:

- [LrgSaveManager](save-manager.md) - Save/load coordinator
- [LrgSaveGame](save-game.md) - Save slot metadata
- [LrgSaveContext](save-context.md) - Serialization interface
- [LrgSaveable](saveable.md) - Interface for saveable objects
