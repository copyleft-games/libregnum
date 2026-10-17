/* test-script-scheduler.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgScriptTask, LrgScriptScheduler, LrgScriptTaskCallback
 * and (with LuaJIT) LrgScriptTaskLua: waits by seconds, frames and
 * conditions, cancellation with its grace resume, owners, the watchdog,
 * the per-frame instruction budget that preempts a Lua busy loop, the
 * Lua library (wait, wait_until, cancelled, spawn) and failures.
 */

#include <glib.h>
#include <libregnum.h>
#include <string.h>

/* ==========================================================================
 * C step tasks
 * ========================================================================== */

typedef struct
{
    guint    steps;
    guint    stop_after;
    gdouble  wait;
    gboolean saw_cancel;
    gboolean cleaned;
} Counter;

static void
counter_cleanup (gpointer data)
{
    ((Counter *)data)->cleaned = TRUE;
}

/* counts, waits counter->wait seconds (0: frames), stops after stop_after */
static gboolean
count_step (LrgScriptTask  *task,
            gpointer        user_data,
            GError        **error)
{
    Counter *counter = user_data;

    (void)error;
    if (lrg_script_task_is_cancel_requested (task))
    {
        counter->saw_cancel = TRUE;
        return TRUE;
    }
    counter->steps++;
    if (counter->stop_after > 0 && counter->steps >= counter->stop_after)
        lrg_script_task_finish (task);
    else if (counter->wait > 0.0)
        lrg_script_task_wait_seconds (task, counter->wait);
    else
        lrg_script_task_wait_frames (task, 2);
    return TRUE;
}

static void
test_callback_waits (void)
{
    g_autoptr(LrgScriptScheduler) scheduler = lrg_script_scheduler_new ();
    g_autoptr(LrgScriptTask) task = NULL;
    Counter counter = { 0, 3, 0.5, FALSE, FALSE };
    guint id;
    guint i;

    task = lrg_script_task_callback_new (count_step, &counter, counter_cleanup);
    g_assert_cmpint (lrg_script_task_get_state (task), ==, LRG_SCRIPT_TASK_STATE_PENDING);
    id = lrg_script_scheduler_add (scheduler, task);
    g_assert_cmpuint (id, !=, 0);
    g_assert_cmpuint (lrg_script_task_get_id (task), ==, id);
    g_assert_false (lrg_script_task_supports_preemption (task));

    /* frame 1 runs it, then it waits half a second: 1/8 s frames */
    lrg_script_scheduler_update (scheduler, 0.125);
    g_assert_cmpuint (counter.steps, ==, 1);
    g_assert_cmpint (lrg_script_task_get_state (task), ==, LRG_SCRIPT_TASK_STATE_WAITING);
    g_assert_cmpint (lrg_script_task_get_wait_kind (task), ==, LRG_SCRIPT_WAIT_SECONDS);
    for (i = 0; i < 3; i++)
        lrg_script_scheduler_update (scheduler, 0.125);
    g_assert_cmpuint (counter.steps, ==, 1);
    lrg_script_scheduler_update (scheduler, 0.125);
    g_assert_cmpuint (counter.steps, ==, 2);
    for (i = 0; i < 4; i++)
        lrg_script_scheduler_update (scheduler, 0.125);
    g_assert_cmpuint (counter.steps, ==, 3);
    g_assert_cmpint (lrg_script_task_get_state (task), ==, LRG_SCRIPT_TASK_STATE_FINISHED);
    g_assert_true (counter.cleaned);
    g_assert_cmpuint (lrg_script_scheduler_get_task_count (scheduler), ==, 0);
    g_assert_null (lrg_script_scheduler_get_task (scheduler, id));
    g_assert_cmpuint (lrg_script_task_get_resume_count (task), ==, 3);
}

