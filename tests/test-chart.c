/* test-chart.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the Chart module.
 */

#include <glib.h>
#include <math.h>
#include <libregnum.h>

/* ==========================================================================
 * GBoxed Type Tests - LrgChartDataPoint
 * ========================================================================== */

static void
test_data_point_new (void)
{
    g_autoptr(LrgChartDataPoint) point = NULL;

    point = lrg_chart_data_point_new (1.0, 2.0);

    g_assert_nonnull (point);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_x (point), 1.0, 0.0001);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_y (point), 2.0, 0.0001);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_z (point), 0.0, 0.0001);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_w (point), 0.0, 0.0001);
    g_assert_null (lrg_chart_data_point_get_label (point));
    g_assert_null (lrg_chart_data_point_get_color (point));
}

static void
test_data_point_new_with_z (void)
{
    g_autoptr(LrgChartDataPoint) point = NULL;

    point = lrg_chart_data_point_new_with_z (1.0, 2.0, 3.0);

    g_assert_nonnull (point);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_x (point), 1.0, 0.0001);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_y (point), 2.0, 0.0001);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_z (point), 3.0, 0.0001);
}

static void
test_data_point_new_full (void)
{
    g_autoptr(LrgChartDataPoint) point = NULL;

    point = lrg_chart_data_point_new_full (1.0, 2.0, 3.0, 4.0, NULL, NULL);

    g_assert_nonnull (point);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_x (point), 1.0, 0.0001);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_y (point), 2.0, 0.0001);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_z (point), 3.0, 0.0001);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_w (point), 4.0, 0.0001);
}

static void
test_data_point_new_labeled (void)
{
    g_autoptr(LrgChartDataPoint) point = NULL;

    point = lrg_chart_data_point_new_labeled (1.0, 2.0, "Test Label");

    g_assert_nonnull (point);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_x (point), 1.0, 0.0001);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_y (point), 2.0, 0.0001);
    g_assert_nonnull (lrg_chart_data_point_get_label (point));
    g_assert_cmpstr (lrg_chart_data_point_get_label (point), ==, "Test Label");
}

static void
test_data_point_copy (void)
{
    g_autoptr(LrgChartDataPoint) original = NULL;
    g_autoptr(LrgChartDataPoint) copy = NULL;
    g_autoptr(GrlColor) color = NULL;

    color = grl_color_new (255, 0, 0, 255);
    original = lrg_chart_data_point_new_labeled (1.0, 2.0, "Original");
    lrg_chart_data_point_set_color (original, color);

    copy = lrg_chart_data_point_copy (original);

    g_assert_nonnull (copy);
    g_assert_true (copy != original);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_x (copy), 1.0, 0.0001);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_y (copy), 2.0, 0.0001);
    g_assert_cmpstr (lrg_chart_data_point_get_label (copy), ==, "Original");
    g_assert_nonnull (lrg_chart_data_point_get_color (copy));
    g_assert_cmpuint (grl_color_get_r (lrg_chart_data_point_get_color (copy)), ==, 255);
}

/* ==========================================================================
 * GBoxed Type Tests - LrgChartAxisConfig
 * ========================================================================== */

static void
test_axis_config_new (void)
{
    g_autoptr(LrgChartAxisConfig) config = NULL;

    config = lrg_chart_axis_config_new ();

    g_assert_nonnull (config);
    g_assert_null (lrg_chart_axis_config_get_title (config));
    g_assert_true (isnan (lrg_chart_axis_config_get_min (config)));
    g_assert_true (isnan (lrg_chart_axis_config_get_max (config)));
    g_assert_true (lrg_chart_axis_config_get_show_grid (config));
    g_assert_false (lrg_chart_axis_config_get_logarithmic (config));
}

static void
test_axis_config_new_with_title (void)
{
    g_autoptr(LrgChartAxisConfig) config = NULL;

    config = lrg_chart_axis_config_new_with_title ("X Axis");

    g_assert_nonnull (config);
    g_assert_nonnull (lrg_chart_axis_config_get_title (config));
    g_assert_cmpstr (lrg_chart_axis_config_get_title (config), ==, "X Axis");
}

static void
test_axis_config_new_with_range (void)
{
    g_autoptr(LrgChartAxisConfig) config = NULL;

    config = lrg_chart_axis_config_new_with_range (NULL, 0.0, 100.0);

    g_assert_nonnull (config);
    g_assert_cmpfloat_with_epsilon (lrg_chart_axis_config_get_min (config), 0.0, 0.0001);
    g_assert_cmpfloat_with_epsilon (lrg_chart_axis_config_get_max (config), 100.0, 0.0001);
}

