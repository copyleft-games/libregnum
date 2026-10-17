/* lrg-script-task-lua.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Lua coroutine tasks.
 *
 *   body      a thread of the context's state, anchored in the registry,
 *             with the body function on its stack until the first resume
 *   waits     the primitives yield (sentinel, kind, value): the resume
 *             reads them and turns them into the task's wait; any other
 *             yield of the task's thread is a wait for the next frame
 *   budget    a LUA_MASKCOUNT hook every slice instructions adds the
 *             slice to the resume's count; once the count reaches the
 *             budget or the time passes the deadline, it yields the
 *             thread (LuaJIT lets count hooks yield) when the code is
 *             directly in the task (no C function but pcall/xpcall
 *             between it and the task's base), else it waits for such a
 *             point and raises an error past the watchdog
 *
 * LuaJIT keeps one hook for the whole state, so the hook is set for the
 * resume and removed after it.
 */

#include "config.h"

#include "lrg-script-task-lua.h"
#include "lrg-script-task-private.h"
#include "lrg-scripting-lua-private.h"
#include "../lrg-log.h"

#include <string.h>
#include <luajit.h>

/* the first value of every primitive's yield */
static gchar task_sentinel;

/* registry keys of the functions a yield may cross */
#define REG_PCALL   "lrg.task.pcall"
#define REG_XPCALL  "lrg.task.xpcall"
#define LIB_META    "lrg.task.library"

struct _LrgScriptTaskLua
{
    LrgScriptTask   parent_instance;

    GWeakRef        scripting;    /* LrgScriptingLua */
    lua_State      *main;         /* the state the thread lives in */
    guint           generation;   /* the context's state when it was made */
    lua_State      *thread;
    gint            thread_ref;   /* registry reference, LUA_NOREF when released */
};

G_DEFINE_FINAL_TYPE (LrgScriptTaskLua, lrg_script_task_lua, LRG_TYPE_SCRIPT_TASK)

/* what the hook needs during one resume */
typedef struct _ResumeContext ResumeContext;

struct _ResumeContext
{
    LrgScriptTaskLua *task;
    lua_State        *thread;
    guint64           budget;       /* instructions */
    guint64           used;
    guint             slice;
    gint64            deadline_us;
    gint64            watchdog_us;
    gboolean          preempted;
    gboolean          watchdog_hit;
    ResumeContext    *previous;
};

static ResumeContext *active_resume = NULL;

/* --- the hook --- */

/*
 * can_yield:
 * TRUE when no C function other than pcall / xpcall lies between the
 * running code and the base of @L (a yield would cross it).
 */
static gboolean
can_yield (lua_State *L)
{
    lua_Debug ar;
    gint level;

    for (level = 1; lua_getstack (L, level, &ar); level++)
    {
        gboolean crossable;

        if (!lua_getinfo (L, "Sf", &ar))
            return FALSE;
        if (ar.what == NULL || strcmp (ar.what, "C") != 0)
        {
            lua_pop (L, 1);
            continue;
        }
        lua_getfield (L, LUA_REGISTRYINDEX, REG_PCALL);
        crossable = lua_rawequal (L, -1, -2);
        lua_pop (L, 1);
        if (!crossable)
        {
            lua_getfield (L, LUA_REGISTRYINDEX, REG_XPCALL);
            crossable = lua_rawequal (L, -1, -2);
            lua_pop (L, 1);
        }
        lua_pop (L, 1);
        if (!crossable)
            return FALSE;
    }
    return TRUE;
}