static void
test_callback_frames (void)
{
    g_autoptr(LrgScriptScheduler) scheduler = lrg_script_scheduler_new ();
    g_autoptr(LrgScriptTask) task = NULL;
    Counter counter = { 0, 0, 0.0, FALSE, FALSE };
    guint i;

    /* wait_frames(2): every other frame */
    task = lrg_script_task_callback_new (count_step, &counter, NULL);
    lrg_script_scheduler_add (scheduler, task);
    for (i = 0; i < 10; i++)
        lrg_script_scheduler_update (scheduler, 0.0);
    g_assert_cmpuint (counter.steps, ==, 5);
    g_assert_cmpuint (lrg_script_scheduler_get_frame (scheduler), ==, 10);
}

static gboolean
flag_condition (LrgScriptTask *task,
                gpointer       user_data)
{
    (void)task;
    return *(gboolean *)user_data;
}

typedef struct
{
    gboolean *flag;
    guint     runs;
    gboolean  timed_out;
    gdouble   timeout;
} Waiter;

static gboolean
condition_step (LrgScriptTask  *task,
                gpointer        user_data,
                GError        **error)
{
    Waiter *waiter = user_data;

    (void)error;
    waiter->runs++;
    if (waiter->runs == 1)
    {
        lrg_script_task_wait_condition (task, flag_condition, waiter->flag, NULL, waiter->timeout);
        return TRUE;
    }
    waiter->timed_out = lrg_script_task_get_wait_timed_out (task);
    lrg_script_task_finish (task);
    return TRUE;
}

static void
test_condition_wait (void)
{
    g_autoptr(LrgScriptScheduler) scheduler = lrg_script_scheduler_new ();
    g_autoptr(LrgScriptTask) a = NULL;
    g_autoptr(LrgScriptTask) b = NULL;
    gboolean flag = FALSE;
    gboolean never = FALSE;
    Waiter wa = { NULL, 0, FALSE, 0.0 };
    Waiter wb = { NULL, 0, FALSE, 1.0 };
    guint i;

    wa.flag = &flag;
    wb.flag = &never;
    a = lrg_script_task_callback_new (condition_step, &wa, NULL);
    b = lrg_script_task_callback_new (condition_step, &wb, NULL);
    lrg_script_scheduler_add (scheduler, a);
    lrg_script_scheduler_add (scheduler, b);
    for (i = 0; i < 5; i++)
        lrg_script_scheduler_update (scheduler, 0.1);
    g_assert_cmpint (lrg_script_task_get_wait_kind (a), ==, LRG_SCRIPT_WAIT_CONDITION);
    g_assert_cmpuint (wa.runs, ==, 1);
    flag = TRUE;
    lrg_script_scheduler_update (scheduler, 0.1);
    g_assert_cmpuint (wa.runs, ==, 2);
    g_assert_false (wa.timed_out);
    g_assert_true (lrg_script_task_is_done (a));

    /* b's condition never holds: its timeout of one second ends it */
    for (i = 0; i < 6; i++)
        lrg_script_scheduler_update (scheduler, 0.1);
    g_assert_cmpuint (wb.runs, ==, 2);
    g_assert_true (wb.timed_out);
}

static void
on_finished (LrgScriptScheduler *scheduler,
             LrgScriptTask      *task,
             gpointer            user_data)
{
    (void)scheduler;
    (void)task;
    (*(guint *)user_data)++;
}

