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
#define LRG_SCRIPTING_PRIVATE

#include "lrg-scripting-lua.h"
#include "lrg-script-debugger.h"
#include "lrg-script-reloader.h"
#include "lrg-scripting-lua-private.h"
#include "lrg-lua-bridge.h"
#include "lrg-lua-api.h"
#include "../lrg-log.h"
#include "../core/lrg-engine.h"
#include "../core/lrg-registry.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <luajit.h>

struct _LrgScriptingLua
{
    LrgScripting  parent_instance;

    lua_State    *L;                /* Main Lua state */
    LrgRegistry  *registry;         /* Type registry (weak ref) */
    LrgEngine    *engine;           /* Engine (weak ref) */
    GPtrArray    *update_hooks;     /* Array of function names (gchar *) */
    GPtrArray    *search_paths;     /* Array of search paths (gchar *) */
    gchar        *default_path;     /* Default Lua package path */
    guint         generation;       /* states created so far (reset bumps it) */
    gboolean      jit_disabled;     /* lrg_scripting_lua_set_jit_enabled() */
    LrgScriptDebugger *debugger;    /* not ref'd; the attached debugger */
    gint          c_depth;          /* host pcall nesting */
    gint          in_hook;          /* line-hook nesting */
    gboolean      hook_dirty;       /* re-arm when the hook returns */
};

static void lua_debuggable_init (LrgScriptDebuggableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgScriptingLua, lrg_scripting_lua, LRG_TYPE_SCRIPTING,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_SCRIPT_DEBUGGABLE,
                                                lua_debuggable_init))

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
 * Debugger and hot reload
 * ========================================================================== */

#define LRG_LUA_CHILD_CAP (500)

typedef enum
{
    LUA_H_LOCALS = 1,
    LUA_H_UPVALUES,
    LUA_H_GLOBALS,
    LUA_H_TABLE,
    LUA_H_OBJECT
} LuaHandleKind;

typedef struct
{
    LuaHandleKind  kind;
    lua_State     *L;
    int            level;
    int            ref;
    GObject       *object;
} LuaHandle;

static char lrg_lua_self_key;

static void lua_line_hook (lua_State *L, lua_Debug *ar);
static void lua_arm (LrgScriptingLua *self);

static LrgScriptingLua *
lua_from_state (lua_State *L)
{
    LrgScriptingLua *self;

    lua_pushlightuserdata (L, &lrg_lua_self_key);
    lua_rawget (L, LUA_REGISTRYINDEX);
    self = (LrgScriptingLua *) lua_touserdata (L, -1);
    lua_pop (L, 1);
    return self;
}

static void
lua_store_self (LrgScriptingLua *self)
{
    if (self->L == NULL)
        return;
    lua_pushlightuserdata (self->L, &lrg_lua_self_key);
    lua_pushlightuserdata (self->L, self);
    lua_rawset (self->L, LUA_REGISTRYINDEX);
}

