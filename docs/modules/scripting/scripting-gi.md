# LrgScriptingGI

## Overview

`LrgScriptingGI` is an intermediate derivable class between `LrgScripting` and GObject Introspection-based scripting backends. It provides common infrastructure for languages that use GI bindings (Python via PyGObject, JavaScript via Gjs, etc.).

**This class is abstract and cannot be instantiated directly.** Use concrete implementations like `LrgScriptingPyGObject`.

**Requirements:** `gobject-introspection-1.0` pkg-config

## Type Hierarchy

```
GObject
└── LrgScripting (abstract)
    └── LrgScriptingGI (abstract, derivable)
        └── LrgScriptingPyGObject (final)
```

## Key Features

- **GIRepository Integration**: Load typelibs and expose them to scripts
- **GObject Exposure**: Expose GObject instances as script globals with native bindings
- **Registry Integration**: Connect to `LrgRegistry` for type lookups
- **Engine Integration**: Connect to `LrgEngine` for subsystem access
- **Update Hooks**: Register script functions to be called each frame
- **Search Paths**: Manage module import paths

## Registry Integration

### lrg_scripting_gi_set_registry

```c
void
lrg_scripting_gi_set_registry (LrgScriptingGI *self,
                               LrgRegistry    *registry);
```

Sets the registry used for type lookups. The registry is held as a weak reference.

### lrg_scripting_gi_get_registry

```c
LrgRegistry *
lrg_scripting_gi_get_registry (LrgScriptingGI *self);
```

Gets the current registry, or NULL if not set.

**Example:**
```c
LrgScriptingGI *gi_self = LRG_SCRIPTING_GI (scripting);
LrgRegistry *registry = lrg_engine_get_registry (engine);

lrg_scripting_gi_set_registry (gi_self, registry);
```

## Engine Integration

### lrg_scripting_gi_set_engine

```c
void
lrg_scripting_gi_set_engine (LrgScriptingGI *self,
                             LrgEngine      *engine);
```

Sets the engine instance to expose to scripts. The engine is held as a weak reference.

### lrg_scripting_gi_get_engine

```c
LrgEngine *
lrg_scripting_gi_get_engine (LrgScriptingGI *self);
```

Gets the current engine, or NULL if not set.

**Example:**
```c
LrgScriptingGI *gi_self = LRG_SCRIPTING_GI (scripting);
LrgEngine *engine = lrg_engine_get_default ();

lrg_scripting_gi_set_engine (gi_self, engine);
```

## Search Paths

### lrg_scripting_gi_add_search_path

```c
void
lrg_scripting_gi_add_search_path (LrgScriptingGI *self,
                                  const gchar    *path);
```

Adds a directory to the script search path. This allows scripts to import modules from the specified directory.

### lrg_scripting_gi_clear_search_paths

```c
void
lrg_scripting_gi_clear_search_paths (LrgScriptingGI *self);
```

Clears all custom search paths. Default language-specific paths are preserved.

### lrg_scripting_gi_get_search_paths

```c
const gchar * const *
lrg_scripting_gi_get_search_paths (LrgScriptingGI *self);
```

Gets the list of custom search paths as a NULL-terminated array.

**Example:**
```c
/* Add mod script directory */
lrg_scripting_gi_add_search_path (gi_self, "mods/mymod/scripts");

/* Scripts can now import from this directory */
```

## Update Hooks

Update hooks are script functions called every frame with delta time.

### lrg_scripting_gi_register_update_hook

```c
void
lrg_scripting_gi_register_update_hook (LrgScriptingGI *self,
                                       const gchar    *func_name);
```

Registers a script function to be called each frame. Multiple hooks can be registered.

### lrg_scripting_gi_unregister_update_hook

```c
gboolean
lrg_scripting_gi_unregister_update_hook (LrgScriptingGI *self,
                                         const gchar    *func_name);
```

Unregisters an update hook. Returns TRUE if the hook was found and removed.

