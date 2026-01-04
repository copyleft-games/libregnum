/* lrg-histogram-chart2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-histogram-chart2d.h"
#include "lrg-chart-data-series.h"
#include "lrg-chart-data-point.h"
#include "lrg-chart-hit-info.h"
#include "../lrg-log.h"
#include <math.h>

/*
 * Bin structure for histogram computation.
 */
typedef struct
{
    gdouble min_val;
    gdouble max_val;
    guint   count;
    gdouble density;
    gdouble cumulative;
} HistogramBin;

struct _LrgHistogramChart2D
{
    LrgChart2D parent_instance;

    /* Binning configuration */
    guint   bin_count;      /* 0 = auto */
    gdouble bin_width;      /* 0 = auto */
    gdouble range_min;      /* -G_MAXDOUBLE = auto */
    gdouble range_max;      /* G_MAXDOUBLE = auto */

    /* Display mode */
    gboolean density;       /* Show density vs frequency */
    gboolean cumulative;    /* Show cumulative distribution */

    /* Style */
    GrlColor *bar_color;
    GrlColor *border_color;
    gfloat    border_width;
    gfloat    bar_spacing;

    /* Cumulative line */
    gboolean  show_cumulative_line;
    GrlColor *cumulative_line_color;
    gfloat    cumulative_line_width;

    /* Computed bins */
    GArray   *bins;
    guint     total_count;
    gboolean  needs_recalc;
};

