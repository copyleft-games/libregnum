/* lrg-lua-bridge.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * GObject <-> Lua type conversion bridge implementation.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SCRIPTING

#include "lrg-lua-bridge.h"
#include "lrg-scriptable.h"
#include "../lrg-log.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

/* Registry keys for our metatables and tables */
#define LRG_LUA_GOBJECT_METATABLE   "LrgGObjectMT"
#define LRG_LUA_WEAK_TABLE          "LrgWeakTable"

/* ==========================================================================
 * Forward Declarations
 * ========================================================================== */

static int gobject_index        (lua_State *L);
static int gobject_newindex     (lua_State *L);
static int gobject_gc           (lua_State *L);
static int gobject_tostring     (lua_State *L);
static int gobject_connect      (lua_State *L);
static int gobject_script_call  (lua_State *L);

/* ==========================================================================
 * GValue <-> Lua Conversion
 * ========================================================================== */

/**
 * lrg_lua_push_gvalue:
 *
 * Pushes a GValue onto the Lua stack.
 */
gint
lrg_lua_push_gvalue (lua_State    *L,
                     const GValue *value)
{
    GType type;

    if (value == NULL || !G_IS_VALUE (value))
    {
        lua_pushnil (L);
        return 1;
    }

    type = G_VALUE_TYPE (value);

    switch (G_TYPE_FUNDAMENTAL (type))
    {
    case G_TYPE_NONE:
    case G_TYPE_INVALID:
        lua_pushnil (L);
        return 1;

    case G_TYPE_BOOLEAN:
        lua_pushboolean (L, g_value_get_boolean (value));
        return 1;

    case G_TYPE_CHAR:
        lua_pushinteger (L, g_value_get_schar (value));
        return 1;

    case G_TYPE_UCHAR:
        lua_pushinteger (L, g_value_get_uchar (value));
        return 1;

    case G_TYPE_INT:
        lua_pushinteger (L, g_value_get_int (value));
        return 1;

    case G_TYPE_UINT:
        lua_pushinteger (L, (lua_Integer)g_value_get_uint (value));
        return 1;

    case G_TYPE_LONG:
        lua_pushinteger (L, g_value_get_long (value));
        return 1;

    case G_TYPE_ULONG:
        lua_pushinteger (L, (lua_Integer)g_value_get_ulong (value));
        return 1;

    case G_TYPE_INT64:
        lua_pushinteger (L, (lua_Integer)g_value_get_int64 (value));
        return 1;

    case G_TYPE_UINT64:
        lua_pushinteger (L, (lua_Integer)g_value_get_uint64 (value));
        return 1;

    case G_TYPE_FLOAT:
        lua_pushnumber (L, (lua_Number)g_value_get_float (value));
        return 1;

    case G_TYPE_DOUBLE:
        lua_pushnumber (L, g_value_get_double (value));
        return 1;

    case G_TYPE_STRING:
        {
            const gchar *str = g_value_get_string (value);
            if (str != NULL)
                lua_pushstring (L, str);
            else
                lua_pushnil (L);
            return 1;
        }

    case G_TYPE_ENUM:
        lua_pushinteger (L, g_value_get_enum (value));
        return 1;

    case G_TYPE_FLAGS:
        lua_pushinteger (L, (lua_Integer)g_value_get_flags (value));
        return 1;

    case G_TYPE_OBJECT:
        {
            GObject *obj = g_value_get_object (value);
            lrg_lua_push_gobject (L, obj);
            return 1;
        }

    default:
        lrg_debug (LRG_LOG_DOMAIN_SCRIPTING,
                   "Unsupported GValue type: %s",
                   g_type_name (type));
        lua_pushnil (L);
        return 0;
    }
}

/**
 * lrg_lua_to_gvalue:
 *
 * Reads a value from the Lua stack and converts it to a GValue.
 */
