/* test-template.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the base LrgGameTemplate type.
 * Tests construction, properties, game feel systems (hit stop, screen shake),
 * time scale, and subsystem access.
 */

#include <glib.h>
#include <math.h>
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
 * Test Cases - LrgGameTemplate Construction
 * ========================================================================== */

static void
test_game_template_new (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;

    template = lrg_game_template_new ();

    g_assert_nonnull (template);
    g_assert_true (LRG_IS_GAME_TEMPLATE (template));
}

static void
test_game_template_new_with_properties (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;

    template = g_object_new (LRG_TYPE_GAME_TEMPLATE,
                              "title", "Test Game",
                              "window-width", 1280,
                              "window-height", 720,
                              NULL);

    g_assert_nonnull (template);
    g_assert_cmpstr (lrg_game_template_get_title (template), ==, "Test Game");
}

/* ==========================================================================
 * Test Cases - LrgGameTemplate Properties
 * ========================================================================== */

static void
test_game_template_title (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;

    template = lrg_game_template_new ();
    g_assert_nonnull (template);

    /* Default title should exist */
    g_assert_nonnull (lrg_game_template_get_title (template));

    /* Set and verify title */
    lrg_game_template_set_title (template, "My Game Title");
    g_assert_cmpstr (lrg_game_template_get_title (template), ==, "My Game Title");
}

static void
test_game_template_title_via_property (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;
    gchar *title = NULL;

    template = lrg_game_template_new ();
    g_assert_nonnull (template);

    g_object_set (template, "title", "Property Title", NULL);
    g_object_get (template, "title", &title, NULL);

    g_assert_cmpstr (title, ==, "Property Title");
    g_free (title);
}

static void
test_game_template_window_size (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;
    gint width;
    gint height;

    template = g_object_new (LRG_TYPE_GAME_TEMPLATE,
                              "window-width", 1920,
                              "window-height", 1080,
                              NULL);
    g_assert_nonnull (template);

    lrg_game_template_get_window_size (template, &width, &height);

    g_assert_cmpint (width, ==, 1920);
    g_assert_cmpint (height, ==, 1080);
}

static void
test_game_template_window_size_null_params (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;
    gint width;

    template = g_object_new (LRG_TYPE_GAME_TEMPLATE,
                              "window-width", 800,
                              "window-height", 600,
                              NULL);
    g_assert_nonnull (template);

    /* Should handle NULL for height without crashing */
    lrg_game_template_get_window_size (template, &width, NULL);
    g_assert_cmpint (width, ==, 800);

    /* Should handle NULL for width without crashing */
    lrg_game_template_get_window_size (template, NULL, NULL);
}

static void
test_game_template_set_window_size (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;
    gint width;
    gint height;

    /* Test without running - should update config values */
    template = g_object_new (LRG_TYPE_GAME_TEMPLATE,
                              "window-width", 800,
                              "window-height", 600,
                              NULL);
    g_assert_nonnull (template);

    lrg_game_template_set_window_size (template, 1920, 1080);
    lrg_game_template_get_window_size (template, &width, &height);

    g_assert_cmpint (width, ==, 1920);
    g_assert_cmpint (height, ==, 1080);
}

static void
test_game_template_is_fullscreen (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;
    gboolean fullscreen;

    template = lrg_game_template_new ();
    g_assert_nonnull (template);

    /* Before running, should return FALSE */
    fullscreen = lrg_game_template_is_fullscreen (template);
    g_assert_false (fullscreen);
}

/* ==========================================================================
 * Test Cases - LrgGameTemplate Time Scale
 * ========================================================================== */

static void
test_game_template_time_scale_default (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;
    gdouble time_scale;

    template = lrg_game_template_new ();
    g_assert_nonnull (template);

    time_scale = lrg_game_template_get_time_scale (template);

    /* Default should be 1.0 (normal speed) */
    g_assert_cmpfloat_with_epsilon (time_scale, 1.0, 0.001);
}

