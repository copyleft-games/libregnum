# Libregnum Documentation

Welcome to the Libregnum documentation. Libregnum is a GObject-based game engine library providing high-level systems for game development.

## Getting Started

- [Building](building.md) - How to build and install Libregnum
- [Quick Start](quickstart.md) - Get up and running quickly

## Core Concepts

### Engine Singleton

The `LrgEngine` is the central hub for all engine subsystems. It manages the lifecycle of the engine and provides access to all other systems.

```c
LrgEngine *engine = lrg_engine_get_default ();
lrg_engine_startup (engine, &error);

/* Game loop */
while (lrg_engine_is_running (engine))
{
    lrg_engine_update (engine, delta_time);
}

lrg_engine_shutdown (engine);
```

### Type Registry

The `LrgRegistry` maps string names to GTypes, enabling data-driven object creation:

```c
LrgRegistry *registry = lrg_engine_get_registry (engine);

/* Register your types */
lrg_registry_register (registry, "player", MY_TYPE_PLAYER);
lrg_registry_register (registry, "enemy", MY_TYPE_ENEMY);

/* Create objects by name */
GObject *player = lrg_registry_create (registry, "player",
                                       "name", "Hero",
                                       NULL);
```

### Data Loader

The `LrgDataLoader` loads GObjects from YAML files:

```c
LrgDataLoader *loader = lrg_engine_get_data_loader (engine);

/* Load a single file */
GObject *obj = lrg_data_loader_load_file (loader, "data/player.yaml", &error);

/* Load all files in a directory */
GList *objects = lrg_data_loader_load_directory (loader, "data/entities/",
                                                 TRUE, &error);
```

YAML format:
```yaml
type: player
name: "Hero"
health: 100
speed: 5.0
```

## API Reference

### Core Module

| Type | Description |
|------|-------------|
| `LrgEngine` | Engine singleton managing all subsystems |
| `LrgRegistry` | Type registry for data-driven instantiation |
| `LrgDataLoader` | YAML file loading with type resolution |

### Enumerations

| Enum | Description |
|------|-------------|
| `LrgEngineState` | Engine lifecycle states |
| `LrgEngineError` | Engine error codes |
| `LrgDataLoaderError` | Data loader error codes |

## Planned Modules

The following modules are planned for future releases:

### Phase 1
- **ECS** - Entity-Component-System with `LrgGameObject` and `LrgComponent`
- **Input** - Rebindable input actions and mappings
- **UI** - Widget-based UI framework
- **Tilemap** - 2D tile-based maps

### Phase 2
- **Dialog** - Branching dialog trees
- **Inventory** - Item definitions and equipment
- **Quest** - Quest objectives and tracking
- **Save** - Serialization and save management
- **Audio** - Sound and music management

### Phase 3
- **AI** - Behavior trees
- **Pathfinding** - A* on navigation grids
- **Physics** - Rigid body physics
- **I18N** - Localization
- **Net** - Client/server networking
- **World3D** - 3D level systems

### Phase 4
- **Debug** - Console, inspector, profiler
- **Mod** - Mod loading with dependencies

## Language Bindings

Libregnum generates GObject Introspection (GIR) files, enabling use from:

- **Python** via PyGObject
- **JavaScript** via GJS
- **Vala** native support
- **Lua** via lgi

Example (Python):
```python
import gi
gi.require_version('Libregnum', '1')
from gi.repository import Libregnum

engine = Libregnum.Engine.get_default()
engine.startup()
# ...
engine.shutdown()
```

## License

Libregnum is licensed under the **GNU Affero General Public License v3.0 or later** (AGPL-3.0-or-later).
