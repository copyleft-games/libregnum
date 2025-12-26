---
title: Core Module
module: core
---

# Core Module Documentation

The Core module contains the fundamental engine systems that all games built with Libregnum use.

> **[Home](../../index.md)** > **[Modules](../index.md)** > Core

## Overview

The Core module provides:

- **[LrgEngine](engine.md)** - Singleton engine managing all subsystems
- **[LrgRegistry](registry.md)** - Type mapping for data-driven design
- **[LrgDataLoader](data-loader.md)** - YAML deserialization with GObject integration
- **[LrgAssetManager](asset-manager.md)** - Asset caching with mod overlay support

## Essential Concepts

### Engine Lifecycle

The engine follows a state machine:

```
UNINITIALIZED → INITIALIZING → RUNNING ↔ RUNNING → SHUTTING_DOWN → TERMINATED
                 (startup)              (updates)    (shutdown)
```

Every game must follow this pattern:

```c
LrgEngine *engine = lrg_engine_get_default ();
lrg_engine_startup (engine, &error);        /* Initialize */
while (lrg_engine_is_running (engine))
    lrg_engine_update (engine, delta_time); /* Run game loop */
lrg_engine_shutdown (engine);                /* Cleanup */
```

### Data-Driven Design

Use YAML files for all game data:

```yaml
# entities/player.yaml
type: player
name: "Hero"
health: 100
equipment:
  - iron-sword
  - leather-armor
```

The DataLoader deserializes this to a GObject using the Registry:

```c
/* Register type */
lrg_registry_register (registry, "player", GAME_TYPE_PLAYER);

/* Load from YAML */
GObject *player = lrg_data_loader_load_file (loader, "entities/player.yaml", &error);
```

### Asset Management

Unified asset loading with caching and mod overlay:

```c
/* Add search paths */
lrg_asset_manager_add_search_path (manager, "base/assets/");
lrg_asset_manager_add_search_path (manager, "mods/my-mod/assets/");

/* Load asset - searches in reverse priority order */
GrlTexture *texture = lrg_asset_manager_load_texture (manager, "sprites/player.png", &error);
```

## Core Types

| Type | Description | Derivable |
|------|-------------|-----------|
| `LrgEngine` | Engine singleton | Yes |
| `LrgRegistry` | Type registry | No |
| `LrgDataLoader` | YAML loader | No |
| `LrgAssetManager` | Asset manager | Yes |

## Enumerations

| Enum | Values | Description |
|------|--------|-------------|
| `LrgEngineState` | UNINITIALIZED, INITIALIZING, RUNNING, PAUSED, SHUTTING_DOWN, TERMINATED | Engine state |
| `LrgEngineError` | FAILED, INIT, STATE | Engine errors |
| `LrgDataLoaderError` | FAILED, IO, PARSE, TYPE, PROPERTY | Data loader errors |
| `LrgAssetManagerError` | NOT_FOUND, LOAD_FAILED, INVALID_TYPE | Asset manager errors |

## Quick Start

### Minimal Setup

```c
#include <libregnum.h>

int main ()
{
    g_autoptr(GError) error = NULL;
    LrgEngine *engine = lrg_engine_get_default ();

    if (!lrg_engine_startup (engine, &error))
    {
        g_error ("Startup failed: %s", error->message);
    }

    /* Use engine here */

    lrg_engine_shutdown (engine);
    return 0;
}
```

### Game Loop

```c
int main ()
{
    g_autoptr(GError) error = NULL;
    LrgEngine *engine = lrg_engine_get_default ();

    lrg_engine_startup (engine, &error);

    /* Game loop */
    while (lrg_engine_is_running (engine))
    {
        lrg_engine_update (engine, 0.016f); /* 60 FPS */
    }

    lrg_engine_shutdown (engine);
    return 0;
}
```

### Loading Game Data

```c
int main ()
{
    g_autoptr(GError) error = NULL;
    LrgEngine *engine = lrg_engine_get_default ();

    lrg_engine_startup (engine, &error);

    /* Get subsystems */
    LrgRegistry *registry = lrg_engine_get_registry (engine);
    LrgDataLoader *loader = lrg_engine_get_data_loader (engine);

    /* Register types */
    lrg_registry_register (registry, "player", MY_TYPE_PLAYER);
    lrg_registry_register (registry, "enemy", MY_TYPE_ENEMY);

    /* Connect registry to loader */
    lrg_data_loader_set_registry (loader, registry);

    /* Load game data */
    GList *entities = lrg_data_loader_load_directory (loader, "data/entities/", FALSE, &error);
    g_print ("Loaded %u entities\n", g_list_length (entities));

    lrg_engine_shutdown (engine);
    return 0;
}
```

## Type Reference

### LrgEngine

The central engine singleton.

- **Methods**: [See engine.md](engine.md)
- **Signals**: startup, shutdown, pre-update, post-update
- **Derivable**: Yes (for custom engines)