static void
test_game_template_time_scale_set (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;
    gdouble time_scale;

    template = lrg_game_template_new ();
    g_assert_nonnull (template);

    /* Slow motion */
    lrg_game_template_set_time_scale (template, 0.5);
    time_scale = lrg_game_template_get_time_scale (template);
    g_assert_cmpfloat_with_epsilon (time_scale, 0.5, 0.001);

    /* Fast forward */
    lrg_game_template_set_time_scale (template, 2.0);
    time_scale = lrg_game_template_get_time_scale (template);
    g_assert_cmpfloat_with_epsilon (time_scale, 2.0, 0.001);

    /* Back to normal */
    lrg_game_template_set_time_scale (template, 1.0);
    time_scale = lrg_game_template_get_time_scale (template);
    g_assert_cmpfloat_with_epsilon (time_scale, 1.0, 0.001);
}

static void
test_game_template_time_scale_zero (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;
    gdouble time_scale;

    template = lrg_game_template_new ();
    g_assert_nonnull (template);

    /* Pause (zero time scale) */
    lrg_game_template_set_time_scale (template, 0.0);
    time_scale = lrg_game_template_get_time_scale (template);
    g_assert_cmpfloat_with_epsilon (time_scale, 0.0, 0.001);
}

/* ==========================================================================
 * Test Cases - LrgGameTemplate Screen Shake
 * ========================================================================== */

