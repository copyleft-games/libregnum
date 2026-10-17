/* lrg-script-task.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The base of resumable script code.  The task keeps its life cycle
 * (pending, running, waiting, a final state), its wait and its counters;
 * subclasses only resume the body and say what it waits for.  A wait
 * asked during a resume is recorded as a request and turned into a
 * target (a scheduler time or frame) when the resume ends, so subclasses
 * never need the scheduler's clock.
 */

#include "config.h"

#include "lrg-script-task.h"
#include "lrg-script-task-private.h"
#include "../lrg-log.h"

/* --- the budget record --- */

LrgScriptBudget *
lrg_script_budget_copy (const LrgScriptBudget *budget)
{
    g_return_val_if_fail (budget != NULL, NULL);

    return (LrgScriptBudget *)g_memdup2 (budget, sizeof (LrgScriptBudget));
}

void
lrg_script_budget_free (LrgScriptBudget *budget)
{
    g_free (budget);
}

G_DEFINE_BOXED_TYPE (LrgScriptBudget, lrg_script_budget,
                     lrg_script_budget_copy, lrg_script_budget_free)

/* --- the task --- */

typedef struct
{
    guint                   id;
    gchar                  *name;
    gchar                  *owner;
    LrgScriptTaskState      state;
    GError                 *error;
    gboolean                scheduled;
    gboolean                cancel_requested;
    gboolean                grace;          /* the resume after a cancel request */
    gboolean                released;

    /* the wait the task is in */
    LrgScriptWaitKind       wait_kind;
    gdouble                 wait_until_time;
    guint64                 wait_until_frame;
    LrgScriptTaskCondition  condition;
    gpointer                condition_data;
    GDestroyNotify          condition_destroy;
    gdouble                 condition_deadline;  /* scheduler time, < 0 none */
    gboolean                timed_out;

    /* requested during the current resume */
    gboolean                finish_requested;
    gboolean                wait_requested;
    LrgScriptWaitKind       request_kind;
    gdouble                 request_seconds;
    guint                   request_frames;
    gdouble                 request_timeout;

    /* counters */
    guint                   resumes;
    guint                   preemptions;
    guint64                 instructions;
    guint64                 last_instructions;
    gint64                  last_time_us;
} LrgScriptTaskPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgScriptTask, lrg_script_task, G_TYPE_OBJECT)

enum
{
    SIGNAL_FINISHED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* the task resumed now; the engine runs scripts on one thread */
static LrgScriptTask *current_task = NULL;

static void
clear_condition (LrgScriptTaskPrivate *priv)
{
    if (priv->condition_destroy != NULL && priv->condition_data != NULL)
        priv->condition_destroy (priv->condition_data);
    priv->condition = NULL;
    priv->condition_data = NULL;
    priv->condition_destroy = NULL;
}

/* releases the body once (class release vfunc) */
static void
release_body (LrgScriptTask *self)
{
    LrgScriptTaskPrivate *priv = lrg_script_task_get_instance_private (self);

    if (priv->released)
        return;
    priv->released = TRUE;
    if (LRG_SCRIPT_TASK_GET_CLASS (self)->release != NULL)
        LRG_SCRIPT_TASK_GET_CLASS (self)->release (self);
}

static void
lrg_script_task_dispose (GObject *object)
{
    LrgScriptTask *self = LRG_SCRIPT_TASK (object);
    LrgScriptTaskPrivate *priv = lrg_script_task_get_instance_private (self);

    clear_condition (priv);
    release_body (self);

    G_OBJECT_CLASS (lrg_script_task_parent_class)->dispose (object);
}

static void
lrg_script_task_finalize (GObject *object)
{
    LrgScriptTask *self = LRG_SCRIPT_TASK (object);
    LrgScriptTaskPrivate *priv = lrg_script_task_get_instance_private (self);

    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->owner, g_free);
    g_clear_error (&priv->error);

    G_OBJECT_CLASS (lrg_script_task_parent_class)->finalize (object);
}

