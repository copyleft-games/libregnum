/* example-chart-3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D Chart demonstration showing LrgBarChart3D with interactive camera.
 *
 * Features demonstrated:
 * - LrgBarChart3D: 3D bar chart with depth perspective
 * - Camera rotation and zoom controls
 * - Multiple data series
 * - Animation transitions
 *
 * Controls:
 *   Arrow keys   - Rotate camera
 *   +/-          - Zoom in/out
 *   R            - Randomize data
 *   Space        - Toggle auto-rotate
 *   Escape       - Quit
 */

/* =============================================================================
 * INCLUDES
 * ========================================================================== */

#include <libregnum.h>
#include <graylib.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

/* =============================================================================
 * CONSTANTS
 * ========================================================================== */

#define WINDOW_WIDTH    1024
#define WINDOW_HEIGHT   768
#define CHART_X         100
#define CHART_Y         80
#define CHART_WIDTH     600
#define CHART_HEIGHT    550

#define ROTATE_SPEED    60.0f
#define ZOOM_SPEED      100.0f
#define AUTO_ROTATE_SPEED 15.0f

/* =============================================================================
 * COLORS
 * ========================================================================== */

static GrlColor *color_bg = NULL;
static GrlColor *color_text = NULL;
static GrlColor *color_dim = NULL;
static GrlColor *color_accent = NULL;
static GrlColor *color_panel = NULL;

static void
init_colors (void)
{
    color_bg = grl_color_new (20, 25, 35, 255);
    color_text = grl_color_new (230, 235, 245, 255);
    color_dim = grl_color_new (130, 140, 160, 255);
    color_accent = grl_color_new (100, 200, 255, 255);
    color_panel = grl_color_new (35, 40, 55, 255);
}

static void
cleanup_colors (void)
{
    g_clear_pointer (&color_bg, grl_color_free);
    g_clear_pointer (&color_text, grl_color_free);
    g_clear_pointer (&color_dim, grl_color_free);
    g_clear_pointer (&color_accent, grl_color_free);
    g_clear_pointer (&color_panel, grl_color_free);
}

/* =============================================================================
 * APP STATE
 * ========================================================================== */

typedef struct
{
    LrgBarChart3D *chart;
    gfloat camera_pitch;
    gfloat camera_yaw;
    gfloat camera_distance;
    gboolean auto_rotate;
} AppState;

/* =============================================================================
 * DATA GENERATION
 * ========================================================================== */

static gdouble
random_value (gdouble min, gdouble max)
{
    return min + (gdouble)rand () / RAND_MAX * (max - min);
}

static void
populate_chart_data (LrgBarChart3D *chart)
{
    const gchar *quarters[] = { "Q1", "Q2", "Q3", "Q4" };
    LrgChartDataSeries *series1;
    LrgChartDataSeries *series2;
    LrgChartDataSeries *series3;
    gint i;
    g_autoptr(GrlColor) c1 = NULL;
    g_autoptr(GrlColor) c2 = NULL;
    g_autoptr(GrlColor) c3 = NULL;

    lrg_chart_clear_series (LRG_CHART (chart));

    c1 = grl_color_new (66, 133, 244, 255);
    c2 = grl_color_new (234, 67, 53, 255);
    c3 = grl_color_new (52, 168, 83, 255);

    /* Revenue series */
    series1 = lrg_chart_data_series_new ("Revenue");
    lrg_chart_data_series_set_color (series1, c1);
    for (i = 0; i < 4; i++)
        lrg_chart_data_series_add_point_labeled (series1, (gdouble)i,
                                                  random_value (100, 300),
                                                  quarters[i]);

    /* Expenses series */
    series2 = lrg_chart_data_series_new ("Expenses");
    lrg_chart_data_series_set_color (series2, c2);
    for (i = 0; i < 4; i++)
        lrg_chart_data_series_add_point_labeled (series2, (gdouble)i,
                                                  random_value (50, 200),
                                                  quarters[i]);

    /* Profit series */
    series3 = lrg_chart_data_series_new ("Profit");
    lrg_chart_data_series_set_color (series3, c3);
    for (i = 0; i < 4; i++)
        lrg_chart_data_series_add_point_labeled (series3, (gdouble)i,
                                                  random_value (30, 150),
                                                  quarters[i]);

    lrg_chart_add_series (LRG_CHART (chart), series1);
    lrg_chart_add_series (LRG_CHART (chart), series2);
    lrg_chart_add_series (LRG_CHART (chart), series3);

    lrg_chart_animate_to_data (LRG_CHART (chart), LRG_CHART_ANIM_GROW, 0.6f);
}