Key methods:
- `lrg_engine_get_default()` - Get singleton
- `lrg_engine_startup()` - Initialize
- `lrg_engine_update()` - Process one frame
- `lrg_engine_shutdown()` - Cleanup
- `lrg_engine_get_state()` - Current state
- `lrg_engine_is_running()` - Check if running

### LrgRegistry

Maps type names to GTypes.

- **Methods**: [See registry.md](registry.md)
- **Final Type**: Cannot subclass

Key methods:
- `lrg_registry_new()` - Create registry
- `lrg_registry_register()` - Register type
- `lrg_registry_lookup()` - Get type by name
- `lrg_registry_create()` - Create object by name

### LrgDataLoader

Deserializes YAML to GObjects.

- **Methods**: [See data-loader.md](data-loader.md)
- **Final Type**: Cannot subclass

Key methods:
- `lrg_data_loader_new()` - Create loader
- `lrg_data_loader_set_registry()` - Set type registry
- `lrg_data_loader_load_file()` - Load single file
- `lrg_data_loader_load_directory()` - Load directory

### LrgAssetManager

Loads and caches game assets.

- **Methods**: [See asset-manager.md](asset-manager.md)
- **Derivable**: Yes (for custom asset loading)

Key methods:
- `lrg_asset_manager_new()` - Create manager
- `lrg_asset_manager_add_search_path()` - Add asset location
- `lrg_asset_manager_load_texture()` - Load texture
- `lrg_asset_manager_load_font()` - Load font

## API Overview

### Singleton Access

```c
LrgEngine *engine = lrg_engine_get_default ();
LrgRegistry *registry = lrg_engine_get_registry (engine);
LrgDataLoader *loader = lrg_engine_get_data_loader (engine);
LrgAssetManager *manager = lrg_engine_get_asset_manager (engine);
```

### Lifecycle

```c
lrg_engine_startup (engine, &error);
lrg_engine_update (engine, delta_time);
lrg_engine_shutdown (engine);
```

### Type Registration

```c
lrg_registry_register (registry, "type-name", G_TYPE);
GObject *obj = lrg_registry_create (registry, "type-name", NULL);
```

### Data Loading

```c
GObject *obj = lrg_data_loader_load_file (loader, "file.yaml", &error);
GList *objects = lrg_data_loader_load_directory (loader, "dir/", TRUE, &error);
```

### Asset Loading

```c
GrlTexture *texture = lrg_asset_manager_load_texture (manager, "sprites/player.png", &error);
GrlFont *font = lrg_asset_manager_load_font (manager, "fonts/main.ttf", 16, &error);
```

## Error Handling

All error-returning functions document their error domain:

```c
g_autoptr(GError) error = NULL;

if (!lrg_engine_startup (engine, &error))
{
    if (error->domain == LRG_ENGINE_ERROR)
    {
        switch (error->code)
        {
        case LRG_ENGINE_ERROR_STATE:
            g_warning ("Already started");
            break;
        /* ... */
        }
    }
    return 1;
}
```

## Detailed Documentation

- **[LrgEngine](engine.md)** - Complete engine API
- **[LrgRegistry](registry.md)** - Registry operations
- **[LrgDataLoader](data-loader.md)** - Data loading
- **[LrgAssetManager](asset-manager.md)** - Asset management

## Concepts

- **[Engine Lifecycle](../../concepts/engine-lifecycle.md)** - Startup/shutdown patterns
- **[Type Registry](../../concepts/type-registry.md)** - Registration and creation
- **[Data Loading](../../concepts/data-loading.md)** - YAML deserialization
- **[Error Handling](../../concepts/error-handling.md)** - GError patterns

## Examples

### Complete Example

```c
#include <libregnum.h>

int
main (int argc, char *argv[])
{
    g_autoptr(GError) error = NULL;
    LrgEngine *engine;
    LrgRegistry *registry;
    LrgDataLoader *loader;
    g_autoptr(GObject) obj = NULL;

    /* Get engine */
    engine = lrg_engine_get_default ();

    /* Startup */
    if (!lrg_engine_startup (engine, &error))
    {
        g_error ("Startup failed: %s", error->message);
    }

    /* Get subsystems */
    registry = lrg_engine_get_registry (engine);
    loader = lrg_engine_get_data_loader (engine);

    /* Register type */
    lrg_registry_register (registry, "player", GAME_TYPE_PLAYER);

    /* Setup loader */
    lrg_data_loader_set_registry (loader, registry);

    /* Load from YAML */
    obj = lrg_data_loader_load_file (loader, "player.yaml", &error);

    if (obj)
    {
        g_print ("Loaded %s\n", G_OBJECT_TYPE_NAME (obj));
    }
    else
    {
        g_warning ("Failed to load: %s", error->message);
    }

    /* Game loop */
    for (int frame = 0; frame < 100; frame++)
    {
        lrg_engine_update (engine, 0.016f);
    }

    /* Shutdown */
    lrg_engine_shutdown (engine);

    return 0;
}
```

## See Also

- [Quickstart Guide](../../quickstart.md)
- [Architecture Overview](../../architecture.md)
- [Concepts](../../concepts/index.md)
- [Module Index](../index.md)
