/* lrg-scripting-lua.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LuaJIT scripting backend implementation.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SCRIPTING

#include "lrg-scripting-lua.h"
#include "lrg-scripting-lua-private.h"
#include "lrg-lua-bridge.h"
#include "lrg-lua-api.h"
#include "../lrg-log.h"
#include "../core/lrg-engine.h"
#include "../core/lrg-registry.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

struct _LrgScriptingLua
{
    LrgScripting  parent_instance;

    lua_State    *L;                /* Main Lua state */
    LrgRegistry  *registry;         /* Type registry (weak ref) */
    LrgEngine    *engine;           /* Engine (weak ref) */
    GPtrArray    *update_hooks;     /* Array of function names (gchar *) */
    GPtrArray    *search_paths;     /* Array of search paths (gchar *) */
    gchar        *default_path;     /* Default Lua package path */
};

G_DEFINE_TYPE (LrgScriptingLua, lrg_scripting_lua, LRG_TYPE_SCRIPTING)

/* ==========================================================================
 * Private Functions
 * ========================================================================== */

/*
 * Lua panic handler.
 */
static int
lua_panic (lua_State *L)
{
    const char *msg = lua_tostring (L, -1);

    lrg_error (LRG_LOG_DOMAIN_SCRIPTING, "Lua PANIC: %s", msg ? msg : "(no message)");

    return 0;
}

/*
 * Update the Lua package path with custom search paths.
 */
static void
update_package_path (LrgScriptingLua *self)
{
    GString *path;
    guint    i;

    if (self->L == NULL)
        return;

    path = g_string_new (NULL);

    /* Add custom paths first (higher priority) */
    for (i = 0; i < self->search_paths->len; i++)
    {
        const gchar *dir = g_ptr_array_index (self->search_paths, i);
        g_string_append_printf (path, "%s/?.lua;", dir);
    }

    /* Add default path */
    if (self->default_path != NULL)
    {
        g_string_append (path, self->default_path);
    }

    /* Set package.path */
    lua_getglobal (self->L, "package");
    if (lua_istable (self->L, -1))
    {
        lua_pushstring (self->L, path->str);
        lua_setfield (self->L, -2, "path");
    }
    lua_pop (self->L, 1);

    g_string_free (path, TRUE);
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static gboolean
lrg_scripting_lua_load_file (LrgScripting  *scripting,
                             const gchar   *path,
                             GError       **error)
{
    LrgScriptingLua *self = LRG_SCRIPTING_LUA (scripting);
    int              result;

    g_return_val_if_fail (self->L != NULL, FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Loading script: %s", path);

    result = luaL_loadfile (self->L, path);
    if (result != 0)
    {
        const gchar *msg = lua_tostring (self->L, -1);
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     result == LUA_ERRSYNTAX ? LRG_SCRIPTING_ERROR_SYNTAX
                                             : LRG_SCRIPTING_ERROR_LOAD,
                     "Failed to load '%s': %s",
                     path, msg ? msg : "(unknown error)");
        lua_pop (self->L, 1);
        return FALSE;
    }

    /* Execute the loaded chunk */
    result = lua_pcall (self->L, 0, 0, 0);
    if (result != 0)
    {
        const gchar *msg = lua_tostring (self->L, -1);
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_RUNTIME,
                     "Error executing '%s': %s",
                     path, msg ? msg : "(unknown error)");
        lua_pop (self->L, 1);
        return FALSE;
    }

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Loaded script: %s", path);

    return TRUE;
}

static gboolean
lrg_scripting_lua_load_string (LrgScripting  *scripting,
                               const gchar   *name,
                               const gchar   *code,
                               GError       **error)
{
    LrgScriptingLua *self = LRG_SCRIPTING_LUA (scripting);
    int              result;

    g_return_val_if_fail (self->L != NULL, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (code != NULL, FALSE);

    result = luaL_loadbuffer (self->L, code, strlen (code), name);
    if (result != 0)
    {
        const gchar *msg = lua_tostring (self->L, -1);
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     result == LUA_ERRSYNTAX ? LRG_SCRIPTING_ERROR_SYNTAX
                                             : LRG_SCRIPTING_ERROR_LOAD,
                     "Failed to load '%s': %s",
                     name, msg ? msg : "(unknown error)");
        lua_pop (self->L, 1);
        return FALSE;
    }

    /* Execute the loaded chunk */
    result = lua_pcall (self->L, 0, 0, 0);
    if (result != 0)
    {
        const gchar *msg = lua_tostring (self->L, -1);
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_RUNTIME,
                     "Error executing '%s': %s",
                     name, msg ? msg : "(unknown error)");
        lua_pop (self->L, 1);
        return FALSE;
    }

    return TRUE;
}

