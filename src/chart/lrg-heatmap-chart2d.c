/* lrg-heatmap-chart2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-heatmap-chart2d.h"
#include "lrg-chart-data-series.h"
#include "lrg-chart-data-point.h"
#include "lrg-chart-hit-info.h"
#include "../lrg-log.h"

struct _LrgHeatmapChart2D
{
    LrgChart2D parent_instance;

    LrgChartColorScale *color_scale;

    /* Grid style */
    gfloat    cell_spacing;
    gfloat    cell_radius;
    gboolean  show_grid;
    GrlColor *grid_color;

    /* Value display */
    gboolean  show_values;
    gchar    *value_format;
    gfloat    value_font_size;

    /* Labels */
    GPtrArray *row_labels;
    GPtrArray *col_labels;

    /* Color scale legend */
    gboolean  show_scale;
    gfloat    scale_width;

    /* Cached grid info */
    guint     num_rows;
    guint     num_cols;
};

enum
{
    PROP_0,
    PROP_COLOR_SCALE,
    PROP_CELL_SPACING,
    PROP_CELL_RADIUS,
    PROP_SHOW_GRID,
    PROP_GRID_COLOR,
    PROP_SHOW_VALUES,
    PROP_VALUE_FORMAT,
    PROP_VALUE_FONT_SIZE,
    PROP_SHOW_SCALE,
    PROP_SCALE_WIDTH,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (LrgHeatmapChart2D, lrg_heatmap_chart2d, LRG_TYPE_CHART2D)

/* ==========================================================================
 * Internal Helpers
 * ========================================================================== */

/*
 * Calculate grid dimensions from data.
 * Data points use X for column, Y for row.
 */
static void
calculate_grid_size (LrgHeatmapChart2D *self)
{
    LrgChart *chart = LRG_CHART (self);
    GPtrArray *series_list;
    guint i, j;
    guint max_row;
    guint max_col;

    max_row = 0;
    max_col = 0;

    series_list = lrg_chart_get_series_list (chart);
    if (series_list == NULL)
    {
        self->num_rows = 0;
        self->num_cols = 0;
        return;
    }

    for (i = 0; i < series_list->len; i++)
    {
        LrgChartDataSeries *series;
        guint point_count;

        series = g_ptr_array_index (series_list, i);
        point_count = lrg_chart_data_series_get_point_count (series);

        for (j = 0; j < point_count; j++)
        {
            const LrgChartDataPoint *pt;
            guint col, row;

            pt = lrg_chart_data_series_get_point (series, j);
            col = (guint)(lrg_chart_data_point_get_x (pt) + 0.5);
            row = (guint)(lrg_chart_data_point_get_y (pt) + 0.5);

            if (col > max_col)
                max_col = col;
            if (row > max_row)
                max_row = row;
        }
    }

    self->num_cols = max_col + 1;
    self->num_rows = max_row + 1;
}

/*
 * Get the value at a specific grid cell.
 * Returns NAN if cell has no data.
 */
static gdouble
get_cell_value (LrgHeatmapChart2D *self,
                guint              col,
                guint              row)
{
    LrgChart *chart = LRG_CHART (self);
    GPtrArray *series_list;
    guint i, j;

    series_list = lrg_chart_get_series_list (chart);
    if (series_list == NULL)
        return G_MAXDOUBLE;

    for (i = 0; i < series_list->len; i++)
    {
        LrgChartDataSeries *series;
        guint point_count;

        series = g_ptr_array_index (series_list, i);
        point_count = lrg_chart_data_series_get_point_count (series);

        for (j = 0; j < point_count; j++)
        {
            const LrgChartDataPoint *pt;
            guint pt_col, pt_row;

            pt = lrg_chart_data_series_get_point (series, j);
            pt_col = (guint)(lrg_chart_data_point_get_x (pt) + 0.5);
            pt_row = (guint)(lrg_chart_data_point_get_y (pt) + 0.5);

            if (pt_col == col && pt_row == row)
                return lrg_chart_data_point_get_z (pt);  /* Z is the value */
        }
    }

    return G_MAXDOUBLE;  /* No data for this cell */
}

/*
 * Get brightness of a color (0.0 to 1.0) for contrast calculation.
 */
static gdouble
get_color_brightness (GrlColor *color)
{
    guint8 r, g, b;

    r = grl_color_get_r (color);
    g = grl_color_get_g (color);
    b = grl_color_get_b (color);

    /* Perceived brightness formula */
    return (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;
}

/*
 * Draw the color scale legend bar.
 */
static void
draw_color_scale_legend (LrgHeatmapChart2D *self,
                         gfloat             x,
                         gfloat             y,
                         gfloat             height)
{
    gfloat scale_height;
    gint steps;
    gint i;
    gdouble min_val, max_val;
    gchar value_str[32];
    g_autoptr(GrlColor) text_color = NULL;

    if (self->color_scale == NULL)
        return;

    text_color = grl_color_new (200, 200, 200, 255);
    scale_height = height - 40.0f;  /* Space for labels */
    steps = (gint)(scale_height / 2.0f);
    if (steps < 10)
        steps = 10;

    min_val = lrg_chart_color_scale_get_min_value (self->color_scale);
    max_val = lrg_chart_color_scale_get_max_value (self->color_scale);

    /* Draw gradient bar */
    for (i = 0; i < steps; i++)
    {
        gdouble t;
        g_autoptr(GrlColor) color = NULL;
        gfloat seg_y;
        gfloat seg_h;

        t = (gdouble)i / (gdouble)(steps - 1);
        color = lrg_chart_color_scale_get_color_at (self->color_scale, 1.0 - t);

        seg_y = y + 20.0f + (t * scale_height);
        seg_h = scale_height / (gfloat)steps + 1.0f;

        grl_draw_rectangle (x, seg_y, self->scale_width, seg_h, color);
    }

    /* Draw min/max labels */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    g_snprintf (value_str, sizeof (value_str), self->value_format, max_val);
#pragma GCC diagnostic pop
    grl_draw_text (value_str, (gint)(x + self->scale_width + 5.0f),
                   (gint)(y + 20.0f), 10, text_color);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    g_snprintf (value_str, sizeof (value_str), self->value_format, min_val);
#pragma GCC diagnostic pop
    grl_draw_text (value_str, (gint)(x + self->scale_width + 5.0f),
                   (gint)(y + 20.0f + scale_height - 10.0f), 10, text_color);
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

static void
lrg_heatmap_chart2d_draw_data (LrgChart2D *chart2d)
{
    LrgHeatmapChart2D *self = LRG_HEATMAP_CHART2D (chart2d);
    LrgChart *chart = LRG_CHART (self);
    GrlRectangle bounds;
    gfloat plot_x, plot_y, plot_w, plot_h;
    gfloat cell_w, cell_h;
    guint row, col;
    gfloat label_space;
    gfloat scale_space;

    lrg_chart_get_content_bounds (chart, &bounds);
    plot_x = bounds.x;
    plot_y = bounds.y;
    plot_w = bounds.width;
    plot_h = bounds.height;
    calculate_grid_size (self);

    if (self->num_rows == 0 || self->num_cols == 0)
        return;

    /* Adjust for labels */
    label_space = 0.0f;
    if (self->row_labels != NULL && self->row_labels->len > 0)
        label_space = 60.0f;

    /* Adjust for color scale legend */
    scale_space = 0.0f;
    if (self->show_scale)
        scale_space = self->scale_width + 50.0f;

    /* Calculate cell size */
    cell_w = (plot_w - label_space - scale_space -
              (self->num_cols - 1) * self->cell_spacing) / (gfloat)self->num_cols;
    cell_h = (plot_h - (self->num_rows - 1) * self->cell_spacing) / (gfloat)self->num_rows;

    /* Draw cells */
    for (row = 0; row < self->num_rows; row++)
    {
        for (col = 0; col < self->num_cols; col++)
        {
            gdouble value;
            gfloat cx, cy;

            value = get_cell_value (self, col, row);

            cx = plot_x + label_space +
                 col * (cell_w + self->cell_spacing);
            cy = plot_y + row * (cell_h + self->cell_spacing);

            /* Draw cell background */
            if (value != G_MAXDOUBLE && self->color_scale != NULL)
            {
                g_autoptr(GrlColor) color = NULL;
                GrlRectangle cell_rect;

                color = lrg_chart_color_scale_get_color (self->color_scale, value);

                if (self->cell_radius > 0.0f)
                {
                    cell_rect.x = cx;
                    cell_rect.y = cy;
                    cell_rect.width = cell_w;
                    cell_rect.height = cell_h;
                    grl_draw_rectangle_rounded (&cell_rect,
                        self->cell_radius, 0, color);
                }
                else
                {
                    grl_draw_rectangle (cx, cy, cell_w, cell_h, color);
                }

                /* Draw value text */
                if (self->show_values)
                {
                    gchar value_str[32];
                    gint text_w;
                    gfloat text_x, text_y;
                    g_autoptr(GrlColor) text_color = NULL;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
                    g_snprintf (value_str, sizeof (value_str),
                                self->value_format, value);
#pragma GCC diagnostic pop
                    text_w = grl_measure_text (value_str,
                                               (gint)self->value_font_size);

                    text_x = cx + (cell_w - text_w) / 2.0f;
                    text_y = cy + (cell_h - self->value_font_size) / 2.0f;

                    /* Use contrasting text color */
                    if (get_color_brightness (color) > 0.5)
                        text_color = grl_color_new (0, 0, 0, 255);
                    else
                        text_color = grl_color_new (255, 255, 255, 255);

                    grl_draw_text (value_str, (gint)text_x, (gint)text_y,
                                   (gint)self->value_font_size, text_color);
                }
            }
            else
            {
                /* Draw empty cell */
                g_autoptr(GrlColor) empty_color = NULL;

                empty_color = grl_color_new (40, 40, 40, 255);
                grl_draw_rectangle (cx, cy, cell_w, cell_h, empty_color);
            }
        }
    }

    /* Draw gridlines */
    if (self->show_grid)
    {
        for (row = 0; row <= self->num_rows; row++)
        {
            gfloat y_pos;

            y_pos = plot_y + row * (cell_h + self->cell_spacing) -
                    self->cell_spacing / 2.0f;
            grl_draw_line_ex (
                &(GrlVector2){ plot_x + label_space, y_pos },
                &(GrlVector2){ plot_x + label_space +
                              self->num_cols * (cell_w + self->cell_spacing), y_pos },
                1.0f, self->grid_color);
        }

        for (col = 0; col <= self->num_cols; col++)
        {
            gfloat x_pos;

            x_pos = plot_x + label_space +
                    col * (cell_w + self->cell_spacing) -
                    self->cell_spacing / 2.0f;
            grl_draw_line_ex (
                &(GrlVector2){ x_pos, plot_y },
                &(GrlVector2){ x_pos, plot_y +
                              self->num_rows * (cell_h + self->cell_spacing) },
                1.0f, self->grid_color);
        }
    }

    /* Draw row labels */
    if (self->row_labels != NULL)
    {
        g_autoptr(GrlColor) label_color = NULL;

        label_color = grl_color_new (200, 200, 200, 255);

        for (row = 0; row < self->num_rows && row < self->row_labels->len; row++)
        {
            const gchar *label;
            gfloat ly;

            label = g_ptr_array_index (self->row_labels, row);
            if (label != NULL)
            {
                ly = plot_y + row * (cell_h + self->cell_spacing) +
                     (cell_h - 10.0f) / 2.0f;
                grl_draw_text (label, (gint)(plot_x + 5.0f), (gint)ly,
                               10, label_color);
            }
        }
    }

    /* Draw column labels */
    if (self->col_labels != NULL)
    {
        g_autoptr(GrlColor) label_color = NULL;

        label_color = grl_color_new (200, 200, 200, 255);

        for (col = 0; col < self->num_cols && col < self->col_labels->len; col++)
        {
            const gchar *label;
            gfloat lx;
            gint text_w;

            label = g_ptr_array_index (self->col_labels, col);
            if (label != NULL)
            {
                text_w = grl_measure_text (label, 10);
                lx = plot_x + label_space +
                     col * (cell_w + self->cell_spacing) +
                     (cell_w - text_w) / 2.0f;
                grl_draw_text (label, (gint)lx, (gint)(plot_y + plot_h + 5.0f),
                               10, label_color);
            }
        }
    }

    /* Draw color scale legend */
    if (self->show_scale)
    {
        gfloat scale_x;

        scale_x = plot_x + plot_w - scale_space + 10.0f;
        draw_color_scale_legend (self, scale_x, plot_y, plot_h);
    }
}

static void
lrg_heatmap_chart2d_draw_axes (LrgChart2D *chart2d)
{
    /* Heatmap doesn't use traditional axes */
    (void)chart2d;
}

static void
lrg_heatmap_chart2d_draw_grid (LrgChart2D *chart2d)
{
    /* Grid is drawn as part of draw_data */
    (void)chart2d;
}

static gboolean
lrg_heatmap_chart2d_hit_test (LrgChart        *chart,
                              gfloat           x,
                              gfloat           y,
                              LrgChartHitInfo *out_hit)
{
    LrgHeatmapChart2D *self = LRG_HEATMAP_CHART2D (chart);
    GPtrArray *series_list;
    GrlRectangle bounds;
    gfloat plot_x, plot_y, plot_w, plot_h;
    gfloat cell_w, cell_h;
    gfloat label_space;
    gfloat scale_space;
    gint hit_col, hit_row;
    guint i, j;

    lrg_chart_get_content_bounds (chart, &bounds);
    plot_x = bounds.x;
    plot_y = bounds.y;
    plot_w = bounds.width;
    plot_h = bounds.height;
    calculate_grid_size (self);

    if (self->num_rows == 0 || self->num_cols == 0)
        return FALSE;

    /* Adjust for labels and scale */
    label_space = 0.0f;
    if (self->row_labels != NULL && self->row_labels->len > 0)
        label_space = 60.0f;

    scale_space = 0.0f;
    if (self->show_scale)
        scale_space = self->scale_width + 50.0f;

    /* Calculate cell size */
    cell_w = (plot_w - label_space - scale_space -
              (self->num_cols - 1) * self->cell_spacing) / (gfloat)self->num_cols;
    cell_h = (plot_h - (self->num_rows - 1) * self->cell_spacing) / (gfloat)self->num_rows;

    /* Determine which cell was hit */
    hit_col = (gint)((x - plot_x - label_space) / (cell_w + self->cell_spacing));
    hit_row = (gint)((y - plot_y) / (cell_h + self->cell_spacing));

    if (hit_col < 0 || hit_col >= (gint)self->num_cols ||
        hit_row < 0 || hit_row >= (gint)self->num_rows)
        return FALSE;

    /* Find the data point for this cell */
    series_list = lrg_chart_get_series_list (chart);
    if (series_list == NULL)
        return FALSE;

    for (i = 0; i < series_list->len; i++)
    {
        LrgChartDataSeries *series;
        guint point_count;

        series = g_ptr_array_index (series_list, i);
        point_count = lrg_chart_data_series_get_point_count (series);

        for (j = 0; j < point_count; j++)
        {
            const LrgChartDataPoint *pt;
            guint pt_col, pt_row;

            pt = lrg_chart_data_series_get_point (series, j);
            pt_col = (guint)(lrg_chart_data_point_get_x (pt) + 0.5);
            pt_row = (guint)(lrg_chart_data_point_get_y (pt) + 0.5);

            if (pt_col == (guint)hit_col && pt_row == (guint)hit_row)
            {
                gfloat cx, cy;
                GrlRectangle hit_bounds;

                cx = plot_x + label_space +
                     hit_col * (cell_w + self->cell_spacing);
                cy = plot_y + hit_row * (cell_h + self->cell_spacing);

                lrg_chart_hit_info_set_series_index (out_hit, (gint)i);
                lrg_chart_hit_info_set_point_index (out_hit, (gint)j);
                lrg_chart_hit_info_set_screen_x (out_hit, cx + cell_w / 2.0f);
                lrg_chart_hit_info_set_screen_y (out_hit, cy + cell_h / 2.0f);

                hit_bounds.x = cx;
                hit_bounds.y = cy;
                hit_bounds.width = cell_w;
                hit_bounds.height = cell_h;
                lrg_chart_hit_info_set_bounds (out_hit, &hit_bounds);

                return TRUE;
            }
        }
    }

    return FALSE;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_heatmap_chart2d_finalize (GObject *object)
{
    LrgHeatmapChart2D *self = LRG_HEATMAP_CHART2D (object);

    g_clear_object (&self->color_scale);
    g_clear_pointer (&self->grid_color, grl_color_free);
    g_clear_pointer (&self->value_format, g_free);
    g_clear_pointer (&self->row_labels, g_ptr_array_unref);
    g_clear_pointer (&self->col_labels, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_heatmap_chart2d_parent_class)->finalize (object);
}

static void
lrg_heatmap_chart2d_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgHeatmapChart2D *self = LRG_HEATMAP_CHART2D (object);

    switch (prop_id)
    {
    case PROP_COLOR_SCALE:
        g_value_set_object (value, self->color_scale);
        break;
    case PROP_CELL_SPACING:
        g_value_set_float (value, self->cell_spacing);
        break;
    case PROP_CELL_RADIUS:
        g_value_set_float (value, self->cell_radius);
        break;
    case PROP_SHOW_GRID:
        g_value_set_boolean (value, self->show_grid);
        break;
    case PROP_GRID_COLOR:
        g_value_set_boxed (value, self->grid_color);
        break;
    case PROP_SHOW_VALUES:
        g_value_set_boolean (value, self->show_values);
        break;
    case PROP_VALUE_FORMAT:
        g_value_set_string (value, self->value_format);
        break;
    case PROP_VALUE_FONT_SIZE:
        g_value_set_float (value, self->value_font_size);
        break;
    case PROP_SHOW_SCALE:
        g_value_set_boolean (value, self->show_scale);
        break;
    case PROP_SCALE_WIDTH:
        g_value_set_float (value, self->scale_width);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_heatmap_chart2d_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgHeatmapChart2D *self = LRG_HEATMAP_CHART2D (object);

    switch (prop_id)
    {
    case PROP_COLOR_SCALE:
        lrg_heatmap_chart2d_set_color_scale (self, g_value_get_object (value));
        break;
    case PROP_CELL_SPACING:
        lrg_heatmap_chart2d_set_cell_spacing (self, g_value_get_float (value));
        break;
    case PROP_CELL_RADIUS:
        lrg_heatmap_chart2d_set_cell_radius (self, g_value_get_float (value));
        break;
    case PROP_SHOW_GRID:
        lrg_heatmap_chart2d_set_show_grid (self, g_value_get_boolean (value));
        break;
    case PROP_GRID_COLOR:
        lrg_heatmap_chart2d_set_grid_color (self, g_value_get_boxed (value));
        break;
    case PROP_SHOW_VALUES:
        lrg_heatmap_chart2d_set_show_values (self, g_value_get_boolean (value));
        break;
    case PROP_VALUE_FORMAT:
        lrg_heatmap_chart2d_set_value_format (self, g_value_get_string (value));
        break;
    case PROP_VALUE_FONT_SIZE:
        lrg_heatmap_chart2d_set_value_font_size (self, g_value_get_float (value));
        break;
    case PROP_SHOW_SCALE:
        lrg_heatmap_chart2d_set_show_scale (self, g_value_get_boolean (value));
        break;
    case PROP_SCALE_WIDTH:
        lrg_heatmap_chart2d_set_scale_width (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_heatmap_chart2d_class_init (LrgHeatmapChart2DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgChartClass *chart_class = LRG_CHART_CLASS (klass);
    LrgChart2DClass *chart2d_class = LRG_CHART2D_CLASS (klass);

    object_class->finalize = lrg_heatmap_chart2d_finalize;
    object_class->get_property = lrg_heatmap_chart2d_get_property;
    object_class->set_property = lrg_heatmap_chart2d_set_property;

    chart_class->hit_test = lrg_heatmap_chart2d_hit_test;

    chart2d_class->draw_data = lrg_heatmap_chart2d_draw_data;
    chart2d_class->draw_axes = lrg_heatmap_chart2d_draw_axes;
    chart2d_class->draw_grid = lrg_heatmap_chart2d_draw_grid;

    /**
     * LrgHeatmapChart2D:color-scale:
     *
     * The color scale used for value-to-color mapping.
     */
    properties[PROP_COLOR_SCALE] =
        g_param_spec_object ("color-scale",
                             "Color Scale",
                             "Color scale for value mapping",
                             LRG_TYPE_CHART_COLOR_SCALE,
                             G_PARAM_READWRITE |
                             G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgHeatmapChart2D:cell-spacing:
     *
     * Spacing between cells in pixels.
     */
    properties[PROP_CELL_SPACING] =
        g_param_spec_float ("cell-spacing",
                            "Cell Spacing",
                            "Spacing between cells",
                            0.0f, 50.0f, 2.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgHeatmapChart2D:cell-radius:
     *
     * Corner radius for rounded cells.
     */
    properties[PROP_CELL_RADIUS] =
        g_param_spec_float ("cell-radius",
                            "Cell Radius",
                            "Corner radius for cells",
                            0.0f, 50.0f, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgHeatmapChart2D:show-grid:
     *
     * Whether to display gridlines.
     */
    properties[PROP_SHOW_GRID] =
        g_param_spec_boolean ("show-grid",
                              "Show Grid",
                              "Display gridlines",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgHeatmapChart2D:grid-color:
     *
     * Color for gridlines.
     */
    properties[PROP_GRID_COLOR] =
        g_param_spec_boxed ("grid-color",
                            "Grid Color",
                            "Color for gridlines",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgHeatmapChart2D:show-values:
     *
     * Whether to display values in cells.
     */
    properties[PROP_SHOW_VALUES] =
        g_param_spec_boolean ("show-values",
                              "Show Values",
                              "Display values in cells",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgHeatmapChart2D:value-format:
     *
     * Printf format for cell values.
     */
    properties[PROP_VALUE_FORMAT] =
        g_param_spec_string ("value-format",
                             "Value Format",
                             "Printf format for values",
                             "%.1f",
                             G_PARAM_READWRITE |
                             G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgHeatmapChart2D:value-font-size:
     *
     * Font size for cell values.
     */
    properties[PROP_VALUE_FONT_SIZE] =
        g_param_spec_float ("value-font-size",
                            "Value Font Size",
                            "Font size for cell values",
                            4.0f, 48.0f, 10.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgHeatmapChart2D:show-scale:
     *
     * Whether to display the color scale legend.
     */
    properties[PROP_SHOW_SCALE] =
        g_param_spec_boolean ("show-scale",
                              "Show Scale",
                              "Display color scale legend",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgHeatmapChart2D:scale-width:
     *
     * Width of the color scale legend.
     */
    properties[PROP_SCALE_WIDTH] =
        g_param_spec_float ("scale-width",
                            "Scale Width",
                            "Width of color scale legend",
                            10.0f, 100.0f, 20.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_heatmap_chart2d_init (LrgHeatmapChart2D *self)
{
    self->color_scale = lrg_chart_color_scale_new_heat ();
    self->cell_spacing = 2.0f;
    self->cell_radius = 0.0f;
    self->show_grid = FALSE;
    self->grid_color = grl_color_new (80, 80, 80, 255);
    self->show_values = FALSE;
    self->value_format = g_strdup ("%.1f");
    self->value_font_size = 10.0f;
    self->row_labels = NULL;
    self->col_labels = NULL;
    self->show_scale = TRUE;
    self->scale_width = 20.0f;
    self->num_rows = 0;
    self->num_cols = 0;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgHeatmapChart2D *
lrg_heatmap_chart2d_new (void)
{
    return g_object_new (LRG_TYPE_HEATMAP_CHART2D, NULL);
}

LrgHeatmapChart2D *
lrg_heatmap_chart2d_new_with_size (gfloat width,
                                   gfloat height)
{
    return g_object_new (LRG_TYPE_HEATMAP_CHART2D,
                         "width", width,
                         "height", height,
                         NULL);
}

LrgChartColorScale *
lrg_heatmap_chart2d_get_color_scale (LrgHeatmapChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HEATMAP_CHART2D (self), NULL);

    return self->color_scale;
}

void
lrg_heatmap_chart2d_set_color_scale (LrgHeatmapChart2D  *self,
                                     LrgChartColorScale *scale)
{
    g_return_if_fail (LRG_IS_HEATMAP_CHART2D (self));

    if (g_set_object (&self->color_scale, scale))
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR_SCALE]);
}

gfloat
lrg_heatmap_chart2d_get_cell_spacing (LrgHeatmapChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HEATMAP_CHART2D (self), 0.0f);

    return self->cell_spacing;
}

void
lrg_heatmap_chart2d_set_cell_spacing (LrgHeatmapChart2D *self,
                                      gfloat             spacing)
{
    g_return_if_fail (LRG_IS_HEATMAP_CHART2D (self));

    if (self->cell_spacing != spacing)
    {
        self->cell_spacing = spacing;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CELL_SPACING]);
    }
}

gfloat
lrg_heatmap_chart2d_get_cell_radius (LrgHeatmapChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HEATMAP_CHART2D (self), 0.0f);

    return self->cell_radius;
}

void
lrg_heatmap_chart2d_set_cell_radius (LrgHeatmapChart2D *self,
                                     gfloat             radius)
{
    g_return_if_fail (LRG_IS_HEATMAP_CHART2D (self));

    if (self->cell_radius != radius)
    {
        self->cell_radius = radius;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CELL_RADIUS]);
    }
}

gboolean
lrg_heatmap_chart2d_get_show_grid (LrgHeatmapChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HEATMAP_CHART2D (self), FALSE);

    return self->show_grid;
}

void
lrg_heatmap_chart2d_set_show_grid (LrgHeatmapChart2D *self,
                                   gboolean           show)
{
    g_return_if_fail (LRG_IS_HEATMAP_CHART2D (self));

    show = !!show;
    if (self->show_grid != show)
    {
        self->show_grid = show;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_GRID]);
    }
}

GrlColor *
lrg_heatmap_chart2d_get_grid_color (LrgHeatmapChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HEATMAP_CHART2D (self), NULL);

    return self->grid_color;
}

void
lrg_heatmap_chart2d_set_grid_color (LrgHeatmapChart2D *self,
                                    GrlColor          *color)
{
    g_return_if_fail (LRG_IS_HEATMAP_CHART2D (self));

    g_clear_pointer (&self->grid_color, grl_color_free);
    if (color != NULL)
        self->grid_color = grl_color_copy (color);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GRID_COLOR]);
}

gboolean
lrg_heatmap_chart2d_get_show_values (LrgHeatmapChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HEATMAP_CHART2D (self), FALSE);

    return self->show_values;
}

void
lrg_heatmap_chart2d_set_show_values (LrgHeatmapChart2D *self,
                                     gboolean           show)
{
    g_return_if_fail (LRG_IS_HEATMAP_CHART2D (self));

    show = !!show;
    if (self->show_values != show)
    {
        self->show_values = show;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_VALUES]);
    }
}

const gchar *
lrg_heatmap_chart2d_get_value_format (LrgHeatmapChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HEATMAP_CHART2D (self), NULL);

