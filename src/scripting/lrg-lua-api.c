/* lrg-lua-api.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Built-in Lua API implementation.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SCRIPTING

#include "lrg-lua-api.h"
#include "lrg-lua-bridge.h"
#include "lrg-scripting-lua-private.h"
#include "../lrg-log.h"
#include "../core/lrg-engine.h"
#include "../core/lrg-registry.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

/* Registry keys for storing references */
#define LRG_LUA_SCRIPTING_KEY  "LrgScripting"
#define LRG_LUA_ENGINE_KEY     "LrgEngine"
#define LRG_LUA_REGISTRY_KEY   "LrgRegistry"

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

/*
 * Get the registry from the Lua state.
 */
static LrgRegistry *
get_registry (lua_State *L)
{
    LrgRegistry *registry;

    lua_getfield (L, LUA_REGISTRYINDEX, LRG_LUA_REGISTRY_KEY);
    registry = (LrgRegistry *)lua_touserdata (L, -1);
    lua_pop (L, 1);

    return registry;
}

/* ==========================================================================
 * Log API
 * ========================================================================== */

/*
 * Format a log message from Lua arguments.
 * Uses printf-style formatting if multiple arguments provided.
 */
static gchar *
format_log_message (lua_State *L,
                    gint       start_index)
{
    GString     *msg;
    const gchar *format;
    gint         nargs;
    gint         i;

    nargs = lua_gettop (L);
    if (nargs < start_index)
        return g_strdup ("");

    format = luaL_checkstring (L, start_index);

    if (nargs == start_index)
        return g_strdup (format);

    /* Simple printf-style formatting */
    msg = g_string_new (NULL);

    i = start_index + 1;
    while (*format)
    {
        if (*format == '%' && *(format + 1) != '\0')
        {
            format++;
            switch (*format)
            {
            case 's':
                if (i <= nargs)
                {
                    const gchar *str = lua_tostring (L, i++);
                    if (str != NULL)
                        g_string_append (msg, str);
                    else
                        g_string_append (msg, "(nil)");
                }
                break;

            case 'd':
            case 'i':
                if (i <= nargs)
                    g_string_append_printf (msg, "%d", (gint)lua_tointeger (L, i++));
                break;

            case 'f':
                if (i <= nargs)
                    g_string_append_printf (msg, "%f", lua_tonumber (L, i++));
                break;

            case '%':
                g_string_append_c (msg, '%');
                break;

            default:
                g_string_append_c (msg, '%');
                g_string_append_c (msg, *format);
                break;
            }
        }
        else
        {
            g_string_append_c (msg, *format);
        }
        format++;
    }

    return g_string_free (msg, FALSE);
}

static int
log_debug (lua_State *L)
{
    g_autofree gchar *msg = format_log_message (L, 1);
    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "[Lua] %s", msg);
    return 0;
}

static int
log_info (lua_State *L)
{
    g_autofree gchar *msg = format_log_message (L, 1);
    lrg_info (LRG_LOG_DOMAIN_SCRIPTING, "[Lua] %s", msg);
    return 0;
}

static int
log_warning (lua_State *L)
{
    g_autofree gchar *msg = format_log_message (L, 1);
    lrg_warning (LRG_LOG_DOMAIN_SCRIPTING, "[Lua] %s", msg);
    return 0;
}

static int
log_error (lua_State *L)
{
    g_autofree gchar *msg = format_log_message (L, 1);
    lrg_error (LRG_LOG_DOMAIN_SCRIPTING, "[Lua] %s", msg);
    return 0;
}

/**
 * lrg_lua_api_register_log:
 *
 * Registers the Log global.
 */
void
lrg_lua_api_register_log (lua_State *L)
{
    lua_newtable (L);

    lua_pushcfunction (L, log_debug);
    lua_setfield (L, -2, "debug");

    lua_pushcfunction (L, log_info);
    lua_setfield (L, -2, "info");

    lua_pushcfunction (L, log_warning);
    lua_setfield (L, -2, "warning");

    lua_pushcfunction (L, log_error);
    lua_setfield (L, -2, "error");

    lua_setglobal (L, "Log");
}

/* ==========================================================================
 * Registry API
 * ========================================================================== */

/*
 * Registry:create(type_name, [properties])
 *
 * Creates a new object of the specified type with optional properties.
 */