static void
lua_arm (LrgScriptingLua *self)
{
    if (self->L == NULL)
        return;
    if (self->in_hook > 0)
    {
        self->hook_dirty = TRUE;
        return;
    }
    if (self->debugger != NULL &&
        lrg_script_debugger_wants_line_events (self->debugger))
    {
        /* Line hooks abort traces; keep the interpreter interpreted so
         * every source line is reported. */
        if (!self->jit_disabled)
            luaJIT_setmode (self->L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
        lua_sethook (self->L, lua_line_hook, LUA_MASKLINE, 0);
    }
    else
    {
        lua_sethook (self->L, NULL, 0, 0);
        if (!self->jit_disabled)
            luaJIT_setmode (self->L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_ON);
    }
}

static guint
lua_script_depth (lua_State *L)
{
    lua_Debug ar;
    int level = 0;
    guint depth = 0;

    while (lua_getstack (L, level, &ar) != 0)
    {
        lua_getinfo (L, "S", &ar);
        if (ar.what == NULL || ar.what[0] != 'C')
            depth++;
        level++;
    }
    if (depth == 0)
        depth = 1;
    return depth;
}

static void
lua_line_hook (lua_State *L,
               lua_Debug *ar)
{
    LrgScriptingLua *self;

    if (ar->event != LUA_HOOKLINE)
        return;
    self = lua_from_state (L);
    if (self == NULL || self->debugger == NULL)
        return;
    if (!lrg_script_debugger_line_may_break (self->debugger, ar->currentline))
        return;

    self->in_hook++;
    lua_getinfo (L, "Sl", ar);
    lrg_script_debugger_report_line (self->debugger,
                                     LRG_SCRIPT_DEBUGGABLE (self),
                                     ar->source,
                                     ar->currentline,
                                     lua_script_depth (L));
    self->in_hook--;
    if (self->in_hook == 0 && self->hook_dirty)
    {
        self->hook_dirty = FALSE;
        lua_arm (self);
    }
}

static int
lua_msgh (lua_State *L)
{
    LrgScriptingLua *self = lua_from_state (L);
    const char *msg = lua_tostring (L, -1);
    lua_Debug ar;
    const char *file = NULL;
    int line = 0;

    if (lua_getstack (L, 1, &ar) != 0 && lua_getinfo (L, "Sl", &ar) != 0)
    {
        file = ar.source;
        line = ar.currentline;
    }
    if (self != NULL && self->debugger != NULL)
        lrg_script_debugger_report_exception (self->debugger,
                                              LRG_SCRIPT_DEBUGGABLE (self),
                                              file,
                                              line > 0 ? line : 0,
                                              msg != NULL ? msg : "",
                                              TRUE);
    if (self != NULL)
        lrg_scripting_priv_set_error_location (LRG_SCRIPTING (self),
                                               file != NULL ? file : "",
                                               line);
    return 1;
}

static int
lua_pcall_host (LrgScriptingLua *self,
                int              nargs,
                int              nresults)
{
    int msgh;
    int rc;

    lua_pushcfunction (self->L, lua_msgh);
    lua_insert (self->L, -(nargs + 2));
    msgh = lua_gettop (self->L) - nargs - 1;

    self->c_depth++;
    lrg_scripting_priv_push_current (LRG_SCRIPTING (self));
    rc = lua_pcall (self->L, nargs, nresults, msgh);
    lrg_scripting_priv_pop_current (LRG_SCRIPTING (self));
    self->c_depth--;
    lua_remove (self->L, msgh);

    if (self->c_depth == 0 && self->debugger != NULL)
        lrg_script_debugger_note_host_return (self->debugger);
    return rc;
}

static void
lua_set_error_from_msg (LrgScripting *scripting,
                        const gchar  *fallback,
                        const gchar  *msg)
{
    const gchar *cursor;

    if (msg == NULL)
    {
        lrg_scripting_priv_set_error_location (scripting, fallback, 0);
        return;
    }
    cursor = msg;
    while ((cursor = strchr (cursor, ':')) != NULL)
    {
        const gchar *digit = cursor + 1;
        const gchar *start;
        gint n = 0;

        if (!g_ascii_isdigit (*digit))
        {
            cursor++;
            continue;
        }
        while (g_ascii_isdigit (*digit))
        {
            n = n * 10 + (*digit - '0');
            digit++;
        }
        if (*digit != ':' || n <= 0)
        {
            cursor++;
            continue;
        }
        start = cursor;
        while (start > msg &&
               start[-1] != ' ' && start[-1] != '\'' && start[-1] != '"')
            start--;
        {
            g_autofree gchar *file = g_strndup (start, (gsize) (cursor - start));

            lrg_scripting_priv_set_error_location (scripting, file, n);
            return;
        }
    }
    lrg_scripting_priv_set_error_location (scripting, fallback, 0);
}

static int
lua_snapshot_globals (lua_State *L)
{
    int snap;

    lua_newtable (L);
    snap = lua_gettop (L);
    lua_getglobal (L, "_G");
    lua_pushnil (L);
    while (lua_next (L, -2) != 0)
    {
        lua_pushvalue (L, -2);
        lua_pushvalue (L, -2);
        lua_rawset (L, snap);
        lua_pop (L, 1);
    }
    lua_pop (L, 1);
    return luaL_ref (L, LUA_REGISTRYINDEX);
}

static void
lua_restore_globals (lua_State *L,
                     int        ref)
{
    int snap;
    int globals;
    int dels;
    int count;
    int i;

    lua_rawgeti (L, LUA_REGISTRYINDEX, ref);
    snap = lua_gettop (L);
    lua_getglobal (L, "_G");
    globals = lua_gettop (L);
    lua_newtable (L);
    dels = lua_gettop (L);

    lua_pushnil (L);
    while (lua_next (L, globals) != 0)
    {
        lua_pop (L, 1);
        lua_pushvalue (L, -1);
        lua_rawget (L, snap);
        if (lua_isnil (L, -1))
        {
            lua_pop (L, 1);
            lua_pushvalue (L, -1);
            lua_rawseti (L, dels, (int) lua_objlen (L, dels) + 1);
        }
        else
            lua_pop (L, 1);
    }

    count = (int) lua_objlen (L, dels);
    for (i = 1; i <= count; i++)
    {
        lua_rawgeti (L, dels, i);
        lua_pushnil (L);
        lua_rawset (L, globals);
    }

    lua_pushnil (L);
    while (lua_next (L, snap) != 0)
    {
        lua_pushvalue (L, -2);
        lua_pushvalue (L, -2);
        lua_rawset (L, globals);
        lua_pop (L, 1);
    }
    lua_settop (L, snap - 1);
}

static gboolean
lua_reload_file (LrgScripting  *scripting,
                 const gchar   *path,
                 GError       **error)
{
    LrgScriptingLua *self = LRG_SCRIPTING_LUA (scripting);
    int state_ref = LUA_NOREF;
    int snap = LUA_NOREF;
    int rc;
    const char *msg;

    g_return_val_if_fail (self->L != NULL, FALSE);

    lua_getglobal (self->L, LRG_SCRIPT_HOOK_BEFORE_RELOAD);
    if (lua_isfunction (self->L, -1))
    {
        if (lua_pcall_host (self, 0, 1) == 0)
            state_ref = luaL_ref (self->L, LUA_REGISTRYINDEX);
        else
        {
            msg = lua_tostring (self->L, -1);
            lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "%s: %s failed: %s",
                       path, LRG_SCRIPT_HOOK_BEFORE_RELOAD,
                       msg != NULL ? msg : "(unknown)");
            lua_pop (self->L, 1);
        }
    }
    else
        lua_pop (self->L, 1);

    /* Snapshot after the hook and before the new chunk runs, so a failed
     * compile keeps the previous definitions.  In-place edits of tables
     * the hook already returned are not rolled back. */
    snap = lua_snapshot_globals (self->L);

    rc = luaL_loadfile (self->L, path);
    if (rc != 0)
    {
        msg = lua_tostring (self->L, -1);
        lua_set_error_from_msg (scripting, path, msg);
        g_set_error (error, LRG_SCRIPTING_ERROR,
                     rc == LUA_ERRSYNTAX ? LRG_SCRIPTING_ERROR_SYNTAX
                                         : LRG_SCRIPTING_ERROR_LOAD,
                     "Failed to load '%s': %s",
                     path, msg != NULL ? msg : "(unknown error)");
        lua_pop (self->L, 1);
        luaL_unref (self->L, LUA_REGISTRYINDEX, snap);
        if (state_ref != LUA_NOREF)
            luaL_unref (self->L, LUA_REGISTRYINDEX, state_ref);
        return FALSE;
    }

    rc = lua_pcall_host (self, 0, 0);
    if (rc != 0)
    {
        msg = lua_tostring (self->L, -1);
        lua_set_error_from_msg (scripting, path, msg);
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_RUNTIME,
                     "Error executing '%s': %s",
                     path, msg != NULL ? msg : "(unknown error)");
        lua_pop (self->L, 1);
        lua_restore_globals (self->L, snap);
        luaL_unref (self->L, LUA_REGISTRYINDEX, snap);
        if (state_ref != LUA_NOREF)
            luaL_unref (self->L, LUA_REGISTRYINDEX, state_ref);
        return FALSE;
    }

    lua_getglobal (self->L, LRG_SCRIPT_HOOK_RELOAD);
    if (lua_isfunction (self->L, -1))
    {
        if (state_ref != LUA_NOREF)
            lua_rawgeti (self->L, LUA_REGISTRYINDEX, state_ref);
        else
            lua_pushnil (self->L);
        rc = lua_pcall_host (self, 1, 0);
        if (rc != 0)
        {
            msg = lua_tostring (self->L, -1);
            lua_set_error_from_msg (scripting, path, msg);
            g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_RUNTIME,
                         "Error calling '%s': %s",
                         LRG_SCRIPT_HOOK_RELOAD,
                         msg != NULL ? msg : "(unknown error)");
            lua_pop (self->L, 1);
            lua_restore_globals (self->L, snap);
            luaL_unref (self->L, LUA_REGISTRYINDEX, snap);
            if (state_ref != LUA_NOREF)
                luaL_unref (self->L, LUA_REGISTRYINDEX, state_ref);
            return FALSE;
        }
    }
    else
        lua_pop (self->L, 1);

    luaL_unref (self->L, LUA_REGISTRYINDEX, snap);
    if (state_ref != LUA_NOREF)
        luaL_unref (self->L, LUA_REGISTRYINDEX, state_ref);
    return TRUE;
}