gboolean
lrg_lua_to_gvalue (lua_State *L,
                   gint       index,
                   GValue    *value)
{
    int ltype;

    g_return_val_if_fail (L != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    ltype = lua_type (L, index);

    switch (ltype)
    {
    case LUA_TNIL:
        g_value_init (value, G_TYPE_NONE);
        return TRUE;

    case LUA_TBOOLEAN:
        g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, lua_toboolean (L, index));
        return TRUE;

    case LUA_TNUMBER:
        {
            lua_Number num = lua_tonumber (L, index);

            /* Check if it's an integer */
            if (num == (lua_Number)(lua_Integer)num)
            {
                g_value_init (value, G_TYPE_INT64);
                g_value_set_int64 (value, (gint64)lua_tointeger (L, index));
            }
            else
            {
                g_value_init (value, G_TYPE_DOUBLE);
                g_value_set_double (value, (gdouble)num);
            }
            return TRUE;
        }

    case LUA_TSTRING:
        g_value_init (value, G_TYPE_STRING);
        g_value_set_string (value, lua_tostring (L, index));
        return TRUE;

    case LUA_TUSERDATA:
        {
            GObject *obj = lrg_lua_to_gobject (L, index);
            if (obj != NULL)
            {
                g_value_init (value, G_TYPE_OBJECT);
                g_value_set_object (value, obj);
                return TRUE;
            }
            return FALSE;
        }

    default:
        lrg_debug (LRG_LOG_DOMAIN_SCRIPTING,
                   "Cannot convert Lua type %s to GValue",
                   lua_typename (L, ltype));
        return FALSE;
    }
}

/**
 * lrg_lua_to_gvalue_with_type:
 *
 * Reads a value from the Lua stack and converts it to a GValue
 * of the specified type.
 */