static void
test_cancel (void)
{
    g_autoptr(LrgScriptScheduler) scheduler = lrg_script_scheduler_new ();
    g_autoptr(LrgScriptTask) fresh = NULL;
    g_autoptr(LrgScriptTask) started = NULL;
    g_autoptr(LrgScriptTask) loose = NULL;
    Counter c1 = { 0, 0, 10.0, FALSE, FALSE };
    Counter c2 = { 0, 0, 10.0, FALSE, FALSE };
    Counter c3 = { 0, 0, 10.0, FALSE, FALSE };
    guint finished = 0;

    g_signal_connect (scheduler, "task-finished", G_CALLBACK (on_finished), &finished);

    /* outside a scheduler: cancelled at once */
    loose = lrg_script_task_callback_new (count_step, &c3, counter_cleanup);
    lrg_script_task_cancel (loose);
    g_assert_cmpint (lrg_script_task_get_state (loose), ==, LRG_SCRIPT_TASK_STATE_CANCELLED);
    g_assert_true (c3.cleaned);

    fresh = lrg_script_task_callback_new (count_step, &c1, NULL);
    started = lrg_script_task_callback_new (count_step, &c2, NULL);
    lrg_script_task_set_owner (started, "mod-a");
    lrg_script_scheduler_add (scheduler, started);
    lrg_script_scheduler_update (scheduler, 0.1);
    g_assert_cmpuint (c2.steps, ==, 1);
    lrg_script_scheduler_add (scheduler, fresh);

    /* never run: ends without a resume */
    g_assert_true (lrg_script_scheduler_cancel (scheduler, lrg_script_task_get_id (fresh)));
    g_assert_cmpint (lrg_script_task_get_state (fresh), ==, LRG_SCRIPT_TASK_STATE_CANCELLED);
    g_assert_cmpuint (c1.steps, ==, 0);
    g_assert_cmpuint (finished, ==, 1);
    g_assert_false (lrg_script_scheduler_cancel (scheduler, lrg_script_task_get_id (fresh)));

    /* started and waiting 10 s: the next update is its grace resume */
    g_assert_cmpuint (lrg_script_scheduler_cancel_owner (scheduler, "mod-b"), ==, 0);
    g_assert_cmpuint (lrg_script_scheduler_cancel_owner (scheduler, "mod-a"), ==, 1);
    g_assert_true (lrg_script_task_is_cancel_requested (started));
    g_assert_false (lrg_script_task_is_done (started));
    lrg_script_scheduler_update (scheduler, 0.1);
    g_assert_true (c2.saw_cancel);
    g_assert_cmpint (lrg_script_task_get_state (started), ==, LRG_SCRIPT_TASK_STATE_CANCELLED);
    g_assert_cmpuint (finished, ==, 2);
    g_assert_cmpuint (lrg_script_scheduler_get_task_count (scheduler), ==, 0);
}

static void
test_drop_all (void)
{
    g_autoptr(LrgScriptScheduler) scheduler = lrg_script_scheduler_new ();
    g_autoptr(LrgScriptTask) task = NULL;
    Counter counter = { 0, 0, 10.0, FALSE, FALSE };

    task = lrg_script_task_callback_new (count_step, &counter, counter_cleanup);
    lrg_script_scheduler_add (scheduler, task);
    lrg_script_scheduler_update (scheduler, 0.1);
    lrg_script_scheduler_drop_all (scheduler);
    g_assert_cmpint (lrg_script_task_get_state (task), ==, LRG_SCRIPT_TASK_STATE_CANCELLED);
    g_assert_false (counter.saw_cancel);
    g_assert_true (counter.cleaned);
    g_assert_cmpuint (lrg_script_scheduler_get_task_count (scheduler), ==, 0);
}

static gboolean
slow_step (LrgScriptTask  *task,
           gpointer        user_data,
           GError        **error)
{
    (void)task;
    (void)user_data;
    (void)error;
    g_usleep (30000);
    return TRUE;
}

static gboolean
failing_step (LrgScriptTask  *task,
              gpointer        user_data,
              GError        **error)
{
    (void)task;
    (void)user_data;
    g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_RUNTIME, "broken step");
    return FALSE;
}

