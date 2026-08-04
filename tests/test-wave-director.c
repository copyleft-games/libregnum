/* test-wave-director.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgWaveDirector - data-driven enemy wave sequencing.
 * All tests are headless-safe (no window or audio device required).
 */

#include <glib.h>
#include <math.h>
#include <libregnum.h>

/* ==========================================================================
 * Signal Capture Helpers
 * ========================================================================== */

typedef struct
{
    GPtrArray *events;  /* owned strings: "started:N", "spawn:id:N", ... */
    GArray    *xs;      /* gfloat spawn X positions, in emission order */
    GArray    *ys;      /* gfloat spawn Y positions, in emission order */
} Capture;

static Capture *
capture_new (void)
{
    Capture *cap;

    cap = g_new0 (Capture, 1);
    cap->events = g_ptr_array_new_with_free_func (g_free);
    cap->xs = g_array_new (FALSE, FALSE, sizeof (gfloat));
    cap->ys = g_array_new (FALSE, FALSE, sizeof (gfloat));

    return cap;
}

static void
capture_free (Capture *cap)
{
    g_ptr_array_unref (cap->events);
    g_array_unref (cap->xs);
    g_array_unref (cap->ys);
    g_free (cap);
}

static void
on_spawn (LrgWaveDirector *director,
          const gchar     *enemy_id,
          gfloat           x,
          gfloat           y,
          guint            wave_index,
          gpointer         user_data)
{
    Capture *cap = user_data;

    g_ptr_array_add (cap->events,
                     g_strdup_printf ("spawn:%s:%u", enemy_id, wave_index));
    g_array_append_val (cap->xs, x);
    g_array_append_val (cap->ys, y);
}

static void
on_wave_started (LrgWaveDirector *director,
                 guint            index,
                 gpointer         user_data)
{
    Capture *cap = user_data;

    g_ptr_array_add (cap->events, g_strdup_printf ("started:%u", index));
}

static void
on_wave_completed (LrgWaveDirector *director,
                   guint            index,
                   gpointer         user_data)
{
    Capture *cap = user_data;

    g_ptr_array_add (cap->events, g_strdup_printf ("completed:%u", index));
}

static void
on_finished (LrgWaveDirector *director,
             gpointer         user_data)
{
    Capture *cap = user_data;

    g_ptr_array_add (cap->events, g_strdup ("finished"));
}

static void
capture_connect (Capture         *cap,
                 LrgWaveDirector *director)
{
    g_signal_connect (director, "spawn", G_CALLBACK (on_spawn), cap);
    g_signal_connect (director, "wave-started", G_CALLBACK (on_wave_started), cap);
    g_signal_connect (director, "wave-completed", G_CALLBACK (on_wave_completed), cap);
    g_signal_connect (director, "finished", G_CALLBACK (on_finished), cap);
}

static const gchar *
capture_event (Capture *cap,
               guint    index)
{
    g_assert_cmpuint (index, <, cap->events->len);

    return g_ptr_array_index (cap->events, index);
}

static guint
capture_count_prefix (Capture     *cap,
                      const gchar *prefix)
{
    guint i;
    guint count;

    count = 0;
    for (i = 0; i < cap->events->len; i++)
    {
        const gchar *event = g_ptr_array_index (cap->events, i);

        if (g_str_has_prefix (event, prefix))
            count++;
    }

    return count;
}

/* ==========================================================================
 * Test Cases - Construction
 * ========================================================================== */

static void
test_wave_director_new (void)
{
    g_autoptr(LrgWaveDirector) director = NULL;

    director = lrg_wave_director_new ();

    g_assert_nonnull (director);
    g_assert_cmpuint (lrg_wave_director_get_wave_count (director), ==, 0);
    g_assert_cmpuint (lrg_wave_director_get_current_wave (director), ==, 0);
    g_assert_false (lrg_wave_director_is_started (director));
    g_assert_false (lrg_wave_director_is_finished (director));
}

/* ==========================================================================
 * Test Cases - Programmatic Build and Ordering
 * ========================================================================== */

