# LrgScriptingLua

## Overview

`LrgScriptingLua` is the LuaJIT scripting backend. It provides a fast, lightweight scripting environment ideal for game logic and modding.

**Requirements:** LuaJIT 2.1+ (via `luajit` pkg-config)

## Construction

### lrg_scripting_lua_new

```c
LrgScriptingLua *
lrg_scripting_lua_new (void);
```

Creates a new LuaJIT scripting context with standard libraries loaded.

**Example:**
```c
g_autoptr(LrgScriptingLua) scripting = lrg_scripting_lua_new ();

/* Execute Lua code */
lrg_scripting_load_string (LRG_SCRIPTING (scripting),
                           "hello",
                           "print('Hello from Lua!')",
                           NULL);
```

## Registry Integration

### lrg_scripting_lua_set_registry

```c
void
lrg_scripting_lua_set_registry (LrgScriptingLua *self,
                                LrgRegistry     *registry);
```

Sets the registry for type lookups, enabling object creation from Lua.

### lrg_scripting_lua_get_registry

```c
LrgRegistry *
lrg_scripting_lua_get_registry (LrgScriptingLua *self);
```

Gets the current registry, or NULL if not set.

**Example:**
```c
LrgRegistry *registry = lrg_engine_get_registry (engine);
lrg_scripting_lua_set_registry (scripting, registry);

/* Now Lua can create objects */
lrg_scripting_load_string (LRG_SCRIPTING (scripting), "create",
    "local player = Registry:create('player', { name = 'Hero' })",
    NULL);
```

## Search Paths

### lrg_scripting_lua_add_search_path

```c
void
lrg_scripting_lua_add_search_path (LrgScriptingLua *self,
                                   const gchar     *path);
```

Adds a directory to `package.path`, enabling `require()` to find modules.

### lrg_scripting_lua_clear_search_paths

```c
void
lrg_scripting_lua_clear_search_paths (LrgScriptingLua *self);
```

Removes all custom search paths.

**Example:**
```c
/* Add mod script directory */
lrg_scripting_lua_add_search_path (scripting, "mods/mymod/scripts");

/* Now scripts can use require */
lrg_scripting_load_string (LRG_SCRIPTING (scripting), "init",
    "local utils = require('utils')",  /* Loads mods/mymod/scripts/utils.lua */
    NULL);
```

## Update Hooks

Update hooks are Lua functions called every frame. Use them for game loop integration.

### lrg_scripting_lua_register_update_hook

```c
void
lrg_scripting_lua_register_update_hook (LrgScriptingLua *self,
                                        const gchar     *func_name);
```

Register a Lua function to be called each frame.

### lrg_scripting_lua_unregister_update_hook

```c
gboolean
lrg_scripting_lua_unregister_update_hook (LrgScriptingLua *self,
                                          const gchar     *func_name);
```

Remove an update hook.

### lrg_scripting_lua_clear_update_hooks

```c
void
lrg_scripting_lua_clear_update_hooks (LrgScriptingLua *self);
```

Remove all update hooks.

### lrg_scripting_lua_update

```c
void
lrg_scripting_lua_update (LrgScriptingLua *self,
                          gfloat           delta);
```

Call all registered update hooks with the delta time.

**Example:**
```c
/* Define update function in Lua */
lrg_scripting_load_string (LRG_SCRIPTING (scripting), "game",
    "player_x = 0\n"
    "function game_update(delta)\n"
    "    player_x = player_x + 100 * delta  -- Move 100 units/second\n"
    "end",
    NULL);

/* Register the hook */
lrg_scripting_lua_register_update_hook (scripting, "game_update");

/* In game loop */
while (running)
{
    gfloat delta = /* calculate delta time */;
    lrg_scripting_lua_update (scripting, delta);
    /* ... render, etc ... */
}
```

## Engine Access

### lrg_scripting_lua_set_engine

```c
void
lrg_scripting_lua_set_engine (LrgScriptingLua *self,
                              LrgEngine       *engine);
```

Exposes the engine to Lua as the `Engine` global.

### lrg_scripting_lua_get_engine

```c
LrgEngine *
lrg_scripting_lua_get_engine (LrgScriptingLua *self);
```

Gets the current engine, or NULL if not set.

## Lua API Reference

### Log

```lua
Log:debug(message)   -- Debug level logging
Log:info(message)    -- Info level logging
Log:warning(message) -- Warning level logging
Log:error(message)   -- Error level logging
```

### Registry

```lua
-- Create an object with properties
local obj = Registry:create("type-name", { prop1 = value1, prop2 = value2 })

-- Check if a type is registered
local exists = Registry:is_registered("type-name")

-- Get all registered type names
local types = Registry:get_types()
```

### Engine

```lua
-- Access engine subsystems
local registry = Engine.registry
local assets = Engine.asset_manager
local loader = Engine.data_loader

-- Check engine state
local state = Engine.state
local running = Engine.is_running
```

### GObject Properties

GObjects returned from C or created via Registry have automatic property access:

```lua
local player = Registry:create("player", { name = "Hero" })

-- Read property
print(player.name)  -- "Hero"

-- Write property
player.health = 100
```

## Complete Example

```c
#include <libregnum.h>

int
main (int argc, char *argv[])
{
    g_autoptr(LrgEngine) engine = lrg_engine_get_default ();
    g_autoptr(LrgScriptingLua) scripting = lrg_scripting_lua_new ();
    g_autoptr(GError) error = NULL;

    /* Connect to engine */
    lrg_scripting_lua_set_engine (scripting, engine);
    lrg_scripting_lua_set_registry (scripting, lrg_engine_get_registry (engine));
    lrg_engine_set_scripting (engine, LRG_SCRIPTING (scripting));

    /* Add script search path */
    lrg_scripting_lua_add_search_path (scripting, "scripts");

    /* Load main script */
    if (!lrg_scripting_load_file (LRG_SCRIPTING (scripting),
                                  "scripts/main.lua",
                                  &error))
    {
        g_printerr ("Script error: %s\n", error->message);
        return 1;
    }

    /* Register update hook */
    lrg_scripting_lua_register_update_hook (scripting, "game_update");

    /* Game loop */
    lrg_engine_startup (engine, NULL);
    while (lrg_engine_is_running (engine))
    {
        gfloat delta = 0.016f;
        lrg_scripting_lua_update (scripting, delta);
        lrg_engine_update (engine, delta);
    }
    lrg_engine_shutdown (engine);

    return 0;
}
```

## See Also

- [LrgScripting](scripting.md) - Base class API
- [LrgScriptingPython](scripting-python.md) - Python alternative
- [Scripting Examples](../../examples/scripting-basics.md) - More examples
