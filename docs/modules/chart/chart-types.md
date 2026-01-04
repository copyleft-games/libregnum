# Chart Types

This page documents all available chart types with usage examples.

## 2D Charts

### LrgBarChart2D

Bar charts display categorical data with rectangular bars.

```c
g_autoptr(LrgBarChart2D) chart = lrg_bar_chart2d_new ();
lrg_widget_set_size (LRG_WIDGET (chart), 400.0f, 300.0f);

/* Bar mode: grouped, stacked, or percent */
lrg_bar_chart2d_set_bar_mode (chart, LRG_CHART_BAR_GROUPED);

/* Bar width ratio (0.0 to 1.0, relative to category width) */
lrg_bar_chart2d_set_bar_width_ratio (chart, 0.8f);

/* Spacing between bars in a group */
lrg_bar_chart2d_set_bar_spacing (chart, 0.1f);

/* Add data */
g_autoptr(LrgChartDataSeries) q1 = lrg_chart_data_series_new ("Q1");
lrg_chart_data_series_add_point_labeled (q1, 0.0, 100.0, "Jan");
lrg_chart_data_series_add_point_labeled (q1, 1.0, 120.0, "Feb");
lrg_chart_data_series_add_point_labeled (q1, 2.0, 90.0, "Mar");
lrg_chart_add_series (LRG_CHART (chart), g_steal_pointer (&q1));
```

**Bar Modes:**
- `LRG_CHART_BAR_GROUPED` - Side by side bars for each category
- `LRG_CHART_BAR_STACKED` - Bars stacked on top of each other
- `LRG_CHART_BAR_PERCENT` - Stacked to 100% (show proportions)

### LrgLineChart2D

Line charts display data as connected points.

```c
g_autoptr(LrgLineChart2D) chart = lrg_line_chart2d_new ();

/* Smooth curves (Bezier interpolation) */
lrg_line_chart2d_set_smooth (chart, TRUE);

/* Show data point markers */
lrg_line_chart2d_set_show_markers (chart, TRUE);

/* Fill area under line */
lrg_line_chart2d_set_fill_area (chart, TRUE);
lrg_line_chart2d_set_fill_opacity (chart, 0.3f);

/* Add data */
g_autoptr(LrgChartDataSeries) series = lrg_chart_data_series_new ("Temperature");
lrg_chart_data_series_set_line_width (series, 2.0f);
lrg_chart_data_series_set_marker (series, LRG_CHART_MARKER_CIRCLE);

for (gint i = 0; i < 12; i++)
{
    lrg_chart_data_series_add_point (series, (gdouble)i, 20.0 + rand() % 15);
}
lrg_chart_add_series (LRG_CHART (chart), g_steal_pointer (&series));
```

### LrgPieChart2D

Pie charts show proportional data as slices.

```c
g_autoptr(LrgPieChart2D) chart = lrg_pie_chart2d_new ();

/* Donut chart (inner radius > 0) */
lrg_pie_chart2d_set_inner_radius (chart, 0.5f);

/* Start angle (degrees, 0 = right, 90 = top) */
lrg_pie_chart2d_set_start_angle (chart, 90.0f);

/* Add slices (Y value = slice size, label = slice name) */
g_autoptr(LrgChartDataSeries) series = lrg_chart_data_series_new ("Market Share");
lrg_chart_data_series_add_point_labeled (series, 0.0, 35.0, "Product A");
lrg_chart_data_series_add_point_labeled (series, 1.0, 25.0, "Product B");
lrg_chart_data_series_add_point_labeled (series, 2.0, 20.0, "Product C");
lrg_chart_data_series_add_point_labeled (series, 3.0, 20.0, "Other");
lrg_chart_add_series (LRG_CHART (chart), g_steal_pointer (&series));
```

### LrgAreaChart2D

Area charts are line charts with filled regions.

```c
g_autoptr(LrgAreaChart2D) chart = lrg_area_chart2d_new ();

/* Stacked areas */
lrg_area_chart2d_set_stacked (chart, TRUE);

/* Fill opacity */
lrg_area_chart2d_set_fill_opacity (chart, 0.6f);

/* Add multiple series for stacked view */
g_autoptr(LrgChartDataSeries) desktop = lrg_chart_data_series_new ("Desktop");
g_autoptr(LrgChartDataSeries) mobile = lrg_chart_data_series_new ("Mobile");
g_autoptr(LrgChartDataSeries) tablet = lrg_chart_data_series_new ("Tablet");

/* Add data points... */
lrg_chart_add_series (LRG_CHART (chart), g_steal_pointer (&desktop));
lrg_chart_add_series (LRG_CHART (chart), g_steal_pointer (&mobile));
lrg_chart_add_series (LRG_CHART (chart), g_steal_pointer (&tablet));
```

### LrgScatterChart2D