static void
test_wave_director_programmatic_ordering (void)
{
    g_autoptr(LrgWaveDirector) director = NULL;
    Capture *cap;
    guint wave0;
    guint wave1;
    guint i;

    director = lrg_wave_director_new ();
    cap = capture_new ();
    capture_connect (cap, director);

    wave0 = lrg_wave_director_add_wave (director, LRG_WAVE_TRIGGER_TIME, 1.0f);
    g_assert_cmpuint (wave0, ==, 0);
    lrg_wave_director_wave_add_entry (director, wave0, "grunt", 2,
                                      LRG_WAVE_PATTERN_POINT,
                                      10.0f, 20.0f, 0.0f, 0.0f);

    wave1 = lrg_wave_director_add_wave (director, LRG_WAVE_TRIGGER_CLEARED, 0.0f);
    g_assert_cmpuint (wave1, ==, 1);
    lrg_wave_director_wave_add_entry (director, wave1, "boss", 1,
                                      LRG_WAVE_PATTERN_POINT,
                                      160.0f, 40.0f, 0.0f, 0.0f);

    g_assert_cmpuint (lrg_wave_director_get_wave_count (director), ==, 2);

    lrg_wave_director_start (director);
    g_assert_true (lrg_wave_director_is_started (director));
    g_assert_cmpuint (lrg_wave_director_get_current_wave (director), ==, 0);

    /* Time trigger 1.0 must not fire at 0.5 elapsed */
    lrg_wave_director_update (director, 0.5f, 0.0f, 5);
    g_assert_cmpuint (cap->events->len, ==, 0);

    /* Second 0.5 reaches 1.0: wave 0 fires and spawns everything */
    lrg_wave_director_update (director, 0.5f, 0.0f, 5);
    g_assert_cmpuint (lrg_wave_director_get_current_wave (director), ==, 1);
    g_assert_false (lrg_wave_director_is_finished (director));

    /* Cleared trigger must not fire while enemies are alive */
    lrg_wave_director_update (director, 0.016f, 0.0f, 3);
    g_assert_cmpuint (capture_count_prefix (cap, "spawn:boss"), ==, 0);

    /* All enemies dead: wave 1 fires, spawns, and the director finishes */
    lrg_wave_director_update (director, 0.016f, 0.0f, 0);
    g_assert_true (lrg_wave_director_is_finished (director));

    /* Exact event ordering: started/completed pairs bracketing spawns */
    g_assert_cmpuint (cap->events->len, ==, 8);
    g_assert_cmpstr (capture_event (cap, 0), ==, "started:0");
    g_assert_cmpstr (capture_event (cap, 1), ==, "spawn:grunt:0");
    g_assert_cmpstr (capture_event (cap, 2), ==, "spawn:grunt:0");
    g_assert_cmpstr (capture_event (cap, 3), ==, "completed:0");
    g_assert_cmpstr (capture_event (cap, 4), ==, "started:1");
    g_assert_cmpstr (capture_event (cap, 5), ==, "spawn:boss:1");
    g_assert_cmpstr (capture_event (cap, 6), ==, "completed:1");
    g_assert_cmpstr (capture_event (cap, 7), ==, "finished");

    /* Point pattern spawns exactly at the configured position */
    for (i = 0; i < 2; i++)
    {
        g_assert_cmpfloat (g_array_index (cap->xs, gfloat, i), ==, 10.0f);
        g_assert_cmpfloat (g_array_index (cap->ys, gfloat, i), ==, 20.0f);
    }
    g_assert_cmpfloat (g_array_index (cap->xs, gfloat, 2), ==, 160.0f);
    g_assert_cmpfloat (g_array_index (cap->ys, gfloat, 2), ==, 40.0f);

    /* Updates after finish are no-ops */
    lrg_wave_director_update (director, 1.0f, 0.0f, 0);
    g_assert_cmpuint (cap->events->len, ==, 8);

    capture_free (cap);
}

