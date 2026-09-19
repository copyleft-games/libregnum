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

typedef GObject InputHost;
typedef GObjectClass InputHostClass;

static void input_host_iface_init (LrgGameHostInterface *iface);
GType input_host_get_type (void);
G_DEFINE_TYPE_WITH_CODE (InputHost, input_host, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_GAME_HOST,
                                                input_host_iface_init))

static LrgEngine *
input_host_get_engine (LrgGameHost *host)
{
    return lrg_engine_get_default ();
}

static void
input_host_iface_init (LrgGameHostInterface *iface)
{
    iface->get_engine = input_host_get_engine;
}

static void
input_host_class_init (InputHostClass *klass)
{
}

static void
input_host_init (InputHost *self)
{
}

typedef struct
{
    LrgGameTemplate parent_instance;
    LrgInputBinding *binding;
    gboolean expect_pressed;
    gboolean expect_released;
    guint input_calls;
    guint fixed_calls;
} InputTemplate;

typedef LrgGameTemplateClass InputTemplateClass;

GType input_template_get_type (void);
G_DEFINE_TYPE (InputTemplate, input_template, LRG_TYPE_GAME_TEMPLATE)

static gboolean
input_template_handle_input (LrgGameTemplate *template)
{
    InputTemplate *self = (InputTemplate *)template;

    g_assert_cmpint (lrg_input_binding_is_pressed (self->binding), ==,
                     self->expect_pressed);
    g_assert_cmpint (lrg_input_binding_is_released (self->binding), ==,
                     self->expect_released);
    self->input_calls++;
    return FALSE;
}

static void
input_template_fixed_update (LrgGameTemplate *template,
                             gdouble          delta)
{
    InputTemplate *self = (InputTemplate *)template;

    /* Every simulation step in one host frame sees the same snapshot. */
    g_assert_cmpint (lrg_input_binding_is_pressed (self->binding), ==,
                     self->expect_pressed);
    g_assert_cmpint (lrg_input_binding_is_released (self->binding), ==,
                     self->expect_released);
    self->fixed_calls++;
}

typedef LrgGameState TimerPauseState;
typedef LrgGameStateClass TimerPauseStateClass;

GType timer_pause_state_get_type (void);
G_DEFINE_TYPE (TimerPauseState, timer_pause_state, LRG_TYPE_GAME_STATE)

static void
timer_pause_state_noop (LrgGameState *state)
{
}

static void
timer_pause_state_update (LrgGameState *state,
                          gdouble       delta)
{
}

static void
timer_pause_state_exit (LrgGameState *state)
{
    LrgTimerManager *timers = g_object_get_data (G_OBJECT (state), "timers");

    /* Shutdown must also cancel work scheduled by state exit hooks. */
    lrg_timer_manager_add (timers, 10.0, FALSE, FALSE);
}

static void
timer_pause_state_class_init (TimerPauseStateClass *klass)
{
    klass->enter = timer_pause_state_noop;
    klass->exit = timer_pause_state_exit;
    klass->update = timer_pause_state_update;
    klass->draw = timer_pause_state_noop;
}

static void
timer_pause_state_init (TimerPauseState *self)
{
}

static LrgGameState *
input_template_create_pause (LrgGameTemplate *template)
{
    LrgGameState *state = g_object_new (timer_pause_state_get_type (), NULL);

    g_object_set_data (G_OBJECT (state), "timers",
                       lrg_game_template_get_timer_manager (template));
    return state;
}

static void
input_template_class_init (InputTemplateClass *klass)
{
    klass->handle_global_input = input_template_handle_input;
    klass->fixed_update = input_template_fixed_update;
    klass->create_initial_state = NULL;
    klass->create_pause_state = input_template_create_pause;
}

static void
input_template_init (InputTemplate *self)
{
}

static void
test_game_template_timer_lifetime (void)
{
    g_autoptr(LrgTimerManager) timers = NULL;
    LrgGameTemplate *template;

    template = g_object_new (input_template_get_type (), NULL);
    timers = g_object_ref (lrg_game_template_get_timer_manager (template));
    lrg_timer_manager_add (timers, 1.0, TRUE, FALSE);
    /* Even a retained manager must not retain work from a destroyed template. */
    g_object_unref (template);
    g_assert_cmpuint (lrg_timer_manager_get_count (timers), ==, 0);
}

