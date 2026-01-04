# Chart Module

The Chart module provides a comprehensive charting and graphing system for data visualization. Charts are UI widgets (extend `LrgWidget`) with full interactivity, animations, and support for common chart types in both 2D and 3D.

## Overview

- **LrgChartDataPoint** - GBoxed data point (x, y, z, w, label, color)
- **LrgChartAxisConfig** - GBoxed axis configuration
- **LrgChartHitInfo** - GBoxed hit test result
- **LrgChartDataSeries** - GObject for series with points
- **LrgChartColorScale** - Color gradient for heatmaps
- **LrgChart** - Abstract base class (derivable)
- **LrgChart2D** - Intermediate class for 2D charts (derivable)
- **LrgChart3D** - Intermediate class for 3D charts (derivable)

### 2D Chart Types

| Type | Description |
|------|-------------|
| `LrgBarChart2D` | Grouped, stacked, or percent bar charts |
| `LrgLineChart2D` | Lines with markers and optional area fill |
| `LrgPieChart2D` | Pie and donut charts |
| `LrgAreaChart2D` | Filled area charts |
| `LrgScatterChart2D` | Scatter plots with markers |
| `LrgRadarChart2D` | Spider/radar polygon charts |
| `LrgCandlestickChart2D` | OHLC financial charts |
| `LrgGaugeChart2D` | Dial/meter displays |
| `LrgHeatmapChart2D` | Color-coded grid |
| `LrgHistogramChart2D` | Binned distribution |

### 3D Chart Types

| Type | Description |
|------|-------------|
| `LrgBarChart3D` | 3D bar charts with depth |

## Quick Start

```c
#include <libregnum.h>

/* Create a bar chart */
g_autoptr(LrgBarChart2D) chart = g_object_new (LRG_TYPE_BAR_CHART2D,
    "width", 400.0f,
    "height", 300.0f,
    NULL);

lrg_chart_set_title (LRG_CHART (chart), "Monthly Sales");

/* Create a data series */
g_autoptr(LrgChartDataSeries) sales = lrg_chart_data_series_new ("2024");

/* Add data points with labels */
lrg_chart_data_series_add_point_labeled (sales, 0.0, 1200.0, "Jan");
lrg_chart_data_series_add_point_labeled (sales, 1.0, 1800.0, "Feb");
lrg_chart_data_series_add_point_labeled (sales, 2.0, 1500.0, "Mar");
lrg_chart_data_series_add_point_labeled (sales, 3.0, 2200.0, "Apr");

/* Add series to chart (chart takes ownership) */
lrg_chart_add_series (LRG_CHART (chart), g_steal_pointer (&sales));

/* Connect to click signal */
g_signal_connect (chart, "data-clicked",
                  G_CALLBACK (on_bar_clicked), NULL);

/* Add to UI container */
lrg_container_add_child (container, LRG_WIDGET (chart));
```

## Architecture

```
LrgWidget
 |
 +-- LrgChart (abstract derivable)
      |  - Properties: title, margins, colors, animation
      |  - Signals: "data-clicked", "hover-changed", "data-changed"
      |  - vfuncs: update_data(), rebuild_layout(), hit_test()
      |
      +-- LrgChart2D (intermediate derivable)
      |    |  - Properties: x-axis, y-axis, grid, legend
      |    |  - vfuncs: draw_axes(), draw_grid(), draw_data()
      |    |
      |    +-- LrgBarChart2D, LrgLineChart2D, LrgPieChart2D, etc.
      |
      +-- LrgChart3D (intermediate derivable)
           |  - Properties: camera-distance, rotation-x/y
           |  - vfuncs: draw_axes_3d(), draw_data_3d()
           |
           +-- LrgBarChart3D
```

## Interactivity

Charts support mouse interaction for tooltips and click handling:

```c
/* Enable hover effects */
lrg_chart_set_hover_enabled (LRG_CHART (chart), TRUE);

/* Handle clicks */
static void
on_bar_clicked (LrgChart        *chart,
                LrgChartHitInfo *hit,
                gpointer         user_data)
{
    const LrgChartDataPoint *point;

    point = lrg_chart_hit_info_get_data_point (hit);
    g_print ("Clicked: series %d, point %d (x=%.2f, y=%.2f)\n",
             lrg_chart_hit_info_get_series_index (hit),
             lrg_chart_hit_info_get_point_index (hit),
             lrg_chart_data_point_get_x (point),
             lrg_chart_data_point_get_y (point));
}

g_signal_connect (chart, "data-clicked",
                  G_CALLBACK (on_bar_clicked), NULL);
```

## Animation

Charts animate smoothly when data changes:

```c
/* Set animation type and duration */
lrg_chart_set_animation_type (LRG_CHART (chart), LRG_CHART_ANIM_GROW);
lrg_chart_set_animation_duration (LRG_CHART (chart), 0.5f);

/* Trigger animation when adding new data */
lrg_chart_animate_to_data (LRG_CHART (chart),
                           LRG_CHART_ANIM_GROW,
                           0.5f);
```

Available animation types:
- `LRG_CHART_ANIM_NONE` - No animation
- `LRG_CHART_ANIM_GROW` - Bars/lines grow from baseline
- `LRG_CHART_ANIM_FADE` - Elements fade in
- `LRG_CHART_ANIM_SLIDE` - Elements slide into position
- `LRG_CHART_ANIM_MORPH` - Smooth morphing between states

## Files

| File | Description |
|------|-------------|
| [data-model.md](data-model.md) | Data structures (points, series, axis config) |
| [chart-types.md](chart-types.md) | All chart types with examples |
| [customization.md](customization.md) | Styling, colors, and theming |
