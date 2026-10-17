/* lrg-script-task-callback.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Step tasks: a C step, or a script function called through the
 * generic LrgScripting call (so every backend has a task kind).  The
 * function's return value is read as the next wait.
 */

#include "config.h"

#include "lrg-script-task-callback.h"

struct _LrgScriptTaskCallback
{
    LrgScriptTask          parent_instance;

    LrgScriptTaskStepFunc  func;
    gpointer               user_data;
    GDestroyNotify         destroy;

    GWeakRef               scripting;   /* script steps */
    gchar                 *func_name;
};

G_DEFINE_FINAL_TYPE (LrgScriptTaskCallback, lrg_script_task_callback, LRG_TYPE_SCRIPT_TASK)

/* a number (any numeric GValue) as a double */
static gboolean
value_as_number (const GValue *value,
                 gdouble      *out)
{
    GValue converted = G_VALUE_INIT;

    if (!G_IS_VALUE (value))
        return FALSE;
    if (G_VALUE_HOLDS_BOOLEAN (value) || G_VALUE_HOLDS_STRING (value))
        return FALSE;
    g_value_init (&converted, G_TYPE_DOUBLE);
    if (!g_value_type_transformable (G_VALUE_TYPE (value), G_TYPE_DOUBLE) ||
        !g_value_transform (value, &converted))
    {
        g_value_unset (&converted);
        return FALSE;
    }
    *out = g_value_get_double (&converted);
    g_value_unset (&converted);
    return TRUE;
}

/* calls the script function and reads its answer as the next wait */
static gboolean
resume_script (LrgScriptTaskCallback  *self,
               GError                **error)
{
    g_autoptr(LrgScripting) scripting = NULL;
    LrgScriptTask *task = LRG_SCRIPT_TASK (self);
    GValue args[2] = { G_VALUE_INIT, G_VALUE_INIT };
    GValue result = G_VALUE_INIT;
    gdouble seconds;
    gboolean ok;

    scripting = (LrgScripting *)g_weak_ref_get (&self->scripting);
    if (scripting == NULL)
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_NOT_FOUND,
                     "the scripting context of '%s' is gone", self->func_name);
        return FALSE;
    }
    g_value_init (&args[0], G_TYPE_INT64);
    g_value_set_int64 (&args[0], (gint64)lrg_script_task_get_id (task));
    g_value_init (&args[1], G_TYPE_BOOLEAN);
    g_value_set_boolean (&args[1], lrg_script_task_is_cancel_requested (task));
    ok = lrg_scripting_call_function (scripting, self->func_name, &result, 2, args, error);
    g_value_unset (&args[0]);
    g_value_unset (&args[1]);
    if (!ok)
    {
        if (G_IS_VALUE (&result))
            g_value_unset (&result);
        return FALSE;
    }

    /* --- the answer: seconds, the next frame, or done --- */
    if (value_as_number (&result, &seconds))
    {
        if (seconds > 0.0)
            lrg_script_task_wait_seconds (task, seconds);
        else
            lrg_script_task_wait_frames (task, 1);
    }
    else if (G_IS_VALUE (&result) && G_VALUE_HOLDS_BOOLEAN (&result) &&
             g_value_get_boolean (&result))
    {
        lrg_script_task_wait_frames (task, 1);
    }
    else
    {
        lrg_script_task_finish (task);
    }
    if (G_IS_VALUE (&result))
        g_value_unset (&result);
    return TRUE;
}

static gboolean
lrg_script_task_callback_resume (LrgScriptTask          *task,
                                 const LrgScriptBudget  *budget,
                                 GError                **error)
{
    LrgScriptTaskCallback *self = LRG_SCRIPT_TASK_CALLBACK (task);

    (void)budget;
    if (self->func_name != NULL)
        return resume_script (self, error);
    if (self->func == NULL)
    {
        lrg_script_task_finish (task);
        return TRUE;
    }
    return self->func (task, self->user_data, error);
}

static void
lrg_script_task_callback_release (LrgScriptTask *task)
{
    LrgScriptTaskCallback *self = LRG_SCRIPT_TASK_CALLBACK (task);

    if (self->destroy != NULL && self->user_data != NULL)
        self->destroy (self->user_data);
    self->user_data = NULL;
    self->destroy = NULL;
    self->func = NULL;
}

static void
lrg_script_task_callback_finalize (GObject *object)
{
    LrgScriptTaskCallback *self = LRG_SCRIPT_TASK_CALLBACK (object);

    g_weak_ref_clear (&self->scripting);
    g_clear_pointer (&self->func_name, g_free);

    G_OBJECT_CLASS (lrg_script_task_callback_parent_class)->finalize (object);
}

static void
lrg_script_task_callback_class_init (LrgScriptTaskCallbackClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgScriptTaskClass *task_class = LRG_SCRIPT_TASK_CLASS (klass);

    object_class->finalize = lrg_script_task_callback_finalize;
    task_class->resume = lrg_script_task_callback_resume;
    task_class->release = lrg_script_task_callback_release;
}

static void
lrg_script_task_callback_init (LrgScriptTaskCallback *self)
{
    g_weak_ref_init (&self->scripting, NULL);
}

LrgScriptTask *
lrg_script_task_callback_new (LrgScriptTaskStepFunc func,
                              gpointer              user_data,
                              GDestroyNotify        destroy)
{
    LrgScriptTaskCallback *self;

    g_return_val_if_fail (func != NULL, NULL);

    self = g_object_new (LRG_TYPE_SCRIPT_TASK_CALLBACK, NULL);
    self->func = func;
    self->user_data = user_data;
    self->destroy = destroy;
    return LRG_SCRIPT_TASK (self);
}

LrgScriptTask *
lrg_script_task_callback_new_for_function (LrgScripting *scripting,
                                           const gchar  *func_name)
{
    LrgScriptTaskCallback *self;

    g_return_val_if_fail (LRG_IS_SCRIPTING (scripting), NULL);
    g_return_val_if_fail (func_name != NULL && *func_name != '\0', NULL);

    self = g_object_new (LRG_TYPE_SCRIPT_TASK_CALLBACK, NULL);
    g_weak_ref_set (&self->scripting, scripting);
    self->func_name = g_strdup (func_name);
    lrg_script_task_set_name (LRG_SCRIPT_TASK (self), func_name);
    return LRG_SCRIPT_TASK (self);
}

const gchar *
lrg_script_task_callback_get_function (LrgScriptTaskCallback *self)
{
    g_return_val_if_fail (LRG_IS_SCRIPT_TASK_CALLBACK (self), NULL);
    return self->func_name;
}
