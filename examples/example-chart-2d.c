/* example-chart-2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interactive 2D Chart Gallery demonstrating the charting module.
 *
 * Features demonstrated:
 * - LrgBarChart2D: Grouped and stacked bar charts
 * - LrgLineChart2D: Line charts with markers and smooth curves
 * - LrgPieChart2D: Pie and donut charts
 * - LrgGaugeChart2D: Animated gauge/meter display
 * - LrgRadarChart2D: Spider/radar charts
 * - Interactivity: Hover effects and click handling
 * - Animation: Data transitions with easing
 *
 * Controls:
 *   1-5        - Switch chart type (Bar, Line, Pie, Gauge, Radar)
 *   Space      - Toggle mode (stacked, donut, etc.)
 *   R          - Randomize data
 *   Escape     - Quit
 */

/* =============================================================================
 * INCLUDES
 * ========================================================================== */

#include <libregnum.h>
#include <graylib.h>
#include <stdlib.h>
#include <time.h>

/* =============================================================================
 * CONSTANTS
 * ========================================================================== */

#define WINDOW_WIDTH    1024
#define WINDOW_HEIGHT   768
#define CHART_X         50
#define CHART_Y         80
#define CHART_WIDTH     700
#define CHART_HEIGHT    500
#define INFO_X          780
#define INFO_Y          80

#define TAB_BAR     0
#define TAB_LINE    1
#define TAB_PIE     2
#define TAB_GAUGE   3
#define TAB_RADAR   4
#define TAB_COUNT   5

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
    color_bg = grl_color_new (30, 35, 45, 255);
    color_text = grl_color_new (230, 235, 245, 255);
    color_dim = grl_color_new (130, 140, 160, 255);
    color_accent = grl_color_new (100, 180, 255, 255);
    color_panel = grl_color_new (45, 50, 65, 255);
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
 * CHART STATE
 * ========================================================================== */