static void
lua_handle_free (gpointer data)
{
    LuaHandle *handle = (LuaHandle *) data;

    if (handle->kind == LUA_H_TABLE && handle->L != NULL)
        luaL_unref (handle->L, LUA_REGISTRYINDEX, handle->ref);
    if (handle->object != NULL)
        g_object_unref (handle->object);
    g_free (handle);
}

static gchar *
lua_repr (lua_State *L,
          int        index)
{
    int type = lua_type (L, index);
    gchar *text;

    if (type == LUA_TSTRING)
        return g_strdup (lua_tostring (L, index));
    if (type == LUA_TNUMBER)
    {
        lua_pushvalue (L, index);
        text = g_strdup (lua_tostring (L, -1));
        lua_pop (L, 1);
        return text;
    }
    if (type == LUA_TBOOLEAN)
        return g_strdup (lua_toboolean (L, index) ? "true" : "false");
    if (type == LUA_TNIL)
        return g_strdup ("nil");
    if (lrg_lua_is_gobject (L, index))
        return g_strdup ("GObject");
    return g_strdup (lua_typename (L, type));
}

static gboolean
lua_value_expandable (lua_State *L,
                      int        index)
{
    if (lua_istable (L, index))
        return TRUE;
    return lrg_lua_is_gobject (L, index);
}