enum
{
    PROP_0,
    PROP_BIN_COUNT,
    PROP_BIN_WIDTH,
    PROP_RANGE_MIN,
    PROP_RANGE_MAX,
    PROP_DENSITY,
    PROP_CUMULATIVE,
    PROP_BAR_COLOR,
    PROP_BORDER_COLOR,
    PROP_BORDER_WIDTH,
    PROP_BAR_SPACING,
    PROP_SHOW_CUMULATIVE_LINE,
    PROP_CUMULATIVE_LINE_COLOR,
    PROP_CUMULATIVE_LINE_WIDTH,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (LrgHistogramChart2D, lrg_histogram_chart2d, LRG_TYPE_CHART2D)

/* ==========================================================================
 * Internal Helpers
 * ========================================================================== */

/*
 * Collect all Y values from data series.
 */
static GArray *
collect_values (LrgHistogramChart2D *self)
{
    LrgChart *chart = LRG_CHART (self);
    GPtrArray *series_list;
    GArray *values;
    guint i, j;

    values = g_array_new (FALSE, FALSE, sizeof (gdouble));

    series_list = lrg_chart_get_series_list (chart);
    if (series_list == NULL)
        return values;

    for (i = 0; i < series_list->len; i++)
    {
        LrgChartDataSeries *series;
        guint point_count;

        series = g_ptr_array_index (series_list, i);
        point_count = lrg_chart_data_series_get_point_count (series);

        for (j = 0; j < point_count; j++)
        {
            const LrgChartDataPoint *pt;
            gdouble val;

            pt = lrg_chart_data_series_get_point (series, j);
            val = lrg_chart_data_point_get_y (pt);  /* Use Y value for histogram */

            if (!isnan (val) && !isinf (val))
                g_array_append_val (values, val);
        }
    }

    return values;
}

/*
 * Calculate optimal bin count using Sturges' formula.
 */
static guint
calculate_auto_bin_count (guint n)
{
    if (n == 0)
        return 1;

    return (guint)(1.0 + 3.322 * log10 ((double)n));
}

/*
 * Recalculate bins from data.
 */
static void
recalculate_bins (LrgHistogramChart2D *self)
{
    GArray *values;
    gdouble data_min, data_max;
    gdouble effective_min, effective_max;
    gdouble effective_bin_width;
    guint effective_bin_count;
    guint i;
    gdouble cumulative_sum;

    /* Clear existing bins */
    if (self->bins != NULL)
        g_array_set_size (self->bins, 0);
    else
        self->bins = g_array_new (FALSE, TRUE, sizeof (HistogramBin));

    self->total_count = 0;
    self->needs_recalc = FALSE;

    /* Collect data values */
    values = collect_values (self);
    if (values->len == 0)
    {
        g_array_unref (values);
        return;
    }

    self->total_count = values->len;

    /* Find data range */
    data_min = G_MAXDOUBLE;
    data_max = -G_MAXDOUBLE;

    for (i = 0; i < values->len; i++)
    {
        gdouble val;

        val = g_array_index (values, gdouble, i);
        if (val < data_min)
            data_min = val;
        if (val > data_max)
            data_max = val;
    }

    /* Determine effective range (sentinel values mean auto) */
    effective_min = (self->range_min <= -G_MAXDOUBLE) ? data_min : self->range_min;
    effective_max = (self->range_max >= G_MAXDOUBLE) ? data_max : self->range_max;

    if (effective_max <= effective_min)
        effective_max = effective_min + 1.0;

    /* Determine bin count and width */
    if (self->bin_width > 0.0)
    {
        /* Fixed bin width */
        effective_bin_width = self->bin_width;
        effective_bin_count = (guint)ceil ((effective_max - effective_min) /
                                           effective_bin_width);
        if (effective_bin_count < 1)
            effective_bin_count = 1;
    }
    else if (self->bin_count > 0)
    {
        /* Fixed bin count */
        effective_bin_count = self->bin_count;
        effective_bin_width = (effective_max - effective_min) /
                              (gdouble)effective_bin_count;
    }
    else
    {
        /* Auto using Sturges' formula */
        effective_bin_count = calculate_auto_bin_count (values->len);
        if (effective_bin_count < 5)
            effective_bin_count = 5;
        effective_bin_width = (effective_max - effective_min) /
                              (gdouble)effective_bin_count;
    }

    /* Initialize bins */
    g_array_set_size (self->bins, effective_bin_count);

    for (i = 0; i < effective_bin_count; i++)
    {
        HistogramBin *bin;

        bin = &g_array_index (self->bins, HistogramBin, i);
        bin->min_val = effective_min + i * effective_bin_width;
        bin->max_val = effective_min + (i + 1) * effective_bin_width;
        bin->count = 0;
        bin->density = 0.0;
        bin->cumulative = 0.0;
    }

    /* Count values in each bin */
    for (i = 0; i < values->len; i++)
    {
        gdouble val;
        guint bin_idx;

        val = g_array_index (values, gdouble, i);

        if (val < effective_min || val > effective_max)
            continue;

        bin_idx = (guint)((val - effective_min) / effective_bin_width);
        if (bin_idx >= effective_bin_count)
            bin_idx = effective_bin_count - 1;

        g_array_index (self->bins, HistogramBin, bin_idx).count++;
    }

    /* Calculate density and cumulative */
    cumulative_sum = 0.0;

    for (i = 0; i < effective_bin_count; i++)
    {
        HistogramBin *bin;

        bin = &g_array_index (self->bins, HistogramBin, i);

        /* Density = count / (total * bin_width) */
        if (self->total_count > 0 && effective_bin_width > 0.0)
        {
            bin->density = (gdouble)bin->count /
                          ((gdouble)self->total_count * effective_bin_width);
        }

        /* Cumulative */
        cumulative_sum += (gdouble)bin->count;
        bin->cumulative = cumulative_sum / (gdouble)self->total_count;
    }

    g_array_unref (values);
}

/*
 * Ensure bins are calculated.
 */
static void
ensure_bins (LrgHistogramChart2D *self)
{
    if (self->needs_recalc || self->bins == NULL || self->bins->len == 0)
        recalculate_bins (self);
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

static void
lrg_histogram_chart2d_draw_data (LrgChart2D *chart2d)
{
    LrgHistogramChart2D *self = LRG_HISTOGRAM_CHART2D (chart2d);
    LrgChart *chart = LRG_CHART (self);
    GrlRectangle bounds;
    gfloat plot_x, plot_y, plot_w, plot_h;
    guint bin_count;
    gdouble max_height;
    gfloat bar_width;
    guint i;

    ensure_bins (self);

    if (self->bins == NULL || self->bins->len == 0)
        return;

    lrg_chart_get_content_bounds (chart, &bounds);
    plot_x = bounds.x;
    plot_y = bounds.y;
    plot_w = bounds.width;
    plot_h = bounds.height;

    bin_count = self->bins->len;

    /* Find maximum height */
    max_height = 0.0;
    for (i = 0; i < bin_count; i++)
    {
        HistogramBin *bin;
        gdouble height;

        bin = &g_array_index (self->bins, HistogramBin, i);

        if (self->cumulative)
            height = bin->cumulative;
        else if (self->density)
            height = bin->density;
        else
            height = (gdouble)bin->count;

        if (height > max_height)
            max_height = height;
    }

    if (max_height <= 0.0)
        max_height = 1.0;

    /* Calculate bar width */
    bar_width = (plot_w / (gfloat)bin_count) * (1.0f - self->bar_spacing);

    /* Draw bars */
    for (i = 0; i < bin_count; i++)
    {
        HistogramBin *bin;
        gdouble height_val;
        gfloat bar_h, bar_x, bar_y;

        bin = &g_array_index (self->bins, HistogramBin, i);

        if (self->cumulative)
            height_val = bin->cumulative;
        else if (self->density)
            height_val = bin->density;
        else
            height_val = (gdouble)bin->count;

        bar_h = (gfloat)(height_val / max_height) * plot_h;
        bar_x = plot_x + (plot_w / (gfloat)bin_count) * i +
                ((plot_w / (gfloat)bin_count) - bar_width) / 2.0f;
        bar_y = plot_y + plot_h - bar_h;

        /* Draw bar fill */
        grl_draw_rectangle (bar_x, bar_y, bar_width, bar_h, self->bar_color);

        /* Draw bar border */
        if (self->border_width > 0.0f)
        {
            GrlRectangle bar_rect;
            bar_rect.x = bar_x;
            bar_rect.y = bar_y;
            bar_rect.width = bar_width;
            bar_rect.height = bar_h;
            grl_draw_rectangle_lines_ex (&bar_rect,
                self->border_width, self->border_color);
        }
    }

    /* Draw cumulative line if enabled */
    if (self->show_cumulative_line && !self->cumulative)
    {
        for (i = 0; i < bin_count - 1; i++)
        {
            HistogramBin *bin1, *bin2;
            gfloat x1, y1, x2, y2;

            bin1 = &g_array_index (self->bins, HistogramBin, i);
            bin2 = &g_array_index (self->bins, HistogramBin, i + 1);

            x1 = plot_x + (plot_w / (gfloat)bin_count) * (i + 0.5f);
            y1 = plot_y + plot_h * (1.0f - (gfloat)bin1->cumulative);

            x2 = plot_x + (plot_w / (gfloat)bin_count) * (i + 1.5f);
            y2 = plot_y + plot_h * (1.0f - (gfloat)bin2->cumulative);

            grl_draw_line_ex (
                &(GrlVector2){ x1, y1 },
                &(GrlVector2){ x2, y2 },
                self->cumulative_line_width,
                self->cumulative_line_color);
        }
    }
}

static gboolean
lrg_histogram_chart2d_hit_test (LrgChart        *chart,
                                gfloat           x,
                                gfloat           y,
                                LrgChartHitInfo *out_hit)
{
    LrgHistogramChart2D *self = LRG_HISTOGRAM_CHART2D (chart);
    GrlRectangle bounds;
    gfloat plot_x, plot_y, plot_w, plot_h;
    guint bin_count;
    gdouble max_height;
    gfloat bar_width;
    guint i;

    ensure_bins (self);

    if (self->bins == NULL || self->bins->len == 0)
        return FALSE;

    lrg_chart_get_content_bounds (chart, &bounds);
    plot_x = bounds.x;
    plot_y = bounds.y;
    plot_w = bounds.width;
    plot_h = bounds.height;

    bin_count = self->bins->len;

    /* Find maximum height */
    max_height = 0.0;
    for (i = 0; i < bin_count; i++)
    {
        HistogramBin *bin;
        gdouble height;

        bin = &g_array_index (self->bins, HistogramBin, i);

        if (self->cumulative)
            height = bin->cumulative;
        else if (self->density)
            height = bin->density;
        else
            height = (gdouble)bin->count;

        if (height > max_height)
            max_height = height;
    }

    if (max_height <= 0.0)
        return FALSE;

    bar_width = (plot_w / (gfloat)bin_count) * (1.0f - self->bar_spacing);

    /* Check each bar */
    for (i = 0; i < bin_count; i++)
    {
        HistogramBin *bin;
        gdouble height_val;
        gfloat bar_h, bar_x, bar_y;

        bin = &g_array_index (self->bins, HistogramBin, i);

        if (self->cumulative)
            height_val = bin->cumulative;
        else if (self->density)
            height_val = bin->density;
        else
            height_val = (gdouble)bin->count;

        bar_h = (gfloat)(height_val / max_height) * plot_h;
        bar_x = plot_x + (plot_w / (gfloat)bin_count) * i +
                ((plot_w / (gfloat)bin_count) - bar_width) / 2.0f;
        bar_y = plot_y + plot_h - bar_h;

        if (x >= bar_x && x <= bar_x + bar_width &&
            y >= bar_y && y <= bar_y + bar_h)
        {
            GrlRectangle hit_bounds;

            lrg_chart_hit_info_set_series_index (out_hit, 0);
            lrg_chart_hit_info_set_point_index (out_hit, (gint)i);
            lrg_chart_hit_info_set_screen_x (out_hit, bar_x + bar_width / 2.0f);
            lrg_chart_hit_info_set_screen_y (out_hit, bar_y);

            hit_bounds.x = bar_x;
            hit_bounds.y = bar_y;
            hit_bounds.width = bar_width;
            hit_bounds.height = bar_h;
            lrg_chart_hit_info_set_bounds (out_hit, &hit_bounds);

            return TRUE;
        }
    }

    return FALSE;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_histogram_chart2d_finalize (GObject *object)
{
    LrgHistogramChart2D *self = LRG_HISTOGRAM_CHART2D (object);

    g_clear_pointer (&self->bar_color, grl_color_free);
    g_clear_pointer (&self->border_color, grl_color_free);
    g_clear_pointer (&self->cumulative_line_color, grl_color_free);
    g_clear_pointer (&self->bins, g_array_unref);

    G_OBJECT_CLASS (lrg_histogram_chart2d_parent_class)->finalize (object);
}

static void
lrg_histogram_chart2d_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    LrgHistogramChart2D *self = LRG_HISTOGRAM_CHART2D (object);

    switch (prop_id)
    {
    case PROP_BIN_COUNT:
        g_value_set_uint (value, self->bin_count);
        break;
    case PROP_BIN_WIDTH:
        g_value_set_double (value, self->bin_width);
        break;
    case PROP_RANGE_MIN:
        g_value_set_double (value, self->range_min);
        break;
    case PROP_RANGE_MAX:
        g_value_set_double (value, self->range_max);
        break;
    case PROP_DENSITY:
        g_value_set_boolean (value, self->density);
        break;
    case PROP_CUMULATIVE:
        g_value_set_boolean (value, self->cumulative);
        break;
    case PROP_BAR_COLOR:
        g_value_set_boxed (value, self->bar_color);
        break;
    case PROP_BORDER_COLOR:
        g_value_set_boxed (value, self->border_color);
        break;
    case PROP_BORDER_WIDTH:
        g_value_set_float (value, self->border_width);
        break;
    case PROP_BAR_SPACING:
        g_value_set_float (value, self->bar_spacing);
        break;
    case PROP_SHOW_CUMULATIVE_LINE:
        g_value_set_boolean (value, self->show_cumulative_line);
        break;
    case PROP_CUMULATIVE_LINE_COLOR:
        g_value_set_boxed (value, self->cumulative_line_color);
        break;
    case PROP_CUMULATIVE_LINE_WIDTH:
        g_value_set_float (value, self->cumulative_line_width);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_histogram_chart2d_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
    LrgHistogramChart2D *self = LRG_HISTOGRAM_CHART2D (object);

    switch (prop_id)
    {
    case PROP_BIN_COUNT:
        lrg_histogram_chart2d_set_bin_count (self, g_value_get_uint (value));
        break;
    case PROP_BIN_WIDTH:
        lrg_histogram_chart2d_set_bin_width (self, g_value_get_double (value));
        break;
    case PROP_RANGE_MIN:
        lrg_histogram_chart2d_set_range_min (self, g_value_get_double (value));
        break;
    case PROP_RANGE_MAX:
        lrg_histogram_chart2d_set_range_max (self, g_value_get_double (value));
        break;
    case PROP_DENSITY:
        lrg_histogram_chart2d_set_density (self, g_value_get_boolean (value));
        break;
    case PROP_CUMULATIVE:
        lrg_histogram_chart2d_set_cumulative (self, g_value_get_boolean (value));
        break;
    case PROP_BAR_COLOR:
        lrg_histogram_chart2d_set_bar_color (self, g_value_get_boxed (value));
        break;
    case PROP_BORDER_COLOR:
        lrg_histogram_chart2d_set_border_color (self, g_value_get_boxed (value));
        break;
    case PROP_BORDER_WIDTH:
        lrg_histogram_chart2d_set_border_width (self, g_value_get_float (value));
        break;
    case PROP_BAR_SPACING:
        lrg_histogram_chart2d_set_bar_spacing (self, g_value_get_float (value));
        break;
    case PROP_SHOW_CUMULATIVE_LINE:
        lrg_histogram_chart2d_set_show_cumulative_line (self, g_value_get_boolean (value));
        break;
    case PROP_CUMULATIVE_LINE_COLOR:
        lrg_histogram_chart2d_set_cumulative_line_color (self, g_value_get_boxed (value));
        break;
    case PROP_CUMULATIVE_LINE_WIDTH:
        lrg_histogram_chart2d_set_cumulative_line_width (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_histogram_chart2d_class_init (LrgHistogramChart2DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgChartClass *chart_class = LRG_CHART_CLASS (klass);
    LrgChart2DClass *chart2d_class = LRG_CHART2D_CLASS (klass);

    object_class->finalize = lrg_histogram_chart2d_finalize;
    object_class->get_property = lrg_histogram_chart2d_get_property;
    object_class->set_property = lrg_histogram_chart2d_set_property;

    chart_class->hit_test = lrg_histogram_chart2d_hit_test;
    chart2d_class->draw_data = lrg_histogram_chart2d_draw_data;

    /**
     * LrgHistogramChart2D:bin-count:
     *
     * Number of bins (0 for auto).
     */
    properties[PROP_BIN_COUNT] =
        g_param_spec_uint ("bin-count",
                           "Bin Count",
                           "Number of histogram bins",
                           0, 1000, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgHistogramChart2D:bin-width:
     *
     * Fixed bin width (0 for auto).
     */
    properties[PROP_BIN_WIDTH] =
        g_param_spec_double ("bin-width",
                             "Bin Width",
                             "Fixed bin width",
                             0.0, G_MAXDOUBLE, 0.0,
                             G_PARAM_READWRITE |
                             G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgHistogramChart2D:range-min:
     *
     * Minimum value for binning range (-G_MAXDOUBLE for auto).
     */
    properties[PROP_RANGE_MIN] =
        g_param_spec_double ("range-min",
                             "Range Min",
                             "Minimum value for binning (-G_MAXDOUBLE for auto)",
                             -G_MAXDOUBLE, G_MAXDOUBLE, -G_MAXDOUBLE,
                             G_PARAM_READWRITE |
                             G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgHistogramChart2D:range-max:
     *
     * Maximum value for binning range (G_MAXDOUBLE for auto).
     */
    properties[PROP_RANGE_MAX] =
        g_param_spec_double ("range-max",
                             "Range Max",
                             "Maximum value for binning (G_MAXDOUBLE for auto)",
                             -G_MAXDOUBLE, G_MAXDOUBLE, G_MAXDOUBLE,
                             G_PARAM_READWRITE |
                             G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgHistogramChart2D:density:
     *
     * Whether to show probability density.
     */
    properties[PROP_DENSITY] =
        g_param_spec_boolean ("density",
                              "Density",
                              "Show probability density",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgHistogramChart2D:cumulative:
     *
     * Whether to show cumulative distribution.
     */
    properties[PROP_CUMULATIVE] =
        g_param_spec_boolean ("cumulative",
                              "Cumulative",
                              "Show cumulative distribution",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgHistogramChart2D:bar-color:
     *
     * Fill color for histogram bars.
     */
    properties[PROP_BAR_COLOR] =
        g_param_spec_boxed ("bar-color",
                            "Bar Color",
                            "Fill color for bars",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgHistogramChart2D:border-color:
     *
     * Border color for histogram bars.
     */
    properties[PROP_BORDER_COLOR] =
        g_param_spec_boxed ("border-color",
                            "Border Color",
                            "Border color for bars",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgHistogramChart2D:border-width:
     *
     * Border width for histogram bars.
     */
    properties[PROP_BORDER_WIDTH] =
        g_param_spec_float ("border-width",
                            "Border Width",
                            "Border width for bars",
                            0.0f, 10.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgHistogramChart2D:bar-spacing:
     *
     * Spacing between bars as fraction of bar width.
     */
    properties[PROP_BAR_SPACING] =
        g_param_spec_float ("bar-spacing",
                            "Bar Spacing",
                            "Spacing between bars",
                            0.0f, 0.9f, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgHistogramChart2D:show-cumulative-line:
     *
     * Whether to show cumulative distribution line overlay.
     */
    properties[PROP_SHOW_CUMULATIVE_LINE] =
        g_param_spec_boolean ("show-cumulative-line",
                              "Show Cumulative Line",
                              "Show cumulative distribution line",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgHistogramChart2D:cumulative-line-color:
     *
     * Color for cumulative distribution line.
     */
    properties[PROP_CUMULATIVE_LINE_COLOR] =
        g_param_spec_boxed ("cumulative-line-color",
                            "Cumulative Line Color",
                            "Color for cumulative line",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgHistogramChart2D:cumulative-line-width:
     *
     * Width of cumulative distribution line.
     */
    properties[PROP_CUMULATIVE_LINE_WIDTH] =
        g_param_spec_float ("cumulative-line-width",
                            "Cumulative Line Width",
                            "Width of cumulative line",
                            0.5f, 10.0f, 2.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_histogram_chart2d_init (LrgHistogramChart2D *self)
{
    self->bin_count = 0;  /* Auto */
    self->bin_width = 0.0;  /* Auto */
    self->range_min = -G_MAXDOUBLE;  /* Auto (sentinel value) */
    self->range_max = G_MAXDOUBLE;  /* Auto (sentinel value) */
    self->density = FALSE;
    self->cumulative = FALSE;
    self->bar_color = grl_color_new (100, 149, 237, 200);  /* Cornflower blue */
    self->border_color = grl_color_new (65, 105, 225, 255);  /* Royal blue */
    self->border_width = 1.0f;
    self->bar_spacing = 0.0f;  /* Adjacent bars typical for histogram */
    self->show_cumulative_line = FALSE;
    self->cumulative_line_color = grl_color_new (255, 69, 0, 255);  /* Orange-red */
    self->cumulative_line_width = 2.0f;
    self->bins = NULL;
    self->total_count = 0;
    self->needs_recalc = TRUE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgHistogramChart2D *
lrg_histogram_chart2d_new (void)
{
    return g_object_new (LRG_TYPE_HISTOGRAM_CHART2D, NULL);
}

LrgHistogramChart2D *
lrg_histogram_chart2d_new_with_size (gfloat width,
                                     gfloat height)
{
    return g_object_new (LRG_TYPE_HISTOGRAM_CHART2D,
                         "width", width,
                         "height", height,
                         NULL);
}

guint
lrg_histogram_chart2d_get_bin_count (LrgHistogramChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HISTOGRAM_CHART2D (self), 0);

    return self->bin_count;
}

void
lrg_histogram_chart2d_set_bin_count (LrgHistogramChart2D *self,
                                     guint                count)
{
    g_return_if_fail (LRG_IS_HISTOGRAM_CHART2D (self));

    if (self->bin_count != count)
    {
        self->bin_count = count;
        self->needs_recalc = TRUE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BIN_COUNT]);
    }
}

gdouble
lrg_histogram_chart2d_get_bin_width (LrgHistogramChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HISTOGRAM_CHART2D (self), 0.0);

    return self->bin_width;
}

void
lrg_histogram_chart2d_set_bin_width (LrgHistogramChart2D *self,
                                     gdouble              width)
{
    g_return_if_fail (LRG_IS_HISTOGRAM_CHART2D (self));

    if (self->bin_width != width)
    {
        self->bin_width = width;
        self->needs_recalc = TRUE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BIN_WIDTH]);
    }
}

gdouble
lrg_histogram_chart2d_get_range_min (LrgHistogramChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HISTOGRAM_CHART2D (self), -G_MAXDOUBLE);

    return self->range_min;
}

void
lrg_histogram_chart2d_set_range_min (LrgHistogramChart2D *self,
                                     gdouble              min)
{
    g_return_if_fail (LRG_IS_HISTOGRAM_CHART2D (self));

    if (self->range_min != min)
    {
        self->range_min = min;
        self->needs_recalc = TRUE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RANGE_MIN]);
    }
}

gdouble
lrg_histogram_chart2d_get_range_max (LrgHistogramChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HISTOGRAM_CHART2D (self), G_MAXDOUBLE);

    return self->range_max;
}

void
lrg_histogram_chart2d_set_range_max (LrgHistogramChart2D *self,
                                     gdouble              max)
{
    g_return_if_fail (LRG_IS_HISTOGRAM_CHART2D (self));

    if (self->range_max != max)
    {
        self->range_max = max;
        self->needs_recalc = TRUE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RANGE_MAX]);
    }
}

gboolean
lrg_histogram_chart2d_get_density (LrgHistogramChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HISTOGRAM_CHART2D (self), FALSE);

    return self->density;
}

void
lrg_histogram_chart2d_set_density (LrgHistogramChart2D *self,
                                   gboolean             density)
{
    g_return_if_fail (LRG_IS_HISTOGRAM_CHART2D (self));

    density = !!density;
    if (self->density != density)
    {
        self->density = density;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DENSITY]);
    }
}

gboolean
lrg_histogram_chart2d_get_cumulative (LrgHistogramChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HISTOGRAM_CHART2D (self), FALSE);

    return self->cumulative;
}

void
lrg_histogram_chart2d_set_cumulative (LrgHistogramChart2D *self,
                                      gboolean             cumulative)
{
    g_return_if_fail (LRG_IS_HISTOGRAM_CHART2D (self));

    cumulative = !!cumulative;
    if (self->cumulative != cumulative)
    {
        self->cumulative = cumulative;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CUMULATIVE]);
    }
}

GrlColor *
lrg_histogram_chart2d_get_bar_color (LrgHistogramChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HISTOGRAM_CHART2D (self), NULL);

    return self->bar_color;
}

void
lrg_histogram_chart2d_set_bar_color (LrgHistogramChart2D *self,
                                     GrlColor            *color)
{
    g_return_if_fail (LRG_IS_HISTOGRAM_CHART2D (self));

    g_clear_pointer (&self->bar_color, grl_color_free);
    if (color != NULL)
        self->bar_color = grl_color_copy (color);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BAR_COLOR]);
}

