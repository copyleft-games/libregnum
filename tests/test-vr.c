/* test-vr.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for VR support module.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgVRService *service;
    LrgVRComfortSettings *comfort;
} VRFixture;

static void
fixture_set_up (VRFixture     *fixture,
                gconstpointer  user_data)
{
    fixture->service = LRG_VR_SERVICE (lrg_vr_stub_new ());
    fixture->comfort = lrg_vr_comfort_settings_new ();
}

static void
fixture_tear_down (VRFixture     *fixture,
                   gconstpointer  user_data)
{
    g_clear_object (&fixture->service);
    g_clear_object (&fixture->comfort);
}

/* ==========================================================================
 * LrgVRService Interface Tests
 * ========================================================================== */

static void
test_vr_service_stub_new (VRFixture     *fixture,
                          gconstpointer  user_data)
{
    g_assert_nonnull (fixture->service);
    g_assert_true (LRG_IS_VR_SERVICE (fixture->service));
    g_assert_true (LRG_IS_VR_STUB (fixture->service));
}

static void
test_vr_service_stub_is_available (VRFixture     *fixture,
                                   gconstpointer  user_data)
{
    /* Stub should report VR as not available */
    g_assert_false (lrg_vr_service_is_available (fixture->service));
}

static void
test_vr_service_stub_is_hmd_present (VRFixture     *fixture,
                                     gconstpointer  user_data)
{
    /* Stub should report no HMD present */
    g_assert_false (lrg_vr_service_is_hmd_present (fixture->service));
}

static void
test_vr_service_stub_initialize (VRFixture     *fixture,
                                 gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    gboolean result;

    /* Stub should fail initialization with appropriate error */
    result = lrg_vr_service_initialize (fixture->service, &error);

    g_assert_false (result);
    g_assert_nonnull (error);
    g_assert_error (error, LRG_VR_ERROR, LRG_VR_ERROR_NOT_AVAILABLE);
}

static void
test_vr_service_stub_shutdown (VRFixture     *fixture,
                               gconstpointer  user_data)
{
    /* Shutdown should not crash */
    lrg_vr_service_shutdown (fixture->service);
}

static void
test_vr_service_stub_poll_events (VRFixture     *fixture,
                                  gconstpointer  user_data)
{
    /* Poll events should not crash */
    lrg_vr_service_poll_events (fixture->service);
}

static void
test_vr_service_stub_render_size (VRFixture     *fixture,
                                  gconstpointer  user_data)
{
    guint width = 0;
    guint height = 0;

    lrg_vr_service_get_recommended_render_size (fixture->service, &width, &height);

    /* Stub should return reasonable defaults */
    g_assert_cmpuint (width, ==, 1024);
    g_assert_cmpuint (height, ==, 1024);
}

static void
test_vr_service_stub_eye_projection (VRFixture     *fixture,
                                     gconstpointer  user_data)
{
    gfloat matrix[16];
    gint i;

    lrg_vr_service_get_eye_projection (fixture->service, LRG_VR_EYE_LEFT,
                                       0.1f, 100.0f, matrix);

    /* Stub should return identity matrix */
    for (i = 0; i < 16; i++)
    {
        if (i % 5 == 0)
            g_assert_cmpfloat (matrix[i], ==, 1.0f);
        else
            g_assert_cmpfloat (matrix[i], ==, 0.0f);
    }
}

static void
test_vr_service_stub_eye_to_head (VRFixture     *fixture,
                                  gconstpointer  user_data)
{
    gfloat matrix[16];
    gint i;

    lrg_vr_service_get_eye_to_head (fixture->service, LRG_VR_EYE_RIGHT, matrix);

    /* Stub should return identity matrix */
    for (i = 0; i < 16; i++)
    {
        if (i % 5 == 0)
            g_assert_cmpfloat (matrix[i], ==, 1.0f);
        else
            g_assert_cmpfloat (matrix[i], ==, 0.0f);
    }
}

static void
test_vr_service_stub_hmd_pose (VRFixture     *fixture,
                               gconstpointer  user_data)
{
    gfloat matrix[16];
    gint i;

    lrg_vr_service_get_hmd_pose (fixture->service, matrix);

    /* Stub should return identity matrix */
    for (i = 0; i < 16; i++)
    {
        if (i % 5 == 0)
            g_assert_cmpfloat (matrix[i], ==, 1.0f);
        else
            g_assert_cmpfloat (matrix[i], ==, 0.0f);
    }
}

static void
test_vr_service_stub_submit_frame (VRFixture     *fixture,
                                   gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    gboolean result;

    /* Stub should fail frame submission */
    result = lrg_vr_service_submit_frame (fixture->service, LRG_VR_EYE_LEFT, 1, &error);

    g_assert_false (result);
    g_assert_nonnull (error);
    g_assert_error (error, LRG_VR_ERROR, LRG_VR_ERROR_COMPOSITOR);
}

static void
test_vr_service_stub_controller_pose (VRFixture     *fixture,
                                      gconstpointer  user_data)
{
    gfloat matrix[16];
    gint i;

    lrg_vr_service_get_controller_pose (fixture->service, LRG_VR_HAND_LEFT, matrix);

    /* Stub should return identity matrix */
    for (i = 0; i < 16; i++)
    {
        if (i % 5 == 0)
            g_assert_cmpfloat (matrix[i], ==, 1.0f);
        else
            g_assert_cmpfloat (matrix[i], ==, 0.0f);
    }
}

