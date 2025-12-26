# How to Create Mods

This guide provides an overview of Libregnum's mod system, which allows developers and modders to extend the game with new content and features.

## Mod System Overview

The Libregnum mod system is designed to be:
- **Data-Driven**: Most mods use YAML and data files
- **Type-Safe**: Mods integrating C code maintain type safety via GObject Introspection
- **Hot-Loadable**: Some mod types can be loaded at runtime
- **Extensible**: The mod system itself can be extended

## Mod Types

### 1. Data Mods

Pure data-driven mods with no code. Easiest to create and most accessible.

**Contents:**
- Item definitions
- NPC definitions
- Map/tilemap data
- Dialog/quest data
- Configuration files

**Advantages:**
- No compilation needed
- Easy for non-programmers
- Can be loaded/unloaded at runtime
- Platform independent

**Example structure:**
```
my_mod/
├── manifest.yaml
├── items/
│   ├── weapons.yaml
│   └── armor.yaml
├── npcs/
│   └── traders.yaml
├── dialogs/
│   └── conversations.yaml
└── config.yaml
```

### 2. Script Mods

Mods using embedded scripting languages (if supported by the engine version).

**Contents:**
- Lua/Python scripts
- Data definitions
- Configuration

**Advantages:**
- More flexible than pure data
- Can implement custom logic
- No compilation needed

**Challenges:**
- Requires scripting runtime
- Performance overhead
- Sandbox/security considerations

### 3. Native Mods

C-based mods compiled to shared libraries (.so/.dll).

**Contents:**
- C code implementing game systems
- Custom components
- Custom item types
- Custom systems

**Advantages:**
- Full performance
- Complete type safety
- Access to all engine APIs

**Challenges:**
- Requires compilation
- Must match engine ABI
- Version compatibility issues

## Creating a Data Mod

### Step 1: Create Directory Structure

```bash
mkdir -p my_adventure_mod/{items,npcs,dialogs}
cd my_adventure_mod
```

### Step 2: Create Manifest

```yaml
# manifest.yaml
id: my_adventure_mod
version: 1.0.0
name: My Adventure Expansion
author: Your Name
description: Adds new items, NPCs, and quests to the game

# Engine version compatibility
engine_version: 1.0

# Mod dependencies
dependencies:
  - core_game

# Mod type (data, script, or native)
type: data

# Load order (higher = loaded later)
priority: 100

# Permissions/capabilities
features:
  - items
  - npcs
  - dialogs
  - quests
```

### Step 3: Define Items

```yaml
# items/new_weapons.yaml
id: sword_excalibur
name: Excalibur
description: A legendary sword of great power
type: weapon
stackable: false
max_stack: 1
value: 10000

properties:
  attack: 50
  weight: 4.5
  element: light
  magical: true
  unique: true
  attack_speed: 1.5
```

### Step 4: Define NPCs

```yaml
# npcs/questgivers.yaml
characters:
  - id: npc_merchant_old
    name: Old Merchant
    description: A weathered trader with many stories
    class: merchant
    location: town_square

    properties:
      health: 50
      dialogue_theme: friendly

    items_for_sale:
      - sword_excalibur
      - potion_health
      - gold_coin
```

### Step 5: Define Dialogs

```yaml
# dialogs/merchant.yaml
id: merchant_greeting
start_node: greeting

nodes:
  greeting:
    text: "Welcome, adventurer! Looking for something special?"
    options:
      - text: "Show me your weapons"
        next: show_weapons
      - text: "I'll come back later"
        next: end

  show_weapons:
    text: "Here are my finest blades. Very rare, very expensive."
    options:
      - text: "Tell me more about Excalibur"
        next: excalibur_description
      - text: "Never mind"
        next: greeting

  excalibur_description:
    text: "Ah, the legendary Excalibur... A sword of unmatched power."
    options:
      - text: "How much?"
        next: price
      - text: "Back"
        next: show_weapons

  price:
    text: "10000 gold coins. A steal for such a legendary weapon!"
    options:
      - text: "Goodbye"
        next: end

  end:
    text: "Safe travels!"
    options: []
```

### Step 6: Load the Mod

```c
LrgEngine *engine = lrg_engine_get_default();
LrgModSystem *mods = lrg_engine_get_mod_system(engine);

GError *error = NULL;

gboolean loaded = lrg_mod_system_load_mod(mods,
                                          "path/to/my_adventure_mod",
                                          &error);

if (!loaded) {
    g_warning("Failed to load mod: %s", error->message);
    g_clear_error(&error);
}
```

## Creating a Native Mod

### Step 1: Set Up Build System

