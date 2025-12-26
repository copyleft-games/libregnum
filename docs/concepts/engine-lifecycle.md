---
title: Engine Lifecycle
concept: core
---

# Engine Lifecycle

The LrgEngine manages the complete lifecycle of a Libregnum application. Understanding this lifecycle is fundamental to building games with Libregnum.

> **[Home](../../index.md)** > **[Concepts](index.md)** > Engine Lifecycle

## Overview

The engine follows a strict state machine:

```
UNINITIALIZED
    ↓ startup()
INITIALIZING
    ↓
RUNNING
    ↑ ↓ (update loop)
    update()
    ↓
SHUTTING_DOWN
    ↓
TERMINATED
```

The `LrgEngineState` enum defines these states:

```c
typedef enum
{
    LRG_ENGINE_STATE_UNINITIALIZED,  /* Initial state */
    LRG_ENGINE_STATE_INITIALIZING,   /* During startup */
    LRG_ENGINE_STATE_RUNNING,        /* Ready for game loop */
    LRG_ENGINE_STATE_PAUSED,         /* Paused (future) */
    LRG_ENGINE_STATE_SHUTTING_DOWN,  /* During shutdown */
    LRG_ENGINE_STATE_TERMINATED      /* After shutdown */
} LrgEngineState;
```

## Engine Singleton

The engine is a singleton - there is only one instance:

```c
/* Always returns the same instance */
LrgEngine *engine1 = lrg_engine_get_default ();
LrgEngine *engine2 = lrg_engine_get_default ();
g_assert (engine1 == engine2);
```

This design simplifies subsystem access and ensures consistent state.

## Startup Process

### The startup() Method

Starting the engine initializes all subsystems:

```c
g_autoptr(GError) error = NULL;
gboolean result = lrg_engine_startup (engine, &error);

if (!result)
{
    /* Handle startup error */
    g_warning ("Startup failed: %s", error->message);
    return 1;
}
```

### Startup Sequence

During startup:

1. Engine moves to `INITIALIZING` state
2. All subsystems are initialized:
   - Registry (type mapping)
   - DataLoader (YAML loading)
   - AssetManager (asset caching)
   - Future systems (ECS, Physics, etc.)
3. Builtin types are registered
4. Engine moves to `RUNNING` state
5. "startup" signal is emitted

### Startup Errors

Startup can fail for various reasons. Common errors include:

```c
/* LRG_ENGINE_ERROR domain with codes: */
LRG_ENGINE_ERROR_FAILED,      /* Generic failure */
LRG_ENGINE_ERROR_INIT,        /* Initialization error */
LRG_ENGINE_ERROR_STATE        /* Invalid state (double startup) */
```

Example error handling:

```c
g_autoptr(GError) error = NULL;

if (!lrg_engine_startup (engine, &error))
{
    if (error->domain == LRG_ENGINE_ERROR)
    {
        switch (error->code)
        {
        case LRG_ENGINE_ERROR_INIT:
            g_warning ("Failed to initialize subsystems");
            break;
        case LRG_ENGINE_ERROR_STATE:
            g_warning ("Engine already started");
            break;
        default:
            g_warning ("Unknown error: %s", error->message);
        }
    }
    return 1;
}
```

### Pre-initialization Checks

Before starting, verify the engine state:

```c
LrgEngineState state = lrg_engine_get_state (engine);

if (state != LRG_ENGINE_STATE_UNINITIALIZED &&
    state != LRG_ENGINE_STATE_TERMINATED)
{
    g_warning ("Engine is in state %d, cannot start", state);
    return 1;
}
```

## Game Loop

Once started, the engine runs the game loop:

```c
while (lrg_engine_is_running (engine))
{
    /* Calculate delta time */
    gfloat delta_time = calculate_frame_time ();

    /* Update engine and all systems */
    lrg_engine_update (engine, delta_time);

    /* Render your game */
    render_frame ();
}
```

### The update() Method

Update is called once per frame:

```c
void lrg_engine_update (LrgEngine *self,
                        gfloat     delta)
```

Where:
- `self` is the engine
- `delta` is time since last frame in seconds

### Update Signals

During each update, two signals are emitted:

```c
static void on_pre_update (LrgEngine *engine, gfloat delta, gpointer user_data)
{
    /* Called before engine systems update */
}

static void on_post_update (LrgEngine *engine, gfloat delta, gpointer user_data)
{
    /* Called after engine systems update */
}

/* Connect them */
g_signal_connect (engine, "pre-update", G_CALLBACK (on_pre_update), NULL);
g_signal_connect (engine, "post-update", G_CALLBACK (on_post_update), NULL);

/* During update: pre-update → system updates → post-update */
lrg_engine_update (engine, 0.016f);  /* ~60 FPS */
```

### Game Loop Pattern

Common game loop pattern:

```c
static gboolean running = TRUE;

static gboolean
on_timeout (gpointer user_data)
{
    LrgEngine *engine = (LrgEngine *)user_data;

    lrg_engine_update (engine, 0.016f);

    return running;  /* Continue if TRUE, stop if FALSE */
}

int main ()
{
    g_autoptr(GError) error = NULL;
    LrgEngine *engine = lrg_engine_get_default ();

    lrg_engine_startup (engine, &error);

    /* Setup periodic update */
    g_timeout_add (16, on_timeout, engine);  /* 16ms ~= 60fps */

    /* Run main loop */
    g_main_loop_run (g_main_loop_new (NULL, FALSE));

    running = FALSE;
    lrg_engine_shutdown (engine);

    return 0;
}
```

## Shutdown Process

### The shutdown() Method

Cleanly shutdown the engine:

```c
lrg_engine_shutdown (engine);
```

Shutdown is always successful - no error checking needed.

### Shutdown Sequence

During shutdown:

1. Engine moves to `SHUTTING_DOWN` state
2. All subsystems are shut down
3. Resources are freed
4. Engine moves to `TERMINATED` state
5. "shutdown" signal is emitted

### Shutdown Example

```c
int main ()
{
    g_autoptr(GError) error = NULL;
    LrgEngine *engine = lrg_engine_get_default ();

    if (!lrg_engine_startup (engine, &error))
    {
        g_error ("Startup failed: %s", error->message);
    }

    /* Game loop */
    for (int i = 0; i < 100; i++)
    {
        lrg_engine_update (engine, 0.016f);
    }

    /* Always shutdown before exit */
    lrg_engine_shutdown (engine);

    return 0;
}
```

### Restart After Shutdown

After shutdown, the engine can be restarted:

```c
/* First run */
lrg_engine_startup (engine, &error);
/* ... */
lrg_engine_shutdown (engine);

/* Can restart */
lrg_engine_startup (engine, &error);  /* OK */
/* ... */
lrg_engine_shutdown (engine);
```

But not from RUNNING state:

```c
lrg_engine_startup (engine, &error);
lrg_engine_startup (engine, &error);  /* ERROR: LRG_ENGINE_ERROR_STATE */
```

## Subsystem Access

Subsystems are available only after startup:

```c
LrgRegistry *registry = lrg_engine_get_registry (engine);      /* NULL before startup */
LrgDataLoader *loader = lrg_engine_get_data_loader (engine);  /* NULL before startup */
LrgAssetManager *manager = lrg_engine_get_asset_manager (engine);  /* NULL before startup */
```

Safe pattern:

```c
g_autoptr(GError) error = NULL;
LrgEngine *engine = lrg_engine_get_default ();

if (!lrg_engine_startup (engine, &error))
{
    g_error ("Startup failed: %s", error->message);
}

/* Now safe to use subsystems */
LrgRegistry *registry = lrg_engine_get_registry (engine);
g_assert_nonnull (registry);
```

## State Checking

Check the current engine state:

```c
LrgEngineState state = lrg_engine_get_state (engine);

/* Is it running? */
if (lrg_engine_is_running (engine))
{
    /* Safe to call update */
}

/* Explicit check */
if (state == LRG_ENGINE_STATE_RUNNING)
{
    /* ... */
}
```

## Signals Reference

### startup signal

```c
void user_function (LrgEngine *engine,
                    gpointer   user_data);
```

Emitted when engine transitions to RUNNING.

### shutdown signal

```c
void user_function (LrgEngine *engine,
                    gpointer   user_data);
```

Emitted when engine transitions to TERMINATED.

### pre-update signal

```c
void user_function (LrgEngine *engine,
                    gfloat     delta,
                    gpointer   user_data);
```

Emitted before engine update. Delta is frame time in seconds.

### post-update signal

```c
void user_function (LrgEngine *engine,
                    gfloat     delta,
                    gpointer   user_data);
```

Emitted after engine update.

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
on_shutdown (LrgEngine *engine, gpointer user_data)
{
    g_print ("Engine stopped\n");
}

static void
on_update (LrgEngine *engine, gfloat delta, gpointer user_data)
{
    static guint frame = 0;
    g_print ("Frame %u (delta=%.3fs)\n", frame++, delta);
}

int
main (int argc, char *argv[])
{
    g_autoptr(GError) error = NULL;
    LrgEngine *engine;

    engine = lrg_engine_get_default ();

    /* Connect lifecycle signals */
    g_signal_connect (engine, "startup", G_CALLBACK (on_startup), NULL);
    g_signal_connect (engine, "shutdown", G_CALLBACK (on_shutdown), NULL);
    g_signal_connect (engine, "pre-update", G_CALLBACK (on_update), NULL);

    /* Startup */
    if (!lrg_engine_startup (engine, &error))
    {
        g_error ("Startup failed: %s", error->message);
    }

    /* Run 10 frames */
    for (int i = 0; i < 10; i++)
    {
        lrg_engine_update (engine, 0.016f);
    }

    /* Shutdown */
    lrg_engine_shutdown (engine);

    return 0;
}
```

## See Also

- [LrgEngine Documentation](../modules/core/engine.md) - Full API
- [Architecture Overview](../architecture.md) - System design
- [Quickstart Guide](../quickstart.md) - Getting started