static void
test_game_template_shake_offset_default (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;
    gfloat x;
    gfloat y;

    template = lrg_game_template_new ();
    g_assert_nonnull (template);

    /* Without any shake, offset should be zero */
    lrg_game_template_get_shake_offset (template, &x, &y);

    g_assert_cmpfloat_with_epsilon (x, 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (y, 0.0f, 0.001f);
}

static void
test_game_template_shake_offset_null_params (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;
    gfloat x;

    template = lrg_game_template_new ();
    g_assert_nonnull (template);

    /* Should handle NULL parameters without crashing */
    lrg_game_template_get_shake_offset (template, &x, NULL);
    lrg_game_template_get_shake_offset (template, NULL, NULL);

    /* No crash means success */
    g_assert_true (TRUE);
}

static void
test_game_template_shake (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;

    template = lrg_game_template_new ();
    g_assert_nonnull (template);

    /* Apply shake - should not crash */
    lrg_game_template_shake (template, 0.5f);

    /* Apply max shake */
    lrg_game_template_shake (template, 1.0f);

    /* Apply zero shake (no effect) */
    lrg_game_template_shake (template, 0.0f);
}

static void
test_game_template_shake_with_params (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;

    template = lrg_game_template_new ();
    g_assert_nonnull (template);

    /* Apply shake with custom parameters */
    lrg_game_template_shake_with_params (template,
                                          0.5f,   /* trauma */
                                          0.8f,   /* decay */
                                          30.0f); /* frequency */

    /* Should not crash */
    g_assert_true (TRUE);
}

/* ==========================================================================
 * Test Cases - LrgGameTemplate Hit Stop
 * ========================================================================== */

static void
test_game_template_hit_stop (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;

    template = lrg_game_template_new ();
    g_assert_nonnull (template);

    /* Apply hit stop - should not crash */
    lrg_game_template_hit_stop (template, 0.1);

    /* Apply longer hit stop */
    lrg_game_template_hit_stop (template, 0.5);

    /* Apply zero duration */
    lrg_game_template_hit_stop (template, 0.0);
}

/* ==========================================================================
 * Test Cases - LrgGameTemplate Camera
 * ========================================================================== */

static void
test_game_template_camera_position_default (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;
    gfloat x;
    gfloat y;

    template = lrg_game_template_new ();
    g_assert_nonnull (template);

    lrg_game_template_get_camera_position (template, &x, &y);

    /* Default camera position should be at origin or center */
    /* We just verify it doesn't crash and returns reasonable values */
    g_assert_true (isfinite (x));
    g_assert_true (isfinite (y));
}

static void
test_game_template_camera_position_null_params (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;
    gfloat x;

    template = lrg_game_template_new ();
    g_assert_nonnull (template);

    /* Should handle NULL parameters without crashing */
    lrg_game_template_get_camera_position (template, &x, NULL);
    lrg_game_template_get_camera_position (template, NULL, NULL);

    g_assert_true (TRUE);
}

static void
test_game_template_camera_follow (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;

    template = lrg_game_template_new ();
    g_assert_nonnull (template);

    /* Enable camera follow */
    lrg_game_template_set_camera_follow (template, TRUE, 0.1f);

    /* Set deadzone */
    lrg_game_template_set_camera_deadzone (template, 50.0f, 30.0f);

    /* Update follow target */
    lrg_game_template_update_camera_follow_target (template, 100.0f, 200.0f);

    /* Disable camera follow */
    lrg_game_template_set_camera_follow (template, FALSE, 0.0f);
}

static void
test_game_template_camera_zoom_pulse (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;

    template = lrg_game_template_new ();
    g_assert_nonnull (template);

    /* Apply zoom pulse */
    lrg_game_template_camera_zoom_pulse (template, 0.1f, 0.2f);

    /* Apply negative zoom (zoom out) */
    lrg_game_template_camera_zoom_pulse (template, -0.1f, 0.3f);

    g_assert_true (TRUE);
}

/* ==========================================================================
 * Test Cases - LrgGameTemplate Interpolation
 * ========================================================================== */

static void
test_game_template_interpolation_alpha (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;
    gdouble alpha;

    template = lrg_game_template_new ();
    g_assert_nonnull (template);

    alpha = lrg_game_template_get_interpolation_alpha (template);

    /* Alpha should be between 0 and 1 */
    g_assert_cmpfloat (alpha, >=, 0.0);
    g_assert_cmpfloat (alpha, <=, 1.0);
}

/* ==========================================================================
 * Test Cases - LrgGameTemplate Subsystem Access
 * ========================================================================== */

static void
test_game_template_get_state_manager (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;
    LrgGameStateManager *manager;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_game_template_new ();
    SKIP_IF_NULL (template);

    manager = lrg_game_template_get_state_manager (template);

    /* State manager may be NULL if not initialized with configure() */
    if (manager != NULL)
    {
        g_assert_true (LRG_IS_GAME_STATE_MANAGER (manager));
    }
}

static void
test_game_template_get_input_map (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;
    LrgInputMap *map;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_game_template_new ();
    SKIP_IF_NULL (template);

    map = lrg_game_template_get_input_map (template);

    if (map != NULL)
    {
        g_assert_true (LRG_IS_INPUT_MAP (map));
    }
}

static void
test_game_template_get_event_bus (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;
    LrgEventBus *bus;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_game_template_new ();
    SKIP_IF_NULL (template);

    bus = lrg_game_template_get_event_bus (template);

    if (bus != NULL)
    {
        g_assert_true (LRG_IS_EVENT_BUS (bus));
    }
}

static void
test_game_template_get_engine (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;
    LrgEngine *engine;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_game_template_new ();
    SKIP_IF_NULL (template);

    engine = lrg_game_template_get_engine (template);

    if (engine != NULL)
    {
        g_assert_true (LRG_IS_ENGINE (engine));
    }
}

/* ==========================================================================
 * Test Cases - LrgGameTemplate Pause State
 * ========================================================================== */

static void
test_game_template_paused_default (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;

    template = lrg_game_template_new ();
    g_assert_nonnull (template);

    /* Should not be paused initially */
    g_assert_false (lrg_game_template_is_paused (template));
}

/* ==========================================================================
 * Test Cases - LrgGameTemplate Focus
 * ========================================================================== */

static void
test_game_template_has_focus (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;
    gboolean has_focus;

    template = lrg_game_template_new ();
    g_assert_nonnull (template);

    /* Just verify this doesn't crash and returns a boolean */
    has_focus = lrg_game_template_has_focus (template);

    g_assert_true (has_focus == TRUE || has_focus == FALSE);
}

/* ==========================================================================
 * Test Cases - LrgGameTemplate Type Hierarchy
 * ========================================================================== */

static void
test_game_template_type_hierarchy (void)
{
    g_autoptr(LrgGameTemplate) template = NULL;

    template = lrg_game_template_new ();
    g_assert_nonnull (template);

    /* LrgGameTemplate should inherit from GObject */
    g_assert_true (G_IS_OBJECT (template));
    g_assert_true (LRG_IS_GAME_TEMPLATE (template));
}

static void
test_game_template_derivable (void)
{
    GType type;
    GTypeClass *klass;

    type = LRG_TYPE_GAME_TEMPLATE;

    /* Should be derivable */
    g_assert_true (G_TYPE_IS_DERIVABLE (type));

    /* Class should have virtual methods */
    klass = g_type_class_ref (type);
    g_assert_nonnull (klass);

    g_type_class_unref (klass);
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
    g_test_add_func ("/template/base/new",
                     test_game_template_new);
    g_test_add_func ("/template/base/new-with-properties",
                     test_game_template_new_with_properties);

    /* Property tests */
    g_test_add_func ("/template/base/title",
                     test_game_template_title);
    g_test_add_func ("/template/base/title-via-property",
                     test_game_template_title_via_property);
    g_test_add_func ("/template/base/window-size",
                     test_game_template_window_size);
    g_test_add_func ("/template/base/window-size-null-params",
                     test_game_template_window_size_null_params);
    g_test_add_func ("/template/base/set-window-size",
                     test_game_template_set_window_size);
    g_test_add_func ("/template/base/is-fullscreen",
                     test_game_template_is_fullscreen);

    /* Time scale tests */
    g_test_add_func ("/template/base/time-scale/default",
                     test_game_template_time_scale_default);
    g_test_add_func ("/template/base/time-scale/set",
                     test_game_template_time_scale_set);
    g_test_add_func ("/template/base/time-scale/zero",
                     test_game_template_time_scale_zero);

    /* Screen shake tests */
    g_test_add_func ("/template/base/shake/offset-default",
                     test_game_template_shake_offset_default);
    g_test_add_func ("/template/base/shake/offset-null-params",
                     test_game_template_shake_offset_null_params);
    g_test_add_func ("/template/base/shake/apply",
                     test_game_template_shake);
    g_test_add_func ("/template/base/shake/with-params",
                     test_game_template_shake_with_params);

    /* Hit stop tests */
    g_test_add_func ("/template/base/hit-stop",
                     test_game_template_hit_stop);

    /* Camera tests */
    g_test_add_func ("/template/base/camera/position-default",
                     test_game_template_camera_position_default);
    g_test_add_func ("/template/base/camera/position-null-params",
                     test_game_template_camera_position_null_params);
    g_test_add_func ("/template/base/camera/follow",
                     test_game_template_camera_follow);
    g_test_add_func ("/template/base/camera/zoom-pulse",
                     test_game_template_camera_zoom_pulse);

    /* Interpolation tests */
    g_test_add_func ("/template/base/interpolation-alpha",
                     test_game_template_interpolation_alpha);

    /* Subsystem access tests */
    g_test_add_func ("/template/base/subsystem/state-manager",
                     test_game_template_get_state_manager);
    g_test_add_func ("/template/base/subsystem/input-map",
                     test_game_template_get_input_map);
    g_test_add_func ("/template/base/subsystem/event-bus",
                     test_game_template_get_event_bus);
    g_test_add_func ("/template/base/subsystem/engine",
                     test_game_template_get_engine);

    /* Pause state tests */
    g_test_add_func ("/template/base/paused-default",
                     test_game_template_paused_default);

    /* Focus tests */
    g_test_add_func ("/template/base/has-focus",
                     test_game_template_has_focus);

    /* Type hierarchy tests */
    g_test_add_func ("/template/base/type-hierarchy",
                     test_game_template_type_hierarchy);
    g_test_add_func ("/template/base/derivable",
                     test_game_template_derivable);

    return g_test_run ();
}
