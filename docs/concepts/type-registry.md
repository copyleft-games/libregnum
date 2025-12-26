---
title: Type Registry
concept: core
---

# Type Registry

The Type Registry is the foundation of Libregnum's data-driven design. It maps string names to GTypes, enabling YAML files to specify object types without hardcoding type references in your code.

> **[Home](../../index.md)** > **[Concepts](index.md)** > Type Registry

## Why a Registry?

Without a registry, type creation is tightly coupled to code:

```c
/* Bad: hardcoded type references in data */
if (strcmp (type_string, "player") == 0)
    obj = g_object_new (MY_TYPE_PLAYER, NULL);
else if (strcmp (type_string, "enemy") == 0)
    obj = g_object_new (MY_TYPE_ENEMY, NULL);
/* Dozens of if/else branches... */
```

With a registry, types are dynamically mapped:

```c
/* Good: registry decouples type references from data loading */
lrg_registry_register (registry, "player", MY_TYPE_PLAYER);
lrg_registry_register (registry, "enemy", MY_TYPE_ENEMY);

/* Create by name */
GObject *obj = lrg_registry_create (registry, "player", NULL);
```

This enables:
- **Mod overrides** - Replace "player" with a custom type
- **Data-driven design** - YAML files specify types, not code
- **Flexible extensibility** - Add new types without changing core code

## Creating a Registry

Create a new registry:

```c
LrgRegistry *registry = lrg_registry_new ();

/* ... use it ... */

g_object_unref (registry);
```

The engine provides a registry:

```c
LrgEngine *engine = lrg_engine_get_default ();
lrg_engine_startup (engine, &error);

LrgRegistry *registry = lrg_engine_get_registry (engine);
g_assert_nonnull (registry);  /* Never NULL after startup */
```

## Registering Types

Register a GType with a string name:

```c
/* Simple registration */
lrg_registry_register (registry, "player", MY_TYPE_PLAYER);

/* Multiple types */
lrg_registry_register (registry, "player", MY_TYPE_PLAYER);
lrg_registry_register (registry, "enemy", MY_TYPE_ENEMY);
lrg_registry_register (registry, "item", MY_TYPE_ITEM);
```

### Naming Convention

Use simple, lowercase names:

```c
/* Good names */
lrg_registry_register (registry, "player", MY_TYPE_PLAYER);
lrg_registry_register (registry, "enemy-goblin", MY_TYPE_ENEMY_GOBLIN);
lrg_registry_register (registry, "quest-marker", MY_TYPE_QUEST_MARKER);

/* Bad names (avoid) */
lrg_registry_register (registry, "MyPlayer", MY_TYPE_PLAYER);  /* CamelCase */
lrg_registry_register (registry, "player_object", MY_TYPE_PLAYER);  /* Underscores */
```

### Overwriting Registration

Register the same name twice to override:

```c
lrg_registry_register (registry, "player", MY_TYPE_PLAYER);

/* Later, override with custom type (for mods) */
lrg_registry_register (registry, "player", MOD_TYPE_CUSTOM_PLAYER);

/* Now "player" creates the mod's custom type */
GObject *obj = lrg_registry_create (registry, "player", NULL);
g_assert (G_OBJECT_TYPE (obj) == MOD_TYPE_CUSTOM_PLAYER);
```

This is the mechanism for mod overrides.

## Type Lookup

### Looking Up by Name

Get the GType from a name:

```c
GType type = lrg_registry_lookup (registry, "player");

if (type == G_TYPE_INVALID)
{
    g_warning ("Type 'player' not registered");
}
else
{
    /* Use the type */
    GObject *obj = g_object_new (type, NULL);
}
```

### Looking Up by Type

Get the name from a GType:

```c
const gchar *name = lrg_registry_lookup_name (registry, MY_TYPE_PLAYER);

if (name)
{
    g_print ("Type is registered as '%s'\n", name);
}
else
{
    g_print ("Type not registered\n");
}
```

### Checking Registration

Check if a name is registered:

```c
if (lrg_registry_is_registered (registry, "player"))
{
    g_print ("Type 'player' is registered\n");
}
```

