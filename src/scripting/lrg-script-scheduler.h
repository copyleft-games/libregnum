/* lrg-script-scheduler.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScriptScheduler - runs #LrgScriptTask objects a little every frame
 * on a time and instruction budget, so long-running script code (a loop
 * with waits, a busy loop) never takes the main thread.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-script-task.h"

G_BEGIN_DECLS

/**
 * LRG_SCRIPT_SCHEDULER_DEFAULT_FRAME_BUDGET_US:
 *
 * Microseconds of every frame the scheduler spends on its tasks unless
 * told otherwise (2 ms).
 */
#define LRG_SCRIPT_SCHEDULER_DEFAULT_FRAME_BUDGET_US (2000)

/**
 * LRG_SCRIPT_SCHEDULER_DEFAULT_INSTRUCTION_BUDGET:
 *
 * Instructions per frame counting backends may run unless told
 * otherwise.
 */
#define LRG_SCRIPT_SCHEDULER_DEFAULT_INSTRUCTION_BUDGET (200000)

/**
 * LRG_SCRIPT_SCHEDULER_DEFAULT_SLICE:
 *
 * How often (in instructions) a counting backend checks the budget.
 */
#define LRG_SCRIPT_SCHEDULER_DEFAULT_SLICE (1000)

/**
 * LRG_SCRIPT_SCHEDULER_DEFAULT_WATCHDOG_US:
 *
 * How far past the frame's budget one resume may run when it cannot be
 * cut short before its task fails (250 ms).
 */
#define LRG_SCRIPT_SCHEDULER_DEFAULT_WATCHDOG_US (250000)

#define LRG_TYPE_SCRIPT_SCHEDULER (lrg_script_scheduler_get_type ())

#pragma GCC visibility push(default)
G_DECLARE_FINAL_TYPE (LrgScriptScheduler, lrg_script_scheduler, LRG, SCRIPT_SCHEDULER, GObject)
#pragma GCC visibility pop

/**
 * LrgScriptScheduler:
 *
 * Every lrg_script_scheduler_update() advances the scheduler's frame and
 * clock, then resumes the tasks whose wait is over, round robin from
 * where the last frame stopped, until the frame's time budget
 * (#LrgScriptScheduler:frame-budget-us) or instruction budget
 * (#LrgScriptScheduler:instruction-budget) is spent; the rest run on a
 * later frame.  Tasks added during an update first run on the next one.
 * A finished, cancelled or failed task is dropped after the update that
 * ended it, after #LrgScriptScheduler::task-finished.
 *
 * Properties: frame-budget-us, instruction-budget, instruction-slice,
 * watchdog-us (all writable at any time).
 */

/**
 * LrgScriptScheduler::task-finished:
 * @self: the scheduler
 * @task: the task that reached a final state
 *
 * Emitted when the scheduler drops a task (at the end of an update, or
 * at once from lrg_script_scheduler_cancel_all() for tasks that never
 * ran).
 */

/**
 * lrg_script_scheduler_new:
 *
 * Returns: (transfer full): a scheduler with the default budgets
 */
LRG_AVAILABLE_IN_ALL
LrgScriptScheduler *
lrg_script_scheduler_new (void);

/**
 * lrg_script_scheduler_add:
 * @self: an #LrgScriptScheduler
 * @task: a task not in a scheduler yet
 *
 * Schedules @task (the scheduler takes a reference) and gives it an id.
 *
 * Returns: the task's id (never 0)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_script_scheduler_add (LrgScriptScheduler *self,
                          LrgScriptTask      *task);

/**
 * lrg_script_scheduler_update:
 * @self: an #LrgScriptScheduler
 * @delta: seconds since the last update (the scheduler's clock)
 *
 * One frame: see #LrgScriptScheduler.
 *
 * Returns: the resumes this frame
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_script_scheduler_update (LrgScriptScheduler *self,
                             gdouble             delta);

/**
 * lrg_script_scheduler_get_task:
 * @self: an #LrgScriptScheduler
 * @id: a task id
 *
 * Returns: (transfer none) (nullable): the scheduled task, %NULL once it
 *   was dropped
 */
LRG_AVAILABLE_IN_ALL
LrgScriptTask *
lrg_script_scheduler_get_task (LrgScriptScheduler *self,
                               guint               id);

