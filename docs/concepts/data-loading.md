---
title: Data Loading
concept: core
---

# Data Loading with YAML

The DataLoader enables fully data-driven game development by deserializing YAML files to GObjects. Combined with the Registry, it allows complete separation of data from code.

> **[Home](../../index.md)** > **[Concepts](index.md)** > Data Loading

## Why YAML?

YAML is human-readable and perfect for game data:

```yaml
# player.yaml - Human friendly, no quotes needed
name: Hero
health: 100
mana: 50
strength: 15
inventory:
  - iron-sword
  - health-potion
```

vs JSON (more verbose):

```json
{
  "name": "Hero",
  "health": 100,
  "mana": 50,
  "strength": 15,
  "inventory": ["iron-sword", "health-potion"]
}
```

## Basic Structure

A YAML file with a "type" field creates a typed object:

```yaml
type: player
name: "Hero"
health: 100
level: 1
```

The DataLoader:
1. Reads the YAML file
2. Extracts `type: player`
3. Looks up "player" in the Registry
4. Creates an object of that type
5. Sets properties from remaining fields

## Creating a DataLoader

Create and configure:

```c
LrgDataLoader *loader = lrg_data_loader_new ();

/* Optionally set the registry (optional if no typed loading) */
LrgRegistry *registry = lrg_engine_get_registry (engine);
lrg_data_loader_set_registry (loader, registry);
```

Or use the engine's loader:

```c
LrgEngine *engine = lrg_engine_get_default ();
lrg_engine_startup (engine, &error);

LrgDataLoader *loader = lrg_engine_get_data_loader (engine);
```

## Loading Single Files

### Synchronous Loading

Block until the file is loaded:

```c
g_autoptr(GError) error = NULL;

GObject *obj = lrg_data_loader_load_file (loader, "data/player.yaml", &error);

if (obj)
{
    g_print ("Loaded %s\n", G_OBJECT_TYPE_NAME (obj));
    g_object_unref (obj);
}
else if (error)
{
    g_warning ("Load failed: %s", error->message);
}
```

### From GFile

Load from a GFile object:

```c
g_autoptr(GFile) file = g_file_new_for_path ("data/player.yaml");
g_autoptr(GError) error = NULL;

GObject *obj = lrg_data_loader_load_gfile (loader, file, NULL, &error);
```

### From Data String

Load from YAML in memory:

```c
const gchar *yaml_data = "type: player\nname: Hero\n";

GObject *obj = lrg_data_loader_load_data (loader, yaml_data, -1, &error);
```

### Type-Specific Loading

Load without a "type" field:

```yaml
# enemy.yaml (no type field)
name: "Goblin"
health: 25
difficulty: 2
```

```c
/* Specify type explicitly */
GObject *obj = lrg_data_loader_load_typed (loader, GAME_TYPE_ENEMY,
                                           "data/enemy.yaml", &error);
```

This deserializes the entire file into the given type.

## Batch Loading

### Load Directory

Load all YAML files from a directory:

```c
g_autoptr(GError) error = NULL;

GList *objects = lrg_data_loader_load_directory (loader, "data/entities/",
                                                 TRUE,  /* recursive */
                                                 &error);

g_print ("Loaded %u objects\n", g_list_length (objects));

g_list_free_full (objects, g_object_unref);
```

### Load Multiple Files

Load specific files:

```c
const gchar *paths[] = {
    "data/player.yaml",
    "data/enemies.yaml",
    "data/items.yaml",
    NULL
};

GList *objects = lrg_data_loader_load_files (loader, paths, &error);
```

Files are loaded in order, continuing even if one fails (with warning).

## Asynchronous Loading

### Basic Async

Load without blocking:

```c
#include <libdex.h>

DexFuture *future = lrg_data_loader_load_file_async (loader, "data/player.yaml");

/* Use with callback */
dex_future_then (future, on_loaded, NULL, NULL);
```

### Async Callback

Handle results:

```c
static void
on_loaded (DexFuture *future,
           gpointer   user_data)
{
    g_autoptr(GError) error = NULL;
    GObject *obj = dex_future_get_value (future, (gpointer *)&error);

    if (obj)
    {
        g_print ("Loaded %s\n", G_OBJECT_TYPE_NAME (obj));
        g_object_unref (obj);
    }
    else if (error)
    {
        g_warning ("Load failed: %s", error->message);
    }
}
```

### Async in Fiber

Use with libdex fibers:

```c
static DexFuture *
load_async (gpointer user_data)
{
    LrgDataLoader *loader = (LrgDataLoader *)user_data;

    /* Blocks in fiber, doesn't block main */
    DexFuture *future = lrg_data_loader_load_file_async (loader, "data/player.yaml");

    return dex_future_await (future);
}

/* Run in fiber */
DexFuture *fiber = dex_future_new_fiber (load_async, loader);
```

## Error Handling

### Error Codes

```c
typedef enum
{
    LRG_DATA_LOADER_ERROR_FAILED,      /* Generic failure */
    LRG_DATA_LOADER_ERROR_IO,          /* File not found, permission denied */
    LRG_DATA_LOADER_ERROR_PARSE,       /* Invalid YAML syntax */
    LRG_DATA_LOADER_ERROR_TYPE,        /* Type not registered */
    LRG_DATA_LOADER_ERROR_PROPERTY     /* Property error */
} LrgDataLoaderError;
```

### Error Checking