static void
test_watchdog_and_failure (void)
{
    g_autoptr(LrgScriptScheduler) scheduler = lrg_script_scheduler_new ();
    g_autoptr(LrgScriptTask) slow = NULL;
    g_autoptr(LrgScriptTask) broken = NULL;

    lrg_script_scheduler_set_frame_budget_us (scheduler, 1000);
    lrg_script_scheduler_set_watchdog_us (scheduler, 2000);
    slow = lrg_script_task_callback_new (slow_step, NULL, NULL);
    broken = lrg_script_task_callback_new (failing_step, NULL, NULL);
    lrg_script_scheduler_add (scheduler, slow);
    lrg_script_scheduler_add (scheduler, broken);
    lrg_script_scheduler_update (scheduler, 0.1);
    lrg_script_scheduler_update (scheduler, 0.1);
    g_assert_cmpint (lrg_script_task_get_state (slow), ==, LRG_SCRIPT_TASK_STATE_FAILED);
    g_assert_error ((GError *)lrg_script_task_get_error (slow), LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_BUDGET);
    g_assert_cmpint (lrg_script_task_get_state (broken), ==, LRG_SCRIPT_TASK_STATE_FAILED);
    g_assert_error ((GError *)lrg_script_task_get_error (broken), LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_RUNTIME);
    g_assert_cmpstr (lrg_script_task_get_error (broken)->message, ==, "broken step");
}

static void
test_budget_properties (void)
{
    g_autoptr(LrgScriptScheduler) scheduler = lrg_script_scheduler_new ();
    gint64 budget_us;

    g_assert_cmpint (lrg_script_scheduler_get_frame_budget_us (scheduler), ==,
                     LRG_SCRIPT_SCHEDULER_DEFAULT_FRAME_BUDGET_US);
    g_assert_cmpuint (lrg_script_scheduler_get_instruction_slice (scheduler), ==,
                      LRG_SCRIPT_SCHEDULER_DEFAULT_SLICE);
    g_object_set (scheduler, "frame-budget-us", (gint64)5000, NULL);
    g_object_get (scheduler, "frame-budget-us", &budget_us, NULL);
    g_assert_cmpint (budget_us, ==, 5000);
    lrg_script_scheduler_set_instruction_slice (scheduler, 1);
    g_assert_cmpuint (lrg_script_scheduler_get_instruction_slice (scheduler), ==, 10);
    lrg_script_scheduler_set_instruction_budget (scheduler, 2);
    g_assert_cmpuint (lrg_script_scheduler_get_instruction_budget (scheduler), ==, 10);
}

/* ==========================================================================
 * Lua coroutine tasks
 * ========================================================================== */

#ifdef LRG_HAS_LUAJIT

static LrgScriptScheduler *
resolve_scheduler (gpointer user_data)
{
    return LRG_SCRIPT_SCHEDULER (user_data);
}

static LrgScriptingLua *
new_lua (LrgScriptScheduler *scheduler)
{
    LrgScriptingLua *lua;
    g_autoptr(GError) error = NULL;

    lua = lrg_scripting_lua_new ();
    g_assert_true (lrg_script_task_lua_install_library (lua, "task", "tests", resolve_scheduler,
                                                        scheduler, NULL, &error));
    g_assert_no_error (error);
    return lua;
}

static gint64
lua_global_int (LrgScriptingLua *lua,
                const gchar     *name)
{
    GValue value = G_VALUE_INIT;
    gint64 out;

    g_assert_true (lrg_scripting_get_global (LRG_SCRIPTING (lua), name, &value, NULL));
    if (G_VALUE_HOLDS_INT64 (&value))
        out = g_value_get_int64 (&value);
    else if (G_VALUE_HOLDS_DOUBLE (&value))
        out = (gint64)g_value_get_double (&value);
    else if (G_VALUE_HOLDS_BOOLEAN (&value))
        out = g_value_get_boolean (&value) ? 1 : 0;
    else
        out = -1;
    g_value_unset (&value);
    return out;
}

