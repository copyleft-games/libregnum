---
title: Libregnum Documentation
layout: landing
---

# Libregnum Game Engine

Libregnum is a GObject-based game engine library built on top of [graylib](https://github.com/graylib/graylib) (raylib GObject wrapper) and [yaml-glib](https://gitlab.com/GNOME/yaml-glib) (YAML parsing with GObject serialization). It provides high-level game systems with a focus on data-driven design, extensibility, and modding support.

## Quick Navigation

- **[Getting Started](quickstart.md)** - Install and run your first program
- **[Architecture Overview](architecture.md)** - Understand the system design
- **[Core Concepts](concepts/index.md)** - Learn key game development concepts
- **[Module Documentation](modules/index.md)** - Detailed API reference
- **[API Classes Index](api/classes.md)** - All classes and types
- **[Building](building.md)** - Build and installation instructions

## Key Features

### Core Systems
- **Engine Singleton** - Central hub managing all subsystems
- **Type Registry** - Data-driven type mapping for YAML deserialization
- **Data Loader** - Load and deserialize YAML files to GObjects
- **Asset Manager** - Unified texture, font, sound, and music loading with caching

### Game Systems (Planned Phases)
- **ECS** - Entity-Component-System architecture for flexible gameplay
- **Input** - Keyboard, mouse, and gamepad input handling
- **UI** - Complete widget system with theming support
- **Tilemap** - 2D tilemap rendering and collision
- **Dialog** - Branching dialogue trees with conditions
- **Inventory** - Item management and equipment systems
- **Quest** - Quest definitions, objectives, and tracking
- **Save** - Game save/load serialization
- **Audio** - Sound effects and music management

### Advanced Features
- **AI** - Behavior trees, blackboards, and state machines
- **Pathfinding** - A* navigation grid and smooth path following
- **Physics** - 2D/3D rigid body simulation and collision
- **3D World** - 3D level support with portals and sectors
- **Networking** - Multiplayer networking (client/server)
- **Debug Tools** - Profiling, console, overlay, and inspector
- **Modding** - Complete mod system with dependencies
- **Localization** - Multi-language support with CLDR plural rules

## Getting Started Quickly

### Installation

```bash
# Build from source
make DEBUG=1
make test
make install PREFIX=$HOME/.local
```

### Minimal Example

```c
#include <libregnum.h>
#include <glib.h>

int
main (int argc, char *argv[])
{
    g_autoptr(GError) error = NULL;
    LrgEngine *engine;

    /* Get the engine singleton */
    engine = lrg_engine_get_default ();

    /* Start the engine */
    if (!lrg_engine_startup (engine, &error))
    {
        g_warning ("Failed to start engine: %s", error->message);
        return 1;
    }

    /* Game loop */
    while (lrg_engine_is_running (engine))
    {
        lrg_engine_update (engine, 0.016f); /* 60 FPS */
    }

    /* Shutdown */
    lrg_engine_shutdown (engine);

    return 0;
}
```

See the [Quickstart Guide](quickstart.md) for more detailed examples.

## Complete Examples

- **[3D Omnomagon](examples/omnomagon.md)** - A complete playable 3D Omnomagon game demonstrating data-driven design, type registration, YAML loading, 3D rendering, collision detection, and simple AI

## Core Concepts Overview

### Data-Driven Design

Libregnum heavily leverages YAML for configuration and data. The Registry and DataLoader work together to enable fully data-driven gameplay:

```yaml
# player.yaml
type: player
name: "Hero"
health: 100
mana: 50
```

### Type Registry

The Registry maps string names to GTypes, enabling YAML files to reference types without hardcoding:

```c
LrgRegistry *registry = lrg_engine_get_registry (engine);
lrg_registry_register (registry, "player", MY_TYPE_PLAYER);

/* Now load from YAML */
LrgDataLoader *loader = lrg_engine_get_data_loader (engine);
GObject *player = lrg_data_loader_load_file (loader, "player.yaml", &error);
```

### Asset Management

The Asset Manager provides unified access to game assets with caching and mod overlay support:

```c
LrgAssetManager *manager = lrg_engine_get_asset_manager (engine);

/* Add search paths (mods have higher priority) */
lrg_asset_manager_add_search_path (manager, "base/assets/");
lrg_asset_manager_add_search_path (manager, "mods/my-mod/assets/");

/* Load assets (searches in reverse priority order) */
GrlTexture *texture = lrg_asset_manager_load_texture (manager, "sprites/player.png", &error);
```

## Documentation Structure

### For Users
- [Quickstart Guide](quickstart.md) - Get up and running quickly
- [Core Concepts](concepts/index.md) - Understand how the engine works
- [Module Documentation](modules/index.md) - Detailed guides for each module

### For Developers
- [Architecture Overview](architecture.md) - System design and patterns
- [API Reference](api/classes.md) - Complete class documentation
- [Error Handling](concepts/error-handling.md) - Error patterns and recovery

## Module Structure

```
libregnum/
├── core/           # Engine, Registry, DataLoader, AssetManager (DONE)
├── ecs/            # Entity-Component-System (Phase 1)
├── input/          # Input handling (Phase 1)
├── ui/             # User interface widgets (Phase 1)
├── tilemap/        # 2D tilemap support (Phase 1)
├── dialog/         # Dialogue system (Phase 2)
├── inventory/      # Inventory and equipment (Phase 2)
├── quest/          # Quest management (Phase 2)
├── save/           # Save/load system (Phase 2)
├── audio/          # Sound and music (Phase 2)
├── ai/             # AI and behavior trees (Phase 3)
├── pathfinding/    # Navigation and pathfinding (Phase 3)
├── physics/        # Physics simulation (Phase 3)
├── i18n/           # Internationalization (Phase 3)
├── debug/          # Debug tools (Phase 4)
├── net/            # Networking (Phase 3)
├── world3d/        # 3D world support (Phase 3)
└── mod/            # Modding system (Phase 4)
```

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
# Game loop here...
engine.shutdown()
```

## Contributing

Libregnum is an AGPL-3.0-or-later project. Contributions are welcome!

## License

Libregnum is licensed under the **GNU Affero General Public License v3.0 or later** (AGPL-3.0-or-later).
