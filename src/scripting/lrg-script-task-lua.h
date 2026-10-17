/* lrg-script-task-lua.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScriptTaskLua - a Lua coroutine run by an #LrgScriptScheduler: it
 * yields on every wait, and a count hook suspends it when the frame's
 * instruction or time budget runs out, so even `while true do end`
 * never takes the main thread.  The library that scripts use
 * (`wait`, `wait_frames`, `wait_until`, `yield`, `cancelled`, `spawn`,
 * `cancel`, `running`, `time`, `frame`) is installed into a table of a
 * context with lrg_script_task_lua_install_library().
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

#include "../lrg-version.h"
#include "lrg-script-task.h"
#include "lrg-script-scheduler.h"
#include "lrg-scripting-lua.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCRIPT_TASK_LUA (lrg_script_task_lua_get_type ())

#pragma GCC visibility push(default)
G_DECLARE_FINAL_TYPE (LrgScriptTaskLua, lrg_script_task_lua, LRG, SCRIPT_TASK_LUA, LrgScriptTask)
#pragma GCC visibility pop

/**
 * LrgScriptTaskLua:
 *
 * A coroutine of an #LrgScriptingLua context.  Waits are yields of the
 * coroutine; a count hook every #LrgScriptScheduler:instruction-slice
 * instructions suspends it once the frame's budget is spent (a
 * "preemption": it continues on the next frame where it stopped).  The
 * hook cannot suspend Lua code running under a C function other than
 * `pcall` / `xpcall` (a `table.sort` comparator, a host function calling
 * back into Lua) or inside a coroutine of the script's own: there it
 * waits until the code is back in the task; code that stays there past
 * #LrgScriptScheduler:watchdog-us (or eight budgets of instructions)
 * fails the task with %LRG_SCRIPTING_ERROR_BUDGET.
 *
 * LuaJIT runs compiled traces without hooks, so the task switches the
 * compiler off for its function and every function defined inside it;
 * functions the task calls that were defined elsewhere stay compiled
 * and can only be suspended when they return.  A context that runs
 * nothing but tasks turns the compiler off entirely
 * (lrg_scripting_lua_set_jit_enabled()).
 *
 * The task holds a weak reference to its context: when the context is
 * gone or was reset, the next resume fails the task.
 */

/**
 * lrg_script_task_lua_new_from_string:
 * @lua: a Lua context
 * @name: a chunk name for errors
 * @code: Lua source; the chunk is the task's body
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): the task, %NULL with
 *   %LRG_SCRIPTING_ERROR_SYNTAX when @code does not compile
 */
LRG_AVAILABLE_IN_ALL
LrgScriptTask *
lrg_script_task_lua_new_from_string (LrgScriptingLua  *lua,
                                     const gchar      *name,
                                     const gchar      *code,
                                     GError          **error);

/**
 * lrg_script_task_lua_new_from_global:
 * @lua: a Lua context
 * @func_name: a global function of the context
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): a task running the function, %NULL
 *   with %LRG_SCRIPTING_ERROR_NOT_FOUND when there is none
 */
LRG_AVAILABLE_IN_ALL
LrgScriptTask *
lrg_script_task_lua_new_from_global (LrgScriptingLua  *lua,
                                     const gchar      *func_name,
                                     GError          **error);

/**
 * LrgScriptSchedulerResolveFunc:
 * @user_data: (closure): data given to lrg_script_task_lua_install_library()
 *
 * Finds the scheduler a script's `spawn` adds its tasks to, when it is
 * called (a game's scheduler may come and go with its session).
 *
 * Returns: (transfer none) (nullable): the scheduler, %NULL when there is
 *   none now
 */
typedef LrgScriptScheduler *(*LrgScriptSchedulerResolveFunc) (gpointer user_data);

/**
 * lrg_script_task_lua_install_library:
 * @lua: a Lua context
 * @table: the global table to fill (created when missing), e.g. "lf"
 * @owner: (nullable): the owner tag of the tasks its `spawn` starts
 * @resolve: (scope notified) (closure user_data) (destroy destroy): finds
 *   the scheduler for `spawn`
 * @user_data: data for @resolve
 * @destroy: (nullable): frees @user_data when the context closes
 * @error: return location for a #GError
 *
 * Installs the task primitives into @table:
 *
 * - `wait(seconds)`, `wait_frames(n)`, `yield()`: suspend the running
 *   task (inside a task only; anywhere else they raise an error);
 * - `wait_until(fn [, timeout])`: yields every frame until `fn()` is
 *   true (returns true) or @timeout seconds passed (returns false);
 * - `cancelled()`: true once the running task was asked to stop (false
 *   outside a task);
 * - `spawn(fn [, name])`: starts `fn` as a new task (next frame), returns
 *   its id; `cancel(id)`: asks it to stop; `running()`: the running
 *   task's id or nil;
 * - `time()`, `frame()`: the scheduler's clock and frame (0 without one).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_script_task_lua_install_library (LrgScriptingLua                *lua,
                                     const gchar                    *table,
                                     const gchar                    *owner,
                                     LrgScriptSchedulerResolveFunc   resolve,
                                     gpointer                        user_data,
                                     GDestroyNotify                  destroy,
                                     GError                        **error);

G_END_DECLS
