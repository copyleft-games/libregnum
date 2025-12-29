# Libregnum Project Guide

## Project Overview

Libregnum is a GObject-based game engine library built on top of graylib (raylib GObject wrapper) and yaml-glib (YAML parsing with GObject serialization). It provides high-level game systems with a focus on data-driven design, extensibility, and modding support.

## Directory Structure

```
libregnum/
├── COPYING/LICENSE           # AGPL-3.0-or-later license
├── Makefile                  # Root build orchestration
├── config.mk                 # Build configuration
├── rules.mk                  # Build rules and helpers
├── libregnum.pc.in           # pkg-config template
├── README.md                 # Build instructions
├── CLAUDE.md                 # This file (AI context)
├── deps/
│   ├── graylib/              # GObject wrapper for raylib
│   └── yaml-glib.git/        # YAML parsing library
├── src/
│   ├── libregnum.h           # Master include header
│   ├── lrg-version.h.in      # Version template
│   ├── config.h.in           # Internal config template
│   ├── lrg-types.h           # Forward declarations
│   ├── lrg-enums.h/.c        # Enumerations with GType
│   ├── lrg-log.h             # Logging macros
│   ├── core/                 # Core systems
│   │   ├── lrg-engine.h/.c
│   │   ├── lrg-registry.h/.c
│   │   ├── lrg-data-loader.h/.c
│   │   ├── lrg-asset-manager.h/.c
│   │   └── lrg-asset-pack.h/.c   # Resource pack loading
│   ├── audio/                # Audio systems
│   │   ├── lrg-audio-manager.h/.c
│   │   ├── lrg-sound-bank.h/.c
│   │   ├── lrg-wave-data.h/.c        # Wave data wrapper
│   │   └── lrg-procedural-audio.h/.c # Procedural synthesis
│   ├── graphics/             # Graphics and rendering
│   │   ├── lrg-camera3d.h/.c
│   │   └── lrg-renderer.h/.c
│   ├── ecs/                  # Entity Component System
│   │   ├── lrg-world.h/.c
│   │   ├── lrg-entity.h/.c
│   │   └── components/
│   │       └── lrg-transform-component.h/.c
│   ├── input/                # Input handling
│   ├── ui/                   # UI widgets
│   ├── tilemap/              # 2D tilemap system
│   ├── dialog/               # Dialog/conversation system
│   ├── inventory/            # Inventory management
│   ├── quest/                # Quest tracking
│   ├── save/                 # Save/load system
│   ├── ai/                   # AI behaviors
│   ├── pathfinding/          # A* and navigation
│   ├── physics/              # Physics simulation
│   ├── i18n/                 # Internationalization
│   ├── net/                  # Networking
│   ├── world3d/              # 3D world management
│   ├── scene/                # Scene graph
│   ├── mod/                  # Mod system
│   ├── debug/                # Debug tools
│   ├── scripting/            # Python/GJS scripting
│   ├── economy/              # Economy/trading
│   ├── building/             # Building placement
│   ├── vehicle/              # Vehicle system
│   ├── idle/                 # Idle game mechanics
│   ├── particles/            # Particle effects
│   ├── postprocess/          # Post-processing
│   ├── animation/            # Animation system
│   ├── text/                 # Text rendering
│   ├── video/                # Video playback
│   ├── tween/                # Tweening/easing
│   ├── transition/           # Scene transitions
│   ├── trigger2d/            # 2D trigger zones
│   ├── atlas/                # Texture atlases
│   ├── tutorial/             # Tutorial system
│   ├── weather/              # Weather effects
│   └── lighting/             # Lighting system
├── tests/
│   ├── Makefile
│   └── test-*.c              # One test file per module
└── docs/
    └── modules/              # Per-module documentation
```

## Build Commands

```bash
make              # Build dependencies, then library + GIR
make DEBUG=1      # Debug build with -g3 -O0
make test         # Build and run all tests
make install      # Install to PREFIX (default: /usr/local)
make clean        # Remove build artifacts
make help         # Show all targets
```

## C Standard and Compiler Flags

- **Standard**: `gnu89` (GNU C89 extensions)
- **No `-pedantic`** - GNU extensions are allowed
- **Zero warning tolerance** - All warnings are errors (`-Werror`)

Key warning flags:
```makefile
WARN_CFLAGS := -Wall -Wextra -Werror
WARN_CFLAGS += -Wformat=2 -Wformat-security
WARN_CFLAGS += -Wnull-dereference
WARN_CFLAGS += -Wstrict-prototypes -Wmissing-prototypes
WARN_CFLAGS += -Wold-style-definition -Wdeclaration-after-statement
WARN_CFLAGS += -Wno-unused-parameter
```