    return self->value_format;
}

void
lrg_heatmap_chart2d_set_value_format (LrgHeatmapChart2D *self,
                                      const gchar       *format)
{
    g_return_if_fail (LRG_IS_HEATMAP_CHART2D (self));

    if (g_strcmp0 (self->value_format, format) != 0)
    {
        g_free (self->value_format);
        self->value_format = g_strdup (format ? format : "%.1f");
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VALUE_FORMAT]);
    }
}

gfloat
lrg_heatmap_chart2d_get_value_font_size (LrgHeatmapChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HEATMAP_CHART2D (self), 10.0f);

    return self->value_font_size;
}

void
lrg_heatmap_chart2d_set_value_font_size (LrgHeatmapChart2D *self,
                                         gfloat             size)
{
    g_return_if_fail (LRG_IS_HEATMAP_CHART2D (self));

    if (self->value_font_size != size)
    {
        self->value_font_size = size;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VALUE_FONT_SIZE]);
    }
}

void
lrg_heatmap_chart2d_set_row_labels (LrgHeatmapChart2D  *self,
                                    const gchar *const *labels)
{
    guint i;

    g_return_if_fail (LRG_IS_HEATMAP_CHART2D (self));

    g_clear_pointer (&self->row_labels, g_ptr_array_unref);

    if (labels != NULL)
    {
        self->row_labels = g_ptr_array_new_with_free_func (g_free);
        for (i = 0; labels[i] != NULL; i++)
            g_ptr_array_add (self->row_labels, g_strdup (labels[i]));
    }
}