static guint
lua_alloc_value (LrgScriptDebugger *debugger,
                 lua_State         *L,
                 int                index)
{
    LuaHandle *handle;

    if (lua_istable (L, index))
    {
        handle = g_new0 (LuaHandle, 1);
        handle->kind = LUA_H_TABLE;
        handle->L = L;
        lua_pushvalue (L, index);
        handle->ref = luaL_ref (L, LUA_REGISTRYINDEX);
        return lrg_script_debugger_alloc_reference (debugger, handle, lua_handle_free);
    }
    if (lrg_lua_is_gobject (L, index))
    {
        GObject *object = lrg_lua_to_gobject (L, index);

        if (object == NULL)
            return 0;
        handle = g_new0 (LuaHandle, 1);
        handle->kind = LUA_H_OBJECT;
        handle->object = g_object_ref (object);
        return lrg_script_debugger_alloc_reference (debugger, handle, lua_handle_free);
    }
    return 0;
}

static LrgScriptVariable *
lua_variable_at (LrgScriptDebugger *debugger,
                 lua_State         *L,
                 const gchar       *name,
                 int                index)
{
    g_autofree gchar *repr = lua_repr (L, index);
    guint ref = 0;
    LrgScriptVariable *var;

    if (debugger != NULL && lua_value_expandable (L, index))
        ref = lua_alloc_value (debugger, L, index);
    var = lrg_script_variable_new (name, repr, lua_typename (L, lua_type (L, index)), ref);
    lrg_script_variable_set_truthy (var, lua_toboolean (L, index));
    return var;
}