## Code Style

### Naming Conventions

| Type | Convention | Example |
|------|------------|---------|
| Macros/Defines | `UPPERCASE_SNAKE_CASE` | `LRG_TYPE_ENGINE` |
| Types/Classes | `PascalCase` | `LrgEngine`, `LrgDataLoader` |
| Functions | `lowercase_snake_case` | `lrg_engine_startup` |
| Variables | `lowercase_snake_case` | `default_engine` |
| Properties | `kebab-case` | `"type-field-name"` |
| Signals | `kebab-case` | `"pre-update"` |

### Function Signature Style

Return type on separate line, parameters aligned:

```c
LrgEngine *
lrg_engine_get_default (void)
{
    /* ... */
}

gboolean
lrg_engine_startup (LrgEngine  *self,
                    GError    **error)
{
    /* ... */
}
```

### Comment Style

Always use `/* */` style, never `//`:

```c
/* Single line comment */

/*
 * Multi-line comment
 * with additional lines.
 */
```

### GObject Patterns

**Derivable types** (for inheritance):
```c
G_DECLARE_DERIVABLE_TYPE (LrgEngine, lrg_engine, LRG, ENGINE, GObject)

struct _LrgEngineClass
{
    GObjectClass parent_class;

    void (*startup)  (LrgEngine *self);
    void (*shutdown) (LrgEngine *self);

    gpointer _reserved[8];  /* ABI stability padding */
};
```

**Final types** (no subclassing):
```c
G_DECLARE_FINAL_TYPE (LrgRegistry, lrg_registry, LRG, REGISTRY, GObject)
```

**Memory management**:
```c
g_autoptr(GError) error = NULL;
g_autofree gchar *str = g_strdup ("value");
g_clear_object (&self->registry);
g_clear_pointer (&self->name, g_free);
return g_steal_pointer (&object);
```

## GObject Introspection Annotations

All public API must include gtk-doc comments with GIR annotations:

```c
/**
 * lrg_data_loader_load_file:
 * @self: an #LrgDataLoader
 * @path: path to the YAML file
 * @error: (nullable): return location for error
 *
 * Loads a GObject from a YAML file.
 *
 * Returns: (transfer full) (nullable): The loaded #GObject, or %NULL on error
 */
```

Common annotations:
- `(transfer full)` - Caller owns returned value
- `(transfer none)` - Caller does NOT own returned value
- `(nullable)` - Can be NULL
- `(out)` - Output parameter
- `(element-type X)` - Container element type
- `(array length=n)` - Array with length parameter

## Dependencies

### Required packages (Fedora)

```bash
glib2-devel gobject-introspection-devel libdex-devel
libyaml-devel json-glib-devel
```

### Submodules

- `deps/graylib/` - Raylib GObject wrapper
- `deps/yaml-glib.git/` - YAML parsing library

## Key Type Patterns

### Engine Singleton

```c
LrgEngine *engine = lrg_engine_get_default ();
lrg_engine_startup (engine, &error);
lrg_engine_update (engine, delta_time);
lrg_engine_shutdown (engine);
```

### Registry (Type Mapping)

```c
LrgRegistry *registry = lrg_engine_get_registry (engine);
lrg_registry_register (registry, "player", MY_TYPE_PLAYER);
GType type = lrg_registry_lookup (registry, "player");
GObject *obj = lrg_registry_create (registry, "player", "name", "Hero", NULL);
```

### Data Loader (YAML)

```c
LrgDataLoader *loader = lrg_engine_get_data_loader (engine);
lrg_data_loader_set_registry (loader, registry);
GObject *obj = lrg_data_loader_load_file (loader, "data/player.yaml", &error);
```

### Async Operations (libdex)

```c
DexFuture *future = lrg_data_loader_load_file_async (loader, path);
/* Use dex_await() in fibers or dex_future_then() for callbacks */
```

### Procedural Audio (Synthesis)

