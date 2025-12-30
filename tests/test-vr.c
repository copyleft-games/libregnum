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

static void
test_vr_error_enum (void)
{
    GType type = LRG_TYPE_VR_ERROR;

    g_assert_true (g_type_is_a (type, G_TYPE_ENUM));
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

    return g_test_run ();
}