static void
test_axis_config_copy (void)
{
    g_autoptr(LrgChartAxisConfig) original = NULL;
    g_autoptr(LrgChartAxisConfig) copy = NULL;

    original = lrg_chart_axis_config_new_with_title ("Test");
    lrg_chart_axis_config_set_min (original, 10.0);
    lrg_chart_axis_config_set_max (original, 90.0);
    lrg_chart_axis_config_set_show_grid (original, FALSE);
    lrg_chart_axis_config_set_logarithmic (original, TRUE);

    copy = lrg_chart_axis_config_copy (original);

    g_assert_nonnull (copy);
    g_assert_true (copy != original);
    g_assert_cmpstr (lrg_chart_axis_config_get_title (copy), ==, "Test");
    g_assert_cmpfloat_with_epsilon (lrg_chart_axis_config_get_min (copy), 10.0, 0.0001);
    g_assert_cmpfloat_with_epsilon (lrg_chart_axis_config_get_max (copy), 90.0, 0.0001);
    g_assert_false (lrg_chart_axis_config_get_show_grid (copy));
    g_assert_true (lrg_chart_axis_config_get_logarithmic (copy));
}

/* ==========================================================================
 * GBoxed Type Tests - LrgChartHitInfo
 * ========================================================================== */

static void
test_hit_info_new (void)
{
    g_autoptr(LrgChartHitInfo) hit = NULL;

    hit = lrg_chart_hit_info_new ();

    g_assert_nonnull (hit);
    g_assert_cmpint (lrg_chart_hit_info_get_series_index (hit), ==, -1);
    g_assert_cmpint (lrg_chart_hit_info_get_point_index (hit), ==, -1);
    g_assert_null (lrg_chart_hit_info_get_data_point (hit));
}

static void
test_hit_info_copy (void)
{
    g_autoptr(LrgChartHitInfo) original = NULL;
    g_autoptr(LrgChartHitInfo) copy = NULL;
    g_autoptr(LrgChartDataPoint) point = NULL;

    original = lrg_chart_hit_info_new ();
    lrg_chart_hit_info_set_series_index (original, 1);
    lrg_chart_hit_info_set_point_index (original, 5);
    lrg_chart_hit_info_set_screen_x (original, 100.0f);
    lrg_chart_hit_info_set_screen_y (original, 200.0f);

    point = lrg_chart_data_point_new (10.0, 20.0);
    lrg_chart_hit_info_set_data_point (original, point);

    copy = lrg_chart_hit_info_copy (original);

    g_assert_nonnull (copy);
    g_assert_cmpint (lrg_chart_hit_info_get_series_index (copy), ==, 1);
    g_assert_cmpint (lrg_chart_hit_info_get_point_index (copy), ==, 5);
    g_assert_cmpfloat_with_epsilon (lrg_chart_hit_info_get_screen_x (copy), 100.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_chart_hit_info_get_screen_y (copy), 200.0f, 0.0001f);
    g_assert_nonnull (lrg_chart_hit_info_get_data_point (copy));
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_x (lrg_chart_hit_info_get_data_point (copy)), 10.0, 0.0001);
}

/* ==========================================================================
 * LrgChartDataSeries Tests
 * ========================================================================== */

static void
test_data_series_new (void)
{
    g_autoptr(LrgChartDataSeries) series = NULL;

    series = lrg_chart_data_series_new ("Test Series");

    g_assert_nonnull (series);
    g_assert_true (LRG_IS_CHART_DATA_SERIES (series));
    g_assert_cmpstr (lrg_chart_data_series_get_name (series), ==, "Test Series");
    g_assert_cmpuint (lrg_chart_data_series_get_point_count (series), ==, 0);
    g_assert_true (lrg_chart_data_series_get_visible (series));
}

static void
test_data_series_add_point (void)
{
    g_autoptr(LrgChartDataSeries) series = NULL;

    series = lrg_chart_data_series_new ("Test");

    lrg_chart_data_series_add_point (series, 1.0, 10.0);
    lrg_chart_data_series_add_point (series, 2.0, 20.0);
    lrg_chart_data_series_add_point (series, 3.0, 30.0);

    g_assert_cmpuint (lrg_chart_data_series_get_point_count (series), ==, 3);
}

static void
test_data_series_add_point_labeled (void)
{
    g_autoptr(LrgChartDataSeries) series = NULL;
    const LrgChartDataPoint *point;

    series = lrg_chart_data_series_new ("Test");

    lrg_chart_data_series_add_point_labeled (series, 0.0, 100.0, "First");
    lrg_chart_data_series_add_point_labeled (series, 1.0, 200.0, "Second");

    g_assert_cmpuint (lrg_chart_data_series_get_point_count (series), ==, 2);

    point = lrg_chart_data_series_get_point (series, 0);
    g_assert_cmpstr (lrg_chart_data_point_get_label (point), ==, "First");
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_y (point), 100.0, 0.0001);

    point = lrg_chart_data_series_get_point (series, 1);
    g_assert_cmpstr (lrg_chart_data_point_get_label (point), ==, "Second");
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_y (point), 200.0, 0.0001);
}

