/* test-photomode.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the photo mode module.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Skip Macro for Headless Environment
 * ========================================================================== */

#define SKIP_IF_NO_DISPLAY() \
    do { \
        if (g_getenv ("DISPLAY") == NULL && g_getenv ("WAYLAND_DISPLAY") == NULL) \
        { \
            g_test_skip ("No display available (headless environment)"); \
            return; \
        } \
    } while (0)

/* ==========================================================================
 * LrgScreenshot Tests
 * ========================================================================== */

static void
test_screenshot_new (void)
{
    g_autoptr(LrgScreenshot) screenshot = NULL;

    screenshot = lrg_screenshot_new ();
    g_assert_nonnull (screenshot);
    g_assert_true (LRG_IS_SCREENSHOT (screenshot));
    g_assert_cmpint (lrg_screenshot_get_width (screenshot), ==, 0);
    g_assert_cmpint (lrg_screenshot_get_height (screenshot), ==, 0);
    g_assert_null (lrg_screenshot_get_image (screenshot));
}

static void
test_screenshot_new_from_image (void)
{
    g_autoptr(LrgScreenshot) screenshot = NULL;
    g_autoptr(GrlImage) image = NULL;

    SKIP_IF_NO_DISPLAY ();

    /* Create a simple test image */
    image = grl_image_new_color (100, 100, grl_color_new (255, 0, 0, 255));
    if (image == NULL)
    {
        g_test_skip ("Could not create test image");
        return;
    }

    screenshot = lrg_screenshot_new_from_image (image);
    g_assert_nonnull (screenshot);
    g_assert_cmpint (lrg_screenshot_get_width (screenshot), ==, 100);
    g_assert_cmpint (lrg_screenshot_get_height (screenshot), ==, 100);
    g_assert_nonnull (lrg_screenshot_get_image (screenshot));
}

static void
test_screenshot_properties (void)
{
    g_autoptr(LrgScreenshot) screenshot = NULL;
    g_autoptr(GrlImage) image = NULL;
    gint width;
    gint height;

    SKIP_IF_NO_DISPLAY ();

    image = grl_image_new_color (200, 150, grl_color_new (0, 255, 0, 255));
    if (image == NULL)
    {
        g_test_skip ("Could not create test image");
        return;
    }

    screenshot = lrg_screenshot_new_from_image (image);
    g_assert_nonnull (screenshot);

    g_object_get (screenshot,
                  "width", &width,
                  "height", &height,
                  NULL);

    g_assert_cmpint (width, ==, 200);
    g_assert_cmpint (height, ==, 150);
}

/* ==========================================================================
 * LrgPhotoCameraController Tests
 * ========================================================================== */

static void
test_camera_controller_new (void)
{
    g_autoptr(LrgPhotoCameraController) controller = NULL;

    controller = lrg_photo_camera_controller_new ();
    g_assert_nonnull (controller);
    g_assert_true (LRG_IS_PHOTO_CAMERA_CONTROLLER (controller));
}

static void
test_camera_controller_get_camera (void)
{
    g_autoptr(LrgPhotoCameraController) controller = NULL;
    LrgCamera3D *camera;

    controller = lrg_photo_camera_controller_new ();
    camera = lrg_photo_camera_controller_get_camera (controller);

    g_assert_nonnull (camera);
    g_assert_true (LRG_IS_CAMERA3D (camera));
}

