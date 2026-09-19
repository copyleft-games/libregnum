/* Gameplay timer regression tests.
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <glib.h>
#include <math.h>
#include <core/lrg-timer-manager.h>

static void
record_timeout (LrgTimerManager *manager,
                guint64          id,
                GArray          *events)
{
    g_array_append_val (events, id);
}

static void
test_clocks (void)
{
    g_autoptr(LrgTimerManager) manager = lrg_timer_manager_new ();
    g_autoptr(GArray) events = g_array_new (FALSE, FALSE, sizeof (guint64));
    guint64 scaled;
    guint64 unscaled;

    g_signal_connect (manager, "timeout", G_CALLBACK (record_timeout), events);
    scaled = lrg_timer_manager_add (manager, 1.0, FALSE, FALSE);
    unscaled = lrg_timer_manager_add (manager, 1.0, FALSE, TRUE);
    g_assert_true (lrg_timer_manager_update (manager, 0.25, 0.5));
    g_assert_cmpuint (events->len, ==, 0);
    g_assert_cmpfloat (lrg_timer_manager_get_remaining (manager, scaled), ==, 0.75);
    g_assert_true (lrg_timer_manager_update (manager, 0.0, 0.5));
    g_assert_cmpuint (events->len, ==, 1);
    g_assert_cmpuint (g_array_index (events, guint64, 0), ==, unscaled);
    g_assert_cmpfloat (lrg_timer_manager_get_remaining (manager, unscaled), ==, -1.0);
    g_assert_true (lrg_timer_manager_update (manager, 0.75, 0.0));
    g_assert_cmpuint (events->len, ==, 2);
    g_assert_cmpuint (g_array_index (events, guint64, 1), ==, scaled);
    g_assert_cmpuint (lrg_timer_manager_get_count (manager), ==, 0);
    g_assert_true (lrg_timer_manager_update (manager, 10.0, 10.0));
    g_assert_cmpuint (events->len, ==, 2);
}

static void
test_repeat (void)
{
    g_autoptr(LrgTimerManager) manager = lrg_timer_manager_new ();
    g_autoptr(GArray) events = g_array_new (FALSE, FALSE, sizeof (guint64));
    guint64 id;

    g_signal_connect (manager, "timeout", G_CALLBACK (record_timeout), events);
    id = lrg_timer_manager_add (manager, 1.0, TRUE, FALSE);
    lrg_timer_manager_update (manager, 3.25, 0.0);
    g_assert_cmpuint (events->len, ==, 1);
    g_assert_cmpfloat (lrg_timer_manager_get_remaining (manager, id), ==, 0.75);
    lrg_timer_manager_update (manager, 0.5, 0.0);
    g_assert_cmpuint (events->len, ==, 1);
    lrg_timer_manager_update (manager, 0.25, 0.0);
    g_assert_cmpuint (events->len, ==, 2);
    g_assert_cmpfloat (lrg_timer_manager_get_remaining (manager, id), ==, 1.0);
    lrg_timer_manager_update (manager, G_MAXDOUBLE, 0.0);
    g_assert_cmpuint (events->len, ==, 3);
    g_assert_cmpfloat (lrg_timer_manager_get_remaining (manager, id), >, 0.0);
    g_assert_true (lrg_timer_manager_cancel (manager, id));
    g_assert_false (lrg_timer_manager_cancel (manager, id));
}

static void
test_pause_and_clear (void)
{
    g_autoptr(LrgTimerManager) manager = lrg_timer_manager_new ();
    guint64 id;
    guint64 next;

    id = lrg_timer_manager_add (manager, 1.0, FALSE, TRUE);
    lrg_timer_manager_update (manager, 0.25, 0.25);
    g_assert_true (lrg_timer_manager_set_paused (manager, id, TRUE));
    lrg_timer_manager_update (manager, 10.0, 10.0);
    g_assert_cmpfloat (lrg_timer_manager_get_remaining (manager, id), ==, 0.75);
    g_assert_true (lrg_timer_manager_set_paused (manager, id, FALSE));
    lrg_timer_manager_update (manager, 0.0, 0.5);
    g_assert_cmpfloat (lrg_timer_manager_get_remaining (manager, id), ==, 0.25);
    lrg_timer_manager_clear (manager);
    g_assert_cmpuint (lrg_timer_manager_get_count (manager), ==, 0);
    g_assert_false (lrg_timer_manager_set_paused (manager, id, FALSE));
    next = lrg_timer_manager_add (manager, 1.0, FALSE, FALSE);
    g_assert_cmpuint (next, >, id);
    g_assert_false (lrg_timer_manager_cancel (manager, id));
}

static void
test_invalid_and_zero (void)
{
    g_autoptr(LrgTimerManager) manager = lrg_timer_manager_new ();
    guint64 id;

    g_assert_cmpuint (lrg_timer_manager_add (manager, NAN, FALSE, FALSE), ==, 0);
    g_assert_cmpuint (lrg_timer_manager_add (manager, INFINITY, FALSE, FALSE), ==, 0);
    g_assert_cmpuint (lrg_timer_manager_add (manager, -1.0, FALSE, FALSE), ==, 0);
    g_assert_cmpuint (lrg_timer_manager_add (manager, 0.0, TRUE, FALSE), ==, 0);
    id = lrg_timer_manager_add (manager, 0.0, FALSE, FALSE);
    g_assert_cmpuint (id, >, 0);
    g_assert_false (lrg_timer_manager_update (manager, 1.0, NAN));
    g_assert_false (lrg_timer_manager_update (manager, INFINITY, 1.0));
    g_assert_false (lrg_timer_manager_update (manager, -1.0, 1.0));
    g_assert_false (lrg_timer_manager_update (manager, 1.0, -1.0));
    g_assert_cmpuint (lrg_timer_manager_get_count (manager), ==, 1);
    lrg_timer_manager_update (manager, 0.0, 1.0);
    g_assert_cmpuint (lrg_timer_manager_get_count (manager), ==, 1);
    lrg_timer_manager_update (manager, 0.01, 0.0);
    g_assert_cmpuint (lrg_timer_manager_get_count (manager), ==, 0);
}

typedef struct
{
    guint64 first;
    guint64 second;
    guint64 added;
    guint calls;
    gboolean clear;
    gboolean pause;
} Mutation;

static void
mutate_timeout (LrgTimerManager *manager,
                guint64          id,
                Mutation        *mutation)
{
    mutation->calls++;
    g_assert_false (lrg_timer_manager_update (manager, 100.0, 100.0));
    if (id != mutation->first)
        return;

    /* Repeat is rearmed before callback. Cancellation must not resurrect it. */
    g_assert_cmpfloat (lrg_timer_manager_get_remaining (manager, id), ==, 1.0);
    g_assert_true (lrg_timer_manager_cancel (manager, id));
    if (mutation->clear)
        lrg_timer_manager_clear (manager);
    else if (mutation->pause)
        g_assert_true (lrg_timer_manager_set_paused (manager, mutation->second, TRUE));
    else
        g_assert_true (lrg_timer_manager_cancel (manager, mutation->second));
    mutation->added = lrg_timer_manager_add (manager, 0.0, FALSE, FALSE);
}

