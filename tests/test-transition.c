/* test-transition.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the transition module.
 */

#include <glib.h>
#include "../src/libregnum.h"

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgTransitionManager *manager;
} TransitionFixture;

static void
transition_fixture_set_up (TransitionFixture *fixture,
                           gconstpointer      user_data)
{
    (void) user_data;

    fixture->manager = lrg_transition_manager_new ();
    lrg_transition_manager_initialize (fixture->manager, 1280, 720, NULL);
}

static void
transition_fixture_tear_down (TransitionFixture *fixture,
                              gconstpointer      user_data)
{
    (void) user_data;

    lrg_transition_manager_shutdown (fixture->manager);
    g_object_unref (fixture->manager);
}

/* ==========================================================================
 * Base Transition Tests
 * ========================================================================== */

static void
test_fade_transition_new (TransitionFixture *fixture,
                          gconstpointer      user_data)
{
    g_autoptr(LrgFadeTransition) fade = NULL;
    guint8 r, g, b;

    (void) fixture;
    (void) user_data;

    fade = lrg_fade_transition_new ();
    g_assert_nonnull (fade);
    g_assert_true (LRG_IS_FADE_TRANSITION (fade));
    g_assert_true (LRG_IS_TRANSITION (fade));

    /* Default color should be black */
    lrg_fade_transition_get_color (fade, &r, &g, &b);
    g_assert_cmpuint (r, ==, 0);
    g_assert_cmpuint (g, ==, 0);
    g_assert_cmpuint (b, ==, 0);
}

static void
test_fade_transition_with_color (TransitionFixture *fixture,
                                 gconstpointer      user_data)
{
    g_autoptr(LrgFadeTransition) fade = NULL;
    guint8 r, g, b;

    (void) fixture;
    (void) user_data;

    fade = lrg_fade_transition_new_with_color (255, 128, 64);
    g_assert_nonnull (fade);

    lrg_fade_transition_get_color (fade, &r, &g, &b);
    g_assert_cmpuint (r, ==, 255);
    g_assert_cmpuint (g, ==, 128);
    g_assert_cmpuint (b, ==, 64);
}

static void
test_fade_transition_set_color (TransitionFixture *fixture,
                                gconstpointer      user_data)
{
    g_autoptr(LrgFadeTransition) fade = NULL;
    guint8 r, g, b;

    (void) fixture;
    (void) user_data;

    fade = lrg_fade_transition_new ();
    lrg_fade_transition_set_color (fade, 100, 150, 200);

    lrg_fade_transition_get_color (fade, &r, &g, &b);
    g_assert_cmpuint (r, ==, 100);
    g_assert_cmpuint (g, ==, 150);
    g_assert_cmpuint (b, ==, 200);
}

static void
test_wipe_transition_new (TransitionFixture *fixture,
                          gconstpointer      user_data)
{
    g_autoptr(LrgWipeTransition) wipe = NULL;

    (void) fixture;
    (void) user_data;

    wipe = lrg_wipe_transition_new ();
    g_assert_nonnull (wipe);
    g_assert_true (LRG_IS_WIPE_TRANSITION (wipe));

    /* Default direction should be left */
    g_assert_cmpint (lrg_wipe_transition_get_direction (wipe), ==, LRG_TRANSITION_DIRECTION_LEFT);
    g_assert_cmpfloat (lrg_wipe_transition_get_softness (wipe), ==, 0.0f);
}

static void
test_wipe_transition_with_direction (TransitionFixture *fixture,
                                     gconstpointer      user_data)
{
    g_autoptr(LrgWipeTransition) wipe = NULL;

    (void) fixture;
    (void) user_data;

    wipe = lrg_wipe_transition_new_with_direction (LRG_TRANSITION_DIRECTION_UP);
    g_assert_nonnull (wipe);
    g_assert_cmpint (lrg_wipe_transition_get_direction (wipe), ==, LRG_TRANSITION_DIRECTION_UP);
}

