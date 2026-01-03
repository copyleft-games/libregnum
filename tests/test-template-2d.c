/* test-template-2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for 2D game templates:
 * - LrgGame2DTemplate
 * - LrgPlatformerTemplate
 * - LrgTopDownTemplate
 * - LrgShooter2DTemplate
 * - LrgTwinStickTemplate
 * - LrgShmupTemplate
 * - LrgTycoonTemplate
 * - LrgRacing2DTemplate
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

#define SKIP_IF_NULL(ptr) \
    do { \
        if ((ptr) == NULL) \
        { \
            g_test_skip ("Resource not available"); \
            return; \
        } \
    } while (0)

/* ==========================================================================
 * Test Cases - LrgGame2DTemplate Construction
 * ========================================================================== */

static void
test_game_2d_template_new (void)
{
    g_autoptr(LrgGame2DTemplate) template = NULL;

    template = lrg_game_2d_template_new ();

    g_assert_nonnull (template);
    g_assert_true (LRG_IS_GAME_2D_TEMPLATE (template));
    g_assert_true (LRG_IS_GAME_TEMPLATE (template));
}

/* ==========================================================================
 * Test Cases - LrgGame2DTemplate Virtual Resolution
 * ========================================================================== */

static void
test_game_2d_template_virtual_resolution_default (void)
{
    g_autoptr(LrgGame2DTemplate) template = NULL;
    gint width;
    gint height;

    template = lrg_game_2d_template_new ();
    g_assert_nonnull (template);

    /* Default virtual resolution should be set */
    width = lrg_game_2d_template_get_virtual_width (template);
    height = lrg_game_2d_template_get_virtual_height (template);

    g_assert_cmpint (width, >, 0);
    g_assert_cmpint (height, >, 0);
}

static void
test_game_2d_template_virtual_resolution_set (void)
{
    g_autoptr(LrgGame2DTemplate) template = NULL;

    template = lrg_game_2d_template_new ();
    g_assert_nonnull (template);

    /* Set virtual resolution */
    lrg_game_2d_template_set_virtual_width (template, 1920);
    lrg_game_2d_template_set_virtual_height (template, 1080);

    g_assert_cmpint (lrg_game_2d_template_get_virtual_width (template), ==, 1920);
    g_assert_cmpint (lrg_game_2d_template_get_virtual_height (template), ==, 1080);
}

static void
test_game_2d_template_virtual_resolution_set_both (void)
{
    g_autoptr(LrgGame2DTemplate) template = NULL;

    template = lrg_game_2d_template_new ();
    g_assert_nonnull (template);

    /* Set both at once */
    lrg_game_2d_template_set_virtual_resolution (template, 640, 480);

    g_assert_cmpint (lrg_game_2d_template_get_virtual_width (template), ==, 640);
    g_assert_cmpint (lrg_game_2d_template_get_virtual_height (template), ==, 480);
}

/* ==========================================================================
 * Test Cases - LrgGame2DTemplate Scaling Mode
 * ========================================================================== */

static void
test_game_2d_template_scaling_mode (void)
{
    g_autoptr(LrgGame2DTemplate) template = NULL;
    LrgScalingMode mode;

    template = lrg_game_2d_template_new ();
    g_assert_nonnull (template);

    /* Test setting different scaling modes */
    lrg_game_2d_template_set_scaling_mode (template, LRG_SCALING_MODE_LETTERBOX);
    mode = lrg_game_2d_template_get_scaling_mode (template);
    g_assert_cmpint (mode, ==, LRG_SCALING_MODE_LETTERBOX);

    lrg_game_2d_template_set_scaling_mode (template, LRG_SCALING_MODE_STRETCH);
    mode = lrg_game_2d_template_get_scaling_mode (template);
    g_assert_cmpint (mode, ==, LRG_SCALING_MODE_STRETCH);
}