static void
test_mutation (gconstpointer data)
{
    g_autoptr(LrgTimerManager) manager = lrg_timer_manager_new ();
    Mutation mutation = { 0 };

    mutation.clear = GPOINTER_TO_INT (data) == 1;
    mutation.pause = GPOINTER_TO_INT (data) == 2;
    mutation.first = lrg_timer_manager_add (manager, 1.0, TRUE, FALSE);
    mutation.second = lrg_timer_manager_add (manager, 1.0, FALSE, FALSE);
    g_signal_connect (manager, "timeout", G_CALLBACK (mutate_timeout), &mutation);
    lrg_timer_manager_update (manager, 1.0, 0.0);
    g_assert_cmpuint (mutation.calls, ==, 1);
    g_assert_cmpuint (mutation.added, >, mutation.second);
    if (mutation.pause)
    {
        g_assert_cmpfloat (lrg_timer_manager_get_remaining (manager, mutation.second), ==, 0.0);
        lrg_timer_manager_set_paused (manager, mutation.second, FALSE);
    }
    lrg_timer_manager_update (manager, 1.0, 0.0);
    g_assert_cmpuint (mutation.calls, ==, mutation.pause ? 3 : 2);
    g_assert_cmpuint (lrg_timer_manager_get_count (manager), ==, 0);
}