```c
/* Create a custom synthesizer by subclassing LrgProceduralAudio */
G_DECLARE_FINAL_TYPE (MySynth, my_synth, MY, SYNTH, LrgProceduralAudio)

static void
my_synth_generate (LrgProceduralAudio *self,
                   gfloat             *buffer,
                   gint                frame_count)
{
    MySynth *synth = MY_SYNTH (self);
    guint channels = lrg_procedural_audio_get_channels (self);
    guint sample_rate = lrg_procedural_audio_get_sample_rate (self);

    for (gint i = 0; i < frame_count; i++)
    {
        gfloat sample = sinf (synth->phase * 2.0f * G_PI);
        synth->phase += synth->frequency / sample_rate;
        if (synth->phase >= 1.0f) synth->phase -= 1.0f;

        for (guint c = 0; c < channels; c++)
            buffer[i * channels + c] = sample;
    }
}

/* In class_init, override the generate vfunc */
static void
my_synth_class_init (MySynthClass *klass)
{
    LrgProceduralAudioClass *audio_class = LRG_PROCEDURAL_AUDIO_CLASS (klass);
    audio_class->generate = my_synth_generate;
}

/* Usage */
LrgProceduralAudio *synth = g_object_new (MY_TYPE_SYNTH,
                                           "sample-rate", 44100,
                                           "channels", 2,
                                           NULL);
lrg_procedural_audio_play (synth);
/* Call lrg_procedural_audio_update() each frame */
```

### Wave Data (Audio Samples)

```c
/* Load from file */
LrgWaveData *wave = lrg_wave_data_new_from_file ("sound.wav", &error);

/* Create procedural (empty buffer for synthesis) */
LrgWaveData *wave = lrg_wave_data_new_procedural (44100, 2, 1.0f);

/* Manipulate */
LrgWaveData *cropped = lrg_wave_data_crop (wave, 0.0f, 0.5f);
LrgWaveData *resampled = lrg_wave_data_resample (wave, 22050);

/* Convert to playable sound */
GrlSound *sound = lrg_wave_data_to_sound (wave);
```

### Asset Packs (Resource Bundles)

```c
/* Load a resource pack */
LrgAssetPack *pack = lrg_asset_pack_new ("assets.rres", &error);

/* Load encrypted pack */
LrgAssetPack *pack = lrg_asset_pack_new_encrypted ("assets.rres", "password", &error);

/* Load resources */
GrlTexture *tex = lrg_asset_pack_load_texture (pack, "player.png", &error);
GrlSound *sfx = lrg_asset_pack_load_sound (pack, "jump.wav", &error);
LrgWaveData *wave = lrg_asset_pack_load_wave (pack, "ambient.wav", &error);

/* Check contents */
if (lrg_asset_pack_contains (pack, "config.yaml"))
{
    GObject *obj = lrg_asset_pack_load_object (pack, "config.yaml", loader, &error);
}
```

### Quaternion Rotations (3D)

```c
/* Use GrlQuaternion directly from graylib */
GrlQuaternion *quat = grl_quaternion_from_euler (pitch, yaw, roll);

/* Camera orientation */
lrg_camera3d_set_orientation (camera, quat);
lrg_camera3d_slerp_to (camera, target_quat, 0.1f);

/* Transform component */
lrg_transform_component_set_rotation_quaternion (transform, quat);
lrg_transform_component_slerp_rotation (transform, target, amount);
```

## Testing

Tests use GLib testing framework:

```c
static void
test_registry_register (RegistryFixture *fixture,
                        gconstpointer    user_data)
{
    lrg_registry_register (fixture->registry, "test", TEST_TYPE_OBJECT);
    g_assert_true (lrg_registry_is_registered (fixture->registry, "test"));
}

int main (int argc, char *argv[])
{
    g_test_init (&argc, &argv, NULL);
    g_test_add ("/registry/register", RegistryFixture, NULL,
                fixture_set_up, test_registry_register, fixture_tear_down);
    return g_test_run ();
}
```

Run with: `make test`

### Headless Environment Testing

For tests that require display or audio (graphics, procedural audio), use skip macros:

```c
/* Skip if no display available (CI/headless) */
#define SKIP_IF_NO_DISPLAY() \
    do { \
        if (g_getenv ("DISPLAY") == NULL && g_getenv ("WAYLAND_DISPLAY") == NULL) \
        { \
            g_test_skip ("No display available (headless environment)"); \
            return; \
        } \
    } while (0)

/* Skip if resource creation failed (e.g., no audio device) */
#define SKIP_IF_NULL(ptr) \
    do { \
        if ((ptr) == NULL) \
        { \
            g_test_skip ("Resource not available"); \
            return; \
        } \
    } while (0)

static void
test_procedural_audio_new (void)
{
    g_autoptr(LrgProceduralAudio) audio = NULL;

    SKIP_IF_NO_DISPLAY ();

    audio = lrg_procedural_audio_new (44100, 1);
    SKIP_IF_NULL (audio);

    g_assert_cmpuint (lrg_procedural_audio_get_sample_rate (audio), ==, 44100);
}
```