### lrg_scripting_gi_clear_update_hooks

```c
void
lrg_scripting_gi_clear_update_hooks (LrgScriptingGI *self);
```

Clears all registered update hooks.

### lrg_scripting_gi_update

```c
void
lrg_scripting_gi_update (LrgScriptingGI *self,
                         gfloat          delta);
```

Calls all registered update hooks with the given delta time. Errors in individual hooks are logged but do not stop other hooks.

**Example:**
```c
/* Register hooks */
lrg_scripting_gi_register_update_hook (gi_self, "game_update");
lrg_scripting_gi_register_update_hook (gi_self, "physics_update");

/* In game loop */
while (running)
{
    gfloat delta = get_delta_time ();
    lrg_scripting_gi_update (gi_self, delta);
}
```

## Typelib Loading

### lrg_scripting_gi_require_typelib

```c
gboolean
lrg_scripting_gi_require_typelib (LrgScriptingGI  *self,
                                  const gchar     *namespace_,
                                  const gchar     *version,
                                  GError         **error);
```

Loads a typelib via GIRepository and exposes it to the interpreter. This allows scripts to access GI-exposed types.

### lrg_scripting_gi_require_libregnum

```c
gboolean
lrg_scripting_gi_require_libregnum (LrgScriptingGI  *self,
                                    GError         **error);
```

Convenience function to load the Libregnum typelib (version "1").

**Example:**
```c
g_autoptr(GError) error = NULL;

/* Load GLib and Libregnum typelibs */
if (!lrg_scripting_gi_require_typelib (gi_self, "GLib", "2.0", &error))
{
    g_printerr ("Failed to load GLib: %s\n", error->message);
    return FALSE;
}

if (!lrg_scripting_gi_require_libregnum (gi_self, &error))
{
    g_printerr ("Failed to load Libregnum: %s\n", error->message);
    return FALSE;
}

/* Scripts can now use:
 * from gi.repository import GLib, Libregnum
 */
```

## GObject Exposure

### lrg_scripting_gi_expose_object

```c
gboolean
lrg_scripting_gi_expose_object (LrgScriptingGI  *self,
                                const gchar     *name,
                                GObject         *object,
                                GError         **error);
```

Exposes a GObject instance to scripts as a named global. The object is wrapped using the language's native GI bindings.

**Example:**
```c
LrgEngine *engine = lrg_engine_get_default ();

/* Expose engine to scripts as 'engine' global */
lrg_scripting_gi_expose_object (gi_self, "engine", G_OBJECT (engine), &error);

/* Scripts can now access:
 * registry = engine.get_registry()
 */
```

## Implementing a Subclass

To create a new GI-based scripting backend, implement these virtual methods:

| Method | Required | Description |
|--------|----------|-------------|
| `init_interpreter` | Yes | Set up language runtime |
| `finalize_interpreter` | Yes | Clean up resources |
| `expose_typelib` | Yes | Make loaded typelib available |
| `expose_gobject` | Yes | Wrap and expose GObject |
| `call_update_hook` | Yes | Invoke update function |
| `update_search_paths` | No | Update import paths |
| `get_interpreter_name` | Yes | Return name for logging |

**Example subclass structure:**
```c
G_DEFINE_TYPE (MyScripting, my_scripting, LRG_TYPE_SCRIPTING_GI)

static gboolean
my_scripting_init_interpreter (LrgScriptingGI *self, GError **error)
{
    /* Initialize your interpreter here */
    return TRUE;
}

static void
my_scripting_class_init (MyScriptingClass *klass)
{
    LrgScriptingGIClass *gi_class = LRG_SCRIPTING_GI_CLASS (klass);

    gi_class->init_interpreter = my_scripting_init_interpreter;
    /* ... other virtual methods ... */
}
```

## See Also

- [LrgScripting](scripting.md) - Abstract base class
- [LrgScriptingPyGObject](scripting-pygobject.md) - PyGObject implementation
- [Scripting Module](index.md) - Module overview