static GPtrArray *
lua_object_children (GObject *object)
{
    GPtrArray *vars = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
    guint n_props = 0;
    GParamSpec **props;
    guint i;

    props = g_object_class_list_properties (G_OBJECT_GET_CLASS (object), &n_props);
    for (i = 0; i < n_props && vars->len < LRG_LUA_CHILD_CAP; i++)
    {
        GValue value = G_VALUE_INIT;
        gchar *text;
        LrgScriptVariable *var;

        if ((props[i]->flags & G_PARAM_READABLE) == 0)
            continue;
        g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (props[i]));
        g_object_get_property (object, props[i]->name, &value);
        text = g_strdup_value_contents (&value);
        var = lrg_script_variable_new (props[i]->name, text,
                                       g_type_name (G_PARAM_SPEC_VALUE_TYPE (props[i])),
                                       0);
        g_ptr_array_add (vars, var);
        g_free (text);
        g_value_unset (&value);
    }
    g_free (props);
    return vars;
}

static GPtrArray *
lua_debug_frames (LrgScriptDebuggable *debuggable)
{
    LrgScriptingLua *self = LRG_SCRIPTING_LUA (debuggable);
    GPtrArray *frames = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
    lua_Debug ar;
    int level = 0;

    if (self->L == NULL)
        return frames;
    while (lua_getstack (self->L, level, &ar) != 0)
    {
        lua_getinfo (self->L, "Snl", &ar);
        if (ar.what == NULL || ar.what[0] != 'C')
        {
            LrgScriptStackFrame *frame;

            frame = lrg_script_stack_frame_new (level, ar.name, ar.source, ar.currentline);
            g_ptr_array_add (frames, frame);
        }
        level++;
    }
    return frames;
}

static GPtrArray *
lua_debug_scopes (LrgScriptDebuggable *debuggable,
                  LrgScriptStackFrame *frame)
{
    LrgScriptingLua *self = LRG_SCRIPTING_LUA (debuggable);
    GPtrArray *scopes = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
    int level = frame != NULL ? lrg_script_stack_frame_get_level (frame) : 0;
    LuaHandle *locals;
    LuaHandle *ups;
    LuaHandle *globals;

    locals = g_new0 (LuaHandle, 1);
    locals->kind = LUA_H_LOCALS;
    locals->L = self->L;
    locals->level = level;
    g_ptr_array_add (scopes, lrg_script_scope_new ("Locals",
        lrg_script_debugger_alloc_reference (self->debugger, locals, lua_handle_free),
        FALSE));

    ups = g_new0 (LuaHandle, 1);
    ups->kind = LUA_H_UPVALUES;
    ups->L = self->L;
    ups->level = level;
    g_ptr_array_add (scopes, lrg_script_scope_new ("Upvalues",
        lrg_script_debugger_alloc_reference (self->debugger, ups, lua_handle_free),
        FALSE));

    globals = g_new0 (LuaHandle, 1);
    globals->kind = LUA_H_GLOBALS;
    globals->L = self->L;
    g_ptr_array_add (scopes, lrg_script_scope_new ("Globals",
        lrg_script_debugger_alloc_reference (self->debugger, globals, lua_handle_free),
        TRUE));
    return scopes;
}

/* LuaJIT's lua_getlocal takes the activation record, not a stack level. */
static const char *
lua_named_local (lua_State *L,
                 int        level,
                 int        index)
{
    lua_Debug ar;

    if (lua_getstack (L, level, &ar) == 0)
        return NULL;
    return lua_getlocal (L, &ar, index);
}

static GPtrArray *
lua_debug_locals (LrgScriptingLua *self,
                  int              level)
{
    GPtrArray *vars = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
    int index = 1;
    const char *name;

    while (vars->len < LRG_LUA_CHILD_CAP &&
           (name = lua_named_local (self->L, level, index)) != NULL)
    {
        if (name[0] != '(')
            g_ptr_array_add (vars, lua_variable_at (self->debugger, self->L, name, -1));
        lua_pop (self->L, 1);
        index++;
    }
    return vars;
}