static void
test_vr_service_stub_controller_buttons (VRFixture     *fixture,
                                         gconstpointer  user_data)
{
    guint buttons;

    buttons = lrg_vr_service_get_controller_buttons (fixture->service, LRG_VR_HAND_RIGHT);

    /* Stub should return no buttons pressed */
    g_assert_cmpuint (buttons, ==, 0);
}

static void
test_vr_service_stub_controller_axis (VRFixture     *fixture,
                                      gconstpointer  user_data)
{
    gfloat value;

    value = lrg_vr_service_get_controller_axis (fixture->service, LRG_VR_HAND_LEFT, 0);

    /* Stub should return 0 */
    g_assert_cmpfloat (value, ==, 0.0f);
}

static void
test_vr_service_stub_trigger_haptic (VRFixture     *fixture,
                                     gconstpointer  user_data)
{
    /* Should not crash */
    lrg_vr_service_trigger_haptic (fixture->service, LRG_VR_HAND_RIGHT, 0.5f, 1.0f);
}

static void
test_vr_stub_get_default (void)
{
    LrgVRStub *stub1;
    LrgVRStub *stub2;

    stub1 = lrg_vr_stub_get_default ();
    stub2 = lrg_vr_stub_get_default ();

    g_assert_nonnull (stub1);
    g_assert_true (stub1 == stub2);
}

/* ==========================================================================
 * LrgVRComfortSettings Tests
 * ========================================================================== */

static void
test_vr_comfort_new (VRFixture     *fixture,
                     gconstpointer  user_data)
{
    g_assert_nonnull (fixture->comfort);
    g_assert_true (LRG_IS_VR_COMFORT_SETTINGS (fixture->comfort));
}

static void
test_vr_comfort_defaults (VRFixture     *fixture,
                          gconstpointer  user_data)
{
    /* Check default values */
    g_assert_cmpint (lrg_vr_comfort_settings_get_turn_mode (fixture->comfort),
                     ==, LRG_VR_TURN_MODE_SMOOTH);
    g_assert_cmpfloat (lrg_vr_comfort_settings_get_snap_turn_angle (fixture->comfort),
                       ==, 45.0f);
    g_assert_cmpint (lrg_vr_comfort_settings_get_locomotion_mode (fixture->comfort),
                     ==, LRG_VR_LOCOMOTION_SMOOTH);
    g_assert_false (lrg_vr_comfort_settings_get_vignette_enabled (fixture->comfort));
    g_assert_cmpfloat (lrg_vr_comfort_settings_get_vignette_intensity (fixture->comfort),
                       ==, 0.5f);
    g_assert_cmpfloat (lrg_vr_comfort_settings_get_height_adjustment (fixture->comfort),
                       ==, 0.0f);
}

static void
test_vr_comfort_turn_mode (VRFixture     *fixture,
                           gconstpointer  user_data)
{
    lrg_vr_comfort_settings_set_turn_mode (fixture->comfort, LRG_VR_TURN_MODE_SNAP);
    g_assert_cmpint (lrg_vr_comfort_settings_get_turn_mode (fixture->comfort),
                     ==, LRG_VR_TURN_MODE_SNAP);

    lrg_vr_comfort_settings_set_turn_mode (fixture->comfort, LRG_VR_TURN_MODE_SMOOTH);
    g_assert_cmpint (lrg_vr_comfort_settings_get_turn_mode (fixture->comfort),
                     ==, LRG_VR_TURN_MODE_SMOOTH);
}

static void
test_vr_comfort_snap_turn_angle (VRFixture     *fixture,
                                 gconstpointer  user_data)
{
    /* Normal value */
    lrg_vr_comfort_settings_set_snap_turn_angle (fixture->comfort, 30.0f);
    g_assert_cmpfloat (lrg_vr_comfort_settings_get_snap_turn_angle (fixture->comfort),
                       ==, 30.0f);

    /* Clamped to minimum */
    lrg_vr_comfort_settings_set_snap_turn_angle (fixture->comfort, 5.0f);
    g_assert_cmpfloat (lrg_vr_comfort_settings_get_snap_turn_angle (fixture->comfort),
                       ==, 15.0f);

    /* Clamped to maximum */
    lrg_vr_comfort_settings_set_snap_turn_angle (fixture->comfort, 120.0f);
    g_assert_cmpfloat (lrg_vr_comfort_settings_get_snap_turn_angle (fixture->comfort),
                       ==, 90.0f);
}

static void
test_vr_comfort_locomotion_mode (VRFixture     *fixture,
                                 gconstpointer  user_data)
{
    lrg_vr_comfort_settings_set_locomotion_mode (fixture->comfort, LRG_VR_LOCOMOTION_TELEPORT);
    g_assert_cmpint (lrg_vr_comfort_settings_get_locomotion_mode (fixture->comfort),
                     ==, LRG_VR_LOCOMOTION_TELEPORT);

    lrg_vr_comfort_settings_set_locomotion_mode (fixture->comfort, LRG_VR_LOCOMOTION_SMOOTH);
    g_assert_cmpint (lrg_vr_comfort_settings_get_locomotion_mode (fixture->comfort),
                     ==, LRG_VR_LOCOMOTION_SMOOTH);
}