/* default resume: an abstract task cannot run */
static gboolean
lrg_script_task_real_resume (LrgScriptTask          *self,
                             const LrgScriptBudget  *budget,
                             GError                **error)
{
    (void)budget;
    g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_UNSUPPORTED,
                 "%s cannot be resumed", G_OBJECT_TYPE_NAME (self));
    return FALSE;
}

static gboolean
lrg_script_task_real_supports_preemption (LrgScriptTask *self)
{
    (void)self;
    return FALSE;
}

static void
lrg_script_task_class_init (LrgScriptTaskClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_script_task_dispose;
    object_class->finalize = lrg_script_task_finalize;
    klass->resume = lrg_script_task_real_resume;
    klass->supports_preemption = lrg_script_task_real_supports_preemption;

    signals[SIGNAL_FINISHED] =
        g_signal_new ("finished",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_script_task_init (LrgScriptTask *self)
{
    LrgScriptTaskPrivate *priv = lrg_script_task_get_instance_private (self);

    priv->state = LRG_SCRIPT_TASK_STATE_PENDING;
    priv->wait_kind = LRG_SCRIPT_WAIT_NONE;
    priv->condition_deadline = -1.0;
}

/* --- identity --- */

guint
lrg_script_task_get_id (LrgScriptTask *self)
{
    LrgScriptTaskPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPT_TASK (self), 0);
    priv = lrg_script_task_get_instance_private (self);
    return priv->id;
}

const gchar *
lrg_script_task_get_name (LrgScriptTask *self)
{
    LrgScriptTaskPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPT_TASK (self), NULL);
    priv = lrg_script_task_get_instance_private (self);
    return priv->name;
}

void
lrg_script_task_set_name (LrgScriptTask *self,
                          const gchar   *name)
{
    LrgScriptTaskPrivate *priv;

    g_return_if_fail (LRG_IS_SCRIPT_TASK (self));
    priv = lrg_script_task_get_instance_private (self);
    g_free (priv->name);
    priv->name = g_strdup (name);
}

const gchar *
lrg_script_task_get_owner (LrgScriptTask *self)
{
    LrgScriptTaskPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPT_TASK (self), NULL);
    priv = lrg_script_task_get_instance_private (self);
    return priv->owner;
}

void
lrg_script_task_set_owner (LrgScriptTask *self,
                           const gchar   *owner)
{
    LrgScriptTaskPrivate *priv;

    g_return_if_fail (LRG_IS_SCRIPT_TASK (self));
    priv = lrg_script_task_get_instance_private (self);
    g_free (priv->owner);
    priv->owner = g_strdup (owner);
}

/* --- state --- */

LrgScriptTaskState
lrg_script_task_get_state (LrgScriptTask *self)
{
    LrgScriptTaskPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPT_TASK (self), LRG_SCRIPT_TASK_STATE_FAILED);
    priv = lrg_script_task_get_instance_private (self);
    return priv->state;
}

gboolean
lrg_script_task_is_done (LrgScriptTask *self)
{
    LrgScriptTaskState state;

    g_return_val_if_fail (LRG_IS_SCRIPT_TASK (self), TRUE);
    state = lrg_script_task_get_state (self);
    return state == LRG_SCRIPT_TASK_STATE_FINISHED ||
           state == LRG_SCRIPT_TASK_STATE_CANCELLED ||
           state == LRG_SCRIPT_TASK_STATE_FAILED;
}

const GError *
lrg_script_task_get_error (LrgScriptTask *self)
{
    LrgScriptTaskPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPT_TASK (self), NULL);
    priv = lrg_script_task_get_instance_private (self);
    return priv->error;
}

LrgScriptWaitKind
lrg_script_task_get_wait_kind (LrgScriptTask *self)
{
    LrgScriptTaskPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPT_TASK (self), LRG_SCRIPT_WAIT_NONE);
    priv = lrg_script_task_get_instance_private (self);
    return priv->wait_kind;
}

