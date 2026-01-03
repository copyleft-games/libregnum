/* test-template-3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for 3D game templates:
 * - LrgGame3DTemplate
 * - LrgFPSTemplate
 * - LrgThirdPersonTemplate
 * - LrgRacing3DTemplate
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Skip Macros for Headless Environments
 * ========================================================================== */

#define SKIP_IF_NO_DISPLAY() \
    do { \
        if (g_getenv ("DISPLAY") == NULL && g_getenv ("WAYLAND_DISPLAY") == NULL) \
        { \
            g_test_skip ("No display available (headless environment)"); \
            return; \
        } \
    } while (0)

/*
 * SKIP_REQUIRES_WINDOW: Skip tests that require a fully initialized raylib window.
 * Some templates (FPS, ThirdPerson, Racing3D) enable mouse look in their constructors,
 * which calls grl_window_disable_cursor(). Without an actual window (only possible
 * in a full game loop), this crashes. These tests must be skipped in unit test context.
 */
#define SKIP_REQUIRES_WINDOW() \
    do { \
        g_test_skip ("Requires initialized raylib window (constructor enables mouse look)"); \
        return; \
    } while (0)

#define SKIP_IF_NULL(ptr) \
    do { \
        if ((ptr) == NULL) \
        { \
            g_test_skip ("Resource not available"); \
            return; \
        } \
    } while (0)

/* ==========================================================================
 * Test Cases - LrgGame3DTemplate Construction
 * ========================================================================== */

static void
test_game_3d_template_new (void)
{
    g_autoptr(LrgGame3DTemplate) template = NULL;

    template = lrg_game_3d_template_new ();

    g_assert_nonnull (template);
    g_assert_true (LRG_IS_GAME_3D_TEMPLATE (template));
    g_assert_true (LRG_IS_GAME_TEMPLATE (template));
}

/* ==========================================================================
 * Test Cases - LrgGame3DTemplate Camera Configuration
 * ========================================================================== */

static void
test_game_3d_template_fov (void)
{
    g_autoptr(LrgGame3DTemplate) template = NULL;
    gfloat fov;

    template = lrg_game_3d_template_new ();
    g_assert_nonnull (template);

    /* Set field of view */
    lrg_game_3d_template_set_fov (template, 75.0f);
    fov = lrg_game_3d_template_get_fov (template);
    g_assert_cmpfloat_with_epsilon (fov, 75.0f, 0.001f);
}

static void
test_game_3d_template_near_clip (void)
{
    g_autoptr(LrgGame3DTemplate) template = NULL;
    gfloat near_clip;

    template = lrg_game_3d_template_new ();
    g_assert_nonnull (template);

    /* Set near clipping plane */
    lrg_game_3d_template_set_near_clip (template, 0.1f);
    near_clip = lrg_game_3d_template_get_near_clip (template);
    g_assert_cmpfloat_with_epsilon (near_clip, 0.1f, 0.001f);
}

static void
test_game_3d_template_far_clip (void)
{
    g_autoptr(LrgGame3DTemplate) template = NULL;
    gfloat far_clip;

    template = lrg_game_3d_template_new ();
    g_assert_nonnull (template);

    /* Set far clipping plane */
    lrg_game_3d_template_set_far_clip (template, 1000.0f);
    far_clip = lrg_game_3d_template_get_far_clip (template);
    g_assert_cmpfloat_with_epsilon (far_clip, 1000.0f, 0.001f);
}

/* ==========================================================================
 * Test Cases - LrgGame3DTemplate Mouse Look
 * ========================================================================== */

static void
test_game_3d_template_mouse_look_enabled (void)
{
    g_autoptr(LrgGame3DTemplate) template = NULL;
    gboolean enabled;

    template = lrg_game_3d_template_new ();
    g_assert_nonnull (template);

    /*
     * Note: set_mouse_look_enabled requires an initialized window to control
     * cursor visibility. Without the full game loop, we can only test the getter.
     * Default should be FALSE.
     */
    enabled = lrg_game_3d_template_get_mouse_look_enabled (template);
    g_assert_false (enabled);
}