void
lrg_heatmap_chart2d_set_col_labels (LrgHeatmapChart2D  *self,
                                    const gchar *const *labels)
{
    guint i;

    g_return_if_fail (LRG_IS_HEATMAP_CHART2D (self));

    g_clear_pointer (&self->col_labels, g_ptr_array_unref);

    if (labels != NULL)
    {
        self->col_labels = g_ptr_array_new_with_free_func (g_free);
        for (i = 0; labels[i] != NULL; i++)
            g_ptr_array_add (self->col_labels, g_strdup (labels[i]));
    }
}

const gchar *
lrg_heatmap_chart2d_get_row_label (LrgHeatmapChart2D *self,
                                   guint              row)
{
    g_return_val_if_fail (LRG_IS_HEATMAP_CHART2D (self), NULL);

    if (self->row_labels == NULL || row >= self->row_labels->len)
        return NULL;

    return g_ptr_array_index (self->row_labels, row);
}

const gchar *
lrg_heatmap_chart2d_get_col_label (LrgHeatmapChart2D *self,
                                   guint              col)
{
    g_return_val_if_fail (LRG_IS_HEATMAP_CHART2D (self), NULL);

    if (self->col_labels == NULL || col >= self->col_labels->len)
        return NULL;

    return g_ptr_array_index (self->col_labels, col);
}