Scatter charts show relationships between two variables.

```c
g_autoptr(LrgScatterChart2D) chart = lrg_scatter_chart2d_new ();

/* Marker style and size */
lrg_scatter_chart2d_set_marker_style (chart, LRG_CHART_MARKER_CIRCLE);
lrg_scatter_chart2d_set_marker_size (chart, 8.0f);

/* Bubble mode (size based on Z value) */
lrg_scatter_chart2d_set_size_by_value (chart, TRUE);

/* Show trend line */
lrg_scatter_chart2d_set_show_trend_line (chart, TRUE);

/* Add points with optional Z for bubble size */
g_autoptr(LrgChartDataSeries) series = lrg_chart_data_series_new ("Data");
for (gint i = 0; i < 50; i++)
{
    gdouble x = rand() % 100;
    gdouble y = x * 0.5 + (rand() % 20 - 10);
    gdouble size = rand() % 20 + 5;  /* Bubble size */

    LrgChartDataPoint *pt = lrg_chart_data_point_new_with_z (x, y, size);
    lrg_chart_data_series_add_point_full (series, pt);
}
lrg_chart_add_series (LRG_CHART (chart), g_steal_pointer (&series));
```

### LrgRadarChart2D

Radar (spider) charts display multivariate data on radial axes.

```c
g_autoptr(LrgRadarChart2D) chart = lrg_radar_chart2d_new ();

/* Fill the polygon */
lrg_radar_chart2d_set_fill (chart, TRUE);
lrg_radar_chart2d_set_fill_opacity (chart, 0.3f);

/* Axis labels */
const gchar *labels[] = { "Speed", "Power", "Range", "Defense", "Agility" };
lrg_radar_chart2d_set_axis_labels (chart, labels, 5);

/* Add data (one value per axis) */
g_autoptr(LrgChartDataSeries) player1 = lrg_chart_data_series_new ("Player 1");
lrg_chart_data_series_add_point (player1, 0.0, 80.0);   /* Speed */
lrg_chart_data_series_add_point (player1, 1.0, 70.0);   /* Power */
lrg_chart_data_series_add_point (player1, 2.0, 60.0);   /* Range */
lrg_chart_data_series_add_point (player1, 3.0, 90.0);   /* Defense */
lrg_chart_data_series_add_point (player1, 4.0, 85.0);   /* Agility */
lrg_chart_add_series (LRG_CHART (chart), g_steal_pointer (&player1));
```

### LrgCandlestickChart2D

Candlestick charts for financial OHLC (Open, High, Low, Close) data.

```c
g_autoptr(LrgCandlestickChart2D) chart = lrg_candlestick_chart2d_new ();

/* Colors for up/down candles */
g_autoptr(GrlColor) up_color = grl_color_new (0, 200, 0, 255);
g_autoptr(GrlColor) down_color = grl_color_new (200, 0, 0, 255);
lrg_candlestick_chart2d_set_up_color (chart, up_color);
lrg_candlestick_chart2d_set_down_color (chart, down_color);

/* Candle width */
lrg_candlestick_chart2d_set_candle_width (chart, 0.8f);

/* Add OHLC data (x=index, y=open, z=high, w=low, then close separately) */
g_autoptr(LrgChartDataSeries) prices = lrg_chart_data_series_new ("STOCK");
/* Use add_point_full for OHLC: x=time, y=open, z=high, w=low */
/* Close is set via set_close_value or encoded differently */
lrg_chart_add_series (LRG_CHART (chart), g_steal_pointer (&prices));
```

### LrgGaugeChart2D

Gauge charts display a single value on a dial/meter.

```c
g_autoptr(LrgGaugeChart2D) chart = lrg_gauge_chart2d_new ();

/* Value range */
lrg_gauge_chart2d_set_min_value (chart, 0.0);
lrg_gauge_chart2d_set_max_value (chart, 100.0);

/* Current value */
lrg_gauge_chart2d_set_value (chart, 75.0);

/* Arc angles (degrees) */
lrg_gauge_chart2d_set_start_angle (chart, 135.0f);
lrg_gauge_chart2d_set_end_angle (chart, 405.0f);

/* Show value label */
lrg_gauge_chart2d_set_show_value (chart, TRUE);

/* Color zones (optional) */
lrg_gauge_chart2d_add_zone (chart, 0.0, 30.0, green);    /* Good */
lrg_gauge_chart2d_add_zone (chart, 30.0, 70.0, yellow);  /* Warning */
lrg_gauge_chart2d_add_zone (chart, 70.0, 100.0, red);    /* Danger */
```

### LrgHeatmapChart2D

Heatmaps display data as a colored grid.