/* ==========================================================================
 * Test Cases - Interval Spawning
 * ========================================================================== */

static void
test_wave_director_interval_spawning (void)
{
    g_autoptr(LrgWaveDirector) director = NULL;
    Capture *cap;
    guint wave;

    director = lrg_wave_director_new ();
    cap = capture_new ();
    capture_connect (cap, director);

    wave = lrg_wave_director_add_wave (director, LRG_WAVE_TRIGGER_TIME, 0.0f);
    lrg_wave_director_wave_add_entry (director, wave, "drone", 3,
                                      LRG_WAVE_PATTERN_POINT,
                                      0.0f, 0.0f, 0.0f, 0.5f);

    lrg_wave_director_start (director);

    /* Trigger fires immediately; only the first spawn is dispatched */
    lrg_wave_director_update (director, 0.016f, 0.0f, 0);
    g_assert_cmpuint (capture_count_prefix (cap, "spawn:"), ==, 1);
    g_assert_false (lrg_wave_director_is_finished (director));

    /* 0.25s accumulated: not yet at the 0.5s interval */
    lrg_wave_director_update (director, 0.25f, 0.0f, 0);
    g_assert_cmpuint (capture_count_prefix (cap, "spawn:"), ==, 1);

    /* 0.5s accumulated: second spawn */
    lrg_wave_director_update (director, 0.25f, 0.0f, 0);
    g_assert_cmpuint (capture_count_prefix (cap, "spawn:"), ==, 2);
    g_assert_false (lrg_wave_director_is_finished (director));

    /* Another 0.5s: third and final spawn, wave completes */
    lrg_wave_director_update (director, 0.5f, 0.0f, 0);
    g_assert_cmpuint (capture_count_prefix (cap, "spawn:"), ==, 3);
    g_assert_true (lrg_wave_director_is_finished (director));

    capture_free (cap);
}

/* ==========================================================================
 * Test Cases - Cleared Trigger
 * ========================================================================== */

static void
test_wave_director_cleared_trigger (void)
{
    g_autoptr(LrgWaveDirector) director = NULL;
    Capture *cap;
    guint wave0;
    guint wave1;

    director = lrg_wave_director_new ();
    cap = capture_new ();
    capture_connect (cap, director);

    wave0 = lrg_wave_director_add_wave (director, LRG_WAVE_TRIGGER_TIME, 0.0f);
    lrg_wave_director_wave_add_entry (director, wave0, "grunt", 1,
                                      LRG_WAVE_PATTERN_POINT,
                                      0.0f, 0.0f, 0.0f, 0.0f);

    wave1 = lrg_wave_director_add_wave (director, LRG_WAVE_TRIGGER_CLEARED, 0.0f);
    lrg_wave_director_wave_add_entry (director, wave1, "elite", 1,
                                      LRG_WAVE_PATTERN_POINT,
                                      0.0f, 0.0f, 0.0f, 0.0f);

    lrg_wave_director_start (director);

    /* Wave 0 fires and completes immediately */
    lrg_wave_director_update (director, 0.016f, 0.0f, 4);
    g_assert_cmpuint (lrg_wave_director_get_current_wave (director), ==, 1);
    g_assert_cmpuint (capture_count_prefix (cap, "spawn:elite"), ==, 0);

    /* Wave 1 must wait while enemies remain */
    lrg_wave_director_update (director, 0.016f, 0.0f, 4);
    lrg_wave_director_update (director, 0.016f, 0.0f, 2);
    lrg_wave_director_update (director, 0.016f, 0.0f, 1);
    g_assert_cmpuint (capture_count_prefix (cap, "spawn:elite"), ==, 0);
    g_assert_false (lrg_wave_director_is_finished (director));

    /* active_count reaches zero: wave 1 fires */
    lrg_wave_director_update (director, 0.016f, 0.0f, 0);
    g_assert_cmpuint (capture_count_prefix (cap, "spawn:elite"), ==, 1);
    g_assert_true (lrg_wave_director_is_finished (director));

    capture_free (cap);
}