static void
test_dissolve_transition_new (TransitionFixture *fixture,
                              gconstpointer      user_data)
{
    g_autoptr(LrgDissolveTransition) dissolve = NULL;

    (void) fixture;
    (void) user_data;

    dissolve = lrg_dissolve_transition_new ();
    g_assert_nonnull (dissolve);
    g_assert_true (LRG_IS_DISSOLVE_TRANSITION (dissolve));

    g_assert_cmpfloat (lrg_dissolve_transition_get_noise_scale (dissolve), ==, 8.0f);
    g_assert_cmpfloat (lrg_dissolve_transition_get_edge_width (dissolve), ==, 0.0f);
}

static void
test_dissolve_transition_edge_color (TransitionFixture *fixture,
                                     gconstpointer      user_data)
{
    g_autoptr(LrgDissolveTransition) dissolve = NULL;
    guint8 r, g, b;

    (void) fixture;
    (void) user_data;

    dissolve = lrg_dissolve_transition_new ();
    lrg_dissolve_transition_set_edge_color (dissolve, 255, 128, 0);

    lrg_dissolve_transition_get_edge_color (dissolve, &r, &g, &b);
    g_assert_cmpuint (r, ==, 255);
    g_assert_cmpuint (g, ==, 128);
    g_assert_cmpuint (b, ==, 0);
}

static void
test_slide_transition_new (TransitionFixture *fixture,
                           gconstpointer      user_data)
{
    g_autoptr(LrgSlideTransition) slide = NULL;

    (void) fixture;
    (void) user_data;

    slide = lrg_slide_transition_new ();
    g_assert_nonnull (slide);
    g_assert_true (LRG_IS_SLIDE_TRANSITION (slide));

    g_assert_cmpint (lrg_slide_transition_get_direction (slide), ==, LRG_TRANSITION_DIRECTION_LEFT);
    g_assert_cmpint (lrg_slide_transition_get_mode (slide), ==, LRG_SLIDE_MODE_PUSH);
}

static void
test_slide_transition_with_options (TransitionFixture *fixture,
                                    gconstpointer      user_data)
{
    g_autoptr(LrgSlideTransition) slide = NULL;

    (void) fixture;
    (void) user_data;

    slide = lrg_slide_transition_new_with_options (LRG_TRANSITION_DIRECTION_RIGHT,
                                                   LRG_SLIDE_MODE_COVER);
    g_assert_nonnull (slide);
    g_assert_cmpint (lrg_slide_transition_get_direction (slide), ==, LRG_TRANSITION_DIRECTION_RIGHT);
    g_assert_cmpint (lrg_slide_transition_get_mode (slide), ==, LRG_SLIDE_MODE_COVER);
}

static void
test_zoom_transition_new (TransitionFixture *fixture,
                          gconstpointer      user_data)
{
    g_autoptr(LrgZoomTransition) zoom = NULL;

    (void) fixture;
    (void) user_data;

    zoom = lrg_zoom_transition_new ();
    g_assert_nonnull (zoom);
    g_assert_true (LRG_IS_ZOOM_TRANSITION (zoom));

    g_assert_cmpint (lrg_zoom_transition_get_direction (zoom), ==, LRG_ZOOM_DIRECTION_IN);
    g_assert_cmpfloat (lrg_zoom_transition_get_scale (zoom), ==, 2.0f);
    g_assert_cmpfloat (lrg_zoom_transition_get_center_x (zoom), ==, 0.5f);
    g_assert_cmpfloat (lrg_zoom_transition_get_center_y (zoom), ==, 0.5f);
}

static void
test_zoom_transition_set_center (TransitionFixture *fixture,
                                 gconstpointer      user_data)
{
    g_autoptr(LrgZoomTransition) zoom = NULL;

    (void) fixture;
    (void) user_data;

    zoom = lrg_zoom_transition_new ();
    lrg_zoom_transition_set_center (zoom, 0.25f, 0.75f);

    g_assert_cmpfloat (lrg_zoom_transition_get_center_x (zoom), ==, 0.25f);
    g_assert_cmpfloat (lrg_zoom_transition_get_center_y (zoom), ==, 0.75f);
}

static void
test_shader_transition_new (TransitionFixture *fixture,
                            gconstpointer      user_data)
{
    g_autoptr(LrgShaderTransition) shader = NULL;

    (void) fixture;
    (void) user_data;

    shader = lrg_shader_transition_new ();
    g_assert_nonnull (shader);
    g_assert_true (LRG_IS_SHADER_TRANSITION (shader));
    g_assert_false (lrg_shader_transition_is_shader_loaded (shader));
}