static gboolean
lrg_scripting_lua_call_function (LrgScripting  *scripting,
                                 const gchar   *func_name,
                                 GValue        *return_value,
                                 guint          n_args,
                                 const GValue  *args,
                                 GError       **error)
{
    LrgScriptingLua *self = LRG_SCRIPTING_LUA (scripting);
    int              result;
    int              nresults;
    guint            i;

    g_return_val_if_fail (self->L != NULL, FALSE);
    g_return_val_if_fail (func_name != NULL, FALSE);

    /* Get the function */
    lua_getglobal (self->L, func_name);
    if (!lua_isfunction (self->L, -1))
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_NOT_FOUND,
                     "Function '%s' not found",
                     func_name);
        lua_pop (self->L, 1);
        return FALSE;
    }

    /* Push arguments */
    for (i = 0; i < n_args; i++)
    {
        lrg_lua_push_gvalue (self->L, &args[i]);
    }

    /* Call the function */
    nresults = (return_value != NULL) ? 1 : 0;
    result = lua_pcall (self->L, (int)n_args, nresults, 0);

    if (result != 0)
    {
        const gchar *msg = lua_tostring (self->L, -1);
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_RUNTIME,
                     "Error calling '%s': %s",
                     func_name, msg ? msg : "(unknown error)");
        lua_pop (self->L, 1);
        return FALSE;
    }

    /* Get return value if requested */
    if (return_value != NULL && lua_gettop (self->L) > 0)
    {
        if (!lrg_lua_to_gvalue (self->L, -1, return_value))
        {
            g_set_error (error,
                         LRG_SCRIPTING_ERROR,
                         LRG_SCRIPTING_ERROR_TYPE,
                         "Cannot convert return value from '%s'",
                         func_name);
            lua_pop (self->L, 1);
            return FALSE;
        }
        lua_pop (self->L, 1);
    }

    return TRUE;
}

/*
 * C function wrapper for Lua.
 */
static int
c_function_wrapper (lua_State *L)
{
    RegisteredCFunction *reg;
    GValue              *args = NULL;
    GValue               return_value = G_VALUE_INIT;
    g_autoptr(GError)    error = NULL;
    guint                n_args;
    guint                i;
    gboolean             success;

    /* Get the registered function data from upvalue */
    reg = (RegisteredCFunction *)lua_touserdata (L, lua_upvalueindex (1));
    if (reg == NULL || reg->func == NULL)
    {
        return luaL_error (L, "Invalid C function registration");
    }

    /* Get arguments */
    n_args = (guint)lua_gettop (L);

    if (n_args > 0)
    {
        args = g_new0 (GValue, n_args);

        for (i = 0; i < n_args; i++)
        {
            if (!lrg_lua_to_gvalue (L, (gint)(i + 1), &args[i]))
            {
                guint j;

                /* Clean up already initialized values */
                for (j = 0; j < i; j++)
                {
                    g_value_unset (&args[j]);
                }
                g_free (args);
                return luaL_error (L, "Cannot convert argument %d", i + 1);
            }
        }
    }

    /* Call the C function */
    success = reg->func (LRG_SCRIPTING (reg->scripting),
                         n_args,
                         args,
                         &return_value,
                         reg->user_data,
                         &error);

    /* Clean up arguments */
    for (i = 0; i < n_args; i++)
    {
        g_value_unset (&args[i]);
    }
    g_free (args);

    if (!success)
    {
        const gchar *msg = error ? error->message : "Unknown error";
        return luaL_error (L, "%s", msg);
    }

    /* Push return value */
    if (G_IS_VALUE (&return_value))
    {
        lrg_lua_push_gvalue (L, &return_value);
        g_value_unset (&return_value);
        return 1;
    }

    return 0;
}