/* ==========================================================================
 * Test Cases - Progress Trigger
 * ========================================================================== */

static void
test_wave_director_progress_trigger (void)
{
    g_autoptr(LrgWaveDirector) director = NULL;
    Capture *cap;
    guint wave;

    director = lrg_wave_director_new ();
    cap = capture_new ();
    capture_connect (cap, director);

    wave = lrg_wave_director_add_wave (director, LRG_WAVE_TRIGGER_PROGRESS, 100.0f);
    lrg_wave_director_wave_add_entry (director, wave, "ambusher", 2,
                                      LRG_WAVE_PATTERN_POINT,
                                      5.0f, 5.0f, 0.0f, 0.0f);

    lrg_wave_director_start (director);

    /* Below the threshold: nothing happens no matter how long we wait */
    lrg_wave_director_update (director, 10.0f, 0.0f, 0);
    lrg_wave_director_update (director, 10.0f, 50.0f, 0);
    lrg_wave_director_update (director, 10.0f, 99.9f, 0);
    g_assert_cmpuint (cap->events->len, ==, 0);

    /* Threshold reached: wave fires */
    lrg_wave_director_update (director, 0.016f, 100.0f, 0);
    g_assert_cmpuint (capture_count_prefix (cap, "spawn:ambusher"), ==, 2);
    g_assert_true (lrg_wave_director_is_finished (director));

    capture_free (cap);
}

/* ==========================================================================
 * Test Cases - YAML Loading
 * ========================================================================== */

static const gchar *YAML_WAVES =
    "waves:\n"
    "  - trigger: {type: time, value: 2.0}\n"
    "    entries:\n"
    "      - {enemy: crumbsy, count: 6, pattern: edge-random, interval: 0.4}\n"
    "      - {enemy: turret, count: 2, pattern: point, x: 40.0, y: 60.0}\n"
    "      - {enemy: ring-squad, count: 8, pattern: ring, x: 240.0, y: 135.0, radius: 80.0}\n"
    "  - trigger: {type: cleared}\n"
    "    entries:\n"
    "      - {enemy: boss, count: 1, pattern: point, x: 240.0, y: 40.0}\n";

static void
test_wave_director_load_from_data (void)
{
    g_autoptr(LrgWaveDirector) director = NULL;
    g_autoptr(GError) error = NULL;
    Capture *cap;
    gboolean loaded;
    guint i;

    director = lrg_wave_director_new ();
    cap = capture_new ();
    capture_connect (cap, director);

    loaded = lrg_wave_director_load_from_data (director, YAML_WAVES, -1, &error);
    g_assert_no_error (error);
    g_assert_true (loaded);
    g_assert_cmpuint (lrg_wave_director_get_wave_count (director), ==, 2);

    lrg_wave_director_set_bounds (director, 0.0f, 0.0f, 480.0f, 270.0f);
    lrg_wave_director_start (director);

    /* Time trigger at 2.0 seconds */
    lrg_wave_director_update (director, 1.0f, 0.0f, 0);
    g_assert_cmpuint (cap->events->len, ==, 0);

    /* Fires: turret x2 and ring-squad x8 spawn at once, crumbsy 1 of 6 */
    lrg_wave_director_update (director, 1.0f, 0.0f, 0);
    g_assert_cmpuint (capture_count_prefix (cap, "started:0"), ==, 1);
    g_assert_cmpuint (capture_count_prefix (cap, "spawn:turret"), ==, 2);
    g_assert_cmpuint (capture_count_prefix (cap, "spawn:ring-squad"), ==, 8);
    g_assert_cmpuint (capture_count_prefix (cap, "spawn:crumbsy"), ==, 1);

    /* Remaining crumbsy spawns follow the 0.4s interval */
    for (i = 0; i < 5; i++)
        lrg_wave_director_update (director, 0.4f, 0.0f, 10);
    g_assert_cmpuint (capture_count_prefix (cap, "spawn:crumbsy"), ==, 6);
    g_assert_cmpuint (capture_count_prefix (cap, "completed:0"), ==, 1);

    /* Cleared-triggered boss wave */
    lrg_wave_director_update (director, 0.016f, 0.0f, 10);
    g_assert_cmpuint (capture_count_prefix (cap, "spawn:boss"), ==, 0);

    lrg_wave_director_update (director, 0.016f, 0.0f, 0);
    g_assert_cmpuint (capture_count_prefix (cap, "spawn:boss"), ==, 1);
    g_assert_true (lrg_wave_director_is_finished (director));

    capture_free (cap);
}