static void
count_hook (lua_State *L,
            lua_Debug *ar)
{
    ResumeContext *ctx;
    gint64 now;

    (void)ar;
    ctx = active_resume;
    if (ctx == NULL)
        return;
    ctx->used += ctx->slice;
    now = g_get_monotonic_time ();
    if (ctx->used < ctx->budget && now < ctx->deadline_us)
        return;

    /* --- the budget is spent: suspend the task where it is --- */
    if (L == ctx->thread && can_yield (L))
    {
        ctx->preempted = TRUE;
        lua_yield (L, 0);
        return;
    }

    /* --- it cannot be suspended here: wait for a point, bounded --- */
    if ((ctx->watchdog_us > 0 && now > ctx->deadline_us + ctx->watchdog_us) ||
        ctx->used > ctx->budget * 8 + ctx->slice)
    {
        ctx->watchdog_hit = TRUE;
        /* (Lua's formatter knows %d, not 64-bit formats) */
        luaL_error (L, "script task budget exceeded: no point to suspend at for %d ms and %d"
                    " instructions (inside a C function or a coroutine of its own)",
                    (int)MAX ((now - ctx->deadline_us) / 1000, 0),
                    (int)MIN (ctx->used, (guint64)G_MAXINT));
    }
}

/* --- the task --- */

/* the context is the one the task was made in, and still there */
static LrgScriptingLua *
get_context (LrgScriptTaskLua *self)
{
    LrgScriptingLua *lua;

    lua = (LrgScriptingLua *)g_weak_ref_get (&self->scripting);
    if (lua == NULL)
        return NULL;
    if (lrg_scripting_lua_get_generation (lua) != self->generation ||
        lrg_scripting_lua_get_state (lua) != self->main)
    {
        g_object_unref (lua);
        return NULL;
    }
    return lua;
}

/* the yield's values: a primitive's wait, or a plain yield */
static void
read_wait (LrgScriptTaskLua *self)
{
    LrgScriptTask *task = LRG_SCRIPT_TASK (self);
    lua_State *T = self->thread;
    const gchar *kind;

    if (lua_gettop (T) < 2 || lua_touserdata (T, 1) != (void *)&task_sentinel)
    {
        lrg_script_task_wait_frames (task, 1);
        return;
    }
    kind = lua_tostring (T, 2);
    if (g_strcmp0 (kind, "seconds") == 0)
        lrg_script_task_wait_seconds (task, lua_tonumber (T, 3));
    else if (g_strcmp0 (kind, "frames") == 0)
        lrg_script_task_wait_frames (task, (guint)MAX (lua_tonumber (T, 3), 0.0));
    else
        lrg_script_task_wait_frames (task, 1);
}

static gboolean
lrg_script_task_lua_resume (LrgScriptTask          *task,
                            const LrgScriptBudget  *budget,
                            GError                **error)
{
    LrgScriptTaskLua *self = LRG_SCRIPT_TASK_LUA (task);
    g_autoptr(LrgScriptingLua) lua = NULL;
    ResumeContext ctx;
    gint status;
    gint nargs;

    lua = get_context (self);
    if (lua == NULL || self->thread == NULL || self->thread_ref == LUA_NOREF)
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_NOT_FOUND,
                     "the Lua context of the task is gone or was reset");
        return FALSE;
    }

    /* --- the hook for this resume --- */
    memset (&ctx, 0, sizeof (ctx));
    ctx.task = self;
    ctx.thread = self->thread;
    ctx.budget = budget != NULL ? budget->instructions : G_MAXUINT64;
    ctx.slice = budget != NULL && budget->slice > 0 ? budget->slice : 1000;
    ctx.deadline_us = budget != NULL ? budget->deadline_us : G_MAXINT64 / 2;
    ctx.watchdog_us = budget != NULL ? budget->watchdog_us : 0;
    ctx.previous = active_resume;
    active_resume = &ctx;
    lua_sethook (self->main, count_hook, LUA_MASKCOUNT, (int)ctx.slice);

    /* the first resume runs the function on the stack; later ones
     * continue from the yield (its values were cleared) */
    nargs = lua_status (self->thread) == LUA_YIELD ? 0 : lua_gettop (self->thread) - 1;
    status = lua_resume (self->thread, MAX (nargs, 0));

    active_resume = ctx.previous;
    if (active_resume == NULL)
        lua_sethook (self->main, NULL, 0, 0);
    else
        lua_sethook (self->main, count_hook, LUA_MASKCOUNT, (int)active_resume->slice);
    lrg_script_task_add_instructions (task, ctx.used);

    /* --- what the body did --- */
    if (status == LUA_YIELD)
    {
        if (ctx.preempted)
            lrg_script_task_note_preempted (task);
        else
            read_wait (self);
        lua_settop (self->thread, 0);
        return TRUE;
    }
    if (status == 0)
    {
        lua_settop (self->thread, 0);
        lrg_script_task_finish (task);
        return TRUE;
    }
    {
        const gchar *message;

        message = lua_tostring (self->thread, -1);
        g_set_error (error, LRG_SCRIPTING_ERROR,
                     ctx.watchdog_hit ? LRG_SCRIPTING_ERROR_BUDGET : LRG_SCRIPTING_ERROR_RUNTIME,
                     "%s", message != NULL ? message : "(unknown error)");
        lua_settop (self->thread, 0);
    }
    return FALSE;
}

