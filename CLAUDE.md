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
├── ROADMAP.md                # Development history and feature planning
├── deps/
│   ├── graylib/              # GObject wrapper for raylib
│   └── yaml-glib/            # YAML parsing library
├── src/                      # 55+ modules, 310+ header files
│   ├── libregnum.h           # Master include header
│   ├── lrg-version.h.in      # Version template
│   ├── config.h.in           # Internal config template
│   ├── lrg-types.h           # Forward declarations (all types)
│   ├── lrg-enums.h/.c        # Enumerations with GType
│   ├── lrg-log.h             # Logging macros
│   │
│   │ # Core Systems
│   ├── core/                 # Engine, Registry, DataLoader, AssetManager, AssetPack
│   ├── ecs/                  # World, GameObject, Component + transform/sprite/collider/animator
│   ├── graphics/             # Window, Renderer, Camera (7 types), Drawable interface
│   ├── input/                # Keyboard, Mouse, Gamepad, InputMap/Action/Binding
│   ├── audio/                # AudioManager, SoundBank, MusicTrack, WaveData, ProceduralAudio
│   │
│   │ # Game Systems
│   ├── ui/                   # 8 widgets, 3 layouts, Theme, Canvas
│   ├── tilemap/              # Tileset, TilemapLayer, Tilemap
│   ├── dialog/               # DialogNode, DialogTree, DialogRunner
│   ├── quest/                # QuestDef, QuestObjective, QuestInstance, QuestLog
│   ├── inventory/            # ItemDef, ItemStack, Inventory, Equipment
│   ├── save/                 # Saveable interface, SaveContext, SaveGame, SaveManager
│   ├── ai/                   # BehaviorTree, Blackboard, BTNode hierarchy
│   ├── pathfinding/          # NavGrid, NavCell, Path, Pathfinder (A*)
│   ├── physics/              # PhysicsWorld, RigidBody, CollisionInfo
│   │
│   │ # Genre-Specific
│   ├── economy/              # Resource, ResourcePool, Producer, Consumer, Market, EconomyManager
│   ├── building/             # BuildingDef, BuildingInstance, PlacementSystem, BuildGrid
│   ├── vehicle/              # Vehicle, VehicleController, Wheel, TrafficAgent, Road, RoadNetwork
│   ├── idle/                 # IdleCalculator, BigNumber, Prestige, UnlockTree, Automation
│   │
│   │ # Visual Effects
│   ├── particles/            # ParticleEmitter, Particle, ParticleSystem, ParticleForce
│   ├── postprocess/          # PostProcessor, PostEffect, Bloom, ColorGrade, FXAA
│   ├── lighting/             # Light2D, PointLight2D, SpotLight2D, DirectionalLight2D, ShadowMap
│   ├── weather/              # Rain, Snow, Fog, Lightning, Weather, DayNightCycle
│   │
│   │ # Animation & Transitions
│   ├── animation/            # Skeleton, Bone, AnimationClip, Animator, StateMachine, IKSolver
│   ├── tween/                # Easing, Tween, TweenGroup, TweenSequence, TweenParallel
│   ├── transition/           # Fade, Wipe, Dissolve, Slide, Zoom, Shader transitions
│   │
│   │ # Content & UI
│   ├── text/                 # RichText, TextSpan, TextEffect, FontManager
│   ├── video/                # VideoPlayer, VideoTexture, VideoSubtitles
│   ├── atlas/                # AtlasRegion, TextureAtlas, SpriteSheet, NineSlice, AtlasPacker
│   ├── tutorial/             # TutorialStep, Tutorial, TutorialManager, Highlight, InputPrompt
│   ├── trigger2d/            # TriggerRect, TriggerCircle, TriggerPolygon, TriggerManager
│   │
│   │ # Infrastructure
│   ├── i18n/                 # Locale, Localization
│   ├── net/                  # NetMessage, NetPeer, NetServer, NetClient
│   ├── world3d/              # Level3D, Octree, Sector, Portal, Trigger3D, SpawnPoint3D
│   ├── scene/                # Scene, SceneObject, SceneSerializer interface
│   ├── mod/                  # ModManifest, Mod, ModLoader, Modable interface, Providers
│   ├── debug/                # Profiler, DebugConsole, DebugOverlay, Inspector
│   ├── scripting/            # Lua, Python, PyGObject, Gjs backends, Scriptable interface
│   ├── shapes/               # Shape, Shape2D, Shape3D primitives
│   │
│   │ # Platform Integration
│   ├── steam/                # SteamService, SteamClient, Achievements, Cloud, Workshop, Leaderboards
│   ├── settings/             # Settings, GraphicsSettings, AudioSettings, ControlSettings
│   ├── accessibility/        # ColorFilter, SubtitleManager, ScreenReader
│   ├── crash/                # CrashReporter, CrashDialog
│   ├── gamestate/            # GameState, GameStateManager, MainMenu, PauseMenu, LoadingScreen
│   │
│   │ # Enhancements
│   ├── analytics/            # Analytics, AnalyticsEvent, Consent, AnalyticsBackend
│   ├── achievement/          # Achievement, AchievementProgress, AchievementManager, Notification
│   ├── photomode/            # Screenshot, PhotoCameraController, PhotoMode
│   ├── demo/                 # DemoGatable interface, DemoManager
│   └── vr/                   # VRService interface, VRStub, VRComfort
│
├── tests/
│   ├── Makefile
│   └── test-*.c              # 70+ test files covering all modules
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

