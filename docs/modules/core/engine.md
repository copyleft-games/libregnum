---
title: LrgEngine
module: core
type: Class
parent: GObject
since: 1.0
---

# LrgEngine

The central hub for all engine subsystems. LrgEngine manages the complete lifecycle of a Libregnum application from startup through shutdown.

> **[Home](../../index.md)** > **[Modules](../index.md)** > **[Core](index.md)** > LrgEngine

## Overview

LrgEngine is a singleton that:
- Manages engine state (startup, running, shutdown)
- Coordinates all subsystems (registry, data loader, asset manager)
- Provides game loop integration via update()
- Emits signals for lifecycle events

The engine is **derivable** - subclasses can override virtual methods to customize behavior.

## Key Concepts

### Singleton Pattern

Only one instance exists per process:

```c
LrgEngine *engine1 = lrg_engine_get_default ();
LrgEngine *engine2 = lrg_engine_get_default ();
g_assert (engine1 == engine2);  /* Same instance */
```

### State Management

Engine follows strict state machine:

```
UNINITIALIZED
    ↓ startup()
INITIALIZING
    ↓
RUNNING
    ↓ update()
    (loop)
    ↓
SHUTTING_DOWN
    ↓
TERMINATED
```

### Subsystem Access

Get other subsystems from engine:

```c
LrgRegistry *registry = lrg_engine_get_registry (engine);
LrgDataLoader *loader = lrg_engine_get_data_loader (engine);
LrgAssetManager *manager = lrg_engine_get_asset_manager (engine);
```

## API Reference

### Singleton Access

#### lrg_engine_get_default()

```c
LrgEngine *
lrg_engine_get_default (void)
```

Gets the default engine instance. Creates it if it doesn't exist.

**Returns**: (transfer none) The default LrgEngine instance

**Example**:
```c
LrgEngine *engine = lrg_engine_get_default ();
```

### Lifecycle Methods

#### lrg_engine_startup()

```c
gboolean
lrg_engine_startup (LrgEngine  *self,
                    GError    **error)
```

Starts up the engine and all subsystems.

Must be called before using the engine. Initializes:
- Registry (type mapping)
- DataLoader (YAML loading)
- AssetManager (asset caching)
- All future subsystems

**Parameters**:
- `self` - The engine
- `error` - (nullable) return location for error

**Returns**: TRUE on success, FALSE on error

**Errors**: LRG_ENGINE_ERROR
- `LRG_ENGINE_ERROR_INIT` - Initialization failed
- `LRG_ENGINE_ERROR_STATE` - Already started (double startup)

**Example**:
```c
g_autoptr(GError) error = NULL;
if (!lrg_engine_startup (engine, &error))
{
    g_warning ("Startup failed: %s", error->message);
    return 1;
}
```

#### lrg_engine_shutdown()

```c
void
lrg_engine_shutdown (LrgEngine *self)
```

Shuts down the engine and all subsystems.

After calling this, the engine cannot be used until startup() is called again. Always succeeds - no error checking needed.

**Parameters**:
- `self` - The engine

**Example**:
```c
lrg_engine_shutdown (engine);
```

#### lrg_engine_update()

```c
void
lrg_engine_update (LrgEngine *self,
                   gfloat     delta)
```

Updates the engine for one frame.

Should be called from the game loop to update all engine systems. Emits pre-update and post-update signals.

Does nothing if engine is not running (safe to call anytime).

**Parameters**:
- `self` - The engine
- `delta` - Time since last frame in seconds

**Example**:
```c
while (lrg_engine_is_running (engine))
{
    lrg_engine_update (engine, 0.016f);  /* 60 FPS */
}
```

### State Queries

#### lrg_engine_get_state()

```c
LrgEngineState
lrg_engine_get_state (LrgEngine *self)
```

Gets the current engine state.

**Parameters**:
- `self` - The engine

**Returns**: The current LrgEngineState

**Example**:
```c
LrgEngineState state = lrg_engine_get_state (engine);

if (state == LRG_ENGINE_STATE_RUNNING)
{
    /* Safe to use engine */
}
```

#### lrg_engine_is_running()

```c
gboolean
lrg_engine_is_running (LrgEngine *self)
```

Checks if the engine is in the running state.

**Parameters**:
- `self` - The engine

**Returns**: TRUE if the engine is running

**Example**:
```c
while (lrg_engine_is_running (engine))
{
    lrg_engine_update (engine, delta_time);
}
```

### Subsystem Access

#### lrg_engine_get_registry()

```c
LrgRegistry *
lrg_engine_get_registry (LrgEngine *self)
```

Gets the engine's type registry.

The registry maps string names to GTypes for data-driven instantiation.

Returns NULL before startup.

**Parameters**:
- `self` - The engine

**Returns**: (transfer none) The LrgRegistry

**Example**:
```c
LrgRegistry *registry = lrg_engine_get_registry (engine);
g_assert_nonnull (registry);
```

#### lrg_engine_get_data_loader()

```c
LrgDataLoader *
lrg_engine_get_data_loader (LrgEngine *self)
```

Gets the engine's data loader.

The data loader handles loading YAML files and converting them to GObjects.

Returns NULL before startup.

**Parameters**:
- `self` - The engine

**Returns**: (transfer none) The LrgDataLoader