static int
registry_create (lua_State *L)
{
    LrgRegistry  *registry;
    const gchar  *type_name;
    GType         gtype;
    GObject      *object;
    GObjectClass *klass;

    registry = get_registry (L);
    if (registry == NULL)
    {
        return luaL_error (L, "No registry available");
    }

    type_name = luaL_checkstring (L, 2);

    /* Look up the type */
    gtype = lrg_registry_lookup (registry, type_name);
    if (gtype == G_TYPE_INVALID)
    {
        return luaL_error (L, "Type '%s' is not registered", type_name);
    }

    /* Create the object */
    object = g_object_new (gtype, NULL);

    /* Apply properties from table if provided */
    if (lua_istable (L, 3))
    {
        klass = G_OBJECT_GET_CLASS (object);

        lua_pushnil (L);
        while (lua_next (L, 3) != 0)
        {
            const gchar *prop_name;
            GParamSpec  *pspec;

            if (lua_type (L, -2) == LUA_TSTRING)
            {
                prop_name = lua_tostring (L, -2);
                pspec = g_object_class_find_property (klass, prop_name);

                if (pspec != NULL && (pspec->flags & G_PARAM_WRITABLE))
                {
                    GValue value = G_VALUE_INIT;

                    if (lrg_lua_to_gvalue_with_type (L, -1, pspec->value_type, &value))
                    {
                        g_object_set_property (object, prop_name, &value);
                        g_value_unset (&value);
                    }
                }
            }
            lua_pop (L, 1);  /* Pop value, keep key */
        }
    }

    /* Push the object to Lua */
    lrg_lua_push_gobject (L, object);

    /* We transferred ownership to Lua */
    g_object_unref (object);

    return 1;
}

/*
 * Registry:is_registered(type_name)
 *
 * Returns true if the type is registered.
 */
static int
registry_is_registered (lua_State *L)
{
    LrgRegistry *registry;
    const gchar *type_name;

    registry = get_registry (L);
    if (registry == NULL)
    {
        lua_pushboolean (L, FALSE);
        return 1;
    }

    type_name = luaL_checkstring (L, 2);
    lua_pushboolean (L, lrg_registry_is_registered (registry, type_name));

    return 1;
}

/*
 * RegistryForeachData:
 *
 * Data passed to the registry foreach callback.
 */
typedef struct
{
    lua_State *L;
} RegistryForeachData;

/*
 * Callback for lrg_registry_foreach.
 */
static void
registry_types_foreach (const gchar *name,
                        GType        gtype,
                        gpointer     user_data)
{
    RegistryForeachData *data = user_data;

    lua_pushstring (data->L, name);
    lua_pushinteger (data->L, (lua_Integer)gtype);
    lua_rawset (data->L, -3);
}

/*
 * Registry:get_types()
 *
 * Returns a table of all registered types (name -> gtype).
 */
static int
registry_get_types (lua_State *L)
{
    LrgRegistry        *registry;
    RegistryForeachData data;

    registry = get_registry (L);
    if (registry == NULL)
    {
        lua_newtable (L);
        return 1;
    }

    lua_newtable (L);
    data.L = L;

    lrg_registry_foreach (registry, registry_types_foreach, &data);

    return 1;
}

/*
 * Registry metatable __index method.
 */
static int
registry_index (lua_State *L)
{
    const gchar *key;

    key = luaL_checkstring (L, 2);

    if (g_strcmp0 (key, "create") == 0)
    {
        lua_pushcfunction (L, registry_create);
        return 1;
    }
    else if (g_strcmp0 (key, "is_registered") == 0)
    {
        lua_pushcfunction (L, registry_is_registered);
        return 1;
    }
    else if (g_strcmp0 (key, "get_types") == 0)
    {
        lua_pushcfunction (L, registry_get_types);
        return 1;
    }

    lua_pushnil (L);
    return 1;
}

/**
 * lrg_lua_api_register_registry:
 *
 * Registers the Registry global.
 */
void
lrg_lua_api_register_registry (lua_State       *L,
                               LrgScriptingLua *scripting)
{
    LrgRegistry *registry;

    (void)scripting;  /* May be used later */

    /* Create Registry table */
    lua_newtable (L);

    /* Create metatable */
    lua_newtable (L);
    lua_pushcfunction (L, registry_index);
    lua_setfield (L, -2, "__index");
    lua_setmetatable (L, -2);

    lua_setglobal (L, "Registry");

    /* Store registry reference if available */
    registry = lrg_scripting_lua_get_registry (scripting);
    if (registry != NULL)
    {
        lua_pushlightuserdata (L, registry);
    }
    else
    {
        lua_pushnil (L);
    }
    lua_setfield (L, LUA_REGISTRYINDEX, LRG_LUA_REGISTRY_KEY);
}