### Warning Levels in Tests

GTest treats `g_warning()` as fatal by default. For expected failure paths that return FALSE/NULL, use `lrg_debug` instead of `lrg_warning` to avoid test failures:

```c
/* Good - use debug for expected failures */
if (sound == NULL)
{
    lrg_log_debug ("Failed to convert wave - audio device unavailable");
    return FALSE;
}

/* Bad - warning becomes fatal in tests */
if (sound == NULL)
{
    lrg_log_warning ("Failed to convert wave");  /* Test fails! */
    return FALSE;
}
```

## Logging

Per-module log domains:

```c
#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_CORE
#include "lrg-log.h"

lrg_debug (LRG_LOG_DOMAIN_CORE, "Debug message: %s", value);
lrg_info (LRG_LOG_DOMAIN_CORE, "Info message");
lrg_warning (LRG_LOG_DOMAIN_CORE, "Warning: %d", code);
```

Enable debug output: `G_MESSAGES_DEBUG=Libregnum-Core ./my-app`

## Error Handling

Use GError pattern:

```c
g_autoptr(GError) error = NULL;

if (!lrg_engine_startup (engine, &error))
{
    lrg_warning (LRG_LOG_DOMAIN_CORE, "Startup failed: %s", error->message);
    return FALSE;
}
```

Error domains:
- `LRG_ENGINE_ERROR` - Engine errors
- `LRG_DATA_LOADER_ERROR` - Data loading errors
- `LRG_ASSET_PACK_ERROR` - Asset pack loading errors
- `LRG_MOD_ERROR` - Mod system errors
- `LRG_SAVE_ERROR` - Save/load errors
- `LRG_NET_ERROR` - Network errors

## Files to Reference for Patterns

| Pattern | File |
|---------|------|
| Derivable GObject with signals | `src/core/lrg-engine.h/.c` |
| Derivable GObject with virtual method | `src/audio/lrg-procedural-audio.h/.c` |
| Final GObject with properties | `src/core/lrg-registry.h/.c` |
| GObject wrapper for graylib type | `src/audio/lrg-wave-data.h/.c` |
| Resource pack loading | `src/core/lrg-asset-pack.h/.c` |
| Async with libdex | `src/core/lrg-data-loader.c` |
| Quaternion/3D rotation | `src/graphics/lrg-camera3d.h/.c` |
| ECS transform component | `src/ecs/components/lrg-transform-component.h/.c` |
| GTest with fixtures | `tests/test-registry.c` |
| Headless-safe tests | `tests/test-procedural-audio.c` |
| Build system | `Makefile`, `config.mk`, `rules.mk` |
| graylib patterns | `deps/graylib/src/scene/grl-entity.h` |
| yaml-glib patterns | `deps/yaml-glib.git/src/yaml-gobject.h` |

## Implementation Status

The library is feature-complete with all major systems implemented:

### Core Systems
- [x] Engine singleton, Registry, DataLoader, AssetManager
- [x] Asset packs (rres format with encryption support)
- [x] Logging, error handling, versioning

### Audio
- [x] AudioManager, SoundBank, MusicPlayer
- [x] WaveData (loading, manipulation, conversion)
- [x] ProceduralAudio (real-time synthesis with virtual generate())

### Graphics & 3D
- [x] Camera3D with quaternion orientation
- [x] Renderer, Scene graph, Materials
- [x] Particles, Post-processing, Lighting
- [x] Weather effects, Video playback

### ECS & Components
- [x] World, Entity, Component system
- [x] TransformComponent with quaternion rotation
- [x] SpriteComponent, PhysicsComponent, etc.

### Game Systems
- [x] Input handling, UI widgets
- [x] Tilemap, Dialog, Inventory, Quest
- [x] Save/Load, AI, Pathfinding
- [x] Economy, Building, Vehicle, Idle mechanics
- [x] Trigger2D, Tutorial, Atlas

### Infrastructure
- [x] Animation, Tween, Transition systems
- [x] I18N, Networking, Scripting (Python/GJS)
- [x] Mod system, Debug tools, Accessibility
- [x] Steam integration

### Testing
- [x] 60+ test files covering all modules
- [x] Headless environment support (skip macros)
- [x] GTest with TAP output
