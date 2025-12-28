# LrgScriptingPyGObject

## Overview

`LrgScriptingPyGObject` is the PyGObject-based Python scripting backend. Unlike `LrgScriptingPython` which uses direct Python C API wrappers, this class uses native PyGObject bindings, allowing scripts to access the full Libregnum GObject Introspection API directly.

**Requirements:**
- Python 3.12+ (via `python3-embed` pkg-config)
- PyGObject 3.x (`python3-gobject` or `pygobject3-devel` package)
- GObject Introspection runtime (`gobject-introspection-1.0` pkg-config)

## Type Hierarchy

```
GObject
└── LrgScripting (abstract)
    └── LrgScriptingGI (abstract, derivable)
        └── LrgScriptingPyGObject (final)
```

## When to Use

Choose `LrgScriptingPyGObject` when:

- Scripts need access to the full Libregnum type system
- You want native GI bindings (`from gi.repository import Libregnum`)
- Interoperating with other GI-based libraries (Gtk, Gio, etc.)
- Building modding systems that leverage the type system

Choose `LrgScriptingPython` when:

- You need lightweight Python scripting without GI overhead
- Scripts don't need direct access to Libregnum types
- Simpler use cases with just globals and function calls

## Construction

### lrg_scripting_pygobject_new

```c
LrgScriptingPyGObject *
lrg_scripting_pygobject_new (void);
```

Creates a new PyGObject-based Python scripting context. The interpreter is initialized automatically with the `gi` and `gi.repository` modules loaded.

**Example:**
```c
g_autoptr(LrgScriptingPyGObject) scripting = lrg_scripting_pygobject_new ();
g_autoptr(GError) error = NULL;

/* Execute Python code */
lrg_scripting_load_string (LRG_SCRIPTING (scripting),
                           "hello",
                           "print('Hello from PyGObject!')",
                           &error);
```

## Typical Setup

The recommended setup pattern for `LrgScriptingPyGObject`:

```c
g_autoptr(LrgScriptingPyGObject) scripting = lrg_scripting_pygobject_new ();
LrgScriptingGI *gi_self = LRG_SCRIPTING_GI (scripting);
g_autoptr(GError) error = NULL;

/* 1. Load the Libregnum typelib */
if (!lrg_scripting_gi_require_libregnum (gi_self, &error))
{
    g_printerr ("Failed to load Libregnum typelib: %s\n", error->message);
    return 1;
}

/* 2. Set engine and registry */
LrgEngine *engine = lrg_engine_get_default ();
lrg_scripting_gi_set_engine (gi_self, engine);
lrg_scripting_gi_set_registry (gi_self, lrg_engine_get_registry (engine));

/* 3. Expose engine to scripts */
lrg_scripting_gi_expose_object (gi_self, "engine", G_OBJECT (engine), &error);

/* 4. Add script search paths */
lrg_scripting_gi_add_search_path (gi_self, "scripts");
lrg_scripting_gi_add_search_path (gi_self, "mods/mymod/scripts");

/* 5. Load and run scripts */
lrg_scripting_load_file (LRG_SCRIPTING (scripting), "scripts/main.py", &error);

/* 6. Register update hooks */
lrg_scripting_gi_register_update_hook (gi_self, "game_update");
```

## Inherited API (from LrgScriptingGI)

`LrgScriptingPyGObject` inherits all functionality from `LrgScriptingGI`:

| Category | Methods |
|----------|---------|
| Registry Integration | `set_registry`, `get_registry` |
| Engine Integration | `set_engine`, `get_engine` |
| Search Paths | `add_search_path`, `clear_search_paths`, `get_search_paths` |
| Update Hooks | `register_update_hook`, `unregister_update_hook`, `clear_update_hooks`, `update` |
| Typelib Loading | `require_typelib`, `require_libregnum` |
| Object Exposure | `expose_object` |

See [LrgScriptingGI](scripting-gi.md) for detailed documentation.

## Script Execution (from LrgScripting)

### lrg_scripting_load_file

```c
gboolean
lrg_scripting_load_file (LrgScripting  *scripting,
                         const gchar   *path,
                         GError       **error);
```

Loads and executes a Python script file.

### lrg_scripting_load_string

```c
gboolean
lrg_scripting_load_string (LrgScripting  *scripting,
                           const gchar   *name,
                           const gchar   *code,
                           GError       **error);
```

Executes Python code from a string.

**Example:**
```c
lrg_scripting_load_string (LRG_SCRIPTING (scripting),
                           "setup",
                           "from gi.repository import Libregnum\n"
                           "player = engine.get_registry().create('player', name='Hero')\n"
                           "print(f'Created: {player.get_property(\"name\")}')",
                           &error);
```

## Globals

### lrg_scripting_get_global / lrg_scripting_set_global

```c
gboolean
lrg_scripting_get_global (LrgScripting  *scripting,
                          const gchar   *name,
                          GValue        *value,
                          GError       **error);

gboolean
lrg_scripting_set_global (LrgScripting  *scripting,
                          const gchar   *name,
                          const GValue  *value,
                          GError       **error);
```

Access and set Python global variables.

**Example:**
```c
/* Set a global from C */
GValue value = G_VALUE_INIT;
g_value_init (&value, G_TYPE_INT);
g_value_set_int (&value, 42);
lrg_scripting_set_global (LRG_SCRIPTING (scripting), "score", &value, NULL);
g_value_unset (&value);

/* Script can access it */
lrg_scripting_load_string (LRG_SCRIPTING (scripting),
                           "use_score",
                           "print(f'Score: {score}')",
                           NULL);
```

## Function Calls

### lrg_scripting_call_function

