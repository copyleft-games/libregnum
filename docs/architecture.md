---
title: Architecture Overview
---

# Libregnum Architecture Overview

Libregnum is designed around principles of modularity, extensibility, and data-driven design. This document describes the high-level architecture and design patterns.

> **[Home](index.md)** > Architecture

## Design Philosophy

### 1. Data-Driven Design

Configuration and game data are expressed in YAML, not hardcoded. The Registry and DataLoader enable fully data-driven object creation:

- **Registry**: Maps string names to GTypes
- **DataLoader**: Deserializes YAML to GObjects using the Registry
- **Result**: Game content exists in YAML files, not code

### 2. GObject Foundation

All public APIs use GObject:

- **Type Safety**: GType system provides runtime type checking
- **Properties**: Automatic property bindings and validation
- **Signals**: Event-driven communication between systems
- **Introspection**: Language bindings via GObject Introspection

### 3. Modular Architecture

The engine is split into independent modules:

```
libregnum/
├── core/       # Engine, Registry, DataLoader, AssetManager
├── ecs/        # Entity-Component-System
├── input/      # Input handling
├── ui/         # Widget system
├── tilemap/    # 2D tilemaps
├── audio/      # Sound/music
├── physics/    # Physics simulation
├── ai/         # Behavior trees
├── mod/        # Mod loading
└── ...
```

Each module is:
- **Independent**: Can be used standalone
- **Layered**: Higher modules depend on lower modules
- **Testable**: Unit tests for each module
- **Extensible**: Virtual methods for customization

### 4. Error Handling

GError pattern throughout:

```c
g_autoptr(GError) error = NULL;

if (!lrg_engine_startup (engine, &error))
{
    /* Handle specific error */
    if (error->domain == LRG_ENGINE_ERROR)
    {
        switch (error->code)
        {
        case LRG_ENGINE_ERROR_INIT:
            /* Handle initialization error */
            break;
        /* ... */
        }
    }
}
```

Error domains per module:
- `LRG_ENGINE_ERROR` - Engine errors
- `LRG_DATA_LOADER_ERROR` - Data loading errors
- `LRG_ASSET_MANAGER_ERROR` - Asset loading errors
- `LRG_MOD_ERROR` - Mod system errors

## Core Module Architecture

### Engine Singleton

The Engine is the central hub:

```
LrgEngine (singleton)
    |
    ├── LrgRegistry (type mapping)
    ├── LrgDataLoader (YAML loading)
    ├── LrgAssetManager (asset caching)
    └── [future subsystems]
```

**Lifecycle**:

```
UNINITIALIZED
    ↓ startup()
INITIALIZING
    ↓
RUNNING
    ↑ ↓ (update loop)
    update()
    ↓
PAUSED (future)
    ↓ shutdown()
SHUTTING_DOWN
    ↓
TERMINATED
```

**Key Signals**:

- `startup` - Emitted when engine starts
- `shutdown` - Emitted when engine stops
- `pre-update` - Before frame update
- `post-update` - After frame update

### Registry Pattern

The Registry enables data-driven type creation:

```c
/* Registration (during setup) */
LrgRegistry *registry = lrg_engine_get_registry (engine);
lrg_registry_register (registry, "player", MY_TYPE_PLAYER);
lrg_registry_register (registry, "enemy", MY_TYPE_ENEMY);

/* Creation (from YAML or code) */
GObject *player = lrg_registry_create (registry, "player",
                                       "name", "Hero",
                                       NULL);
```

**Benefits**:

- Decouples type references from code
- Enables mods to override types
- Type registration is optional (can create without registry)
- Bulk enumeration via `lrg_registry_foreach()`

### DataLoader Pattern

The DataLoader deserializes YAML to GObjects:

```yaml
# player.yaml
type: player
name: "Hero"
health: 100
```

```c
/* Load single file */
GObject *obj = lrg_data_loader_load_file (loader, "player.yaml", &error);

/* Load directory */
GList *objects = lrg_data_loader_load_directory (loader, "data/", TRUE, &error);

/* Load async */
DexFuture *future = lrg_data_loader_load_file_async (loader, "player.yaml");
```

**Features**:

- Integrates with Registry for type lookup
- Supports sync and async loading
- Batch loading from directories
- Customizable type field name
- Extensible file extensions

### Asset Manager Pattern

The Asset Manager provides caching with mod overlay:

```c
/* Setup search paths (last = highest priority) */
LrgAssetManager *manager = lrg_engine_get_asset_manager (engine);
lrg_asset_manager_add_search_path (manager, "base/assets/");
lrg_asset_manager_add_search_path (manager, "mods/my-mod/");

/* Load (searches in reverse priority) */
GrlTexture *tex = lrg_asset_manager_load_texture (manager, "sprites/player.png", &error);
```

**Design**:

- Multiple search paths with priority
- Automatic caching (returns cached on second load)
- Mod overlay: later paths override earlier
- Separate caches per asset type
- Manual unload capability

## Module Dependency Graph

```
       mod/         (mod system)
       |
┌──────┴────────────────────────┐
|                               |
debug/   (debug tools)      game modules (ECS, UI, etc.)
|                               |
└──────────────┬────────────────┘
               |
        core module (engine, registry, loader, assets)
        |
    glib/gobject
    |
    graylib (raylib bindings)
    yaml-glib (YAML support)
    libdex (async/futures)
```