/**
 * lrg_script_scheduler_get_task_count:
 * @self: an #LrgScriptScheduler
 *
 * Returns: tasks scheduled (not dropped yet)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_script_scheduler_get_task_count (LrgScriptScheduler *self);

/**
 * lrg_script_scheduler_get_tasks:
 * @self: an #LrgScriptScheduler
 *
 * Returns: (transfer container) (element-type LrgScriptTask): the
 *   scheduled tasks in the order they were added
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_script_scheduler_get_tasks (LrgScriptScheduler *self);

/**
 * lrg_script_scheduler_cancel:
 * @self: an #LrgScriptScheduler
 * @id: a task id
 *
 * lrg_script_task_cancel() on the task.
 *
 * Returns: %TRUE when the task was scheduled and not done
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_script_scheduler_cancel (LrgScriptScheduler *self,
                             guint               id);

/**
 * lrg_script_scheduler_cancel_owner:
 * @self: an #LrgScriptScheduler
 * @owner: an owner tag
 *
 * Cancels every task of @owner (a mod unloading, the macros on /stop).
 *
 * Returns: the tasks cancelled
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_script_scheduler_cancel_owner (LrgScriptScheduler *self,
                                   const gchar        *owner);

/**
 * lrg_script_scheduler_cancel_all:
 * @self: an #LrgScriptScheduler
 *
 * Cancels every task.
 *
 * Returns: the tasks cancelled
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_script_scheduler_cancel_all (LrgScriptScheduler *self);

/**
 * lrg_script_scheduler_drop_all:
 * @self: an #LrgScriptScheduler
 *
 * Ends every task at once without a grace resume
 * (%LRG_SCRIPT_TASK_STATE_CANCELLED) and drops it: for a context that
 * goes away (a new game, the end of a session).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_script_scheduler_drop_all (LrgScriptScheduler *self);

/**
 * lrg_script_scheduler_get_frame:
 * @self: an #LrgScriptScheduler
 *
 * Returns: updates so far
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_script_scheduler_get_frame (LrgScriptScheduler *self);

/**
 * lrg_script_scheduler_get_time:
 * @self: an #LrgScriptScheduler
 *
 * Returns: the sum of the updates' deltas, seconds
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_script_scheduler_get_time (LrgScriptScheduler *self);

/**
 * lrg_script_scheduler_get_frame_budget_us:
 * @self: an #LrgScriptScheduler
 *
 * Returns: microseconds per frame the tasks may use
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_script_scheduler_get_frame_budget_us (LrgScriptScheduler *self);

/**
 * lrg_script_scheduler_set_frame_budget_us:
 * @self: an #LrgScriptScheduler
 * @budget_us: microseconds per frame (at least 1)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_script_scheduler_set_frame_budget_us (LrgScriptScheduler *self,
                                          gint64              budget_us);

/**
 * lrg_script_scheduler_get_instruction_budget:
 * @self: an #LrgScriptScheduler
 *
 * Returns: instructions per frame counting backends may run
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_script_scheduler_get_instruction_budget (LrgScriptScheduler *self);

/**
 * lrg_script_scheduler_set_instruction_budget:
 * @self: an #LrgScriptScheduler
 * @instructions: instructions per frame (at least one slice)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_script_scheduler_set_instruction_budget (LrgScriptScheduler *self,
                                             guint64             instructions);

/**
 * lrg_script_scheduler_get_instruction_slice:
 * @self: an #LrgScriptScheduler
 *
 * Returns: how often counting backends check the budget
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_script_scheduler_get_instruction_slice (LrgScriptScheduler *self);

/**
 * lrg_script_scheduler_set_instruction_slice:
 * @self: an #LrgScriptScheduler
 * @slice: instructions between two checks (10 .. 1000000)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_script_scheduler_set_instruction_slice (LrgScriptScheduler *self,
                                            guint               slice);

/**
 * lrg_script_scheduler_get_watchdog_us:
 * @self: an #LrgScriptScheduler
 *
 * Returns: the watchdog past the frame budget, microseconds
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_script_scheduler_get_watchdog_us (LrgScriptScheduler *self);

/**
 * lrg_script_scheduler_set_watchdog_us:
 * @self: an #LrgScriptScheduler
 * @watchdog_us: microseconds (0 disables the watchdog)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_script_scheduler_set_watchdog_us (LrgScriptScheduler *self,
                                      gint64              watchdog_us);

/**
 * lrg_script_scheduler_get_last_frame_instructions:
 * @self: an #LrgScriptScheduler
 *
 * Returns: instructions the tasks ran in the last update
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_script_scheduler_get_last_frame_instructions (LrgScriptScheduler *self);

/**
 * lrg_script_scheduler_get_last_frame_time_us:
 * @self: an #LrgScriptScheduler
 *
 * Returns: microseconds the tasks took in the last update
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_script_scheduler_get_last_frame_time_us (LrgScriptScheduler *self);

/**
 * lrg_script_scheduler_get_max_frame_instructions:
 * @self: an #LrgScriptScheduler
 *
 * Returns: the most instructions any one update ran since the scheduler
 *   was made (or lrg_script_scheduler_reset_stats())
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_script_scheduler_get_max_frame_instructions (LrgScriptScheduler *self);

/**
 * lrg_script_scheduler_get_max_frame_time_us:
 * @self: an #LrgScriptScheduler
 *
 * Returns: the longest any one update's tasks took, microseconds
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_script_scheduler_get_max_frame_time_us (LrgScriptScheduler *self);

/**
 * lrg_script_scheduler_reset_stats:
 * @self: an #LrgScriptScheduler
 *
 * Forgets the maxima.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_script_scheduler_reset_stats (LrgScriptScheduler *self);

G_END_DECLS
