# Chart Customization

This guide covers styling, theming, and customization options for charts.

## Colors

### Background and Text

```c
/* Chart background */
g_autoptr(GrlColor) bg = grl_color_new (255, 255, 255, 255);
lrg_chart_set_background_color (LRG_CHART (chart), bg);

/* Text color for all labels */
g_autoptr(GrlColor) text = grl_color_new (50, 50, 50, 255);
lrg_chart_set_text_color (LRG_CHART (chart), text);
```

### Series Colors

Each series can have its own color:

```c
g_autoptr(GrlColor) color1 = grl_color_new (66, 133, 244, 255);   /* Blue */
g_autoptr(GrlColor) color2 = grl_color_new (234, 67, 53, 255);    /* Red */
g_autoptr(GrlColor) color3 = grl_color_new (52, 168, 83, 255);    /* Green */

lrg_chart_data_series_set_color (series1, color1);
lrg_chart_data_series_set_color (series2, color2);
lrg_chart_data_series_set_color (series3, color3);
```

### Automatic Color Palette

Charts automatically assign colors from a default palette when series colors aren't set. Override the palette:

```c
GrlColor *palette[] = {
    grl_color_new (0x4285, 0xF4FF, 0, 255),  /* Google Blue */
    grl_color_new (0xEA43, 0x35FF, 0, 255),  /* Google Red */
    grl_color_new (0xFBBC, 0x05FF, 0, 255),  /* Google Yellow */
    grl_color_new (0x34A8, 0x53FF, 0, 255),  /* Google Green */
};
lrg_chart2d_set_color_palette (LRG_CHART2D (chart), palette, 4);
```

### Per-Point Colors

Individual data points can override series colors:

```c
LrgChartDataPoint *point = lrg_chart_data_point_new (1.0, 100.0);
g_autoptr(GrlColor) highlight = grl_color_new (255, 0, 0, 255);
lrg_chart_data_point_set_color (point, highlight);
lrg_chart_data_series_add_point_full (series, point);
```

## Margins and Layout

### Chart Margins

```c
/* Set all margins at once: top, right, bottom, left */
lrg_chart_set_margins (LRG_CHART (chart), 50.0f, 30.0f, 50.0f, 60.0f);

/* Or individually */
gfloat top = lrg_chart_get_margin_top (LRG_CHART (chart));
```

### Content Bounds

Get the actual drawing area (excluding margins, axes, legend):

```c
GrlRectangle bounds;
lrg_chart_get_content_bounds (LRG_CHART (chart), &bounds);
g_print ("Chart area: %.0f x %.0f at (%.0f, %.0f)\n",
         bounds.width, bounds.height, bounds.x, bounds.y);
```

## Axes (2D Charts)

### Axis Titles

```c
g_autoptr(LrgChartAxisConfig) x_config = lrg_chart_axis_config_new ();
lrg_chart_axis_config_set_title (x_config, "Month");

g_autoptr(LrgChartAxisConfig) y_config = lrg_chart_axis_config_new ();
lrg_chart_axis_config_set_title (y_config, "Revenue ($)");

lrg_chart2d_set_x_axis (LRG_CHART2D (chart), x_config);
lrg_chart2d_set_y_axis (LRG_CHART2D (chart), y_config);
```

### Axis Range and Ticks

```c
/* Fixed range */
lrg_chart_axis_config_set_min (y_config, 0.0);
lrg_chart_axis_config_set_max (y_config, 1000.0);
lrg_chart_axis_config_set_step (y_config, 100.0);

/* Auto range (use NAN) */
lrg_chart_axis_config_set_min (y_config, NAN);  /* Auto min */
lrg_chart_axis_config_set_max (y_config, NAN);  /* Auto max */
```

### Logarithmic Scale

```c
lrg_chart_axis_config_set_logarithmic (y_config, TRUE);
```

### Label Formatting

```c
/* Currency format */
lrg_chart_axis_config_set_format (y_config, "$%.2f");

/* Percentage */
lrg_chart_axis_config_set_format (y_config, "%.1f%%");

/* Scientific notation */
lrg_chart_axis_config_set_format (y_config, "%.2e");
```

### Rotated Labels

```c
/* Rotate X-axis labels for long text */
lrg_chart_axis_config_set_label_rotation (x_config, 45.0f);
```

### Axis Colors

```c
g_autoptr(GrlColor) axis_color = grl_color_new (100, 100, 100, 255);
lrg_chart_axis_config_set_color (x_config, axis_color);
```

## Grid Lines

```c
/* Show/hide grid */
lrg_chart2d_set_show_grid (LRG_CHART2D (chart), TRUE);

/* Grid for specific axis */
lrg_chart_axis_config_set_show_grid (x_config, FALSE);
lrg_chart_axis_config_set_show_grid (y_config, TRUE);

/* Grid color */
g_autoptr(GrlColor) grid_color = grl_color_new (200, 200, 200, 128);
lrg_chart_axis_config_set_grid_color (y_config, grid_color);
```

## Legend

### Position and Visibility

```c
lrg_chart2d_set_show_legend (LRG_CHART2D (chart), TRUE);
lrg_chart2d_set_legend_position (LRG_CHART2D (chart), LRG_LEGEND_RIGHT);
```

**Legend Positions:**
- `LRG_LEGEND_TOP` - Above the chart
- `LRG_LEGEND_BOTTOM` - Below the chart
- `LRG_LEGEND_LEFT` - Left of the chart
- `LRG_LEGEND_RIGHT` - Right of the chart

### Per-Series Legend Control

```c
/* Hide a series from the legend */
lrg_chart_data_series_set_show_in_legend (series, FALSE);
```

## Line and Marker Styles

### Line Width