static void
test_shader_transition_load_source (TransitionFixture *fixture,
                                    gconstpointer      user_data)
{
    g_autoptr(LrgShaderTransition) shader = NULL;
    g_autoptr(GError) error = NULL;
    const gchar *source = "void main() { gl_FragColor = vec4(1.0); }";
    gboolean result;

    (void) fixture;
    (void) user_data;

    shader = lrg_shader_transition_new ();
    result = lrg_shader_transition_load_from_source (shader, source, &error);

    g_assert_true (result);
    g_assert_no_error (error);
    g_assert_true (lrg_shader_transition_is_shader_loaded (shader));
}

/* ==========================================================================
 * Transition State Tests
 * ========================================================================== */

static void
test_transition_initial_state (TransitionFixture *fixture,
                               gconstpointer      user_data)
{
    g_autoptr(LrgFadeTransition) fade = NULL;

    (void) fixture;
    (void) user_data;

    fade = lrg_fade_transition_new ();
    g_assert_cmpint (lrg_transition_get_state (LRG_TRANSITION (fade)), ==, LRG_TRANSITION_STATE_IDLE);
    g_assert_false (lrg_transition_is_complete (LRG_TRANSITION (fade)));
    g_assert_false (lrg_transition_is_at_midpoint (LRG_TRANSITION (fade)));
}

static void
test_transition_start (TransitionFixture *fixture,
                       gconstpointer      user_data)
{
    g_autoptr(LrgFadeTransition) fade = NULL;

    (void) user_data;

    fade = lrg_fade_transition_new ();
    lrg_transition_manager_start (fixture->manager, LRG_TRANSITION (fade));

    g_assert_true (lrg_transition_manager_is_active (fixture->manager));
    g_assert_cmpint (lrg_transition_get_state (LRG_TRANSITION (fade)), ==, LRG_TRANSITION_STATE_OUT);
}

static void
test_transition_update_progress (TransitionFixture *fixture,
                                 gconstpointer      user_data)
{
    g_autoptr(LrgFadeTransition) fade = NULL;
    gfloat progress_before;
    gfloat progress_after;

    (void) user_data;

    fade = lrg_fade_transition_new ();
    lrg_transition_set_duration (LRG_TRANSITION (fade), 1.0f);
    lrg_transition_manager_start (fixture->manager, LRG_TRANSITION (fade));

    progress_before = lrg_transition_get_progress (LRG_TRANSITION (fade));

    lrg_transition_manager_update (fixture->manager, 0.25f);

    progress_after = lrg_transition_get_progress (LRG_TRANSITION (fade));
    g_assert_cmpfloat (progress_after, >, progress_before);
}

static void
test_transition_phases (TransitionFixture *fixture,
                        gconstpointer      user_data)
{
    g_autoptr(LrgFadeTransition) fade = NULL;
    LrgTransitionState state;

    (void) user_data;

    fade = lrg_fade_transition_new ();
    lrg_transition_set_out_duration (LRG_TRANSITION (fade), 0.5f);
    lrg_transition_set_hold_duration (LRG_TRANSITION (fade), 0.2f);
    lrg_transition_set_in_duration (LRG_TRANSITION (fade), 0.5f);

    lrg_transition_manager_start (fixture->manager, LRG_TRANSITION (fade));

    /* Start in OUT phase */
    state = lrg_transition_get_state (LRG_TRANSITION (fade));
    g_assert_cmpint (state, ==, LRG_TRANSITION_STATE_OUT);

    /* Update past OUT phase into HOLD */
    lrg_transition_manager_update (fixture->manager, 0.55f);
    state = lrg_transition_get_state (LRG_TRANSITION (fade));
    g_assert_cmpint (state, ==, LRG_TRANSITION_STATE_HOLD);
    g_assert_true (lrg_transition_is_at_midpoint (LRG_TRANSITION (fade)));

    /* Update past HOLD into IN */
    lrg_transition_manager_update (fixture->manager, 0.25f);
    state = lrg_transition_get_state (LRG_TRANSITION (fade));
    g_assert_cmpint (state, ==, LRG_TRANSITION_STATE_IN);

    /* Update to completion */
    lrg_transition_manager_update (fixture->manager, 0.5f);
    state = lrg_transition_get_state (LRG_TRANSITION (fade));
    g_assert_cmpint (state, ==, LRG_TRANSITION_STATE_COMPLETE);
    g_assert_true (lrg_transition_is_complete (LRG_TRANSITION (fade)));
}