static void
test_lua_waits (void)
{
    g_autoptr(LrgScriptScheduler) scheduler = lrg_script_scheduler_new ();
    g_autoptr(LrgScriptingLua) lua = new_lua (scheduler);
    g_autoptr(LrgScriptTask) task = NULL;
    g_autoptr(GError) error = NULL;
    guint i;

    task = lrg_script_task_lua_new_from_string (lua, "waits",
        "step = 0\n"
        "step = 1; task.wait(0.5)\n"
        "step = 2; task.wait_frames(3)\n"
        "step = 3; task.yield()\n"
        "step = 4; flag = false\n"
        "local ok = task.wait_until(function() return flag end)\n"
        "step = 5\n"
        "timed = task.wait_until(function() return false end, 0.3)\n"
        "step = 6\n", &error);
    g_assert_no_error (error);
    g_assert_true (lrg_script_task_supports_preemption (task));
    lrg_script_scheduler_add (scheduler, task);

    lrg_script_scheduler_update (scheduler, 0.125);
    g_assert_cmpint (lua_global_int (lua, "step"), ==, 1);
    for (i = 0; i < 3; i++)
        lrg_script_scheduler_update (scheduler, 0.125);
    g_assert_cmpint (lua_global_int (lua, "step"), ==, 1);
    lrg_script_scheduler_update (scheduler, 0.125);
    g_assert_cmpint (lua_global_int (lua, "step"), ==, 2);
    lrg_script_scheduler_update (scheduler, 0.125);
    lrg_script_scheduler_update (scheduler, 0.125);
    g_assert_cmpint (lua_global_int (lua, "step"), ==, 2);
    lrg_script_scheduler_update (scheduler, 0.125);
    g_assert_cmpint (lua_global_int (lua, "step"), ==, 3);
    lrg_script_scheduler_update (scheduler, 0.125);
    g_assert_cmpint (lua_global_int (lua, "step"), ==, 4);
    for (i = 0; i < 3; i++)
        lrg_script_scheduler_update (scheduler, 0.125);
    g_assert_cmpint (lua_global_int (lua, "step"), ==, 4);
    g_assert_true (lrg_scripting_load_string (LRG_SCRIPTING (lua), "set", "flag = true", NULL));
    lrg_script_scheduler_update (scheduler, 0.125);
    g_assert_cmpint (lua_global_int (lua, "step"), ==, 5);
    for (i = 0; i < 5; i++)
        lrg_script_scheduler_update (scheduler, 0.125);
    g_assert_cmpint (lua_global_int (lua, "step"), ==, 6);
    g_assert_cmpint (lua_global_int (lua, "timed"), ==, 0);
    g_assert_cmpint (lrg_script_task_get_state (task), ==, LRG_SCRIPT_TASK_STATE_FINISHED);
}

static void
test_lua_busy_loop_budget (void)
{
    g_autoptr(LrgScriptScheduler) scheduler = lrg_script_scheduler_new ();
    g_autoptr(LrgScriptingLua) lua = new_lua (scheduler);
    g_autoptr(LrgScriptTask) task = NULL;
    g_autoptr(GError) error = NULL;
    gint64 last;
    guint i;

    /* time is not the limit here: the instruction budget is */
    lrg_script_scheduler_set_frame_budget_us (scheduler, G_GINT64_CONSTANT (60000000));
    lrg_script_scheduler_set_instruction_slice (scheduler, 1000);
    lrg_script_scheduler_set_instruction_budget (scheduler, 50000);
    task = lrg_script_task_lua_new_from_string (lua, "busy",
        "counter = 0\nwhile true do counter = counter + 1 end\n", &error);
    g_assert_no_error (error);
    lrg_script_scheduler_add (scheduler, task);

    last = -1;
    for (i = 0; i < 20; i++)
    {
        gint64 now;

        lrg_script_scheduler_update (scheduler, 1.0 / 60.0);
        g_assert_cmpuint (lrg_script_scheduler_get_last_frame_instructions (scheduler), <=, 50000);
        g_assert_cmpuint (lrg_script_scheduler_get_last_frame_instructions (scheduler), >=, 49000);
        now = lua_global_int (lua, "counter");
        g_assert_cmpint (now, >, last);
        last = now;
    }
    g_assert_cmpint (lrg_script_task_get_state (task), ==, LRG_SCRIPT_TASK_STATE_WAITING);
    g_assert_cmpuint (lrg_script_task_get_preemption_count (task), ==, 20);
    g_assert_cmpuint (lrg_script_scheduler_get_max_frame_instructions (scheduler), <=, 50000);

    /* a busy loop inside pcall is preempted too */
    {
        g_autoptr(LrgScriptTask) wrapped = NULL;

        wrapped = lrg_script_task_lua_new_from_string (lua, "pcall-busy",
            "pcall(function() while true do end end)\n", &error);
        g_assert_no_error (error);
        lrg_script_scheduler_add (scheduler, wrapped);
        for (i = 0; i < 6; i++)
            lrg_script_scheduler_update (scheduler, 1.0 / 60.0);
        g_assert_cmpint (lrg_script_task_get_state (wrapped), ==, LRG_SCRIPT_TASK_STATE_WAITING);
        g_assert_cmpuint (lrg_script_task_get_preemption_count (wrapped), >=, 2);
        /* both tasks progressed: round robin */
        g_assert_cmpint (lua_global_int (lua, "counter"), >, last);
    }
}