static void
test_game_2d_template_pixel_perfect (void)
{
    g_autoptr(LrgGame2DTemplate) template = NULL;

    template = lrg_game_2d_template_new ();
    g_assert_nonnull (template);

    /* Default should be FALSE */
    g_assert_false (lrg_game_2d_template_get_pixel_perfect (template));

    /* Enable pixel perfect */
    lrg_game_2d_template_set_pixel_perfect (template, TRUE);
    g_assert_true (lrg_game_2d_template_get_pixel_perfect (template));

    /* Disable */
    lrg_game_2d_template_set_pixel_perfect (template, FALSE);
    g_assert_false (lrg_game_2d_template_get_pixel_perfect (template));
}

/* ==========================================================================
 * Test Cases - LrgGame2DTemplate Camera Settings
 * ========================================================================== */

static void
test_game_2d_template_camera_smoothing (void)
{
    g_autoptr(LrgGame2DTemplate) template = NULL;
    gfloat smoothing;

    template = lrg_game_2d_template_new ();
    g_assert_nonnull (template);

    /* Set smoothing */
    lrg_game_2d_template_set_camera_smoothing (template, 0.15f);
    smoothing = lrg_game_2d_template_get_camera_smoothing (template);
    g_assert_cmpfloat_with_epsilon (smoothing, 0.15f, 0.001f);
}

