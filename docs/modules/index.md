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