static void
test_lua_time_budget (void)
{
    g_autoptr(LrgScriptScheduler) scheduler = lrg_script_scheduler_new ();
    g_autoptr(LrgScriptingLua) lua = new_lua (scheduler);
    g_autoptr(LrgScriptTask) task = NULL;
    g_autoptr(GError) error = NULL;
    guint i;

    /* the time is the limit: a frame stays near its 2 ms */
    lrg_script_scheduler_set_frame_budget_us (scheduler, 2000);
    lrg_script_scheduler_set_instruction_budget (scheduler, G_MAXUINT64 / 4);
    task = lrg_script_task_lua_new_from_string (lua, "busy", "while true do end", &error);
    g_assert_no_error (error);
    lrg_script_scheduler_add (scheduler, task);
    for (i = 0; i < 5; i++)
    {
        lrg_script_scheduler_update (scheduler, 0.016);
        /* generous: a loaded test machine may stretch one slice */
        g_assert_cmpint (lrg_script_scheduler_get_last_frame_time_us (scheduler), <, 200000);
    }
    g_assert_cmpuint (lrg_script_task_get_preemption_count (task), ==, 5);
}

static void
test_lua_cancel (void)
{
    g_autoptr(LrgScriptScheduler) scheduler = lrg_script_scheduler_new ();
    g_autoptr(LrgScriptingLua) lua = new_lua (scheduler);
    g_autoptr(LrgScriptTask) task = NULL;
    g_autoptr(GError) error = NULL;
    guint i;

    task = lrg_script_task_lua_new_from_string (lua, "loop",
        "rounds = 0\n"
        "while not task.cancelled() do rounds = rounds + 1; task.wait(2) end\n"
        "cleaned = true\n", &error);
    g_assert_no_error (error);
    lrg_script_scheduler_add (scheduler, task);
    for (i = 0; i < 50; i++)
        lrg_script_scheduler_update (scheduler, 0.1);
    g_assert_cmpint (lua_global_int (lua, "rounds"), ==, 3);
    lrg_script_task_cancel (task);
    lrg_script_scheduler_update (scheduler, 0.1);
    g_assert_cmpint (lrg_script_task_get_state (task), ==, LRG_SCRIPT_TASK_STATE_CANCELLED);
    g_assert_cmpint (lua_global_int (lua, "cleaned"), ==, 1);
    g_assert_cmpint (lua_global_int (lua, "rounds"), ==, 3);
}

