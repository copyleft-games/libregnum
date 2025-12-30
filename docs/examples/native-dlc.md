# Native DLC Example (GModule Loading)

## Overview

The `game-creature-collector` example demonstrates loading native shared libraries (`.so` files) as DLC content using GModule. This is the most powerful type of mod, allowing DLCs to execute native code and directly integrate with the game's systems.

## Running the Example

```bash
cd examples

# Build the game and native mod
make game-creature-collector

# Run WITH mods (shows 5 creatures: 3 base + 2 DLC)
./build/release/examples/game-creature-collector

# Run WITHOUT mods (shows 3 base creatures only)
./build/release/examples/game-creature-collector --no-mods
```

## Features Demonstrated

| Feature | Description |
|---------|-------------|
| GModule Loading | Loading `.so` shared libraries at runtime |
| Symbol Export | Main executable exports functions for DLC to call |
| Entry Points | `lrg_mod_init()` and `lrg_mod_shutdown()` functions |
| Content Registration | DLC registering content with the game's systems |
| Visual Difference | Clear before/after comparison with `--no-mods` |

## Controls

| Key | Action |
|-----|--------|
| M | Toggle mod information overlay |
| R | Respawn all creatures |
| ESC | Exit |

## Architecture

### Symbol Visibility Pattern

The native DLC pattern requires careful symbol management:

1. **Main Game** exports registration functions with `G_MODULE_EXPORT`
2. **Main Game** is linked with `-Wl,--export-dynamic` to expose symbols
3. **DLC** declares functions as `extern` and calls them at runtime
4. **GModule** resolves symbols when the shared library loads

```
┌─────────────────────┐         ┌─────────────────────┐
│   Main Executable   │         │   DLC Shared Lib    │
│                     │         │  (libbonuscreatures)│
│  register_creature  │◄────────│  lrg_mod_init()     │
│  get_creature_types │         │  lrg_mod_shutdown() │
│                     │         │                     │
│  -Wl,--export-      │         │  -shared -fPIC      │
│   dynamic           │         │                     │
└─────────────────────┘         └─────────────────────┘
```

## File Structure

```
examples/
├── game-creature-collector.c    # Main game source
├── creature-registry.h          # Shared API header
├── Makefile                     # Build rules
└── mods/
    └── bonus-creatures/
        ├── mod.yaml             # DLC manifest
        └── lib/
            └── libbonuscreatures.c   # Native DLC source
            └── libbonuscreatures.so  # Built shared library
```

## DLC Manifest

Native DLCs use `type: native` and specify an `entry_point`:

```yaml
# mods/bonus-creatures/mod.yaml
id: bonus-creatures
name: "Bonus Creatures Pack"
version: "1.0.0"
type: native
author: "Demo Games"
description: "Adds 2 bonus creatures to the game!"

# Path to shared library (relative to mod directory)
entry_point: "lib/libbonuscreatures.so"

dlc:
  type: expansion
  store_id: "bonus-creatures-pack"
  price: "$2.99"
  currency: "USD"

  ownership:
    method: manifest
```

## Shared API Header

Both the game and DLC include a shared header defining the registration API:

```c
/* creature-registry.h */
#ifndef CREATURE_REGISTRY_H
#define CREATURE_REGISTRY_H

#include <glib.h>
#include <gmodule.h>

G_BEGIN_DECLS

typedef struct
{
    gchar    *name;
    guint8    r, g, b;    /* Color */
    gfloat    speed;      /* Movement speed multiplier */
    gboolean  from_mod;   /* TRUE if added by mod */
} CreatureType;

/* Exported by main game, called by DLC */
G_MODULE_EXPORT void
register_creature_type (const gchar *name,
                        guint8       r,
                        guint8       g,
                        guint8       b,
                        gfloat       speed,
                        gboolean     from_mod);

G_MODULE_EXPORT GPtrArray *
get_creature_types (void);

G_END_DECLS

#endif /* CREATURE_REGISTRY_H */
```

## Main Game Implementation

### Exporting Registration Function

```c
/* In game-creature-collector.c */

/* Global creature registry */
static GPtrArray *g_creature_types = NULL;

/* Exported function - DLC calls this to register creatures */
G_MODULE_EXPORT void
register_creature_type (const gchar *name,
                        guint8       r,
                        guint8       g,
                        guint8       b,
                        gfloat       speed,
                        gboolean     from_mod)
{
    CreatureType *type = g_new0 (CreatureType, 1);
    type->name = g_strdup (name);
    type->r = r;
    type->g = g;
    type->b = b;
    type->speed = speed;
    type->from_mod = from_mod;

    g_ptr_array_add (g_creature_types, type);

    g_print ("Registered creature: %s (%s)\n",
             name, from_mod ? "DLC" : "base");
}
```

### Loading Mods

```c
static void
load_mods (void)
{
    g_autoptr(LrgModManager) mod_manager = lrg_mod_manager_new ();
    g_autoptr(GError) error = NULL;

    /* Set mod search path */
    lrg_mod_manager_add_search_path (mod_manager, "mods");

    /* Discover mods */
    if (!lrg_mod_manager_discover (mod_manager, &error))
    {
        g_warning ("Discovery failed: %s", error->message);
        return;
    }

    /* Load all discovered mods - this calls lrg_mod_init() */
    if (!lrg_mod_manager_load_all (mod_manager, &error))
    {
        g_warning ("Load failed: %s", error->message);
    }

    /* Check what loaded */
    GPtrArray *mods = lrg_mod_manager_get_loaded_mods (mod_manager);
    g_print ("Loaded %u mod(s)\n", mods->len);
}
```