static void
test_wave_director_load_invalid_data (void)
{
    g_autoptr(LrgWaveDirector) director = NULL;
    g_autoptr(GError) error = NULL;
    gboolean loaded;

    director = lrg_wave_director_new ();

    /* Unknown trigger type must fail without adding any waves */
    loaded = lrg_wave_director_load_from_data (director,
                                               "waves:\n"
                                               "  - trigger: {type: bogus}\n",
                                               -1, &error);
    g_assert_false (loaded);
    g_assert_nonnull (error);
    g_assert_cmpuint (lrg_wave_director_get_wave_count (director), ==, 0);

    g_clear_error (&error);

    /* Missing 'waves' sequence must fail */
    loaded = lrg_wave_director_load_from_data (director, "other: 1\n", -1, &error);
    g_assert_false (loaded);
    g_assert_nonnull (error);
    g_assert_cmpuint (lrg_wave_director_get_wave_count (director), ==, 0);
}

/* ==========================================================================
 * Test Cases - Skip and Reset
 * ========================================================================== */

static void
test_wave_director_skip_to_wave (void)
{
    g_autoptr(LrgWaveDirector) director = NULL;
    Capture *cap;
    guint i;

    director = lrg_wave_director_new ();
    cap = capture_new ();
    capture_connect (cap, director);

    for (i = 0; i < 3; i++)
    {
        guint wave;

        wave = lrg_wave_director_add_wave (director, LRG_WAVE_TRIGGER_TIME, 10.0f);
        lrg_wave_director_wave_add_entry (director, wave, "grunt", 1,
                                          LRG_WAVE_PATTERN_POINT,
                                          0.0f, 0.0f, 0.0f, 0.0f);
    }

    lrg_wave_director_start (director);
    lrg_wave_director_update (director, 5.0f, 0.0f, 0);
    g_assert_cmpuint (cap->events->len, ==, 0);

    /* Checkpoint restore: jump straight to the last wave */
    lrg_wave_director_skip_to_wave (director, 2);
    g_assert_cmpuint (lrg_wave_director_get_current_wave (director), ==, 2);
    g_assert_false (lrg_wave_director_is_finished (director));

    /* Trigger state was reset: the earlier 5s must not count */
    lrg_wave_director_update (director, 5.0f, 0.0f, 0);
    g_assert_cmpuint (cap->events->len, ==, 0);

    /* Reaching 10s wave-local time fires wave 2 only */
    lrg_wave_director_update (director, 5.0f, 0.0f, 0);
    g_assert_cmpuint (capture_count_prefix (cap, "started:2"), ==, 1);
    g_assert_cmpuint (capture_count_prefix (cap, "spawn:grunt:2"), ==, 1);
    g_assert_cmpuint (capture_count_prefix (cap, "started:0"), ==, 0);
    g_assert_cmpuint (capture_count_prefix (cap, "started:1"), ==, 0);
    g_assert_true (lrg_wave_director_is_finished (director));

    capture_free (cap);
}