static gboolean
lrg_scripting_lua_register_function (LrgScripting           *scripting,
                                     const gchar            *name,
                                     LrgScriptingCFunction   func,
                                     gpointer                user_data,
                                     GError                **error)
{
    LrgScriptingLua     *self = LRG_SCRIPTING_LUA (scripting);
    RegisteredCFunction *reg;

    g_return_val_if_fail (self->L != NULL, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (func != NULL, FALSE);

    (void)error;  /* No errors expected */

    /* Create registration data as userdata */
    reg = (RegisteredCFunction *)lua_newuserdata (self->L, sizeof (RegisteredCFunction));
    reg->scripting = self;  /* Weak reference */
    reg->func = func;
    reg->user_data = user_data;

    /* Create closure with the userdata as upvalue */
    lua_pushcclosure (self->L, c_function_wrapper, 1);

    /* Set as global */
    lua_setglobal (self->L, name);

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Registered C function: %s", name);

    return TRUE;
}

static gboolean
lrg_scripting_lua_get_global (LrgScripting  *scripting,
                              const gchar   *name,
                              GValue        *value,
                              GError       **error)
{
    LrgScriptingLua *self = LRG_SCRIPTING_LUA (scripting);

    g_return_val_if_fail (self->L != NULL, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    lua_getglobal (self->L, name);

    if (lua_isnil (self->L, -1))
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_NOT_FOUND,
                     "Global '%s' not found",
                     name);
        lua_pop (self->L, 1);
        return FALSE;
    }

    if (!lrg_lua_to_gvalue (self->L, -1, value))
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_TYPE,
                     "Cannot convert global '%s' to GValue",
                     name);
        lua_pop (self->L, 1);
        return FALSE;
    }

    lua_pop (self->L, 1);

    return TRUE;
}