## Native DLC Implementation

### Entry Point Functions

```c
/* libbonuscreatures.c */
#include <glib.h>
#include <gmodule.h>
#include <libregnum.h>
#include "../../../creature-registry.h"

/* Forward declarations for exported functions */
G_MODULE_EXPORT gboolean
lrg_mod_init (LrgMod   *mod,
              gpointer *user_data);

G_MODULE_EXPORT void
lrg_mod_shutdown (LrgMod  *mod,
                  gpointer user_data);

/**
 * lrg_mod_init:
 * @mod: The LrgMod instance
 * @user_data: (out): Optional user data to store
 *
 * Called when the mod is loaded.
 *
 * Returns: TRUE on success, FALSE on failure
 */
G_MODULE_EXPORT gboolean
lrg_mod_init (LrgMod   *mod,
              gpointer *user_data)
{
    g_print ("Bonus Creatures Pack Initializing...\n");

    /* Register bonus creatures with the game */
    register_creature_type ("Golden Slime", 255, 215, 0, 2.5f, TRUE);
    register_creature_type ("Shadow Beast", 128, 0, 200, 3.0f, TRUE);

    g_print ("Bonus Creatures Pack loaded!\n");

    *user_data = NULL;
    return TRUE;
}

/**
 * lrg_mod_shutdown:
 * @mod: The LrgMod instance
 * @user_data: User data from init
 *
 * Called when the mod is unloaded.
 */
G_MODULE_EXPORT void
lrg_mod_shutdown (LrgMod  *mod,
                  gpointer user_data)
{
    g_print ("Bonus Creatures Pack unloaded.\n");
}
```

## Build System

### Main Game Build Rule

The key is `-Wl,--export-dynamic` to export symbols:

```makefile
# Build game with exported symbols for DLC to call
$(EXAMPLES_BUILDDIR)/game-creature-collector: game-creature-collector.c creature-registry.h | mods
	$(CC) $(CFLAGS) -Wl,--export-dynamic -o $@ $< $(LDFLAGS) $(LIBS)
```

### Native Mod Build Rule

```makefile
# Flags for building shared libraries
MOD_CFLAGS := $(CFLAGS) -fPIC
MOD_LDFLAGS := -shared $(LDFLAGS)

# Build the native mod shared library
mods/bonus-creatures/lib/libbonuscreatures.so: mods/bonus-creatures/lib/libbonuscreatures.c creature-registry.h
	$(CC) $(MOD_CFLAGS) -o $@ $< $(MOD_LDFLAGS) $(LIBS)
```

## Expected Output

### With Mods (default)

```
=== Registering base creatures ===
Registered creature: Red Blob (base)
Registered creature: Blue Blob (base)
Registered creature: Green Blob (base)
=== Loading mods ===
Searching for mods in: /path/to/examples/mods
=== Bonus Creatures Pack Initializing ===
Registered creature: Golden Slime (DLC)
Registered creature: Shadow Beast (DLC)
Bonus Creatures Pack loaded successfully!
Loaded 1 mod(s):
  - Bonus Creatures Pack v1.0.0
=== Spawning creatures ===
=== Starting game loop ===
Total creatures: 5 (3 base + 2 DLC)
```

### Without Mods (`--no-mods`)

```
=== Registering base creatures ===
Registered creature: Red Blob (base)
Registered creature: Blue Blob (base)
Registered creature: Green Blob (base)
=== Loading mods ===
Mod loading disabled (--no-mods)
=== Spawning creatures ===
=== Starting game loop ===
Total creatures: 3 (3 base + 0 DLC)
```

## Visual Comparison

| Without DLC | With DLC |
|-------------|----------|
| 3 creatures bouncing | 5 creatures bouncing |
| Status: "NO MODS" | Status: "DLC LOADED" |
| White outlines only | Gold outlines on DLC creatures |
| Count: "3 (base only)" | Count: "5 (3 base + 2 DLC)" |

## Common Issues

### Symbol Not Found

If the DLC fails to find `register_creature_type`:

1. Ensure main executable is linked with `-Wl,--export-dynamic`
2. Verify `G_MODULE_EXPORT` macro on exported functions
3. Check that the function signature matches exactly

### GModule Load Failed

If `g_module_open()` fails:

1. Check the shared library path in `entry_point`
2. Verify library dependencies with `ldd libbonuscreatures.so`
3. Ensure `LD_LIBRARY_PATH` includes required libraries

### Missing Prototype Warning

Native mod functions need forward declarations:

```c
/* Add forward declarations before the implementation */
G_MODULE_EXPORT gboolean
lrg_mod_init (LrgMod *mod, gpointer *user_data);

G_MODULE_EXPORT void
lrg_mod_shutdown (LrgMod *mod, gpointer user_data);
```

## See Also

- [DLC Store Example](dlc-store.md) - DLC ownership and store integration
- [Modding Guide](modding-guide.md) - Creating content mods
- [LrgMod](../modules/mod/mod.md) - Mod object reference
- [LrgModManifest](../modules/mod/mod-manifest.md) - Manifest format