```c
gboolean
lrg_scripting_call_function (LrgScripting  *scripting,
                             const gchar   *func_name,
                             GValue        *return_value,
                             guint          n_args,
                             const GValue  *args,
                             GError       **error);
```

Calls a Python function defined in the global namespace.

**Example:**
```c
/* Define a function in Python */
lrg_scripting_load_string (LRG_SCRIPTING (scripting),
                           "funcs",
                           "def calculate_damage(base, multiplier):\n"
                           "    return int(base * multiplier)",
                           NULL);

/* Call from C */
GValue args[2] = { G_VALUE_INIT, G_VALUE_INIT };
GValue result = G_VALUE_INIT;

g_value_init (&args[0], G_TYPE_INT64);
g_value_set_int64 (&args[0], 100);
g_value_init (&args[1], G_TYPE_DOUBLE);
g_value_set_double (&args[1], 1.5);

lrg_scripting_call_function (LRG_SCRIPTING (scripting),
                             "calculate_damage",
                             &result, 2, args, &error);

gint64 damage = g_value_get_int64 (&result);  /* 150 */

g_value_unset (&args[0]);
g_value_unset (&args[1]);
g_value_unset (&result);
```

## Python Script API

### Importing Libregnum

After calling `lrg_scripting_gi_require_libregnum()`, scripts can import the library:

```python
from gi.repository import Libregnum

# Access engine (if exposed as global)
registry = engine.get_registry()

# Create objects via registry
player = registry.create("player", name="Hero", health=100)

# Access object properties
print(player.get_property("name"))
player.set_property("health", 50)
```

### Exposed Objects

Objects exposed via `lrg_scripting_gi_expose_object()` are available as globals:

```python
# Assuming engine was exposed as "engine"
registry = engine.get_registry()
asset_manager = engine.get_asset_manager()
data_loader = engine.get_data_loader()
```

### Update Hooks

Update hooks receive delta time as a float:

```python
def game_update(delta):
    """Called each frame with time since last frame."""
    global player_x
    player_x += velocity * delta

def physics_update(delta):
    """Called each frame for physics simulation."""
    apply_gravity(delta)
```

### Using Other GI Libraries

Because PyGObject is used, scripts can import other GI-exposed libraries:

```python
from gi.repository import GLib, Gio, Libregnum

# Use GLib utilities
now = GLib.DateTime.new_now_local()
print(f"Current time: {now.format('%H:%M:%S')}")

# Use Gio for file operations
file = Gio.File.new_for_path("data/config.yaml")
```

## Complete Example

```c
#include <libregnum.h>

int
main (int argc, char *argv[])
{
    g_autoptr(LrgEngine) engine = lrg_engine_get_default ();
    g_autoptr(LrgScriptingPyGObject) scripting = lrg_scripting_pygobject_new ();
    LrgScriptingGI *gi_self = LRG_SCRIPTING_GI (scripting);
    g_autoptr(GError) error = NULL;

    /* Load required typelibs */
    if (!lrg_scripting_gi_require_typelib (gi_self, "GLib", "2.0", &error) ||
        !lrg_scripting_gi_require_libregnum (gi_self, &error))
    {
        g_printerr ("Failed to load typelibs: %s\n", error->message);
        return 1;
    }

    /* Connect scripting to engine */
    lrg_scripting_gi_set_engine (gi_self, engine);
    lrg_scripting_gi_set_registry (gi_self, lrg_engine_get_registry (engine));
    lrg_engine_set_scripting (engine, LRG_SCRIPTING (scripting));

    /* Expose engine to scripts */
    lrg_scripting_gi_expose_object (gi_self, "engine", G_OBJECT (engine), &error);

    /* Add script search path */
    lrg_scripting_gi_add_search_path (gi_self, "scripts");

    /* Load main game script */
    if (!lrg_scripting_load_file (LRG_SCRIPTING (scripting),
                                  "scripts/main.py",
                                  &error))
    {
        g_printerr ("Script error: %s\n", error->message);
        return 1;
    }

    /* Register update hook */
    lrg_scripting_gi_register_update_hook (gi_self, "game_update");

    /* Game loop */
    lrg_engine_startup (engine, NULL);
    while (lrg_engine_is_running (engine))
    {
        gfloat delta = 0.016f;  /* ~60 FPS */
        lrg_scripting_gi_update (gi_self, delta);
        lrg_engine_update (engine, delta);
    }
    lrg_engine_shutdown (engine);

    return 0;
}
```

**scripts/main.py:**
```python
from gi.repository import GLib, Libregnum

# Game state
player_x = 0.0
player_speed = 100.0

def game_update(delta):
    """Main game update function, called each frame."""
    global player_x

    # Move player
    player_x += player_speed * delta

    # Access engine via exposed global
    if engine.get_is_running():
        registry = engine.get_registry()
        # ... game logic using registry ...

print("Game script loaded!")
print(f"Using GLib version: {GLib.MAJOR_VERSION}.{GLib.MINOR_VERSION}")
```

## Error Handling

PyGObject provides detailed error messages from Python exceptions:

```c
g_autoptr(GError) error = NULL;

if (!lrg_scripting_load_string (LRG_SCRIPTING (scripting),
                                "bad_code",
                                "undefined_variable + 1",
                                &error))
{
    g_printerr ("Python error: %s\n", error->message);
    /* Output: Python error: Error executing 'bad_code': NameError: name 'undefined_variable' is not defined */
}
```

## See Also

- [LrgScriptingGI](scripting-gi.md) - Parent class with GI infrastructure
- [LrgScripting](scripting.md) - Abstract base class API
- [LrgScriptingPython](scripting-python.md) - Direct Python C API alternative
- [Scripting Module](index.md) - Module overview
