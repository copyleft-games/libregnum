# Save/Load System Example

Complete example showing how to implement the save system with custom `LrgSaveable` objects.

## Implementing a Saveable Object

```c
#include <libregnum.h>

/* Define a game state object */
typedef struct {
    GObject parent;
    gint player_level;
    gint current_gold;
    gchar *player_name;
    gfloat playtime;
} GameState;

typedef struct {
    GObjectClass parent_class;
} GameStateClass;

G_DEFINE_TYPE_WITH_CODE(GameState, game_state, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(LRG_TYPE_SAVEABLE, game_state_saveable_init))

/* Implement LrgSaveable interface */
static const gchar *
game_state_get_save_id(LrgSaveable *self) {
    return "game_state";
}

static gboolean
game_state_save(LrgSaveable *self, LrgSaveContext *ctx, GError **error) {
    GameState *state = GAME_STATE(self);

    /* Create section for this object */
    lrg_save_context_begin_section(ctx, "game_state");

    /* Write data */
    lrg_save_context_write_int(ctx, "player_level", state->player_level);
    lrg_save_context_write_int(ctx, "current_gold", state->current_gold);
    lrg_save_context_write_string(ctx, "player_name", state->player_name);
    lrg_save_context_write_double(ctx, "playtime", state->playtime);

    lrg_save_context_end_section(ctx);
    return TRUE;
}

static gboolean
game_state_load(LrgSaveable *self, LrgSaveContext *ctx, GError **error) {
    GameState *state = GAME_STATE(self);

    /* Find and enter section for this object */
    if (!lrg_save_context_enter_section(ctx, "game_state")) {
        return FALSE;  /* Section not found */
    }

    /* Read data with defaults */
    state->player_level = lrg_save_context_read_int(ctx, "player_level", 1);
    state->current_gold = lrg_save_context_read_int(ctx, "current_gold", 0);

    g_free(state->player_name);
    state->player_name = g_strdup(
        lrg_save_context_read_string(ctx, "player_name", "Hero")
    );

    state->playtime = lrg_save_context_read_double(ctx, "playtime", 0.0);

    lrg_save_context_leave_section(ctx);
    return TRUE;
}

static void
game_state_saveable_init(LrgSaveableInterface *iface) {
    iface->get_save_id = game_state_get_save_id;
    iface->save = game_state_save;
    iface->load = game_state_load;
}

static void
game_state_init(GameState *self) {
    self->player_level = 1;
    self->current_gold = 0;
    self->player_name = g_strdup("Hero");
    self->playtime = 0.0;
}

static void
game_state_class_init(GameStateClass *klass) {
    /* Setup GObject methods */
}
```

## Implementing an Inventory Saveable

```c
typedef struct {
    gchar *item_id;
    guint count;
} InventoryItem;

typedef struct {
    GObject parent;
    GPtrArray *items;  /* Array of InventoryItem */
} Inventory;

static const gchar *
inventory_get_save_id(LrgSaveable *self) {
    return "inventory";
}

static gboolean
inventory_save(LrgSaveable *self, LrgSaveContext *ctx, GError **error) {
    Inventory *inv = INVENTORY(self);

    lrg_save_context_begin_section(ctx, "inventory");

    /* Save items array */
    lrg_save_context_write_int(ctx, "item_count", inv->items->len);

    for (guint i = 0; i < inv->items->len; i++) {
        InventoryItem *item = g_ptr_array_index(inv->items, i);

        /* Create section for each item */
        g_autofree gchar *key = g_strdup_printf("item_%u", i);
        lrg_save_context_begin_section(ctx, key);

        lrg_save_context_write_string(ctx, "id", item->item_id);
        lrg_save_context_write_uint(ctx, "count", item->count);

        lrg_save_context_end_section(ctx);
    }

    lrg_save_context_end_section(ctx);
    return TRUE;
}

static gboolean
inventory_load(LrgSaveable *self, LrgSaveContext *ctx, GError **error) {
    Inventory *inv = INVENTORY(self);

    if (!lrg_save_context_enter_section(ctx, "inventory")) {
        return FALSE;
    }

    guint item_count = lrg_save_context_read_uint(ctx, "item_count", 0);

    for (guint i = 0; i < item_count; i++) {
        g_autofree gchar *key = g_strdup_printf("item_%u", i);

        if (!lrg_save_context_enter_section(ctx, key)) {
            continue;  /* Skip missing items */
        }

        const gchar *item_id = lrg_save_context_read_string(ctx, "id", "");
        guint count = lrg_save_context_read_uint(ctx, "count", 0);

        inventory_add_item(inv, item_id, count);

        lrg_save_context_leave_section(ctx);
    }

    lrg_save_context_leave_section(ctx);
    return TRUE;
}
```

## Using the Save Manager

```c
void game_init(Game *game) {
    /* Initialize save manager */
    LrgSaveManager *save_manager = lrg_save_manager_get_default();

    /* Set save directory */
    const gchar *home = g_getenv("HOME");
    g_autofree gchar *save_dir = g_build_filename(
        home, ".local", "share", "mygame", "saves", NULL
    );
    lrg_save_manager_set_save_directory(save_manager, save_dir);

    /* Set save version */
    lrg_save_manager_set_save_version(save_manager, 1);

    /* Create game state objects */
    game->game_state = g_object_new(GAME_TYPE_STATE, NULL);
    game->inventory = g_object_new(GAME_TYPE_INVENTORY, NULL);
    game->quest_log = g_object_new(GAME_TYPE_QUEST_LOG, NULL);

    /* Register saveables */
    lrg_save_manager_register(save_manager, LRG_SAVEABLE(game->game_state));
    lrg_save_manager_register(save_manager, LRG_SAVEABLE(game->inventory));
    lrg_save_manager_register(save_manager, LRG_SAVEABLE(game->quest_log));
}
```