typedef struct
{
    gint current_tab;
    gboolean mode_toggle;       /* stacked mode, donut mode, etc. */
    gfloat gauge_value;
    gfloat gauge_target;
    gchar status_text[256];

    /* Charts */
    LrgBarChart2D   *bar_chart;
    LrgLineChart2D  *line_chart;
    LrgPieChart2D   *pie_chart;
    LrgGaugeChart2D *gauge_chart;
    LrgRadarChart2D *radar_chart;
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
populate_bar_data (LrgBarChart2D *chart)
{
    const gchar *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun" };
    LrgChartDataSeries *series1;
    LrgChartDataSeries *series2;
    gint i;
    g_autoptr(GrlColor) c1 = NULL;
    g_autoptr(GrlColor) c2 = NULL;

    lrg_chart_clear_series (LRG_CHART (chart));

    c1 = grl_color_new (66, 133, 244, 255);
    c2 = grl_color_new (234, 67, 53, 255);

    series1 = lrg_chart_data_series_new ("Product A");
    lrg_chart_data_series_set_color (series1, c1);
    for (i = 0; i < 6; i++)
        lrg_chart_data_series_add_point_labeled (series1, (gdouble)i,
                                                  random_value (50, 200),
                                                  months[i]);

    series2 = lrg_chart_data_series_new ("Product B");
    lrg_chart_data_series_set_color (series2, c2);
    for (i = 0; i < 6; i++)
        lrg_chart_data_series_add_point_labeled (series2, (gdouble)i,
                                                  random_value (30, 150),
                                                  months[i]);

    lrg_chart_add_series (LRG_CHART (chart), series1);
    lrg_chart_add_series (LRG_CHART (chart), series2);

    lrg_chart_animate_to_data (LRG_CHART (chart), LRG_CHART_ANIM_GROW, 0.5f);
}

static void
populate_line_data (LrgLineChart2D *chart)
{
    LrgChartDataSeries *series1;
    LrgChartDataSeries *series2;
    gint i;
    g_autoptr(GrlColor) c1 = NULL;
    g_autoptr(GrlColor) c2 = NULL;

    lrg_chart_clear_series (LRG_CHART (chart));

    c1 = grl_color_new (52, 168, 83, 255);
    c2 = grl_color_new (251, 188, 5, 255);

    series1 = lrg_chart_data_series_new ("Temperature");
    lrg_chart_data_series_set_color (series1, c1);
    lrg_chart_data_series_set_line_width (series1, 2.5f);
    lrg_chart_data_series_set_marker (series1, LRG_CHART_MARKER_CIRCLE);
    for (i = 0; i < 12; i++)
        lrg_chart_data_series_add_point (series1, (gdouble)i,
                                          20.0 + random_value (-5, 15));

    series2 = lrg_chart_data_series_new ("Humidity");
    lrg_chart_data_series_set_color (series2, c2);
    lrg_chart_data_series_set_line_width (series2, 2.5f);
    lrg_chart_data_series_set_marker (series2, LRG_CHART_MARKER_SQUARE);
    for (i = 0; i < 12; i++)
        lrg_chart_data_series_add_point (series2, (gdouble)i,
                                          60.0 + random_value (-20, 20));

    lrg_chart_add_series (LRG_CHART (chart), series1);
    lrg_chart_add_series (LRG_CHART (chart), series2);

    lrg_chart_animate_to_data (LRG_CHART (chart), LRG_CHART_ANIM_GROW, 0.5f);
}

static void
populate_pie_data (LrgPieChart2D *chart)
{
    LrgChartDataSeries *series;
    g_autoptr(GrlColor) c1 = NULL;
    g_autoptr(GrlColor) c2 = NULL;
    g_autoptr(GrlColor) c3 = NULL;
    g_autoptr(GrlColor) c4 = NULL;
    g_autoptr(GrlColor) c5 = NULL;
    LrgChartDataPoint *pt;

    lrg_chart_clear_series (LRG_CHART (chart));

    c1 = grl_color_new (66, 133, 244, 255);
    c2 = grl_color_new (234, 67, 53, 255);
    c3 = grl_color_new (251, 188, 5, 255);
    c4 = grl_color_new (52, 168, 83, 255);
    c5 = grl_color_new (156, 39, 176, 255);

    series = lrg_chart_data_series_new ("Market Share");

    pt = lrg_chart_data_point_new_labeled (0.0, random_value (20, 40), "Chrome");
    lrg_chart_data_point_set_color (pt, c1);
    lrg_chart_data_series_add_point_full (series, pt);

    pt = lrg_chart_data_point_new_labeled (1.0, random_value (15, 30), "Firefox");
    lrg_chart_data_point_set_color (pt, c2);
    lrg_chart_data_series_add_point_full (series, pt);

    pt = lrg_chart_data_point_new_labeled (2.0, random_value (10, 25), "Safari");
    lrg_chart_data_point_set_color (pt, c3);
    lrg_chart_data_series_add_point_full (series, pt);

    pt = lrg_chart_data_point_new_labeled (3.0, random_value (5, 15), "Edge");
    lrg_chart_data_point_set_color (pt, c4);
    lrg_chart_data_series_add_point_full (series, pt);

    pt = lrg_chart_data_point_new_labeled (4.0, random_value (5, 15), "Other");
    lrg_chart_data_point_set_color (pt, c5);
    lrg_chart_data_series_add_point_full (series, pt);

    lrg_chart_add_series (LRG_CHART (chart), series);

    lrg_chart_animate_to_data (LRG_CHART (chart), LRG_CHART_ANIM_GROW, 0.5f);
}

static void
populate_radar_data (LrgRadarChart2D *chart)
{
    LrgChartDataSeries *series1;
    LrgChartDataSeries *series2;
    g_autoptr(GrlColor) c1 = NULL;
    g_autoptr(GrlColor) c2 = NULL;

    lrg_chart_clear_series (LRG_CHART (chart));

    c1 = grl_color_new (66, 133, 244, 200);
    c2 = grl_color_new (234, 67, 53, 200);

    series1 = lrg_chart_data_series_new ("Player 1");
    lrg_chart_data_series_set_color (series1, c1);
    lrg_chart_data_series_add_point (series1, 0.0, random_value (50, 100));
    lrg_chart_data_series_add_point (series1, 1.0, random_value (50, 100));
    lrg_chart_data_series_add_point (series1, 2.0, random_value (50, 100));
    lrg_chart_data_series_add_point (series1, 3.0, random_value (50, 100));
    lrg_chart_data_series_add_point (series1, 4.0, random_value (50, 100));
    lrg_chart_data_series_add_point (series1, 5.0, random_value (50, 100));

    series2 = lrg_chart_data_series_new ("Player 2");
    lrg_chart_data_series_set_color (series2, c2);
    lrg_chart_data_series_add_point (series2, 0.0, random_value (50, 100));
    lrg_chart_data_series_add_point (series2, 1.0, random_value (50, 100));
    lrg_chart_data_series_add_point (series2, 2.0, random_value (50, 100));
    lrg_chart_data_series_add_point (series2, 3.0, random_value (50, 100));
    lrg_chart_data_series_add_point (series2, 4.0, random_value (50, 100));
    lrg_chart_data_series_add_point (series2, 5.0, random_value (50, 100));

    lrg_chart_add_series (LRG_CHART (chart), series1);
    lrg_chart_add_series (LRG_CHART (chart), series2);

    lrg_chart_animate_to_data (LRG_CHART (chart), LRG_CHART_ANIM_GROW, 0.5f);
}

/* =============================================================================
 * CHART CREATION
 * ========================================================================== */

static void
create_charts (AppState *state)
{
    g_autoptr(GrlColor) chart_bg = grl_color_new (40, 45, 60, 255);
    g_autoptr(GrlColor) chart_text = grl_color_new (220, 225, 235, 255);

    /* Bar Chart */
    state->bar_chart = lrg_bar_chart2d_new ();
    lrg_widget_set_position (LRG_WIDGET (state->bar_chart), CHART_X, CHART_Y);
    lrg_widget_set_size (LRG_WIDGET (state->bar_chart), CHART_WIDTH, CHART_HEIGHT);
    lrg_chart_set_title (LRG_CHART (state->bar_chart), "Monthly Sales by Product");
    lrg_chart_set_background_color (LRG_CHART (state->bar_chart), chart_bg);
    lrg_chart_set_text_color (LRG_CHART (state->bar_chart), chart_text);
    lrg_chart_set_hover_enabled (LRG_CHART (state->bar_chart), TRUE);
    lrg_bar_chart2d_set_bar_mode (state->bar_chart, LRG_CHART_BAR_GROUPED);
    lrg_bar_chart2d_set_bar_width_ratio (state->bar_chart, 0.8f);
    populate_bar_data (state->bar_chart);

    /* Line Chart */
    state->line_chart = lrg_line_chart2d_new ();
    lrg_widget_set_position (LRG_WIDGET (state->line_chart), CHART_X, CHART_Y);
    lrg_widget_set_size (LRG_WIDGET (state->line_chart), CHART_WIDTH, CHART_HEIGHT);
    lrg_chart_set_title (LRG_CHART (state->line_chart), "Climate Data Over Time");
    lrg_chart_set_background_color (LRG_CHART (state->line_chart), chart_bg);
    lrg_chart_set_text_color (LRG_CHART (state->line_chart), chart_text);
    lrg_chart_set_hover_enabled (LRG_CHART (state->line_chart), TRUE);
    lrg_line_chart2d_set_smooth (state->line_chart, TRUE);
    lrg_line_chart2d_set_show_markers (state->line_chart, TRUE);
    populate_line_data (state->line_chart);

    /* Pie Chart */
    state->pie_chart = lrg_pie_chart2d_new ();
    lrg_widget_set_position (LRG_WIDGET (state->pie_chart), CHART_X, CHART_Y);
    lrg_widget_set_size (LRG_WIDGET (state->pie_chart), CHART_WIDTH, CHART_HEIGHT);
    lrg_chart_set_title (LRG_CHART (state->pie_chart), "Browser Market Share");
    lrg_chart_set_background_color (LRG_CHART (state->pie_chart), chart_bg);
    lrg_chart_set_text_color (LRG_CHART (state->pie_chart), chart_text);
    lrg_chart_set_hover_enabled (LRG_CHART (state->pie_chart), TRUE);
    lrg_pie_chart2d_set_start_angle (state->pie_chart, 90.0f);
    populate_pie_data (state->pie_chart);

    /* Gauge Chart */
    state->gauge_chart = lrg_gauge_chart2d_new ();
    lrg_widget_set_position (LRG_WIDGET (state->gauge_chart), CHART_X, CHART_Y);
    lrg_widget_set_size (LRG_WIDGET (state->gauge_chart), CHART_WIDTH, CHART_HEIGHT);
    lrg_chart_set_title (LRG_CHART (state->gauge_chart), "System Performance");
    lrg_chart_set_background_color (LRG_CHART (state->gauge_chart), chart_bg);
    lrg_chart_set_text_color (LRG_CHART (state->gauge_chart), chart_text);
    lrg_gauge_chart2d_set_min_value (state->gauge_chart, 0.0);
    lrg_gauge_chart2d_set_max_value (state->gauge_chart, 100.0);
    lrg_gauge_chart2d_set_value (state->gauge_chart, 50.0);
    state->gauge_value = 50.0f;
    state->gauge_target = 75.0f;

    /* Radar Chart */
    state->radar_chart = lrg_radar_chart2d_new ();
    lrg_widget_set_position (LRG_WIDGET (state->radar_chart), CHART_X, CHART_Y);
    lrg_widget_set_size (LRG_WIDGET (state->radar_chart), CHART_WIDTH, CHART_HEIGHT);
    lrg_chart_set_title (LRG_CHART (state->radar_chart), "Player Stats Comparison");
    lrg_chart_set_background_color (LRG_CHART (state->radar_chart), chart_bg);
    lrg_chart_set_text_color (LRG_CHART (state->radar_chart), chart_text);
    lrg_chart_set_hover_enabled (LRG_CHART (state->radar_chart), TRUE);
    lrg_radar_chart2d_set_fill_opacity (state->radar_chart, 0.3f);
    populate_radar_data (state->radar_chart);
}

static void
cleanup_charts (AppState *state)
{
    g_clear_object (&state->bar_chart);
    g_clear_object (&state->line_chart);
    g_clear_object (&state->pie_chart);
    g_clear_object (&state->gauge_chart);
    g_clear_object (&state->radar_chart);
}

/* =============================================================================
 * INPUT HANDLING
 * ========================================================================== */

static void
handle_input (AppState *state)
{
    /* Tab selection */
    if (grl_input_is_key_pressed (GRL_KEY_ONE))
        state->current_tab = TAB_BAR;
    else if (grl_input_is_key_pressed (GRL_KEY_TWO))
        state->current_tab = TAB_LINE;
    else if (grl_input_is_key_pressed (GRL_KEY_THREE))
        state->current_tab = TAB_PIE;
    else if (grl_input_is_key_pressed (GRL_KEY_FOUR))
        state->current_tab = TAB_GAUGE;
    else if (grl_input_is_key_pressed (GRL_KEY_FIVE))
        state->current_tab = TAB_RADAR;

    /* Mode toggle */
    if (grl_input_is_key_pressed (GRL_KEY_SPACE))
    {
        state->mode_toggle = !state->mode_toggle;

        switch (state->current_tab)
        {
        case TAB_BAR:
            lrg_bar_chart2d_set_bar_mode (state->bar_chart,
                state->mode_toggle ? LRG_CHART_BAR_STACKED : LRG_CHART_BAR_GROUPED);
            break;
        case TAB_LINE:
            lrg_line_chart2d_set_smooth (state->line_chart, !state->mode_toggle);
            break;
        case TAB_PIE:
            lrg_pie_chart2d_set_inner_radius (state->pie_chart,
                state->mode_toggle ? 0.5f : 0.0f);
            break;
        case TAB_RADAR:
            lrg_radar_chart2d_set_fill_opacity (state->radar_chart,
                state->mode_toggle ? 0.0f : 0.3f);
            break;
        default:
            break;
        }
    }

    /* Randomize data */
    if (grl_input_is_key_pressed (GRL_KEY_R))
    {
        switch (state->current_tab)
        {
        case TAB_BAR:
            populate_bar_data (state->bar_chart);
            break;
        case TAB_LINE:
            populate_line_data (state->line_chart);
            break;
        case TAB_PIE:
            populate_pie_data (state->pie_chart);
            break;
        case TAB_GAUGE:
            state->gauge_target = random_value (10, 90);
            break;
        case TAB_RADAR:
            populate_radar_data (state->radar_chart);
            break;
        }
    }
}

/* =============================================================================
 * UPDATE
 * ========================================================================== */

static void
update_state (AppState *state, gfloat delta)
{
    /* Animate gauge value */
    if (state->current_tab == TAB_GAUGE)
    {
        gfloat diff;

        diff = state->gauge_target - state->gauge_value;
        if (fabsf (diff) > 0.1f)
        {
            state->gauge_value += diff * delta * 3.0f;
            lrg_gauge_chart2d_set_value (state->gauge_chart, state->gauge_value);
        }
    }

    /* Update status based on current chart's hover */
    state->status_text[0] = '\0';

    switch (state->current_tab)
    {
    case TAB_BAR:
        {
            const LrgChartHitInfo *hit = lrg_chart_get_current_hover (
                LRG_CHART (state->bar_chart));
            if (hit != NULL)
            {
                const LrgChartDataPoint *pt = lrg_chart_hit_info_get_data_point (hit);
                g_snprintf (state->status_text, sizeof (state->status_text),
                            "Hovering: %s (%.1f)",
                            lrg_chart_data_point_get_label (pt) ?: "?",
                            lrg_chart_data_point_get_y (pt));
            }
        }
        break;
    case TAB_LINE:
        {
            const LrgChartHitInfo *hit = lrg_chart_get_current_hover (
                LRG_CHART (state->line_chart));
            if (hit != NULL)
            {
                const LrgChartDataPoint *pt = lrg_chart_hit_info_get_data_point (hit);
                g_snprintf (state->status_text, sizeof (state->status_text),
                            "Point: (%.1f, %.1f)",
                            lrg_chart_data_point_get_x (pt),
                            lrg_chart_data_point_get_y (pt));
            }
        }
        break;
    case TAB_PIE:
        {
            const LrgChartHitInfo *hit = lrg_chart_get_current_hover (
                LRG_CHART (state->pie_chart));
            if (hit != NULL)
            {
                const LrgChartDataPoint *pt = lrg_chart_hit_info_get_data_point (hit);
                g_snprintf (state->status_text, sizeof (state->status_text),
                            "Slice: %s (%.1f%%)",
                            lrg_chart_data_point_get_label (pt) ?: "?",
                            lrg_chart_data_point_get_y (pt));
            }
        }
        break;
    case TAB_GAUGE:
        g_snprintf (state->status_text, sizeof (state->status_text),
                    "Value: %.1f / Target: %.1f",
                    state->gauge_value, state->gauge_target);
        break;
    case TAB_RADAR:
        {
            const LrgChartHitInfo *hit = lrg_chart_get_current_hover (
                LRG_CHART (state->radar_chart));
            if (hit != NULL)
            {
                g_snprintf (state->status_text, sizeof (state->status_text),
                            "Series %d, Axis %d",
                            lrg_chart_hit_info_get_series_index (hit),
                            lrg_chart_hit_info_get_point_index (hit));
            }
        }
        break;
    }
}

/* =============================================================================
 * DRAWING
 * ========================================================================== */

static void
draw_tabs (AppState *state)
{
    const gchar *tab_names[] = { "1: Bar", "2: Line", "3: Pie", "4: Gauge", "5: Radar" };
    gint x;
    gint i;

    x = 50;
    for (i = 0; i < TAB_COUNT; i++)
    {
        GrlColor *tab_color;

        if (i == state->current_tab)
            tab_color = color_accent;
        else
            tab_color = color_dim;

        grl_draw_text (tab_names[i], x, 30, 20, tab_color);
        x += 120;
    }
}

static void
draw_info_panel (AppState *state)
{
    const gchar *mode_text;

    grl_draw_rectangle (INFO_X - 10, INFO_Y - 10, 220, 200, color_panel);

    grl_draw_text ("Controls:", INFO_X, INFO_Y, 18, color_accent);
    grl_draw_text ("1-5: Switch chart", INFO_X, INFO_Y + 30, 14, color_dim);
    grl_draw_text ("Space: Toggle mode", INFO_X, INFO_Y + 50, 14, color_dim);
    grl_draw_text ("R: Randomize data", INFO_X, INFO_Y + 70, 14, color_dim);
    grl_draw_text ("Esc: Quit", INFO_X, INFO_Y + 90, 14, color_dim);

    /* Mode indicator */
    mode_text = "";
    switch (state->current_tab)
    {
    case TAB_BAR:
        mode_text = state->mode_toggle ? "Mode: Stacked" : "Mode: Grouped";
        break;
    case TAB_LINE:
        mode_text = state->mode_toggle ? "Mode: Angular" : "Mode: Smooth";
        break;
    case TAB_PIE:
        mode_text = state->mode_toggle ? "Mode: Donut" : "Mode: Pie";
        break;
    case TAB_RADAR:
        mode_text = state->mode_toggle ? "Mode: Lines" : "Mode: Filled";
        break;
    default:
        break;
    }
    grl_draw_text (mode_text, INFO_X, INFO_Y + 130, 16, color_text);

    /* Status */
    if (state->status_text[0] != '\0')
        grl_draw_text (state->status_text, INFO_X, INFO_Y + 160, 14, color_accent);
}

static void
draw_current_chart (AppState *state)
{
    switch (state->current_tab)
    {
    case TAB_BAR:
        lrg_widget_draw (LRG_WIDGET (state->bar_chart));
        break;
    case TAB_LINE:
        lrg_widget_draw (LRG_WIDGET (state->line_chart));
        break;
    case TAB_PIE:
        lrg_widget_draw (LRG_WIDGET (state->pie_chart));
        break;
    case TAB_GAUGE:
        lrg_widget_draw (LRG_WIDGET (state->gauge_chart));
        break;
    case TAB_RADAR:
        lrg_widget_draw (LRG_WIDGET (state->radar_chart));
        break;
    }
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

    window = grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT,
                             "Libregnum Chart Gallery - 2D Charts");
    grl_window_set_target_fps (window, 60);

    init_colors ();
    create_charts (&state);

    while (!grl_window_should_close (window))
    {
        gfloat delta;

        delta = grl_window_get_frame_time (window);

        if (grl_input_is_key_pressed (GRL_KEY_ESCAPE))
            break;

        handle_input (&state);
        update_state (&state, delta);

        grl_window_begin_drawing (window);
        grl_draw_clear_background (color_bg);

        draw_tabs (&state);
        draw_current_chart (&state);
        draw_info_panel (&state);

        grl_draw_fps (WINDOW_WIDTH - 80, 10);
        grl_window_end_drawing (window);
    }

    cleanup_charts (&state);
    cleanup_colors ();

    return 0;
}