static void
test_game_2d_template_camera_deadzone (void)
{
    g_autoptr(LrgGame2DTemplate) template = NULL;
    gfloat width;
    gfloat height;

    template = lrg_game_2d_template_new ();
    g_assert_nonnull (template);

    /* Set deadzone */
    lrg_game_2d_template_set_camera_deadzone (template, 100.0f, 50.0f);
    lrg_game_2d_template_get_camera_deadzone (template, &width, &height);

    g_assert_cmpfloat_with_epsilon (width, 100.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (height, 50.0f, 0.001f);
}

/* ==========================================================================
 * Test Cases - LrgPlatformerTemplate Construction
 * ========================================================================== */

static void
test_platformer_template_new (void)
{
    g_autoptr(LrgPlatformerTemplate) template = NULL;

    template = lrg_platformer_template_new ();

    g_assert_nonnull (template);
    g_assert_true (LRG_IS_PLATFORMER_TEMPLATE (template));
    g_assert_true (LRG_IS_GAME_2D_TEMPLATE (template));
    g_assert_true (LRG_IS_GAME_TEMPLATE (template));
}

/* ==========================================================================
 * Test Cases - LrgPlatformerTemplate Physics Properties
 * ========================================================================== */

static void
test_platformer_template_gravity (void)
{
    g_autoptr(LrgPlatformerTemplate) template = NULL;
    gfloat gravity;

    template = lrg_platformer_template_new ();
    g_assert_nonnull (template);

    /* Set gravity */
    lrg_platformer_template_set_gravity (template, 980.0f);
    gravity = lrg_platformer_template_get_gravity (template);
    g_assert_cmpfloat_with_epsilon (gravity, 980.0f, 0.001f);
}

static void
test_platformer_template_jump_height (void)
{
    g_autoptr(LrgPlatformerTemplate) template = NULL;
    gfloat height;

    template = lrg_platformer_template_new ();
    g_assert_nonnull (template);

    /* Set jump height */
    lrg_platformer_template_set_jump_height (template, 100.0f);
    height = lrg_platformer_template_get_jump_height (template);
    g_assert_cmpfloat_with_epsilon (height, 100.0f, 0.001f);
}

static void
test_platformer_template_move_speed (void)
{
    g_autoptr(LrgPlatformerTemplate) template = NULL;
    gfloat speed;

    template = lrg_platformer_template_new ();
    g_assert_nonnull (template);

    /* Set move speed */
    lrg_platformer_template_set_move_speed (template, 250.0f);
    speed = lrg_platformer_template_get_move_speed (template);
    g_assert_cmpfloat_with_epsilon (speed, 250.0f, 0.001f);
}

static void
test_platformer_template_coyote_time (void)
{
    g_autoptr(LrgPlatformerTemplate) template = NULL;
    gfloat time;

    template = lrg_platformer_template_new ();
    g_assert_nonnull (template);

    /* Set coyote time */
    lrg_platformer_template_set_coyote_time (template, 0.1f);
    time = lrg_platformer_template_get_coyote_time (template);
    g_assert_cmpfloat_with_epsilon (time, 0.1f, 0.001f);
}

static void
test_platformer_template_jump_buffer_time (void)
{
    g_autoptr(LrgPlatformerTemplate) template = NULL;
    gfloat time;

    template = lrg_platformer_template_new ();
    g_assert_nonnull (template);

    /* Set jump buffer time */
    lrg_platformer_template_set_jump_buffer_time (template, 0.15f);
    time = lrg_platformer_template_get_jump_buffer_time (template);
    g_assert_cmpfloat_with_epsilon (time, 0.15f, 0.001f);
}

/* ==========================================================================
 * Test Cases - LrgPlatformerTemplate Wall Mechanics
 * ========================================================================== */

static void
test_platformer_template_wall_slide (void)
{
    g_autoptr(LrgPlatformerTemplate) template = NULL;

    template = lrg_platformer_template_new ();
    g_assert_nonnull (template);

    /* Test wall slide toggle */
    lrg_platformer_template_set_wall_slide_enabled (template, TRUE);
    g_assert_true (lrg_platformer_template_get_wall_slide_enabled (template));

    lrg_platformer_template_set_wall_slide_enabled (template, FALSE);
    g_assert_false (lrg_platformer_template_get_wall_slide_enabled (template));
}

static void
test_platformer_template_wall_jump (void)
{
    g_autoptr(LrgPlatformerTemplate) template = NULL;

    template = lrg_platformer_template_new ();
    g_assert_nonnull (template);

    /* Test wall jump toggle */
    lrg_platformer_template_set_wall_jump_enabled (template, TRUE);
    g_assert_true (lrg_platformer_template_get_wall_jump_enabled (template));

    lrg_platformer_template_set_wall_jump_enabled (template, FALSE);
    g_assert_false (lrg_platformer_template_get_wall_jump_enabled (template));
}

static void
test_platformer_template_wall_jump_force (void)
{
    g_autoptr(LrgPlatformerTemplate) template = NULL;
    gfloat x;
    gfloat y;

    template = lrg_platformer_template_new ();
    g_assert_nonnull (template);

    /* Set wall jump force */
    lrg_platformer_template_set_wall_jump_force (template, 200.0f, 300.0f);
    lrg_platformer_template_get_wall_jump_force (template, &x, &y);

    g_assert_cmpfloat_with_epsilon (x, 200.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (y, 300.0f, 0.001f);
}

/* ==========================================================================
 * Test Cases - LrgPlatformerTemplate Player State
 * ========================================================================== */

static void
test_platformer_template_player_position (void)
{
    g_autoptr(LrgPlatformerTemplate) template = NULL;
    gfloat x;
    gfloat y;

    template = lrg_platformer_template_new ();
    g_assert_nonnull (template);

    /* Set and get position */
    lrg_platformer_template_set_player_position (template, 100.0f, 200.0f);
    lrg_platformer_template_get_player_position (template, &x, &y);

    g_assert_cmpfloat_with_epsilon (x, 100.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (y, 200.0f, 0.001f);
}

static void
test_platformer_template_velocity (void)
{
    g_autoptr(LrgPlatformerTemplate) template = NULL;
    gfloat vx;
    gfloat vy;

    template = lrg_platformer_template_new ();
    g_assert_nonnull (template);

    /* Set and get velocity */
    lrg_platformer_template_set_velocity (template, 50.0f, -100.0f);
    lrg_platformer_template_get_velocity (template, &vx, &vy);

    g_assert_cmpfloat_with_epsilon (vx, 50.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (vy, -100.0f, 0.001f);
}

/* ==========================================================================
 * Test Cases - LrgTopDownTemplate Construction
 * ========================================================================== */

static void
test_top_down_template_new (void)
{
    g_autoptr(LrgTopDownTemplate) template = NULL;

    template = lrg_top_down_template_new ();

    g_assert_nonnull (template);
    g_assert_true (LRG_IS_TOP_DOWN_TEMPLATE (template));
    g_assert_true (LRG_IS_GAME_2D_TEMPLATE (template));
}

/* ==========================================================================
 * Test Cases - LrgShooter2DTemplate Construction
 * ========================================================================== */

static void
test_shooter_2d_template_new (void)
{
    g_autoptr(LrgShooter2DTemplate) template = NULL;

    template = lrg_shooter_2d_template_new ();

    g_assert_nonnull (template);
    g_assert_true (LRG_IS_SHOOTER_2D_TEMPLATE (template));
    g_assert_true (LRG_IS_GAME_2D_TEMPLATE (template));
}

/* ==========================================================================
 * Test Cases - LrgTwinStickTemplate Construction
 * ========================================================================== */

static void
test_twin_stick_template_new (void)
{
    g_autoptr(LrgTwinStickTemplate) template = NULL;

    template = lrg_twin_stick_template_new ();

    g_assert_nonnull (template);
    g_assert_true (LRG_IS_TWIN_STICK_TEMPLATE (template));
    g_assert_true (LRG_IS_SHOOTER_2D_TEMPLATE (template));
}

/* ==========================================================================
 * Test Cases - LrgShmupTemplate Construction
 * ========================================================================== */

static void
test_shmup_template_new (void)
{
    g_autoptr(LrgShmupTemplate) template = NULL;

    template = lrg_shmup_template_new ();

    g_assert_nonnull (template);
    g_assert_true (LRG_IS_SHMUP_TEMPLATE (template));
    g_assert_true (LRG_IS_SHOOTER_2D_TEMPLATE (template));
}

/* ==========================================================================
 * Test Cases - LrgTycoonTemplate Construction
 * ========================================================================== */

static void
test_tycoon_template_new (void)
{
    g_autoptr(LrgTycoonTemplate) template = NULL;

    template = lrg_tycoon_template_new ();

    g_assert_nonnull (template);
    g_assert_true (LRG_IS_TYCOON_TEMPLATE (template));
    g_assert_true (LRG_IS_GAME_2D_TEMPLATE (template));
}

/* ==========================================================================
 * Test Cases - LrgRacing2DTemplate Construction
 * ========================================================================== */

static void
test_racing_2d_template_new (void)
{
    g_autoptr(LrgRacing2DTemplate) template = NULL;

    template = lrg_racing_2d_template_new ();

    g_assert_nonnull (template);
    g_assert_true (LRG_IS_RACING_2D_TEMPLATE (template));
    g_assert_true (LRG_IS_GAME_2D_TEMPLATE (template));
}

/* ==========================================================================
 * Test Cases - Property Inheritance
 *
 * Verify that derived templates inherit base template properties.
 * ========================================================================== */

static void
test_template_property_inheritance (void)
{
    g_autoptr(LrgPlatformerTemplate) template = NULL;
    gchar *title;

    template = lrg_platformer_template_new ();
    g_assert_nonnull (template);

    /* LrgPlatformerTemplate should inherit "title" property from LrgGameTemplate */
    g_object_set (template, "title", "Test Platformer", NULL);
    g_object_get (template, "title", &title, NULL);

    g_assert_cmpstr (title, ==, "Test Platformer");
    g_free (title);
}

static void
test_template_virtual_resolution_inheritance (void)
{
    g_autoptr(LrgPlatformerTemplate) template = NULL;

    template = lrg_platformer_template_new ();
    g_assert_nonnull (template);

    /* LrgPlatformerTemplate should have virtual resolution from LrgGame2DTemplate */
    lrg_game_2d_template_set_virtual_resolution (LRG_GAME_2D_TEMPLATE (template),
                                                  320, 240);

    g_assert_cmpint (lrg_game_2d_template_get_virtual_width (
                         LRG_GAME_2D_TEMPLATE (template)), ==, 320);
    g_assert_cmpint (lrg_game_2d_template_get_virtual_height (
                         LRG_GAME_2D_TEMPLATE (template)), ==, 240);
}

/* ==========================================================================
 * Test Cases - GObject Construction with Properties
 * ========================================================================== */

static void
test_platformer_construct_with_properties (void)
{
    g_autoptr(LrgPlatformerTemplate) template = NULL;

    template = g_object_new (LRG_TYPE_PLATFORMER_TEMPLATE,
                              "title", "My Platformer",
                              "gravity", 980.0f,
                              "jump-height", 120.0f,
                              NULL);

    g_assert_nonnull (template);
    g_assert_cmpfloat_with_epsilon (lrg_platformer_template_get_gravity (template),
                                     980.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_platformer_template_get_jump_height (template),
                                     120.0f, 0.001f);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* LrgGame2DTemplate Construction */
    g_test_add_func ("/template/2d/new", test_game_2d_template_new);

    /* LrgGame2DTemplate Virtual Resolution */
    g_test_add_func ("/template/2d/virtual-resolution/default",
                      test_game_2d_template_virtual_resolution_default);
    g_test_add_func ("/template/2d/virtual-resolution/set",
                      test_game_2d_template_virtual_resolution_set);
    g_test_add_func ("/template/2d/virtual-resolution/set-both",
                      test_game_2d_template_virtual_resolution_set_both);

    /* LrgGame2DTemplate Scaling */
    g_test_add_func ("/template/2d/scaling-mode", test_game_2d_template_scaling_mode);
    g_test_add_func ("/template/2d/pixel-perfect", test_game_2d_template_pixel_perfect);

    /* LrgGame2DTemplate Camera */
    g_test_add_func ("/template/2d/camera/smoothing",
                      test_game_2d_template_camera_smoothing);
    g_test_add_func ("/template/2d/camera/deadzone",
                      test_game_2d_template_camera_deadzone);

    /* LrgPlatformerTemplate Construction */
    g_test_add_func ("/template/platformer/new", test_platformer_template_new);

    /* LrgPlatformerTemplate Physics */
    g_test_add_func ("/template/platformer/gravity", test_platformer_template_gravity);
    g_test_add_func ("/template/platformer/jump-height",
                      test_platformer_template_jump_height);
    g_test_add_func ("/template/platformer/move-speed",
                      test_platformer_template_move_speed);
    g_test_add_func ("/template/platformer/coyote-time",
                      test_platformer_template_coyote_time);
    g_test_add_func ("/template/platformer/jump-buffer-time",
                      test_platformer_template_jump_buffer_time);

    /* LrgPlatformerTemplate Wall Mechanics */
    g_test_add_func ("/template/platformer/wall-slide",
                      test_platformer_template_wall_slide);
    g_test_add_func ("/template/platformer/wall-jump",
                      test_platformer_template_wall_jump);
    g_test_add_func ("/template/platformer/wall-jump-force",
                      test_platformer_template_wall_jump_force);

    /* LrgPlatformerTemplate Player State */
    g_test_add_func ("/template/platformer/player-position",
                      test_platformer_template_player_position);
    g_test_add_func ("/template/platformer/velocity",
                      test_platformer_template_velocity);

    /* Other 2D Templates Construction */
    g_test_add_func ("/template/top-down/new", test_top_down_template_new);
    g_test_add_func ("/template/shooter-2d/new", test_shooter_2d_template_new);
    g_test_add_func ("/template/twin-stick/new", test_twin_stick_template_new);
    g_test_add_func ("/template/shmup/new", test_shmup_template_new);
    g_test_add_func ("/template/tycoon/new", test_tycoon_template_new);
    g_test_add_func ("/template/racing-2d/new", test_racing_2d_template_new);

    /* Property Inheritance */
    g_test_add_func ("/template/inheritance/title",
                      test_template_property_inheritance);
    g_test_add_func ("/template/inheritance/virtual-resolution",
                      test_template_virtual_resolution_inheritance);

    /* GObject Construction */
    g_test_add_func ("/template/platformer/construct-with-properties",
                      test_platformer_construct_with_properties);

    return g_test_run ();
}