static void
test_transition_reset (TransitionFixture *fixture,
                       gconstpointer      user_data)
{
    g_autoptr(LrgFadeTransition) fade = NULL;

    (void) user_data;

    fade = lrg_fade_transition_new ();
    lrg_transition_manager_start (fixture->manager, LRG_TRANSITION (fade));

    /* Update a bit */
    lrg_transition_manager_update (fixture->manager, 0.5f);
    g_assert_cmpfloat (lrg_transition_get_progress (LRG_TRANSITION (fade)), >, 0.0f);

    /* Reset */
    lrg_transition_reset (LRG_TRANSITION (fade));
    g_assert_cmpint (lrg_transition_get_state (LRG_TRANSITION (fade)), ==, LRG_TRANSITION_STATE_IDLE);
}

/* ==========================================================================
 * Transition Timing Tests
 * ========================================================================== */

static void
test_transition_duration (TransitionFixture *fixture,
                          gconstpointer      user_data)
{
    g_autoptr(LrgFadeTransition) fade = NULL;
    gfloat total;

    (void) fixture;
    (void) user_data;

    fade = lrg_fade_transition_new ();

    /* Default durations: 0.5 + 0.0 + 0.5 = 1.0 */
    total = lrg_transition_get_duration (LRG_TRANSITION (fade));
    g_assert_cmpfloat (total, ==, 1.0f);

    /* Set individual durations */
    lrg_transition_set_out_duration (LRG_TRANSITION (fade), 0.3f);
    lrg_transition_set_hold_duration (LRG_TRANSITION (fade), 0.1f);
    lrg_transition_set_in_duration (LRG_TRANSITION (fade), 0.4f);

    total = lrg_transition_get_duration (LRG_TRANSITION (fade));
    g_assert_cmpfloat_with_epsilon (total, 0.8f, 0.001f);
}

static void
test_transition_set_duration_proportional (TransitionFixture *fixture,
                                           gconstpointer      user_data)
{
    g_autoptr(LrgFadeTransition) fade = NULL;
    gfloat out_dur;
    gfloat hold_dur;
    gfloat in_dur;

    (void) fixture;
    (void) user_data;

    fade = lrg_fade_transition_new ();

    /* Set to 0.5 + 0.0 + 0.5 = 1.0 default */
    /* Now set total to 2.0, should double all */
    lrg_transition_set_duration (LRG_TRANSITION (fade), 2.0f);

    out_dur = lrg_transition_get_out_duration (LRG_TRANSITION (fade));
    hold_dur = lrg_transition_get_hold_duration (LRG_TRANSITION (fade));
    in_dur = lrg_transition_get_in_duration (LRG_TRANSITION (fade));

    g_assert_cmpfloat (out_dur, ==, 1.0f);
    g_assert_cmpfloat (hold_dur, ==, 0.0f);
    g_assert_cmpfloat (in_dur, ==, 1.0f);
}

static void
test_transition_easing (TransitionFixture *fixture,
                        gconstpointer      user_data)
{
    g_autoptr(LrgFadeTransition) fade = NULL;

    (void) fixture;
    (void) user_data;

    fade = lrg_fade_transition_new ();

    /* Default is linear */
    g_assert_cmpint (lrg_transition_get_easing (LRG_TRANSITION (fade)), ==, LRG_EASING_LINEAR);

    /* Set to cubic */
    lrg_transition_set_easing (LRG_TRANSITION (fade), LRG_EASING_EASE_IN_OUT_CUBIC);
    g_assert_cmpint (lrg_transition_get_easing (LRG_TRANSITION (fade)), ==, LRG_EASING_EASE_IN_OUT_CUBIC);
}

/* ==========================================================================
 * Transition Manager Tests
 * ========================================================================== */

static void
test_manager_new (TransitionFixture *fixture,
                  gconstpointer      user_data)
{
    (void) user_data;

    g_assert_nonnull (fixture->manager);
    g_assert_true (LRG_IS_TRANSITION_MANAGER (fixture->manager));
}