static void
test_camera_controller_position (void)
{
    g_autoptr(LrgPhotoCameraController) controller = NULL;
    g_autoptr(GrlVector3) position = NULL;
    g_autoptr(GrlVector3) new_pos = NULL;
    g_autoptr(GrlVector3) retrieved = NULL;

    controller = lrg_photo_camera_controller_new ();
    position = lrg_photo_camera_controller_get_position (controller);
    g_assert_nonnull (position);

    /* Set a new position */
    new_pos = grl_vector3_new (10.0f, 20.0f, 30.0f);
    lrg_photo_camera_controller_set_position (controller, new_pos);

    retrieved = lrg_photo_camera_controller_get_position (controller);
    g_assert_cmpfloat_with_epsilon (retrieved->x, 10.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (retrieved->y, 20.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (retrieved->z, 30.0f, 0.001f);
}

static void
test_camera_controller_rotation (void)
{
    g_autoptr(LrgPhotoCameraController) controller = NULL;

    controller = lrg_photo_camera_controller_new ();

    /* Test yaw */
    lrg_photo_camera_controller_set_yaw (controller, 45.0f);
    g_assert_cmpfloat_with_epsilon (
        lrg_photo_camera_controller_get_yaw (controller), 45.0f, 0.001f);

    /* Test pitch (should be clamped) */
    lrg_photo_camera_controller_set_pitch (controller, 95.0f);
    g_assert_cmpfloat_with_epsilon (
        lrg_photo_camera_controller_get_pitch (controller), 89.0f, 0.001f);

    lrg_photo_camera_controller_set_pitch (controller, -95.0f);
    g_assert_cmpfloat_with_epsilon (
        lrg_photo_camera_controller_get_pitch (controller), -89.0f, 0.001f);

    /* Test roll */
    lrg_photo_camera_controller_set_roll (controller, 15.0f);
    g_assert_cmpfloat_with_epsilon (
        lrg_photo_camera_controller_get_roll (controller), 15.0f, 0.001f);
}

static void
test_camera_controller_config (void)
{
    g_autoptr(LrgPhotoCameraController) controller = NULL;

    controller = lrg_photo_camera_controller_new ();

    /* Test move speed */
    lrg_photo_camera_controller_set_move_speed (controller, 25.0f);
    g_assert_cmpfloat_with_epsilon (
        lrg_photo_camera_controller_get_move_speed (controller), 25.0f, 0.001f);

    /* Test look sensitivity */
    lrg_photo_camera_controller_set_look_sensitivity (controller, 1.5f);
    g_assert_cmpfloat_with_epsilon (
        lrg_photo_camera_controller_get_look_sensitivity (controller), 1.5f, 0.001f);

    /* Test smoothing */
    lrg_photo_camera_controller_set_smoothing (controller, 0.5f);
    g_assert_cmpfloat_with_epsilon (
        lrg_photo_camera_controller_get_smoothing (controller), 0.5f, 0.001f);

    /* Test FOV */
    lrg_photo_camera_controller_set_fov (controller, 60.0f);
    g_assert_cmpfloat_with_epsilon (
        lrg_photo_camera_controller_get_fov (controller), 60.0f, 0.001f);
}

static void
test_camera_controller_reset (void)
{
    g_autoptr(LrgPhotoCameraController) controller = NULL;
    g_autoptr(GrlVector3) new_pos = NULL;
    g_autoptr(GrlVector3) retrieved = NULL;

    controller = lrg_photo_camera_controller_new ();

    /* Modify state */
    new_pos = grl_vector3_new (100.0f, 100.0f, 100.0f);
    lrg_photo_camera_controller_set_position (controller, new_pos);
    lrg_photo_camera_controller_set_yaw (controller, 90.0f);
    lrg_photo_camera_controller_set_pitch (controller, 45.0f);

    /* Reset */
    lrg_photo_camera_controller_reset (controller);

    /* Check reset values (default position is 0, 10, 10) */
    retrieved = lrg_photo_camera_controller_get_position (controller);
    g_assert_cmpfloat_with_epsilon (retrieved->x, 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (retrieved->y, 10.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (retrieved->z, 10.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_photo_camera_controller_get_yaw (controller), 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_photo_camera_controller_get_pitch (controller), 0.0f, 0.001f);
}

static void
test_camera_controller_update (void)
{
    g_autoptr(LrgPhotoCameraController) controller = NULL;

    controller = lrg_photo_camera_controller_new ();

    /* Update should not crash */
    lrg_photo_camera_controller_update (controller, 0.016f);
    lrg_photo_camera_controller_update (controller, 0.016f);
    lrg_photo_camera_controller_update (controller, 0.016f);
}

/* ==========================================================================
 * LrgPhotoMode Tests
 * ========================================================================== */

static void
test_photo_mode_singleton (void)
{
    LrgPhotoMode *mode1;
    LrgPhotoMode *mode2;

    mode1 = lrg_photo_mode_get_default ();
    mode2 = lrg_photo_mode_get_default ();

    g_assert_nonnull (mode1);
    g_assert_true (LRG_IS_PHOTO_MODE (mode1));
    g_assert_true (mode1 == mode2);
}

static void
test_photo_mode_enter_exit (void)
{
    LrgPhotoMode *mode;
    g_autoptr(GError) error = NULL;
    gboolean result;

    mode = lrg_photo_mode_get_default ();

    /* Ensure not active initially */
    if (lrg_photo_mode_is_active (mode))
        lrg_photo_mode_exit (mode);

    g_assert_false (lrg_photo_mode_is_active (mode));

    /* Enter photo mode */
    result = lrg_photo_mode_enter (mode, NULL, &error);
    g_assert_true (result);
    g_assert_no_error (error);
    g_assert_true (lrg_photo_mode_is_active (mode));
    g_assert_nonnull (lrg_photo_mode_get_camera_controller (mode));

    /* Try to enter again - should fail */
    result = lrg_photo_mode_enter (mode, NULL, &error);
    g_assert_false (result);
    g_assert_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_ALREADY_ACTIVE);
    g_clear_error (&error);

    /* Exit photo mode */
    lrg_photo_mode_exit (mode);
    g_assert_false (lrg_photo_mode_is_active (mode));
    g_assert_null (lrg_photo_mode_get_camera_controller (mode));
}

static void
test_photo_mode_toggle (void)
{
    LrgPhotoMode *mode;
    g_autoptr(GError) error = NULL;

    mode = lrg_photo_mode_get_default ();

    /* Ensure not active */
    if (lrg_photo_mode_is_active (mode))
        lrg_photo_mode_exit (mode);

    g_assert_false (lrg_photo_mode_is_active (mode));

    /* Toggle on */
    lrg_photo_mode_toggle (mode, NULL, &error);
    g_assert_no_error (error);
    g_assert_true (lrg_photo_mode_is_active (mode));

    /* Toggle off */
    lrg_photo_mode_toggle (mode, NULL, &error);
    g_assert_no_error (error);
    g_assert_false (lrg_photo_mode_is_active (mode));
}

static void
test_photo_mode_ui_visibility (void)
{
    LrgPhotoMode *mode;
    g_autoptr(GError) error = NULL;

    mode = lrg_photo_mode_get_default ();

    /* Ensure active state */
    if (lrg_photo_mode_is_active (mode))
        lrg_photo_mode_exit (mode);
    lrg_photo_mode_enter (mode, NULL, &error);

    /* Default UI visible */
    g_assert_true (lrg_photo_mode_get_ui_visible (mode));

    /* Hide UI */
    lrg_photo_mode_set_ui_visible (mode, FALSE);
    g_assert_false (lrg_photo_mode_get_ui_visible (mode));

    /* Toggle UI */
    lrg_photo_mode_toggle_ui (mode);
    g_assert_true (lrg_photo_mode_get_ui_visible (mode));

    lrg_photo_mode_exit (mode);
}

static void
test_photo_mode_screenshot_directory (void)
{
    LrgPhotoMode *mode;

    mode = lrg_photo_mode_get_default ();

    /* Should have a default directory */
    g_assert_nonnull (lrg_photo_mode_get_screenshot_directory (mode));

    /* Set custom directory */
    lrg_photo_mode_set_screenshot_directory (mode, "/tmp/screenshots");
    g_assert_cmpstr (lrg_photo_mode_get_screenshot_directory (mode), ==, "/tmp/screenshots");

    /* Reset to default */
    lrg_photo_mode_set_screenshot_directory (mode, g_get_user_special_dir (G_USER_DIRECTORY_PICTURES));
}

static void
test_photo_mode_default_format (void)
{
    LrgPhotoMode *mode;

    mode = lrg_photo_mode_get_default ();

    /* Default should be PNG */
    g_assert_cmpint (lrg_photo_mode_get_default_format (mode), ==, LRG_SCREENSHOT_FORMAT_PNG);

    /* Change to JPG */
    lrg_photo_mode_set_default_format (mode, LRG_SCREENSHOT_FORMAT_JPG);
    g_assert_cmpint (lrg_photo_mode_get_default_format (mode), ==, LRG_SCREENSHOT_FORMAT_JPG);

    /* Reset to PNG */
    lrg_photo_mode_set_default_format (mode, LRG_SCREENSHOT_FORMAT_PNG);
}

static void
test_photo_mode_generate_filename (void)
{
    LrgPhotoMode *mode;
    g_autofree gchar *filename_png = NULL;
    g_autofree gchar *filename_jpg = NULL;

    mode = lrg_photo_mode_get_default ();

    filename_png = lrg_photo_mode_generate_filename (mode, LRG_SCREENSHOT_FORMAT_PNG);
    g_assert_nonnull (filename_png);
    g_assert_true (g_str_has_suffix (filename_png, ".png"));

    filename_jpg = lrg_photo_mode_generate_filename (mode, LRG_SCREENSHOT_FORMAT_JPG);
    g_assert_nonnull (filename_jpg);
    g_assert_true (g_str_has_suffix (filename_jpg, ".jpg"));

    /* Filenames should be different */
    g_assert_cmpstr (filename_png, !=, filename_jpg);
}

static void
test_photo_mode_update (void)
{
    LrgPhotoMode *mode;
    g_autoptr(GError) error = NULL;

    mode = lrg_photo_mode_get_default ();

    /* Ensure not active initially */
    if (lrg_photo_mode_is_active (mode))
        lrg_photo_mode_exit (mode);

    /* Update when not active - should not crash */
    lrg_photo_mode_update (mode, 0.016f);

    /* Enter and update */
    lrg_photo_mode_enter (mode, NULL, &error);
    g_assert_no_error (error);

    lrg_photo_mode_update (mode, 0.016f);
    lrg_photo_mode_update (mode, 0.016f);
    lrg_photo_mode_update (mode, 0.016f);

    lrg_photo_mode_exit (mode);
}

static void
test_photo_mode_properties (void)
{
    LrgPhotoMode *mode;
    gboolean active;
    gboolean ui_visible;
    gchar *directory;
    gint format;

    mode = lrg_photo_mode_get_default ();

    /* Ensure consistent state */
    if (lrg_photo_mode_is_active (mode))
        lrg_photo_mode_exit (mode);

    g_object_get (mode,
                  "active", &active,
                  "ui-visible", &ui_visible,
                  "screenshot-directory", &directory,
                  "default-format", &format,
                  NULL);

    g_assert_false (active);
    g_assert_true (ui_visible);
    g_assert_nonnull (directory);

    g_free (directory);
}

/* ==========================================================================
 * Signal Tests
 * ========================================================================== */

static gboolean signal_entered_received = FALSE;
static gboolean signal_exited_received = FALSE;

static void
on_entered (LrgPhotoMode *mode, gpointer user_data)
{
    signal_entered_received = TRUE;
}

static void
on_exited (LrgPhotoMode *mode, gpointer user_data)
{
    signal_exited_received = TRUE;
}

static void
test_photo_mode_signals (void)
{
    LrgPhotoMode *mode;
    g_autoptr(GError) error = NULL;

    mode = lrg_photo_mode_get_default ();

    /* Ensure consistent state */
    if (lrg_photo_mode_is_active (mode))
        lrg_photo_mode_exit (mode);

    /* Connect signals */
    g_signal_connect (mode, "entered", G_CALLBACK (on_entered), NULL);
    g_signal_connect (mode, "exited", G_CALLBACK (on_exited), NULL);

    /* Reset flags */
    signal_entered_received = FALSE;
    signal_exited_received = FALSE;

    /* Enter */
    lrg_photo_mode_enter (mode, NULL, &error);
    g_assert_no_error (error);
    g_assert_true (signal_entered_received);
    g_assert_false (signal_exited_received);

    /* Exit */
    lrg_photo_mode_exit (mode);
    g_assert_true (signal_exited_received);

    /* Disconnect signals */
    g_signal_handlers_disconnect_by_func (mode, on_entered, NULL);
    g_signal_handlers_disconnect_by_func (mode, on_exited, NULL);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int argc, char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Screenshot tests */
    g_test_add_func ("/photomode/screenshot/new", test_screenshot_new);
    g_test_add_func ("/photomode/screenshot/new-from-image", test_screenshot_new_from_image);
    g_test_add_func ("/photomode/screenshot/properties", test_screenshot_properties);

    /* Camera controller tests */
    g_test_add_func ("/photomode/camera-controller/new", test_camera_controller_new);
    g_test_add_func ("/photomode/camera-controller/get-camera", test_camera_controller_get_camera);
    g_test_add_func ("/photomode/camera-controller/position", test_camera_controller_position);
    g_test_add_func ("/photomode/camera-controller/rotation", test_camera_controller_rotation);
    g_test_add_func ("/photomode/camera-controller/config", test_camera_controller_config);
    g_test_add_func ("/photomode/camera-controller/reset", test_camera_controller_reset);
    g_test_add_func ("/photomode/camera-controller/update", test_camera_controller_update);

    /* Photo mode tests */
    g_test_add_func ("/photomode/photo-mode/singleton", test_photo_mode_singleton);
    g_test_add_func ("/photomode/photo-mode/enter-exit", test_photo_mode_enter_exit);
    g_test_add_func ("/photomode/photo-mode/toggle", test_photo_mode_toggle);
    g_test_add_func ("/photomode/photo-mode/ui-visibility", test_photo_mode_ui_visibility);
    g_test_add_func ("/photomode/photo-mode/screenshot-directory", test_photo_mode_screenshot_directory);
    g_test_add_func ("/photomode/photo-mode/default-format", test_photo_mode_default_format);
    g_test_add_func ("/photomode/photo-mode/generate-filename", test_photo_mode_generate_filename);
    g_test_add_func ("/photomode/photo-mode/update", test_photo_mode_update);
    g_test_add_func ("/photomode/photo-mode/properties", test_photo_mode_properties);
    g_test_add_func ("/photomode/photo-mode/signals", test_photo_mode_signals);

    return g_test_run ();
}