static void
test_wave_director_reset (void)
{
    g_autoptr(LrgWaveDirector) director = NULL;
    Capture *cap;
    guint wave;

    director = lrg_wave_director_new ();
    cap = capture_new ();
    capture_connect (cap, director);

    wave = lrg_wave_director_add_wave (director, LRG_WAVE_TRIGGER_TIME, 1.0f);
    lrg_wave_director_wave_add_entry (director, wave, "grunt", 2,
                                      LRG_WAVE_PATTERN_POINT,
                                      0.0f, 0.0f, 0.0f, 0.0f);

    /* Run the director to completion */
    lrg_wave_director_start (director);
    lrg_wave_director_update (director, 2.0f, 0.0f, 0);
    g_assert_true (lrg_wave_director_is_finished (director));
    g_assert_cmpuint (capture_count_prefix (cap, "spawn:"), ==, 2);

    /* Reset: back to pre-start, waves kept */
    lrg_wave_director_reset (director);
    g_assert_false (lrg_wave_director_is_started (director));
    g_assert_false (lrg_wave_director_is_finished (director));
    g_assert_cmpuint (lrg_wave_director_get_current_wave (director), ==, 0);
    g_assert_cmpuint (lrg_wave_director_get_wave_count (director), ==, 1);

    /* Update while stopped is a no-op */
    lrg_wave_director_update (director, 5.0f, 0.0f, 0);
    g_assert_cmpuint (capture_count_prefix (cap, "spawn:"), ==, 2);

    /* The director runs again from the beginning */
    lrg_wave_director_start (director);
    lrg_wave_director_update (director, 2.0f, 0.0f, 0);
    g_assert_true (lrg_wave_director_is_finished (director));
    g_assert_cmpuint (capture_count_prefix (cap, "spawn:"), ==, 4);

    capture_free (cap);
}

/* ==========================================================================
 * Test Cases - Spawn Patterns
 * ========================================================================== */

static void
test_wave_director_edge_patterns (void)
{
    g_autoptr(LrgWaveDirector) director = NULL;
    Capture *cap;
    guint wave;
    guint i;

    director = lrg_wave_director_new ();
    cap = capture_new ();
    capture_connect (cap, director);

    lrg_wave_director_set_bounds (director, 0.0f, 0.0f, 320.0f, 240.0f);

    wave = lrg_wave_director_add_wave (director, LRG_WAVE_TRIGGER_TIME, 0.0f);
    lrg_wave_director_wave_add_entry (director, wave, "top", 20,
                                      LRG_WAVE_PATTERN_EDGE_TOP,
                                      0.0f, 0.0f, 0.0f, 0.0f);
    lrg_wave_director_wave_add_entry (director, wave, "any", 50,
                                      LRG_WAVE_PATTERN_EDGE_RANDOM,
                                      0.0f, 0.0f, 0.0f, 0.0f);

    lrg_wave_director_start (director);
    lrg_wave_director_update (director, 0.016f, 0.0f, 0);

    g_assert_cmpuint (capture_count_prefix (cap, "spawn:"), ==, 70);
    g_assert_true (lrg_wave_director_is_finished (director));

    for (i = 0; i < cap->xs->len; i++)
    {
        gfloat x;
        gfloat y;

        x = g_array_index (cap->xs, gfloat, i);
        y = g_array_index (cap->ys, gfloat, i);

        /* Every edge spawn stays inside the bounds rect */
        g_assert_cmpfloat (x, >=, 0.0f);
        g_assert_cmpfloat (x, <=, 320.0f);
        g_assert_cmpfloat (y, >=, 0.0f);
        g_assert_cmpfloat (y, <=, 240.0f);
    }

    /* The first 20 spawns came from the top edge entry */
    for (i = 0; i < 20; i++)
        g_assert_cmpfloat (g_array_index (cap->ys, gfloat, i), ==, 0.0f);

    capture_free (cap);
}

