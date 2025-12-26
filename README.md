# Libregnum

A GObject-based game engine library built on top of [graylib](https://github.com/example/graylib) and [yaml-glib](https://github.com/example/yaml-glib).

**License:** AGPL-3.0-or-later

## Overview

Libregnum provides high-level game engine systems with a focus on:

- **Data-Driven Design**: Load game objects from YAML files with automatic type resolution
- **Modular Architecture**: ECS-based component system, UI framework, dialog trees, inventory, quests, AI behavior trees, and more
- **Extensibility**: Abstract base classes and interfaces for game-specific customization
- **Mod Support**: Full modding system with dependency resolution and semantic versioning
- **GObject Introspection**: Python and other language bindings via GIR

## Dependencies

### Build Dependencies (Fedora)

```bash
sudo dnf install \
    gcc \
    make \
    pkgconf \
    glib2-devel \
    gobject-introspection-devel \
    libdex-devel \
    libyaml-devel \
    json-glib-devel
```

### Submodule Dependencies

The following are included as git submodules in `deps/`:

- **graylib** - GObject wrapper around raylib for rendering
- **yaml-glib** - YAML parsing with GObject serialization

Initialize submodules:

```bash
git submodule update --init --recursive
```

## Building

### Basic Build

```bash
make
```

This will:
1. Build submodule dependencies (graylib, yaml-glib)
2. Build the Libregnum library (static and shared)
3. Generate GObject Introspection files

### Build Options

```bash
make DEBUG=1        # Debug build with symbols
make test           # Build and run unit tests
make examples       # Build example programs
make install        # Install to /usr/local (or set PREFIX=)
make help           # Show all available targets
```

### Configuration

Edit `config.mk` to customize:

- `PREFIX` - Installation prefix (default: `/usr/local`)
- `BUILD_STATIC` - Build static library (default: 1)
- `BUILD_SHARED` - Build shared library (default: 1)
- `BUILD_GIR` - Generate GIR files (default: 1)
- `BUILD_TESTS` - Enable test suite (default: 1)

## Quick Start

### C Example

```c
#include <libregnum.h>

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
        g_printerr ("Failed to start engine: %s\n", error->message);
        return 1;
    }

    /* Register custom types */
    LrgRegistry *registry = lrg_engine_get_registry (engine);
    lrg_registry_register (registry, "player", MY_TYPE_PLAYER);

    /* Load game data */
    LrgDataLoader *loader = lrg_engine_get_data_loader (engine);
    g_autoptr(GObject) player = lrg_data_loader_load_file (loader,
                                                           "data/player.yaml",
                                                           &error);

    /* Game loop */
    while (lrg_engine_is_running (engine))
    {
        lrg_engine_update (engine, 1.0f / 60.0f);
    }

    /* Shutdown */
    lrg_engine_shutdown (engine);

    return 0;
}
```

### YAML Data File

```yaml
# data/player.yaml
type: player
name: "Hero"
health: 100
speed: 5.0
```

### Python Example (via GIR)

```python
import gi
gi.require_version('Libregnum', '1')
from gi.repository import Libregnum

engine = Libregnum.Engine.get_default()
engine.startup()

registry = engine.get_registry()
# Register types...

loader = engine.get_data_loader()
player = loader.load_file("data/player.yaml")

engine.shutdown()
```

## Architecture

### Core Systems

- **LrgEngine** - Central singleton managing all subsystems
- **LrgRegistry** - Maps type names to GTypes for data-driven instantiation
- **LrgDataLoader** - Loads GObjects from YAML files

### Planned Modules

| Module | Description |
|--------|-------------|
| ECS | Entity-Component-System with LrgGameObject and LrgComponent |
| Input | Rebindable input actions and mappings |
| UI | Widget-based UI framework with layouts |
| Tilemap | 2D tile-based maps with layers |
| Dialog | Branching dialog trees |
| Inventory | Item definitions, stacks, and equipment |
| Quest | Quest objectives and tracking |
| Save | Serialization interface and save management |
| Audio | Sound banks and music management |
| AI | Behavior trees with composites, decorators, and leafs |
| Pathfinding | A* pathfinding on navigation grids |
| Physics | Rigid body physics |
| I18N | Localization support |
| Net | Client/server networking |
| World3D | 3D level loading and spatial optimization |
| Debug | Console, inspector, and profiler |
| Mod | Mod loading with dependency resolution |

## Testing

Run the test suite:

```bash
make test
```

Tests use the GLib testing framework (GTest).

## Documentation

Build API documentation (requires gi-docgen):

```bash
make docs
```

## Contributing

Contributions are welcome! Please ensure:

1. Code follows GNU89 C standard
2. All warnings are resolved (warnings are treated as errors)
3. GObject Introspection annotations are complete
4. Tests are added for new functionality
5. Documentation is updated

## License

This project is licensed under the **GNU Affero General Public License v3.0 or later**.

See [LICENSE](LICENSE) for the full license text.