static gboolean
lrg_scripting_lua_set_global (LrgScripting  *scripting,
                              const gchar   *name,
                              const GValue  *value,
                              GError       **error)
{
    LrgScriptingLua *self = LRG_SCRIPTING_LUA (scripting);

    g_return_val_if_fail (self->L != NULL, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    (void)error;  /* No errors expected */

    lrg_lua_push_gvalue (self->L, value);
    lua_setglobal (self->L, name);

    return TRUE;
}

static void
lrg_scripting_lua_reset (LrgScripting *scripting)
{
    LrgScriptingLua *self = LRG_SCRIPTING_LUA (scripting);

    /* Close old state */
    if (self->L != NULL)
    {
        lua_close (self->L);
        self->L = NULL;
    }

    /* Clear update hooks */
    g_ptr_array_set_size (self->update_hooks, 0);

    /* Create new state */
    self->L = luaL_newstate ();
    if (self->L != NULL)
    {
        lua_atpanic (self->L, lua_panic);
        luaL_openlibs (self->L);

        /* Save default path */
        g_free (self->default_path);
        self->default_path = NULL;

        lua_getglobal (self->L, "package");
        if (lua_istable (self->L, -1))
        {
            lua_getfield (self->L, -1, "path");
            self->default_path = g_strdup (lua_tostring (self->L, -1));
            lua_pop (self->L, 1);
        }
        lua_pop (self->L, 1);

        /* Register API */
        lrg_lua_api_register_all (self->L, self);

        /* Update package path */
        update_package_path (self);
    }

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Script context reset");
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_scripting_lua_finalize (GObject *object)
{
    LrgScriptingLua *self = LRG_SCRIPTING_LUA (object);

    if (self->L != NULL)
    {
        lua_close (self->L);
        self->L = NULL;
    }

    g_clear_pointer (&self->update_hooks, g_ptr_array_unref);
    g_clear_pointer (&self->search_paths, g_ptr_array_unref);
    g_clear_pointer (&self->default_path, g_free);

    G_OBJECT_CLASS (lrg_scripting_lua_parent_class)->finalize (object);
}

static void
lrg_scripting_lua_class_init (LrgScriptingLuaClass *klass)
{
    GObjectClass      *object_class = G_OBJECT_CLASS (klass);
    LrgScriptingClass *scripting_class = LRG_SCRIPTING_CLASS (klass);

    object_class->finalize = lrg_scripting_lua_finalize;

    /* Override virtual methods */
    scripting_class->load_file = lrg_scripting_lua_load_file;
    scripting_class->load_string = lrg_scripting_lua_load_string;
    scripting_class->call_function = lrg_scripting_lua_call_function;
    scripting_class->register_function = lrg_scripting_lua_register_function;
    scripting_class->get_global = lrg_scripting_lua_get_global;
    scripting_class->set_global = lrg_scripting_lua_set_global;
    scripting_class->reset = lrg_scripting_lua_reset;
}

static void
lrg_scripting_lua_init (LrgScriptingLua *self)
{
    self->L = NULL;
    self->registry = NULL;
    self->engine = NULL;
    self->update_hooks = g_ptr_array_new_with_free_func (g_free);
    self->search_paths = g_ptr_array_new_with_free_func (g_free);
    self->default_path = NULL;

    /* Initialize the Lua state */
    lrg_scripting_reset (LRG_SCRIPTING (self));
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_scripting_lua_new:
 *
 * Creates a new LuaJIT scripting context.
 *
 * Returns: (transfer full): a new #LrgScriptingLua
 */
LrgScriptingLua *
lrg_scripting_lua_new (void)
{
    return g_object_new (LRG_TYPE_SCRIPTING_LUA, NULL);
}

/**
 * lrg_scripting_lua_set_registry:
 * @self: an #LrgScriptingLua
 * @registry: (nullable): the #LrgRegistry for type lookups
 *
 * Sets the registry used to expose types to Lua.
 */
void
lrg_scripting_lua_set_registry (LrgScriptingLua *self,
                                LrgRegistry     *registry)
{
    g_return_if_fail (LRG_IS_SCRIPTING_LUA (self));
    g_return_if_fail (registry == NULL || LRG_IS_REGISTRY (registry));

    self->registry = registry;  /* Weak reference */

    if (self->L != NULL)
    {
        lrg_lua_api_update_registry (self->L, registry);
    }
}

/**
 * lrg_scripting_lua_get_registry:
 * @self: an #LrgScriptingLua
 *
 * Gets the registry used for type lookups.
 *
 * Returns: (transfer none) (nullable): the #LrgRegistry
 */
LrgRegistry *
lrg_scripting_lua_get_registry (LrgScriptingLua *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPTING_LUA (self), NULL);

    return self->registry;
}

/**
 * lrg_scripting_lua_add_search_path:
 * @self: an #LrgScriptingLua
 * @path: (type filename): directory path to add
 *
 * Adds a directory to the Lua package search path.
 */
void
lrg_scripting_lua_add_search_path (LrgScriptingLua *self,
                                   const gchar     *path)
{
    g_return_if_fail (LRG_IS_SCRIPTING_LUA (self));
    g_return_if_fail (path != NULL);

    g_ptr_array_add (self->search_paths, g_strdup (path));
    update_package_path (self);
}

/**
 * lrg_scripting_lua_clear_search_paths:
 * @self: an #LrgScriptingLua
 *
 * Clears all custom search paths.
 */
void
lrg_scripting_lua_clear_search_paths (LrgScriptingLua *self)
{
    g_return_if_fail (LRG_IS_SCRIPTING_LUA (self));

    g_ptr_array_set_size (self->search_paths, 0);
    update_package_path (self);
}

/**
 * lrg_scripting_lua_register_update_hook:
 * @self: an #LrgScriptingLua
 * @func_name: name of the Lua function to call on update
 *
 * Registers a Lua function to be called each frame.
 */
void
lrg_scripting_lua_register_update_hook (LrgScriptingLua *self,
                                        const gchar     *func_name)
{
    g_return_if_fail (LRG_IS_SCRIPTING_LUA (self));
    g_return_if_fail (func_name != NULL);

    g_ptr_array_add (self->update_hooks, g_strdup (func_name));

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Registered update hook: %s", func_name);
}

/**
 * lrg_scripting_lua_unregister_update_hook:
 * @self: an #LrgScriptingLua
 * @func_name: name of the Lua function to unregister
 *
 * Unregisters a previously registered update hook.
 *
 * Returns: %TRUE if the hook was found and removed
 */
gboolean
lrg_scripting_lua_unregister_update_hook (LrgScriptingLua *self,
                                          const gchar     *func_name)
{
    guint i;

    g_return_val_if_fail (LRG_IS_SCRIPTING_LUA (self), FALSE);
    g_return_val_if_fail (func_name != NULL, FALSE);

    for (i = 0; i < self->update_hooks->len; i++)
    {
        const gchar *name = g_ptr_array_index (self->update_hooks, i);
        if (g_strcmp0 (name, func_name) == 0)
        {
            g_ptr_array_remove_index (self->update_hooks, i);
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * lrg_scripting_lua_clear_update_hooks:
 * @self: an #LrgScriptingLua
 *
 * Clears all registered update hooks.
 */
void
lrg_scripting_lua_clear_update_hooks (LrgScriptingLua *self)
{
    g_return_if_fail (LRG_IS_SCRIPTING_LUA (self));

    g_ptr_array_set_size (self->update_hooks, 0);
}

/**
 * lrg_scripting_lua_update:
 * @self: an #LrgScriptingLua
 * @delta: time since last frame in seconds
 *
 * Calls all registered update hooks.
 */
void
lrg_scripting_lua_update (LrgScriptingLua *self,
                          gfloat           delta)
{
    guint i;

    g_return_if_fail (LRG_IS_SCRIPTING_LUA (self));
    g_return_if_fail (self->L != NULL);

    for (i = 0; i < self->update_hooks->len; i++)
    {
        const gchar *func_name = g_ptr_array_index (self->update_hooks, i);
        int          result;

        /* Get the function */
        lua_getglobal (self->L, func_name);
        if (!lua_isfunction (self->L, -1))
        {
            lrg_warning (LRG_LOG_DOMAIN_SCRIPTING,
                         "Update hook '%s' is not a function",
                         func_name);
            lua_pop (self->L, 1);
            continue;
        }

        /* Push delta argument */
        lua_pushnumber (self->L, (lua_Number)delta);

        /* Call the function */
        result = lua_pcall (self->L, 1, 0, 0);
        if (result != 0)
        {
            const gchar *msg = lua_tostring (self->L, -1);
            lrg_warning (LRG_LOG_DOMAIN_SCRIPTING,
                         "Update hook '%s' error: %s",
                         func_name, msg ? msg : "(unknown)");
            lua_pop (self->L, 1);
        }
    }
}

/**
 * lrg_scripting_lua_set_engine:
 * @self: an #LrgScriptingLua
 * @engine: (nullable): the #LrgEngine to expose to scripts
 *
 * Sets the engine instance to expose to Lua.
 */
void
lrg_scripting_lua_set_engine (LrgScriptingLua *self,
                              LrgEngine       *engine)
{
    g_return_if_fail (LRG_IS_SCRIPTING_LUA (self));
    g_return_if_fail (engine == NULL || LRG_IS_ENGINE (engine));

    self->engine = engine;  /* Weak reference */

    if (self->L != NULL)
    {
        lrg_lua_api_update_engine (self->L, engine);
    }
}

/**
 * lrg_scripting_lua_get_engine:
 * @self: an #LrgScriptingLua
 *
 * Gets the engine instance exposed to Lua.
 *
 * Returns: (transfer none) (nullable): the #LrgEngine
 */
LrgEngine *
lrg_scripting_lua_get_engine (LrgScriptingLua *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPTING_LUA (self), NULL);

    return self->engine;
}

/**
 * lrg_scripting_lua_get_state:
 * @self: an #LrgScriptingLua
 *
 * Gets the internal Lua state. This is primarily for internal use
 * by the bridge and API modules.
 *
 * Returns: (transfer none): the lua_State
 */
lua_State *
lrg_scripting_lua_get_state (LrgScriptingLua *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPTING_LUA (self), NULL);

    return self->L;
}