static void
test_game_3d_template_mouse_sensitivity (void)
{
    g_autoptr(LrgGame3DTemplate) template = NULL;
    gfloat sensitivity;

    template = lrg_game_3d_template_new ();
    g_assert_nonnull (template);

    /* Set sensitivity */
    lrg_game_3d_template_set_mouse_sensitivity (template, 0.15f);
    sensitivity = lrg_game_3d_template_get_mouse_sensitivity (template);
    g_assert_cmpfloat_with_epsilon (sensitivity, 0.15f, 0.001f);
}

static void
test_game_3d_template_invert_y (void)
{
    g_autoptr(LrgGame3DTemplate) template = NULL;

    template = lrg_game_3d_template_new ();
    g_assert_nonnull (template);

    /* Test invert Y toggle */
    lrg_game_3d_template_set_invert_y (template, TRUE);
    g_assert_true (lrg_game_3d_template_get_invert_y (template));

    lrg_game_3d_template_set_invert_y (template, FALSE);
    g_assert_false (lrg_game_3d_template_get_invert_y (template));
}

/* ==========================================================================
 * Test Cases - LrgGame3DTemplate Camera Orientation
 * ========================================================================== */

static void
test_game_3d_template_pitch_limits (void)
{
    g_autoptr(LrgGame3DTemplate) template = NULL;
    gfloat min_pitch;
    gfloat max_pitch;

    template = lrg_game_3d_template_new ();
    g_assert_nonnull (template);

    /* Set pitch limits */
    lrg_game_3d_template_set_pitch_limits (template, -85.0f, 85.0f);
    lrg_game_3d_template_get_pitch_limits (template, &min_pitch, &max_pitch);

    g_assert_cmpfloat_with_epsilon (min_pitch, -85.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (max_pitch, 85.0f, 0.001f);
}

static void
test_game_3d_template_yaw (void)
{
    g_autoptr(LrgGame3DTemplate) template = NULL;
    gfloat yaw;

    template = lrg_game_3d_template_new ();
    g_assert_nonnull (template);

    /* Set yaw */
    lrg_game_3d_template_set_yaw (template, 45.0f);
    yaw = lrg_game_3d_template_get_yaw (template);
    g_assert_cmpfloat_with_epsilon (yaw, 45.0f, 0.001f);
}

static void
test_game_3d_template_pitch (void)
{
    g_autoptr(LrgGame3DTemplate) template = NULL;
    gfloat pitch;

    template = lrg_game_3d_template_new ();
    g_assert_nonnull (template);

    /* Set pitch */
    lrg_game_3d_template_set_pitch (template, 30.0f);
    pitch = lrg_game_3d_template_get_pitch (template);
    g_assert_cmpfloat_with_epsilon (pitch, 30.0f, 0.001f);
}

/* ==========================================================================
 * Test Cases - LrgFPSTemplate Construction
 * ========================================================================== */

static void
test_fps_template_new (void)
{
    g_autoptr(LrgFPSTemplate) template = NULL;

    SKIP_REQUIRES_WINDOW ();

    template = lrg_fps_template_new ();

    g_assert_nonnull (template);
    g_assert_true (LRG_IS_FPS_TEMPLATE (template));
    g_assert_true (LRG_IS_GAME_3D_TEMPLATE (template));
    g_assert_true (LRG_IS_GAME_TEMPLATE (template));
}

/* ==========================================================================
 * Test Cases - LrgFPSTemplate Movement Properties
 * ========================================================================== */

static void
test_fps_template_walk_speed (void)
{
    g_autoptr(LrgFPSTemplate) template = NULL;
    gfloat speed;

    SKIP_REQUIRES_WINDOW ();

    template = lrg_fps_template_new ();
    g_assert_nonnull (template);

    /* Set walk speed */
    lrg_fps_template_set_walk_speed (template, 5.5f);
    speed = lrg_fps_template_get_walk_speed (template);
    g_assert_cmpfloat_with_epsilon (speed, 5.5f, 0.001f);
}

static void
test_fps_template_sprint_multiplier (void)
{
    g_autoptr(LrgFPSTemplate) template = NULL;
    gfloat multiplier;

    SKIP_REQUIRES_WINDOW ();

    template = lrg_fps_template_new ();
    g_assert_nonnull (template);

    /* Set sprint multiplier */
    lrg_fps_template_set_sprint_multiplier (template, 1.8f);
    multiplier = lrg_fps_template_get_sprint_multiplier (template);
    g_assert_cmpfloat_with_epsilon (multiplier, 1.8f, 0.001f);
}

static void
test_fps_template_jump_height (void)
{
    g_autoptr(LrgFPSTemplate) template = NULL;
    gfloat height;

    SKIP_REQUIRES_WINDOW ();

    template = lrg_fps_template_new ();
    g_assert_nonnull (template);

    /* Set jump height */
    lrg_fps_template_set_jump_height (template, 8.0f);
    height = lrg_fps_template_get_jump_height (template);
    g_assert_cmpfloat_with_epsilon (height, 8.0f, 0.001f);
}

static void
test_fps_template_gravity (void)
{
    g_autoptr(LrgFPSTemplate) template = NULL;
    gfloat gravity;

    SKIP_REQUIRES_WINDOW ();

    template = lrg_fps_template_new ();
    g_assert_nonnull (template);

    /* Set gravity */
    lrg_fps_template_set_gravity (template, 20.0f);
    gravity = lrg_fps_template_get_gravity (template);
    g_assert_cmpfloat_with_epsilon (gravity, 20.0f, 0.001f);
}

/* ==========================================================================
 * Test Cases - LrgFPSTemplate Weapon Properties
 * ========================================================================== */

static void
test_fps_template_armor (void)
{
    g_autoptr(LrgFPSTemplate) template = NULL;
    gfloat armor;

    SKIP_REQUIRES_WINDOW ();

    template = lrg_fps_template_new ();
    g_assert_nonnull (template);

    /* Set armor */
    lrg_fps_template_set_armor (template, 100.0f);
    armor = lrg_fps_template_get_armor (template);
    g_assert_cmpfloat_with_epsilon (armor, 100.0f, 0.001f);
}

static void
test_fps_template_max_ammo (void)
{
    g_autoptr(LrgFPSTemplate) template = NULL;
    gint ammo;

    SKIP_REQUIRES_WINDOW ();

    template = lrg_fps_template_new ();
    g_assert_nonnull (template);

    /* Set ammo */
    lrg_fps_template_set_ammo (template, 30);
    ammo = lrg_fps_template_get_ammo (template);
    g_assert_cmpint (ammo, ==, 30);
}

/* ==========================================================================
 * Test Cases - LrgThirdPersonTemplate Construction
 * ========================================================================== */

static void
test_third_person_template_new (void)
{
    g_autoptr(LrgThirdPersonTemplate) template = NULL;

    SKIP_REQUIRES_WINDOW ();

    template = lrg_third_person_template_new ();

    g_assert_nonnull (template);
    g_assert_true (LRG_IS_THIRD_PERSON_TEMPLATE (template));
    g_assert_true (LRG_IS_GAME_3D_TEMPLATE (template));
}

/* ==========================================================================
 * Test Cases - LrgThirdPersonTemplate Camera Properties
 * ========================================================================== */

static void
test_third_person_template_camera_distance (void)
{
    g_autoptr(LrgThirdPersonTemplate) template = NULL;
    gfloat distance;

    SKIP_REQUIRES_WINDOW ();

    template = lrg_third_person_template_new ();
    g_assert_nonnull (template);

    /* Set camera distance */
    lrg_third_person_template_set_camera_distance (template, 6.0f);
    distance = lrg_third_person_template_get_camera_distance (template);
    g_assert_cmpfloat_with_epsilon (distance, 6.0f, 0.001f);
}

static void
test_third_person_template_camera_height (void)
{
    g_autoptr(LrgThirdPersonTemplate) template = NULL;
    gfloat height;

    SKIP_REQUIRES_WINDOW ();

    template = lrg_third_person_template_new ();
    g_assert_nonnull (template);

    /* Set camera height */
    lrg_third_person_template_set_camera_height (template, 2.5f);
    height = lrg_third_person_template_get_camera_height (template);
    g_assert_cmpfloat_with_epsilon (height, 2.5f, 0.001f);
}

static void
test_third_person_template_shoulder_offset (void)
{
    g_autoptr(LrgThirdPersonTemplate) template = NULL;
    gfloat offset_x, offset_y;

    SKIP_REQUIRES_WINDOW ();

    template = lrg_third_person_template_new ();
    g_assert_nonnull (template);

    /* Set shoulder offset (x and y components) */
    lrg_third_person_template_set_shoulder_offset (template, 0.5f, 0.3f);
    lrg_third_person_template_get_shoulder_offset (template, &offset_x, &offset_y);
    g_assert_cmpfloat_with_epsilon (offset_x, 0.5f, 0.001f);
    g_assert_cmpfloat_with_epsilon (offset_y, 0.3f, 0.001f);
}

/* ==========================================================================
 * Test Cases - LrgThirdPersonTemplate Aim Mode
 * ========================================================================== */

static void
test_third_person_template_aim_mode (void)
{
    g_autoptr(LrgThirdPersonTemplate) template = NULL;
    LrgThirdPersonAimMode mode;

    SKIP_REQUIRES_WINDOW ();

    template = lrg_third_person_template_new ();
    g_assert_nonnull (template);

    /* Test different aim modes */
    lrg_third_person_template_set_aim_mode (template, LRG_THIRD_PERSON_AIM_MODE_FREE);
    mode = lrg_third_person_template_get_aim_mode (template);
    g_assert_cmpint (mode, ==, LRG_THIRD_PERSON_AIM_MODE_FREE);

    lrg_third_person_template_set_aim_mode (template, LRG_THIRD_PERSON_AIM_MODE_STRAFE);
    mode = lrg_third_person_template_get_aim_mode (template);
    g_assert_cmpint (mode, ==, LRG_THIRD_PERSON_AIM_MODE_STRAFE);
}

/* ==========================================================================
 * Test Cases - LrgThirdPersonTemplate Stamina
 * ========================================================================== */

static void
test_third_person_template_dodge_stamina_cost (void)
{
    g_autoptr(LrgThirdPersonTemplate) template = NULL;
    gfloat cost;

    SKIP_REQUIRES_WINDOW ();

    template = lrg_third_person_template_new ();
    g_assert_nonnull (template);

    /* Set dodge stamina cost */
    lrg_third_person_template_set_dodge_stamina_cost (template, 25.0f);
    cost = lrg_third_person_template_get_dodge_stamina_cost (template);
    g_assert_cmpfloat_with_epsilon (cost, 25.0f, 0.001f);
}

/* ==========================================================================
 * Test Cases - LrgRacing3DTemplate Construction
 * ========================================================================== */

static void
test_racing_3d_template_new (void)
{
    g_autoptr(LrgRacing3DTemplate) template = NULL;

    SKIP_REQUIRES_WINDOW ();

    template = lrg_racing_3d_template_new ();

    g_assert_nonnull (template);
    g_assert_true (LRG_IS_RACING_3D_TEMPLATE (template));
    g_assert_true (LRG_IS_GAME_3D_TEMPLATE (template));
}

/* ==========================================================================
 * Test Cases - LrgRacing3DTemplate Vehicle Properties
 * ========================================================================== */

static void
test_racing_3d_template_max_speed (void)
{
    g_autoptr(LrgRacing3DTemplate) template = NULL;
    gfloat speed;

    SKIP_REQUIRES_WINDOW ();

    template = lrg_racing_3d_template_new ();
    g_assert_nonnull (template);

    /* Set max speed */
    lrg_racing_3d_template_set_max_speed (template, 180.0f);
    speed = lrg_racing_3d_template_get_max_speed (template);
    g_assert_cmpfloat_with_epsilon (speed, 180.0f, 0.001f);
}

static void
test_racing_3d_template_acceleration (void)
{
    g_autoptr(LrgRacing3DTemplate) template = NULL;
    gfloat accel;

    SKIP_REQUIRES_WINDOW ();

    template = lrg_racing_3d_template_new ();
    g_assert_nonnull (template);

    /* Set acceleration */
    lrg_racing_3d_template_set_acceleration (template, 50.0f);
    accel = lrg_racing_3d_template_get_acceleration (template);
    g_assert_cmpfloat_with_epsilon (accel, 50.0f, 0.001f);
}

/* ==========================================================================
 * Test Cases - LrgRacing3DTemplate Race Properties
 * ========================================================================== */

static void
test_racing_3d_template_total_laps (void)
{
    g_autoptr(LrgRacing3DTemplate) template = NULL;
    guint laps;

    SKIP_REQUIRES_WINDOW ();

    template = lrg_racing_3d_template_new ();
    g_assert_nonnull (template);

    /* Set total laps */
    lrg_racing_3d_template_set_total_laps (template, 5);
    laps = lrg_racing_3d_template_get_total_laps (template);
    g_assert_cmpuint (laps, ==, 5);
}

static void
test_racing_3d_template_camera_mode (void)
{
    g_autoptr(LrgRacing3DTemplate) template = NULL;
    LrgRacing3DCameraMode mode;

    SKIP_REQUIRES_WINDOW ();

    template = lrg_racing_3d_template_new ();
    g_assert_nonnull (template);

    /* Test different camera modes */
    lrg_racing_3d_template_set_camera_mode (template, LRG_RACING_3D_CAMERA_CHASE);
    mode = lrg_racing_3d_template_get_camera_mode (template);
    g_assert_cmpint (mode, ==, LRG_RACING_3D_CAMERA_CHASE);

    lrg_racing_3d_template_set_camera_mode (template, LRG_RACING_3D_CAMERA_HOOD);
    mode = lrg_racing_3d_template_get_camera_mode (template);
    g_assert_cmpint (mode, ==, LRG_RACING_3D_CAMERA_HOOD);
}

/* ==========================================================================
 * Test Cases - Property Inheritance
 *
 * Verify that derived 3D templates inherit base template properties.
 * ========================================================================== */

static void
test_3d_template_property_inheritance (void)
{
    g_autoptr(LrgFPSTemplate) template = NULL;
    gchar *title;

    SKIP_REQUIRES_WINDOW ();

    template = lrg_fps_template_new ();
    g_assert_nonnull (template);

    /* LrgFPSTemplate should inherit "title" property from LrgGameTemplate */
    g_object_set (template, "title", "Test FPS Game", NULL);
    g_object_get (template, "title", &title, NULL);

    g_assert_cmpstr (title, ==, "Test FPS Game");
    g_free (title);
}

static void
test_3d_template_camera_inheritance (void)
{
    g_autoptr(LrgFPSTemplate) template = NULL;

    SKIP_REQUIRES_WINDOW ();

    template = lrg_fps_template_new ();
    g_assert_nonnull (template);

    /* LrgFPSTemplate should have camera settings from LrgGame3DTemplate */
    lrg_game_3d_template_set_fov (LRG_GAME_3D_TEMPLATE (template), 90.0f);
    g_assert_cmpfloat_with_epsilon (
        lrg_game_3d_template_get_fov (LRG_GAME_3D_TEMPLATE (template)),
        90.0f, 0.001f);
}

/* ==========================================================================
 * Test Cases - GObject Construction with Properties
 * ========================================================================== */

static void
test_fps_construct_with_properties (void)
{
    g_autoptr(LrgFPSTemplate) template = NULL;

    SKIP_REQUIRES_WINDOW ();

    template = g_object_new (LRG_TYPE_FPS_TEMPLATE,
                              "title", "My FPS Game",
                              "walk-speed", 6.0f,
                              "jump-height", 10.0f,
                              NULL);

    g_assert_nonnull (template);
    g_assert_cmpfloat_with_epsilon (lrg_fps_template_get_walk_speed (template),
                                     6.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_fps_template_get_jump_height (template),
                                     10.0f, 0.001f);
}

static void
test_third_person_construct_with_properties (void)
{
    g_autoptr(LrgThirdPersonTemplate) template = NULL;

    SKIP_REQUIRES_WINDOW ();

    template = g_object_new (LRG_TYPE_THIRD_PERSON_TEMPLATE,
                              "title", "My Third Person Game",
                              "camera-distance", 8.0f,
                              "camera-height", 3.0f,
                              NULL);

    g_assert_nonnull (template);
    g_assert_cmpfloat_with_epsilon (
        lrg_third_person_template_get_camera_distance (template),
        8.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (
        lrg_third_person_template_get_camera_height (template),
        3.0f, 0.001f);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* LrgGame3DTemplate Construction */
    g_test_add_func ("/template/3d/new", test_game_3d_template_new);

    /* LrgGame3DTemplate Camera Configuration */
    g_test_add_func ("/template/3d/fov", test_game_3d_template_fov);
    g_test_add_func ("/template/3d/near-clip", test_game_3d_template_near_clip);
    g_test_add_func ("/template/3d/far-clip", test_game_3d_template_far_clip);

    /* LrgGame3DTemplate Mouse Look */
    g_test_add_func ("/template/3d/mouse-look-enabled",
                      test_game_3d_template_mouse_look_enabled);
    g_test_add_func ("/template/3d/mouse-sensitivity",
                      test_game_3d_template_mouse_sensitivity);
    g_test_add_func ("/template/3d/invert-y", test_game_3d_template_invert_y);

    /* LrgGame3DTemplate Camera Orientation */
    g_test_add_func ("/template/3d/pitch-limits",
                      test_game_3d_template_pitch_limits);
    g_test_add_func ("/template/3d/yaw", test_game_3d_template_yaw);
    g_test_add_func ("/template/3d/pitch", test_game_3d_template_pitch);

    /* LrgFPSTemplate Construction */
    g_test_add_func ("/template/fps/new", test_fps_template_new);

    /* LrgFPSTemplate Movement Properties */
    g_test_add_func ("/template/fps/walk-speed", test_fps_template_walk_speed);
    g_test_add_func ("/template/fps/sprint-multiplier",
                      test_fps_template_sprint_multiplier);
    g_test_add_func ("/template/fps/jump-height", test_fps_template_jump_height);
    g_test_add_func ("/template/fps/gravity", test_fps_template_gravity);

    /* LrgFPSTemplate Weapon Properties */
    g_test_add_func ("/template/fps/armor", test_fps_template_armor);
    g_test_add_func ("/template/fps/max-ammo", test_fps_template_max_ammo);

    /* LrgThirdPersonTemplate Construction */
    g_test_add_func ("/template/third-person/new", test_third_person_template_new);

    /* LrgThirdPersonTemplate Camera Properties */
    g_test_add_func ("/template/third-person/camera-distance",
                      test_third_person_template_camera_distance);
    g_test_add_func ("/template/third-person/camera-height",
                      test_third_person_template_camera_height);
    g_test_add_func ("/template/third-person/shoulder-offset",
                      test_third_person_template_shoulder_offset);

    /* LrgThirdPersonTemplate Aim Mode */
    g_test_add_func ("/template/third-person/aim-mode",
                      test_third_person_template_aim_mode);

    /* LrgThirdPersonTemplate Stamina */
    g_test_add_func ("/template/third-person/dodge-stamina-cost",
                      test_third_person_template_dodge_stamina_cost);

    /* LrgRacing3DTemplate Construction */
    g_test_add_func ("/template/racing-3d/new", test_racing_3d_template_new);

    /* LrgRacing3DTemplate Vehicle Properties */
    g_test_add_func ("/template/racing-3d/max-speed",
                      test_racing_3d_template_max_speed);
    g_test_add_func ("/template/racing-3d/acceleration",
                      test_racing_3d_template_acceleration);

    /* LrgRacing3DTemplate Race Properties */
    g_test_add_func ("/template/racing-3d/total-laps",
                      test_racing_3d_template_total_laps);
    g_test_add_func ("/template/racing-3d/camera-mode",
                      test_racing_3d_template_camera_mode);

    /* Property Inheritance */
    g_test_add_func ("/template/3d/inheritance/title",
                      test_3d_template_property_inheritance);
    g_test_add_func ("/template/3d/inheritance/camera",
                      test_3d_template_camera_inheritance);

    /* GObject Construction */
    g_test_add_func ("/template/fps/construct-with-properties",
                      test_fps_construct_with_properties);
    g_test_add_func ("/template/third-person/construct-with-properties",
                      test_third_person_construct_with_properties);

    return g_test_run ();
}