static gboolean
lrg_script_task_lua_supports_preemption (LrgScriptTask *task)
{
    (void)task;
    return TRUE;
}

static void
lrg_script_task_lua_release (LrgScriptTask *task)
{
    LrgScriptTaskLua *self = LRG_SCRIPT_TASK_LUA (task);
    g_autoptr(LrgScriptingLua) lua = NULL;

    /* the registry entry belongs to a state that may be gone */
    lua = get_context (self);
    if (lua != NULL && self->thread_ref != LUA_NOREF)
        luaL_unref (self->main, LUA_REGISTRYINDEX, self->thread_ref);
    self->thread_ref = LUA_NOREF;
    self->thread = NULL;
}

static void
lrg_script_task_lua_finalize (GObject *object)
{
    LrgScriptTaskLua *self = LRG_SCRIPT_TASK_LUA (object);

    g_weak_ref_clear (&self->scripting);

    G_OBJECT_CLASS (lrg_script_task_lua_parent_class)->finalize (object);
}

static void
lrg_script_task_lua_class_init (LrgScriptTaskLuaClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgScriptTaskClass *task_class = LRG_SCRIPT_TASK_CLASS (klass);

    object_class->finalize = lrg_script_task_lua_finalize;
    task_class->resume = lrg_script_task_lua_resume;
    task_class->release = lrg_script_task_lua_release;
    task_class->supports_preemption = lrg_script_task_lua_supports_preemption;
}

static void
lrg_script_task_lua_init (LrgScriptTaskLua *self)
{
    g_weak_ref_init (&self->scripting, NULL);
    self->thread_ref = LUA_NOREF;
}

/* the functions a preemption may yield across, kept once per state */
static void
remember_crossable (lua_State *L)
{
    lua_getfield (L, LUA_REGISTRYINDEX, REG_PCALL);
    if (!lua_isnil (L, -1))
    {
        lua_pop (L, 1);
        return;
    }
    lua_pop (L, 1);
    lua_getglobal (L, "pcall");
    lua_setfield (L, LUA_REGISTRYINDEX, REG_PCALL);
    lua_getglobal (L, "xpcall");
    lua_setfield (L, LUA_REGISTRYINDEX, REG_XPCALL);
}

/*
 * new_task:
 * A task with a fresh thread of @lua's state; the caller pushes the body
 * function onto the thread's stack.
 */
static LrgScriptTaskLua *
new_task (LrgScriptingLua *lua,
          lua_State       *L)
{
    LrgScriptTaskLua *self;

    self = g_object_new (LRG_TYPE_SCRIPT_TASK_LUA, NULL);
    g_weak_ref_set (&self->scripting, lua);
    self->main = lrg_scripting_lua_get_state (lua);
    self->generation = lrg_scripting_lua_get_generation (lua);
    remember_crossable (L);
    self->thread = lua_newthread (L);
    self->thread_ref = luaL_ref (L, LUA_REGISTRYINDEX);
    return self;
}

