---
title: Module Documentation
---

# Libregnum Modules

Complete documentation for all Libregnum game engine modules and systems.

> **[Home](../index.md)** > Modules

## Completed Modules

### [Core Module](core/index.md) - Foundation

Essential engine systems completed in Phase 0:

- **[LrgEngine](core/engine.md)** - Central engine singleton managing lifecycle and subsystems
- **[LrgRegistry](core/registry.md)** - Type registry for data-driven object creation
- **[LrgDataLoader](core/data-loader.md)** - YAML file loading with GObject deserialization
- **[LrgAssetManager](core/asset-manager.md)** - Asset caching with mod overlay support

### [Graphics Module](graphics/index.md) - Window and Rendering

Window management, cameras, and rendering abstractions:

- **[LrgDrawable](graphics/index.md#lrgdrawable)** - Interface for renderable objects
- **[LrgWindow](graphics/index.md#lrgwindow)** - Abstract window base class
- **[LrgGrlWindow](graphics/index.md#lrggrlwindow)** - Graylib window implementation
- **[LrgCamera](graphics/index.md#lrgcamera)** - Abstract camera base class
- **[LrgCamera2D](graphics/index.md#lrgcamera2d)** - 2D camera for top-down/side-scrolling
- **[LrgCamera3D](graphics/index.md#lrgcamera3d)** - 3D camera for 3D games
- **[LrgRenderer](graphics/index.md#lrgrenderer)** - Render management and layer system

### [Particles Module](particles/index.md) - Particle Systems

GPU-efficient particle effects:

- **[LrgParticle](particles/particle.md)** - Individual particle state
- **[LrgParticlePool](particles/pool.md)** - Memory-efficient particle storage
- **[LrgParticleEmitter](particles/emitter.md)** - Particle spawning with shapes
- **[LrgParticleForce](particles/forces.md)** - Gravity, wind, attractors, turbulence
- **[LrgParticleSystem](particles/system.md)** - Complete particle effect system

### [Post-Processing Module](postprocess/index.md) - Visual Effects

Screen-space effects pipeline:

- **[LrgPostEffect](postprocess/post-effect.md)** - Base effect class
- **[LrgPostProcessor](postprocess/post-processor.md)** - Effect chain manager
- **[Effects](postprocess/effects.md)** - Bloom, vignette, color grade, FXAA, film grain, screen shake
- **[Accessibility](postprocess/accessibility.md)** - Colorblind simulation and correction

### [Animation Module](animation/index.md) - Skeletal Animation

Complete skeletal animation system:

- **[LrgSkeleton](animation/skeleton.md)** - Bone hierarchy and poses
- **[LrgAnimationClip](animation/clip.md)** - Keyframe animation data
- **[LrgAnimator](animation/animator.md)** - Simple playback with crossfading
- **[LrgAnimationStateMachine](animation/state-machine.md)** - Parameter-driven state machine
- **[LrgBlendTree](animation/blend-tree.md)** - 1D/2D parametric blending
- **[LrgAnimationLayer](animation/layers.md)** - Layered animation with bone masks
- **[LrgIKSolver](animation/ik-solver.md)** - FABRIK, CCD, two-bone, look-at

### [Rich Text Module](text/index.md) - Styled Text

BBCode-style text markup:

- **[LrgFontManager](text/font-manager.md)** - Font loading and caching
- **[LrgRichText](text/rich-text.md)** - Markup parsing and rendering
- **[Markup Reference](text/markup-reference.md)** - Tag documentation
- **[LrgTextEffect](text/effects.md)** - Shake, wave, rainbow, typewriter effects

### [Video Module](video/index.md) - Video Playback

FFmpeg-based video playback:

- **[LrgVideoPlayer](video/video-player.md)** - Video playback with audio
- **[Subtitles](video/subtitles.md)** - SRT/VTT subtitle support
- **[FFmpeg Setup](video/ffmpeg-setup.md)** - Dependency installation

### [Tween Module](tween/index.md) - Property Animation

Smooth property animation with easing:

- **[LrgEasing](tween/index.md#easing-functions)** - 30+ easing functions
- **[LrgTween](tween/index.md#quick-start)** - Single property animation
- **[LrgTweenSequence](tween/index.md#sequences-and-parallel)** - Chain tweens together
- **[LrgTweenParallel](tween/index.md#sequences-and-parallel)** - Run tweens simultaneously
- **[LrgTweenManager](tween/index.md#quick-start)** - Active tween coordinator

### [Transition Module](transition/index.md) - Scene Transitions

Smooth scene changes:

- **[LrgTransition](transition/index.md)** - Base transition class
- **[LrgFadeTransition](transition/index.md#fade-transition)** - Fade to/from color
- **[LrgWipeTransition](transition/index.md#wipe-transition)** - Directional wipe
- **[LrgDissolveTransition](transition/index.md#dissolve-transition)** - Noise dissolve
- **[LrgSlideTransition](transition/index.md#slide-transition)** - Push/cover/reveal
- **[LrgTransitionManager](transition/index.md)** - Transition coordinator

### [2D Trigger Module](trigger2d/index.md) - Area Triggers

Event-based trigger zones:

- **[LrgTrigger2D](trigger2d/index.md)** - Base trigger class
- **[LrgTriggerRect](trigger2d/index.md#rectangle-trigger)** - Rectangle triggers
- **[LrgTriggerCircle](trigger2d/index.md#circle-trigger)** - Circle triggers
- **[LrgTriggerPolygon](trigger2d/index.md#polygon-trigger)** - Polygon triggers
- **[LrgTriggerManager](trigger2d/index.md)** - Trigger coordinator

### [Atlas Module](atlas/index.md) - Texture Atlases

Texture packing and sprite sheets:

- **[LrgTextureAtlas](atlas/index.md#texture-atlas)** - Packed texture atlas
- **[LrgSpriteSheet](atlas/index.md#sprite-sheet)** - Animation sprite sheets
- **[LrgNineSlice](atlas/index.md#nine-slice-9-patch)** - Scalable UI sprites
- **[LrgAtlasPacker](atlas/index.md#atlas-packer-build-tool)** - Build-time packer

### [Tutorial Module](tutorial/index.md) - Player Onboarding

Guided tutorials and hints:

- **[LrgTutorial](tutorial/index.md)** - Tutorial sequence
- **[LrgTutorialStep](tutorial/index.md#tutorial-steps)** - Individual instruction step
- **[LrgTutorialManager](tutorial/index.md)** - Tutorial coordinator
- **[LrgHighlight](tutorial/index.md#highlight-effects)** - UI element highlighting
- **[LrgInputPrompt](tutorial/index.md#input-prompts)** - Device-aware input hints

### [Weather Module](weather/index.md) - Dynamic Weather

Atmospheric weather effects:

- **[LrgRain](weather/index.md#rain)** - Rain particle effect
- **[LrgSnow](weather/index.md#snow)** - Snow particle effect
- **[LrgFog](weather/index.md#fog)** - Screen-space fog
- **[LrgLightning](weather/index.md#lightning)** - Lightning flashes
- **[LrgDayNightCycle](weather/index.md#daynight-cycle)** - Time-of-day system
- **[LrgWeatherManager](weather/index.md)** - Weather coordinator

### [2D Lighting Module](lighting/index.md) - Dynamic Lighting

2D lights and shadows:

- **[LrgLight2D](lighting/index.md)** - Base light class
- **[LrgPointLight2D](lighting/index.md#point-light)** - Radial point lights
- **[LrgSpotLight2D](lighting/index.md#spot-light)** - Directional cone lights
- **[LrgDirectionalLight2D](lighting/index.md#directional-light)** - Sun-like lights
- **[LrgShadowMap](lighting/index.md#shadow-map)** - Shadow rendering
- **[LrgLightmap](lighting/index.md#baked-lighting-lightmaps)** - Baked lighting
- **[LrgLightingManager](lighting/index.md)** - Lighting coordinator

### [Template Module](template/index.md) - Ready-to-Use Menu States & Engagement

Pre-built game state implementations:

- **[LrgTemplateMainMenuState](template/index.md#main-menu-state)** - Title screen with buttons
- **[LrgTemplatePauseMenuState](template/index.md#pause-menu-state)** - Pause overlay with audio ducking
- **[LrgTemplateSettingsMenuState](template/index.md#settings-menu-state)** - Tabbed settings interface
- **[LrgTemplateLoadingState](template/index.md#loading-state)** - Loading screen with progress
- **[LrgTemplateErrorState](template/index.md#error-state)** - Error recovery screen
- **[LrgTemplateConfirmationState](template/index.md#confirmation-state)** - Modal confirmation dialog

Engagement systems for player retention:

- **[LrgTemplateStatistics](template/index.md#statistics-tracking)** - Game statistics tracking
- **[LrgTemplateDailyRewards](template/index.md#daily-rewards-interface)** - Daily login rewards with streaks
- **[LrgTemplateDifficulty](template/index.md#dynamic-difficulty-interface)** - Dynamic difficulty adjustment

## Planned Modules

### Phase 1: Basic Game Systems (Implemented, docs pending)

- **ECS Module** - Entity-Component-System architecture
- **Input Module** - Keyboard, mouse, gamepad handling
- **UI Module** - Widget system with theming
- **Tilemap Module** - 2D tile-based map rendering

### Phase 2: Content and Gameplay (Implemented, docs pending)

- **Dialog Module** - Branching dialogue trees
- **Inventory Module** - Item management and equipment
- **Quest Module** - Quest objectives and tracking
- **Save Module** - Save/load serialization
- **Audio Module** - Sound effects and music

### Phase 3: Advanced Systems (Partial)

- **AI Module** - Behavior trees and decision making
- **Pathfinding Module** - A* navigation and path smoothing
- **Physics Module** - 2D/3D rigid body simulation
- **I18N Module** - Localization and translations
- **Networking Module** - Client/server multiplayer

### Phase 4: Engine Tools

- **Debug Module** - Profiler, console, inspector, overlay
- **Mod Module** - Complete modding system with dependencies
- **3D World Module** - 3D level support with portals and sectors

## Module Architecture

```
Phase 4   mod/           debug/
          |              |
Phase 3   ai/  pathfinding/  physics/  i18n/  net/  world3d/
          |              |              |      |     |
Phase 2   dialog/  inventory/  quest/  save/  audio/
          |              |              |      |
Phase 1   ecs/      input/        ui/      tilemap/
          |              |              |
Phase 0   core/ (engine, registry, loader, assets)
          |
          glib/gobject/graylib/yaml-glib/libdex
```

## Quick Links

### For Game Developers

- **[Quickstart Guide](../quickstart.md)** - Get started immediately
- **[Core Module](core/index.md)** - Essential APIs you'll use first
- **[Concepts](../concepts/index.md)** - Understand design patterns

### For Advanced Users

- **[Architecture Overview](../architecture.md)** - System design
- **[API Classes Index](../api/classes.md)** - Complete type reference
- **[Error Handling](../concepts/error-handling.md)** - Error patterns

### For System Design

- **[Module Dependency Graph](#module-architecture)** - How modules relate
- **[Engine Lifecycle](../concepts/engine-lifecycle.md)** - Engine startup/shutdown
- **[Type Registry](../concepts/type-registry.md)** - Data-driven design

## Module Status

| Module | Phase | Status | Key Types |
|--------|-------|--------|-----------|
| Core | 0 | Complete | Engine, Registry, DataLoader, AssetManager |
| Graphics | 0 | Complete | Window, GrlWindow, Camera, Camera2D, Camera3D, Renderer, Drawable |
| Particles | 3 | Complete | Particle, ParticlePool, ParticleEmitter, ParticleForce, ParticleSystem |
| PostProcess | 3 | Complete | PostEffect, PostProcessor, Bloom, Vignette, ColorGrade, FXAA |
| Animation | 3 | Complete | Skeleton, Bone, AnimationClip, Animator, StateMachine, BlendTree, IKSolver |
| Text | 3 | Complete | FontManager, RichText, TextSpan, TextEffect |
| Video | 3 | Complete | VideoPlayer, VideoTexture, SubtitleTrack, VideoSubtitles |
| ECS | 1 | Implemented | GameObject, Component, World |
| Input | 1 | Implemented | InputMap, InputAction, InputBinding |
| UI | 1 | Implemented | Widget, Container, Button, Label |
| Tilemap | 1 | Implemented | Tileset, Tilemap, TilemapLayer |
| Dialog | 2 | Implemented | DialogTree, DialogNode, DialogRunner |
| Inventory | 2 | Implemented | ItemDef, ItemStack, Inventory, Equipment |
| Quest | 2 | Implemented | QuestDef, QuestInstance, QuestLog |
| Save | 2 | Implemented | SaveGame, SaveManager, SaveContext |
| Audio | 2 | Implemented | SoundBank, MusicTrack, AudioManager |
| AI | 3 | Implemented | BehaviorTree, BTNode, Blackboard |
| Pathfinding | 3 | Implemented | NavGrid, Pathfinder, Path |
| Physics | 3 | Implemented | RigidBody, PhysicsWorld, CollisionInfo |
| I18N | 3 | Implemented | Locale, Localization |
| Networking | 3 | Implemented | NetPeer, NetServer, NetClient |
| 3D World | 3 | Implemented | Level3D, Portal, PortalSystem |
| Debug | 4 | Implemented | Profiler, DebugConsole, DebugOverlay, Inspector |
| Mod | 4 | Implemented | Mod, ModManager, ModManifest, ModLoader |
| DLC | 4 | Complete | LrgDlc, LrgDlcOwnership, ExpansionPack, QuestPack, ItemPack, etc. |
| Tween | 4 | Complete | Easing, Tween, TweenSequence, TweenParallel, TweenManager |
| Transition | 4 | Complete | FadeTransition, WipeTransition, DissolveTransition, TransitionManager |
| Trigger2D | 4 | Complete | TriggerRect, TriggerCircle, TriggerPolygon, TriggerManager |
| Atlas | 4 | Complete | TextureAtlas, SpriteSheet, NineSlice, AtlasPacker |
| Tutorial | 4 | Complete | Tutorial, TutorialStep, TutorialManager, Highlight, InputPrompt |
| Weather | 4 | Complete | Rain, Snow, Fog, Lightning, DayNightCycle, WeatherManager |
| Lighting | 4 | Complete | PointLight2D, SpotLight2D, DirectionalLight2D, ShadowMap, LightingManager |
| Template | 4 | Complete | MainMenuState, PauseMenuState, SettingsMenuState, LoadingState, ErrorState, ConfirmationState, Statistics, DailyRewards, Difficulty |

## Building with Multiple Modules

Once available, use modules in concert:

```c
/* Core: Setup engine */
LrgEngine *engine = lrg_engine_get_default ();
lrg_engine_startup (engine, &error);

/* ECS: Create game world */
LrgWorld *world = lrg_world_new ();

/* Input: Handle player input */
LrgInputMap *input = lrg_engine_get_input_map (engine);

/* Tilemap: Load level */
LrgTilemap *map = lrg_data_loader_load_file (loader, "level1.yaml", &error);

/* Physics: Simulate */
LrgPhysicsWorld *physics = lrg_world_get_physics (world);

/* Audio: Play sounds */
LrgAudioManager *audio = lrg_engine_get_audio_manager (engine);

/* Save: Persist game state */
LrgSaveManager *save = lrg_engine_get_save_manager (engine);
```

## Module Guidelines

### For Users

- Master the Core module first
- Use DataLoader for YAML-based game data
- Use the Registry for type management
- Understand module dependencies before using

### For Contributors

- Keep modules independent where possible
- Document virtual methods for extension
- Follow GObject conventions
- Write unit tests for new code
- Update documentation with API changes

## See Also

- **[Core Module](core/index.md)** - Start here
- **[Quickstart Guide](../quickstart.md)** - Getting started
- **[Architecture Overview](../architecture.md)** - System design
- **[Contributing Guide](../../CONTRIBUTING.md)** - How to help