## Creating Objects

### Simple Creation

Create an instance by name:

```c
/* No properties */
GObject *obj = lrg_registry_create (registry, "player", NULL);

if (obj)
{
    g_print ("Created %s\n", G_OBJECT_TYPE_NAME (obj));
    g_object_unref (obj);
}
else
{
    g_warning ("Type 'player' not registered");
}
```

### Creation with Properties

Set properties during creation:

```c
GObject *obj = lrg_registry_create (registry, "player",
                                    "name", "Hero",
                                    "health", 100,
                                    "level", 1,
                                    NULL);
```

This is equivalent to:

```c
GObject *obj = g_object_new (MY_TYPE_PLAYER,
                             "name", "Hero",
                             "health", 100,
                             "level", 1,
                             NULL);
```

### Array-based Creation

For dynamic property lists:

```c
const gchar *names[] = { "name", "health", "level", NULL };
GValue values[3] = { G_VALUE_INIT, G_VALUE_INIT, G_VALUE_INIT };

g_value_init (&values[0], G_TYPE_STRING);
g_value_set_string (&values[0], "Hero");

g_value_init (&values[1], G_TYPE_INT);
g_value_set_int (&values[1], 100);

g_value_init (&values[2], G_TYPE_INT);
g_value_set_int (&values[2], 1);

GObject *obj = lrg_registry_create_with_properties (registry, "player",
                                                    3, names, values);

g_value_unset (&values[0]);
g_value_unset (&values[1]);
g_value_unset (&values[2]);
```

## Enumeration

### Getting All Names

List all registered names:

```c
GList *names = lrg_registry_get_names (registry);

for (GList *iter = names; iter; iter = iter->next)
{
    const gchar *name = (const gchar *)iter->data;
    g_print ("Registered: %s\n", name);
}

g_list_free (names);  /* Free list, not strings */
```

### Getting Count

How many types are registered:

```c
guint count = lrg_registry_get_count (registry);
g_print ("Registry has %u types\n", count);
```

### Iterating with Callback

Call a function for each registration:

```c
static void
print_registration (const gchar *name,
                    GType        type,
                    gpointer     user_data)
{
    g_print ("%s -> %s\n", name, g_type_name (type));
}

lrg_registry_foreach (registry, print_registration, NULL);
```

## Unregistering Types

Remove a type from the registry:

```c
gboolean result = lrg_registry_unregister (registry, "player");

if (result)
{
    g_print ("Unregistered 'player'\n");
}
else
{
    g_print ("'player' was not registered\n");
}
```

Clear all registrations:

```c
lrg_registry_clear (registry);
g_assert_cmpuint (lrg_registry_get_count (registry), ==, 0);
```

## Registry + DataLoader Integration

The DataLoader uses the Registry to deserialize YAML:

```yaml
# player.yaml
type: player
name: "Hero"
health: 100
```

```c
/* Register type */
lrg_registry_register (registry, "player", MY_TYPE_PLAYER);

/* Setup loader with registry */
LrgDataLoader *loader = lrg_data_loader_new ();
lrg_data_loader_set_registry (loader, registry);

/* Load YAML - loader uses registry to resolve "type: player" */
GObject *obj = lrg_data_loader_load_file (loader, "player.yaml", &error);
/* obj is a MY_TYPE_PLAYER instance */
```

Without the registry, the YAML "type" field is ignored.

## Builtin Type Registration

The engine automatically registers some built-in types during startup:

```c
lrg_engine_startup (engine, &error);

/* These are pre-registered */
lrg_registry_is_registered (registry, "g-object");  /* May be true */
```

You can register more during initialization:

```c
static void
register_game_types (LrgRegistry *registry)
{
    lrg_registry_register (registry, "player", MY_TYPE_PLAYER);
    lrg_registry_register (registry, "enemy", MY_TYPE_ENEMY);
    lrg_registry_register (registry, "item", MY_TYPE_ITEM);
    /* ... more types ... */
}

int main ()
{
    g_autoptr(GError) error = NULL;
    LrgEngine *engine = lrg_engine_get_default ();

    lrg_engine_startup (engine, &error);

    LrgRegistry *registry = lrg_engine_get_registry (engine);
    register_game_types (registry);

    /* Now ready to load game data */
}
```