/* compiled traces skip hooks: the body and its inner functions interpret */
static void
interpret_body (LrgScriptTaskLua *self)
{
    luaJIT_setmode (self->thread, -1, LUAJIT_MODE_ALLFUNC | LUAJIT_MODE_OFF);
}

LrgScriptTask *
lrg_script_task_lua_new_from_string (LrgScriptingLua  *lua,
                                     const gchar      *name,
                                     const gchar      *code,
                                     GError          **error)
{
    LrgScriptTaskLua *self;
    lua_State *L;
    gint result;

    g_return_val_if_fail (LRG_IS_SCRIPTING_LUA (lua), NULL);
    g_return_val_if_fail (code != NULL, NULL);

    L = lrg_scripting_lua_get_state (lua);
    g_return_val_if_fail (L != NULL, NULL);

    self = new_task (lua, L);
    result = luaL_loadbuffer (self->thread, code, strlen (code), name != NULL ? name : "task");
    if (result != 0)
    {
        const gchar *message;

        message = lua_tostring (self->thread, -1);
        g_set_error (error, LRG_SCRIPTING_ERROR,
                     result == LUA_ERRSYNTAX ? LRG_SCRIPTING_ERROR_SYNTAX : LRG_SCRIPTING_ERROR_LOAD,
                     "%s", message != NULL ? message : "(unknown error)");
        lua_settop (self->thread, 0);
        g_object_unref (self);
        return NULL;
    }
    interpret_body (self);
    lrg_script_task_set_name (LRG_SCRIPT_TASK (self), name);
    return LRG_SCRIPT_TASK (self);
}

LrgScriptTask *
lrg_script_task_lua_new_from_global (LrgScriptingLua  *lua,
                                     const gchar      *func_name,
                                     GError          **error)
{
    LrgScriptTaskLua *self;
    lua_State *L;

    g_return_val_if_fail (LRG_IS_SCRIPTING_LUA (lua), NULL);
    g_return_val_if_fail (func_name != NULL, NULL);

    L = lrg_scripting_lua_get_state (lua);
    g_return_val_if_fail (L != NULL, NULL);

    lua_getglobal (L, func_name);
    if (!lua_isfunction (L, -1))
    {
        lua_pop (L, 1);
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_NOT_FOUND,
                     "Function '%s' not found", func_name);
        return NULL;
    }
    lua_pop (L, 1);
    self = new_task (lua, L);
    lua_getglobal (self->thread, func_name);
    interpret_body (self);
    lrg_script_task_set_name (LRG_SCRIPT_TASK (self), func_name);
    return LRG_SCRIPT_TASK (self);
}

/* ------------------------------------------------------------------ */
/* the library                                                          */
/* ------------------------------------------------------------------ */

/* the upvalue of every library function (a full userdata) */
typedef struct
{
    GWeakRef                       scripting;
    gchar                         *owner;
    LrgScriptSchedulerResolveFunc  resolve;
    gpointer                       user_data;
    GDestroyNotify                 destroy;
} Library;

static gint
library_gc (lua_State *L)
{
    Library *lib;

    lib = (Library *)lua_touserdata (L, 1);
    if (lib == NULL)
        return 0;
    if (lib->destroy != NULL && lib->user_data != NULL)
        lib->destroy (lib->user_data);
    lib->destroy = NULL;
    lib->user_data = NULL;
    g_clear_pointer (&lib->owner, g_free);
    g_weak_ref_clear (&lib->scripting);
    return 0;
}

static Library *
library_get (lua_State *L)
{
    return (Library *)lua_touserdata (L, lua_upvalueindex (1));
}

static LrgScriptScheduler *
library_scheduler (lua_State *L)
{
    Library *lib;

    lib = library_get (L);
    return lib != NULL && lib->resolve != NULL ? lib->resolve (lib->user_data) : NULL;
}