static void
test_data_series_add_point_full (void)
{
    g_autoptr(LrgChartDataSeries) series = NULL;
    LrgChartDataPoint *point_to_add;
    const LrgChartDataPoint *point;

    series = lrg_chart_data_series_new ("3D Data");

    /* Create a 3D point and add it (series takes ownership) */
    point_to_add = lrg_chart_data_point_new_with_z (1.0, 2.0, 3.0);
    lrg_chart_data_series_add_point_full (series, point_to_add);

    g_assert_cmpuint (lrg_chart_data_series_get_point_count (series), ==, 1);

    point = lrg_chart_data_series_get_point (series, 0);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_x (point), 1.0, 0.0001);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_y (point), 2.0, 0.0001);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_z (point), 3.0, 0.0001);
}

static void
test_data_series_get_point (void)
{
    g_autoptr(LrgChartDataSeries) series = NULL;
    const LrgChartDataPoint *point;

    series = lrg_chart_data_series_new ("Test");

    lrg_chart_data_series_add_point (series, 5.0, 50.0);
    lrg_chart_data_series_add_point (series, 10.0, 100.0);

    point = lrg_chart_data_series_get_point (series, 0);
    g_assert_nonnull (point);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_x (point), 5.0, 0.0001);

    point = lrg_chart_data_series_get_point (series, 1);
    g_assert_nonnull (point);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_x (point), 10.0, 0.0001);

    /* Note: Out of bounds triggers g_return_val_if_fail CRITICAL,
     * which is fatal in GTest. Use g_assert_cmpuint to check bounds instead. */
    g_assert_cmpuint (lrg_chart_data_series_get_point_count (series), ==, 2);
}

static void
test_data_series_remove_point (void)
{
    g_autoptr(LrgChartDataSeries) series = NULL;
    const LrgChartDataPoint *point;

    series = lrg_chart_data_series_new ("Test");

    lrg_chart_data_series_add_point (series, 1.0, 10.0);
    lrg_chart_data_series_add_point (series, 2.0, 20.0);
    lrg_chart_data_series_add_point (series, 3.0, 30.0);

    g_assert_cmpuint (lrg_chart_data_series_get_point_count (series), ==, 3);

    lrg_chart_data_series_remove_point (series, 1);
    g_assert_cmpuint (lrg_chart_data_series_get_point_count (series), ==, 2);

    /* Verify remaining points */
    point = lrg_chart_data_series_get_point (series, 0);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_x (point), 1.0, 0.0001);

    point = lrg_chart_data_series_get_point (series, 1);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_point_get_x (point), 3.0, 0.0001);
}

static void
test_data_series_clear (void)
{
    g_autoptr(LrgChartDataSeries) series = NULL;

    series = lrg_chart_data_series_new ("Test");

    lrg_chart_data_series_add_point (series, 1.0, 10.0);
    lrg_chart_data_series_add_point (series, 2.0, 20.0);

    g_assert_cmpuint (lrg_chart_data_series_get_point_count (series), ==, 2);

    lrg_chart_data_series_clear (series);
    g_assert_cmpuint (lrg_chart_data_series_get_point_count (series), ==, 0);
}

static void
test_data_series_visibility (void)
{
    g_autoptr(LrgChartDataSeries) series = NULL;

    series = lrg_chart_data_series_new ("Test");

    g_assert_true (lrg_chart_data_series_get_visible (series));

    lrg_chart_data_series_set_visible (series, FALSE);
    g_assert_false (lrg_chart_data_series_get_visible (series));

    lrg_chart_data_series_set_visible (series, TRUE);
    g_assert_true (lrg_chart_data_series_get_visible (series));
}

static void
test_data_series_color (void)
{
    g_autoptr(LrgChartDataSeries) series = NULL;
    g_autoptr(GrlColor) color = NULL;
    const GrlColor *retrieved;

    series = lrg_chart_data_series_new ("Test");
    color = grl_color_new (128, 64, 32, 255);

    lrg_chart_data_series_set_color (series, color);

    retrieved = lrg_chart_data_series_get_color (series);
    g_assert_nonnull (retrieved);
    g_assert_cmpuint (grl_color_get_r (retrieved), ==, 128);
    g_assert_cmpuint (grl_color_get_g (retrieved), ==, 64);
    g_assert_cmpuint (grl_color_get_b (retrieved), ==, 32);
}