## Saving Game State

```c
gboolean game_save(Game *game, const gchar *slot_name) {
    LrgSaveManager *manager = lrg_save_manager_get_default();

    /* Update metadata */
    LrgSaveGame *save = lrg_save_manager_get_save(manager, slot_name);
    if (!save) {
        save = lrg_save_game_new(slot_name);
    }

    lrg_save_game_set_display_name(save, "My Save Game");
    lrg_save_game_update_timestamp(save);
    lrg_save_game_set_custom_string(save, "level_name", game->current_level);
    lrg_save_game_set_custom_int(save, "playtime_seconds",
                                  (gint64)game->total_playtime);

    /* Update playtime in game state */
    game->game_state->playtime = game->total_playtime;

    /* Synchronous save */
    g_autoptr(GError) error = NULL;
    if (!lrg_save_manager_save(manager, slot_name, &error)) {
        g_warning("Save failed: %s", error->message);
        return FALSE;
    }

    g_print("Game saved to slot: %s\n", slot_name);
    return TRUE;
}

gboolean game_save_async(Game *game, const gchar *slot_name) {
    LrgSaveManager *manager = lrg_save_manager_get_default();

    /* Asynchronous save using libdex */
    DexFuture *future = lrg_save_manager_save_async(manager, slot_name);

    dex_future_then(future,
        on_save_complete,
        game,
        NULL);

    return TRUE;
}

static void
on_save_complete(DexFuture *future, gpointer user_data) {
    Game *game = user_data;

    g_autoptr(GError) error = NULL;
    gboolean success = dex_future_get_boolean(future, &error);

    if (success) {
        g_print("Save completed successfully\n");
        show_save_notification(game);
    } else {
        g_warning("Save failed: %s", error->message);
        show_error_notification(game, "Failed to save game");
    }
}
```

## Loading Game State

```c
gboolean game_load(Game *game, const gchar *slot_name) {
    LrgSaveManager *manager = lrg_save_manager_get_default();

    /* Check if save exists */
    if (!lrg_save_manager_slot_exists(manager, slot_name)) {
        g_warning("Save slot does not exist: %s", slot_name);
        return FALSE;
    }

    /* Synchronous load */
    g_autoptr(GError) error = NULL;
    if (!lrg_save_manager_load(manager, slot_name, &error)) {
        g_warning("Load failed: %s", error->message);
        return FALSE;
    }

    /* Access loaded metadata */
    LrgSaveGame *save = lrg_save_manager_get_save(manager, slot_name);
    if (save) {
        const gchar *level = lrg_save_game_get_custom_string(save, "level_name");
        gint64 playtime = lrg_save_game_get_custom_int(save, "playtime_seconds", 0);

        g_print("Loaded: %s\n", lrg_save_game_get_display_name(save));
        g_print("Level: %s\n", level);
        g_print("Playtime: %ld seconds\n", playtime);
    }

    g_print("Game loaded from slot: %s\n", slot_name);
    return TRUE;
}

gboolean game_load_async(Game *game, const gchar *slot_name) {
    LrgSaveManager *manager = lrg_save_manager_get_default();

    DexFuture *future = lrg_save_manager_load_async(manager, slot_name);

    dex_future_then(future,
        on_load_complete,
        game,
        NULL);

    return TRUE;
}

static void
on_load_complete(DexFuture *future, gpointer user_data) {
    Game *game = user_data;

    g_autoptr(GError) error = NULL;
    gboolean success = dex_future_get_boolean(future, &error);

    if (success) {
        g_print("Load completed successfully\n");
        game_enter_loaded_state(game);
    } else {
        g_warning("Load failed: %s", error->message);
        show_error_notification(game, "Failed to load game");
    }
}
```

## Managing Save Slots

```c
void display_save_slots(LrgSaveManager *manager) {
    GList *saves = lrg_save_manager_list_saves(manager);

    g_print("Available saves:\n");
    for (GList *l = saves; l; l = l->next) {
        LrgSaveGame *save = l->data;

        const gchar *name = lrg_save_game_get_display_name(save);
        const gchar *slot = lrg_save_game_get_slot_name(save);
        GDateTime *timestamp = lrg_save_game_get_timestamp(save);
        gdouble playtime = lrg_save_game_get_playtime(save);

        g_autofree gchar *time_str = g_date_time_format(timestamp, "%Y-%m-%d %H:%M");

        g_print("  [%s] %s\n", slot, name);
        g_print("    Saved: %s\n", time_str);
        g_print("    Playtime: %.0f hours\n", playtime / 3600.0);
    }

    g_list_free_full(saves, g_object_unref);
}

gboolean delete_save_slot(LrgSaveManager *manager, const gchar *slot_name) {
    g_autoptr(GError) error = NULL;

    if (!lrg_save_manager_delete_save(manager, slot_name, &error)) {
        g_warning("Failed to delete save: %s", error->message);
        return FALSE;
    }

    g_print("Save slot deleted: %s\n", slot_name);
    return TRUE;
}
```

## YAML Output Example

The save file looks like this:

```yaml
version: 1
game_state:
  player_level: 15
  current_gold: 5000
  player_name: Hero
  playtime: 3600.5
inventory:
  item_count: 3
  item_0:
    id: sword_of_flames
    count: 1
  item_1:
    id: healing_potion
    count: 5
  item_2:
    id: iron_ore
    count: 12
quest_log:
  active_quests:
    - slay_dragon
    - collect_herbs
  completed_quests:
    - forest_bridge
```

## See Also

- [Dialog System Example](dialog-system.md) - For dialogue state saving
- [Quest Tracking Example](quest-tracking.md) - For quest progress saving
