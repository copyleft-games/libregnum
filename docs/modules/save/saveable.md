# LrgSaveable - Saveable Interface

Interface that objects implement to participate in the save/load system. Any object that needs to persist state should implement this interface and be registered with `LrgSaveManager`.

## Type

- **Interface** - Implemented by objects that can be saved
- **Type ID** - `LRG_TYPE_SAVEABLE`
- **GIR Name** - `Libregnum.Saveable`

## Interface Methods

### get_save_id()
```c
const gchar * (*get_save_id) (LrgSaveable *self);
```

Returns a unique identifier for this saveable object. This ID is used to organize saved data into sections and match data to objects during loading. Should be stable and unique across all saveable objects.

**Returns:** (transfer none) The save ID string

### save()
```c
gboolean (*save) (LrgSaveable      *self,
                  LrgSaveContext   *context,
                  GError          **error);
```

Serializes the object's state to the save context. Should use the context's write methods to store all relevant state.

**Parameters:**
- `self` - The saveable object
- `context` - The context to save to (in save mode)
- `error` - (optional) Return location for error

**Returns:** `TRUE` on success, `FALSE` with @error set on failure

### load()
```c
gboolean (*load) (LrgSaveable      *self,
                  LrgSaveContext   *context,
                  GError          **error);
```

Deserializes the object's state from the save context. Should restore the object to the exact state it was in when saved using the context's read methods.

**Parameters:**
- `self` - The saveable object
- `context` - The context to load from (in load mode)
- `error` - (optional) Return location for error

**Returns:** `TRUE` on success, `FALSE` with @error set on failure

## Public Functions

### lrg_saveable_get_save_id()
```c
const gchar * lrg_saveable_get_save_id (LrgSaveable *self);
```

Gets the unique save identifier for this object.

**Parameters:**
- `self` - A `LrgSaveable`

**Returns:** (transfer none) The save ID string

### lrg_saveable_save()
```c
gboolean lrg_saveable_save (LrgSaveable      *self,
                            LrgSaveContext   *context,
                            GError          **error);
```

Saves the object's state to the save context.

**Parameters:**
- `self` - A `LrgSaveable`
- `context` - The context to save to
- `error` - (optional) Return location for error

**Returns:** `TRUE` on success, `FALSE` with @error set on failure

### lrg_saveable_load()
```c
gboolean lrg_saveable_load (LrgSaveable      *self,
                            LrgSaveContext   *context,
                            GError          **error);
```

Loads the object's state from the save context.

**Parameters:**
- `self` - A `LrgSaveable`
- `context` - The context to load from
- `error` - (optional) Return location for error

**Returns:** `TRUE` on success, `FALSE` with @error set on failure

## Implementation Example

```c
#include <libregnum.h>

typedef struct {
    GObject parent;
    gint level;
    gint experience;
    gchar *character_class;
} PlayerData;

typedef struct {
    GObjectClass parent_class;
} PlayerDataClass;

/* Implement GObject */
G_DEFINE_TYPE_WITH_CODE(PlayerData, player_data, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(LRG_TYPE_SAVEABLE, player_saveable_init))

/* Implement LrgSaveable::get_save_id */
static const gchar *
player_get_save_id(LrgSaveable *self) {
    return "player";  /* Unique ID for this object */
}

/* Implement LrgSaveable::save */
static gboolean
player_save(LrgSaveable *self, LrgSaveContext *ctx, GError **error) {
    PlayerData *player = PLAYER_DATA(self);

    /* Create a section for this object's data */
    lrg_save_context_begin_section(ctx, "player");

    /* Write fields */
    lrg_save_context_write_int(ctx, "level", player->level);
    lrg_save_context_write_int(ctx, "experience", player->experience);
    lrg_save_context_write_string(ctx, "character_class",
                                   player->character_class);

    lrg_save_context_end_section(ctx);

    return TRUE;
}

/* Implement LrgSaveable::load */
static gboolean
player_load(LrgSaveable *self, LrgSaveContext *ctx, GError **error) {
    PlayerData *player = PLAYER_DATA(self);

    /* Enter the section for this object's data */
    if (!lrg_save_context_enter_section(ctx, "player")) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                    "Player section not found in save data");
        return FALSE;
    }

    /* Read fields with defaults */
    player->level = lrg_save_context_read_int(ctx, "level", 1);
    player->experience = lrg_save_context_read_int(ctx, "experience", 0);

    g_free(player->character_class);
    player->character_class = g_strdup(
        lrg_save_context_read_string(ctx, "character_class", "Warrior")
    );

    lrg_save_context_leave_section(ctx);

    return TRUE;
}

/* Setup interface */
static void
player_saveable_init(LrgSaveableInterface *iface) {
    iface->get_save_id = player_get_save_id;
    iface->save = player_save;
    iface->load = player_load;
}

/* GObject init */
static void
player_data_init(PlayerData *self) {
    self->level = 1;
    self->experience = 0;
    self->character_class = g_strdup("Warrior");
}

static void
player_data_finalize(GObject *obj) {
    PlayerData *player = PLAYER_DATA(obj);
    g_free(player->character_class);
    G_OBJECT_CLASS(player_data_parent_class)->finalize(obj);
}

static void
player_data_class_init(PlayerDataClass *klass) {
    GObjectClass *obj_class = G_OBJECT_CLASS(klass);
    obj_class->finalize = player_data_finalize;
}
```

## Registration and Usage

```c
/* Create object */
PlayerData *player = g_object_new(PLAYER_TYPE_DATA, NULL);

/* Register with save manager */
LrgSaveManager *manager = lrg_save_manager_get_default();
lrg_save_manager_register(manager, LRG_SAVEABLE(player));

/* Now when save/load happens, player will be included */
lrg_save_manager_save(manager, "slot1", NULL);

/* When loading, player state is restored */
lrg_save_manager_load(manager, "slot1", NULL);
```

## Best Practices

1. **Stable IDs** - Save IDs should not change between game versions
2. **Default Values** - Provide reasonable defaults when reading missing data
3. **Error Handling** - Use GError properly to report issues
4. **Sections** - Use sections to organize data logically
5. **Versioning** - Track save version in context for compatibility
6. **Cleanup** - Properly free allocated data in GObject finalize
7. **Validation** - Validate loaded data for consistency