static void
test_data_series_line_width (void)
{
    g_autoptr(LrgChartDataSeries) series = NULL;

    series = lrg_chart_data_series_new ("Test");

    /* Default value */
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_series_get_line_width (series),
                                    2.0f, 0.0001f);

    lrg_chart_data_series_set_line_width (series, 5.0f);
    g_assert_cmpfloat_with_epsilon (lrg_chart_data_series_get_line_width (series),
                                    5.0f, 0.0001f);
}

static void
test_data_series_marker (void)
{
    g_autoptr(LrgChartDataSeries) series = NULL;

    series = lrg_chart_data_series_new ("Test");

    lrg_chart_data_series_set_marker (series, LRG_CHART_MARKER_DIAMOND);
    g_assert_cmpint (lrg_chart_data_series_get_marker (series),
                     ==, LRG_CHART_MARKER_DIAMOND);
}

/* ==========================================================================
 * LrgChartColorScale Tests
 * ========================================================================== */

static void
test_color_scale_new (void)
{
    g_autoptr(LrgChartColorScale) scale = NULL;

    scale = lrg_chart_color_scale_new ();

    g_assert_nonnull (scale);
    g_assert_true (LRG_IS_CHART_COLOR_SCALE (scale));
}

static void
test_color_scale_new_heat (void)
{
    g_autoptr(LrgChartColorScale) scale = NULL;

    scale = lrg_chart_color_scale_new_heat ();

    g_assert_nonnull (scale);
}

static void
test_color_scale_new_cool (void)
{
    g_autoptr(LrgChartColorScale) scale = NULL;

    scale = lrg_chart_color_scale_new_cool ();

    g_assert_nonnull (scale);
}

static void
test_color_scale_new_viridis (void)
{
    g_autoptr(LrgChartColorScale) scale = NULL;

    scale = lrg_chart_color_scale_new_viridis ();

    g_assert_nonnull (scale);
}

static void
test_color_scale_add_stop (void)
{
    g_autoptr(LrgChartColorScale) scale = NULL;
    g_autoptr(GrlColor) red = NULL;
    g_autoptr(GrlColor) blue = NULL;
    g_autoptr(GrlColor) start = NULL;
    g_autoptr(GrlColor) end = NULL;

    scale = lrg_chart_color_scale_new ();
    red = grl_color_new (255, 0, 0, 255);
    blue = grl_color_new (0, 0, 255, 255);

    lrg_chart_color_scale_add_stop (scale, 0.0, red);
    lrg_chart_color_scale_add_stop (scale, 1.0, blue);

    /* Test that stops were added by getting colors */
    start = lrg_chart_color_scale_get_color (scale, 0.0);
    end = lrg_chart_color_scale_get_color (scale, 1.0);

    g_assert_nonnull (start);
    g_assert_nonnull (end);
    g_assert_cmpuint (grl_color_get_r (start), ==, 255);
    g_assert_cmpuint (grl_color_get_b (end), ==, 255);
}

static void
test_color_scale_get_color_interpolation (void)
{
    g_autoptr(LrgChartColorScale) scale = NULL;
    g_autoptr(GrlColor) black = NULL;
    g_autoptr(GrlColor) white = NULL;
    g_autoptr(GrlColor) mid = NULL;

    scale = lrg_chart_color_scale_new ();
    black = grl_color_new (0, 0, 0, 255);
    white = grl_color_new (255, 255, 255, 255);

    lrg_chart_color_scale_add_stop (scale, 0.0, black);
    lrg_chart_color_scale_add_stop (scale, 1.0, white);

    /* At midpoint, should be approximately gray */
    mid = lrg_chart_color_scale_get_color (scale, 0.5);

    g_assert_nonnull (mid);
    /* Allow some tolerance for interpolation */
    g_assert_cmpuint (grl_color_get_r (mid), >=, 120);
    g_assert_cmpuint (grl_color_get_r (mid), <=, 135);
}

static void
test_color_scale_discrete_mode (void)
{
    g_autoptr(LrgChartColorScale) scale = NULL;

    scale = lrg_chart_color_scale_new ();

    /* Default should be interpolated (not discrete) */
    g_assert_false (lrg_chart_color_scale_get_discrete (scale));

    lrg_chart_color_scale_set_discrete (scale, TRUE);
    g_assert_true (lrg_chart_color_scale_get_discrete (scale));
}

/* ==========================================================================
 * Chart Creation Tests (Basic instantiation)
 * ========================================================================== */