static void
test_vr_comfort_vignette_enabled (VRFixture     *fixture,
                                  gconstpointer  user_data)
{
    lrg_vr_comfort_settings_set_vignette_enabled (fixture->comfort, TRUE);
    g_assert_true (lrg_vr_comfort_settings_get_vignette_enabled (fixture->comfort));

    lrg_vr_comfort_settings_set_vignette_enabled (fixture->comfort, FALSE);
    g_assert_false (lrg_vr_comfort_settings_get_vignette_enabled (fixture->comfort));
}

static void
test_vr_comfort_vignette_intensity (VRFixture     *fixture,
                                    gconstpointer  user_data)
{
    /* Normal value */
    lrg_vr_comfort_settings_set_vignette_intensity (fixture->comfort, 0.75f);
    g_assert_cmpfloat (lrg_vr_comfort_settings_get_vignette_intensity (fixture->comfort),
                       ==, 0.75f);

    /* Clamped to minimum */
    lrg_vr_comfort_settings_set_vignette_intensity (fixture->comfort, -0.5f);
    g_assert_cmpfloat (lrg_vr_comfort_settings_get_vignette_intensity (fixture->comfort),
                       ==, 0.0f);

    /* Clamped to maximum */
    lrg_vr_comfort_settings_set_vignette_intensity (fixture->comfort, 1.5f);
    g_assert_cmpfloat (lrg_vr_comfort_settings_get_vignette_intensity (fixture->comfort),
                       ==, 1.0f);
}

static void
test_vr_comfort_height_adjustment (VRFixture     *fixture,
                                   gconstpointer  user_data)
{
    /* Normal value */
    lrg_vr_comfort_settings_set_height_adjustment (fixture->comfort, 0.5f);
    g_assert_cmpfloat (lrg_vr_comfort_settings_get_height_adjustment (fixture->comfort),
                       ==, 0.5f);

    /* Negative value */
    lrg_vr_comfort_settings_set_height_adjustment (fixture->comfort, -0.3f);
    g_assert_cmpfloat (lrg_vr_comfort_settings_get_height_adjustment (fixture->comfort),
                       ==, -0.3f);

    /* Clamped to minimum */
    lrg_vr_comfort_settings_set_height_adjustment (fixture->comfort, -3.0f);
    g_assert_cmpfloat (lrg_vr_comfort_settings_get_height_adjustment (fixture->comfort),
                       ==, -2.0f);

    /* Clamped to maximum */
    lrg_vr_comfort_settings_set_height_adjustment (fixture->comfort, 5.0f);
    g_assert_cmpfloat (lrg_vr_comfort_settings_get_height_adjustment (fixture->comfort),
                       ==, 2.0f);
}

static void
test_vr_comfort_properties (VRFixture     *fixture,
                            gconstpointer  user_data)
{
    LrgVRTurnMode turn_mode;
    gfloat snap_angle;
    LrgVRLocomotionMode locomotion_mode;
    gboolean vignette_enabled;
    gfloat vignette_intensity;
    gfloat height_adjustment;

    /* Set via properties */
    g_object_set (fixture->comfort,
                  "turn-mode", LRG_VR_TURN_MODE_SNAP,
                  "snap-turn-angle", 60.0f,
                  "locomotion-mode", LRG_VR_LOCOMOTION_TELEPORT,
                  "vignette-enabled", TRUE,
                  "vignette-intensity", 0.8f,
                  "height-adjustment", 0.25f,
                  NULL);

    /* Get via properties */
    g_object_get (fixture->comfort,
                  "turn-mode", &turn_mode,
                  "snap-turn-angle", &snap_angle,
                  "locomotion-mode", &locomotion_mode,
                  "vignette-enabled", &vignette_enabled,
                  "vignette-intensity", &vignette_intensity,
                  "height-adjustment", &height_adjustment,
                  NULL);

    g_assert_cmpint (turn_mode, ==, LRG_VR_TURN_MODE_SNAP);
    g_assert_cmpfloat (snap_angle, ==, 60.0f);
    g_assert_cmpint (locomotion_mode, ==, LRG_VR_LOCOMOTION_TELEPORT);
    g_assert_true (vignette_enabled);
    g_assert_cmpfloat (vignette_intensity, ==, 0.8f);
    g_assert_cmpfloat (height_adjustment, ==, 0.25f);
}

/* ==========================================================================
 * Enum Tests
 * ========================================================================== */

static void
test_vr_eye_enum (void)
{
    GType type = LRG_TYPE_VR_EYE;

    g_assert_true (g_type_is_a (type, G_TYPE_ENUM));
}

static void
test_vr_hand_enum (void)
{
    GType type = LRG_TYPE_VR_HAND;

    g_assert_true (g_type_is_a (type, G_TYPE_ENUM));
}