static void
test_lua_library (void)
{
    g_autoptr(LrgScriptScheduler) scheduler = lrg_script_scheduler_new ();
    g_autoptr(LrgScriptingLua) lua = new_lua (scheduler);
    g_autoptr(GError) error = NULL;
    guint i;

    /* outside a task the waits refuse; cancelled() is false */
    g_assert_false (lrg_scripting_load_string (LRG_SCRIPTING (lua), "outside", "task.wait(1)", &error));
    g_assert_nonnull (strstr (error->message, "not inside a task"));
    g_clear_error (&error);
    g_assert_true (lrg_scripting_load_string (LRG_SCRIPTING (lua), "flags",
        "outside_cancelled = task.cancelled()\noutside_running = task.running() == nil", &error));
    g_assert_no_error (error);
    g_assert_cmpint (lua_global_int (lua, "outside_cancelled"), ==, 0);
    g_assert_cmpint (lua_global_int (lua, "outside_running"), ==, 1);

    /* spawn from a script: the task starts on the next update */
    g_assert_true (lrg_scripting_load_string (LRG_SCRIPTING (lua), "spawn",
        "ticks = 0\n"
        "id = task.spawn(function() me = task.running() while true do ticks = ticks + 1 task.yield() end end, 'ticker')\n",
        &error));
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_script_scheduler_get_task_count (scheduler), ==, 1);
    {
        LrgScriptTask *spawned;

        spawned = lrg_script_scheduler_get_task (scheduler, (guint)lua_global_int (lua, "id"));
        g_assert_nonnull (spawned);
        g_assert_cmpstr (lrg_script_task_get_owner (spawned), ==, "tests");
        g_assert_cmpstr (lrg_script_task_get_name (spawned), ==, "ticker");
    }
    for (i = 0; i < 4; i++)
        lrg_script_scheduler_update (scheduler, 0.25);
    g_assert_cmpint (lua_global_int (lua, "ticks"), ==, 4);
    g_assert_cmpint (lua_global_int (lua, "me"), ==, lua_global_int (lua, "id"));
    g_assert_true (lrg_scripting_load_string (LRG_SCRIPTING (lua), "clock",
        "now = task.time() * 100\nframes = task.frame()\nstopped = task.cancel(id)", NULL));
    g_assert_cmpint (lua_global_int (lua, "now"), ==, 100);
    g_assert_cmpint (lua_global_int (lua, "frames"), ==, 4);
    g_assert_cmpint (lua_global_int (lua, "stopped"), ==, 1);
    lrg_script_scheduler_update (scheduler, 0.25);
    g_assert_cmpuint (lrg_script_scheduler_get_task_count (scheduler), ==, 0);

    /* a task from a global function */
    g_assert_true (lrg_scripting_load_string (LRG_SCRIPTING (lua), "fn",
        "function body() done_body = true end", NULL));
    {
        g_autoptr(LrgScriptTask) task = NULL;

        task = lrg_script_task_lua_new_from_global (lua, "body", &error);
        g_assert_no_error (error);
        lrg_script_scheduler_add (scheduler, task);
        lrg_script_scheduler_update (scheduler, 0.1);
        g_assert_cmpint (lrg_script_task_get_state (task), ==, LRG_SCRIPT_TASK_STATE_FINISHED);
        g_assert_cmpint (lua_global_int (lua, "done_body"), ==, 1);
        g_assert_null (lrg_script_task_lua_new_from_global (lua, "nothing_here", &error));
        g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_NOT_FOUND);
        g_clear_error (&error);
    }
}