**Example**:
```c
LrgDataLoader *loader = lrg_engine_get_data_loader (engine);
GObject *obj = lrg_data_loader_load_file (loader, "data.yaml", &error);
```

#### lrg_engine_get_asset_manager()

```c
LrgAssetManager *
lrg_engine_get_asset_manager (LrgEngine *self)
```

Gets the engine's asset manager.

The asset manager handles loading and caching of game assets (textures, fonts, sounds, music) with mod overlay support.

Returns NULL before startup.

**Parameters**:
- `self` - The engine

**Returns**: (transfer none) The LrgAssetManager

**Example**:
```c
LrgAssetManager *manager = lrg_engine_get_asset_manager (engine);
GrlTexture *tex = lrg_asset_manager_load_texture (manager, "sprite.png", &error);
```

## Signals

### "startup"

```c
void user_function (LrgEngine *engine,
                    gpointer   user_data)
```

Emitted when the engine transitions to RUNNING state (end of startup).

**Parameters**:
- `engine` - The engine
- `user_data` - User-provided data

**Example**:
```c
g_signal_connect (engine, "startup",
                  G_CALLBACK (on_engine_startup), NULL);
```

### "shutdown"

```c
void user_function (LrgEngine *engine,
                    gpointer   user_data)
```

Emitted when the engine transitions to TERMINATED state (end of shutdown).

**Parameters**:
- `engine` - The engine
- `user_data` - User-provided data

**Example**:
```c
g_signal_connect (engine, "shutdown",
                  G_CALLBACK (on_engine_shutdown), NULL);
```

### "pre-update"

```c
void user_function (LrgEngine *engine,
                    gfloat     delta,
                    gpointer   user_data)
```

Emitted before engine systems update each frame.

**Parameters**:
- `engine` - The engine
- `delta` - Frame time in seconds
- `user_data` - User-provided data

**Example**:
```c
g_signal_connect (engine, "pre-update",
                  G_CALLBACK (on_pre_update), NULL);
```

### "post-update"

```c
void user_function (LrgEngine *engine,
                    gfloat     delta,
                    gpointer   user_data)
```

Emitted after engine systems update each frame.

**Parameters**:
- `engine` - The engine
- `delta` - Frame time in seconds
- `user_data` - User-provided data

**Example**:
```c
g_signal_connect (engine, "post-update",
                  G_CALLBACK (on_post_update), NULL);
```

## Virtual Methods

Subclasses can override these methods:

```c
struct _LrgEngineClass
{
    GObjectClass parent_class;

    /* Virtual methods */
    void (*startup)  (LrgEngine *self);
    void (*shutdown) (LrgEngine *self);
    void (*update)   (LrgEngine *self, gfloat delta);

    gpointer _reserved[8];  /* ABI stability */
};
```

### Overriding

```c
static void
my_engine_update (LrgEngine *engine,
                  gfloat     delta)
{
    /* Custom update logic */

    /* Call parent implementation */
    LRG_ENGINE_CLASS (my_engine_parent_class)->update (engine, delta);
}
```

## Complete Example

```c
#include <libregnum.h>
#include <glib.h>

static void
on_startup (LrgEngine *engine, gpointer user_data)
{
    g_print ("Engine started\n");
}

static void
on_update (LrgEngine *engine, gfloat delta, gpointer user_data)
{
    static guint frames = 0;
    g_print ("Frame %u (delta=%.3fs)\n", frames++, delta);
}

static void
on_shutdown (LrgEngine *engine, gpointer user_data)
{
    g_print ("Engine stopped\n");
}

int
main (int argc, char *argv[])
{
    g_autoptr(GError) error = NULL;
    LrgEngine *engine = lrg_engine_get_default ();

    /* Connect signals */
    g_signal_connect (engine, "startup", G_CALLBACK (on_startup), NULL);
    g_signal_connect (engine, "pre-update", G_CALLBACK (on_update), NULL);
    g_signal_connect (engine, "shutdown", G_CALLBACK (on_shutdown), NULL);

    /* Startup */
    if (!lrg_engine_startup (engine, &error))
    {
        g_error ("Failed: %s", error->message);
    }

    /* Run 60 frames */
    for (int i = 0; i < 60; i++)
    {
        lrg_engine_update (engine, 0.016f);
    }

    /* Shutdown */
    lrg_engine_shutdown (engine);

    return 0;
}
```

## Errors

### LRG_ENGINE_ERROR

```c
LRG_ENGINE_ERROR_FAILED   /* Generic failure */
LRG_ENGINE_ERROR_INIT     /* Initialization failed */
LRG_ENGINE_ERROR_STATE    /* Invalid state */
```

Get error details:

```c
if (!lrg_engine_startup (engine, &error))
{
    if (error->domain == LRG_ENGINE_ERROR)
    {
        switch (error->code)
        {
        case LRG_ENGINE_ERROR_STATE:
            g_warning ("Already started");
            break;
        default:
            g_warning ("Startup error: %s", error->message);
        }
    }
    return 1;
}
```

## See Also

- [Engine Lifecycle Concept](../../concepts/engine-lifecycle.md) - Detailed lifecycle explanation
- [Core Module](index.md) - Module overview
- [Quickstart Guide](../../quickstart.md) - Getting started