static void
release_owner (LrgTimerManager  *manager,
               guint64           id,
               LrgTimerManager **owner)
{
    g_assert_cmpfloat (lrg_timer_manager_get_remaining (manager, id), ==, -1.0);
    g_clear_object (owner);
}

static void
test_lifetime (void)
{
    LrgTimerManager *manager = lrg_timer_manager_new ();
    gpointer weak = manager;

    g_object_add_weak_pointer (G_OBJECT (manager), &weak);
    g_signal_connect (manager, "timeout", G_CALLBACK (release_owner), &manager);
    lrg_timer_manager_add (manager, 0.0, FALSE, FALSE);
    g_assert_true (lrg_timer_manager_update (manager, 1.0, 0.0));
    g_assert_null (manager);
    g_assert_null (weak);
}

static void
test_registration_order (void)
{
    g_autoptr(LrgTimerManager) manager = lrg_timer_manager_new ();
    g_autoptr(GArray) events = g_array_new (FALSE, FALSE, sizeof (guint64));
    guint i;

    g_signal_connect (manager, "timeout", G_CALLBACK (record_timeout), events);
    for (i = 0; i < 1024; i++)
        lrg_timer_manager_add (manager, (1024 - i) * 0.125, FALSE, FALSE);
    lrg_timer_manager_update (manager, 128.0, 0.0);
    g_assert_cmpuint (events->len, ==, 1024);
    for (i = 0; i < events->len; i++)
        g_assert_cmpuint (g_array_index (events, guint64, i), ==, i + 1);
}

static void
test_large_delta_phase (void)
{
    g_autoptr(LrgTimerManager) manager = lrg_timer_manager_new ();
    g_autoptr(GArray) events = g_array_new (FALSE, FALSE, sizeof (guint64));
    guint64 id = lrg_timer_manager_add (manager, 1.0, TRUE, FALSE);

    g_signal_connect (manager, "timeout", G_CALLBACK (record_timeout), events);
    g_assert_true (lrg_timer_manager_update (manager, 0.25, 0.0));
    g_assert_true (lrg_timer_manager_update (manager, ldexp (1.0, 53), 0.0));
    g_assert_cmpuint (events->len, ==, 1);
    g_assert_cmpfloat (lrg_timer_manager_get_remaining (manager, id), ==, 0.75);
    g_assert_true (lrg_timer_manager_update (manager, G_MAXDOUBLE, 0.0));
    g_assert_cmpuint (events->len, ==, 2);
    g_assert_cmpfloat (lrg_timer_manager_get_remaining (manager, id), ==, 0.75);
    g_assert_true (lrg_timer_manager_update (manager, 0.75, 0.0));
    g_assert_cmpuint (events->len, ==, 3);
    g_assert_cmpfloat (lrg_timer_manager_get_remaining (manager, id), ==, 1.0);
}