/**
 * lrg_lua_api_update_registry:
 *
 * Updates the Registry reference.
 */
void
lrg_lua_api_update_registry (lua_State   *L,
                             LrgRegistry *registry)
{
    if (registry != NULL)
    {
        lua_pushlightuserdata (L, registry);
    }
    else
    {
        lua_pushnil (L);
    }
    lua_setfield (L, LUA_REGISTRYINDEX, LRG_LUA_REGISTRY_KEY);
}

/* ==========================================================================
 * Engine API
 * ========================================================================== */

/*
 * Engine metatable __index method.
 * Provides access to engine properties and subsystems.
 */
static int
engine_index (lua_State *L)
{
    LrgEngine   *engine;
    const gchar *key;

    lua_getfield (L, LUA_REGISTRYINDEX, LRG_LUA_ENGINE_KEY);
    engine = (LrgEngine *)lua_touserdata (L, -1);
    lua_pop (L, 1);

    if (engine == NULL)
    {
        lua_pushnil (L);
        return 1;
    }

    key = luaL_checkstring (L, 2);

    if (g_strcmp0 (key, "state") == 0)
    {
        lua_pushinteger (L, (lua_Integer)lrg_engine_get_state (engine));
        return 1;
    }
    else if (g_strcmp0 (key, "registry") == 0)
    {
        lrg_lua_push_gobject (L, G_OBJECT (lrg_engine_get_registry (engine)));
        return 1;
    }
    else if (g_strcmp0 (key, "data_loader") == 0)
    {
        lrg_lua_push_gobject (L, G_OBJECT (lrg_engine_get_data_loader (engine)));
        return 1;
    }
    else if (g_strcmp0 (key, "asset_manager") == 0)
    {
        lrg_lua_push_gobject (L, G_OBJECT (lrg_engine_get_asset_manager (engine)));
        return 1;
    }
    else if (g_strcmp0 (key, "is_running") == 0)
    {
        lua_pushboolean (L, lrg_engine_is_running (engine));
        return 1;
    }
    else if (g_strcmp0 (key, "connect") == 0)
    {
        /* Return a connect function that works on the engine */
        lrg_lua_push_gobject (L, G_OBJECT (engine));
        lua_getfield (L, -1, "connect");
        lua_remove (L, -2);  /* Remove the engine userdata */
        return 1;
    }

    lua_pushnil (L);
    return 1;
}

/**
 * lrg_lua_api_register_engine:
 *
 * Registers the Engine global.
 */
void
lrg_lua_api_register_engine (lua_State       *L,
                             LrgScriptingLua *scripting)
{
    LrgEngine *engine;

    /* Create Engine table */
    lua_newtable (L);

    /* Create metatable */
    lua_newtable (L);
    lua_pushcfunction (L, engine_index);
    lua_setfield (L, -2, "__index");
    lua_setmetatable (L, -2);

    lua_setglobal (L, "Engine");

    /* Store engine reference if available */
    engine = lrg_scripting_lua_get_engine (scripting);
    if (engine != NULL)
    {
        lua_pushlightuserdata (L, engine);
    }
    else
    {
        lua_pushnil (L);
    }
    lua_setfield (L, LUA_REGISTRYINDEX, LRG_LUA_ENGINE_KEY);
}

/**
 * lrg_lua_api_update_engine:
 *
 * Updates the Engine reference.
 */
void
lrg_lua_api_update_engine (lua_State *L,
                           LrgEngine *engine)
{
    if (engine != NULL)
    {
        lua_pushlightuserdata (L, engine);
    }
    else
    {
        lua_pushnil (L);
    }
    lua_setfield (L, LUA_REGISTRYINDEX, LRG_LUA_ENGINE_KEY);
}

/* ==========================================================================
 * Main Registration
 * ========================================================================== */

/**
 * lrg_lua_api_register_all:
 *
 * Registers all built-in API globals.
 */
void
lrg_lua_api_register_all (lua_State       *L,
                          LrgScriptingLua *scripting)
{
    /* Store scripting context reference */
    lua_pushlightuserdata (L, scripting);
    lua_setfield (L, LUA_REGISTRYINDEX, LRG_LUA_SCRIPTING_KEY);

    /* Register metatables */
    lrg_lua_register_gobject_metatable (L);
    lrg_lua_register_weak_table (L);

    /* Register globals */
    lrg_lua_api_register_log (L);
    lrg_lua_api_register_registry (L, scripting);
    lrg_lua_api_register_engine (L, scripting);
}