static void
test_lua_failures (void)
{
    g_autoptr(LrgScriptScheduler) scheduler = lrg_script_scheduler_new ();
    g_autoptr(LrgScriptingLua) lua = new_lua (scheduler);
    g_autoptr(LrgScriptTask) broken = NULL;
    g_autoptr(LrgScriptTask) sorter = NULL;
    g_autoptr(LrgScriptTask) orphan = NULL;
    g_autoptr(GError) error = NULL;
    guint i;

    /* does not compile */
    g_assert_null (lrg_script_task_lua_new_from_string (lua, "syntax", "while do", &error));
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_SYNTAX);
    g_clear_error (&error);

    /* raises */
    broken = lrg_script_task_lua_new_from_string (lua, "raise", "task.wait(0.1) error('boom')", &error);
    g_assert_no_error (error);
    lrg_script_scheduler_add (scheduler, broken);

    /* busy inside a C function (table.sort): cannot be suspended there,
     * the instruction watchdog stops it */
    lrg_script_scheduler_set_instruction_budget (scheduler, 20000);
    lrg_script_scheduler_set_frame_budget_us (scheduler, G_GINT64_CONSTANT (60000000));
    lrg_script_scheduler_set_watchdog_us (scheduler, 0);
    sorter = lrg_script_task_lua_new_from_string (lua, "sort",
        "local t = {} for i = 1, 50 do t[i] = 51 - i end\n"
        "table.sort(t, function(a, b) while true do end end)\n", &error);
    g_assert_no_error (error);
    lrg_script_scheduler_add (scheduler, sorter);

    for (i = 0; i < 4; i++)
        lrg_script_scheduler_update (scheduler, 0.1);
    g_assert_cmpint (lrg_script_task_get_state (broken), ==, LRG_SCRIPT_TASK_STATE_FAILED);
    g_assert_nonnull (strstr (lrg_script_task_get_error (broken)->message, "boom"));
    g_assert_cmpint (lrg_script_task_get_state (sorter), ==, LRG_SCRIPT_TASK_STATE_FAILED);
    g_assert_error ((GError *)lrg_script_task_get_error (sorter), LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_BUDGET);

    /* the context reset under a waiting task */
    orphan = lrg_script_task_lua_new_from_string (lua, "orphan", "task.wait(0.1) x = 1", &error);
    g_assert_no_error (error);
    lrg_script_scheduler_add (scheduler, orphan);
    lrg_script_scheduler_update (scheduler, 0.1);
    lrg_scripting_reset (LRG_SCRIPTING (lua));
    lrg_script_scheduler_update (scheduler, 0.2);
    g_assert_cmpint (lrg_script_task_get_state (orphan), ==, LRG_SCRIPT_TASK_STATE_FAILED);
}

static void
test_lua_jit_switch (void)
{
    g_autoptr(LrgScriptingLua) lua = lrg_scripting_lua_new ();

    g_assert_true (lrg_scripting_lua_get_jit_enabled (lua));
    lrg_scripting_lua_set_jit_enabled (lua, FALSE);
    g_assert_false (lrg_scripting_lua_get_jit_enabled (lua));
    g_assert_true (lrg_scripting_load_string (LRG_SCRIPTING (lua), "jit",
        "jit_on = jit.status()", NULL));
    lrg_scripting_reset (LRG_SCRIPTING (lua));
    /* a reset keeps the switch */
    g_assert_true (lrg_scripting_load_string (LRG_SCRIPTING (lua), "jit",
        "jit_on = jit.status() and 1 or 0", NULL));
    g_assert_cmpint (lua_global_int (lua, "jit_on"), ==, 0);
}

#endif /* LRG_HAS_LUAJIT */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/script-scheduler/callback/waits", test_callback_waits);
    g_test_add_func ("/script-scheduler/callback/frames", test_callback_frames);
    g_test_add_func ("/script-scheduler/condition", test_condition_wait);
    g_test_add_func ("/script-scheduler/cancel", test_cancel);
    g_test_add_func ("/script-scheduler/drop-all", test_drop_all);
    g_test_add_func ("/script-scheduler/watchdog", test_watchdog_and_failure);
    g_test_add_func ("/script-scheduler/properties", test_budget_properties);
#ifdef LRG_HAS_LUAJIT
    g_test_add_func ("/script-scheduler/lua/waits", test_lua_waits);
    g_test_add_func ("/script-scheduler/lua/busy-loop-budget", test_lua_busy_loop_budget);
    g_test_add_func ("/script-scheduler/lua/time-budget", test_lua_time_budget);
    g_test_add_func ("/script-scheduler/lua/cancel", test_lua_cancel);
    g_test_add_func ("/script-scheduler/lua/library", test_lua_library);
    g_test_add_func ("/script-scheduler/lua/failures", test_lua_failures);
    g_test_add_func ("/script-scheduler/lua/jit", test_lua_jit_switch);
#endif

    return g_test_run ();
}