## Architecture Overview

Libregnum uses a layered GObject architecture with three primary patterns:

1. **Interfaces** (`G_DECLARE_INTERFACE`) - Implement for custom backends/plugins
2. **Derivable Types** (`G_DECLARE_DERIVABLE_TYPE`) - Subclass for custom game behavior
3. **Final Types** (`G_DECLARE_FINAL_TYPE`) - Use directly as data containers/managers

### GObject Interfaces

Interfaces define contracts for pluggable implementations. Implement these when you need custom backends or behavior.

| Interface | Location | Purpose | Key Methods |
|-----------|----------|---------|-------------|
| `LrgVRService` | `src/vr/lrg-vr-service.h` | VR backend abstraction | `initialize`, `shutdown`, `poll_events`, `get_hmd_pose`, `submit_frame` |
| `LrgSteamService` | `src/steam/lrg-steam-service.h` | Steam SDK abstraction | `init`, `shutdown`, `run_callbacks`, `is_available` |
| `LrgSaveable` | `src/save/lrg-saveable.h` | Save/load persistence | `get_save_id`, `save`, `load` |
| `LrgModable` | `src/mod/lrg-modable.h` | Mod lifecycle hooks | `mod_init`, `mod_shutdown`, `mod_get_info` |
| `LrgScriptable` | `src/scripting/lrg-scriptable.h` | Script exposure control | `get_script_methods`, `get_property_access` |
| `LrgDrawable` | `src/graphics/lrg-drawable.h` | Rendering interface | `draw`, `get_bounds` |
| `LrgShadowCaster` | `src/lighting/lrg-shadow-caster.h` | 2D shadow casting | `get_edges`, `is_opaque`, `get_shadow_opacity` |
| `LrgDemoGatable` | `src/demo/lrg-demo-gatable.h` | Demo content gating | `get_content_id`, `is_demo_content`, `get_unlock_message` |
| `LrgSceneSerializer` | `src/scene/lrg-scene-serializer.h` | Scene format handlers | `load_from_file`, `save_to_file` |

**Content Provider Interfaces** (`src/mod/lrg-providers.h`) - For mods providing content:

| Interface | Purpose |
|-----------|---------|
| `LrgEntityProvider` | Provide custom entity GTypes |
| `LrgItemProvider` | Provide item definitions |
| `LrgSceneProvider` | Provide scene objects |
| `LrgDialogProvider` | Provide dialog trees |
| `LrgQuestProvider` | Provide quest definitions |
| `LrgAIProvider` | Provide behavior tree node GTypes |
| `LrgCommandProvider` | Provide console commands |
| `LrgLocaleProvider` | Provide localization data |

### Derivable Type Hierarchies

These are base classes designed for subclassing. Override virtual methods to customize behavior.