static void
test_bar_chart2d_new (void)
{
    g_autoptr(LrgBarChart2D) chart = NULL;

    chart = lrg_bar_chart2d_new ();

    g_assert_nonnull (chart);
    g_assert_true (LRG_IS_BAR_CHART2D (chart));
    g_assert_true (LRG_IS_CHART2D (chart));
    g_assert_true (LRG_IS_CHART (chart));
}

static void
test_bar_chart2d_new_with_size (void)
{
    g_autoptr(LrgBarChart2D) chart = NULL;

    chart = lrg_bar_chart2d_new_with_size (400.0f, 300.0f);

    g_assert_nonnull (chart);
}

static void
test_line_chart2d_new (void)
{
    g_autoptr(LrgLineChart2D) chart = NULL;

    chart = lrg_line_chart2d_new ();

    g_assert_nonnull (chart);
    g_assert_true (LRG_IS_LINE_CHART2D (chart));
}

static void
test_pie_chart2d_new (void)
{
    g_autoptr(LrgPieChart2D) chart = NULL;

    chart = lrg_pie_chart2d_new ();

    g_assert_nonnull (chart);
    g_assert_true (LRG_IS_PIE_CHART2D (chart));
}

static void
test_area_chart2d_new (void)
{
    g_autoptr(LrgAreaChart2D) chart = NULL;

    chart = lrg_area_chart2d_new ();

    g_assert_nonnull (chart);
    g_assert_true (LRG_IS_AREA_CHART2D (chart));
}

static void
test_scatter_chart2d_new (void)
{
    g_autoptr(LrgScatterChart2D) chart = NULL;

    chart = lrg_scatter_chart2d_new ();

    g_assert_nonnull (chart);
    g_assert_true (LRG_IS_SCATTER_CHART2D (chart));
}

static void
test_radar_chart2d_new (void)
{
    g_autoptr(LrgRadarChart2D) chart = NULL;

    chart = lrg_radar_chart2d_new ();

    g_assert_nonnull (chart);
    g_assert_true (LRG_IS_RADAR_CHART2D (chart));
}

static void
test_candlestick_chart2d_new (void)
{
    g_autoptr(LrgCandlestickChart2D) chart = NULL;

    chart = lrg_candlestick_chart2d_new ();

    g_assert_nonnull (chart);
    g_assert_true (LRG_IS_CANDLESTICK_CHART2D (chart));
}

static void
test_gauge_chart2d_new (void)
{
    g_autoptr(LrgGaugeChart2D) chart = NULL;

    chart = lrg_gauge_chart2d_new ();

    g_assert_nonnull (chart);
    g_assert_true (LRG_IS_GAUGE_CHART2D (chart));
}

static void
test_heatmap_chart2d_new (void)
{
    g_autoptr(LrgHeatmapChart2D) chart = NULL;

    chart = lrg_heatmap_chart2d_new ();

    g_assert_nonnull (chart);
    g_assert_true (LRG_IS_HEATMAP_CHART2D (chart));
}

static void
test_histogram_chart2d_new (void)
{
    g_autoptr(LrgHistogramChart2D) chart = NULL;

    chart = lrg_histogram_chart2d_new ();

    g_assert_nonnull (chart);
    g_assert_true (LRG_IS_HISTOGRAM_CHART2D (chart));
}

/* 3D Charts */

static void
test_bar_chart3d_new (void)
{
    g_autoptr(LrgBarChart3D) chart = NULL;

    chart = lrg_bar_chart3d_new ();

    g_assert_nonnull (chart);
    g_assert_true (LRG_IS_BAR_CHART3D (chart));
    g_assert_true (LRG_IS_CHART3D (chart));
}

/* TODO: Uncomment when LrgLineChart3D is implemented
static void
test_line_chart3d_new (void)
{
    g_autoptr(LrgLineChart3D) chart = NULL;

    chart = lrg_line_chart3d_new ();

    g_assert_nonnull (chart);
    g_assert_true (LRG_IS_LINE_CHART3D (chart));
}
*/

/* TODO: Uncomment when LrgPieChart3D is implemented
static void
test_pie_chart3d_new (void)
{
    g_autoptr(LrgPieChart3D) chart = NULL;

    chart = lrg_pie_chart3d_new ();

    g_assert_nonnull (chart);
    g_assert_true (LRG_IS_PIE_CHART3D (chart));
}
*/

/* TODO: Uncomment when LrgSurfaceChart3D is implemented
static void
test_surface_chart3d_new (void)
{
    g_autoptr(LrgSurfaceChart3D) chart = NULL;

    chart = lrg_surface_chart3d_new ();

    g_assert_nonnull (chart);
    g_assert_true (LRG_IS_SURFACE_CHART3D (chart));
}
*/

