# How to Implement the Saveable Interface

This guide explains how to implement the `LrgSaveable` interface to enable saving and loading of game objects.

## Saveable Interface Overview

The `LrgSaveable` interface enables objects to:
- Serialize their state to save files
- Deserialize and restore from saved data
- Handle version compatibility
- Track save state and dirty flags

Any game object that needs to be persisted should implement this interface.

## Implementing Saveable

### Step 1: Add Interface to Class Definition

```c
/* my-character.h */
#pragma once

#include <libregnum.h>

G_BEGIN_DECLS

#define MY_TYPE_CHARACTER (my_character_get_type())

G_DECLARE_FINAL_TYPE(MyCharacter,
                     my_character,
                     MY,
                     CHARACTER,
                     GObject)

/* Implement Saveable interface */
G_IMPLEMENT_INTERFACE(LRG_TYPE_SAVEABLE, my_character_saveable_init)

MyCharacter *
my_character_new(const gchar *name);

const gchar *
my_character_get_name(MyCharacter *self);

guint
my_character_get_level(MyCharacter *self);

guint
my_character_get_experience(MyCharacter *self);

G_END_DECLS
```

### Step 2: Implement Interface Methods

```c
/* my-character.c */
#include "my-character.h"

#define SAVE_VERSION 2

struct _MyCharacter
{
    GObject  parent_instance;
    gchar   *name;
    guint    level;
    guint    experience;
    guint    health;
    guint    max_health;
};

G_DEFINE_TYPE_WITH_CODE(MyCharacter,
                        my_character,
                        G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(LRG_TYPE_SAVEABLE,
                                            my_character_saveable_init))

/* Saveable interface implementation */

static gboolean
my_character_save(LrgSaveable    *saveable,
                  LrgSaveContext *ctx,
                  GError        **error)
{
    MyCharacter *self = MY_CHARACTER(saveable);

    g_debug("Saving character: %s", self->name);

    /* Save version (for compatibility) */
    if (!lrg_save_context_write_uint(ctx, "version", SAVE_VERSION, error))
        return FALSE;

    /* Save basic data */
    if (!lrg_save_context_write_string(ctx, "name", self->name, error))
        return FALSE;

    if (!lrg_save_context_write_uint(ctx, "level", self->level, error))
        return FALSE;

    if (!lrg_save_context_write_uint(ctx, "experience", self->experience, error))
        return FALSE;

    /* Save stats */
    if (!lrg_save_context_write_uint(ctx, "health", self->health, error))
        return FALSE;

    if (!lrg_save_context_write_uint(ctx, "max_health", self->max_health, error))
        return FALSE;

    return TRUE;
}

static gboolean
my_character_load(LrgSaveable    *saveable,
                  LrgLoadContext *ctx,
                  GError        **error)
{
    MyCharacter *self = MY_CHARACTER(saveable);
    guint version;
    g_autofree gchar *name = NULL;
    guint level, experience, health, max_health;

    g_debug("Loading character");

    /* Load and check version */
    if (!lrg_load_context_read_uint(ctx, "version", &version, error))
        return FALSE;

    if (version != SAVE_VERSION) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "Save version mismatch: got %u, expected %u",
                   version, SAVE_VERSION);
        return FALSE;
    }

    /* Load basic data */
    if (!lrg_load_context_read_string(ctx, "name", &name, error))
        return FALSE;

    if (!lrg_load_context_read_uint(ctx, "level", &level, error))
        return FALSE;

    if (!lrg_load_context_read_uint(ctx, "experience", &experience, error))
        return FALSE;

    /* Load stats */
    if (!lrg_load_context_read_uint(ctx, "health", &health, error))
        return FALSE;

    if (!lrg_load_context_read_uint(ctx, "max_health", &max_health, error))
        return FALSE;

    /* Apply loaded data */
    g_free(self->name);
    self->name = g_steal_pointer(&name);
    self->level = level;
    self->experience = experience;
    self->health = health;
    self->max_health = max_health;

    return TRUE;
}

static void
my_character_saveable_init(LrgSaveableInterface *iface)
{
    iface->save = my_character_save;
    iface->load = my_character_load;
}

/* Class and instance initialization */

static void
my_character_class_init(MyCharacterClass *klass)
{
    /* ... other initialization ... */
}

static void
my_character_init(MyCharacter *self)
{
    self->name = g_strdup("Unnamed");
    self->level = 1;
    self->experience = 0;
    self->health = 100;
    self->max_health = 100;
}

MyCharacter *
my_character_new(const gchar *name)
{
    MyCharacter *self = g_object_new(MY_TYPE_CHARACTER, NULL);
    self->name = g_strdup(name);
    return self;
}

const gchar *
my_character_get_name(MyCharacter *self)
{
    g_return_val_if_fail(MY_IS_CHARACTER(self), NULL);
    return self->name;
}

guint
my_character_get_level(MyCharacter *self)
{
    g_return_val_if_fail(MY_IS_CHARACTER(self), 0);
    return self->level;
}

guint
my_character_get_experience(MyCharacter *self)
{
    g_return_val_if_fail(MY_IS_CHARACTER(self), 0);
    return self->experience;
}
```