/* =============================================================================
 * CHART CREATION
 * ========================================================================== */

static void
create_chart (AppState *state)
{
    g_autoptr(GrlColor) chart_bg = grl_color_new (30, 35, 50, 255);
    g_autoptr(GrlColor) chart_text = grl_color_new (220, 225, 240, 255);

    state->chart = lrg_bar_chart3d_new ();
    lrg_widget_set_position (LRG_WIDGET (state->chart), CHART_X, CHART_Y);
    lrg_widget_set_size (LRG_WIDGET (state->chart), CHART_WIDTH, CHART_HEIGHT);
    lrg_chart_set_title (LRG_CHART (state->chart), "Quarterly Financial Summary");
    lrg_chart_set_background_color (LRG_CHART (state->chart), chart_bg);
    lrg_chart_set_text_color (LRG_CHART (state->chart), chart_text);

    /* 3D-specific settings */
    lrg_chart3d_set_camera_distance (LRG_CHART3D (state->chart), state->camera_distance);
    lrg_chart3d_set_camera_pitch (LRG_CHART3D (state->chart), state->camera_pitch);
    lrg_chart3d_set_camera_yaw (LRG_CHART3D (state->chart), state->camera_yaw);
    lrg_bar_chart3d_set_bar_depth (state->chart, 0.6f);

    populate_chart_data (state->chart);
}

/* =============================================================================
 * INPUT HANDLING
 * ========================================================================== */

static void
handle_input (AppState *state, gfloat delta)
{
    /* Camera rotation */
    if (grl_input_is_key_down (GRL_KEY_LEFT))
        state->camera_yaw -= ROTATE_SPEED * delta;
    if (grl_input_is_key_down (GRL_KEY_RIGHT))
        state->camera_yaw += ROTATE_SPEED * delta;
    if (grl_input_is_key_down (GRL_KEY_UP))
        state->camera_pitch -= ROTATE_SPEED * delta;
    if (grl_input_is_key_down (GRL_KEY_DOWN))
        state->camera_pitch += ROTATE_SPEED * delta;

    /* Clamp pitch to avoid flipping */
    if (state->camera_pitch < 10.0f)
        state->camera_pitch = 10.0f;
    if (state->camera_pitch > 80.0f)
        state->camera_pitch = 80.0f;

    /* Zoom */
    if (grl_input_is_key_down (GRL_KEY_EQUAL) || grl_input_is_key_down (GRL_KEY_KP_ADD))
        state->camera_distance -= ZOOM_SPEED * delta;
    if (grl_input_is_key_down (GRL_KEY_MINUS) || grl_input_is_key_down (GRL_KEY_KP_SUBTRACT))
        state->camera_distance += ZOOM_SPEED * delta;

    /* Clamp zoom */
    if (state->camera_distance < 200.0f)
        state->camera_distance = 200.0f;
    if (state->camera_distance > 800.0f)
        state->camera_distance = 800.0f;

    /* Toggle auto-rotate */
    if (grl_input_is_key_pressed (GRL_KEY_SPACE))
        state->auto_rotate = !state->auto_rotate;

    /* Randomize data */
    if (grl_input_is_key_pressed (GRL_KEY_R))
        populate_chart_data (state->chart);
}

/* =============================================================================
 * UPDATE
 * ========================================================================== */