static void
test_wave_director_ring_pattern (void)
{
    g_autoptr(LrgWaveDirector) director = NULL;
    Capture *cap;
    guint wave;
    guint i;

    director = lrg_wave_director_new ();
    cap = capture_new ();
    capture_connect (cap, director);

    wave = lrg_wave_director_add_wave (director, LRG_WAVE_TRIGGER_TIME, 0.0f);
    lrg_wave_director_wave_add_entry (director, wave, "orb", 32,
                                      LRG_WAVE_PATTERN_RING,
                                      100.0f, 100.0f, 50.0f, 0.0f);

    lrg_wave_director_start (director);
    lrg_wave_director_update (director, 0.016f, 0.0f, 0);

    g_assert_cmpuint (capture_count_prefix (cap, "spawn:orb"), ==, 32);

    for (i = 0; i < cap->xs->len; i++)
    {
        gfloat x;
        gfloat y;
        gdouble dist;

        x = g_array_index (cap->xs, gfloat, i);
        y = g_array_index (cap->ys, gfloat, i);

        /* Within the ring's bounding box */
        g_assert_cmpfloat (x, >=, 50.0f - 0.01f);
        g_assert_cmpfloat (x, <=, 150.0f + 0.01f);
        g_assert_cmpfloat (y, >=, 50.0f - 0.01f);
        g_assert_cmpfloat (y, <=, 150.0f + 0.01f);

        /* On the circle: distance from center equals the radius */
        dist = sqrt ((x - 100.0) * (x - 100.0) + (y - 100.0) * (y - 100.0));
        g_assert_cmpfloat (dist, >, 49.9);
        g_assert_cmpfloat (dist, <, 50.1);
    }

    capture_free (cap);
}

static void
test_wave_director_line_pattern (void)
{
    g_autoptr(LrgWaveDirector) director = NULL;
    Capture *cap;
    guint wave;
    guint i;
    gfloat expected_xs[5];

    director = lrg_wave_director_new ();
    cap = capture_new ();
    capture_connect (cap, director);

    wave = lrg_wave_director_add_wave (director, LRG_WAVE_TRIGGER_TIME, 0.0f);
    lrg_wave_director_wave_add_entry (director, wave, "wall", 5,
                                      LRG_WAVE_PATTERN_LINE,
                                      100.0f, 50.0f, 10.0f, 0.0f);

    lrg_wave_director_start (director);
    lrg_wave_director_update (director, 0.016f, 0.0f, 0);

    g_assert_cmpuint (capture_count_prefix (cap, "spawn:wall"), ==, 5);

    /* Evenly spaced, centered on x=100 with spacing 10 */
    expected_xs[0] = 80.0f;
    expected_xs[1] = 90.0f;
    expected_xs[2] = 100.0f;
    expected_xs[3] = 110.0f;
    expected_xs[4] = 120.0f;

    for (i = 0; i < 5; i++)
    {
        g_assert_cmpfloat (g_array_index (cap->xs, gfloat, i), ==, expected_xs[i]);
        g_assert_cmpfloat (g_array_index (cap->ys, gfloat, i), ==, 50.0f);
    }

    capture_free (cap);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Construction tests */
    g_test_add_func ("/wave-director/new",
                     test_wave_director_new);

    /* Sequencing tests */
    g_test_add_func ("/wave-director/programmatic-ordering",
                     test_wave_director_programmatic_ordering);
    g_test_add_func ("/wave-director/interval-spawning",
                     test_wave_director_interval_spawning);
    g_test_add_func ("/wave-director/cleared-trigger",
                     test_wave_director_cleared_trigger);
    g_test_add_func ("/wave-director/progress-trigger",
                     test_wave_director_progress_trigger);

    /* Loading tests */
    g_test_add_func ("/wave-director/load-from-data",
                     test_wave_director_load_from_data);
    g_test_add_func ("/wave-director/load-invalid-data",
                     test_wave_director_load_invalid_data);

    /* Lifecycle tests */
    g_test_add_func ("/wave-director/skip-to-wave",
                     test_wave_director_skip_to_wave);
    g_test_add_func ("/wave-director/reset",
                     test_wave_director_reset);

    /* Pattern tests */
    g_test_add_func ("/wave-director/edge-patterns",
                     test_wave_director_edge_patterns);
    g_test_add_func ("/wave-director/ring-pattern",
                     test_wave_director_ring_pattern);
    g_test_add_func ("/wave-director/line-pattern",
                     test_wave_director_line_pattern);

    return g_test_run ();
}