/* the running task when @L is its thread, else raise */
static LrgScriptTaskLua *
require_task (lua_State   *L,
              const gchar *what)
{
    LrgScriptTask *task;

    task = lrg_script_task_get_current ();
    if (task == NULL || !LRG_IS_SCRIPT_TASK_LUA (task) ||
        LRG_SCRIPT_TASK_LUA (task)->thread != L)
    {
        luaL_error (L, "%s: not inside a task (start one with spawn)", what);
        return NULL;
    }
    return LRG_SCRIPT_TASK_LUA (task);
}

static gint
lib_wait (lua_State *L)
{
    lua_Number seconds;

    seconds = luaL_optnumber (L, 1, 0.0);
    require_task (L, "wait");
    lua_settop (L, 0);
    lua_pushlightuserdata (L, (void *)&task_sentinel);
    lua_pushstring (L, "seconds");
    lua_pushnumber (L, seconds);
    return lua_yield (L, 3);
}

static gint
lib_wait_frames (lua_State *L)
{
    lua_Number frames;

    frames = luaL_optnumber (L, 1, 1.0);
    require_task (L, "wait_frames");
    lua_settop (L, 0);
    lua_pushlightuserdata (L, (void *)&task_sentinel);
    lua_pushstring (L, "frames");
    lua_pushnumber (L, frames);
    return lua_yield (L, 3);
}

static gint
lib_yield (lua_State *L)
{
    require_task (L, "yield");
    lua_settop (L, 0);
    lua_pushlightuserdata (L, (void *)&task_sentinel);
    lua_pushstring (L, "none");
    return lua_yield (L, 2);
}

static gint
lib_cancelled (lua_State *L)
{
    LrgScriptTask *task;

    task = lrg_script_task_get_current ();
    lua_pushboolean (L, task != NULL && lrg_script_task_is_cancel_requested (task));
    return 1;
}

static gint
lib_running (lua_State *L)
{
    LrgScriptTask *task;

    task = lrg_script_task_get_current ();
    if (task != NULL && LRG_IS_SCRIPT_TASK_LUA (task) && LRG_SCRIPT_TASK_LUA (task)->thread == L)
        lua_pushnumber (L, (lua_Number)lrg_script_task_get_id (task));
    else
        lua_pushnil (L);
    return 1;
}

static gint
lib_spawn (lua_State *L)
{
    g_autoptr(LrgScriptingLua) lua = NULL;
    LrgScriptScheduler *scheduler;
    LrgScriptTaskLua *task;
    Library *lib;
    guint id;

    luaL_checktype (L, 1, LUA_TFUNCTION);
    lib = library_get (L);
    scheduler = library_scheduler (L);
    lua = lib != NULL ? (LrgScriptingLua *)g_weak_ref_get (&lib->scripting) : NULL;
    if (scheduler == NULL || lua == NULL)
        return luaL_error (L, "spawn: there is no task scheduler now");

    task = new_task (lua, L);
    lua_pushvalue (L, 1);
    lua_xmove (L, task->thread, 1);
    interpret_body (task);
    lrg_script_task_set_name (LRG_SCRIPT_TASK (task), luaL_optstring (L, 2, "spawned"));
    lrg_script_task_set_owner (LRG_SCRIPT_TASK (task), lib->owner);
    id = lrg_script_scheduler_add (scheduler, LRG_SCRIPT_TASK (task));
    g_object_unref (task);
    lua_pushnumber (L, (lua_Number)id);
    return 1;
}

static gint
lib_cancel (lua_State *L)
{
    LrgScriptScheduler *scheduler;
    lua_Number id;

    id = luaL_checknumber (L, 1);
    scheduler = library_scheduler (L);
    lua_pushboolean (L, scheduler != NULL && id >= 1 &&
                     lrg_script_scheduler_cancel (scheduler, (guint)id));
    return 1;
}

static gint
lib_time (lua_State *L)
{
    LrgScriptScheduler *scheduler;

    scheduler = library_scheduler (L);
    lua_pushnumber (L, scheduler != NULL ? lrg_script_scheduler_get_time (scheduler) : 0.0);
    return 1;
}