static void
test_reschedule_clocks (void)
{
    g_autoptr(LrgTimerManager) manager = lrg_timer_manager_new ();
    g_autoptr(GArray) events = g_array_new (FALSE, FALSE, sizeof (guint64));
    guint64 id = lrg_timer_manager_add (manager, 1.0, FALSE, TRUE);
    guint64 later = lrg_timer_manager_add (manager, 2.0, FALSE, TRUE);

    g_signal_connect (manager, "timeout", G_CALLBACK (record_timeout), events);
    lrg_timer_manager_update (manager, 0.0, 0.25);
    g_assert_true (lrg_timer_manager_set_paused (manager, id, TRUE));
    g_assert_true (lrg_timer_manager_reschedule (manager, id, 2.0));
    lrg_timer_manager_update (manager, 10.0, 0.0);
    g_assert_cmpfloat (lrg_timer_manager_get_remaining (manager, id), ==, 2.0);
    g_assert_true (lrg_timer_manager_reschedule (manager, later, 2.0));
    lrg_timer_manager_update (manager, 0.0, 0.5);
    g_assert_cmpfloat (lrg_timer_manager_get_remaining (manager, id), ==, 2.0);
    g_assert_true (lrg_timer_manager_set_paused (manager, id, FALSE));
    g_assert_true (lrg_timer_manager_reschedule (manager, later, 2.0));
    lrg_timer_manager_update (manager, 0.0, 2.0);
    g_assert_cmpuint (events->len, ==, 2);
    g_assert_cmpuint (g_array_index (events, guint64, 0), ==, id);
    g_assert_cmpuint (g_array_index (events, guint64, 1), ==, later);
    g_assert_false (lrg_timer_manager_reschedule (manager, id, 1.0));
}

static void
test_reschedule_invalid (void)
{
    g_autoptr(LrgTimerManager) manager = lrg_timer_manager_new ();
    guint64 id = lrg_timer_manager_add (manager, 1.0, TRUE, FALSE);

    lrg_timer_manager_update (manager, 0.25, 0.0);
    g_assert_false (lrg_timer_manager_reschedule (manager, id, NAN));
    g_assert_false (lrg_timer_manager_reschedule (manager, id, INFINITY));
    g_assert_false (lrg_timer_manager_reschedule (manager, id, -1.0));
    g_assert_false (lrg_timer_manager_reschedule (manager, id, 0.0));
    g_assert_false (lrg_timer_manager_reschedule (manager, 0, 1.0));
    g_assert_false (lrg_timer_manager_reschedule (manager, G_MAXUINT64, 1.0));
    g_assert_cmpfloat (lrg_timer_manager_get_remaining (manager, id), ==, 0.75);
    lrg_timer_manager_update (manager, 0.75, 0.0);
    g_assert_cmpfloat (lrg_timer_manager_get_remaining (manager, id), ==, 1.0);
    lrg_timer_manager_clear (manager);
    g_assert_false (lrg_timer_manager_reschedule (manager, id, 1.0));
    g_assert_cmpuint (lrg_timer_manager_get_count (manager), ==, 0);
}

typedef struct
{
    guint64 first;
    guint64 second;
    gdouble delay;
} Reschedule;

static void
reschedule_pending (LrgTimerManager *manager,
                    guint64          id,
                    Reschedule      *change)
{
    if (id != change->first)
        return;
    /* One-shots are already removed before their handler runs. */
    g_assert_false (lrg_timer_manager_reschedule (manager, id, 1.0));
    g_assert_cmpint (lrg_timer_manager_reschedule (manager, change->second, change->delay),
                     ==, isfinite (change->delay));
}

