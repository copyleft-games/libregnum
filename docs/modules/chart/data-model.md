# Chart Data Model

The chart module uses a combination of GBoxed types (lightweight value types) and GObjects for the data model.

## LrgChartDataPoint (GBoxed)

A data point represents a single value in a series. Points can have up to 4 coordinate values plus optional metadata.

### Construction

```c
/* Simple 2D point */
LrgChartDataPoint *point = lrg_chart_data_point_new (10.0, 50.0);

/* Point with Z coordinate (3D charts, bubble size) */
LrgChartDataPoint *point = lrg_chart_data_point_new_with_z (10.0, 50.0, 5.0);

/* Full point (OHLC: open, high, low, close) */
LrgChartDataPoint *point = lrg_chart_data_point_new_full (10.0, 150.0, 145.0, 160.0);

/* Point with label (for categorical axes) */
LrgChartDataPoint *point = lrg_chart_data_point_new_labeled (0.0, 100.0, "January");
```

### Accessor Functions

```c
gdouble x = lrg_chart_data_point_get_x (point);
gdouble y = lrg_chart_data_point_get_y (point);
gdouble z = lrg_chart_data_point_get_z (point);  /* 0.0 if not set */
gdouble w = lrg_chart_data_point_get_w (point);  /* 0.0 if not set */
const gchar *label = lrg_chart_data_point_get_label (point);  /* NULL if not set */
const GrlColor *color = lrg_chart_data_point_get_color (point);  /* Per-point override */
```

### Coordinate Usage by Chart Type

| Chart Type | x | y | z | w | label |
|------------|---|---|---|---|-------|
| Bar | Category index | Value | - | - | Category name |
| Line | X position | Y value | - | - | Point label |
| Pie | - | Slice size | - | - | Slice label |
| Scatter | X position | Y position | Bubble size | - | Point label |
| Candlestick | Time index | Open | High | Low (close via separate field) | Date |
| Heatmap | Row | Column | Value | - | Cell label |

### Memory Management

```c
/* Copy a point */
LrgChartDataPoint *copy = lrg_chart_data_point_copy (point);

/* Free a point */
lrg_chart_data_point_free (point);

/* Use g_autoptr for automatic cleanup */
g_autoptr(LrgChartDataPoint) point = lrg_chart_data_point_new (1.0, 2.0);
```

## LrgChartDataSeries (GObject)

A data series is a collection of data points with visual properties.

### Construction

```c
LrgChartDataSeries *series = lrg_chart_data_series_new ("Sales 2024");
```

### Adding Points

```c
/* Add simple points */
lrg_chart_data_series_add_point (series, 1.0, 100.0);
lrg_chart_data_series_add_point (series, 2.0, 150.0);

/* Add labeled points */
lrg_chart_data_series_add_point_labeled (series, 0.0, 100.0, "Jan");
lrg_chart_data_series_add_point_labeled (series, 1.0, 150.0, "Feb");

/* Add pre-created point (takes ownership) */
LrgChartDataPoint *point = lrg_chart_data_point_new_with_z (1.0, 2.0, 3.0);
lrg_chart_data_series_add_point_full (series, point);

/* Insert at specific index */
lrg_chart_data_series_insert_point (series, 0, 0.0, 50.0);
```

### Accessing Points

```c
guint count = lrg_chart_data_series_get_point_count (series);

for (guint i = 0; i < count; i++)
{
    const LrgChartDataPoint *point = lrg_chart_data_series_get_point (series, i);
    g_print ("Point %u: (%.2f, %.2f)\n", i,
             lrg_chart_data_point_get_x (point),
             lrg_chart_data_point_get_y (point));
}
```

### Modifying Series

```c
/* Remove a point */
lrg_chart_data_series_remove_point (series, 2);

/* Clear all points */
lrg_chart_data_series_clear (series);
```

### Visual Properties

```c
/* Color */
g_autoptr(GrlColor) color = grl_color_new (255, 100, 100, 255);
lrg_chart_data_series_set_color (series, color);

/* Line width (for line charts) */
lrg_chart_data_series_set_line_width (series, 2.0f);

/* Marker style */
lrg_chart_data_series_set_marker (series, LRG_CHART_MARKER_CIRCLE);
lrg_chart_data_series_set_marker_size (series, 8.0f);

/* Visibility */
lrg_chart_data_series_set_visible (series, TRUE);
lrg_chart_data_series_set_show_in_legend (series, TRUE);
```

### Marker Styles