gboolean
lrg_heatmap_chart2d_get_show_scale (LrgHeatmapChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HEATMAP_CHART2D (self), FALSE);

    return self->show_scale;
}

void
lrg_heatmap_chart2d_set_show_scale (LrgHeatmapChart2D *self,
                                    gboolean           show)
{
    g_return_if_fail (LRG_IS_HEATMAP_CHART2D (self));

    show = !!show;
    if (self->show_scale != show)
    {
        self->show_scale = show;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_SCALE]);
    }
}

gfloat
lrg_heatmap_chart2d_get_scale_width (LrgHeatmapChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HEATMAP_CHART2D (self), 20.0f);

    return self->scale_width;
}

void
lrg_heatmap_chart2d_set_scale_width (LrgHeatmapChart2D *self,
                                     gfloat             width)
{
    g_return_if_fail (LRG_IS_HEATMAP_CHART2D (self));

    if (self->scale_width != width)
    {
        self->scale_width = width;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCALE_WIDTH]);
    }
}

void
lrg_heatmap_chart2d_auto_range (LrgHeatmapChart2D *self)
{
    LrgChart *chart;
    GPtrArray *series_list;
    gdouble min_val, max_val;
    guint i, j;
    gboolean found;

    g_return_if_fail (LRG_IS_HEATMAP_CHART2D (self));

    if (self->color_scale == NULL)
        return;

    chart = LRG_CHART (self);
    series_list = lrg_chart_get_series_list (chart);
    if (series_list == NULL)
        return;

    min_val = G_MAXDOUBLE;
    max_val = -G_MAXDOUBLE;
    found = FALSE;

    for (i = 0; i < series_list->len; i++)
    {
        LrgChartDataSeries *series;
        guint point_count;

        series = g_ptr_array_index (series_list, i);
        point_count = lrg_chart_data_series_get_point_count (series);

        for (j = 0; j < point_count; j++)
        {
            const LrgChartDataPoint *pt;

            pt = lrg_chart_data_series_get_point (series, j);

            if (lrg_chart_data_point_get_z (pt) < min_val)
                min_val = lrg_chart_data_point_get_z (pt);
            if (lrg_chart_data_point_get_z (pt) > max_val)
                max_val = lrg_chart_data_point_get_z (pt);
            found = TRUE;
        }
    }

    if (found)
        lrg_chart_color_scale_set_range (self->color_scale, min_val, max_val);
}
