# LrgScriptingGjs

## Overview

`LrgScriptingGjs` is the Gjs (GNOME JavaScript) scripting backend. It uses native GObject Introspection bindings via SpiderMonkey (Mozilla's JavaScript engine), allowing scripts to access the full Libregnum GObject Introspection API directly.

**Requirements:**
- Gjs 1.50+ (`gjs-1.0` pkg-config)
- GObject Introspection runtime (`gobject-introspection-1.0` pkg-config)

**Installation (Fedora):**
```bash
sudo dnf install gjs-devel
```

## Type Hierarchy

```
GObject
└── LrgScripting (abstract)
    └── LrgScriptingGI (abstract, derivable)
        └── LrgScriptingGjs (final)
```

## When to Use

Choose `LrgScriptingGjs` when:

- Scripts need access to the full Libregnum type system
- You want native GI bindings (`imports.gi.Libregnum`)
- Interoperating with other GI-based libraries (GLib, Gio, Gtk, etc.)
- Building modding systems that leverage the type system
- You prefer JavaScript syntax over Python

Choose `LrgScriptingPyGObject` when:

- You prefer Python syntax
- You need Python's extensive package ecosystem
- Your modding community is more familiar with Python

Choose `LrgScriptingLua` when:

- You need lightweight, fast scripting without GI overhead
- Scripts don't need direct access to Libregnum types
- Performance is critical (LuaJIT)

## Construction

### lrg_scripting_gjs_new

```c
LrgScriptingGjs *
lrg_scripting_gjs_new (void);
```

Creates a new Gjs-based JavaScript scripting context. The interpreter is initialized automatically with SpiderMonkey and GI bindings ready.

**Example:**
```c
g_autoptr(LrgScriptingGjs) scripting = lrg_scripting_gjs_new ();
g_autoptr(GError) error = NULL;

/* Execute JavaScript code */
lrg_scripting_load_string (LRG_SCRIPTING (scripting),
                           "hello",
                           "print('Hello from Gjs!');",
                           &error);
```

## Typical Setup

The recommended setup pattern for `LrgScriptingGjs`:

```c
g_autoptr(LrgScriptingGjs) scripting = lrg_scripting_gjs_new ();
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

/* 4. Add script search paths (before loading scripts) */
lrg_scripting_gi_add_search_path (gi_self, "scripts");
lrg_scripting_gi_add_search_path (gi_self, "mods/mymod/scripts");

/* 5. Load and run scripts */
lrg_scripting_load_file (LRG_SCRIPTING (scripting), "scripts/main.js", &error);

/* 6. Register update hooks */
lrg_scripting_gi_register_update_hook (gi_self, "game_update");
```

## Inherited API (from LrgScriptingGI)

`LrgScriptingGjs` inherits all functionality from `LrgScriptingGI`:

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

Loads and executes a JavaScript file.

### lrg_scripting_load_string

```c
gboolean
lrg_scripting_load_string (LrgScripting  *scripting,
                           const gchar   *name,
                           const gchar   *code,
                           GError       **error);
```

Executes JavaScript code from a string.

**Example:**
```c
lrg_scripting_load_string (LRG_SCRIPTING (scripting),
                           "setup",
                           "const Libregnum = imports.gi.Libregnum;\n"
                           "let player = engine.get_registry().create('player', {name: 'Hero'});\n"
                           "print('Created: ' + player.get_property('name'));",
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

Access and set JavaScript global variables.

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
                           "print('Score: ' + score);",
                           NULL);
```

**Note:** Gjs uses `globalThis` for global variable access. Variables set via `set_global` are stored on `globalThis`.

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

Calls a JavaScript function defined in the global namespace.

**Example:**
```c
/* Define a function in JavaScript */
lrg_scripting_load_string (LRG_SCRIPTING (scripting),
                           "funcs",
                           "function calculate_damage(base, multiplier) {\n"
                           "    return Math.floor(base * multiplier);\n"
                           "}",
                           NULL);

/* Call from C */
GValue args[2] = { G_VALUE_INIT, G_VALUE_INIT };
GValue result = G_VALUE_INIT;

g_value_init (&args[0], G_TYPE_DOUBLE);
g_value_set_double (&args[0], 100.0);
g_value_init (&args[1], G_TYPE_DOUBLE);
g_value_set_double (&args[1], 1.5);

lrg_scripting_call_function (LRG_SCRIPTING (scripting),
                             "calculate_damage",
                             &result, 2, args, &error);

/* Result: 150 */

g_value_unset (&args[0]);
g_value_unset (&args[1]);
g_value_unset (&result);
```

## JavaScript Script API

### Importing Libraries

After calling `lrg_scripting_gi_require_libregnum()`, scripts can import the library:

```javascript
const GLib = imports.gi.GLib;
const Gio = imports.gi.Gio;
const Libregnum = imports.gi.Libregnum;

// Access engine (if exposed as global)
let registry = engine.get_registry();

// Create objects via registry
let player = registry.create("player", {name: "Hero", health: 100});

// Access object properties
print(player.get_property("name"));
player.set_property("health", 50);
```

### Exposed Objects

Objects exposed via `lrg_scripting_gi_expose_object()` are available as globals:

```javascript
// Assuming engine was exposed as "engine"
let registry = engine.get_registry();
let asset_manager = engine.get_asset_manager();
let data_loader = engine.get_data_loader();
```

### Update Hooks

Update hooks receive delta time as a number:

```javascript
function game_update(delta) {
    // Called each frame with time since last frame
    player_x += velocity * delta;
}

function physics_update(delta) {
    // Called each frame for physics simulation
    apply_gravity(delta);
}
```

### Using Other GI Libraries

Because Gjs is used, scripts can import other GI-exposed libraries:

```javascript
const GLib = imports.gi.GLib;
const Gio = imports.gi.Gio;
const Libregnum = imports.gi.Libregnum;

// Use GLib utilities
let now = GLib.DateTime.new_now_local();
print("Current time: " + now.format("%H:%M:%S"));

// Use Gio for file operations
let file = Gio.File.new_for_path("data/config.yaml");

// Check GLib version
print("Using GLib " + GLib.MAJOR_VERSION + "." + GLib.MINOR_VERSION);
```

## Complete Example

```c
#include <libregnum.h>

int
main (int argc, char *argv[])
{
    g_autoptr(LrgEngine) engine = lrg_engine_get_default ();
    g_autoptr(LrgScriptingGjs) scripting = lrg_scripting_gjs_new ();
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
                                  "scripts/main.js",
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

**scripts/main.js:**
```javascript
const GLib = imports.gi.GLib;
const Libregnum = imports.gi.Libregnum;

// Game state
let player_x = 0.0;
const player_speed = 100.0;

function game_update(delta) {
    // Move player
    player_x += player_speed * delta;

    // Access engine via exposed global
    if (engine.get_is_running()) {
        let registry = engine.get_registry();
        // ... game logic using registry ...
    }
}

print("Game script loaded!");
print("Using GLib version: " + GLib.MAJOR_VERSION + "." + GLib.MINOR_VERSION);
```

## Gjs-Specific Considerations

### Global Variables

Gjs uses `globalThis` for global scope. Variables set from C are stored on `globalThis`:

```javascript
// Variables set via lrg_scripting_set_global are on globalThis
print(globalThis.score);  // Access global set from C
print(score);             // Also works (shorthand)
```

### Error Handling

Gjs logs JavaScript errors as CRITICAL messages. The scripting backend handles this appropriately, but be aware when debugging:

```c
g_autoptr(GError) error = NULL;

if (!lrg_scripting_load_string (LRG_SCRIPTING (scripting),
                                "bad_code",
                                "undefined_variable + 1;",
                                &error))
{
    g_printerr ("JavaScript error: %s\n", error->message);
    /* Output: JavaScript error: ReferenceError: undefined_variable is not defined */
}
```

### Search Paths

For best results, add search paths before loading scripts. The search paths affect `imports` resolution:

```c
/* Add paths before loading */
lrg_scripting_gi_add_search_path (gi_self, "scripts");
lrg_scripting_gi_add_search_path (gi_self, "mods/mymod");

/* Then load scripts */
lrg_scripting_load_file (LRG_SCRIPTING (scripting), "scripts/main.js", &error);
```

### Importing Custom Modules

JavaScript modules in search paths can be imported:

```javascript
// If "scripts/utils.js" exists and "scripts" is in search path
const Utils = imports.utils;

Utils.doSomething();
```

## Comparison with PyGObject

| Aspect | Gjs | PyGObject |
|--------|-----|-----------|
| Language | JavaScript (ES6+) | Python 3.12+ |
| Engine | SpiderMonkey | CPython |
| GI Access | `imports.gi.Namespace` | `from gi.repository import Namespace` |
| Syntax | C-like braces | Indentation-based |
| Package ecosystem | npm (limited GI compat) | pip (extensive) |
| GNOME integration | Native (default for GNOME apps) | Well supported |

## See Also

- [LrgScriptingGI](scripting-gi.md) - Parent class with GI infrastructure
- [LrgScripting](scripting.md) - Abstract base class API
- [LrgScriptingPyGObject](scripting-pygobject.md) - Python alternative with GI
- [Scripting Module](index.md) - Module overview