static gint
lib_frame (lua_State *L)
{
    LrgScriptScheduler *scheduler;

    scheduler = library_scheduler (L);
    lua_pushnumber (L, scheduler != NULL ? (lua_Number)lrg_script_scheduler_get_frame (scheduler) : 0.0);
    return 1;
}

/* wait_until, written in Lua over the C yield and clock */
static const gchar wait_until_source[] =
    "local yield, clock = ...\n"
    "return function(fn, timeout)\n"
    "  local started = clock()\n"
    "  while not fn() do\n"
    "    if timeout ~= nil and clock() - started >= timeout then return false end\n"
    "    yield()\n"
    "  end\n"
    "  return true\n"
    "end\n";

gboolean
lrg_script_task_lua_install_library (LrgScriptingLua                *lua,
                                     const gchar                    *table,
                                     const gchar                    *owner,
                                     LrgScriptSchedulerResolveFunc   resolve,
                                     gpointer                        user_data,
                                     GDestroyNotify                  destroy,
                                     GError                        **error)
{
    static const struct
    {
        const gchar   *name;
        lua_CFunction  func;
    } functions[] = {
        { "wait", lib_wait },
        { "wait_frames", lib_wait_frames },
        { "yield", lib_yield },
        { "cancelled", lib_cancelled },
        { "running", lib_running },
        { "spawn", lib_spawn },
        { "cancel", lib_cancel },
        { "time", lib_time },
        { "frame", lib_frame }
    };
    Library *lib;
    lua_State *L;
    gint table_index;
    gint lib_index;
    guint i;

    g_return_val_if_fail (LRG_IS_SCRIPTING_LUA (lua), FALSE);
    g_return_val_if_fail (table != NULL && *table != '\0', FALSE);
    g_return_val_if_fail (resolve != NULL, FALSE);

    L = lrg_scripting_lua_get_state (lua);
    if (L == NULL)
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_FAILED, "no Lua state");
        return FALSE;
    }
    remember_crossable (L);

    /* --- the table --- */
    lua_getglobal (L, table);
    if (!lua_istable (L, -1))
    {
        lua_pop (L, 1);
        lua_newtable (L);
        lua_pushvalue (L, -1);
        lua_setglobal (L, table);
    }
    table_index = lua_gettop (L);

    /* --- the shared upvalue, collected with the state --- */
    lib = (Library *)lua_newuserdata (L, sizeof (Library));
    memset (lib, 0, sizeof (Library));
    g_weak_ref_init (&lib->scripting, lua);
    lib->owner = g_strdup (owner);
    lib->resolve = resolve;
    lib->user_data = user_data;
    lib->destroy = destroy;
    if (luaL_newmetatable (L, LIB_META))
    {
        lua_pushcfunction (L, library_gc);
        lua_setfield (L, -2, "__gc");
    }
    lua_setmetatable (L, -2);
    lib_index = lua_gettop (L);

    for (i = 0; i < G_N_ELEMENTS (functions); i++)
    {
        lua_pushvalue (L, lib_index);
        lua_pushcclosure (L, functions[i].func, 1);
        lua_setfield (L, table_index, functions[i].name);
    }

    /* --- wait_until over yield and time --- */
    if (luaL_loadbuffer (L, wait_until_source, strlen (wait_until_source), "=wait_until") != 0)
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_LOAD,
                     "wait_until: %s", lua_tostring (L, -1));
        lua_settop (L, table_index - 1);
        return FALSE;
    }
    lua_getfield (L, table_index, "yield");
    lua_getfield (L, table_index, "time");
    if (lua_pcall (L, 2, 1, 0) != 0)
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_RUNTIME,
                     "wait_until: %s", lua_tostring (L, -1));
        lua_settop (L, table_index - 1);
        return FALSE;
    }
    luaJIT_setmode (L, -1, LUAJIT_MODE_FUNC | LUAJIT_MODE_OFF);
    lua_setfield (L, table_index, "wait_until");

    lua_settop (L, table_index - 1);
    return TRUE;
}