gboolean
lrg_lua_to_gvalue_with_type (lua_State *L,
                             gint       index,
                             GType      type,
                             GValue    *value)
{
    int lua_type_id;

    g_return_val_if_fail (L != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    lua_type_id = lua_type (L, index);

    /* Handle nil specially */
    if (lua_type_id == LUA_TNIL)
    {
        g_value_init (value, type);
        return TRUE;
    }

    g_value_init (value, type);

    switch (G_TYPE_FUNDAMENTAL (type))
    {
    case G_TYPE_BOOLEAN:
        if (lua_type_id == LUA_TBOOLEAN)
        {
            g_value_set_boolean (value, lua_toboolean (L, index));
            return TRUE;
        }
        break;

    case G_TYPE_INT:
        if (lua_type_id == LUA_TNUMBER)
        {
            g_value_set_int (value, (gint)lua_tointeger (L, index));
            return TRUE;
        }
        break;

    case G_TYPE_UINT:
        if (lua_type_id == LUA_TNUMBER)
        {
            g_value_set_uint (value, (guint)lua_tointeger (L, index));
            return TRUE;
        }
        break;

    case G_TYPE_LONG:
        if (lua_type_id == LUA_TNUMBER)
        {
            g_value_set_long (value, (glong)lua_tointeger (L, index));
            return TRUE;
        }
        break;

    case G_TYPE_ULONG:
        if (lua_type_id == LUA_TNUMBER)
        {
            g_value_set_ulong (value, (gulong)lua_tointeger (L, index));
            return TRUE;
        }
        break;

    case G_TYPE_INT64:
        if (lua_type_id == LUA_TNUMBER)
        {
            g_value_set_int64 (value, (gint64)lua_tointeger (L, index));
            return TRUE;
        }
        break;

    case G_TYPE_UINT64:
        if (lua_type_id == LUA_TNUMBER)
        {
            g_value_set_uint64 (value, (guint64)lua_tointeger (L, index));
            return TRUE;
        }
        break;

    case G_TYPE_FLOAT:
        if (lua_type_id == LUA_TNUMBER)
        {
            g_value_set_float (value, (gfloat)lua_tonumber (L, index));
            return TRUE;
        }
        break;

    case G_TYPE_DOUBLE:
        if (lua_type_id == LUA_TNUMBER)
        {
            g_value_set_double (value, (gdouble)lua_tonumber (L, index));
            return TRUE;
        }
        break;

    case G_TYPE_STRING:
        if (lua_type_id == LUA_TSTRING)
        {
            g_value_set_string (value, lua_tostring (L, index));
            return TRUE;
        }
        break;

    case G_TYPE_ENUM:
        if (lua_type_id == LUA_TNUMBER)
        {
            g_value_set_enum (value, (gint)lua_tointeger (L, index));
            return TRUE;
        }
        break;

    case G_TYPE_FLAGS:
        if (lua_type_id == LUA_TNUMBER)
        {
            g_value_set_flags (value, (guint)lua_tointeger (L, index));
            return TRUE;
        }
        break;

    case G_TYPE_OBJECT:
        if (lua_type_id == LUA_TUSERDATA)
        {
            GObject *obj = lrg_lua_to_gobject (L, index);
            if (obj != NULL && g_type_is_a (G_OBJECT_TYPE (obj), type))
            {
                g_value_set_object (value, obj);
                return TRUE;
            }
        }
        break;

    default:
        break;
    }

    g_value_unset (value);
    return FALSE;
}

/* ==========================================================================
 * GObject Handling
 * ========================================================================== */

/**
 * lrg_lua_push_gobject:
 *
 * Pushes a GObject onto the Lua stack.
 */
void
lrg_lua_push_gobject (lua_State *L,
                      GObject   *object)
{
    GObject **userdata;

    if (object == NULL)
    {
        lua_pushnil (L);
        return;
    }

    /* Check if we already have a userdata for this object */
    lua_getfield (L, LUA_REGISTRYINDEX, LRG_LUA_WEAK_TABLE);
    if (lua_istable (L, -1))
    {
        lua_pushlightuserdata (L, object);
        lua_rawget (L, -2);

        if (!lua_isnil (L, -1))
        {
            /* Found existing userdata, return it */
            lua_remove (L, -2);  /* Remove weak table */
            return;
        }
        lua_pop (L, 1);  /* Pop nil */
    }
    lua_pop (L, 1);  /* Pop weak table (or nil if not found) */

    /* Create new userdata */
    userdata = (GObject **)lua_newuserdata (L, sizeof (GObject *));
    *userdata = g_object_ref (object);

    /* Set the metatable */
    luaL_getmetatable (L, LRG_LUA_GOBJECT_METATABLE);
    lua_setmetatable (L, -2);

    /* Store in weak table */
    lua_getfield (L, LUA_REGISTRYINDEX, LRG_LUA_WEAK_TABLE);
    if (lua_istable (L, -1))
    {
        lua_pushlightuserdata (L, object);
        lua_pushvalue (L, -3);  /* Push the userdata */
        lua_rawset (L, -3);
    }
    lua_pop (L, 1);  /* Pop weak table */
}

/**
 * lrg_lua_to_gobject:
 *
 * Extracts a GObject from a Lua userdata.
 */
GObject *
lrg_lua_to_gobject (lua_State *L,
                    gint       index)
{
    GObject **userdata;

    if (!lua_isuserdata (L, index))
        return NULL;

    /* Check metatable */
    if (!lua_getmetatable (L, index))
        return NULL;

    luaL_getmetatable (L, LRG_LUA_GOBJECT_METATABLE);
    if (!lua_rawequal (L, -1, -2))
    {
        lua_pop (L, 2);
        return NULL;
    }
    lua_pop (L, 2);

    userdata = (GObject **)lua_touserdata (L, index);
    if (userdata == NULL)
        return NULL;

    return *userdata;
}

/**
 * lrg_lua_is_gobject:
 *
 * Checks if a Lua value is a GObject userdata.
 */
gboolean
lrg_lua_is_gobject (lua_State *L,
                    gint       index)
{
    gboolean result;

    if (!lua_isuserdata (L, index))
        return FALSE;

    if (!lua_getmetatable (L, index))
        return FALSE;

    luaL_getmetatable (L, LRG_LUA_GOBJECT_METATABLE);
    result = lua_rawequal (L, -1, -2);
    lua_pop (L, 2);

    return result;
}

/* ==========================================================================
 * GObject Metatable Methods
 * ========================================================================== */

/*
 * __index metamethod for GObject userdata.
 * Handles property access and method lookup.
 * Supports LrgScriptable custom methods and access control.
 */
static int
gobject_index (lua_State *L)
{
    GObject      *object;
    const gchar  *key;
    GObjectClass *klass;
    GParamSpec   *pspec;

    object = lrg_lua_to_gobject (L, 1);
    if (object == NULL)
    {
        lua_pushnil (L);
        return 1;
    }

    key = luaL_checkstring (L, 2);

    /* Check for special methods first */
    if (g_strcmp0 (key, "connect") == 0)
    {
        lua_pushcfunction (L, gobject_connect);
        return 1;
    }

    /*
     * Check for LrgScriptable custom methods.
     * These take priority over properties with the same name.
     */
    if (LRG_IS_SCRIPTABLE (object))
    {
        const LrgScriptMethod *method;

        method = lrg_scriptable_find_method (LRG_SCRIPTABLE (object), key);
        if (method != NULL)
        {
            /*
             * Return a closure that captures the method pointer.
             * The closure will invoke the method when called.
             */
            lua_pushlightuserdata (L, (gpointer)method);
            lua_pushvalue (L, 1);  /* Push the object */
            lua_pushcclosure (L, gobject_script_call, 2);
            return 1;
        }
    }

    /* Look for a property */
    klass = G_OBJECT_GET_CLASS (object);
    pspec = g_object_class_find_property (klass, key);

    if (pspec != NULL)
    {
        GValue               value = G_VALUE_INIT;
        LrgScriptAccessFlags access_flags;

        /*
         * Check access control if object implements LrgScriptable.
         * Default behavior allows reading if G_PARAM_READABLE is set.
         */
        if (LRG_IS_SCRIPTABLE (object))
        {
            access_flags = lrg_scriptable_get_property_access (
                LRG_SCRIPTABLE (object), key);

            if (!(access_flags & LRG_SCRIPT_ACCESS_READ))
            {
                return luaL_error (L, "Property '%s' is not script-readable",
                                   key);
            }
        }

        g_value_init (&value, pspec->value_type);
        g_object_get_property (object, key, &value);

        lrg_lua_push_gvalue (L, &value);
        g_value_unset (&value);

        return 1;
    }

    /* Property not found */
    lua_pushnil (L);
    return 1;
}

/*
 * __newindex metamethod for GObject userdata.
 * Handles property assignment.
 * Supports LrgScriptable access control.
 */
static int
gobject_newindex (lua_State *L)
{
    GObject      *object;
    const gchar  *key;
    GObjectClass *klass;
    GParamSpec   *pspec;
    GValue        value = G_VALUE_INIT;

    object = lrg_lua_to_gobject (L, 1);
    if (object == NULL)
    {
        return luaL_error (L, "Invalid GObject");
    }

    key = luaL_checkstring (L, 2);

    /* Look for a writable property */
    klass = G_OBJECT_GET_CLASS (object);
    pspec = g_object_class_find_property (klass, key);

    if (pspec == NULL)
    {
        return luaL_error (L, "Property '%s' not found on %s",
                           key, G_OBJECT_TYPE_NAME (object));
    }

    /*
     * Check access control if object implements LrgScriptable.
     * This takes precedence over G_PARAM_WRITABLE check.
     */
    if (LRG_IS_SCRIPTABLE (object))
    {
        LrgScriptAccessFlags access_flags;

        access_flags = lrg_scriptable_get_property_access (
            LRG_SCRIPTABLE (object), key);

        if (!(access_flags & LRG_SCRIPT_ACCESS_WRITE))
        {
            return luaL_error (L, "Property '%s' is not script-writable", key);
        }
    }
    else if (!(pspec->flags & G_PARAM_WRITABLE))
    {
        return luaL_error (L, "Property '%s' is read-only", key);
    }

    /* Convert Lua value to GValue */
    if (!lrg_lua_to_gvalue_with_type (L, 3, pspec->value_type, &value))
    {
        return luaL_error (L, "Cannot convert value for property '%s'", key);
    }

    g_object_set_property (object, key, &value);
    g_value_unset (&value);

    return 0;
}

/*
 * __gc metamethod for GObject userdata.
 * Releases the GObject reference.
 */
static int
gobject_gc (lua_State *L)
{
    GObject **userdata;

    userdata = (GObject **)lua_touserdata (L, 1);
    if (userdata != NULL && *userdata != NULL)
    {
        g_object_unref (*userdata);
        *userdata = NULL;
    }

    return 0;
}

/*
 * __tostring metamethod for GObject userdata.
 */
static int
gobject_tostring (lua_State *L)
{
    GObject *object;

    object = lrg_lua_to_gobject (L, 1);
    if (object == NULL)
    {
        lua_pushstring (L, "(invalid GObject)");
    }
    else
    {
        lua_pushfstring (L, "%s<%p>",
                         G_OBJECT_TYPE_NAME (object),
                         (void *)object);
    }

    return 1;
}

/*
 * connect method for GObject userdata.
 * Usage: object:connect("signal-name", callback)
 */
static int
gobject_connect (lua_State *L)
{
    GObject     *object;
    const gchar *signal_name;
    gint         callback_ref;
    gulong       handler_id;

    object = lrg_lua_to_gobject (L, 1);
    if (object == NULL)
    {
        return luaL_error (L, "Invalid GObject");
    }

    signal_name = luaL_checkstring (L, 2);
    luaL_checktype (L, 3, LUA_TFUNCTION);

    /* Create reference to the callback function */
    lua_pushvalue (L, 3);
    callback_ref = luaL_ref (L, LUA_REGISTRYINDEX);

    /* Connect the signal */
    handler_id = lrg_lua_connect_signal (L, object, signal_name, callback_ref);

    if (handler_id == 0)
    {
        luaL_unref (L, LUA_REGISTRYINDEX, callback_ref);
        return luaL_error (L, "Failed to connect to signal '%s'", signal_name);
    }

    lua_pushinteger (L, (lua_Integer)handler_id);
    return 1;
}

/*
 * Closure for invoking LrgScriptable methods.
 * Upvalue 1: lightuserdata pointer to LrgScriptMethod
 * Upvalue 2: the GObject userdata
 *
 * Usage: object:method_name(arg1, arg2, ...)
 */
static int
gobject_script_call (lua_State *L)
{
    const LrgScriptMethod *method;
    GObject               *object;
    GValue                *args;
    GValue                 return_value = G_VALUE_INIT;
    g_autoptr(GError)      error = NULL;
    gint                   n_args;
    gint                   arg_offset = 0;
    gint                   i;
    gboolean               success;

    /* Get the method from upvalue 1 */
    method = (const LrgScriptMethod *)lua_touserdata (L, lua_upvalueindex (1));
    if (method == NULL)
    {
        return luaL_error (L, "Invalid script method");
    }

    /* Get the object from upvalue 2 */
    object = lrg_lua_to_gobject (L, lua_upvalueindex (2));
    if (object == NULL)
    {
        return luaL_error (L, "Invalid GObject for method call");
    }

    /*
     * Get the number of arguments.
     * When using object:method() syntax, Lua passes self as the first argument.
     * Since we already have the object from the upvalue, we skip the first
     * argument if it's the same object (the implicit self from : syntax).
     */
    n_args = lua_gettop (L);
    if (n_args > 0 && lua_isuserdata (L, 1))
    {
        GObject *first_arg = lrg_lua_to_gobject (L, 1);
        if (first_arg == object)
        {
            /* Skip the implicit self argument */
            arg_offset = 1;
            n_args -= 1;
        }
    }

    /* Check argument count if method specifies exact count */
    if (method->n_params >= 0 && n_args != method->n_params)
    {
        return luaL_error (L, "Method '%s' expects %d arguments, got %d",
                           method->name, method->n_params, n_args);
    }

    /* Convert Lua arguments to GValues */
    if (n_args > 0)
    {
        args = g_new0 (GValue, n_args);

        for (i = 0; i < n_args; i++)
        {
            if (!lrg_lua_to_gvalue (L, i + 1 + arg_offset, &args[i]))
            {
                /* Clean up already converted values */
                gint j;
                for (j = 0; j < i; j++)
                {
                    g_value_unset (&args[j]);
                }
                g_free (args);
                return luaL_error (L, "Cannot convert argument %d for method '%s'",
                                   i + 1, method->name);
            }
        }
    }
    else
    {
        args = NULL;
    }

    /* Invoke the method */
    success = method->func (LRG_SCRIPTABLE (object),
                            (guint)n_args,
                            args,
                            &return_value,
                            &error);

    /* Clean up arguments */
    if (args != NULL)
    {
        for (i = 0; i < n_args; i++)
        {
            g_value_unset (&args[i]);
        }
        g_free (args);
    }

    if (!success)
    {
        const gchar *msg;

        msg = (error != NULL) ? error->message : "Unknown error";
        return luaL_error (L, "Method '%s' failed: %s", method->name, msg);
    }

    /* Push return value if present */
    if (G_IS_VALUE (&return_value))
    {
        lrg_lua_push_gvalue (L, &return_value);
        g_value_unset (&return_value);
        return 1;
    }

    return 0;
}

/* ==========================================================================
 * Metatable Registration
 * ========================================================================== */

/**
 * lrg_lua_register_gobject_metatable:
 *
 * Registers the GObject metatable.
 */
void
lrg_lua_register_gobject_metatable (lua_State *L)
{
    luaL_newmetatable (L, LRG_LUA_GOBJECT_METATABLE);

    lua_pushcfunction (L, gobject_index);
    lua_setfield (L, -2, "__index");

    lua_pushcfunction (L, gobject_newindex);
    lua_setfield (L, -2, "__newindex");

    lua_pushcfunction (L, gobject_gc);
    lua_setfield (L, -2, "__gc");

    lua_pushcfunction (L, gobject_tostring);
    lua_setfield (L, -2, "__tostring");

    lua_pop (L, 1);  /* Pop metatable */
}

/**
 * lrg_lua_register_weak_table:
 *
 * Registers the weak table for GObject tracking.
 */
void
lrg_lua_register_weak_table (lua_State *L)
{
    lua_newtable (L);

    /* Create metatable with weak values */
    lua_newtable (L);
    lua_pushstring (L, "v");
    lua_setfield (L, -2, "__mode");
    lua_setmetatable (L, -2);

    lua_setfield (L, LUA_REGISTRYINDEX, LRG_LUA_WEAK_TABLE);
}

/* ==========================================================================
 * Signal Connection Support
 * ========================================================================== */

/*
 * SignalCallbackData:
 *
 * Data passed to the GObject signal callback.
 */
typedef struct
{
    lua_State *L;
    gint       callback_ref;
} SignalCallbackData;

/*
 * Generic signal callback that invokes the Lua function.
 */
static void
signal_callback (GObject *object,
                 gpointer user_data)
{
    SignalCallbackData *data = user_data;
    lua_State          *L = data->L;

    /* Get the callback function from the registry */
    lua_rawgeti (L, LUA_REGISTRYINDEX, data->callback_ref);

    if (!lua_isfunction (L, -1))
    {
        lua_pop (L, 1);
        return;
    }

    /* Push the object as first argument */
    lrg_lua_push_gobject (L, object);

    /* Call the function */
    if (lua_pcall (L, 1, 0, 0) != 0)
    {
        lrg_warning (LRG_LOG_DOMAIN_SCRIPTING,
                     "Signal callback error: %s",
                     lua_tostring (L, -1));
        lua_pop (L, 1);
    }
}

/*
 * Destroy notify for signal callback data.
 * GClosureNotify signature: void (*)(gpointer data, GClosure *closure)
 */
static void
signal_callback_destroy (gpointer  user_data,
                         GClosure *closure)
{
    SignalCallbackData *data = user_data;

    (void)closure;  /* Unused */

    if (data != NULL)
    {
        luaL_unref (data->L, LUA_REGISTRYINDEX, data->callback_ref);
        g_free (data);
    }
}

/**
 * lrg_lua_connect_signal:
 *
 * Connects a Lua callback to a GObject signal.
 */
gulong
lrg_lua_connect_signal (lua_State   *L,
                        GObject     *object,
                        const gchar *signal_name,
                        gint         callback_ref)
{
    SignalCallbackData *data;
    gulong              handler_id;
    guint               signal_id;

    g_return_val_if_fail (L != NULL, 0);
    g_return_val_if_fail (G_IS_OBJECT (object), 0);
    g_return_val_if_fail (signal_name != NULL, 0);

    /* Check that the signal exists */
    signal_id = g_signal_lookup (signal_name, G_OBJECT_TYPE (object));
    if (signal_id == 0)
    {
        lrg_warning (LRG_LOG_DOMAIN_SCRIPTING,
                     "Signal '%s' not found on %s",
                     signal_name, G_OBJECT_TYPE_NAME (object));
        return 0;
    }

    /* Create callback data */
    data = g_new0 (SignalCallbackData, 1);
    data->L = L;
    data->callback_ref = callback_ref;

    /* Connect with destroy notify */
    handler_id = g_signal_connect_data (object,
                                        signal_name,
                                        G_CALLBACK (signal_callback),
                                        data,
                                        signal_callback_destroy,
                                        0);

    return handler_id;
}

/**
 * lrg_lua_disconnect_signal:
 *
 * Disconnects a Lua callback from a GObject signal.
 */
void
lrg_lua_disconnect_signal (lua_State *L,
                           GObject   *object,
                           gulong     handler_id)
{
    (void)L;  /* Unused */

    g_return_if_fail (G_IS_OBJECT (object));
    g_return_if_fail (handler_id > 0);

    g_signal_handler_disconnect (object, handler_id);
}
