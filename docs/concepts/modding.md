---
title: Modding System
concept: advanced
phase: 4
---

# Modding System

The Libregnum modding system allows users to extend and customize games built with Libregnum. Mods can add new content, modify existing behavior, and override assets and types.

> **[Home](../../index.md)** > **[Concepts](index.md)** > Modding

## Mod Types

### Data Mods

Add or override game data (assets, YAML files):

```
my-mod/
├── mod.yaml                 # Mod manifest
├── assets/
│   ├── sprites/
│   │   ├── player.png       # Override base game
│   │   └── new-enemy.png    # New asset
│   ├── audio/
│   │   └── sword-slash.wav
│   └── data/
│       └── items.yaml
└── locales/
    └── en.yaml
```

**Advantages**: Easiest to create, no coding required

**Limitations**: Limited to data, can't add new mechanics

### Script Mods

Extend engine with scripting (Lua, JavaScript, etc.):

```
my-mod/
├── mod.yaml
├── scripts/
│   ├── main.lua
│   ├── enemies/
│   │   └── custom-enemy.lua
│   └── systems/
│       └── new-game-system.lua
└── assets/
    └── ...
```

**Advantages**: Full control, can add new systems

**Limitations**: Requires learning scripting language

### Native Mods

Extend engine with compiled plugins:

```
my-mod/
├── mod.yaml
├── libmy-mod.so             # Compiled plugin
├── include/
│   └── my-mod.h
└── assets/
    └── ...
```

**Advantages**: Maximum performance, direct C API access

**Limitations**: Requires compilation, C knowledge, platform-specific

## Mod Structure

### Minimal Mod

```
my-mod/
├── mod.yaml
└── assets/
    ├── data/
    │   └── items.yaml
    └── sprites/
        └── cool-sword.png
```

### Complete Mod

```
my-mod/
├── mod.yaml                 # Manifest
├── README.md                # Documentation
├── LICENSE                  # License file
├── assets/
│   ├── data/                # Game data
│   │   ├── items.yaml
│   │   ├── enemies.yaml
│   │   └── quests.yaml
│   ├── sprites/             # Graphics
│   ├── audio/               # Sounds and music
│   └── fonts/               # Custom fonts
├── scripts/                 # Script files (if script mod)
│   └── main.lua
├── locales/                 # Translations
│   ├── en.yaml
│   ├── es.yaml
│   └── fr.yaml
└── native/                  # Compiled plugin (if native mod)
    └── libmy-mod.so
```

### Mod Manifest

The `mod.yaml` file describes the mod:

```yaml
# Mod metadata
id: "my-cool-mod"
name: "My Cool Mod"
version: "1.0.0"
description: "Adds cool new content to the game"

# Author
author: "John Doe"
email: "john@example.com"
url: "https://example.com/mods/my-cool-mod"

# Compatibility
game-version: ">=1.0.0 <2.0.0"  # SemVer constraint
engine-version: ">=1.0.0"

# Dependencies
dependencies:
  - id: "base-game"
    version: ">=1.0.0"
  - id: "my-other-mod"
    version: "1.2.3"

# Type and load order
type: "data"              # data, script, or native
priority: 50              # Higher = loaded later (overrides earlier)

# Mod-specific config
config:
  difficulty: "hard"
  enable-new-quests: true
```

## Asset Overlay

Mods override base game assets using search path priority:

```c
LrgAssetManager *manager = lrg_engine_get_asset_manager (engine);

/* Add base game assets first (lowest priority) */
lrg_asset_manager_add_search_path (manager, "base/assets/");

/* Add mod assets (higher priority) */
lrg_asset_manager_add_search_path (manager, "mods/my-mod/assets/");
lrg_asset_manager_add_search_path (manager, "mods/another-mod/assets/");

/* Load texture - searches in reverse order (last added first) */
GrlTexture *texture = lrg_asset_manager_load_texture (manager, "sprites/player.png", &error);
/* If found in mods/another-mod/assets/sprites/player.png, uses that */
/* Otherwise tries mods/my-mod/assets/sprites/player.png */
/* Finally falls back to base/assets/sprites/player.png */
```