gboolean
lrg_script_task_supports_preemption (LrgScriptTask *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPT_TASK (self), FALSE);
    return LRG_SCRIPT_TASK_GET_CLASS (self)->supports_preemption != NULL &&
           LRG_SCRIPT_TASK_GET_CLASS (self)->supports_preemption (self);
}

/* --- cancellation --- */

void
lrg_script_task_cancel (LrgScriptTask *self)
{
    LrgScriptTaskPrivate *priv;

    g_return_if_fail (LRG_IS_SCRIPT_TASK (self));
    priv = lrg_script_task_get_instance_private (self);
    if (lrg_script_task_is_done (self))
        return;
    priv->cancel_requested = TRUE;
    /* outside a scheduler, or never started: nothing to wind down */
    if (!priv->scheduled ||
        (priv->state == LRG_SCRIPT_TASK_STATE_PENDING && priv->resumes == 0))
        lrg_script_task_end_private (self, LRG_SCRIPT_TASK_STATE_CANCELLED, NULL);
}

gboolean
lrg_script_task_is_cancel_requested (LrgScriptTask *self)
{
    LrgScriptTaskPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPT_TASK (self), FALSE);
    priv = lrg_script_task_get_instance_private (self);
    return priv->cancel_requested;
}

/* --- requests during a resume --- */

void
lrg_script_task_wait_seconds (LrgScriptTask *self,
                              gdouble        seconds)
{
    LrgScriptTaskPrivate *priv;

    g_return_if_fail (LRG_IS_SCRIPT_TASK (self));
    priv = lrg_script_task_get_instance_private (self);
    priv->wait_requested = TRUE;
    priv->request_kind = seconds > 0.0 ? LRG_SCRIPT_WAIT_SECONDS : LRG_SCRIPT_WAIT_NONE;
    priv->request_seconds = seconds > 0.0 ? seconds : 0.0;
}

void
lrg_script_task_wait_frames (LrgScriptTask *self,
                             guint          frames)
{
    LrgScriptTaskPrivate *priv;

    g_return_if_fail (LRG_IS_SCRIPT_TASK (self));
    priv = lrg_script_task_get_instance_private (self);
    priv->wait_requested = TRUE;
    priv->request_kind = frames > 1 ? LRG_SCRIPT_WAIT_FRAMES : LRG_SCRIPT_WAIT_NONE;
    priv->request_frames = frames;
}

void
lrg_script_task_wait_condition (LrgScriptTask          *self,
                                LrgScriptTaskCondition  condition,
                                gpointer                user_data,
                                GDestroyNotify          destroy,
                                gdouble                 timeout)
{
    LrgScriptTaskPrivate *priv;

    g_return_if_fail (LRG_IS_SCRIPT_TASK (self));
    g_return_if_fail (condition != NULL);
    priv = lrg_script_task_get_instance_private (self);
    clear_condition (priv);
    priv->condition = condition;
    priv->condition_data = user_data;
    priv->condition_destroy = destroy;
    priv->wait_requested = TRUE;
    priv->request_kind = LRG_SCRIPT_WAIT_CONDITION;
    priv->request_timeout = timeout;
}

gboolean
lrg_script_task_get_wait_timed_out (LrgScriptTask *self)
{
    LrgScriptTaskPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPT_TASK (self), FALSE);
    priv = lrg_script_task_get_instance_private (self);
    return priv->timed_out;
}

void
lrg_script_task_finish (LrgScriptTask *self)
{
    LrgScriptTaskPrivate *priv;

    g_return_if_fail (LRG_IS_SCRIPT_TASK (self));
    priv = lrg_script_task_get_instance_private (self);
    priv->finish_requested = TRUE;
}

void
lrg_script_task_note_preempted (LrgScriptTask *self)
{
    LrgScriptTaskPrivate *priv;

    g_return_if_fail (LRG_IS_SCRIPT_TASK (self));
    priv = lrg_script_task_get_instance_private (self);
    priv->preemptions++;
    priv->wait_requested = TRUE;
    priv->request_kind = LRG_SCRIPT_WAIT_NONE;
}

