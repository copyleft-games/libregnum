# Libregnum Project Guide

## Project Overview

Libregnum is a GObject-based game engine library built on top of graylib (raylib GObject wrapper) and yaml-glib (YAML parsing with GObject serialization). It provides high-level game systems with a focus on data-driven design, extensibility, and modding support.

## Directory Structure

```
libregnum/
├── COPYING/LICENSE           # AGPL-3.0-or-later license
├── Makefile                  # Root build orchestration
├── config.mk                 # Build configuration
├── rules.mk                  # Build rules and helpers
├── libregnum.pc.in           # pkg-config template
├── README.md                 # Build instructions
├── CLAUDE.md                 # This file (AI context)
├── deps/
│   ├── graylib/              # GObject wrapper for raylib
│   └── yaml-glib.git/        # YAML parsing library
├── src/
│   ├── libregnum.h           # Master include header
│   ├── lrg-version.h.in      # Version template
│   ├── config.h.in           # Internal config template
│   ├── lrg-types.h           # Forward declarations
│   ├── lrg-enums.h/.c        # Enumerations with GType
│   ├── lrg-log.h             # Logging macros
│   └── core/                 # Core module
│       ├── lrg-engine.h/.c   # Engine singleton
│       ├── lrg-registry.h/.c # GType registry
│       └── lrg-data-loader.h/.c  # YAML data loading
├── tests/
│   ├── Makefile
│   ├── test-engine.c
│   ├── test-registry.c
│   └── test-data-loader.c
└── docs/
```

## Build Commands

```bash
make              # Build dependencies, then library + GIR
make DEBUG=1      # Debug build with -g3 -O0
make test         # Build and run all tests
make install      # Install to PREFIX (default: /usr/local)
make clean        # Remove build artifacts
make help         # Show all targets
```

## C Standard and Compiler Flags

- **Standard**: `gnu89` (GNU C89 extensions)
- **No `-pedantic`** - GNU extensions are allowed
- **Zero warning tolerance** - All warnings are errors (`-Werror`)

Key warning flags:
```makefile
WARN_CFLAGS := -Wall -Wextra -Werror
WARN_CFLAGS += -Wformat=2 -Wformat-security
WARN_CFLAGS += -Wnull-dereference
WARN_CFLAGS += -Wstrict-prototypes -Wmissing-prototypes
WARN_CFLAGS += -Wold-style-definition -Wdeclaration-after-statement
WARN_CFLAGS += -Wno-unused-parameter
```

## Code Style

### Naming Conventions

| Type | Convention | Example |
|------|------------|---------|
| Macros/Defines | `UPPERCASE_SNAKE_CASE` | `LRG_TYPE_ENGINE` |
| Types/Classes | `PascalCase` | `LrgEngine`, `LrgDataLoader` |
| Functions | `lowercase_snake_case` | `lrg_engine_startup` |
| Variables | `lowercase_snake_case` | `default_engine` |
| Properties | `kebab-case` | `"type-field-name"` |
| Signals | `kebab-case` | `"pre-update"` |

### Function Signature Style

Return type on separate line, parameters aligned:

```c
LrgEngine *
lrg_engine_get_default (void)
{
    /* ... */
}

gboolean
lrg_engine_startup (LrgEngine  *self,
                    GError    **error)
{
    /* ... */
}
```

### Comment Style

Always use `/* */` style, never `//`:

```c
/* Single line comment */

/*
 * Multi-line comment
 * with additional lines.
 */
```

### GObject Patterns

**Derivable types** (for inheritance):
```c
G_DECLARE_DERIVABLE_TYPE (LrgEngine, lrg_engine, LRG, ENGINE, GObject)

struct _LrgEngineClass
{
    GObjectClass parent_class;

    void (*startup)  (LrgEngine *self);
    void (*shutdown) (LrgEngine *self);

    gpointer _reserved[8];  /* ABI stability padding */
};
```

**Final types** (no subclassing):
```c
G_DECLARE_FINAL_TYPE (LrgRegistry, lrg_registry, LRG, REGISTRY, GObject)
```

**Memory management**:
```c
g_autoptr(GError) error = NULL;
g_autofree gchar *str = g_strdup ("value");
g_clear_object (&self->registry);
g_clear_pointer (&self->name, g_free);
return g_steal_pointer (&object);
```

## GObject Introspection Annotations

All public API must include gtk-doc comments with GIR annotations:

