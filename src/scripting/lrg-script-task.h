/* lrg-script-task.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScriptTask - one piece of script code that runs over many frames:
 * a coroutine (LrgScriptTaskLua), a step function called once a frame
 * (LrgScriptTaskCallback), or any backend's own kind.  Tasks are run by
 * an #LrgScriptScheduler on a per-frame budget; they suspend on waits
 * (seconds, frames, a condition, the next frame), can be cancelled, and
 * report how much of the budget they used.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * LrgScriptBudget:
 * @deadline_us: the g_get_monotonic_time() at which the resume should
 *   suspend (the end of the frame's time budget)
 * @instructions: the instructions the resume may run before it should
 *   suspend (backends that count instructions; 0 means none left)
 * @slice: how often a counting backend checks the budget, in
 *   instructions
 * @watchdog_us: how far past @deadline_us a resume that cannot suspend
 *   may run before the backend raises %LRG_SCRIPTING_ERROR_BUDGET
 *
 * What one resume of an #LrgScriptTask may spend.  The scheduler fills
 * it for every resume from what is left of the frame's budget.
 */
typedef struct _LrgScriptBudget LrgScriptBudget;

struct _LrgScriptBudget
{
    gint64   deadline_us;
    guint64  instructions;
    guint    slice;
    gint64   watchdog_us;
};

#define LRG_TYPE_SCRIPT_BUDGET (lrg_script_budget_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_script_budget_get_type (void) G_GNUC_CONST;

/**
 * lrg_script_budget_copy:
 * @budget: an #LrgScriptBudget
 *
 * Returns: (transfer full): a copy of @budget
 */
LRG_AVAILABLE_IN_ALL
LrgScriptBudget *
lrg_script_budget_copy (const LrgScriptBudget *budget);

/**
 * lrg_script_budget_free:
 * @budget: (nullable): an #LrgScriptBudget
 */
LRG_AVAILABLE_IN_ALL
void
lrg_script_budget_free (LrgScriptBudget *budget);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgScriptBudget, lrg_script_budget_free)

#define LRG_TYPE_SCRIPT_TASK (lrg_script_task_get_type ())

#pragma GCC visibility push(default)
G_DECLARE_DERIVABLE_TYPE (LrgScriptTask, lrg_script_task, LRG, SCRIPT_TASK, GObject)
#pragma GCC visibility pop

/**
 * LrgScriptTaskCondition:
 * @task: the waiting task
 * @user_data: (closure): data given to lrg_script_task_wait_condition()
 *
 * Decides whether a task's wait is over.  The scheduler calls it once a
 * frame before the task would run.
 *
 * Returns: %TRUE when the task may run again
 */
typedef gboolean (*LrgScriptTaskCondition) (LrgScriptTask *task,
                                            gpointer       user_data);

/**
 * LrgScriptTaskClass:
 * @parent_class: the parent class
 * @resume: runs the body until it suspends, finishes or fails; the
 *   subclass calls lrg_script_task_wait_seconds() and friends, or
 *   lrg_script_task_finish(), before it returns (returning with neither
 *   runs the task again on the next frame); returns %FALSE with @error
 *   set when the body failed
 * @release: drops the body (a coroutine, a function reference) once the
 *   task reached a final state; called once
 * @supports_preemption: %TRUE when the backend suspends a resume by
 *   itself when the budget runs out (a counting hook), so a busy loop
 *   cannot hold the frame; tasks without it are bounded only by their
 *   own waits and fail when one resume runs past the watchdog
 *
 * The extension point of script backends: one subclass per kind of
 * resumable code.
 */
struct _LrgScriptTaskClass
{
    GObjectClass parent_class;