## Type Override

Mods override registered types:

```c
LrgRegistry *registry = lrg_engine_get_registry (engine);

/* Base game registers base types */
lrg_registry_register (registry, "player", GAME_TYPE_PLAYER);
lrg_registry_register (registry, "enemy-goblin", GAME_TYPE_ENEMY_GOBLIN);

/* Mod can override types */
lrg_registry_register (registry, "player", MOD_TYPE_ENHANCED_PLAYER);
lrg_registry_register (registry, "enemy-goblin", MOD_TYPE_SCARY_GOBLIN);

/* Now all references to "player" type create the mod's version */
GObject *player = lrg_registry_create (registry, "player", NULL);
/* Creates MOD_TYPE_ENHANCED_PLAYER instead of GAME_TYPE_PLAYER */
```

## Data Mod Example

Create a mod that adds new items:

```yaml
# items.yaml
type: item
id: "legendary-sword"
name: "Legendary Sword"
damage: 50
rarity: legendary
```

```c
/* Game code */
LrgDataLoader *loader = lrg_engine_get_data_loader (engine);
GList *items = lrg_data_loader_load_directory (loader, "data/items/", FALSE, &error);

/* Automatically includes base items and mod items from overlay */
```

## Script Mod Example

Lua script mod (hypothetical):

```lua
-- scripts/main.lua

-- Called when mod loads
function on_load()
    print("My Cool Mod loaded!")

    -- Register custom types
    register_type("my-custom-enemy", CustomEnemy)

    -- Hook into game events
    on_event("game:startup", on_game_start)
    on_event("game:shutdown", on_game_stop)
end

function on_game_start()
    print("Game started, applying mod changes")

    -- Modify game state, add systems, etc
end

function on_game_stop()
    print("Game stopped, cleaning up mod")
end

function CustomEnemy:init()
    -- Custom enemy logic
end
```

## Native Mod Example

C plugin mod (hypothetical):

```c
/* libmy-mod.c */
#include <libregnum.h>
#include <glib.h>

/* Custom game object type */
#define MOD_TYPE_SPECIAL_ENEMY (mod_special_enemy_get_type ())
G_DECLARE_FINAL_TYPE (ModSpecialEnemy, mod_special_enemy, MOD, SPECIAL_ENEMY, GObject)

struct _ModSpecialEnemy { GObject parent_instance; };
G_DEFINE_TYPE (ModSpecialEnemy, mod_special_enemy, G_TYPE_OBJECT)

static void mod_special_enemy_class_init (ModSpecialEnemyClass *klass) { }
static void mod_special_enemy_init (ModSpecialEnemy *self) { }

/* Mod initialization (called when mod loads) */
void
mod_on_load (LrgModManager *manager, LrgMod *mod)
{
    LrgEngine *engine = lrg_engine_get_default ();
    LrgRegistry *registry = lrg_engine_get_registry (engine);

    /* Register custom types */
    lrg_registry_register (registry, "special-enemy", MOD_TYPE_SPECIAL_ENEMY);

    g_print ("My Mod loaded successfully!\n");
}
```

## Mod Loading Process

The mod system manages mod lifecycle:

```
1. Discovery          Load all mod.yaml files
2. Dependency Check   Verify all dependencies present
3. Load Order         Sort by priority and dependencies
4. Initialization     Call mod init functions
5. Asset Setup        Configure search paths
6. Type Registration  Register custom types
7. Ready              Game can use mod content
```

## Mod States

```
UNLOADED       (mod not loaded yet)
  ↓
DISCOVERED     (manifest found, not loaded)
  ↓
LOADING        (currently loading)
  ↓
LOADED         (loaded and active)
or
FAILED         (loading failed)
  ↓
DISABLED       (user disabled it)
```

## Loading Mods