static GPtrArray *
lua_debug_upvalues (LrgScriptingLua *self,
                    int              level)
{
    GPtrArray *vars = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
    lua_Debug ar;
    int index = 1;
    const char *name;

    if (lua_getstack (self->L, level, &ar) == 0)
        return vars;
    lua_getinfo (self->L, "f", &ar);
    while ((name = lua_getupvalue (self->L, -1, index)) != NULL)
    {
        if (vars->len < LRG_LUA_CHILD_CAP)
            g_ptr_array_add (vars, lua_variable_at (self->debugger, self->L, name, -1));
        lua_pop (self->L, 1);
        index++;
    }
    lua_pop (self->L, 1);
    return vars;
}

static GPtrArray *
lua_debug_globals (LrgScriptingLua *self)
{
    GPtrArray *vars = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

    lua_getglobal (self->L, "_G");
    lua_pushnil (self->L);
    while (vars->len < LRG_LUA_CHILD_CAP && lua_next (self->L, -2) != 0)
    {
        g_autofree gchar *name = lua_repr (self->L, -2);

        g_ptr_array_add (vars, lua_variable_at (self->debugger, self->L, name, -1));
        lua_pop (self->L, 1);
    }
    lua_pop (self->L, 1);
    return vars;
}

static GPtrArray *
lua_debug_table (LrgScriptingLua *self,
                 int              ref)
{
    GPtrArray *vars = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

    lua_rawgeti (self->L, LUA_REGISTRYINDEX, ref);
    lua_pushnil (self->L);
    while (vars->len < LRG_LUA_CHILD_CAP && lua_next (self->L, -2) != 0)
    {
        g_autofree gchar *name = lua_repr (self->L, -2);

        g_ptr_array_add (vars, lua_variable_at (self->debugger, self->L, name, -1));
        lua_pop (self->L, 1);
    }
    lua_pop (self->L, 1);
    return vars;
}