void
lrg_script_task_add_instructions (LrgScriptTask *self,
                                  guint64        instructions)
{
    LrgScriptTaskPrivate *priv;

    g_return_if_fail (LRG_IS_SCRIPT_TASK (self));
    priv = lrg_script_task_get_instance_private (self);
    priv->instructions += instructions;
    priv->last_instructions += instructions;
}

/* --- statistics --- */

guint
lrg_script_task_get_resume_count (LrgScriptTask *self)
{
    LrgScriptTaskPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPT_TASK (self), 0);
    priv = lrg_script_task_get_instance_private (self);
    return priv->resumes;
}

guint
lrg_script_task_get_preemption_count (LrgScriptTask *self)
{
    LrgScriptTaskPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPT_TASK (self), 0);
    priv = lrg_script_task_get_instance_private (self);
    return priv->preemptions;
}

guint64
lrg_script_task_get_instructions (LrgScriptTask *self)
{
    LrgScriptTaskPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPT_TASK (self), 0);
    priv = lrg_script_task_get_instance_private (self);
    return priv->instructions;
}

guint64
lrg_script_task_get_last_instructions (LrgScriptTask *self)
{
    LrgScriptTaskPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPT_TASK (self), 0);
    priv = lrg_script_task_get_instance_private (self);
    return priv->last_instructions;
}

gint64
lrg_script_task_get_last_time_us (LrgScriptTask *self)
{
    LrgScriptTaskPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCRIPT_TASK (self), 0);
    priv = lrg_script_task_get_instance_private (self);
    return priv->last_time_us;
}

LrgScriptTask *
lrg_script_task_get_current (void)
{
    return current_task;
}

/* --- the scheduler's side --- */

void
lrg_script_task_set_id_private (LrgScriptTask *self,
                                guint          id)
{
    LrgScriptTaskPrivate *priv = lrg_script_task_get_instance_private (self);

    priv->id = id;
}

void
lrg_script_task_set_scheduled_private (LrgScriptTask *self,
                                       gboolean       scheduled)
{
    LrgScriptTaskPrivate *priv = lrg_script_task_get_instance_private (self);

    priv->scheduled = scheduled;
}

gboolean
lrg_script_task_is_ready_private (LrgScriptTask *self,
                                  guint64        frame,
                                  gdouble        time)
{
    LrgScriptTaskPrivate *priv = lrg_script_task_get_instance_private (self);

    if (lrg_script_task_is_done (self))
        return FALSE;
    /* a cancel request cuts the wait short: the grace resume */
    if (priv->cancel_requested || priv->state == LRG_SCRIPT_TASK_STATE_PENDING)
        return TRUE;
    switch (priv->wait_kind)
    {
    case LRG_SCRIPT_WAIT_SECONDS:
        return time >= priv->wait_until_time;
    case LRG_SCRIPT_WAIT_FRAMES:
        return frame >= priv->wait_until_frame;
    case LRG_SCRIPT_WAIT_CONDITION:
        if (priv->condition != NULL && priv->condition (self, priv->condition_data))
        {
            priv->timed_out = FALSE;
            return TRUE;
        }
        if (priv->condition_deadline >= 0.0 && time >= priv->condition_deadline)
        {
            priv->timed_out = TRUE;
            return TRUE;
        }
        return FALSE;
    case LRG_SCRIPT_WAIT_NONE:
    default:
        return TRUE;
    }
}

void
lrg_script_task_end_private (LrgScriptTask      *self,
                             LrgScriptTaskState  state,
                             GError             *error)
{
    LrgScriptTaskPrivate *priv = lrg_script_task_get_instance_private (self);

    if (lrg_script_task_is_done (self))
    {
        if (error != NULL)
            g_error_free (error);
        return;
    }
    priv->state = state;
    priv->wait_kind = LRG_SCRIPT_WAIT_NONE;
    if (error != NULL)
    {
        g_clear_error (&priv->error);
        priv->error = error;
    }
    clear_condition (priv);
    release_body (self);
    g_signal_emit (self, signals[SIGNAL_FINISHED], 0);
}