static void
test_reschedule_pending (gconstpointer data)
{
    g_autoptr(LrgTimerManager) manager = lrg_timer_manager_new ();
    g_autoptr(GArray) events = g_array_new (FALSE, FALSE, sizeof (guint64));
    Reschedule change = { 0 };
    gint mode = GPOINTER_TO_INT (data);

    change.delay = mode == 0 ? 0.0 : mode == 1 ? 2.0 : NAN;
    change.first = lrg_timer_manager_add (manager, 1.0, FALSE, FALSE);
    change.second = lrg_timer_manager_add (manager, 1.0, FALSE, FALSE);
    g_signal_connect (manager, "timeout", G_CALLBACK (record_timeout), events);
    g_signal_connect (manager, "timeout", G_CALLBACK (reschedule_pending), &change);
    lrg_timer_manager_update (manager, 1.0, 0.0);
    g_assert_cmpuint (events->len, ==, mode == 2 ? 2 : 1);
    if (mode != 2)
    {
        g_assert_cmpfloat (lrg_timer_manager_get_remaining (manager, change.second), ==, change.delay);
        lrg_timer_manager_update (manager, 0.0, 100.0);
        g_assert_cmpuint (events->len, ==, 1);
        lrg_timer_manager_update (manager, 2.0, 0.0);
        g_assert_cmpuint (events->len, ==, 2);
    }
    g_assert_cmpuint (lrg_timer_manager_get_count (manager), ==, 0);
}

static void
reschedule_repeat (LrgTimerManager *manager,
                   guint64          id,
                   guint           *calls)
{
    (*calls)++;
    if (*calls == 1)
        g_assert_true (lrg_timer_manager_reschedule (manager, id, 2.5));
}

static void
test_reschedule_repeat (void)
{
    g_autoptr(LrgTimerManager) manager = lrg_timer_manager_new ();
    guint64 id = lrg_timer_manager_add (manager, 1.0, TRUE, FALSE);
    guint calls = 0;

    g_signal_connect (manager, "timeout", G_CALLBACK (reschedule_repeat), &calls);
    lrg_timer_manager_update (manager, 1.25, 0.0);
    g_assert_cmpfloat (lrg_timer_manager_get_remaining (manager, id), ==, 2.5);
    lrg_timer_manager_update (manager, 2.0, 0.0);
    g_assert_cmpuint (calls, ==, 1);
    lrg_timer_manager_update (manager, 0.5, 0.0);
    g_assert_cmpuint (calls, ==, 2);
    g_assert_cmpfloat (lrg_timer_manager_get_remaining (manager, id), ==, 2.5);
    lrg_timer_manager_update (manager, 5.25, 0.0);
    g_assert_cmpuint (calls, ==, 3);
    g_assert_cmpfloat (lrg_timer_manager_get_remaining (manager, id), ==, 2.25);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/timer/clocks", test_clocks);
    g_test_add_func ("/timer/large-delta-phase", test_large_delta_phase);
    g_test_add_func ("/timer/reschedule-clocks", test_reschedule_clocks);
    g_test_add_func ("/timer/reschedule-invalid", test_reschedule_invalid);
    g_test_add_data_func ("/timer/reschedule-pending-zero", GINT_TO_POINTER (0), test_reschedule_pending);
    g_test_add_data_func ("/timer/reschedule-pending-delay", GINT_TO_POINTER (1), test_reschedule_pending);
    g_test_add_data_func ("/timer/reschedule-pending-invalid", GINT_TO_POINTER (2), test_reschedule_pending);
    g_test_add_func ("/timer/reschedule-repeat", test_reschedule_repeat);
    g_test_add_func ("/timer/repeat", test_repeat);
    g_test_add_func ("/timer/pause-clear", test_pause_and_clear);
    g_test_add_func ("/timer/invalid-zero", test_invalid_and_zero);
    g_test_add_data_func ("/timer/cancel-in-handler", GINT_TO_POINTER (0), test_mutation);
    g_test_add_data_func ("/timer/clear-in-handler", GINT_TO_POINTER (1), test_mutation);
    g_test_add_data_func ("/timer/pause-in-handler", GINT_TO_POINTER (2), test_mutation);
    g_test_add_func ("/timer/lifetime", test_lifetime);
    g_test_add_func ("/timer/registration-order", test_registration_order);
    return g_test_run ();
}