```c
LRG_CHART_MARKER_NONE      /* No marker */
LRG_CHART_MARKER_CIRCLE    /* Filled circle */
LRG_CHART_MARKER_SQUARE    /* Filled square */
LRG_CHART_MARKER_DIAMOND   /* Diamond shape */
LRG_CHART_MARKER_TRIANGLE  /* Triangle pointing up */
LRG_CHART_MARKER_CROSS     /* Plus sign */
LRG_CHART_MARKER_X         /* X mark */
```

### Signals

```c
/* Emitted when a point is added */
g_signal_connect (series, "point-added",
                  G_CALLBACK (on_point_added), NULL);

/* Emitted when a point is removed */
g_signal_connect (series, "point-removed",
                  G_CALLBACK (on_point_removed), NULL);

/* Emitted when series data changes */
g_signal_connect (series, "changed",
                  G_CALLBACK (on_series_changed), NULL);
```

## LrgChartAxisConfig (GBoxed)

Axis configuration controls how chart axes are displayed.

### Construction

```c
/* Default configuration */
LrgChartAxisConfig *config = lrg_chart_axis_config_new ();

/* With title */
LrgChartAxisConfig *config = lrg_chart_axis_config_new_with_title ("Revenue ($)");

/* With range (min, max, step) */
LrgChartAxisConfig *config = lrg_chart_axis_config_new_with_range (0.0, 100.0, 10.0);
```

### Properties

```c
/* Title */
lrg_chart_axis_config_set_title (config, "Revenue ($)");

/* Range (use NAN for auto) */
lrg_chart_axis_config_set_min (config, 0.0);
lrg_chart_axis_config_set_max (config, 100.0);
lrg_chart_axis_config_set_step (config, 10.0);

/* Grid lines */
lrg_chart_axis_config_set_show_grid (config, TRUE);

/* Logarithmic scale */
lrg_chart_axis_config_set_logarithmic (config, TRUE);

/* Label format (printf-style) */
lrg_chart_axis_config_set_format (config, "%.1f%%");

/* Colors */
g_autoptr(GrlColor) axis_color = grl_color_new (100, 100, 100, 255);
lrg_chart_axis_config_set_color (config, axis_color);
lrg_chart_axis_config_set_grid_color (config, grid_color);

/* Label rotation (degrees) */
lrg_chart_axis_config_set_label_rotation (config, 45.0f);
```

## LrgChartHitInfo (GBoxed)

Hit test results returned when interacting with charts.

### Accessor Functions

```c
static void
on_data_clicked (LrgChart        *chart,
                 LrgChartHitInfo *hit,
                 gpointer         user_data)
{
    gint series_idx = lrg_chart_hit_info_get_series_index (hit);
    gint point_idx = lrg_chart_hit_info_get_point_index (hit);
    const LrgChartDataPoint *point = lrg_chart_hit_info_get_data_point (hit);

    /* Screen coordinates of the hit */
    gfloat sx = lrg_chart_hit_info_get_screen_x (hit);
    gfloat sy = lrg_chart_hit_info_get_screen_y (hit);

    /* Bounding rectangle of the hit element */
    GrlRectangle bounds;
    lrg_chart_hit_info_get_bounds (hit, &bounds);
}
```

## LrgChartColorScale (GObject)

Color scales map numeric values to colors, primarily used for heatmaps.

### Predefined Scales

```c
/* Heat scale (cold to hot) */
LrgChartColorScale *scale = lrg_chart_color_scale_new_heat ();

/* Cool scale (warm to cool) */
LrgChartColorScale *scale = lrg_chart_color_scale_new_cool ();

/* Viridis (perceptually uniform) */
LrgChartColorScale *scale = lrg_chart_color_scale_new_viridis ();
```

### Custom Scale

```c
LrgChartColorScale *scale = lrg_chart_color_scale_new ();

/* Add color stops (value 0.0 to 1.0) */
g_autoptr(GrlColor) red = grl_color_new (255, 0, 0, 255);
g_autoptr(GrlColor) yellow = grl_color_new (255, 255, 0, 255);
g_autoptr(GrlColor) green = grl_color_new (0, 255, 0, 255);

lrg_chart_color_scale_add_stop (scale, 0.0, red);
lrg_chart_color_scale_add_stop (scale, 0.5, yellow);
lrg_chart_color_scale_add_stop (scale, 1.0, green);
```

### Using the Scale

```c
/* Get color for a normalized value (0.0 to 1.0) */
g_autoptr(GrlColor) color = lrg_chart_color_scale_get_color (scale, 0.75);

/* Discrete mode (no interpolation) */
lrg_chart_color_scale_set_discrete (scale, TRUE);
```