```c
lrg_chart_data_series_set_line_width (series, 2.0f);
```

### Line Style

```c
lrg_chart_data_series_set_line_style (series, LRG_CHART_LINE_DASHED);
```

**Line Styles:**
- `LRG_CHART_LINE_SOLID` - Continuous line
- `LRG_CHART_LINE_DASHED` - Dashed line
- `LRG_CHART_LINE_DOTTED` - Dotted line
- `LRG_CHART_LINE_NONE` - No line (markers only)

### Markers

```c
lrg_chart_data_series_set_marker (series, LRG_CHART_MARKER_CIRCLE);
lrg_chart_data_series_set_marker_size (series, 8.0f);
```

**Marker Styles:**
- `LRG_CHART_MARKER_NONE` - No marker
- `LRG_CHART_MARKER_CIRCLE` - Filled circle
- `LRG_CHART_MARKER_SQUARE` - Filled square
- `LRG_CHART_MARKER_DIAMOND` - Diamond shape
- `LRG_CHART_MARKER_TRIANGLE` - Triangle
- `LRG_CHART_MARKER_CROSS` - Plus sign (+)
- `LRG_CHART_MARKER_X` - X mark

## Animation

### Animation Types

```c
lrg_chart_set_animation_type (LRG_CHART (chart), LRG_CHART_ANIM_GROW);
lrg_chart_set_animation_duration (LRG_CHART (chart), 0.5f);
```

**Animation Types:**
- `LRG_CHART_ANIM_NONE` - No animation
- `LRG_CHART_ANIM_GROW` - Elements grow from baseline
- `LRG_CHART_ANIM_FADE` - Elements fade in
- `LRG_CHART_ANIM_SLIDE` - Elements slide into position
- `LRG_CHART_ANIM_MORPH` - Smooth morphing between states

### Triggering Animation

```c
/* Animate when data changes */
lrg_chart_animate_to_data (LRG_CHART (chart),
                           LRG_CHART_ANIM_MORPH,
                           0.3f);

/* Check animation progress */
gfloat progress = lrg_chart_get_animation_progress (LRG_CHART (chart));
```

## Interactivity

### Hover Effects

```c
lrg_chart_set_hover_enabled (LRG_CHART (chart), TRUE);

/* Get current hover state */
const LrgChartHitInfo *hover = lrg_chart_get_current_hover (LRG_CHART (chart));
if (hover != NULL)
{
    g_print ("Hovering series %d, point %d\n",
             lrg_chart_hit_info_get_series_index (hover),
             lrg_chart_hit_info_get_point_index (hover));
}
```

### Click Handling

```c
static void
on_click (LrgChart        *chart,
          LrgChartHitInfo *hit,
          gpointer         user_data)
{
    g_print ("Clicked series %d point %d\n",
             lrg_chart_hit_info_get_series_index (hit),
             lrg_chart_hit_info_get_point_index (hit));
}

g_signal_connect (chart, "data-clicked", G_CALLBACK (on_click), NULL);
```

### Hover Changed Signal

```c
static void
on_hover (LrgChart        *chart,
          LrgChartHitInfo *hit,  /* NULL if nothing hovered */
          gpointer         user_data)
{
    if (hit != NULL)
        g_print ("Now hovering point %d\n", lrg_chart_hit_info_get_point_index (hit));
    else
        g_print ("Hover ended\n");
}

g_signal_connect (chart, "hover-changed", G_CALLBACK (on_hover), NULL);
```

## Custom Color Scales (Heatmaps)

### Built-in Scales

```c
LrgChartColorScale *heat = lrg_chart_color_scale_new_heat ();     /* Blue->Red */
LrgChartColorScale *cool = lrg_chart_color_scale_new_cool ();     /* Green->Purple */
LrgChartColorScale *viridis = lrg_chart_color_scale_new_viridis ();
```

### Custom Gradient

```c
LrgChartColorScale *scale = lrg_chart_color_scale_new ();

g_autoptr(GrlColor) low = grl_color_new (0, 0, 255, 255);    /* Blue */
g_autoptr(GrlColor) mid = grl_color_new (255, 255, 0, 255);  /* Yellow */
g_autoptr(GrlColor) high = grl_color_new (255, 0, 0, 255);   /* Red */

lrg_chart_color_scale_add_stop (scale, 0.0, low);
lrg_chart_color_scale_add_stop (scale, 0.5, mid);
lrg_chart_color_scale_add_stop (scale, 1.0, high);
```

### Discrete Mode

```c
/* Use discrete colors instead of interpolation */
lrg_chart_color_scale_set_discrete (scale, TRUE);
```

## Dark Mode Example

```c
/* Dark theme colors */
g_autoptr(GrlColor) dark_bg = grl_color_new (30, 30, 30, 255);
g_autoptr(GrlColor) light_text = grl_color_new (220, 220, 220, 255);
g_autoptr(GrlColor) grid = grl_color_new (60, 60, 60, 200);

lrg_chart_set_background_color (LRG_CHART (chart), dark_bg);
lrg_chart_set_text_color (LRG_CHART (chart), light_text);

g_autoptr(LrgChartAxisConfig) x_axis = lrg_chart_axis_config_new ();
lrg_chart_axis_config_set_color (x_axis, light_text);
lrg_chart_axis_config_set_grid_color (x_axis, grid);
lrg_chart2d_set_x_axis (LRG_CHART2D (chart), x_axis);

/* Bright series colors for contrast */
g_autoptr(GrlColor) cyan = grl_color_new (0, 255, 255, 255);
g_autoptr(GrlColor) magenta = grl_color_new (255, 0, 255, 255);
lrg_chart_data_series_set_color (series1, cyan);
lrg_chart_data_series_set_color (series2, magenta);
```
