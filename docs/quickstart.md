---
title: Quickstart Guide
module: Core
---

# Libregnum Quickstart Guide

Get up and running with Libregnum in minutes. This guide covers installation, basic setup, and complete working examples.

> **[Home](../index.md)** > Quickstart

## Installation

### Prerequisites

Fedora 43:
```bash
sudo dnf install glib2-devel gobject-introspection-devel libdex-devel \
    libyaml-devel json-glib-devel pkg-config gcc make
```

### Build from Source

```bash
# Clone the repository (or extract tarball)
git clone https://gitlab.com/yourusername/libregnum.git
cd libregnum

# Build
make DEBUG=1 -j$(nproc)

# Run tests (optional but recommended)
make test

# Install to PREFIX
make install PREFIX=$HOME/.local
```

### Verify Installation

```bash
pkg-config --modversion libregnum
# Should output: 1.0.0 (or your version)
```

## Hello World

Create a minimal Libregnum application:

```c
/* hello.c */
#include <libregnum.h>
#include <glib.h>

int
main (int argc, char *argv[])
{
    g_autoptr(GError) error = NULL;
    LrgEngine *engine;

    /* Get the engine singleton */
    engine = lrg_engine_get_default ();
    g_print ("Engine created: %p\n", engine);

    /* Start the engine */
    if (!lrg_engine_startup (engine, &error))
    {
        g_warning ("Failed to start engine: %s", error->message);
        return 1;
    }

    g_print ("Engine started. State: %d\n", lrg_engine_get_state (engine));

    /* Shutdown */
    lrg_engine_shutdown (engine);
    g_print ("Engine shut down\n");

    return 0;
}
```

Compile and run:

```bash
gcc `pkg-config --cflags --libs libregnum glib-2.0` hello.c -o hello
./hello
```

Expected output:
```
Engine created: 0x...
Engine started. State: 2
Engine shut down
```

## Basic Engine Setup

Here's a more realistic engine initialization:

```c
#include <libregnum.h>
#include <glib.h>

int
main (int argc, char *argv[])
{
    g_autoptr(GError) error = NULL;
    LrgEngine *engine;
    LrgRegistry *registry;
    LrgDataLoader *loader;

    /* Get the engine singleton */
    engine = lrg_engine_get_default ();

    /* Start the engine (initializes all subsystems) */
    if (!lrg_engine_startup (engine, &error))
    {
        g_warning ("Failed to start engine: %s", error->message);
        return 1;
    }

    /* Get subsystems */
    registry = lrg_engine_get_registry (engine);
    loader = lrg_engine_get_data_loader (engine);

    g_print ("Registry has %u types registered\n",
             lrg_registry_get_count (registry));

    /* Connect to signals (optional) */
    g_signal_connect (engine, "pre-update",
                      G_CALLBACK (on_pre_update), NULL);

    /* Simple game loop (runs 5 frames) */
    for (int i = 0; i < 5; i++)
    {
        lrg_engine_update (engine, 0.016f); /* ~60 FPS */
        g_print ("Frame %d\n", i);
    }

    /* Shutdown */
    lrg_engine_shutdown (engine);

    return 0;
}

static void
on_pre_update (LrgEngine *engine,
               gfloat     delta,
               gpointer   user_data)
{
    g_print ("  Pre-update: delta=%.3fs\n", delta);
}
```

## Working with the Registry

The Registry enables data-driven type creation. Register your GObject types, then create instances by name:

```c
#include <libregnum.h>
#include <glib.h>

/* Example custom type for testing */
#define MY_TYPE_PLAYER (my_player_get_type ())
G_DECLARE_FINAL_TYPE (MyPlayer, my_player, MY, PLAYER, GObject)

struct _MyPlayer
{
    GObject parent_instance;
    gchar *name;
    gint health;
};

G_DEFINE_TYPE (MyPlayer, my_player, G_TYPE_OBJECT)

static void
my_player_class_init (MyPlayerClass *klass)
{
}

static void
my_player_init (MyPlayer *self)
{
    self->name = NULL;
    self->health = 100;
}

int
main (int argc, char *argv[])
{
    g_autoptr(GError) error = NULL;
    LrgEngine *engine;
    LrgRegistry *registry;
    g_autoptr(GObject) player = NULL;

    /* Setup */
    engine = lrg_engine_get_default ();
    lrg_engine_startup (engine, &error);
    registry = lrg_engine_get_registry (engine);

    /* Register custom type */
    lrg_registry_register (registry, "player", MY_TYPE_PLAYER);

    /* Create instance by name */
    player = lrg_registry_create (registry, "player", NULL);

    if (player)
    {
        g_print ("Created player: %s\n", G_OBJECT_TYPE_NAME (player));
    }

    lrg_engine_shutdown (engine);
    return 0;
}
```

## Loading Data from YAML

The DataLoader integrates the Registry with YAML files for fully data-driven gameplay:

Create a YAML file (`player.yaml`):

```yaml
type: player
name: "Hero"
health: 100
```

Load it in your code:

```c
#include <libregnum.h>
#include <glib.h>

int
main (int argc, char *argv[])
{
    g_autoptr(GError) error = NULL;
    LrgEngine *engine;
    LrgDataLoader *loader;
    LrgRegistry *registry;
    g_autoptr(GObject) obj = NULL;

    /* Setup */
    engine = lrg_engine_get_default ();
    lrg_engine_startup (engine, &error);

    registry = lrg_engine_get_registry (engine);
    loader = lrg_engine_get_data_loader (engine);

    /* Register types */
    lrg_registry_register (registry, "player", MY_TYPE_PLAYER);

    /* Connect registry to loader */
    lrg_data_loader_set_registry (loader, registry);

    /* Load YAML file */
    obj = lrg_data_loader_load_file (loader, "player.yaml", &error);

    if (obj)
    {
        g_print ("Loaded %s\n", G_OBJECT_TYPE_NAME (obj));
    }
    else
    {
        g_warning ("Failed to load: %s\n", error->message);
    }

    lrg_engine_shutdown (engine);
    return 0;
}
```

## Asset Management

The Asset Manager provides caching and mod overlay support:

```c
#include <libregnum.h>
#include <glib.h>

int
main (int argc, char *argv[])
{
    g_autoptr(GError) error = NULL;
    LrgEngine *engine;
    LrgAssetManager *manager;
    GrlTexture *texture;

    /* Setup */
    engine = lrg_engine_get_default ();
    lrg_engine_startup (engine, &error);
    manager = lrg_engine_get_asset_manager (engine);

    /* Add search paths (higher priority added last) */
    lrg_asset_manager_add_search_path (manager, "./assets/base/");
    lrg_asset_manager_add_search_path (manager, "./assets/mods/my-mod/");

    /* Load texture (searches in reverse priority order) */
    texture = lrg_asset_manager_load_texture (manager, "sprites/player.png", &error);

    if (texture)
    {
        g_print ("Loaded texture\n");
    }
    else
    {
        g_warning ("Failed to load texture: %s\n", error->message);
    }

    /* Check cache sizes */
    g_print ("Cached textures: %u\n",
             lrg_asset_manager_get_texture_cache_size (manager));

    lrg_engine_shutdown (engine);
    return 0;
}
```

## Signal Handling

React to engine events with signals:

```c
#include <libregnum.h>
#include <glib.h>

static void
on_startup (LrgEngine *engine,
            gpointer   user_data)
{
    g_print ("Engine startup signal received\n");
}

static void
on_shutdown (LrgEngine *engine,
             gpointer   user_data)
{
    g_print ("Engine shutdown signal received\n");
}

static void
on_pre_update (LrgEngine *engine,
               gfloat     delta,
               gpointer   user_data)
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

    /* Connect signals */
    g_signal_connect (engine, "startup", G_CALLBACK (on_startup), NULL);
    g_signal_connect (engine, "shutdown", G_CALLBACK (on_shutdown), NULL);
    g_signal_connect (engine, "pre-update", G_CALLBACK (on_pre_update), NULL);

    /* Start and run for 3 frames */
    lrg_engine_startup (engine, &error);

    for (int i = 0; i < 3; i++)
    {
        lrg_engine_update (engine, 0.016f);
    }

    lrg_engine_shutdown (engine);

    return 0;
}
```

Expected output:
```
Engine startup signal received
Frame 0 (delta=0.016s)
Frame 1 (delta=0.016s)
Frame 2 (delta=0.016s)
Engine shutdown signal received
```

## Error Handling

Always check for errors when calling engine functions:

```c
#include <libregnum.h>
#include <glib.h>

int
main (int argc, char *argv[])
{
    g_autoptr(GError) error = NULL;
    LrgEngine *engine;

    engine = lrg_engine_get_default ();

    /* Check error explicitly */
    if (!lrg_engine_startup (engine, &error))
    {
        if (error)
        {
            g_warning ("Startup failed [%s %d]: %s",
                       g_quark_to_string (error->domain),
                       error->code,
                       error->message);
        }
        return 1;
    }

    lrg_engine_shutdown (engine);
    return 0;
}
```

## Memory Management

Libregnum uses GLib's memory management. Use automatic cleanup:

```c
#include <libregnum.h>
#include <glib.h>

int
main (int argc, char *argv[])
{
    /* g_autoptr automatically unrefs/frees on scope exit */
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgRegistry) registry = NULL;

    registry = lrg_registry_new ();
    /* ... use registry ... */
    /* Automatically freed here */

    return 0;
}
```

## Next Steps

- Read the [Architecture Overview](../architecture.md) to understand system design
- Explore [Core Concepts](../concepts/index.md) for detailed information
- Check the [Core Module Documentation](../modules/core/index.md) for complete API details
- Review test files in `tests/` for more examples

## Troubleshooting

### "error: Package libregnum was not found"

Ensure Libregnum is installed and pkg-config can find it:

```bash
pkg-config --list-all | grep libregnum
# If nothing, install was not successful or PREFIX not in PKG_CONFIG_PATH

# Add to PKG_CONFIG_PATH if needed:
export PKG_CONFIG_PATH=$HOME/.local/lib/pkgconfig:$PKG_CONFIG_PATH
```

### Compilation errors with undefined references

Ensure you're linking with all required dependencies:

```bash
gcc `pkg-config --cflags --libs libregnum glib-2.0 gobject-2.0` yourfile.c -o yourprog
```

### Engine won't start

Check error messages:

```c
g_autoptr(GError) error = NULL;
if (!lrg_engine_startup (engine, &error))
{
    g_error ("Engine startup failed: %s", error->message);
}
```

## See Also

- [Core Module Documentation](../modules/core/index.md) - LrgEngine, LrgRegistry, LrgDataLoader, LrgAssetManager
- [Engine Lifecycle Concept](../concepts/engine-lifecycle.md)
- [Type Registry Concept](../concepts/type-registry.md)
- [Data Loading Concept](../concepts/data-loading.md)