static void
test_game_template_input_frame (void)
{
    LrgInputManager *manager;
    g_autoptr(LrgInputMock) mock = NULL;
    g_autoptr(LrgInputBinding) binding = NULL;
    g_autoptr(LrgGameTemplate) template = NULL;
    g_autoptr(LrgGameHost) host = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) saved_sources = NULL;
    GPtrArray *sources;
    InputTemplate *input_template;
    gboolean saved_enabled;
    LrgTimerManager *timers;
    guint64 scaled_timer;
    guint64 real_timer;
    guint i;

    manager = lrg_input_manager_get_default ();
    saved_enabled = lrg_input_manager_get_enabled (manager);
    saved_sources = g_ptr_array_new_with_free_func (g_object_unref);
    sources = lrg_input_manager_get_sources (manager);
    while (sources->len > 0)
    {
        LrgInput *source = g_ptr_array_index (sources, 0);

        g_ptr_array_add (saved_sources, g_object_ref (source));
        lrg_input_manager_remove_source (manager, source);
    }
    lrg_input_manager_set_enabled (manager, TRUE);
    mock = lrg_input_mock_new ();
    lrg_input_mock_set_gamepad_available (mock, 0, TRUE);
    lrg_input_manager_add_source (manager, LRG_INPUT (mock));
    lrg_input_manager_poll (manager);
    lrg_input_manager_poll (manager);

    binding = lrg_input_binding_new_gamepad_axis (0, GRL_GAMEPAD_AXIS_LEFT_X,
                                                 0.5f, TRUE);
    template = g_object_new (input_template_get_type (),
                             "use-fixed-timestep", TRUE,
                             "fixed-timestep", 0.01, NULL);
    input_template = (InputTemplate *)template;
    input_template->binding = binding;
    host = g_object_new (input_host_get_type (), NULL);
    g_assert_true (lrg_engine_startup (lrg_engine_get_default (), &error));
    g_assert_no_error (error);
    g_test_expect_message (LRG_LOG_DOMAIN_TEMPLATE, G_LOG_LEVEL_WARNING,
                           "*No initial state created*");
    g_assert_true (lrg_game_template_startup (template, host, &error));
    g_assert_no_error (error);
    g_test_assert_expected_messages ();
    timers = lrg_game_template_get_timer_manager (template);
    scaled_timer = lrg_timer_manager_add (timers, 1.0, FALSE, FALSE);
    real_timer = lrg_timer_manager_add (timers, 1.0, FALSE, TRUE);
    input_template->expect_pressed = TRUE;
    lrg_input_mock_set_gamepad_axis (mock, 0, GRL_GAMEPAD_AXIS_LEFT_X, 0.8f);
    lrg_game_template_update (template, 0.025);
    g_assert_cmpuint (input_template->input_calls, ==, 1);
    g_assert_cmpuint (input_template->fixed_calls, ==, 2);

    input_template->expect_pressed = FALSE;
    lrg_game_template_update (template, 0.0);
    g_assert_cmpuint (input_template->input_calls, ==, 2);
    g_assert_cmpuint (input_template->fixed_calls, ==, 2);

    input_template->expect_released = TRUE;
    lrg_input_mock_set_gamepad_axis (mock, 0, GRL_GAMEPAD_AXIS_LEFT_X, 0.0f);
    lrg_game_template_update (template, 0.025);
    input_template->expect_released = FALSE;
    lrg_game_template_update (template, 0.0);
    g_assert_cmpuint (input_template->input_calls, ==, 4);

    /* Two fixed steps must still advance timers by just one host delta. */
    g_assert_cmpfloat_with_epsilon (
        lrg_timer_manager_get_remaining (timers, scaled_timer), 0.95, 1e-9);
    lrg_game_template_set_time_scale (template, 0.5);
    lrg_game_template_update (template, 0.125);
    g_assert_cmpfloat_with_epsilon (
        lrg_timer_manager_get_remaining (timers, scaled_timer), 0.8875, 1e-9);
    g_assert_cmpfloat_with_epsilon (
        lrg_timer_manager_get_remaining (timers, real_timer), 0.825, 1e-9);
    lrg_game_template_set_time_scale (template, 0.0);
    lrg_game_template_update (template, 2.0);
    g_assert_cmpfloat_with_epsilon (
        lrg_timer_manager_get_remaining (timers, scaled_timer), 0.8875, 1e-9);
    g_assert_cmpfloat (lrg_timer_manager_get_remaining (timers, real_timer), ==, -1.0);
    lrg_game_template_set_time_scale (template, 1.0);
    lrg_game_template_pause (template);
    g_assert_true (lrg_game_template_is_paused (template));
    lrg_game_template_update (template, 0.125);
    g_assert_cmpfloat_with_epsilon (
        lrg_timer_manager_get_remaining (timers, scaled_timer), 0.8875, 1e-9);
    lrg_game_template_resume (template);
    lrg_game_template_update (template, 0.125);
    g_assert_cmpfloat_with_epsilon (
        lrg_timer_manager_get_remaining (timers, scaled_timer), 0.7625, 1e-9);
    lrg_game_template_hit_stop (template, 0.5);
    lrg_game_template_update (template, 0.125);
    g_assert_cmpfloat_with_epsilon (
        lrg_timer_manager_get_remaining (timers, scaled_timer), 0.7625, 1e-9);
    lrg_game_template_pause (template);
    lrg_game_template_shutdown_game (template);
    g_assert_cmpuint (lrg_timer_manager_get_count (timers), ==, 0);
    lrg_engine_shutdown (lrg_engine_get_default ());

    lrg_input_manager_remove_source (manager, LRG_INPUT (mock));
    for (i = 0; i < saved_sources->len; i++)
        lrg_input_manager_add_source (manager,
                                     g_ptr_array_index (saved_sources, i));
    lrg_input_manager_set_enabled (manager, saved_enabled);
}

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
    g_test_add_func ("/template/base/timer-lifetime", test_game_template_timer_lifetime);

    /* Construction tests */
    g_test_add_func ("/template/base/input-frame", test_game_template_input_frame);
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
