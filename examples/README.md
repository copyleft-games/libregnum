# Libregnum Examples

This directory contains example programs demonstrating various libregnum features.

## Building Examples

```bash
# From the libregnum root directory
make examples

# Or from the examples directory
cd examples
make
```

## Running Examples

All examples are built to `build/release/examples/`. Run them from the `examples/` directory:

```bash
cd examples
./build/release/examples/game-omnomagon
```

## Example Programs

### Games

| Example | Description | Documentation |
|---------|-------------|---------------|
| `game-omnomagon` | 3D Omnomagon game with data-driven design | [docs](../docs/examples/omnomagon.org) |
| `game-taco-racing` | Simple racing game | - |
| `game-chocolate-chip-clicker` | Clicker/idle game demo | - |
| `game-tuktuk-derby` | Vehicle physics demo | - |
| `game-micro-tycoon` | Economy/tycoon systems | - |
| `game-dungeon-torch` | 2D lighting and shadows | - |
| `game-procgen-dungeon` | Procedural dungeons (BSP, caves, WFC, drunkard, noise) from a YAML recipe, autotiled, with A* monsters | [docs](../docs/modules/procgen/index.org) |
| `game-meta-arcade` | Meta-game/arcade collection | - |
| `game-effects-gallery` | Visual effects showcase | - |
| `game-settings-demo` | Settings/options menu demo | - |

### DLC and Modding

| Example | Description | Documentation |
|---------|-------------|---------------|
| `game-dlc-store` | DLC ownership, trials, store integration | [docs](../docs/examples/dlc-store.org) |
| `game-creature-collector` | Native GModule DLC loading | [docs](../docs/examples/native-dlc.org) |

### Scripting

| Example | Description | Documentation |
|---------|-------------|---------------|
| `scripted-lua-game` | Lua scripting backend | [docs](../docs/examples/scripting-basics.org) |
| `scripted-python-game` | Python scripting backend | [docs](../docs/examples/scripting-basics.org) |
| `scripted-python-gobject-game` | PyGObject scripting | [docs](../docs/examples/scripting-basics.org) |
| `scripted-gjs-game` | GJS (JavaScript) scripting | [docs](../docs/examples/scripting-basics.org) |

### Simulation and Netcode

| Example | Description | Documentation |
|---------|-------------|---------------|
| `replay-demo` | Headless: records a deterministic physics game to a replay file, plays it back, then runs it as two rollback peers over a lossy simulated network | [docs](../docs/modules/replay/index.org) |

### User Interface

| Example | Description | Documentation |
|---------|-------------|---------------|
| `ui-showcase` | Every retained UI widget across 17 pages: menus, glass, motion, data, pickers, editor, graph, timeline, dock | [docs](../docs/examples/ui-showcase.org) |
| `ui-studio` | Tool app from YAML: menu bar, dock, 3D viewport animated by a timeline, node graph, script editor and console | [docs](../docs/examples/ui-studio.org) |
| `ui-theme-editor` | Live theme editor with palette generation and contrast checks | [docs](../docs/examples/ui-theme-editor.org) |
| `game-ui-menu` | Gamepad-navigable game menu over a live scene, themed from YAML | [docs](../docs/examples/ui-game-menu.org) |

### Asset Rendering

| Example | Description |
|---------|-------------|
| `render-yaml-santa` | YAML-driven sprite rendering |
| `render-yaml-taco-truck` | YAML-driven vehicle rendering |

## Data Files

Example data files are located in:

- `data/` - General game data (sprites, levels, etc.)
- `data/dlcs/` - Sample DLC manifest files for `game-dlc-store`
- `data/ui/` - Declarative UI documents (`studio.yaml`, `hud.yaml`, `pages.yaml`)
- `mods/` - Sample native mods for `game-creature-collector`

## Platform Support

Most examples work on both Linux and Windows. Exceptions:

- **Scripted examples** (`scripted-*`) - Linux only (scripting backends not available on Windows)
- **Native mods** (`mods/`) - Linux only (GModule loading)

## Controls

Most examples use common controls:

| Key | Action |
|-----|--------|
| ESC | Exit |
| WASD / Arrow Keys | Movement |
| Space / Enter | Action/Select |

See individual example documentation for specific controls.