static void
test_vr_controller_button_flags (void)
{
    GType type = LRG_TYPE_VR_CONTROLLER_BUTTON;

    g_assert_true (g_type_is_a (type, G_TYPE_FLAGS));
}

static void
test_vr_turn_mode_enum (void)
{
    GType type = LRG_TYPE_VR_TURN_MODE;

    g_assert_true (g_type_is_a (type, G_TYPE_ENUM));
}

static void
test_vr_locomotion_mode_enum (void)
{
    GType type = LRG_TYPE_VR_LOCOMOTION_MODE;

    g_assert_true (g_type_is_a (type, G_TYPE_ENUM));
}

/* ==========================================================================
 * OpenXR service (no headset, no GL context: what must still hold)
 * ========================================================================== */

static void
test_openxr_service_before_initialize (void)
{
    g_autoptr(LrgOpenXRService) xr = lrg_openxr_service_new ("test");
    LrgVRService *svc = LRG_VR_SERVICE (xr);
    gfloat m[16];

    g_assert_true (LRG_IS_VR_SERVICE (xr));
    /* Every frame-loop call is a safe no-op before initialize. */
    g_assert_false (lrg_vr_service_begin_frame (svc));
    g_assert_cmpuint (lrg_vr_service_get_view_count (svc), ==, 0);
    g_assert_false (lrg_vr_service_is_session_running (svc));
    g_assert_false (lrg_vr_service_should_quit (svc));
    g_assert_false (lrg_vr_service_is_controller_tracked (svc, LRG_VR_HAND_LEFT));
    lrg_vr_service_end_frame (svc);
    lrg_vr_service_get_controller_pose (svc, LRG_VR_HAND_RIGHT, m);
    g_assert_cmpfloat (lrg_openxr_service_get_display_refresh_rate (xr), ==, 90.0f);
    g_assert_cmpstr (lrg_openxr_service_get_session_state_name (xr), ==, "none");
    g_assert_cmpint (lrg_openxr_service_get_frame_fd (xr), ==, -1);
    /* The frame thread needs an initialised session. */
    g_assert_cmpint (lrg_openxr_service_start_frame_thread (xr), ==, -1);
}

/* The composition-layer, grip and recentre API before initialisation:
   nothing to make or show, no tracked grip, no recentre pending. */
static void
test_openxr_service_layers_before_initialize (void)
{
    g_autoptr(LrgOpenXRService) xr = lrg_openxr_service_new ("test");
    g_autoptr(GError) error = NULL;
    gfloat pose[16];
    gfloat grip[16];
    guint fbo = 7, w = 7, h = 7;
    gint i;

    for (i = 0; i < 16; i++)
        pose[i] = (i % 5 == 0) ? 1.0f : 0.0f;

    g_assert_cmpuint (lrg_openxr_service_create_layer (xr, 64, 64, &error), ==, 0);
    g_assert_nonnull (error);
    g_assert_false (lrg_openxr_service_begin_layer (xr, 1, &fbo, &w, &h));
    lrg_openxr_service_end_layer (xr, 1);   /* no-op, no crash */
    g_assert_false (lrg_openxr_service_submit_layer_quad (xr, 1, NULL, pose,
                                                          1.0f, 1.0f, TRUE));
    g_assert_false (lrg_openxr_service_submit_layer_cylinder (xr, 1, NULL, pose,
                                                              1.5f, 1.0f, 1.6f, TRUE));
    g_assert_false (lrg_openxr_service_has_cylinder_layers (xr));
    g_assert_false (lrg_openxr_service_get_grip_pose (xr, LRG_VR_HAND_LEFT, grip));
    for (i = 0; i < 16; i++)
    {
        gfloat want = (i % 5 == 0) ? 1.0f : 0.0f;

        g_assert_cmpfloat (grip[i], ==, want);
    }
    g_assert_false (lrg_openxr_service_take_recentered (xr));
}

static void
test_openxr_service_initialize_without_gl (void)
{
    g_autoptr(LrgOpenXRService) xr = lrg_openxr_service_new ("test");
    g_autoptr(GError) error = NULL;

    /* No GL context is current here, so a session cannot be bound whether or
       not a runtime is installed: initialize must fail with an error (not
       crash, not half-succeed) and leave the service reusable. */
    g_assert_false (lrg_vr_service_initialize (LRG_VR_SERVICE (xr), &error));
    g_assert_nonnull (error);
    g_assert_true (error->domain == LRG_VR_ERROR);
    g_assert_false (lrg_vr_service_is_hmd_present (LRG_VR_SERVICE (xr)));
}

static void
test_stub_frame_loop (void)
{
    LrgVRService *stub = LRG_VR_SERVICE (lrg_vr_stub_get_default ());
    gfloat eye[16], proj[16];
    guint fbo = 0, w = 0, h = 0;

    g_assert_false (lrg_vr_service_begin_frame (stub));
    g_assert_cmpuint (lrg_vr_service_get_view_count (stub), ==, 0);
    g_assert_false (lrg_vr_service_begin_view (stub, 0, 0.1f, 100.0f, eye, proj,
                                               &fbo, &w, &h));
}

static void
test_vr_error_enum (void)
{
    GType type = LRG_TYPE_VR_ERROR;

    g_assert_true (g_type_is_a (type, G_TYPE_ENUM));
}