    /*< public >*/
    gboolean (*resume)              (LrgScriptTask          *self,
                                     const LrgScriptBudget  *budget,
                                     GError                **error);
    void     (*release)             (LrgScriptTask          *self);
    gboolean (*supports_preemption) (LrgScriptTask          *self);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * LrgScriptTask::finished:
 * @self: the task
 *
 * Emitted once, when the task reached a final state
 * (%LRG_SCRIPT_TASK_STATE_FINISHED, %LRG_SCRIPT_TASK_STATE_CANCELLED or
 * %LRG_SCRIPT_TASK_STATE_FAILED).
 */

/* --- identity --- */

/**
 * lrg_script_task_get_id:
 * @self: an #LrgScriptTask
 *
 * Returns: the id the scheduler gave the task, 0 before it was added
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_script_task_get_id (LrgScriptTask *self);

/**
 * lrg_script_task_get_name:
 * @self: an #LrgScriptTask
 *
 * Returns: (transfer none) (nullable): the task's name, for logs
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_script_task_get_name (LrgScriptTask *self);

/**
 * lrg_script_task_set_name:
 * @self: an #LrgScriptTask
 * @name: (nullable): a name for logs
 */
LRG_AVAILABLE_IN_ALL
void
lrg_script_task_set_name (LrgScriptTask *self,
                          const gchar   *name);

/**
 * lrg_script_task_get_owner:
 * @self: an #LrgScriptTask
 *
 * Returns: (transfer none) (nullable): the owner tag
 *   (lrg_script_scheduler_cancel_owner())
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_script_task_get_owner (LrgScriptTask *self);

/**
 * lrg_script_task_set_owner:
 * @self: an #LrgScriptTask
 * @owner: (nullable): an owner tag, e.g. a mod id
 */
LRG_AVAILABLE_IN_ALL
void
lrg_script_task_set_owner (LrgScriptTask *self,
                           const gchar   *owner);

/* --- state --- */

/**
 * lrg_script_task_get_state:
 * @self: an #LrgScriptTask
 *
 * Returns: where the task is in its life
 */
LRG_AVAILABLE_IN_ALL
LrgScriptTaskState
lrg_script_task_get_state (LrgScriptTask *self);

/**
 * lrg_script_task_is_done:
 * @self: an #LrgScriptTask
 *
 * Returns: %TRUE in a final state (finished, cancelled, failed)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_script_task_is_done (LrgScriptTask *self);

/**
 * lrg_script_task_get_error:
 * @self: an #LrgScriptTask
 *
 * Returns: (transfer none) (nullable): why the task failed
 */
LRG_AVAILABLE_IN_ALL
const GError *
lrg_script_task_get_error (LrgScriptTask *self);

/**
 * lrg_script_task_get_wait_kind:
 * @self: an #LrgScriptTask
 *
 * Returns: what the task waits for while it is suspended
 */
LRG_AVAILABLE_IN_ALL
LrgScriptWaitKind
lrg_script_task_get_wait_kind (LrgScriptTask *self);

/**
 * lrg_script_task_supports_preemption:
 * @self: an #LrgScriptTask
 *
 * Returns: whether the backend suspends a resume by itself when the
 *   budget runs out (#LrgScriptTaskClass.supports_preemption)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_script_task_supports_preemption (LrgScriptTask *self);

/* --- cancellation --- */

/**
 * lrg_script_task_cancel:
 * @self: an #LrgScriptTask
 *
 * Asks the task to stop.  A task that never ran, or one outside a
 * scheduler, is cancelled at once.  A started task runs once more on its
 * scheduler's next update with lrg_script_task_is_cancel_requested()
 * %TRUE and its wait cut short, so a loop such as
 * `while not cancelled() do ... end` can end cleanly; whatever it does
 * then, it ends %LRG_SCRIPT_TASK_STATE_CANCELLED after that resume.
 * Cancelling a task in a final state does nothing.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_script_task_cancel (LrgScriptTask *self);

/**
 * lrg_script_task_is_cancel_requested:
 * @self: an #LrgScriptTask
 *
 * Returns: %TRUE once lrg_script_task_cancel() was called
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_script_task_is_cancel_requested (LrgScriptTask *self);

/* --- for subclasses, during resume --- */

/**
 * lrg_script_task_wait_seconds:
 * @self: an #LrgScriptTask
 * @seconds: scheduler seconds to wait (0 or less: the next frame)
 *
 * Suspends the task after this resume until @seconds of scheduler time
 * passed.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_script_task_wait_seconds (LrgScriptTask *self,
                              gdouble        seconds);

/**
 * lrg_script_task_wait_frames:
 * @self: an #LrgScriptTask
 * @frames: scheduler frames to wait (0: the next frame)
 *
 * Suspends the task after this resume for @frames frames (1 runs it on
 * the next frame, 2 skips one frame).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_script_task_wait_frames (LrgScriptTask *self,
                             guint          frames);

/**
 * lrg_script_task_wait_condition:
 * @self: an #LrgScriptTask
 * @condition: (scope notified) (closure user_data) (destroy destroy):
 *   decides when the wait is over
 * @user_data: data for @condition
 * @destroy: (nullable): frees @user_data when the wait ends
 * @timeout: seconds after which the wait ends anyway (0 or less: never)
 *
 * Suspends the task after this resume until @condition returns %TRUE
 * or @timeout passes (lrg_script_task_get_wait_timed_out()).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_script_task_wait_condition (LrgScriptTask          *self,
                                LrgScriptTaskCondition  condition,
                                gpointer                user_data,
                                GDestroyNotify          destroy,
                                gdouble                 timeout);

/**
 * lrg_script_task_get_wait_timed_out:
 * @self: an #LrgScriptTask
 *
 * Returns: %TRUE when the last condition wait ended by its timeout
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_script_task_get_wait_timed_out (LrgScriptTask *self);

/**
 * lrg_script_task_finish:
 * @self: an #LrgScriptTask
 *
 * Marks the body as done: after this resume the task is
 * %LRG_SCRIPT_TASK_STATE_FINISHED.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_script_task_finish (LrgScriptTask *self);

/**
 * lrg_script_task_note_preempted:
 * @self: an #LrgScriptTask
 *
 * The backend suspended this resume because the budget ran out (not a
 * wait of the body): the task runs again on the next frame, and the
 * preemption is counted.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_script_task_note_preempted (LrgScriptTask *self);

/**
 * lrg_script_task_add_instructions:
 * @self: an #LrgScriptTask
 * @instructions: instructions run by this resume
 *
 * Counting backends report what a resume ran; the scheduler takes it
 * off the frame's instruction budget.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_script_task_add_instructions (LrgScriptTask *self,
                                  guint64        instructions);

/* --- statistics --- */

/**
 * lrg_script_task_get_resume_count:
 * @self: an #LrgScriptTask
 *
 * Returns: how many times the task was resumed
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_script_task_get_resume_count (LrgScriptTask *self);

/**
 * lrg_script_task_get_preemption_count:
 * @self: an #LrgScriptTask
 *
 * Returns: how many resumes the budget cut short
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_script_task_get_preemption_count (LrgScriptTask *self);

/**
 * lrg_script_task_get_instructions:
 * @self: an #LrgScriptTask
 *
 * Returns: instructions counted over the task's life
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_script_task_get_instructions (LrgScriptTask *self);

/**
 * lrg_script_task_get_last_instructions:
 * @self: an #LrgScriptTask
 *
 * Returns: instructions counted by the last resume
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_script_task_get_last_instructions (LrgScriptTask *self);

/**
 * lrg_script_task_get_last_time_us:
 * @self: an #LrgScriptTask
 *
 * Returns: microseconds the last resume took
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_script_task_get_last_time_us (LrgScriptTask *self);

/**
 * lrg_script_task_get_current:
 *
 * The task being resumed right now on this thread, for the primitives a
 * backend exposes to scripts (a wait must know whose wait it is).
 *
 * Returns: (transfer none) (nullable): the running task
 */
LRG_AVAILABLE_IN_ALL
LrgScriptTask *
lrg_script_task_get_current (void);

G_END_DECLS
