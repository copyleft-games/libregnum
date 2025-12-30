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
| `game-omnomagon` | 3D Omnomagon game with data-driven design | [docs](../docs/examples/omnomagon.md) |
| `game-taco-racing` | Simple racing game | - |
| `game-chocolate-chip-clicker` | Clicker/idle game demo | - |
| `game-tuktuk-derby` | Vehicle physics demo | - |
| `game-micro-tycoon` | Economy/tycoon systems | - |
| `game-dungeon-torch` | 2D lighting and shadows | - |
| `game-meta-arcade` | Meta-game/arcade collection | - |
| `game-effects-gallery` | Visual effects showcase | - |
| `game-settings-demo` | Settings/options menu demo | - |

### DLC and Modding

| Example | Description | Documentation |
|---------|-------------|---------------|
| `game-dlc-store` | DLC ownership, trials, store integration | [docs](../docs/examples/dlc-store.md) |
| `game-creature-collector` | Native GModule DLC loading | [docs](../docs/examples/native-dlc.md) |

### Scripting

| Example | Description | Documentation |
|---------|-------------|---------------|
| `scripted-lua-game` | Lua scripting backend | [docs](../docs/examples/scripting-basics.md) |
| `scripted-python-game` | Python scripting backend | [docs](../docs/examples/scripting-basics.md) |
| `scripted-python-gobject-game` | PyGObject scripting | [docs](../docs/examples/scripting-basics.md) |
| `scripted-gjs-game` | GJS (JavaScript) scripting | [docs](../docs/examples/scripting-basics.md) |

### Asset Rendering

| Example | Description |
|---------|-------------|
| `render-yaml-santa` | YAML-driven sprite rendering |
| `render-yaml-taco-truck` | YAML-driven vehicle rendering |

## Data Files

Example data files are located in:

- `data/` - General game data (sprites, levels, etc.)
- `data/dlcs/` - Sample DLC manifest files for `game-dlc-store`
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