/* ==========================================================================
 * Simulated headset (lrg_vr_stub_new_simulated)
 * ========================================================================== */

static void
test_stub_simulated_frame_loop (void)
{
    g_autoptr(LrgVRStub) stub = lrg_vr_stub_new_simulated ();
    LrgVRService *service = LRG_VR_SERVICE (stub);
    g_autoptr(GError) error = NULL;
    gfloat head[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0.5f, 1.6f, -1.0f, 1 };
    gfloat eye[16], proj[16];
    guint fbo = 99, w = 0, h = 0;

    g_assert_true (lrg_vr_stub_get_simulated (stub));
    g_assert_true (lrg_vr_service_is_available (service));
    g_assert_false (lrg_vr_service_begin_frame (service));
    g_assert_true (lrg_vr_service_initialize (service, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_vr_service_is_hmd_present (service));
    g_assert_true (lrg_vr_service_is_session_running (service));

    lrg_vr_stub_set_hmd_pose (stub, head);
    lrg_vr_stub_set_render_size (stub, 800, 600);
    g_assert_true (lrg_vr_service_begin_frame (service));
    g_assert_cmpuint (lrg_vr_service_get_view_count (service), ==, 2);

    /* the left eye: the head moved by -IPD/2 along its X axis */
    g_assert_true (lrg_vr_service_begin_view (service, 0, 0.1f, 100.0f, eye, proj,
                                              &fbo, &w, &h));
    g_assert_cmpuint (fbo, ==, 0);
    g_assert_cmpuint (w, ==, 800);
    g_assert_cmpuint (h, ==, 600);
    g_assert_cmpfloat_with_epsilon (eye[12], 0.5f - 0.032f, 1e-5);
    g_assert_cmpfloat_with_epsilon (eye[13], 1.6f, 1e-5);
    g_assert_cmpfloat_with_epsilon (eye[14], -1.0f, 1e-5);
    /* 45 degrees each way: a symmetric projection with focal length 1 */
    g_assert_cmpfloat_with_epsilon (proj[0], 1.0f, 1e-5);
    g_assert_cmpfloat_with_epsilon (proj[5], 1.0f, 1e-5);
    g_assert_cmpfloat_with_epsilon (proj[8], 0.0f, 1e-5);
    g_assert_cmpfloat_with_epsilon (proj[11], -1.0f, 1e-5);
    lrg_vr_service_end_view (service, 0);

    /* the right eye */
    g_assert_true (lrg_vr_service_begin_view (service, 1, 0.1f, 100.0f, eye, proj,
                                              &fbo, &w, &h));
    g_assert_cmpfloat_with_epsilon (eye[12], 0.5f + 0.032f, 1e-5);
    g_assert_false (lrg_vr_service_begin_view (service, 2, 0.1f, 100.0f, eye, proj,
                                               &fbo, &w, &h));
    lrg_vr_service_end_frame (service);
    g_assert_cmpuint (lrg_vr_stub_get_frame_count (stub), ==, 1);
    g_assert_cmpuint (lrg_vr_service_get_view_count (service), ==, 0);

    /* an asymmetric field of view shifts the projection's centre */
    lrg_vr_stub_set_fov (stub, -50.0f, 40.0f, 45.0f, -45.0f);
    lrg_vr_service_get_eye_projection (service, LRG_VR_EYE_LEFT, 0.1f, 100.0f, proj);
    g_assert_cmpfloat (proj[8], <, 0.0f);

    /* the dashboard's quit ends the session */
    lrg_vr_stub_request_quit (stub);
    g_assert_true (lrg_vr_service_should_quit (service));
    g_assert_false (lrg_vr_service_is_session_running (service));
    g_assert_false (lrg_vr_service_begin_frame (service));
}

static void
test_stub_simulated_controllers (void)
{
    g_autoptr(LrgVRStub) stub = lrg_vr_stub_new_simulated ();
    LrgVRService *service = LRG_VR_SERVICE (stub);
    gfloat pose[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0.2f, 1.2f, -0.4f, 1 };
    gfloat out[16];

    g_assert_false (lrg_vr_service_is_controller_tracked (service, LRG_VR_HAND_RIGHT));
    lrg_vr_stub_set_controller (stub, LRG_VR_HAND_RIGHT, pose, TRUE);
    g_assert_true (lrg_vr_service_is_controller_tracked (service, LRG_VR_HAND_RIGHT));
    g_assert_false (lrg_vr_service_is_controller_tracked (service, LRG_VR_HAND_LEFT));
    lrg_vr_service_get_controller_pose (service, LRG_VR_HAND_RIGHT, out);
    g_assert_cmpfloat_with_epsilon (out[13], 1.2f, 1e-6);

    lrg_vr_stub_set_controller_buttons (stub, LRG_VR_HAND_RIGHT, LRG_VR_CONTROLLER_BUTTON_TRIGGER);
    g_assert_cmpuint (lrg_vr_service_get_controller_buttons (service, LRG_VR_HAND_RIGHT), ==,
                      LRG_VR_CONTROLLER_BUTTON_TRIGGER);
    lrg_vr_stub_set_controller_axis (stub, LRG_VR_HAND_LEFT, LRG_VR_AXIS_THUMBSTICK_Y, 0.75f);
    g_assert_cmpfloat_with_epsilon (lrg_vr_service_get_controller_axis (service, LRG_VR_HAND_LEFT,
                                                                        LRG_VR_AXIS_THUMBSTICK_Y),
                                    0.75f, 1e-6);
    g_assert_cmpfloat (lrg_vr_service_get_controller_axis (service, LRG_VR_HAND_LEFT, 9), ==, 0.0f);

    lrg_vr_service_trigger_haptic (service, LRG_VR_HAND_LEFT, 0.1f, 0.5f);
    lrg_vr_service_trigger_haptic (service, LRG_VR_HAND_LEFT, 0.1f, 0.5f);
    g_assert_cmpuint (lrg_vr_stub_get_haptic_count (stub, LRG_VR_HAND_LEFT), ==, 2);
    g_assert_cmpuint (lrg_vr_stub_get_haptic_count (stub, LRG_VR_HAND_RIGHT), ==, 0);
}

/* ==========================================================================
 * LrgVRComfort
 * ========================================================================== */

static void
test_comfort_snap_turn (void)
{
    g_autoptr(LrgVRComfortSettings) settings = lrg_vr_comfort_settings_new ();
    g_autoptr(LrgVRComfort) comfort = NULL;

    lrg_vr_comfort_settings_set_turn_mode (settings, LRG_VR_TURN_MODE_SNAP);
    lrg_vr_comfort_settings_set_snap_turn_angle (settings, 30.0f);
    comfort = lrg_vr_comfort_new (settings);
    g_assert_true (lrg_vr_comfort_get_settings (comfort) == settings);

    /* below the threshold nothing; past it one snap; held, no more */
    g_assert_cmpfloat (lrg_vr_comfort_turn (comfort, 0.5f, 0.016f), ==, 0.0f);
    g_assert_cmpfloat (lrg_vr_comfort_turn (comfort, 0.9f, 0.016f), ==, 30.0f);
    g_assert_false (lrg_vr_comfort_get_snap_armed (comfort));
    g_assert_cmpfloat (lrg_vr_comfort_turn (comfort, 1.0f, 0.016f), ==, 0.0f);
    /* half way back is not enough to re-arm (hysteresis) */
    g_assert_cmpfloat (lrg_vr_comfort_turn (comfort, 0.5f, 0.016f), ==, 0.0f);
    g_assert_cmpfloat (lrg_vr_comfort_turn (comfort, 0.9f, 0.016f), ==, 0.0f);
    /* centred: armed again, and the left turns left */
    g_assert_cmpfloat (lrg_vr_comfort_turn (comfort, 0.1f, 0.016f), ==, 0.0f);
    g_assert_true (lrg_vr_comfort_get_snap_armed (comfort));
    g_assert_cmpfloat (lrg_vr_comfort_turn (comfort, -0.8f, 0.016f), ==, -30.0f);
}

static void
test_comfort_smooth_turn (void)
{
    g_autoptr(LrgVRComfort) comfort = lrg_vr_comfort_new (NULL);
    LrgVRComfortSettings *settings = lrg_vr_comfort_get_settings (comfort);

    lrg_vr_comfort_settings_set_smooth_turn_speed (settings, 100.0f);
    g_assert_cmpfloat (lrg_vr_comfort_settings_get_smooth_turn_speed (settings), ==, 100.0f);
    lrg_vr_comfort_settings_set_smooth_turn_speed (settings, 9000.0f);
    g_assert_cmpfloat (lrg_vr_comfort_settings_get_smooth_turn_speed (settings), ==, 360.0f);
    lrg_vr_comfort_settings_set_smooth_turn_speed (settings, 100.0f);

    /* inside the dead zone nothing, full stick the whole speed */
    g_assert_cmpfloat (lrg_vr_comfort_turn (comfort, 0.1f, 1.0f), ==, 0.0f);
    g_assert_cmpfloat_with_epsilon (lrg_vr_comfort_turn (comfort, 1.0f, 0.5f), 50.0f, 1e-4);
    g_assert_cmpfloat_with_epsilon (lrg_vr_comfort_turn (comfort, -1.0f, 0.5f), -50.0f, 1e-4);
    /* half way past the dead zone: half the speed */
    g_assert_cmpfloat_with_epsilon (lrg_vr_comfort_turn (comfort, 0.575f, 1.0f), 50.0f, 1e-3);
}

static void
test_comfort_vignette (void)
{
    g_autoptr(LrgVRComfort) comfort = lrg_vr_comfort_new (NULL);
    LrgVRComfortSettings *settings = lrg_vr_comfort_get_settings (comfort);
    gfloat v;
    gint i;

    /* off: never closes */
    g_assert_cmpfloat (lrg_vr_comfort_update_vignette (comfort, 5.0f, 0.0f, 1.0f), ==, 0.0f);

    lrg_vr_comfort_settings_set_vignette_enabled (settings, TRUE);
    lrg_vr_comfort_settings_set_vignette_intensity (settings, 0.8f);
    /* running at full speed closes it at the rise rate, up to the intensity */
    v = lrg_vr_comfort_update_vignette (comfort, 3.0f, 0.0f, 0.05f);
    g_assert_cmpfloat_with_epsilon (v, 0.3f, 1e-5);
    for (i = 0; i < 20; i++)
        v = lrg_vr_comfort_update_vignette (comfort, 3.0f, 0.0f, 0.05f);
    g_assert_cmpfloat_with_epsilon (v, 0.8f, 1e-5);
    /* half speed: it opens to half at the (slower) fall rate */
    v = lrg_vr_comfort_update_vignette (comfort, 1.5f, 0.0f, 0.05f);
    g_assert_cmpfloat_with_epsilon (v, 0.65f, 1e-5);
    /* standing still opens it fully */
    for (i = 0; i < 40; i++)
        v = lrg_vr_comfort_update_vignette (comfort, 0.0f, 0.0f, 0.05f);
    g_assert_cmpfloat (v, ==, 0.0f);
    /* turning by the stick closes it too */
    for (i = 0; i < 40; i++)
        v = lrg_vr_comfort_update_vignette (comfort, 0.0f, 120.0f, 0.05f);
    g_assert_cmpfloat_with_epsilon (v, 0.8f, 1e-5);
    lrg_vr_comfort_reset (comfort);
    g_assert_cmpfloat (lrg_vr_comfort_get_vignette (comfort), ==, 0.0f);

    /* a snap turn blinks it shut at once, then it fades */
    lrg_vr_comfort_settings_set_turn_mode (settings, LRG_VR_TURN_MODE_SNAP);
    g_assert_cmpfloat (lrg_vr_comfort_turn (comfort, 1.0f, 0.016f), !=, 0.0f);
    v = lrg_vr_comfort_update_vignette (comfort, 0.0f, 0.0f, 0.016f);
    g_assert_cmpfloat_with_epsilon (v, 0.8f, 1e-5);
    for (i = 0; i < 40; i++)
        v = lrg_vr_comfort_update_vignette (comfort, 0.0f, 0.0f, 0.05f);
    g_assert_cmpfloat (v, ==, 0.0f);
}

static void
test_comfort_height (void)
{
    g_autoptr(LrgVRComfort) comfort = lrg_vr_comfort_new (NULL);
    LrgVRComfortSettings *settings = lrg_vr_comfort_get_settings (comfort);

    /* standing: the tracked height as it is */
    g_assert_false (lrg_vr_comfort_needs_calibration (comfort));
    g_assert_cmpfloat_with_epsilon (lrg_vr_comfort_get_eye_height (comfort, 1.6f), 1.6f, 1e-6);

    /* seated: calibrate a 1.2 m seated head to 1.7 m */
    lrg_vr_comfort_settings_set_seated (settings, TRUE);
    g_assert_true (lrg_vr_comfort_settings_get_seated (settings));
    g_assert_true (lrg_vr_comfort_needs_calibration (comfort));
    lrg_vr_comfort_calibrate_height (comfort, 1.2f, 1.7f);
    g_assert_false (lrg_vr_comfort_needs_calibration (comfort));
    g_assert_cmpfloat_with_epsilon (lrg_vr_comfort_settings_get_height_adjustment (settings),
                                    0.5f, 1e-6);
    g_assert_cmpfloat_with_epsilon (lrg_vr_comfort_get_eye_height (comfort, 1.1f), 1.6f, 1e-6);
    /* never under the floor */
    g_assert_cmpfloat (lrg_vr_comfort_get_eye_height (comfort, -5.0f), ==, 0.1f);
    /* a recentre asks again */
    lrg_vr_comfort_reset (comfort);
    g_assert_true (lrg_vr_comfort_needs_calibration (comfort));
}

static void
test_openxr_probe_without_runtime (void)
{
    g_autoptr(GError) error = NULL;
    gchar *runtime = (gchar *) "unset";
    gchar *system = (gchar *) "unset";

    /* a runtime manifest that does not exist: the loader finds none */
    g_setenv ("XR_RUNTIME_JSON", "/nonexistent/librefall-test-runtime.json", TRUE);
    g_assert_false (lrg_openxr_service_probe ("test", &runtime, &system, &error));
    g_assert_error (error, LRG_VR_ERROR, LRG_VR_ERROR_NOT_AVAILABLE);
    g_assert_null (runtime);
    g_assert_null (system);
    g_unsetenv ("XR_RUNTIME_JSON");
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* LrgVRService / LrgVRStub tests */
    g_test_add ("/vr/service/stub-new", VRFixture, NULL,
                fixture_set_up, test_vr_service_stub_new, fixture_tear_down);
    g_test_add ("/vr/service/stub-is-available", VRFixture, NULL,
                fixture_set_up, test_vr_service_stub_is_available, fixture_tear_down);
    g_test_add ("/vr/service/stub-is-hmd-present", VRFixture, NULL,
                fixture_set_up, test_vr_service_stub_is_hmd_present, fixture_tear_down);
    g_test_add ("/vr/service/stub-initialize", VRFixture, NULL,
                fixture_set_up, test_vr_service_stub_initialize, fixture_tear_down);
    g_test_add ("/vr/service/stub-shutdown", VRFixture, NULL,
                fixture_set_up, test_vr_service_stub_shutdown, fixture_tear_down);
    g_test_add ("/vr/service/stub-poll-events", VRFixture, NULL,
                fixture_set_up, test_vr_service_stub_poll_events, fixture_tear_down);
    g_test_add ("/vr/service/stub-render-size", VRFixture, NULL,
                fixture_set_up, test_vr_service_stub_render_size, fixture_tear_down);
    g_test_add ("/vr/service/stub-eye-projection", VRFixture, NULL,
                fixture_set_up, test_vr_service_stub_eye_projection, fixture_tear_down);
    g_test_add ("/vr/service/stub-eye-to-head", VRFixture, NULL,
                fixture_set_up, test_vr_service_stub_eye_to_head, fixture_tear_down);
    g_test_add ("/vr/service/stub-hmd-pose", VRFixture, NULL,
                fixture_set_up, test_vr_service_stub_hmd_pose, fixture_tear_down);
    g_test_add ("/vr/service/stub-submit-frame", VRFixture, NULL,
                fixture_set_up, test_vr_service_stub_submit_frame, fixture_tear_down);
    g_test_add ("/vr/service/stub-controller-pose", VRFixture, NULL,
                fixture_set_up, test_vr_service_stub_controller_pose, fixture_tear_down);
    g_test_add ("/vr/service/stub-controller-buttons", VRFixture, NULL,
                fixture_set_up, test_vr_service_stub_controller_buttons, fixture_tear_down);
    g_test_add ("/vr/service/stub-controller-axis", VRFixture, NULL,
                fixture_set_up, test_vr_service_stub_controller_axis, fixture_tear_down);
    g_test_add ("/vr/service/stub-trigger-haptic", VRFixture, NULL,
                fixture_set_up, test_vr_service_stub_trigger_haptic, fixture_tear_down);
    g_test_add_func ("/vr/service/stub-get-default", test_vr_stub_get_default);

    /* LrgVRComfortSettings tests */
    g_test_add ("/vr/comfort/new", VRFixture, NULL,
                fixture_set_up, test_vr_comfort_new, fixture_tear_down);
    g_test_add ("/vr/comfort/defaults", VRFixture, NULL,
                fixture_set_up, test_vr_comfort_defaults, fixture_tear_down);
    g_test_add ("/vr/comfort/turn-mode", VRFixture, NULL,
                fixture_set_up, test_vr_comfort_turn_mode, fixture_tear_down);
    g_test_add ("/vr/comfort/snap-turn-angle", VRFixture, NULL,
                fixture_set_up, test_vr_comfort_snap_turn_angle, fixture_tear_down);
    g_test_add ("/vr/comfort/locomotion-mode", VRFixture, NULL,
                fixture_set_up, test_vr_comfort_locomotion_mode, fixture_tear_down);
    g_test_add ("/vr/comfort/vignette-enabled", VRFixture, NULL,
                fixture_set_up, test_vr_comfort_vignette_enabled, fixture_tear_down);
    g_test_add ("/vr/comfort/vignette-intensity", VRFixture, NULL,
                fixture_set_up, test_vr_comfort_vignette_intensity, fixture_tear_down);
    g_test_add ("/vr/comfort/height-adjustment", VRFixture, NULL,
                fixture_set_up, test_vr_comfort_height_adjustment, fixture_tear_down);
    g_test_add ("/vr/comfort/properties", VRFixture, NULL,
                fixture_set_up, test_vr_comfort_properties, fixture_tear_down);

    /* Enum tests */
    g_test_add_func ("/vr/enums/eye", test_vr_eye_enum);
    g_test_add_func ("/vr/enums/hand", test_vr_hand_enum);
    g_test_add_func ("/vr/enums/controller-button", test_vr_controller_button_flags);
    g_test_add_func ("/vr/enums/turn-mode", test_vr_turn_mode_enum);
    g_test_add_func ("/vr/enums/locomotion-mode", test_vr_locomotion_mode_enum);
    g_test_add_func ("/vr/enums/error", test_vr_error_enum);
    g_test_add_func ("/vr/openxr/before-initialize",
                     test_openxr_service_before_initialize);
    g_test_add_func ("/vr/openxr/layers-before-initialize",
                     test_openxr_service_layers_before_initialize);
    g_test_add_func ("/vr/openxr/initialize-without-gl",
                     test_openxr_service_initialize_without_gl);
    g_test_add_func ("/vr/stub/frame-loop", test_stub_frame_loop);
    g_test_add_func ("/vr/stub/simulated-frame-loop", test_stub_simulated_frame_loop);
    g_test_add_func ("/vr/stub/simulated-controllers", test_stub_simulated_controllers);
    g_test_add_func ("/vr/comfort-state/snap-turn", test_comfort_snap_turn);
    g_test_add_func ("/vr/comfort-state/smooth-turn", test_comfort_smooth_turn);
    g_test_add_func ("/vr/comfort-state/vignette", test_comfort_vignette);
    g_test_add_func ("/vr/comfort-state/height", test_comfort_height);
    g_test_add_func ("/vr/openxr/probe-without-runtime", test_openxr_probe_without_runtime);

    return g_test_run ();
}