```c
/**
 * lrg_data_loader_load_file:
 * @self: an #LrgDataLoader
 * @path: path to the YAML file
 * @error: (nullable): return location for error
 *
 * Loads a GObject from a YAML file.
 *
 * Returns: (transfer full) (nullable): The loaded #GObject, or %NULL on error
 */
```

Common annotations:
- `(transfer full)` - Caller owns returned value
- `(transfer none)` - Caller does NOT own returned value
- `(nullable)` - Can be NULL
- `(out)` - Output parameter
- `(element-type X)` - Container element type
- `(array length=n)` - Array with length parameter

## Dependencies

### Required packages (Fedora)

```bash
glib2-devel gobject-introspection-devel libdex-devel
libyaml-devel json-glib-devel
```

### Submodules

- `deps/graylib/` - Raylib GObject wrapper
- `deps/yaml-glib.git/` - YAML parsing library

## Key Type Patterns

### Engine Singleton

```c
LrgEngine *engine = lrg_engine_get_default ();
lrg_engine_startup (engine, &error);
lrg_engine_update (engine, delta_time);
lrg_engine_shutdown (engine);
```

### Registry (Type Mapping)

```c
LrgRegistry *registry = lrg_engine_get_registry (engine);
lrg_registry_register (registry, "player", MY_TYPE_PLAYER);
GType type = lrg_registry_lookup (registry, "player");
GObject *obj = lrg_registry_create (registry, "player", "name", "Hero", NULL);
```

### Data Loader (YAML)

```c
LrgDataLoader *loader = lrg_engine_get_data_loader (engine);
lrg_data_loader_set_registry (loader, registry);
GObject *obj = lrg_data_loader_load_file (loader, "data/player.yaml", &error);
```

### Async Operations (libdex)

```c
DexFuture *future = lrg_data_loader_load_file_async (loader, path);
/* Use dex_await() in fibers or dex_future_then() for callbacks */
```

## Testing

Tests use GLib testing framework:

```c
static void
test_registry_register (RegistryFixture *fixture,
                        gconstpointer    user_data)
{
    lrg_registry_register (fixture->registry, "test", TEST_TYPE_OBJECT);
    g_assert_true (lrg_registry_is_registered (fixture->registry, "test"));
}

int main (int argc, char *argv[])
{
    g_test_init (&argc, &argv, NULL);
    g_test_add ("/registry/register", RegistryFixture, NULL,
                fixture_set_up, test_registry_register, fixture_tear_down);
    return g_test_run ();
}
```

Run with: `make test`

## Logging

Per-module log domains:

```c
#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_CORE
#include "lrg-log.h"

lrg_debug (LRG_LOG_DOMAIN_CORE, "Debug message: %s", value);
lrg_info (LRG_LOG_DOMAIN_CORE, "Info message");
lrg_warning (LRG_LOG_DOMAIN_CORE, "Warning: %d", code);
```

Enable debug output: `G_MESSAGES_DEBUG=Libregnum-Core ./my-app`

## Error Handling

Use GError pattern:

```c
g_autoptr(GError) error = NULL;

if (!lrg_engine_startup (engine, &error))
{
    lrg_warning (LRG_LOG_DOMAIN_CORE, "Startup failed: %s", error->message);
    return FALSE;
}
```

Error domains:
- `LRG_ENGINE_ERROR` - Engine errors
- `LRG_DATA_LOADER_ERROR` - Data loading errors
- `LRG_MOD_ERROR` - Mod system errors

## Files to Reference for Patterns

| Pattern | File |
|---------|------|
| Derivable GObject with signals | `src/core/lrg-engine.h/.c` |
| Final GObject with properties | `src/core/lrg-registry.h/.c` |
| Async with libdex | `src/core/lrg-data-loader.c` |
| GTest usage | `tests/test-registry.c` |
| Build system | `Makefile`, `config.mk`, `rules.mk` |
| graylib patterns | `deps/graylib/src/scene/grl-entity.h` |
| yaml-glib patterns | `deps/yaml-glib.git/src/yaml-gobject.h` |

## Implementation Phases

Current status: **Phase 0 (Foundation)** complete.

- [x] Build system (Makefile, config.mk, rules.mk)
- [x] Core module (Engine, Registry, DataLoader)
- [x] Logging system
- [x] Unit tests
- [x] Documentation

Next phases:
- Phase 1: ECS, Input, UI, Tilemap
- Phase 2: Dialog, Inventory, Quest, Save, Audio
- Phase 3: AI, Pathfinding, Physics, I18N, Net, World3D
- Phase 4: Debug tools, Mod system