/* TODO: Uncomment when LrgScatterChart3D is implemented
static void
test_scatter_chart3d_new (void)
{
    g_autoptr(LrgScatterChart3D) chart = NULL;

    chart = lrg_scatter_chart3d_new ();

    g_assert_nonnull (chart);
    g_assert_true (LRG_IS_SCATTER_CHART3D (chart));
}
*/

/* ==========================================================================
 * Chart Property Tests
 * ========================================================================== */

static void
test_bar_chart2d_properties (void)
{
    g_autoptr(LrgBarChart2D) chart = NULL;

    chart = lrg_bar_chart2d_new ();

    /* Test bar mode */
    lrg_bar_chart2d_set_bar_mode (chart, LRG_CHART_BAR_STACKED);
    g_assert_cmpint (lrg_bar_chart2d_get_bar_mode (chart), ==, LRG_CHART_BAR_STACKED);

    /* Test bar width ratio */
    lrg_bar_chart2d_set_bar_width_ratio (chart, 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_bar_chart2d_get_bar_width_ratio (chart), 0.5f, 0.0001f);

    /* Test bar spacing */
    lrg_bar_chart2d_set_bar_spacing (chart, 0.2f);
    g_assert_cmpfloat_with_epsilon (lrg_bar_chart2d_get_bar_spacing (chart), 0.2f, 0.0001f);
}

static void
test_line_chart2d_properties (void)
{
    g_autoptr(LrgLineChart2D) chart = NULL;

    chart = lrg_line_chart2d_new ();

    /* Test smooth */
    lrg_line_chart2d_set_smooth (chart, TRUE);
    g_assert_true (lrg_line_chart2d_get_smooth (chart));

    /* Test show markers */
    lrg_line_chart2d_set_show_markers (chart, FALSE);
    g_assert_false (lrg_line_chart2d_get_show_markers (chart));

    /* Test fill area */
    lrg_line_chart2d_set_fill_area (chart, TRUE);
    g_assert_true (lrg_line_chart2d_get_fill_area (chart));

    /* Test fill opacity */
    lrg_line_chart2d_set_fill_opacity (chart, 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_line_chart2d_get_fill_opacity (chart), 0.5f, 0.0001f);
}

static void
test_pie_chart2d_properties (void)
{
    g_autoptr(LrgPieChart2D) chart = NULL;

    chart = lrg_pie_chart2d_new ();

    /* Test inner radius (donut mode) */
    lrg_pie_chart2d_set_inner_radius (chart, 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_pie_chart2d_get_inner_radius (chart), 0.5f, 0.0001f);

    /* Test start angle */
    lrg_pie_chart2d_set_start_angle (chart, 90.0f);
    g_assert_cmpfloat_with_epsilon (lrg_pie_chart2d_get_start_angle (chart), 90.0f, 0.0001f);
}

static void
test_gauge_chart2d_properties (void)
{
    g_autoptr(LrgGaugeChart2D) chart = NULL;

    chart = lrg_gauge_chart2d_new ();

    /* Test value */
    lrg_gauge_chart2d_set_value (chart, 75.0);
    g_assert_cmpfloat_with_epsilon (lrg_gauge_chart2d_get_value (chart), 75.0, 0.0001);

    /* Test range */
    lrg_gauge_chart2d_set_min_value (chart, 0.0);
    lrg_gauge_chart2d_set_max_value (chart, 100.0);
    g_assert_cmpfloat_with_epsilon (lrg_gauge_chart2d_get_min_value (chart), 0.0, 0.0001);
    g_assert_cmpfloat_with_epsilon (lrg_gauge_chart2d_get_max_value (chart), 100.0, 0.0001);
}

/* TODO: Uncomment when LrgSurfaceChart3D is implemented
static void
test_surface_chart3d_grid (void)
{
    g_autoptr(LrgSurfaceChart3D) chart = NULL;

    chart = lrg_surface_chart3d_new ();

    lrg_surface_chart3d_set_grid_size (chart, 10, 10);
    g_assert_cmpuint (lrg_surface_chart3d_get_rows (chart), ==, 10);
    g_assert_cmpuint (lrg_surface_chart3d_get_cols (chart), ==, 10);

    lrg_surface_chart3d_set_value (chart, 0, 0, 1.5);
    lrg_surface_chart3d_set_value (chart, 5, 5, 3.0);

    g_assert_cmpfloat_with_epsilon (lrg_surface_chart3d_get_value (chart, 0, 0), 1.5, 0.0001);
    g_assert_cmpfloat_with_epsilon (lrg_surface_chart3d_get_value (chart, 5, 5), 3.0, 0.0001);
}
*/

