# LrgScriptingPython

## Overview

`LrgScriptingPython` is the Python scripting backend. It provides access to Python's extensive ecosystem and familiar syntax for game scripting.

**Requirements:** Python 3.12+ (via `python3-embed` pkg-config)

## Construction

### lrg_scripting_python_new

```c
LrgScriptingPython *
lrg_scripting_python_new (void);
```

Creates a new Python scripting context with the interpreter initialized.

**Example:**
```c
g_autoptr(LrgScriptingPython) scripting = lrg_scripting_python_new ();

/* Execute Python code */
lrg_scripting_load_string (LRG_SCRIPTING (scripting),
                           "hello",
                           "print('Hello from Python!')",
                           NULL);
```

## Registry Integration

### lrg_scripting_python_set_registry

```c
void
lrg_scripting_python_set_registry (LrgScriptingPython *self,
                                   LrgRegistry        *registry);
```

Sets the registry for type lookups, enabling object creation from Python.

### lrg_scripting_python_get_registry

```c
LrgRegistry *
lrg_scripting_python_get_registry (LrgScriptingPython *self);
```

Gets the current registry, or NULL if not set.

**Example:**
```c
LrgRegistry *registry = lrg_engine_get_registry (engine);
lrg_scripting_python_set_registry (scripting, registry);

/* Now Python can create objects */
lrg_scripting_load_string (LRG_SCRIPTING (scripting), "create",
    "player = Registry.create('player', name='Hero')",
    NULL);
```

## Search Paths

### lrg_scripting_python_add_search_path

```c
void
lrg_scripting_python_add_search_path (LrgScriptingPython *self,
                                      const gchar        *path);
```

Adds a directory to `sys.path`, enabling Python imports from that location.

### lrg_scripting_python_clear_search_paths

```c
void
lrg_scripting_python_clear_search_paths (LrgScriptingPython *self);
```

Removes all custom search paths.

**Example:**
```c
/* Add mod script directory */
lrg_scripting_python_add_search_path (scripting, "mods/mymod/scripts");

/* Now scripts can use import */
lrg_scripting_load_string (LRG_SCRIPTING (scripting), "init",
    "import utils",  /* Imports mods/mymod/scripts/utils.py */
    NULL);
```

## Update Hooks

Update hooks are Python functions called every frame. Use them for game loop integration.

### lrg_scripting_python_register_update_hook

```c
void
lrg_scripting_python_register_update_hook (LrgScriptingPython *self,
                                           const gchar        *func_name);
```

Register a Python function to be called each frame.

### lrg_scripting_python_unregister_update_hook

```c
gboolean
lrg_scripting_python_unregister_update_hook (LrgScriptingPython *self,
                                             const gchar        *func_name);
```

Remove an update hook.

### lrg_scripting_python_clear_update_hooks

```c
void
lrg_scripting_python_clear_update_hooks (LrgScriptingPython *self);
```

Remove all update hooks.

### lrg_scripting_python_update

```c
void
lrg_scripting_python_update (LrgScriptingPython *self,
                             gfloat              delta);
```

Call all registered update hooks with the delta time.

**Example:**
```c
/* Define update function in Python */
lrg_scripting_load_string (LRG_SCRIPTING (scripting), "game",
    "player_x = 0\n"
    "def game_update(delta):\n"
    "    global player_x\n"
    "    player_x = player_x + 100 * delta  # Move 100 units/second\n",
    NULL);

/* Register the hook */
lrg_scripting_python_register_update_hook (scripting, "game_update");

/* In game loop */
while (running)
{
    gfloat delta = /* calculate delta time */;
    lrg_scripting_python_update (scripting, delta);
    /* ... render, etc ... */
}
```

## Engine Access

### lrg_scripting_python_set_engine

```c
void
lrg_scripting_python_set_engine (LrgScriptingPython *self,
                                 LrgEngine          *engine);
```