```
GObject
│
├── LrgWidget (src/ui/lrg-widget.h)
│   │   vfuncs: draw(), measure(), handle_event()
│   └── LrgContainer (src/ui/lrg-container.h)
│           vfunc: layout_children()
│
├── LrgComponent (src/ecs/lrg-component.h)
│       vfuncs: attached(), detached(), update(delta)
│
├── LrgBTNode (src/ai/lrg-bt-node.h)
│   │   vfuncs: tick(blackboard, delta), reset(), abort()
│   ├── LrgBTComposite (src/ai/lrg-bt-composite.h)
│   └── LrgBTDecorator (src/ai/lrg-bt-decorator.h)
│
├── LrgCamera (src/graphics/lrg-camera.h)
│   │   vfuncs: begin(), end(), world_to_screen(), screen_to_world()
│   ├── LrgCamera2D (src/graphics/lrg-camera2d.h)
│   └── LrgCamera3D (src/graphics/lrg-camera3d.h)
│
├── LrgLight2D (src/lighting/lrg-light2d.h)
│       vfuncs: render(), is_visible(), update(delta), calculate_shadows()
│
├── LrgWeatherEffect (src/weather/lrg-weather-effect.h)
│       vfuncs: activate(), deactivate(), update(delta), render(), set_intensity()
│
├── LrgTransition (src/transition/lrg-transition.h)
│       vfuncs: initialize(), start(), update(delta), render(), reset()
│
├── LrgPostEffect (src/postprocess/lrg-post-effect.h)
│       vfuncs: initialize(), shutdown(), apply(), resize(), get_name()
│
├── LrgTrigger2D (src/trigger2d/lrg-trigger2d.h)
│       vfuncs: test_point(), get_bounds(), get_shape()
│
├── LrgTweenBase (src/tween/lrg-tween-base.h)
│       vfuncs: start(), stop(), pause(), resume(), update(delta), reset()
│
├── LrgAnalyticsBackend (src/analytics/lrg-analytics-backend.h)
│       vfuncs: send_event(), flush(), is_enabled()
│
├── LrgProceduralAudio (src/audio/lrg-procedural-audio.h)
│       vfunc: generate(buffer, frame_count)
│
├── LrgAchievement (src/achievement/lrg-achievement.h)
│       vfuncs: check_unlock(), on_unlocked()
│
├── LrgShape (src/shapes/lrg-shape.h)
│   ├── LrgShape2D (src/shapes/lrg-shape2d.h)
│   └── LrgShape3D (src/shapes/lrg-shape3d.h)
│
├── LrgGameState (src/gamestate/lrg-game-state.h)
│       vfuncs: enter(), exit(), update(delta)
│
└── LrgIKSolver (src/animation/lrg-ik-solver.h)
        vfunc: solve()
```

### Singleton Managers

Access via `*_get_default()`. These are application-wide managers.

| Manager | Location | Access Function | Purpose |
|---------|----------|-----------------|---------|
| `LrgEngine` | `src/core/` | `lrg_engine_get_default()` | Central engine hub, subsystem coordination |
| `LrgAudioManager` | `src/audio/` | `lrg_audio_manager_get_default()` | Audio playback, volume channels |
| `LrgInputManager` | `src/input/` | `lrg_input_manager_get_default()` | Keyboard, mouse, gamepad input |
| `LrgTheme` | `src/ui/` | `lrg_theme_get_default()` | UI theming and styling |
| `LrgSaveManager` | `src/save/` | `lrg_save_manager_get_default()` | Save/load game management |
| `LrgModManager` | `src/mod/` | `lrg_mod_manager_get_default()` | Mod loading and lifecycle |
| `LrgEconomyManager` | `src/economy/` | `lrg_economy_manager_get_default()` | Economy/trading simulation |
| `LrgLocalization` | `src/i18n/` | `lrg_localization_get_default()` | I18N and localization |
| `LrgTweenManager` | `src/tween/` | `lrg_tween_manager_get_default()` | Tween update coordination |
| `LrgTransitionManager` | `src/transition/` | `lrg_transition_manager_get_default()` | Scene transitions |
| `LrgWeatherManager` | `src/weather/` | `lrg_weather_manager_get_default()` | Weather effects, day/night |
| `LrgLightingManager` | `src/lighting/` | `lrg_lighting_manager_get_default()` | 2D lighting system |
| `LrgTriggerManager` | `src/trigger2d/` | `lrg_trigger_manager_get_default()` | 2D trigger zones |
| `LrgAchievementManager` | `src/achievement/` | `lrg_achievement_manager_get_default()` | Local achievements |
| `LrgAnalytics` | `src/analytics/` | `lrg_analytics_get_default()` | Event tracking, telemetry |
| `LrgConsent` | `src/analytics/` | `lrg_consent_get_default()` | GDPR consent management |
| `LrgPhotoMode` | `src/photomode/` | `lrg_photo_mode_get_default()` | Photo mode, screenshots |
| `LrgDemoManager` | `src/demo/` | `lrg_demo_manager_get_default()` | Demo content gating |
| `LrgFontManager` | `src/text/` | `lrg_font_manager_get_default()` | Font loading, fallbacks |
| `LrgProfiler` | `src/debug/` | `lrg_profiler_get_default()` | Performance profiling |
| `LrgDebugConsole` | `src/debug/` | `lrg_debug_console_get_default()` | Debug command console |
| `LrgDebugOverlay` | `src/debug/` | `lrg_debug_overlay_get_default()` | Debug visualization |
| `LrgInspector` | `src/debug/` | `lrg_inspector_get_default()` | Object inspection |
| `LrgCrashReporter` | `src/crash/` | `lrg_crash_reporter_get_default()` | Crash capture/reporting |
| `LrgTutorialManager` | `src/tutorial/` | `lrg_tutorial_manager_get_default()` | Tutorial progression |