GrlColor *
lrg_histogram_chart2d_get_border_color (LrgHistogramChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HISTOGRAM_CHART2D (self), NULL);

    return self->border_color;
}

void
lrg_histogram_chart2d_set_border_color (LrgHistogramChart2D *self,
                                        GrlColor            *color)
{
    g_return_if_fail (LRG_IS_HISTOGRAM_CHART2D (self));

    g_clear_pointer (&self->border_color, grl_color_free);
    if (color != NULL)
        self->border_color = grl_color_copy (color);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER_COLOR]);
}

gfloat
lrg_histogram_chart2d_get_border_width (LrgHistogramChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HISTOGRAM_CHART2D (self), 0.0f);

    return self->border_width;
}

void
lrg_histogram_chart2d_set_border_width (LrgHistogramChart2D *self,
                                        gfloat               width)
{
    g_return_if_fail (LRG_IS_HISTOGRAM_CHART2D (self));

    if (self->border_width != width)
    {
        self->border_width = width;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER_WIDTH]);
    }
}

gfloat
lrg_histogram_chart2d_get_bar_spacing (LrgHistogramChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HISTOGRAM_CHART2D (self), 0.0f);

    return self->bar_spacing;
}

void
lrg_histogram_chart2d_set_bar_spacing (LrgHistogramChart2D *self,
                                       gfloat               spacing)
{
    g_return_if_fail (LRG_IS_HISTOGRAM_CHART2D (self));

    spacing = CLAMP (spacing, 0.0f, 0.9f);
    if (self->bar_spacing != spacing)
    {
        self->bar_spacing = spacing;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BAR_SPACING]);
    }
}

