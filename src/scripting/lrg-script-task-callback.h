/* lrg-script-task-callback.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScriptTaskCallback - a task whose body is a step: a C function, or
 * a function of any #LrgScripting backend, called once per resume.  It
 * is the task kind of the backends without coroutines here (Python,
 * PyGObject, Gjs, crispy C, native modules): the step does a little and
 * says when to be called again.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

#include "../lrg-version.h"
#include "lrg-script-task.h"
#include "lrg-scripting.h"

G_BEGIN_DECLS

/**
 * LrgScriptTaskStepFunc:
 * @task: the task being resumed
 * @user_data: (closure): data given to lrg_script_task_callback_new()
 * @error: return location for a #GError
 *
 * One step of a callback task.  It calls lrg_script_task_wait_seconds(),
 * lrg_script_task_wait_frames(), lrg_script_task_wait_condition() or
 * lrg_script_task_finish() on @task before it returns; with none of them
 * it is called again on the next frame.  While a cancel was requested
 * (lrg_script_task_is_cancel_requested()) this is the last call.
 *
 * Returns: %FALSE with @error set when the step failed
 */
typedef gboolean (*LrgScriptTaskStepFunc) (LrgScriptTask  *task,
                                           gpointer        user_data,
                                           GError        **error);

#define LRG_TYPE_SCRIPT_TASK_CALLBACK (lrg_script_task_callback_get_type ())

#pragma GCC visibility push(default)
G_DECLARE_FINAL_TYPE (LrgScriptTaskCallback, lrg_script_task_callback, LRG, SCRIPT_TASK_CALLBACK, LrgScriptTask)
#pragma GCC visibility pop

/**
 * LrgScriptTaskCallback:
 *
 * A step task.  It cannot be cut short inside a step: a step that runs
 * past the scheduler's watchdog fails the task
 * (%LRG_SCRIPTING_ERROR_BUDGET) after it returned.
 */

/**
 * lrg_script_task_callback_new:
 * @func: (scope notified) (closure user_data) (destroy destroy): the step
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data when the task ends
 *
 * Returns: (transfer full): a task calling @func once per resume
 */
LRG_AVAILABLE_IN_ALL
LrgScriptTask *
lrg_script_task_callback_new (LrgScriptTaskStepFunc func,
                              gpointer              user_data,
                              GDestroyNotify        destroy);

/**
 * lrg_script_task_callback_new_for_function:
 * @scripting: a scripting context of any backend
 * @func_name: a function the context's scripts define
 *
 * A task that calls @func_name (task id, cancelled) once per resume
 * through lrg_scripting_call_function().  What it returns says what
 * next: a number above 0 waits that many seconds, 0 or %TRUE runs it
 * again on the next frame, %FALSE, nothing or nil finishes the task.  A
 * script error fails the task.  The task holds only a weak reference
 * to @scripting: when the context is gone, the task fails.
 *
 * Returns: (transfer full): the task
 */
LRG_AVAILABLE_IN_ALL
LrgScriptTask *
lrg_script_task_callback_new_for_function (LrgScripting *scripting,
                                           const gchar  *func_name);

/**
 * lrg_script_task_callback_get_function:
 * @self: an #LrgScriptTaskCallback
 *
 * Returns: (transfer none) (nullable): the script function's name, %NULL
 *   for a C step
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_script_task_callback_get_function (LrgScriptTaskCallback *self);

G_END_DECLS
