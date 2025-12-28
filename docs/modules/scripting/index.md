# Scripting Module

## Overview

The Scripting module enables game logic to be written in dynamic languages rather than compiled C code. This allows for rapid iteration, modding support, and runtime flexibility.

Libregnum provides multiple scripting backends:

- **Lua** (LuaJIT) - Lightweight, fast, ideal for game scripting
- **Python** (CPython 3.12+) - Rich ecosystem, familiar syntax
- **PyGObject** (Python + GI) - Full GObject Introspection access from Python
- **Gjs** (GNOME JavaScript) - Full GObject Introspection access from JavaScript

All backends implement the same abstract interface (`LrgScripting`), making them interchangeable at the API level. GI-based backends (PyGObject, Gjs) additionally inherit from `LrgScriptingGI` for typelib and object exposure support.

### Key Features

- **Script Loading**: Execute scripts from files or strings
- **Bidirectional Calls**: C can call script functions, scripts can call registered C functions
- **Global Variables**: Share data between C and scripts via globals
- **Update Hooks**: Register script functions to be called each frame
- **Registry Integration**: Create engine objects from scripts
- **Built-in API**: Log, Registry, and Engine globals available in scripts

## Quick Start

### Lua Example

```c
#include <libregnum.h>

int
main (int argc, char *argv[])
{
    g_autoptr(LrgScriptingLua) scripting = lrg_scripting_lua_new ();
    g_autoptr(GError) error = NULL;

    /* Execute a Lua script */
    if (!lrg_scripting_load_string (LRG_SCRIPTING (scripting),
                                    "hello",
                                    "Log:info('Hello from Lua!')",
                                    &error))
    {
        g_printerr ("Script error: %s\n", error->message);
        return 1;
    }

    return 0;
}
```

### Python Example

```c
#include <libregnum.h>

int
main (int argc, char *argv[])
{
    g_autoptr(LrgScriptingPython) scripting = lrg_scripting_python_new ();
    g_autoptr(GError) error = NULL;

    /* Execute a Python script */
    if (!lrg_scripting_load_string (LRG_SCRIPTING (scripting),
                                    "hello",
                                    "Log.info('Hello from Python!')",
                                    &error))
    {
        g_printerr ("Script error: %s\n", error->message);
        return 1;
    }

    return 0;
}
```

## Core Types

| Type | Description |
|------|-------------|
| `LrgScripting` | Abstract base class for scripting backends |
| `LrgScriptingGI` | Abstract base for GObject Introspection backends |
| `LrgScriptingLua` | LuaJIT scripting implementation |
| `LrgScriptingPython` | Python 3.12+ scripting implementation (direct C API) |
| `LrgScriptingPyGObject` | Python with PyGObject (native GI bindings) |
| `LrgScriptingGjs` | JavaScript with Gjs (native GI bindings) |
| `LrgScriptable` | Interface for custom script exposure (opt-in) |

## Feature Comparison

| Feature | Lua | Python | PyGObject | Gjs |
|---------|-----|--------|-----------|-----|
| Performance | Excellent (LuaJIT) | Good | Good | Good |
| Memory footprint | Small | Moderate | Larger | Moderate |
| Standard library | Minimal | Extensive | Extensive | Moderate |
| Package ecosystem | Moderate | Vast | Vast + GI libs | npm + GI libs |
| Syntax familiarity | Unique | Widely known | Widely known | Widely known |
| Update hooks | Yes | Yes | Yes | Yes |
| Registry integration | Yes | Yes | Yes | Yes |
| Engine access | Yes | Yes | Yes | Yes |
| Native GI bindings | No | No | Yes | Yes |
| Import Libregnum types | No | No | Yes | Yes |
| Use Gtk/Gio from scripts | No | No | Yes | Yes |

## Built-in API

All backends expose the same global objects (Log, Registry, Engine). GI-based backends (PyGObject, Gjs) additionally provide native access to GI types via `from gi.repository import Libregnum` (Python) or `imports.gi.Libregnum` (JavaScript).

### Log

Logging interface for script output:

```lua
-- Lua
Log:debug("Debug message")
Log:info("Info message")
Log:warning("Warning message")
Log:error("Error message")
```

```python
# Python
Log.debug("Debug message")
Log.info("Info message")
Log.warning("Warning message")
Log.error("Error message")
```

### Registry

Create registered engine objects from scripts:

```lua
-- Lua
local player = Registry:create("player", { name = "Hero", health = 100 })
local exists = Registry:is_registered("player")
local types = Registry:get_types()
```

```python
# Python
player = Registry.create("player", name="Hero", health=100)
exists = Registry.is_registered("player")
types = Registry.get_types()
```

### Engine

Access engine subsystems:

```lua
-- Lua
local registry = Engine.registry
local assets = Engine.asset_manager
local loader = Engine.data_loader
local is_running = Engine.is_running
```

```python
# Python
registry = Engine.registry
assets = Engine.asset_manager
loader = Engine.data_loader
is_running = Engine.is_running
```

## Engine Integration

Attach scripting to the engine for full integration:

```c
g_autoptr(LrgEngine) engine = lrg_engine_get_default ();
g_autoptr(LrgScriptingPython) scripting = lrg_scripting_python_new ();

/* Connect scripting to engine */
lrg_scripting_python_set_engine (scripting, engine);
lrg_scripting_python_set_registry (scripting, lrg_engine_get_registry (engine));
lrg_engine_set_scripting (engine, LRG_SCRIPTING (scripting));

/* Load game scripts */
lrg_scripting_load_file (LRG_SCRIPTING (scripting), "scripts/main.py", NULL);

/* In game loop */
while (lrg_engine_is_running (engine))
{
    gfloat delta = /* ... */;
    lrg_scripting_python_update (scripting, delta);
    lrg_engine_update (engine, delta);
}
```

## GObject Exposure

By default, all GObjects exposed to scripts have their properties accessible based on `GParamSpec` flags:

| GParamSpec Flag | Script Access |
|-----------------|---------------|
| `G_PARAM_READABLE` | Property can be read |
| `G_PARAM_WRITABLE` | Property can be written |

```lua
-- Lua: access GObject properties directly
local player = get_player()
print(player.name)        -- Read property
player.health = 100       -- Write property
```

### Custom Script Exposure (LrgScriptable)

For advanced use cases, implement the `LrgScriptable` interface to:

- **Expose custom methods** beyond simple property access
- **Control property visibility** (read-only, hidden from scripts)
- **Receive lifecycle callbacks** when entering/leaving script contexts

```lua
-- With LrgScriptable, objects can expose methods
local damage = player:attack(enemy)    -- Custom method
player:heal(25)                        -- Another method
-- player.internal_state               -- Hidden property (nil or error)
```

See [LrgScriptable](scriptable.md) for complete documentation.

## See Also

- [LrgScripting](scripting.md) - Abstract base class documentation
- [LrgScriptingGI](scripting-gi.md) - GI base class for introspection backends
- [LrgScriptingLua](scripting-lua.md) - Lua implementation details
- [LrgScriptingPython](scripting-python.md) - Python implementation details
- [LrgScriptingPyGObject](scripting-pygobject.md) - PyGObject implementation details
- [LrgScriptingGjs](scripting-gjs.md) - Gjs (GNOME JavaScript) implementation details
- [LrgScriptable](scriptable.md) - Custom script exposure interface
- [Scripting Examples](../../examples/scripting-basics.md) - Comprehensive usage examples
