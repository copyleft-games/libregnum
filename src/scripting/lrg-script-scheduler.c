/* lrg-script-scheduler.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The per-frame runner of script tasks.  Every update:
 *
 *   1. the frame counter and the clock advance;
 *   2. tasks added during the last update join the list;
 *   3. starting at the task after the last one resumed (round robin, so
 *      a task that spends the whole budget does not starve the others),
 *      every ready task is resumed with what is left of the frame's time
 *      and instruction budgets, until one of them runs out;
 *   4. tasks in a final state are dropped, each after task-finished.
 *
 * A task holds the frame only as long as its backend lets it: counting
 * backends (Lua) suspend themselves when the budget runs out; the others
 * are bounded by their own waits and by the watchdog.
 */

#include "config.h"

#include "lrg-script-scheduler.h"
#include "lrg-script-task-private.h"
#include "../lrg-log.h"

struct _LrgScriptScheduler
{
    GObject     parent_instance;

    GPtrArray  *tasks;        /* LrgScriptTask, owned, in the order added */
    GPtrArray  *incoming;     /* added during an update */
    gboolean    updating;
    guint       next_id;
    guint       cursor;       /* index after the last task resumed */
    guint64     frame;
    gdouble     time;

    gint64      frame_budget_us;
    guint64     instruction_budget;
    guint       slice;
    gint64      watchdog_us;

    guint64     last_instructions;
    gint64      last_time_us;
    guint64     max_instructions;
    gint64      max_time_us;
};