## Error Handling

Creating an unregistered type returns NULL with a warning:

```c
/* This type isn't registered */
GObject *obj = lrg_registry_create (registry, "nonexistent", NULL);

if (!obj)
{
    g_warning ("Failed to create 'nonexistent'");
}
```

The DataLoader will also fail if a type isn't registered:

```c
/* player.yaml has "type: nonexistent" */
GObject *obj = lrg_data_loader_load_file (loader, "player.yaml", &error);

if (!obj && error)
{
    /* error->domain == LRG_DATA_LOADER_ERROR */
    /* error->code == LRG_DATA_LOADER_ERROR_TYPE */
}
```

## Patterns and Best Practices

### Central Registration

Register all types in one place:

```c
void
game_types_init (LrgRegistry *registry)
{
    /* Player types */
    lrg_registry_register (registry, "player", GAME_TYPE_PLAYER);
    lrg_registry_register (registry, "player-warrior", GAME_TYPE_PLAYER_WARRIOR);
    lrg_registry_register (registry, "player-mage", GAME_TYPE_PLAYER_MAGE);

    /* Enemy types */
    lrg_registry_register (registry, "enemy-goblin", GAME_TYPE_ENEMY_GOBLIN);
    lrg_registry_register (registry, "enemy-orc", GAME_TYPE_ENEMY_ORC);

    /* Items */
    lrg_registry_register (registry, "item-sword", GAME_TYPE_ITEM_SWORD);
    lrg_registry_register (registry, "item-potion", GAME_TYPE_ITEM_POTION);
}
```

### Namespace Convention

Use prefixes for related types:

```c
lrg_registry_register (registry, "creature-player", ...);
lrg_registry_register (registry, "creature-enemy-goblin", ...);

lrg_registry_register (registry, "item-weapon-sword", ...);
lrg_registry_register (registry, "item-armor-helmet", ...);
```

### Type Verification

Check before using a type:

```c
if (!lrg_registry_is_registered (registry, "player"))
{
    g_error ("Required type 'player' not registered!");
}
```

### Mod Overlay Pattern

Let mods override types:

```c
/* Base game types */
lrg_registry_register (registry, "player", GAME_TYPE_PLAYER);

/* Later, a mod registers its custom type */
lrg_registry_register (registry, "player", MOD_TYPE_PLAYER_ENHANCED);

/* Now uses mod's version */
```

## Complete Example

```c
#include <libregnum.h>
#include <glib.h>

#define GAME_TYPE_PLAYER (game_player_get_type ())
G_DECLARE_FINAL_TYPE (GamePlayer, game_player, GAME, PLAYER, GObject)

struct _GamePlayer { GObject parent_instance; };
G_DEFINE_TYPE (GamePlayer, game_player, G_TYPE_OBJECT)
static void game_player_class_init (GamePlayerClass *klass) {}
static void game_player_init (GamePlayer *self) {}

static void
print_types (const gchar *name, GType type, gpointer user_data)
{
    g_print ("  %s -> %s\n", name, g_type_name (type));
}

int
main (int argc, char *argv[])
{
    LrgRegistry *registry = lrg_registry_new ();

    /* Register types */
    lrg_registry_register (registry, "player", GAME_TYPE_PLAYER);
    lrg_registry_register (registry, "object", G_TYPE_OBJECT);

    /* Check registration */
    g_print ("Registered types:\n");
    lrg_registry_foreach (registry, print_types, NULL);

    /* Create by name */
    GObject *player = lrg_registry_create (registry, "player", NULL);
    g_print ("Created: %s\n", G_OBJECT_TYPE_NAME (player));

    g_object_unref (player);
    g_object_unref (registry);

    return 0;
}
```

## See Also

- [LrgRegistry Documentation](../modules/core/registry.md) - Full API
- [Data Loading](data-loading.md) - Integration with DataLoader
- [Architecture Overview](../architecture.md) - Design patterns