static void
test_manager_viewport (TransitionFixture *fixture,
                       gconstpointer      user_data)
{
    (void) user_data;

    g_assert_cmpuint (lrg_transition_manager_get_viewport_width (fixture->manager), ==, 1280);
    g_assert_cmpuint (lrg_transition_manager_get_viewport_height (fixture->manager), ==, 720);

    lrg_transition_manager_set_viewport (fixture->manager, 1920, 1080);
    g_assert_cmpuint (lrg_transition_manager_get_viewport_width (fixture->manager), ==, 1920);
    g_assert_cmpuint (lrg_transition_manager_get_viewport_height (fixture->manager), ==, 1080);
}

static void
test_manager_no_active (TransitionFixture *fixture,
                        gconstpointer      user_data)
{
    (void) user_data;

    g_assert_false (lrg_transition_manager_is_active (fixture->manager));
    g_assert_null (lrg_transition_manager_get_current (fixture->manager));
    g_assert_cmpint (lrg_transition_manager_get_state (fixture->manager), ==, LRG_TRANSITION_STATE_IDLE);
}

static void
test_manager_start_transition (TransitionFixture *fixture,
                               gconstpointer      user_data)
{
    g_autoptr(LrgFadeTransition) fade = NULL;

    (void) user_data;

    fade = lrg_fade_transition_new ();
    lrg_transition_manager_start (fixture->manager, LRG_TRANSITION (fade));

    g_assert_true (lrg_transition_manager_is_active (fixture->manager));
    g_assert_nonnull (lrg_transition_manager_get_current (fixture->manager));
    g_assert_true (lrg_transition_manager_get_current (fixture->manager) == LRG_TRANSITION (fade));
}

static void
test_manager_cancel (TransitionFixture *fixture,
                     gconstpointer      user_data)
{
    g_autoptr(LrgFadeTransition) fade = NULL;

    (void) user_data;

    fade = lrg_fade_transition_new ();
    lrg_transition_manager_start (fixture->manager, LRG_TRANSITION (fade));
    g_assert_true (lrg_transition_manager_is_active (fixture->manager));

    lrg_transition_manager_cancel (fixture->manager);
    g_assert_false (lrg_transition_manager_is_active (fixture->manager));
    g_assert_null (lrg_transition_manager_get_current (fixture->manager));
}

static void
test_manager_auto_cleanup (TransitionFixture *fixture,
                           gconstpointer      user_data)
{
    g_autoptr(LrgFadeTransition) fade = NULL;

    (void) user_data;

    fade = lrg_fade_transition_new ();
    lrg_transition_set_duration (LRG_TRANSITION (fade), 0.1f);
    lrg_transition_manager_start (fixture->manager, LRG_TRANSITION (fade));

    /* Update past completion */
    lrg_transition_manager_update (fixture->manager, 0.2f);

    /* Manager should clean up completed transition */
    g_assert_false (lrg_transition_manager_is_active (fixture->manager));
}

/* ==========================================================================
 * Signal Tests
 * ========================================================================== */

typedef struct
{
    gboolean started_fired;
    gboolean midpoint_fired;
    gboolean completed_fired;
} SignalData;

static void
on_started (LrgTransition *transition,
            gpointer       user_data)
{
    SignalData *data;

    (void) transition;
    data = (SignalData *) user_data;
    data->started_fired = TRUE;
}

static void
on_midpoint (LrgTransition *transition,
             gpointer       user_data)
{
    SignalData *data;

    (void) transition;
    data = (SignalData *) user_data;
    data->midpoint_fired = TRUE;
}

static void
on_completed (LrgTransition *transition,
              gpointer       user_data)
{
    SignalData *data;

    (void) transition;
    data = (SignalData *) user_data;
    data->completed_fired = TRUE;
}