G_DEFINE_FINAL_TYPE (LrgScriptScheduler, lrg_script_scheduler, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_FRAME_BUDGET_US,
    PROP_INSTRUCTION_BUDGET,
    PROP_INSTRUCTION_SLICE,
    PROP_WATCHDOG_US,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_TASK_FINISHED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_script_scheduler_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    LrgScriptScheduler *self = LRG_SCRIPT_SCHEDULER (object);

    switch (prop_id)
    {
    case PROP_FRAME_BUDGET_US:
        g_value_set_int64 (value, self->frame_budget_us);
        break;
    case PROP_INSTRUCTION_BUDGET:
        g_value_set_uint64 (value, self->instruction_budget);
        break;
    case PROP_INSTRUCTION_SLICE:
        g_value_set_uint (value, self->slice);
        break;
    case PROP_WATCHDOG_US:
        g_value_set_int64 (value, self->watchdog_us);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_script_scheduler_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    LrgScriptScheduler *self = LRG_SCRIPT_SCHEDULER (object);

    switch (prop_id)
    {
    case PROP_FRAME_BUDGET_US:
        self->frame_budget_us = g_value_get_int64 (value);
        break;
    case PROP_INSTRUCTION_BUDGET:
        self->instruction_budget = g_value_get_uint64 (value);
        break;
    case PROP_INSTRUCTION_SLICE:
        self->slice = g_value_get_uint (value);
        break;
    case PROP_WATCHDOG_US:
        self->watchdog_us = g_value_get_int64 (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_script_scheduler_dispose (GObject *object)
{
    LrgScriptScheduler *self = LRG_SCRIPT_SCHEDULER (object);

    if (self->tasks != NULL)
        lrg_script_scheduler_drop_all (self);

    G_OBJECT_CLASS (lrg_script_scheduler_parent_class)->dispose (object);
}

static void
lrg_script_scheduler_finalize (GObject *object)
{
    LrgScriptScheduler *self = LRG_SCRIPT_SCHEDULER (object);

    g_clear_pointer (&self->tasks, g_ptr_array_unref);
    g_clear_pointer (&self->incoming, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_script_scheduler_parent_class)->finalize (object);
}

static void
lrg_script_scheduler_class_init (LrgScriptSchedulerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_script_scheduler_get_property;
    object_class->set_property = lrg_script_scheduler_set_property;
    object_class->dispose = lrg_script_scheduler_dispose;
    object_class->finalize = lrg_script_scheduler_finalize;

    /**
     * LrgScriptScheduler:frame-budget-us:
     *
     * Microseconds of every update the tasks may use.
     */
    properties[PROP_FRAME_BUDGET_US] =
        g_param_spec_int64 ("frame-budget-us", "Frame budget",
                            "Microseconds per frame for the tasks",
                            1, G_MAXINT64, LRG_SCRIPT_SCHEDULER_DEFAULT_FRAME_BUDGET_US,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgScriptScheduler:instruction-budget:
     *
     * Instructions per update counting backends may run.
     */
    properties[PROP_INSTRUCTION_BUDGET] =
        g_param_spec_uint64 ("instruction-budget", "Instruction budget",
                             "Instructions per frame for counting backends",
                             1, G_MAXUINT64, LRG_SCRIPT_SCHEDULER_DEFAULT_INSTRUCTION_BUDGET,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgScriptScheduler:instruction-slice:
     *
     * Instructions between two budget checks of a counting backend.
     */
    properties[PROP_INSTRUCTION_SLICE] =
        g_param_spec_uint ("instruction-slice", "Instruction slice",
                           "Instructions between two budget checks",
                           10, 1000000, LRG_SCRIPT_SCHEDULER_DEFAULT_SLICE,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgScriptScheduler:watchdog-us:
     *
     * How far past the frame budget one resume may run before its task
     * fails (0: never).
     */
    properties[PROP_WATCHDOG_US] =
        g_param_spec_int64 ("watchdog-us", "Watchdog",
                            "Microseconds past the budget before a task fails",
                            0, G_MAXINT64, LRG_SCRIPT_SCHEDULER_DEFAULT_WATCHDOG_US,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    signals[SIGNAL_TASK_FINISHED] =
        g_signal_new ("task-finished",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_SCRIPT_TASK);
}

static void
lrg_script_scheduler_init (LrgScriptScheduler *self)
{
    self->tasks = g_ptr_array_new_with_free_func (g_object_unref);
    self->incoming = g_ptr_array_new_with_free_func (g_object_unref);
    self->next_id = 1;
    self->frame_budget_us = LRG_SCRIPT_SCHEDULER_DEFAULT_FRAME_BUDGET_US;
    self->instruction_budget = LRG_SCRIPT_SCHEDULER_DEFAULT_INSTRUCTION_BUDGET;
    self->slice = LRG_SCRIPT_SCHEDULER_DEFAULT_SLICE;
    self->watchdog_us = LRG_SCRIPT_SCHEDULER_DEFAULT_WATCHDOG_US;
}

LrgScriptScheduler *
lrg_script_scheduler_new (void)
{
    return g_object_new (LRG_TYPE_SCRIPT_SCHEDULER, NULL);
}

static gboolean
array_has (GPtrArray *array,
           gpointer   task)
{
    guint i;

    for (i = 0; i < array->len; i++)
    {
        if (g_ptr_array_index (array, i) == task)
            return TRUE;
    }
    return FALSE;
}

guint
lrg_script_scheduler_add (LrgScriptScheduler *self,
                          LrgScriptTask      *task)
{
    guint id;

    g_return_val_if_fail (LRG_IS_SCRIPT_SCHEDULER (self), 0);
    g_return_val_if_fail (LRG_IS_SCRIPT_TASK (task), 0);
    g_return_val_if_fail (!array_has (self->tasks, task) && !array_has (self->incoming, task), 0);

    id = self->next_id++;
    if (self->next_id == 0)
        self->next_id = 1;
    lrg_script_task_set_id_private (task, id);
    lrg_script_task_set_scheduled_private (task, TRUE);
    /* a task added from inside a resume first runs on the next frame */
    g_ptr_array_add (self->updating ? self->incoming : self->tasks, g_object_ref (task));
    return id;
}

/* drops the tasks in a final state, each after task-finished */
static void
drop_done (LrgScriptScheduler *self)
{
    guint i;

    i = 0;
    while (i < self->tasks->len)
    {
        LrgScriptTask *task;

        task = g_ptr_array_index (self->tasks, i);
        if (!lrg_script_task_is_done (task))
        {
            i++;
            continue;
        }
        g_object_ref (task);
        g_ptr_array_remove_index (self->tasks, i);
        if (self->cursor > i)
            self->cursor--;
        lrg_script_task_set_scheduled_private (task, FALSE);
        g_signal_emit (self, signals[SIGNAL_TASK_FINISHED], 0, task);
        g_object_unref (task);
    }
    if (self->cursor >= self->tasks->len)
        self->cursor = 0;
}

guint
lrg_script_scheduler_update (LrgScriptScheduler *self,
                             gdouble             delta)
{
    LrgScriptBudget budget;
    guint64 instructions_left;
    gint64 started;
    gint64 deadline;
    guint resumes;
    guint count;
    guint start;
    guint k;

    g_return_val_if_fail (LRG_IS_SCRIPT_SCHEDULER (self), 0);

    /* --- 1. the frame and the clock --- */
    self->frame++;
    if (delta > 0.0)
        self->time += delta;

    /* --- 2. tasks added since the last update --- */
    while (self->incoming->len > 0)
        g_ptr_array_add (self->tasks, g_ptr_array_steal_index (self->incoming, 0));

    /* --- 3. resume the ready tasks, round robin, within the budgets --- */
    started = g_get_monotonic_time ();
    deadline = started + self->frame_budget_us;
    instructions_left = self->instruction_budget;
    resumes = 0;
    self->last_instructions = 0;
    count = self->tasks->len;
    start = self->cursor;
    self->updating = TRUE;
    for (k = 0; k < count && k < self->tasks->len; k++)
    {
        LrgScriptTask *task;
        guint index;
        guint64 used;

        index = (start + k) % self->tasks->len;
        task = g_ptr_array_index (self->tasks, index);
        if (!lrg_script_task_is_ready_private (task, self->frame, self->time))
            continue;
        /* the budget is spent: the rest run first on the next frame */
        if (g_get_monotonic_time () >= deadline || instructions_left == 0)
        {
            self->cursor = index;
            break;
        }
        budget.deadline_us = deadline;
        budget.instructions = instructions_left;
        budget.slice = self->slice;
        budget.watchdog_us = self->watchdog_us;
        g_object_ref (task);
        lrg_script_task_run_private (task, &budget, self->frame, self->time);
        used = lrg_script_task_get_last_instructions (task);
        g_object_unref (task);
        instructions_left = used >= instructions_left ? 0 : instructions_left - used;
        self->last_instructions += used;
        resumes++;
        self->cursor = (index + 1) % MAX (self->tasks->len, 1);
    }
    self->updating = FALSE;
    self->last_time_us = g_get_monotonic_time () - started;
    self->max_instructions = MAX (self->max_instructions, self->last_instructions);
    self->max_time_us = MAX (self->max_time_us, self->last_time_us);

    /* --- 4. drop what ended --- */
    drop_done (self);
    return resumes;
}

LrgScriptTask *
lrg_script_scheduler_get_task (LrgScriptScheduler *self,
                               guint               id)
{
    guint i;

    g_return_val_if_fail (LRG_IS_SCRIPT_SCHEDULER (self), NULL);

    for (i = 0; i < self->tasks->len; i++)
    {
        if (lrg_script_task_get_id (g_ptr_array_index (self->tasks, i)) == id)
            return g_ptr_array_index (self->tasks, i);
    }
    for (i = 0; i < self->incoming->len; i++)
    {
        if (lrg_script_task_get_id (g_ptr_array_index (self->incoming, i)) == id)
            return g_ptr_array_index (self->incoming, i);
    }
    return NULL;
}

guint
lrg_script_scheduler_get_task_count (LrgScriptScheduler *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPT_SCHEDULER (self), 0);
    return self->tasks->len + self->incoming->len;
}

GPtrArray *
lrg_script_scheduler_get_tasks (LrgScriptScheduler *self)
{
    GPtrArray *out;
    guint i;

    g_return_val_if_fail (LRG_IS_SCRIPT_SCHEDULER (self), NULL);

    out = g_ptr_array_new ();
    for (i = 0; i < self->tasks->len; i++)
        g_ptr_array_add (out, g_ptr_array_index (self->tasks, i));
    for (i = 0; i < self->incoming->len; i++)
        g_ptr_array_add (out, g_ptr_array_index (self->incoming, i));
    return out;
}

gboolean
lrg_script_scheduler_cancel (LrgScriptScheduler *self,
                             guint               id)
{
    LrgScriptTask *task;

    g_return_val_if_fail (LRG_IS_SCRIPT_SCHEDULER (self), FALSE);

    task = lrg_script_scheduler_get_task (self, id);
    if (task == NULL || lrg_script_task_is_done (task))
        return FALSE;
    lrg_script_task_cancel (task);
    /* a task that never ran ended at once */
    if (!self->updating)
        drop_done (self);
    return TRUE;
}

/* cancels the tasks of @owner (NULL: all) in both lists */
static guint
cancel_matching (LrgScriptScheduler *self,
                 const gchar        *owner,
                 gboolean            all)
{
    g_autoptr(GPtrArray) tasks = NULL;
    guint cancelled;
    guint i;

    tasks = lrg_script_scheduler_get_tasks (self);
    g_ptr_array_set_free_func (tasks, NULL);
    cancelled = 0;
    for (i = 0; i < tasks->len; i++)
    {
        LrgScriptTask *task;

        task = g_ptr_array_index (tasks, i);
        if (lrg_script_task_is_done (task))
            continue;
        if (!all && g_strcmp0 (lrg_script_task_get_owner (task), owner) != 0)
            continue;
        lrg_script_task_cancel (task);
        cancelled++;
    }
    /* tasks that never ran end now; drop them unless an update runs */
    if (!self->updating)
        drop_done (self);
    return cancelled;
}

guint
lrg_script_scheduler_cancel_owner (LrgScriptScheduler *self,
                                   const gchar        *owner)
{
    g_return_val_if_fail (LRG_IS_SCRIPT_SCHEDULER (self), 0);
    g_return_val_if_fail (owner != NULL, 0);
    return cancel_matching (self, owner, FALSE);
}

guint
lrg_script_scheduler_cancel_all (LrgScriptScheduler *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPT_SCHEDULER (self), 0);
    return cancel_matching (self, NULL, TRUE);
}

void
lrg_script_scheduler_drop_all (LrgScriptScheduler *self)
{
    g_return_if_fail (LRG_IS_SCRIPT_SCHEDULER (self));

    while (self->incoming->len > 0)
        g_ptr_array_add (self->tasks, g_ptr_array_steal_index (self->incoming, 0));
    {
        guint i;

        for (i = 0; i < self->tasks->len; i++)
            lrg_script_task_end_private (g_ptr_array_index (self->tasks, i),
                                         LRG_SCRIPT_TASK_STATE_CANCELLED, NULL);
    }
    drop_done (self);
}

guint64
lrg_script_scheduler_get_frame (LrgScriptScheduler *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPT_SCHEDULER (self), 0);
    return self->frame;
}

gdouble
lrg_script_scheduler_get_time (LrgScriptScheduler *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPT_SCHEDULER (self), 0.0);
    return self->time;
}

gint64
lrg_script_scheduler_get_frame_budget_us (LrgScriptScheduler *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPT_SCHEDULER (self), 0);
    return self->frame_budget_us;
}

void
lrg_script_scheduler_set_frame_budget_us (LrgScriptScheduler *self,
                                          gint64              budget_us)
{
    g_return_if_fail (LRG_IS_SCRIPT_SCHEDULER (self));
    budget_us = MAX (budget_us, 1);
    if (self->frame_budget_us == budget_us)
        return;
    self->frame_budget_us = budget_us;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FRAME_BUDGET_US]);
}

guint64
lrg_script_scheduler_get_instruction_budget (LrgScriptScheduler *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPT_SCHEDULER (self), 0);
    return self->instruction_budget;
}