/* TODO: Uncomment when LrgScatterChart3D is implemented
static void
test_scatter_chart3d_properties (void)
{
    g_autoptr(LrgScatterChart3D) chart = NULL;

    chart = lrg_scatter_chart3d_new ();

    lrg_scatter_chart3d_set_marker_style (chart, LRG_CHART_MARKER_SQUARE);
    g_assert_cmpint (lrg_scatter_chart3d_get_marker_style (chart), ==, LRG_CHART_MARKER_SQUARE);

    lrg_scatter_chart3d_set_marker_size (chart, 12.0f);
    g_assert_cmpfloat_with_epsilon (lrg_scatter_chart3d_get_marker_size (chart), 12.0f, 0.0001f);

    lrg_scatter_chart3d_set_size_by_value (chart, TRUE);
    g_assert_true (lrg_scatter_chart3d_get_size_by_value (chart));

    lrg_scatter_chart3d_set_depth_fade (chart, TRUE);
    g_assert_true (lrg_scatter_chart3d_get_depth_fade (chart));
}
*/

/* ==========================================================================
 * Chart with Data Tests
 * ========================================================================== */

static void
test_chart_add_series (void)
{
    g_autoptr(LrgBarChart2D) chart = NULL;
    LrgChartDataSeries *series;
    GPtrArray *all_series;

    chart = lrg_bar_chart2d_new ();
    series = lrg_chart_data_series_new ("Sales");

    lrg_chart_data_series_add_point_labeled (series, 0, 100.0, "Jan");
    lrg_chart_data_series_add_point_labeled (series, 1, 150.0, "Feb");
    lrg_chart_data_series_add_point_labeled (series, 2, 120.0, "Mar");

    /* add_series takes ownership, don't use g_autoptr for series */
    lrg_chart_add_series (LRG_CHART (chart), series);

    all_series = lrg_chart_get_series_list (LRG_CHART (chart));
    g_assert_cmpuint (all_series->len, ==, 1);
}

static void
test_chart_multiple_series (void)
{
    g_autoptr(LrgLineChart2D) chart = NULL;
    LrgChartDataSeries *series1;
    LrgChartDataSeries *series2;
    GPtrArray *all_series;

    chart = lrg_line_chart2d_new ();

    series1 = lrg_chart_data_series_new ("Dataset A");
    lrg_chart_data_series_add_point (series1, 0, 10.0);
    lrg_chart_data_series_add_point (series1, 1, 20.0);

    series2 = lrg_chart_data_series_new ("Dataset B");
    lrg_chart_data_series_add_point (series2, 0, 15.0);
    lrg_chart_data_series_add_point (series2, 1, 25.0);

    /* add_series takes ownership */
    lrg_chart_add_series (LRG_CHART (chart), series1);
    lrg_chart_add_series (LRG_CHART (chart), series2);

    all_series = lrg_chart_get_series_list (LRG_CHART (chart));
    g_assert_cmpuint (all_series->len, ==, 2);
}

static void
test_chart_remove_series (void)
{
    g_autoptr(LrgBarChart2D) chart = NULL;
    LrgChartDataSeries *series;
    GPtrArray *all_series;

    chart = lrg_bar_chart2d_new ();
    series = lrg_chart_data_series_new ("Test");

    lrg_chart_data_series_add_point (series, 0, 50.0);

    /* add_series takes ownership */
    lrg_chart_add_series (LRG_CHART (chart), series);
    g_assert_cmpuint (lrg_chart_get_series_list (LRG_CHART (chart))->len, ==, 1);

    /* remove_series takes index, not series pointer */
    lrg_chart_remove_series (LRG_CHART (chart), 0);
    all_series = lrg_chart_get_series_list (LRG_CHART (chart));
    g_assert_cmpuint (all_series->len, ==, 0);
}