```makefile
# Makefile for native mod
CC = gcc
CFLAGS = -fPIC -Wall -Werror
LDFLAGS = -shared

# Compile to shared library
libmy_mod.so: my_mod.c
	$(CC) $(CFLAGS) -c my_mod.c
	$(CC) $(LDFLAGS) -o libmy_mod.so my_mod.o `pkg-config --libs --cflags libregnum`
```

### Step 2: Create Mod Entry Point

```c
/* my_mod.c */
#include <libregnum.h>

/* Types defined in this mod */
#define MY_TYPE_FLAME_COMPONENT (my_flame_component_get_type())

G_DECLARE_FINAL_TYPE(MyFlameComponent,
                     my_flame_component,
                     MY,
                     FLAME_COMPONENT,
                     LrgComponent)

struct _MyFlameComponent {
    LrgComponent parent_instance;
    gfloat fire_intensity;
};

/* Boilerplate... */

G_DEFINE_TYPE(MyFlameComponent,
              my_flame_component,
              LRG_TYPE_COMPONENT)

/* Mod initialization function - called when mod loads */

void
mod_initialize(LrgEngine *engine)
{
    LrgRegistry *registry = lrg_engine_get_registry(engine);

    /* Register custom types */
    lrg_registry_register(registry, "flame_component", MY_TYPE_FLAME_COMPONENT);

    g_message("My Mod initialized successfully");
}

/* Mod cleanup function - called when mod unloads */

void
mod_cleanup(LrgEngine *engine)
{
    g_message("My Mod cleaned up");
}
```

### Step 3: Create Manifest

```yaml
# manifest.yaml for native mod
id: my_flame_mod
version: 1.0.0
name: Flame Effects Mod
author: Your Name
type: native
engine_version: 1.0

entry_point: libmy_mod.so
symbols:
  - mod_initialize
  - mod_cleanup

features:
  - components
  - custom_systems
```

## Mod Packaging

### Standard Package Format

```bash
# Create tarball of mod
tar -czf my_adventure_mod-1.0.0.tar.gz my_adventure_mod/

# Or ZIP
zip -r my_adventure_mod-1.0.0.zip my_adventure_mod/
```

### Distribution

Mods can be distributed:
- Via mod repository/manager
- GitHub releases
- Direct download
- Included with game

## Best Practices

### Naming Conventions

Use reverse domain notation for unique IDs:

```yaml
id: com.example.my_adventure_mod
name: My Adventure Expansion
```

### Dependencies

Always declare dependencies:

```yaml
dependencies:
  - core_game
  - my_other_mod

optional_dependencies:
  - advanced_quest_system
```

### Version Compatibility

Test with target engine versions:

```yaml
engine_version: ">=1.0,<2.0"
```

### Documentation

Include README:

```markdown
# My Adventure Expansion

Adds new items and NPCs to enhance the adventure.

## Installation

1. Extract to `mods/` directory
2. Start game - mod loads automatically

## Features

- 10 new legendary items
- 5 new quest givers
- 15 new dialog branches

## Compatibility

- Works with version 1.0+
- Requires core_game mod
```

### Asset Organization

Keep structure logical:

```
my_mod/
├── manifest.yaml
├── README.md
├── items/
│   ├── weapons.yaml
│   ├── armor.yaml
│   └── accessories.yaml
├── npcs/
│   ├── merchants.yaml
│   └── quest_givers.yaml
├── dialogs/
├── maps/
├── textures/          # If graphics mods supported
├── sounds/            # If audio mods supported
└── config.yaml        # Mod settings
```

## Testing Your Mod

### Unit Testing Data

```bash
# Validate YAML syntax
yamllint my_mod/**/*.yaml

# Test item loading
game --test-load-mod my_mod
```

### Integration Testing

```c
/* test-my_mod.c */
#include <glib.h>
#include <libregnum.h>

static void
test_mod_loads(void)
{
    LrgEngine *engine = lrg_engine_get_default();
    LrgModSystem *mods = lrg_engine_get_mod_system(engine);

    GError *error = NULL;
    gboolean loaded = lrg_mod_system_load_mod(mods, "my_mod", &error);

    g_assert_no_error(error);
    g_assert_true(loaded);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/mod/loads", test_mod_loads);
    return g_test_run();
}
```

## Troubleshooting

### Mod Won't Load

1. Check manifest.yaml syntax
2. Verify mod ID is unique
3. Check engine version compatibility
4. Review engine logs for errors

### Data Not Appearing

1. Verify YAML is valid (check indentation)
2. Ensure keys match expected names
3. Check mod load order (dependencies loaded first)
4. Use debug mode to trace loading

### Performance Issues

1. Profile data loading time
2. Reduce data file sizes
3. Use more efficient YAML structures
4. For native mods, profile code

## See Also

- [Data Loader Documentation](../modules/data-loader/index.md)
- [Registry System](../modules/core/registry.md)
- [Implementing Saveable](implementing-saveable.md)