## Handling Complex Types

For complex data, use nested save contexts:

```c
/* Save inventory as sub-object */

static gboolean
my_character_save_inventory(MyCharacter    *self,
                            LrgSaveContext *ctx,
                            GError        **error)
{
    g_autoptr(LrgSaveContext) inv_ctx = NULL;

    inv_ctx = lrg_save_context_create_child(ctx, "inventory", error);
    if (inv_ctx == NULL)
        return FALSE;

    /* Save each item slot */
    for (guint i = 0; i < lrg_inventory_get_capacity(self->inventory); i++) {
        LrgItemStack *stack = lrg_inventory_get_slot(self->inventory, i);

        if (stack == NULL)
            continue;

        g_autofree gchar *slot_key = g_strdup_printf("slot_%u", i);
        g_autoptr(LrgSaveContext) slot_ctx = NULL;

        slot_ctx = lrg_save_context_create_child(inv_ctx, slot_key, error);
        if (slot_ctx == NULL)
            return FALSE;

        /* Save item definition ID and quantity */
        LrgItemDef *def = lrg_item_stack_get_def(stack);
        if (!lrg_save_context_write_string(slot_ctx,
                                          "item_id",
                                          lrg_item_def_get_id(def),
                                          error)) {
            return FALSE;
        }

        if (!lrg_save_context_write_uint(slot_ctx,
                                        "quantity",
                                        lrg_item_stack_get_quantity(stack),
                                        error)) {
            return FALSE;
        }

        /* Save instance data (durability, etc.) */
        /* ... save any additional stack data ... */
    }

    return TRUE;
}

/* Load inventory from save file */

static gboolean
my_character_load_inventory(MyCharacter    *self,
                            LrgLoadContext *ctx,
                            GError        **error)
{
    g_autoptr(LrgLoadContext) inv_ctx = NULL;
    g_autoptr(GPtrArray) slot_keys = NULL;

    inv_ctx = lrg_load_context_open_child(ctx, "inventory", error);
    if (inv_ctx == NULL)
        return FALSE;

    /* Get all slot keys */
    slot_keys = lrg_load_context_get_keys(inv_ctx);

    for (guint i = 0; i < slot_keys->len; i++) {
        const gchar *key = g_ptr_array_index(slot_keys, i);
        g_autoptr(LrgLoadContext) slot_ctx = NULL;
        g_autofree gchar *item_id = NULL;
        guint quantity;

        slot_ctx = lrg_load_context_open_child(inv_ctx, key, error);
        if (slot_ctx == NULL)
            return FALSE;

        /* Load item ID */
        if (!lrg_load_context_read_string(slot_ctx, "item_id", &item_id, error))
            return FALSE;

        /* Load quantity */
        if (!lrg_load_context_read_uint(slot_ctx, "quantity", &quantity, error))
            return FALSE;

        /* Reconstruct item (need access to item definitions) */
        LrgRegistry *registry = lrg_engine_get_registry(lrg_engine_get_default());
        GType item_type = lrg_registry_lookup(registry, item_id);

        if (item_type == G_TYPE_INVALID) {
            g_warning("Item type '%s' not found in registry", item_id);
            continue;
        }

        /* Create item definition and stack */
        g_autoptr(LrgItemDef) def = g_object_new(item_type, NULL);
        g_autoptr(LrgItemStack) stack = lrg_item_stack_new(def, quantity);

        /* Add to inventory */
        lrg_inventory_add_stack(self->inventory, stack);
    }

    return TRUE;
}
```

## Version Compatibility

Handle different save file versions:

```c
static gboolean
my_character_load_v1(MyCharacter    *self,
                     LrgLoadContext *ctx,
                     GError        **error)
{
    /* Load data in v1 format */
    g_autofree gchar *name = NULL;
    guint level;

    if (!lrg_load_context_read_string(ctx, "name", &name, error))
        return FALSE;

    if (!lrg_load_context_read_uint(ctx, "level", &level, error))
        return FALSE;

    self->name = g_steal_pointer(&name);
    self->level = level;

    /* Set default for new field added in v2 */
    self->experience = 0;

    return TRUE;
}

static gboolean
my_character_load(LrgSaveable    *saveable,
                  LrgLoadContext *ctx,
                  GError        **error)
{
    MyCharacter *self = MY_CHARACTER(saveable);
    guint version;

    if (!lrg_load_context_read_uint(ctx, "version", &version, error))
        return FALSE;

    /* Handle different versions */
    switch (version) {
    case 1:
        return my_character_load_v1(self, ctx, error);

    case 2:
        /* Current version - see original implementation above */
        break;

    default:
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "Unsupported save version: %u", version);
        return FALSE;
    }

    return TRUE;
}
```

## Using Saveable in Game

```c
/* Save game */

void
save_game(void)
{
    MyCharacter *player = game_get_player();
    GError *error = NULL;

    gboolean success = lrg_saveable_save(LRG_SAVEABLE(player),
                                         "save/character.sav",
                                         &error);

    if (!success) {
        g_warning("Failed to save: %s", error->message);
        g_clear_error(&error);
    }
}

/* Load game */

void
load_game(void)
{
    MyCharacter *player = my_character_new("Hero");
    GError *error = NULL;

    gboolean success = lrg_saveable_load(LRG_SAVEABLE(player),
                                         "save/character.sav",
                                         &error);

    if (!success) {
        g_warning("Failed to load: %s", error->message);
        g_clear_error(&error);
    }

    game_set_player(player);
}
```

## Best Practices

1. **Always save version**: Allow future format changes
2. **Use descriptive keys**: Make saves human-readable where possible
3. **Validate data**: Check for invalid values during load
4. **Handle missing data**: Provide sensible defaults
5. **Document format**: Keep save file format documented
6. **Test compatibility**: Test loading old save files
7. **Use type safety**: Define enums and constants for magic values
8. **Error handling**: Always propagate errors properly

## Example: Complete Saveable Implementation

```c
#define CHARACTER_SAVE_VERSION 1

static gboolean
my_character_save(LrgSaveable    *saveable,
                  LrgSaveContext *ctx,
                  GError        **error)
{
    MyCharacter *self = MY_CHARACTER(saveable);

    /* Version */
    if (!lrg_save_context_write_uint(ctx, "version", CHARACTER_SAVE_VERSION, error))
        return FALSE;

    /* Basic info */
    if (!lrg_save_context_write_string(ctx, "name", self->name, error))
        return FALSE;

    /* Stats */
    if (!lrg_save_context_write_uint(ctx, "level", self->level, error))
        return FALSE;
    if (!lrg_save_context_write_uint(ctx, "experience", self->experience, error))
        return FALSE;
    if (!lrg_save_context_write_uint(ctx, "health", self->health, error))
        return FALSE;
    if (!lrg_save_context_write_uint(ctx, "max_health", self->max_health, error))
        return FALSE;

    return TRUE;
}

static gboolean
my_character_load(LrgSaveable    *saveable,
                  LrgLoadContext *ctx,
                  GError        **error)
{
    MyCharacter *self = MY_CHARACTER(saveable);
    guint version;
    g_autofree gchar *name = NULL;
    guint level, exp, health, max_health;

    /* Check version */
    if (!lrg_load_context_read_uint(ctx, "version", &version, error))
        return FALSE;

    if (version != CHARACTER_SAVE_VERSION) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "Incompatible save version: %u", version);
        return FALSE;
    }

    /* Load data */
    if (!lrg_load_context_read_string(ctx, "name", &name, error))
        return FALSE;

    if (!lrg_load_context_read_uint(ctx, "level", &level, error))
        return FALSE;
    if (!lrg_load_context_read_uint(ctx, "experience", &exp, error))
        return FALSE;
    if (!lrg_load_context_read_uint(ctx, "health", &health, error))
        return FALSE;
    if (!lrg_load_context_read_uint(ctx, "max_health", &max_health, error))
        return FALSE;

    /* Apply */
    g_free(self->name);
    self->name = g_steal_pointer(&name);
    self->level = level;
    self->experience = exp;
    self->health = health;
    self->max_health = max_health;

    return TRUE;
}

static void
my_character_saveable_init(LrgSaveableInterface *iface)
{
    iface->save = my_character_save;
    iface->load = my_character_load;
}
```

## See Also

- [Save System Documentation](../modules/save/index.md)
- [Data Types and Serialization](../modules/data/serialization.md)