static GPtrArray *
lua_debug_variables (LrgScriptDebuggable *debuggable,
                     gpointer             handle_data)
{
    LrgScriptingLua *self = LRG_SCRIPTING_LUA (debuggable);
    LuaHandle *handle = (LuaHandle *) handle_data;

    if (handle == NULL || self->L == NULL)
        return g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
    if (handle->kind == LUA_H_LOCALS)
        return lua_debug_locals (self, handle->level);
    if (handle->kind == LUA_H_UPVALUES)
        return lua_debug_upvalues (self, handle->level);
    if (handle->kind == LUA_H_GLOBALS)
        return lua_debug_globals (self);
    if (handle->kind == LUA_H_TABLE)
        return lua_debug_table (self, handle->ref);
    if (handle->kind == LUA_H_OBJECT && handle->object != NULL)
        return lua_object_children (handle->object);
    return g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

static void
lua_fill_env (lua_State *L,
              int        level,
              int        env)
{
    int index = 1;
    const char *name;
    lua_Debug ar;

    while ((name = lua_named_local (L, level, index)) != NULL)
    {
        if (name[0] != '(')
        {
            lua_pushvalue (L, -1);
            lua_setfield (L, env, name);
        }
        lua_pop (L, 1);
        index++;
    }
    if (lua_getstack (L, level, &ar) != 0)
    {
        int fn;

        lua_getinfo (L, "f", &ar);
        fn = lua_gettop (L);
        index = 1;
        while ((name = lua_getupvalue (L, fn, index)) != NULL)
        {
            lua_pushvalue (L, -1);
            lua_setfield (L, env, name);
            lua_pop (L, 1);
            index++;
        }
        lua_pop (L, 1);
    }
}

static LrgScriptVariable *
lua_debug_eval (LrgScriptDebuggable  *debuggable,
                LrgScriptStackFrame  *frame,
                const gchar          *expression,
                GError              **error)
{
    LrgScriptingLua *self = LRG_SCRIPTING_LUA (debuggable);
    int level = frame != NULL ? lrg_script_stack_frame_get_level (frame) : 0;
    int env;
    int fn;
    gchar *chunk;
    const char *msg;
    LrgScriptVariable *var;

    lua_newtable (self->L);
    env = lua_gettop (self->L);
    lua_fill_env (self->L, level, env);
    lua_newtable (self->L);
    lua_getglobal (self->L, "_G");
    lua_setfield (self->L, -2, "__index");
    lua_setmetatable (self->L, env);

    chunk = g_strdup_printf ("return %s", expression);
    if (luaL_loadstring (self->L, chunk) != 0)
    {
        lua_pop (self->L, 1);
        g_free (chunk);
        if (luaL_loadstring (self->L, expression) != 0)
        {
            msg = lua_tostring (self->L, -1);
            g_set_error (error, LRG_SCRIPT_DEBUGGER_ERROR,
                         LRG_SCRIPT_DEBUGGER_ERROR_EVALUATE,
                         "%s", msg != NULL ? msg : "syntax error");
            lua_pop (self->L, 1);
            lua_pop (self->L, 1);
            return NULL;
        }
    }
    else
        g_free (chunk);

    fn = lua_gettop (self->L);
    lua_pushvalue (self->L, env);
    lua_setfenv (self->L, fn);
    if (lua_pcall (self->L, 0, 1, 0) != 0)
    {
        msg = lua_tostring (self->L, -1);
        g_set_error (error, LRG_SCRIPT_DEBUGGER_ERROR,
                     LRG_SCRIPT_DEBUGGER_ERROR_EVALUATE,
                     "%s", msg != NULL ? msg : "evaluation failed");
        lua_pop (self->L, 1);
        lua_pop (self->L, 1);
        return NULL;
    }
    var = lua_variable_at (self->debugger, self->L, "result", -1);
    lua_pop (self->L, 1);
    lua_pop (self->L, 1);
    return var;
}

static gboolean
lua_debug_attach (LrgScriptDebuggable  *debuggable,
                  LrgScriptDebugger    *debugger,
                  GError              **error)
{
    LrgScriptingLua *self = LRG_SCRIPTING_LUA (debuggable);

    if (self->debugger != NULL && self->debugger != debugger)
    {
        g_set_error (error, LRG_SCRIPT_DEBUGGER_ERROR,
                     LRG_SCRIPT_DEBUGGER_ERROR_ALREADY_ATTACHED,
                     "Lua state is already attached to a debugger");
        return FALSE;
    }
    self->debugger = debugger;
    lua_store_self (self);
    lua_arm (self);
    return TRUE;
}

static void
lua_debug_detach (LrgScriptDebuggable *debuggable,
                  LrgScriptDebugger   *debugger)
{
    LrgScriptingLua *self = LRG_SCRIPTING_LUA (debuggable);

    if (self->debugger != NULL && self->debugger != debugger)
        return;
    self->debugger = NULL;
    lua_arm (self);
}

static void
lua_debug_update (LrgScriptDebuggable *debuggable)
{
    lua_arm (LRG_SCRIPTING_LUA (debuggable));
}

static void
lua_debuggable_init (LrgScriptDebuggableInterface *iface)
{
    iface->debug_attach = lua_debug_attach;
    iface->debug_detach = lua_debug_detach;
    iface->debug_update = lua_debug_update;
    iface->debug_get_frames = lua_debug_frames;
    iface->debug_get_scopes = lua_debug_scopes;
    iface->debug_get_variables = lua_debug_variables;
    iface->debug_evaluate = lua_debug_eval;
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
        lua_set_error_from_msg (scripting, path, msg);
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
    result = lua_pcall_host (self, 0, 0);
    if (result != 0)
    {
        const gchar *msg = lua_tostring (self->L, -1);
        lua_set_error_from_msg (scripting, path, msg);
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
        lua_set_error_from_msg (scripting, name, msg);
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
    result = lua_pcall_host (self, 0, 0);
    if (result != 0)
    {
        const gchar *msg = lua_tostring (self->L, -1);
        lua_set_error_from_msg (scripting, name, msg);
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
    result = lua_pcall_host (self, (int) n_args, nresults);

    if (result != 0)
    {
        const gchar *msg = lua_tostring (self->L, -1);
        lua_set_error_from_msg (scripting, func_name, msg);
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
    GError              *error = NULL;
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
        /*
         * lua_error() longjmps out of this frame, so nothing may be left
         * to free when it runs: push the message (Lua copies it), release
         * the error and any return value first, then raise.
         */
        lua_pushstring (L, error != NULL ? error->message : "Unknown error");
        g_clear_error (&error);
        if (G_IS_VALUE (&return_value))
            g_value_unset (&return_value);
        return lua_error (L);
    }
    g_clear_error (&error);

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

    /* Create new state; tasks of the old one see the new generation */
    self->generation++;
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
        lua_store_self (self);

        /* Update package path */
        update_package_path (self);

        if (self->debugger != NULL)
            lua_arm (self);

        if (self->jit_disabled)
            luaJIT_setmode (self->L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
    }

    lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "Script context reset");
}

/* Generic search-path vfunc: forwards to the Lua package.path helper */
static void
lrg_scripting_lua_vfunc_add_search_path (LrgScripting *scripting,
                                         const gchar  *path)
{
    lrg_scripting_lua_add_search_path (LRG_SCRIPTING_LUA (scripting), path);
}

/* A function exists when the global of that name is a Lua function */
static gboolean
lrg_scripting_lua_has_function (LrgScripting *scripting,
                                const gchar  *func_name)
{
    LrgScriptingLua *self = LRG_SCRIPTING_LUA (scripting);
    gboolean         found;

    if (self->L == NULL)
    {
        return FALSE;
    }

    lua_getglobal (self->L, func_name);
    found = lua_isfunction (self->L, -1);
    lua_pop (self->L, 1);

    return found;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_scripting_lua_finalize (GObject *object)
{
    LrgScriptingLua *self = LRG_SCRIPTING_LUA (object);

    /* Weak notify already unlinked us.  Clear the hook before lua_close. */
    self->debugger = NULL;
    if (self->L != NULL)
    {
        lua_sethook (self->L, NULL, 0, 0);
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
    scripting_class->reload_file = lua_reload_file;
    scripting_class->load_string = lrg_scripting_lua_load_string;
    scripting_class->call_function = lrg_scripting_lua_call_function;
    scripting_class->register_function = lrg_scripting_lua_register_function;
    scripting_class->get_global = lrg_scripting_lua_get_global;
    scripting_class->set_global = lrg_scripting_lua_set_global;
    scripting_class->reset = lrg_scripting_lua_reset;
    scripting_class->add_search_path = lrg_scripting_lua_vfunc_add_search_path;
    scripting_class->has_function = lrg_scripting_lua_has_function;
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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
        result = lua_pcall_host (self, 1, 0);
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

/*
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

/*
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

/*
 * lrg_scripting_lua_get_generation:
 * @self: an #LrgScriptingLua
 *
 * Counts the states the context created (lrg_scripting_reset() makes a
 * new one): references into a state, such as a task's coroutine, are
 * valid only while the generation they were made in is current.
 *
 * Returns: the current generation
 */
guint
lrg_scripting_lua_get_generation (LrgScriptingLua *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPTING_LUA (self), 0);

    return self->generation;
}

void
lrg_scripting_lua_set_jit_enabled (LrgScriptingLua *self,
                                   gboolean         enabled)
{
    g_return_if_fail (LRG_IS_SCRIPTING_LUA (self));

    self->jit_disabled = !enabled;
    if (self->L != NULL)
        luaJIT_setmode (self->L, 0, LUAJIT_MODE_ENGINE | (enabled ? LUAJIT_MODE_ON : LUAJIT_MODE_OFF));
}

gboolean
lrg_scripting_lua_get_jit_enabled (LrgScriptingLua *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPTING_LUA (self), FALSE);

    return !self->jit_disabled;
}