gboolean
lrg_histogram_chart2d_get_show_cumulative_line (LrgHistogramChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HISTOGRAM_CHART2D (self), FALSE);

    return self->show_cumulative_line;
}

void
lrg_histogram_chart2d_set_show_cumulative_line (LrgHistogramChart2D *self,
                                                gboolean             show)
{
    g_return_if_fail (LRG_IS_HISTOGRAM_CHART2D (self));

    show = !!show;
    if (self->show_cumulative_line != show)
    {
        self->show_cumulative_line = show;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_CUMULATIVE_LINE]);
    }
}

GrlColor *
lrg_histogram_chart2d_get_cumulative_line_color (LrgHistogramChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HISTOGRAM_CHART2D (self), NULL);

    return self->cumulative_line_color;
}

void
lrg_histogram_chart2d_set_cumulative_line_color (LrgHistogramChart2D *self,
                                                 GrlColor            *color)
{
    g_return_if_fail (LRG_IS_HISTOGRAM_CHART2D (self));

    g_clear_pointer (&self->cumulative_line_color, grl_color_free);
    if (color != NULL)
        self->cumulative_line_color = grl_color_copy (color);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CUMULATIVE_LINE_COLOR]);
}

gfloat
lrg_histogram_chart2d_get_cumulative_line_width (LrgHistogramChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HISTOGRAM_CHART2D (self), 2.0f);

    return self->cumulative_line_width;
}