```c
g_autoptr(LrgHeatmapChart2D) chart = lrg_heatmap_chart2d_new ();

/* Grid size */
lrg_heatmap_chart2d_set_grid_size (chart, 10, 7);  /* 10 columns, 7 rows */

/* Color scale */
g_autoptr(LrgChartColorScale) scale = lrg_chart_color_scale_new_heat ();
lrg_heatmap_chart2d_set_color_scale (chart, scale);

/* Show cell values */
lrg_heatmap_chart2d_set_show_values (chart, TRUE);

/* Set cell values */
for (guint row = 0; row < 7; row++)
{
    for (guint col = 0; col < 10; col++)
    {
        gdouble value = (gdouble)(rand() % 100) / 100.0;
        lrg_heatmap_chart2d_set_cell (chart, row, col, value);
    }
}

/* Row and column labels */
const gchar *days[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
lrg_heatmap_chart2d_set_row_labels (chart, days, 7);
```

### LrgHistogramChart2D

Histograms show frequency distribution of data.

```c
g_autoptr(LrgHistogramChart2D) chart = lrg_histogram_chart2d_new ();

/* Number of bins */
lrg_histogram_chart2d_set_bin_count (chart, 20);

/* Or specify bin width */
lrg_histogram_chart2d_set_bin_width (chart, 5.0);

/* Normalize to show density instead of counts */
lrg_histogram_chart2d_set_normalize (chart, TRUE);

/* Add raw data values (will be binned automatically) */
g_autoptr(LrgChartDataSeries) data = lrg_chart_data_series_new ("Measurements");
for (gint i = 0; i < 1000; i++)
{
    /* Generate normal-ish distribution */
    gdouble val = 50.0 + (rand() % 40 - 20) + (rand() % 40 - 20);
    lrg_chart_data_series_add_point (data, val, 1.0);
}
lrg_chart_add_series (LRG_CHART (chart), g_steal_pointer (&data));
```

## 3D Charts

### LrgBarChart3D

3D bar charts with depth perspective.

```c
g_autoptr(LrgBarChart3D) chart = lrg_bar_chart3d_new ();

/* Camera distance */
lrg_chart3d_set_camera_distance (LRG_CHART3D (chart), 500.0f);

/* Rotation angles */
lrg_chart3d_set_rotation_x (LRG_CHART3D (chart), 30.0f);
lrg_chart3d_set_rotation_y (LRG_CHART3D (chart), 45.0f);

/* Bar depth */
lrg_bar_chart3d_set_bar_depth (chart, 0.5f);

/* Add data (same as 2D) */
g_autoptr(LrgChartDataSeries) series = lrg_chart_data_series_new ("Sales");
lrg_chart_data_series_add_point_labeled (series, 0.0, 100.0, "Q1");
lrg_chart_data_series_add_point_labeled (series, 1.0, 150.0, "Q2");
lrg_chart_data_series_add_point_labeled (series, 2.0, 120.0, "Q3");
lrg_chart_data_series_add_point_labeled (series, 3.0, 180.0, "Q4");
lrg_chart_add_series (LRG_CHART (chart), g_steal_pointer (&series));
```

## Common Base Properties

All charts inherit from `LrgChart` and share these properties:

```c
/* Title */
lrg_chart_set_title (LRG_CHART (chart), "My Chart");

/* Margins */
lrg_chart_set_margins (LRG_CHART (chart), 40.0f, 20.0f, 40.0f, 60.0f);

/* Colors */
g_autoptr(GrlColor) bg = grl_color_new (240, 240, 240, 255);
g_autoptr(GrlColor) text = grl_color_new (50, 50, 50, 255);
lrg_chart_set_background_color (LRG_CHART (chart), bg);
lrg_chart_set_text_color (LRG_CHART (chart), text);

/* Animation */
lrg_chart_set_animation_type (LRG_CHART (chart), LRG_CHART_ANIM_GROW);
lrg_chart_set_animation_duration (LRG_CHART (chart), 0.5f);

/* Interactivity */
lrg_chart_set_hover_enabled (LRG_CHART (chart), TRUE);
```

## 2D Chart Properties

All 2D charts inherit from `LrgChart2D` and share these properties:

```c
/* Axis configuration */
g_autoptr(LrgChartAxisConfig) x_axis = lrg_chart_axis_config_new_with_title ("Month");
g_autoptr(LrgChartAxisConfig) y_axis = lrg_chart_axis_config_new_with_range (0.0, 100.0, 20.0);
lrg_chart2d_set_x_axis (LRG_CHART2D (chart), x_axis);
lrg_chart2d_set_y_axis (LRG_CHART2D (chart), y_axis);

/* Grid */
lrg_chart2d_set_show_grid (LRG_CHART2D (chart), TRUE);

/* Legend */
lrg_chart2d_set_show_legend (LRG_CHART2D (chart), TRUE);
lrg_chart2d_set_legend_position (LRG_CHART2D (chart), LRG_LEGEND_RIGHT);
```