**Layering**:

- **Layer 0** (Dependencies): glib, gobject, graylib, yaml-glib, libdex
- **Layer 1** (Core): LrgEngine, LrgRegistry, LrgDataLoader, LrgAssetManager
- **Layer 2** (Game Systems): ECS, Input, UI, Tilemap, Audio, Physics, AI, etc.
- **Layer 3** (Advanced): Mod system, Debug tools (depend on Layer 2)

## Key Design Patterns

### 1. Singleton Pattern (Engine)

Only one engine instance:

```c
/* Always returns same instance */
LrgEngine *engine1 = lrg_engine_get_default ();
LrgEngine *engine2 = lrg_engine_get_default ();
g_assert (engine1 == engine2);
```

### 2. Registry Pattern (Type Mapping)

Map strings to GTypes:

```c
lrg_registry_register (registry, "player", MY_TYPE_PLAYER);
GType type = lrg_registry_lookup (registry, "player");
GObject *obj = lrg_registry_create (registry, "player", NULL);
```

### 3. Factory Pattern (DataLoader)

Create objects from structured data:

```c
/* YAML defines structure */
GObject *obj = lrg_data_loader_load_file (loader, "config.yaml", &error);
```

### 4. Strategy Pattern (Extensibility)

Virtual methods for custom behavior:

```c
/* Base class */
struct _LrgAssetManagerClass
{
    GObjectClass parent_class;
    GrlTexture * (*load_texture) (LrgAssetManager *self, ...);
    /* ... */
};

/* Subclass overrides */
class MyAssetManager extends LrgAssetManager
{
    /* Custom texture loading logic */
}
```

### 5. Observer Pattern (Signals)

Event-driven communication:

```c
g_signal_connect (engine, "pre-update",
                  G_CALLBACK (my_update_handler), user_data);
```

## Memory Management

### Automatic Cleanup

Use `g_autoptr()` and `g_autofree`:

```c
g_autoptr(GObject) object = g_object_new (MY_TYPE, NULL);
g_autofree gchar *str = g_strdup ("value");
/* Automatically freed on scope exit */
```

### Reference Counting

All GObjects use reference counting:

```c
/* Caller owns reference */
GObject *obj = lrg_registry_create (registry, "player", NULL);
/* ... */
g_object_unref (obj);  /* or use g_autoptr */
```

### Transfer Semantics

All functions document ownership:

- `(transfer full)` - Caller owns returned value
- `(transfer none)` - Caller doesn't own value (don't unref)
- `(nullable)` - Can return NULL

```c
/* These return (transfer full) - caller owns */
GObject *obj = lrg_data_loader_load_file (...);
LrgRegistry *registry = lrg_registry_new ();

/* These return (transfer none) - caller doesn't own */
LrgRegistry *registry = lrg_engine_get_registry (engine);
```

## Async/Await Pattern

Using libdex for futures:

```c
#include <libdex.h>

DexFuture *future = lrg_data_loader_load_file_async (loader, "player.yaml");

/* Option 1: Use in fiber */
DexFuture *fiber = dex_future_new_fiber (async_callback);

/* Option 2: Use then() callback */
dex_future_then (future, on_loaded, NULL, NULL);
```

## Logging

Per-module log domains:

```c
#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_CORE

lrg_debug (LRG_LOG_DOMAIN_CORE, "Debug: %s", value);
lrg_info (LRG_LOG_DOMAIN_CORE, "Info message");
lrg_warning (LRG_LOG_DOMAIN_CORE, "Warning: %d", code);
```

Enable debug output:

```bash
G_MESSAGES_DEBUG=Libregnum-Core ./myapp
```

## Configuration

Build system uses standard autoconf-style variables:

```bash
make DEBUG=1              # Debug build (-g3 -O0)
make install PREFIX=...  # Custom install prefix
make install LIBDIR=...  # Custom library directory
make test                # Run unit tests
```

Package configuration:

```bash
pkg-config --modversion libregnum
pkg-config --cflags --libs libregnum
```

## Extension Points

### Engine Subclassing

Subclass LrgEngine to customize behavior:

```c
struct _MyEngineClass
{
    LrgEngineClass parent_class;

    /* Override virtual methods */
};

/* Override startup, shutdown, update */
```

### Asset Manager Subclassing

Provide custom asset loading:

```c
GrlTexture *
my_asset_manager_load_texture (LrgAssetManager *self,
                               const gchar     *name,
                               GError         **error)
{
    /* Custom loading logic */
}
```

### Registry Customization

Override type lookups before loading:

```c
lrg_registry_register (registry, "player", MY_TYPE_CUSTOM_PLAYER);
/* Now all YAML "type: player" will create custom type */
```

## Thread Safety

Libregnum is **NOT thread-safe** by default:

- All operations must happen in the main thread
- Engine is not designed for multi-threaded access
- Use libdex for concurrent operations within main loop

Future modules (networking, etc.) will handle their own threading.

## See Also

- [Engine Lifecycle Concept](concepts/engine-lifecycle.md) - Detailed lifecycle explanation
- [Type Registry Concept](concepts/type-registry.md) - Registry deep dive
- [Data Loading Concept](concepts/data-loading.md) - DataLoader deep dive
- [Error Handling Concept](concepts/error-handling.md) - GError patterns
- [Core Module Documentation](modules/core/index.md) - Full API reference
