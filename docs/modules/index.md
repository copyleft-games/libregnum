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

## Planned Modules

### Phase 1: Basic Game Systems

Planned for development:

- **ECS Module** - Entity-Component-System architecture
- **Input Module** - Keyboard, mouse, gamepad handling
- **UI Module** - Widget system with theming
- **Tilemap Module** - 2D tile-based map rendering

### Phase 2: Content and Gameplay

- **Dialog Module** - Branching dialogue trees
- **Inventory Module** - Item management and equipment
- **Quest Module** - Quest objectives and tracking
- **Save Module** - Save/load serialization
- **Audio Module** - Sound effects and music

### Phase 3: Advanced Systems

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
| ECS | 1 | Planned | GameObject, Component, World |
| Input | 1 | Planned | InputMap, InputAction, InputBinding |
| UI | 1 | Planned | Widget, Container, Button, Label |
| Tilemap | 1 | Planned | Tileset, Tilemap, TilemapLayer |
| Dialog | 2 | Planned | DialogTree, DialogNode, DialogRunner |
| Inventory | 2 | Planned | ItemDef, ItemStack, Inventory, Equipment |
| Quest | 2 | Planned | QuestDef, QuestInstance, QuestLog |
| Save | 2 | Planned | SaveGame, SaveManager, SaveContext |
| Audio | 2 | Planned | SoundBank, MusicTrack, AudioManager |
| AI | 3 | Planned | BehaviorTree, BTNode, Blackboard |
| Pathfinding | 3 | Planned | NavGrid, Pathfinder, Path |
| Physics | 3 | Planned | RigidBody, PhysicsWorld, CollisionInfo |
| I18N | 3 | Planned | Locale, Localization |
| Networking | 3 | Planned | NetPeer, NetServer, NetClient |
| 3D World | 3 | Planned | Level3D, Portal, PortalSystem |
| Debug | 4 | Planned | Profiler, DebugConsole, DebugOverlay, Inspector |
| Mod | 4 | Planned | Mod, ModManager, ModManifest, ModLoader |

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