```c
LrgEngine *engine = lrg_engine_get_default ();
lrg_engine_startup (engine, &error);

LrgModManager *mod_manager = lrg_engine_get_mod_manager (engine);

/* Add mod directory */
lrg_mod_manager_add_mod_path (mod_manager, "./mods/");

/* Discover and load mods */
GList *loaded = lrg_mod_manager_load_all (mod_manager, &error);

g_print ("Loaded %u mods\n", g_list_length (loaded));
```

## Mod Configuration

Mods can expose configuration options:

```yaml
# mod.yaml
config:
  difficulty: "normal"
  enable-hard-mode: false
  custom-colors: true
```

Access from code:

```c
LrgMod *mod = lrg_mod_manager_get_mod (manager, "my-cool-mod");
GObject *config = lrg_mod_get_config (mod);

/* Read configuration values */
gboolean hard_mode;
g_object_get (config, "enable-hard-mode", &hard_mode, NULL);
```

## Version Constraints

Mods specify compatibility using SemVer:

```yaml
# mod.yaml
game-version: ">=1.0.0 <2.0.0"
engine-version: ">=1.2.0"

dependencies:
  - id: "base-content"
    version: "1.0.0"          # Exact version
  - id: "graphics-pack"
    version: ">=1.5.0"        # Minimum version
  - id: "utils-library"
    version: "1.x"            # Any 1.x version
```

## Circular Dependency Detection

The mod system prevents circular dependencies:

```yaml
# mod-a.yaml
dependencies:
  - id: "mod-b"

# mod-b.yaml
dependencies:
  - id: "mod-c"

# mod-c.yaml
dependencies:
  - id: "mod-a"     # ERROR: Circular dependency!
```

Results in load error with clear message.

## Mod Priority and Overrides

Load order determines override behavior:

```c
lrg_mod_manager_load_mod (manager, "base-mod", &error);      /* Loads first */
lrg_mod_manager_load_mod (manager, "enhancement-mod", &error); /* Overrides base */
lrg_mod_manager_load_mod (manager, "extreme-mod", &error);     /* Overrides both */
```

In `mod.yaml`:

```yaml
priority: 50    # Higher priority loads later
```

## Unloading Mods

Unload or disable mods:

```c
/* Unload mod */
gboolean result = lrg_mod_manager_unload_mod (manager, "my-mod", &error);

/* Disable mod (doesn't unload) */
lrg_mod_manager_set_mod_enabled (manager, "my-mod", FALSE);
```

## Best Practices

### For Mod Creators

1. **Clear mod manifest** - Document all metadata
2. **Follow conventions** - Use standard mod structure
3. **Semantic versioning** - Use SemVer for versions
4. **Document dependencies** - List all required mods
5. **Namespace assets** - Avoid conflicts with other mods
6. **Version constraints** - Be explicit about compatibility

### For Game Developers

1. **Design for mods** - Use Registry and DataLoader
2. **Clear extension points** - Document where mods can hook
3. **Stable API** - Don't break mod compatibility
4. **Handle mod failures** - Gracefully skip broken mods
5. **Provide mod tools** - Documentation, examples, templates

## Complete Mod Example

```
awesome-mod/
├── mod.yaml
├── README.md
├── LICENSE (MIT)
├── assets/
│   ├── data/
│   │   ├── weapons.yaml
│   │   ├── armor.yaml
│   │   └── spells.yaml
│   ├── sprites/
│   │   ├── weapons/
│   │   ├── armor/
│   │   └── effects/
│   ├── audio/
│   │   └── spells/
│   └── locales/
│       └── en.yaml
└── scripts/
    └── main.lua
```

```yaml
# mod.yaml
id: "awesome-mod"
name: "Awesome Mod"
version: "1.2.3"
author: "Jane Doe"
description: "Adds awesome content"
game-version: ">=1.0.0"
type: "data"
priority: 100

config:
  enabled-weapons: true
  enabled-spells: true
```

## See Also

- [Mod System Documentation](../modules/mod/index.md) *(Phase 4)*
- [Asset Manager](../modules/core/asset-manager.md) - Asset overlay
- [Type Registry](type-registry.md) - Type override
- [Architecture Overview](../architecture.md) - Extension patterns