static void
test_transition_signals (TransitionFixture *fixture,
                         gconstpointer      user_data)
{
    g_autoptr(LrgFadeTransition) fade = NULL;
    SignalData data = { FALSE, FALSE, FALSE };

    (void) user_data;

    fade = lrg_fade_transition_new ();
    lrg_transition_set_duration (LRG_TRANSITION (fade), 0.5f);

    g_signal_connect (fade, "started", G_CALLBACK (on_started), &data);
    g_signal_connect (fade, "midpoint-reached", G_CALLBACK (on_midpoint), &data);
    g_signal_connect (fade, "completed", G_CALLBACK (on_completed), &data);

    /* Start should fire started signal */
    lrg_transition_manager_start (fixture->manager, LRG_TRANSITION (fade));
    g_assert_true (data.started_fired);
    g_assert_false (data.midpoint_fired);
    g_assert_false (data.completed_fired);

    /* Update past midpoint */
    lrg_transition_manager_update (fixture->manager, 0.3f);
    g_assert_true (data.midpoint_fired);
    g_assert_false (data.completed_fired);

    /* Update to completion */
    lrg_transition_manager_update (fixture->manager, 0.3f);
    g_assert_true (data.completed_fired);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Fade transition tests */
    g_test_add ("/transition/fade/new",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_fade_transition_new,
                transition_fixture_tear_down);

    g_test_add ("/transition/fade/with-color",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_fade_transition_with_color,
                transition_fixture_tear_down);

    g_test_add ("/transition/fade/set-color",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_fade_transition_set_color,
                transition_fixture_tear_down);

    /* Wipe transition tests */
    g_test_add ("/transition/wipe/new",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_wipe_transition_new,
                transition_fixture_tear_down);

    g_test_add ("/transition/wipe/with-direction",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_wipe_transition_with_direction,
                transition_fixture_tear_down);

    /* Dissolve transition tests */
    g_test_add ("/transition/dissolve/new",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_dissolve_transition_new,
                transition_fixture_tear_down);

    g_test_add ("/transition/dissolve/edge-color",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_dissolve_transition_edge_color,
                transition_fixture_tear_down);

    /* Slide transition tests */
    g_test_add ("/transition/slide/new",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_slide_transition_new,
                transition_fixture_tear_down);

    g_test_add ("/transition/slide/with-options",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_slide_transition_with_options,
                transition_fixture_tear_down);

    /* Zoom transition tests */
    g_test_add ("/transition/zoom/new",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_zoom_transition_new,
                transition_fixture_tear_down);

    g_test_add ("/transition/zoom/set-center",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_zoom_transition_set_center,
                transition_fixture_tear_down);

    /* Shader transition tests */
    g_test_add ("/transition/shader/new",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_shader_transition_new,
                transition_fixture_tear_down);

    g_test_add ("/transition/shader/load-source",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_shader_transition_load_source,
                transition_fixture_tear_down);

    /* State tests */
    g_test_add ("/transition/state/initial",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_transition_initial_state,
                transition_fixture_tear_down);

    g_test_add ("/transition/state/start",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_transition_start,
                transition_fixture_tear_down);

    g_test_add ("/transition/state/update-progress",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_transition_update_progress,
                transition_fixture_tear_down);

    g_test_add ("/transition/state/phases",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_transition_phases,
                transition_fixture_tear_down);

    g_test_add ("/transition/state/reset",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_transition_reset,
                transition_fixture_tear_down);

    /* Timing tests */
    g_test_add ("/transition/timing/duration",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_transition_duration,
                transition_fixture_tear_down);

    g_test_add ("/transition/timing/set-duration-proportional",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_transition_set_duration_proportional,
                transition_fixture_tear_down);

    g_test_add ("/transition/timing/easing",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_transition_easing,
                transition_fixture_tear_down);

    /* Manager tests */
    g_test_add ("/transition/manager/new",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_manager_new,
                transition_fixture_tear_down);

    g_test_add ("/transition/manager/viewport",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_manager_viewport,
                transition_fixture_tear_down);

    g_test_add ("/transition/manager/no-active",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_manager_no_active,
                transition_fixture_tear_down);

    g_test_add ("/transition/manager/start-transition",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_manager_start_transition,
                transition_fixture_tear_down);

    g_test_add ("/transition/manager/cancel",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_manager_cancel,
                transition_fixture_tear_down);

    g_test_add ("/transition/manager/auto-cleanup",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_manager_auto_cleanup,
                transition_fixture_tear_down);

    /* Signal tests */
    g_test_add ("/transition/signals/lifecycle",
                TransitionFixture, NULL,
                transition_fixture_set_up,
                test_transition_signals,
                transition_fixture_tear_down);

    return g_test_run ();
}