static void
update_state (AppState *state, gfloat delta)
{
    /* Auto-rotate */
    if (state->auto_rotate)
        state->camera_yaw += AUTO_ROTATE_SPEED * delta;

    /* Keep yaw in [0, 360) range */
    while (state->camera_yaw >= 360.0f)
        state->camera_yaw -= 360.0f;
    while (state->camera_yaw < 0.0f)
        state->camera_yaw += 360.0f;

    /* Update chart camera */
    lrg_chart3d_set_camera_distance (LRG_CHART3D (state->chart), state->camera_distance);
    lrg_chart3d_set_camera_pitch (LRG_CHART3D (state->chart), state->camera_pitch);
    lrg_chart3d_set_camera_yaw (LRG_CHART3D (state->chart), state->camera_yaw);
}

/* =============================================================================
 * DRAWING
 * ========================================================================== */

static void
draw_info_panel (AppState *state)
{
    gchar buf[64];
    gint panel_x;
    gint panel_y;

    panel_x = 730;
    panel_y = 80;

    grl_draw_rectangle (panel_x - 10, panel_y - 10, 260, 280, color_panel);

    grl_draw_text ("Controls:", panel_x, panel_y, 18, color_accent);
    grl_draw_text ("Arrows: Rotate view", panel_x, panel_y + 30, 14, color_dim);
    grl_draw_text ("+/-: Zoom in/out", panel_x, panel_y + 50, 14, color_dim);
    grl_draw_text ("Space: Auto-rotate", panel_x, panel_y + 70, 14, color_dim);
    grl_draw_text ("R: Randomize data", panel_x, panel_y + 90, 14, color_dim);
    grl_draw_text ("Esc: Quit", panel_x, panel_y + 110, 14, color_dim);

    grl_draw_text ("Camera:", panel_x, panel_y + 150, 18, color_accent);

    g_snprintf (buf, sizeof (buf), "Pitch: %.1f", state->camera_pitch);
    grl_draw_text (buf, panel_x, panel_y + 175, 14, color_text);

    g_snprintf (buf, sizeof (buf), "Yaw: %.1f", state->camera_yaw);
    grl_draw_text (buf, panel_x, panel_y + 195, 14, color_text);

    g_snprintf (buf, sizeof (buf), "Distance: %.0f", state->camera_distance);
    grl_draw_text (buf, panel_x, panel_y + 215, 14, color_text);

    g_snprintf (buf, sizeof (buf), "Auto-rotate: %s",
                state->auto_rotate ? "ON" : "OFF");
    grl_draw_text (buf, panel_x, panel_y + 245, 14,
                   state->auto_rotate ? color_accent : color_dim);
}

/* =============================================================================
 * MAIN
 * ========================================================================== */

int
main (int argc, char *argv[])
{
    g_autoptr(GrlWindow) window = NULL;
    AppState state = {0};

    (void)argc;
    (void)argv;

    srand ((unsigned int)time (NULL));

    /* Initialize state */
    state.camera_pitch = 30.0f;
    state.camera_yaw = 45.0f;
    state.camera_distance = 500.0f;
    state.auto_rotate = FALSE;

    window = grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT,
                             "Libregnum Chart Gallery - 3D Bar Chart");
    grl_window_set_target_fps (window, 60);

    init_colors ();
    create_chart (&state);

    while (!grl_window_should_close (window))
    {
        gfloat delta;

        delta = grl_window_get_frame_time (window);

        if (grl_input_is_key_pressed (GRL_KEY_ESCAPE))
            break;

        handle_input (&state, delta);
        update_state (&state, delta);

        grl_window_begin_drawing (window);
        grl_draw_clear_background (color_bg);

        grl_draw_text ("3D Bar Chart Demo", 100, 30, 24, color_accent);

        lrg_widget_draw (LRG_WIDGET (state.chart));
        draw_info_panel (&state);

        grl_draw_fps (WINDOW_WIDTH - 80, 10);
        grl_window_end_drawing (window);
    }

    g_clear_object (&state.chart);
    cleanup_colors ();

    return 0;
}