```c
g_autoptr(GError) error = NULL;

GObject *obj = lrg_data_loader_load_file (loader, "player.yaml", &error);

if (obj)
{
    g_print ("Success\n");
}
else if (error)
{
    if (error->domain == LRG_DATA_LOADER_ERROR)
    {
        switch (error->code)
        {
        case LRG_DATA_LOADER_ERROR_IO:
            g_warning ("File not found or read error");
            break;
        case LRG_DATA_LOADER_ERROR_TYPE:
            g_warning ("Type not registered in registry");
            break;
        case LRG_DATA_LOADER_ERROR_PROPERTY:
            g_warning ("Property error: %s", error->message);
            break;
        default:
            g_warning ("Unknown error: %s", error->message);
        }
    }
}
```

## YAML File Format

### Type Field

Specifies which GType to create:

```yaml
type: player
# ... rest of properties ...
```

### Property Types

Automatic type conversion:

```yaml
type: player
name: "Hero"              # String
health: 100               # Integer
mana: 50.5                # Double
alive: true               # Boolean
skills:                   # Array/List
  - fireball
  - heal
position:                 # Object (nested)
  x: 10.5
  y: 20.3
```

### Property Mapping

YAML fields map to GObject properties:

```yaml
# YAML
type: my-object
my-property: "value"
another-property: 42

# Equivalent to:
# g_object_new (MY_TYPE,
#               "my-property", "value",
#               "another-property", 42,
#               NULL)
```

## Configuration

### Type Field Name

Change the field used for type specification:

```c
/* Default is "type" */
lrg_data_loader_set_type_field_name (loader, "type");

/* Change to custom name */
lrg_data_loader_set_type_field_name (loader, "class");
```

With "class":

```yaml
class: player          # Instead of: type: player
name: "Hero"
```

### File Extensions

Customize recognized extensions:

```c
/* Default: ".yaml", ".yml" */
const gchar *exts[] = { ".yaml", ".yml", NULL };
lrg_data_loader_set_file_extensions (loader, exts);

/* Custom extensions */
const gchar *custom_exts[] = { ".gamedata", ".data", NULL };
lrg_data_loader_set_file_extensions (loader, custom_exts);
```

## Practical Examples

### Entity Definition

```yaml
# data/entities/player.yaml
type: player
name: "Hero"
health: 100
mana: 50
strength: 15
dexterity: 12
constitution: 14
```

### Item Definition

```yaml
# data/items/sword.yaml
type: item
name: "Iron Sword"
damage: 15
durability: 100
weight: 5.0
```

### Quest Definition

```yaml
# data/quests/save-princess.yaml
type: quest-def
id: "save-princess"
title: "Save the Princess"
description: "The princess is in the tower!"
reward-gold: 1000
objectives:
  - rescue-princess
  - return-to-king
```

## Complete Example

```c
#include <libregnum.h>
#include <glib.h>

#define GAME_TYPE_PLAYER (game_player_get_type ())
G_DECLARE_FINAL_TYPE (GamePlayer, game_player, GAME, PLAYER, GObject)

struct _GamePlayer
{
    GObject parent_instance;
    gchar *name;
    gint health;
};

enum { PROP_0, PROP_NAME, PROP_HEALTH };

static void game_player_set_property (GObject *object, guint prop_id,
                                      const GValue *value, GParamSpec *pspec)
{
    GamePlayer *self = GAME_PLAYER (object);
    switch (prop_id) {
    case PROP_NAME: g_free (self->name);
                    self->name = g_value_dup_string (value); break;
    case PROP_HEALTH: self->health = g_value_get_int (value); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void game_player_get_property (GObject *object, guint prop_id,
                                      GValue *value, GParamSpec *pspec)
{
    GamePlayer *self = GAME_PLAYER (object);
    switch (prop_id) {
    case PROP_NAME: g_value_set_string (value, self->name); break;
    case PROP_HEALTH: g_value_set_int (value, self->health); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void game_player_class_init (GamePlayerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->set_property = game_player_set_property;
    object_class->get_property = game_player_get_property;

    g_object_class_install_property (object_class, PROP_NAME,
        g_param_spec_string ("name", "Name", "Player name",
                             NULL, G_PARAM_READWRITE));
    g_object_class_install_property (object_class, PROP_HEALTH,
        g_param_spec_int ("health", "Health", "Player health",
                          0, 9999, 100, G_PARAM_READWRITE));
}

static void game_player_init (GamePlayer *self) { }

G_DEFINE_TYPE (GamePlayer, game_player, G_TYPE_OBJECT)

int
main (int argc, char *argv[])
{
    g_autoptr(GError) error = NULL;
    LrgEngine *engine = lrg_engine_get_default ();

    lrg_engine_startup (engine, &error);

    LrgRegistry *registry = lrg_engine_get_registry (engine);
    lrg_registry_register (registry, "player", GAME_TYPE_PLAYER);

    LrgDataLoader *loader = lrg_engine_get_data_loader (engine);
    lrg_data_loader_set_registry (loader, registry);

    /* Create test YAML */
    const gchar *yaml = "type: player\nname: Hero\nhealth: 100\n";
    g_autoptr(GObject) obj = lrg_data_loader_load_data (loader, yaml, -1, &error);

    if (obj) {
        GamePlayer *player = GAME_PLAYER (obj);
        g_print ("Loaded player: name=%s health=%d\n", player->name, player->health);
    }

    lrg_engine_shutdown (engine);
    return 0;
}
```

## See Also

- [LrgDataLoader Documentation](../modules/core/data-loader.md) - Full API
- [Type Registry](type-registry.md) - Registry deep dive
- [Asset Management](../modules/core/asset-manager.md) - Asset loading