void
lrg_script_scheduler_set_instruction_budget (LrgScriptScheduler *self,
                                             guint64             instructions)
{
    g_return_if_fail (LRG_IS_SCRIPT_SCHEDULER (self));
    instructions = MAX (instructions, (guint64)self->slice);
    if (self->instruction_budget == instructions)
        return;
    self->instruction_budget = instructions;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INSTRUCTION_BUDGET]);
}

guint
lrg_script_scheduler_get_instruction_slice (LrgScriptScheduler *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPT_SCHEDULER (self), 0);
    return self->slice;
}

void
lrg_script_scheduler_set_instruction_slice (LrgScriptScheduler *self,
                                            guint               slice)
{
    g_return_if_fail (LRG_IS_SCRIPT_SCHEDULER (self));
    slice = CLAMP (slice, 10, 1000000);
    if (self->slice == slice)
        return;
    self->slice = slice;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INSTRUCTION_SLICE]);
}

gint64
lrg_script_scheduler_get_watchdog_us (LrgScriptScheduler *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPT_SCHEDULER (self), 0);
    return self->watchdog_us;
}

void
lrg_script_scheduler_set_watchdog_us (LrgScriptScheduler *self,
                                      gint64              watchdog_us)
{
    g_return_if_fail (LRG_IS_SCRIPT_SCHEDULER (self));
    watchdog_us = MAX (watchdog_us, 0);
    if (self->watchdog_us == watchdog_us)
        return;
    self->watchdog_us = watchdog_us;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WATCHDOG_US]);
}

guint64
lrg_script_scheduler_get_last_frame_instructions (LrgScriptScheduler *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPT_SCHEDULER (self), 0);
    return self->last_instructions;
}

gint64
lrg_script_scheduler_get_last_frame_time_us (LrgScriptScheduler *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPT_SCHEDULER (self), 0);
    return self->last_time_us;
}

guint64
lrg_script_scheduler_get_max_frame_instructions (LrgScriptScheduler *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPT_SCHEDULER (self), 0);
    return self->max_instructions;
}

gint64
lrg_script_scheduler_get_max_frame_time_us (LrgScriptScheduler *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPT_SCHEDULER (self), 0);
    return self->max_time_us;
}

void
lrg_script_scheduler_reset_stats (LrgScriptScheduler *self)
{
    g_return_if_fail (LRG_IS_SCRIPT_SCHEDULER (self));
    self->max_instructions = 0;
    self->max_time_us = 0;
}