gint64
lrg_script_task_run_private (LrgScriptTask         *self,
                             const LrgScriptBudget *budget,
                             guint64                frame,
                             gdouble                time)
{
    LrgScriptTaskPrivate *priv = lrg_script_task_get_instance_private (self);
    g_autoptr(GError) error = NULL;
    LrgScriptTask *previous;
    gboolean ok;
    gint64 started;
    gint64 ended;

    /* --- a cancel request: the grace resume (the wait ends now) --- */
    priv->grace = priv->cancel_requested;
    if (priv->wait_kind == LRG_SCRIPT_WAIT_CONDITION)
        clear_condition (priv);
    priv->wait_kind = LRG_SCRIPT_WAIT_NONE;
    priv->finish_requested = FALSE;
    priv->wait_requested = FALSE;
    priv->last_instructions = 0;
    priv->state = LRG_SCRIPT_TASK_STATE_RUNNING;
    priv->resumes++;

    /* --- resume the body (a task may resume another one) --- */
    g_object_ref (self);
    previous = current_task;
    current_task = self;
    started = g_get_monotonic_time ();
    ok = LRG_SCRIPT_TASK_GET_CLASS (self)->resume (self, budget, &error);
    ended = g_get_monotonic_time ();
    current_task = previous;
    priv->last_time_us = ended - started;

    /* --- settle the state --- */
    if (lrg_script_task_is_done (self))
    {
        /* ended from inside the resume (a nested cancel) */
    }
    else if (!ok)
    {
        if (error == NULL)
            g_set_error (&error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_RUNTIME,
                         "the task failed without a reason");
        lrg_info (LRG_LOG_DOMAIN_SCRIPTING, "task %u (%s) failed: %s",
                     priv->id, priv->name != NULL ? priv->name : "unnamed",
                     error->message);
        lrg_script_task_end_private (self, priv->grace ? LRG_SCRIPT_TASK_STATE_CANCELLED
                                                       : LRG_SCRIPT_TASK_STATE_FAILED,
                                     g_steal_pointer (&error));
    }
    else if (priv->grace)
    {
        lrg_script_task_end_private (self, LRG_SCRIPT_TASK_STATE_CANCELLED, NULL);
    }
    else if (priv->finish_requested)
    {
        lrg_script_task_end_private (self, LRG_SCRIPT_TASK_STATE_FINISHED, NULL);
    }
    else if (!lrg_script_task_supports_preemption (self) && budget != NULL &&
             budget->watchdog_us > 0 && ended > budget->deadline_us + budget->watchdog_us)
    {
        /* a backend that cannot be cut short held the frame too long:
         * the work is done, but the task must not do it again */
        lrg_script_task_end_private (self, LRG_SCRIPT_TASK_STATE_FAILED,
            g_error_new (LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_BUDGET,
                         "task %u ran %" G_GINT64_FORMAT " us in one resume without a wait",
                         priv->id, priv->last_time_us));
    }
    else
    {
        LrgScriptWaitKind kind;

        kind = priv->wait_requested ? priv->request_kind : LRG_SCRIPT_WAIT_NONE;
        priv->wait_kind = kind;
        priv->timed_out = FALSE;
        if (kind == LRG_SCRIPT_WAIT_SECONDS)
            priv->wait_until_time = time + priv->request_seconds;
        else if (kind == LRG_SCRIPT_WAIT_FRAMES)
            priv->wait_until_frame = frame + (guint64)priv->request_frames;
        else if (kind == LRG_SCRIPT_WAIT_CONDITION)
            priv->condition_deadline = priv->request_timeout > 0.0 ? time + priv->request_timeout : -1.0;
        priv->state = LRG_SCRIPT_TASK_STATE_WAITING;
    }
    /* the last reference may go with the unref: read first */
    ended = priv->last_time_us;
    g_object_unref (self);
    return ended;
}