void
lrg_histogram_chart2d_set_cumulative_line_width (LrgHistogramChart2D *self,
                                                 gfloat               width)
{
    g_return_if_fail (LRG_IS_HISTOGRAM_CHART2D (self));

    if (self->cumulative_line_width != width)
    {
        self->cumulative_line_width = width;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CUMULATIVE_LINE_WIDTH]);
    }
}

guint
lrg_histogram_chart2d_get_computed_bin_count (LrgHistogramChart2D *self)
{
    g_return_val_if_fail (LRG_IS_HISTOGRAM_CHART2D (self), 0);

    ensure_bins (self);

    return self->bins != NULL ? self->bins->len : 0;
}

guint
lrg_histogram_chart2d_get_bin_frequency (LrgHistogramChart2D *self,
                                         guint                bin_index)
{
    HistogramBin *bin;

    g_return_val_if_fail (LRG_IS_HISTOGRAM_CHART2D (self), 0);

    ensure_bins (self);

    if (self->bins == NULL || bin_index >= self->bins->len)
        return 0;

    bin = &g_array_index (self->bins, HistogramBin, bin_index);
    return bin->count;
}

void
lrg_histogram_chart2d_get_bin_range (LrgHistogramChart2D *self,
                                     guint                bin_index,
                                     gdouble             *out_min,
                                     gdouble             *out_max)
{
    HistogramBin *bin;

    g_return_if_fail (LRG_IS_HISTOGRAM_CHART2D (self));

    ensure_bins (self);

    if (self->bins == NULL || bin_index >= self->bins->len)
    {
        if (out_min)
            *out_min = 0.0;
        if (out_max)
            *out_max = 0.0;
        return;
    }

    bin = &g_array_index (self->bins, HistogramBin, bin_index);

    if (out_min)
        *out_min = bin->min_val;
    if (out_max)
        *out_max = bin->max_val;
}

void
lrg_histogram_chart2d_recalculate (LrgHistogramChart2D *self)
{
    g_return_if_fail (LRG_IS_HISTOGRAM_CHART2D (self));

    self->needs_recalc = TRUE;
    recalculate_bins (self);
}