## When to Subclass

### Custom UI Widgets

Subclass `LrgWidget` or `LrgContainer`:

```c
G_DECLARE_FINAL_TYPE (MyWidget, my_widget, MY, WIDGET, LrgWidget)

static void
my_widget_draw (LrgWidget *widget)
{
    /* Custom rendering */
}

static void
my_widget_class_init (MyWidgetClass *klass)
{
    LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);
    widget_class->draw = my_widget_draw;
}
```

### Custom ECS Components

Subclass `LrgComponent`:

```c
G_DECLARE_FINAL_TYPE (HealthComponent, health_component, GAME, HEALTH_COMPONENT, LrgComponent)

static void
health_component_update (LrgComponent *component, gfloat delta)
{
    HealthComponent *self = GAME_HEALTH_COMPONENT (component);
    /* Regeneration, poison damage, etc. */
}

static void
health_component_class_init (HealthComponentClass *klass)
{
    LrgComponentClass *comp_class = LRG_COMPONENT_CLASS (klass);
    comp_class->update = health_component_update;
}
```

### Custom AI Behaviors

Subclass `LrgBTNode` (or `LrgBTLeaf` for actions):

```c
static LrgBTStatus
patrol_node_tick (LrgBTNode *node, LrgBlackboard *bb, gfloat delta)
{
    /* Check patrol waypoints, move agent */
    return LRG_BT_STATUS_RUNNING;
}
```

### Custom Audio Synthesis

Subclass `LrgProceduralAudio`:

```c
static void
synth_generate (LrgProceduralAudio *audio, gfloat *buffer, gint frames)
{
    /* Fill buffer with generated samples */
}
```

### Custom Post-Processing Effects

Subclass `LrgPostEffect`:

```c
static void
sepia_effect_apply (LrgPostEffect *effect)
{
    /* Apply sepia shader to render target */
}
```

### Custom Weather Effects

Subclass `LrgWeatherEffect`:

```c
static void
sandstorm_render (LrgWeatherEffect *effect)
{
    /* Render sand particles */
}
```

### Custom Achievements

Subclass `LrgAchievement` for complex unlock conditions:

```c
static gboolean
speedrun_check_unlock (LrgAchievement *achievement)
{
    /* Check if level completed under time limit */
    return elapsed_time < 300.0f;
}
```

## When to Implement Interfaces

### LrgSaveable - For Persistent State

Implement on objects that need save/load support:

```c
static const gchar *
my_object_get_save_id (LrgSaveable *saveable)
{
    return "my-unique-id";
}

static gboolean
my_object_save (LrgSaveable *saveable, LrgSaveContext *ctx, GError **error)
{
    MyObject *self = MY_OBJECT (saveable);
    lrg_save_context_write_int (ctx, "health", self->health);
    return TRUE;
}

static gboolean
my_object_load (LrgSaveable *saveable, LrgSaveContext *ctx, GError **error)
{
    MyObject *self = MY_OBJECT (saveable);
    self->health = lrg_save_context_read_int (ctx, "health");
    return TRUE;
}
```

### LrgModable - For Mod Entry Points

Implement as your mod's main class:

```c
static void
my_mod_init (LrgModable *modable, LrgEngine *engine)
{
    LrgRegistry *registry = lrg_engine_get_registry (engine);
    lrg_registry_register (registry, "custom-enemy", MY_TYPE_ENEMY);
}
```

### LrgDrawable - For Custom Renderables

Implement on objects that draw themselves:

```c
static void
my_entity_draw (LrgDrawable *drawable)
{
    MyEntity *self = MY_ENTITY (drawable);
    grl_draw_texture (self->texture, self->x, self->y, WHITE);
}
```

### LrgShadowCaster - For 2D Shadow Casting

Implement on objects that cast shadows:

```c
static GPtrArray *
wall_get_edges (LrgShadowCaster *caster)
{
    /* Return array of LrgEdge representing shadow-casting edges */
}
```

### LrgDemoGatable - For Demo Content Gating

Implement on content that should be restricted in demo mode:

```c
static const gchar *
level_get_content_id (LrgDemoGatable *gatable)
{
    return "level-5";  /* Unique content identifier */
}

static gboolean
level_is_demo_content (LrgDemoGatable *gatable)
{
    Level *self = GAME_LEVEL (gatable);
    return self->level_number <= 3;  /* First 3 levels in demo */
}
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
- `LRG_ANALYTICS_ERROR` - Analytics errors
- `LRG_ACHIEVEMENT_ERROR` - Achievement errors
- `LRG_PHOTO_MODE_ERROR` - Photo mode errors
- `LRG_WORKSHOP_ERROR` - Steam Workshop errors
- `LRG_DEMO_ERROR` - Demo mode errors
- `LRG_VR_ERROR` - VR errors

## Files to Reference for Patterns

### GObject Patterns

| Pattern | File |
|---------|------|
| Derivable GObject with signals | `src/core/lrg-engine.h/.c` |
| Derivable GObject with virtual method | `src/audio/lrg-procedural-audio.h/.c` |
| Final GObject with properties | `src/core/lrg-registry.h/.c` |
| GObject wrapper for graylib type | `src/audio/lrg-wave-data.h/.c` |
| Interface definition (`G_DECLARE_INTERFACE`) | `src/vr/lrg-vr-service.h/.c` |
| Interface implementation | `src/vr/lrg-vr-stub.h/.c` |
| Multiple interfaces on one class | `src/achievement/lrg-achievement-manager.c` |
| Singleton manager pattern | `src/analytics/lrg-analytics.h/.c` |

### Architecture Patterns

| Pattern | File |
|---------|------|
| Widget hierarchy (UI) | `src/ui/lrg-widget.h`, `src/ui/lrg-container.h` |
| Component hierarchy (ECS) | `src/ecs/lrg-component.h` |
| Behavior tree nodes (AI) | `src/ai/lrg-bt-node.h`, `src/ai/lrg-bt-composite.h` |
| Camera hierarchy | `src/graphics/lrg-camera.h`, `src/graphics/lrg-camera3d.h` |
| Effect base classes | `src/postprocess/lrg-post-effect.h`, `src/weather/lrg-weather-effect.h` |
| Transition base class | `src/transition/lrg-transition.h` |
| Trigger base class | `src/trigger2d/lrg-trigger2d.h` |
| Backend abstraction | `src/analytics/lrg-analytics-backend.h` |

### System Patterns

| Pattern | File |
|---------|------|
| Resource pack loading | `src/core/lrg-asset-pack.h/.c` |
| Async with libdex | `src/core/lrg-data-loader.c` |
| Quaternion/3D rotation | `src/graphics/lrg-camera3d.h/.c` |
| ECS transform component | `src/ecs/components/lrg-transform-component.h/.c` |
| Content gating (demo) | `src/demo/lrg-demo-gatable.h/.c` |
| GDPR consent handling | `src/analytics/lrg-consent.h/.c` |
| Mod provider interfaces | `src/mod/lrg-providers.h` |
| Steam integration | `src/steam/lrg-steam-client.h/.c` |

### Testing Patterns

| Pattern | File |
|---------|------|
| GTest with fixtures | `tests/test-registry.c` |
| Headless-safe tests | `tests/test-procedural-audio.c` |
| Skip macros for CI | `tests/test-graphics.c` |

### Build System

| Pattern | File |
|---------|------|
| Root Makefile | `Makefile` |
| Build configuration | `config.mk` |
| Build rules and helpers | `rules.mk` |
| pkg-config template | `libregnum.pc.in` |

### Dependencies

| Pattern | File |
|---------|------|
| graylib patterns | `deps/graylib/src/scene/grl-entity.h` |
| yaml-glib patterns | `deps/yaml-glib/src/yaml-gobject.h` |

## Implementation Status

The library is feature-complete and ready for commercial game development.

### Library Statistics

| Metric | Count |
|--------|-------|
| Modules | 55+ |
| Header files | 310+ |
| GObject interfaces | 18 |
| Derivable types | 76 |
| Singleton managers | 25 |
| Final types | 182+ |
| Test files | 70+ |

### Core Systems
- [x] Engine singleton, Registry, DataLoader, AssetManager, AssetPack
- [x] Settings/Options system (Graphics, Audio, Controls, Gameplay)
- [x] Game state management (GameState, MainMenu, PauseMenu, LoadingScreen)
- [x] Crash reporting (CrashReporter, CrashDialog)

### Graphics & Rendering
- [x] Window, Renderer, Camera hierarchy (7 camera types)
- [x] Particle system (ParticleEmitter, ParticleSystem, ParticleForce)
- [x] Post-processing pipeline (PostProcessor, PostEffect, Bloom, ColorGrade, FXAA)
- [x] 2D lighting system (Light2D, PointLight2D, SpotLight2D, ShadowMap, Lightmap)
- [x] Weather system (Rain, Snow, Fog, Lightning, DayNightCycle, WeatherManager)
- [x] Video playback (VideoPlayer, VideoTexture, VideoSubtitles)

### Animation & Effects
- [x] Animation state machine (Skeleton, Animator, AnimationStateMachine, IKSolver)
- [x] Tweening/easing (Easing, Tween, TweenSequence, TweenParallel, TweenManager)
- [x] Scene transitions (Fade, Wipe, Dissolve, Slide, Zoom, Shader transitions)

### Game Systems
- [x] ECS (World, GameObject, Component, Transform, Sprite, Collider, Animator)
- [x] Input handling (Keyboard, Mouse, Gamepad, InputMap/Action/Binding)
- [x] UI widgets (8 widgets, 3 layouts, Theme, Canvas)
- [x] Tilemap, Dialog, Inventory, Quest systems
- [x] Save/Load, AI (Behavior Trees), Pathfinding (A*)
- [x] 2D trigger system (TriggerRect, TriggerCircle, TriggerPolygon, TriggerManager)
- [x] Tutorial system (TutorialStep, Tutorial, TutorialManager, Highlight)
- [x] Texture atlas / sprite sheet tools (AtlasPacker, SpriteSheet, NineSlice)

### Genre-Specific Systems
- [x] Economy/Resource system (Resource, Producer, Consumer, Market, EconomyManager)
- [x] Building/Placement system (BuildingDef, PlacementSystem, BuildGrid)
- [x] Vehicle/Driving system (Vehicle, VehicleController, Wheel, TrafficAgent, Road)
- [x] Idle game support (IdleCalculator, BigNumber, Prestige, UnlockTree, Automation)

### Platform Integration
- [x] Steam SDK integration (SteamClient, Cloud, Achievements, Leaderboards, Workshop)
- [x] Analytics/telemetry (Analytics, AnalyticsEvent, Consent, AnalyticsBackend)
- [x] Local achievement system (Achievement, AchievementProgress, AchievementManager)
- [x] Photo mode (Screenshot, PhotoCameraController, PhotoMode)
- [x] Demo support (DemoGatable interface, DemoManager)
- [x] VR support (VRService interface, VRStub, VRComfort)
- [x] Accessibility features (colorblind modes, screen reader, subtitle manager)
- [x] Windows cross-compilation support

### Infrastructure
- [x] I18N/Localization, Networking (NetServer, NetClient)
- [x] Mod system (ModManifest, Mod, ModLoader, ModManager, Provider interfaces)
- [x] Scripting (Python, GJS backends)
- [x] Debug tools (Profiler, DebugConsole, DebugOverlay, Inspector)
- [x] Rich text / font improvements (RichText, TextEffect, FontManager)

### Testing
- [x] 70+ test files covering all modules
- [x] Headless environment support (SKIP_IF_NO_DISPLAY macro)
- [x] GTest with TAP output
- [x] CI-friendly skip macros for hardware-dependent tests