Exposes the engine to Python as the `Engine` global.

### lrg_scripting_python_get_engine

```c
LrgEngine *
lrg_scripting_python_get_engine (LrgScriptingPython *self);
```

Gets the current engine, or NULL if not set.

## Python API Reference

### Log

```python
Log.debug("Debug message")    # Debug level logging
Log.info("Info message")      # Info level logging
Log.warning("Warning message")  # Warning level logging
Log.error("Error message")    # Error level logging
```

### Registry

```python
# Create an object with properties (keyword arguments)
obj = Registry.create("type-name", prop1=value1, prop2=value2)

# Check if a type is registered
exists = Registry.is_registered("type-name")

# Get all registered type names
types = Registry.get_types()
```

### Engine

```python
# Access engine subsystems
registry = Engine.registry
assets = Engine.asset_manager
loader = Engine.data_loader

# Check engine state
state = Engine.state
is_running = Engine.is_running
```

### GObject Properties

GObjects returned from C or created via Registry have automatic property access:

```python
player = Registry.create("player", name="Hero")

# Read property
print(player.name)  # "Hero"

# Write property
player.health = 100

# Properties with dashes can use underscores
player.max_health = 200  # Accesses "max-health" property
```

## Type Conversions

Python types automatically convert to/from GObject types:

| Python Type | GType |
|-------------|-------|
| `bool` | G_TYPE_BOOLEAN |
| `int` | G_TYPE_INT64 / G_TYPE_UINT64 |
| `float` | G_TYPE_DOUBLE |
| `str` | G_TYPE_STRING |
| `None` | G_TYPE_NONE |
| GObject wrapper | G_TYPE_OBJECT |

## Complete Example

```c
#include <libregnum.h>

int
main (int argc, char *argv[])
{
    g_autoptr(LrgEngine) engine = lrg_engine_get_default ();
    g_autoptr(LrgScriptingPython) scripting = lrg_scripting_python_new ();
    g_autoptr(GError) error = NULL;

    /* Connect to engine */
    lrg_scripting_python_set_engine (scripting, engine);
    lrg_scripting_python_set_registry (scripting, lrg_engine_get_registry (engine));
    lrg_engine_set_scripting (engine, LRG_SCRIPTING (scripting));

    /* Add script search path */
    lrg_scripting_python_add_search_path (scripting, "scripts");

    /* Load main script */
    if (!lrg_scripting_load_file (LRG_SCRIPTING (scripting),
                                  "scripts/main.py",
                                  &error))
    {
        g_printerr ("Script error: %s\n", error->message);
        return 1;
    }

    /* Register update hook */
    lrg_scripting_python_register_update_hook (scripting, "game_update");

    /* Game loop */
    lrg_engine_startup (engine, NULL);
    while (lrg_engine_is_running (engine))
    {
        gfloat delta = 0.016f;
        lrg_scripting_python_update (scripting, delta);
        lrg_engine_update (engine, delta);
    }
    lrg_engine_shutdown (engine);

    return 0;
}
```

## Python Script Example

```python
# scripts/main.py

# Use built-in logging
Log.info("Game starting...")

# Global game state
player_x = 0.0
player_y = 0.0
speed = 200.0

def game_update(delta):
    """Called every frame by the engine."""
    global player_x, player_y

    # Move player (simplified - real game would check input)
    player_x += speed * delta

    # Log occasionally
    if int(player_x) % 100 == 0:
        Log.debug(f"Player position: {player_x:.1f}, {player_y:.1f}")

def on_init():
    """Called once when script loads."""
    Log.info("Player initialized")

    # Check what types are available
    types = Registry.get_types()
    Log.debug(f"Available types: {types}")

# Run initialization
on_init()
```

## See Also

- [LrgScripting](scripting.md) - Base class API
- [LrgScriptingLua](scripting-lua.md) - Lua alternative
- [Scripting Examples](../../examples/scripting-basics.md) - More examples