static void
test_chart_title (void)
{
    g_autoptr(LrgBarChart2D) chart = NULL;

    chart = lrg_bar_chart2d_new ();

    lrg_chart_set_title (LRG_CHART (chart), "Monthly Sales");
    g_assert_cmpstr (lrg_chart_get_title (LRG_CHART (chart)), ==, "Monthly Sales");
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Data Point tests */
    g_test_add_func ("/chart/data-point/new", test_data_point_new);
    g_test_add_func ("/chart/data-point/new-with-z", test_data_point_new_with_z);
    g_test_add_func ("/chart/data-point/new-full", test_data_point_new_full);
    g_test_add_func ("/chart/data-point/new-labeled", test_data_point_new_labeled);
    g_test_add_func ("/chart/data-point/copy", test_data_point_copy);

    /* Axis Config tests */
    g_test_add_func ("/chart/axis-config/new", test_axis_config_new);
    g_test_add_func ("/chart/axis-config/new-with-title", test_axis_config_new_with_title);
    g_test_add_func ("/chart/axis-config/new-with-range", test_axis_config_new_with_range);
    g_test_add_func ("/chart/axis-config/copy", test_axis_config_copy);

    /* Hit Info tests */
    g_test_add_func ("/chart/hit-info/new", test_hit_info_new);
    g_test_add_func ("/chart/hit-info/copy", test_hit_info_copy);

    /* Data Series tests */
    g_test_add_func ("/chart/data-series/new", test_data_series_new);
    g_test_add_func ("/chart/data-series/add-point", test_data_series_add_point);
    g_test_add_func ("/chart/data-series/add-point-labeled", test_data_series_add_point_labeled);
    g_test_add_func ("/chart/data-series/add-point-full", test_data_series_add_point_full);
    g_test_add_func ("/chart/data-series/get-point", test_data_series_get_point);
    g_test_add_func ("/chart/data-series/remove-point", test_data_series_remove_point);
    g_test_add_func ("/chart/data-series/clear", test_data_series_clear);
    g_test_add_func ("/chart/data-series/visibility", test_data_series_visibility);
    g_test_add_func ("/chart/data-series/color", test_data_series_color);
    g_test_add_func ("/chart/data-series/line-width", test_data_series_line_width);
    g_test_add_func ("/chart/data-series/marker", test_data_series_marker);

    /* Color Scale tests */
    g_test_add_func ("/chart/color-scale/new", test_color_scale_new);
    g_test_add_func ("/chart/color-scale/new-heat", test_color_scale_new_heat);
    g_test_add_func ("/chart/color-scale/new-cool", test_color_scale_new_cool);
    g_test_add_func ("/chart/color-scale/new-viridis", test_color_scale_new_viridis);
    g_test_add_func ("/chart/color-scale/add-stop", test_color_scale_add_stop);
    g_test_add_func ("/chart/color-scale/interpolation", test_color_scale_get_color_interpolation);
    g_test_add_func ("/chart/color-scale/discrete-mode", test_color_scale_discrete_mode);

    /* 2D Chart creation tests */
    g_test_add_func ("/chart/2d/bar/new", test_bar_chart2d_new);
    g_test_add_func ("/chart/2d/bar/new-with-size", test_bar_chart2d_new_with_size);
    g_test_add_func ("/chart/2d/line/new", test_line_chart2d_new);
    g_test_add_func ("/chart/2d/pie/new", test_pie_chart2d_new);
    g_test_add_func ("/chart/2d/area/new", test_area_chart2d_new);
    g_test_add_func ("/chart/2d/scatter/new", test_scatter_chart2d_new);
    g_test_add_func ("/chart/2d/radar/new", test_radar_chart2d_new);
    g_test_add_func ("/chart/2d/candlestick/new", test_candlestick_chart2d_new);
    g_test_add_func ("/chart/2d/gauge/new", test_gauge_chart2d_new);
    g_test_add_func ("/chart/2d/heatmap/new", test_heatmap_chart2d_new);
    g_test_add_func ("/chart/2d/histogram/new", test_histogram_chart2d_new);

    /* 3D Chart creation tests */
    g_test_add_func ("/chart/3d/bar/new", test_bar_chart3d_new);
    /* TODO: Uncomment when implemented
    g_test_add_func ("/chart/3d/line/new", test_line_chart3d_new);
    g_test_add_func ("/chart/3d/pie/new", test_pie_chart3d_new);
    g_test_add_func ("/chart/3d/surface/new", test_surface_chart3d_new);
    g_test_add_func ("/chart/3d/scatter/new", test_scatter_chart3d_new);
    */

    /* Chart property tests */
    g_test_add_func ("/chart/2d/bar/properties", test_bar_chart2d_properties);
    g_test_add_func ("/chart/2d/line/properties", test_line_chart2d_properties);
    g_test_add_func ("/chart/2d/pie/properties", test_pie_chart2d_properties);
    g_test_add_func ("/chart/2d/gauge/properties", test_gauge_chart2d_properties);
    /* TODO: Uncomment when implemented
    g_test_add_func ("/chart/3d/surface/grid", test_surface_chart3d_grid);
    g_test_add_func ("/chart/3d/scatter/properties", test_scatter_chart3d_properties);
    */

    /* Chart with data tests */
    g_test_add_func ("/chart/base/add-series", test_chart_add_series);
    g_test_add_func ("/chart/base/multiple-series", test_chart_multiple_series);
    g_test_add_func ("/chart/base/remove-series", test_chart_remove_series);
    g_test_add_func ("/chart/base/title", test_chart_title);

    return g_test_run ();
}
