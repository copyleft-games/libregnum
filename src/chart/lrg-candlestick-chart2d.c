/* lrg-candlestick-chart2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Data point mapping for OHLC:
 *   X = time/index
 *   Y = open price
 *   Z = close price
 *   W = high price
 *   Label = "low" value as string (optional, defaults to min(open,close))
 */

#include "config.h"
#include "lrg-candlestick-chart2d.h"
#include "lrg-chart-private.h"
#include <math.h>
#include <stdlib.h>

/* ==========================================================================
 * Structure Definition
 * ========================================================================== */

struct _LrgCandlestickChart2D
{
    LrgChart2D parent_instance;

    /* Colors */
    GrlColor  *up_color;
    GrlColor  *down_color;

    /* Candle style */
    gfloat     candle_width;
    gfloat     wick_width;
    gboolean   filled_candles;
    gboolean   hollow_up;

    /* Volume */
    gboolean   show_volume;
    gfloat     volume_height;

    /* Hit testing */
    gfloat     hit_tolerance;
};

G_DEFINE_TYPE (LrgCandlestickChart2D, lrg_candlestick_chart2d, LRG_TYPE_CHART2D)

/* ==========================================================================
 * Properties
 * ========================================================================== */

enum
{
    PROP_0,
    PROP_UP_COLOR,
    PROP_DOWN_COLOR,
    PROP_CANDLE_WIDTH,
    PROP_WICK_WIDTH,
    PROP_FILLED_CANDLES,
    PROP_HOLLOW_UP,
    PROP_SHOW_VOLUME,
    PROP_VOLUME_HEIGHT,
    PROP_HIT_TOLERANCE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

/*
 * Get OHLC values from a data point.
 * X = time, Y = open, Z = close, W = high
 * Low is parsed from label or defaults to min(open, close).
 */
static void
get_ohlc (const LrgChartDataPoint *point,
          gdouble                 *out_open,
          gdouble                 *out_high,
          gdouble                 *out_low,
          gdouble                 *out_close)
{
    gdouble open, high, low, close;
    const gchar *label = NULL;

    open = lrg_chart_data_point_get_y (point);
    close = lrg_chart_data_point_get_z (point);
    high = lrg_chart_data_point_get_w (point);

    /* Try to get low from label */
    label = lrg_chart_data_point_get_label (point);
    if (label != NULL && label[0] != '\0')
    {
        low = g_ascii_strtod (label, NULL);
    }
    else
    {
        /* Default: low is the minimum of open and close */
        low = MIN (open, close);
    }

    /* Ensure high is actually highest */
    if (high < open) high = open;
    if (high < close) high = close;

    /* Ensure low is actually lowest */
    if (low > open) low = open;
    if (low > close) low = close;

    if (out_open) *out_open = open;
    if (out_high) *out_high = high;
    if (out_low) *out_low = low;
    if (out_close) *out_close = close;
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

static void
lrg_candlestick_chart2d_draw_data (LrgChart2D *chart2d)
{
    LrgCandlestickChart2D *self = LRG_CANDLESTICK_CHART2D (chart2d);
    LrgChart *chart = LRG_CHART (chart2d);
    GrlRectangle bounds;
    guint series_count;
    guint i, j;
    gdouble x_min, x_max;
    gfloat chart_height;

    lrg_chart_get_content_bounds (chart, &bounds);
    series_count = lrg_chart_get_series_count (chart);

    if (series_count == 0)
        return;

    /* Adjust bounds for volume if shown */
    chart_height = bounds.height;
    if (self->show_volume)
    {
        chart_height *= (1.0f - self->volume_height);
    }

    x_min = lrg_chart2d_get_x_min (chart2d);
    x_max = lrg_chart2d_get_x_max (chart2d);

    /* Draw each series */
    for (i = 0; i < series_count; i++)
    {
        LrgChartDataSeries *series = lrg_chart_get_series (chart, i);
        guint point_count;
        gdouble x_range;
        gfloat candle_spacing;

        if (!lrg_chart_data_series_get_visible (series))
            continue;

        point_count = lrg_chart_data_series_get_point_count (series);
        if (point_count == 0)
            continue;

        x_range = x_max - x_min;
        if (x_range <= 0) x_range = point_count;

        candle_spacing = bounds.width / (gfloat)point_count;

        for (j = 0; j < point_count; j++)
        {
            const LrgChartDataPoint *point = lrg_chart_data_series_get_point (series, j);
            gdouble x_val, open, high, low, close;
            gfloat sx, sy_open, sy_high, sy_low, sy_close;
            gfloat body_top, body_bottom, body_height;
            gfloat actual_candle_width;
            gboolean is_up;
            const GrlColor *color;

            x_val = lrg_chart_data_point_get_x (point);
            get_ohlc (point, &open, &high, &low, &close);

            /* Convert to screen coordinates */
            lrg_chart2d_data_to_screen (chart2d, x_val, open, &sx, &sy_open);
            lrg_chart2d_data_to_screen (chart2d, x_val, high, NULL, &sy_high);
            lrg_chart2d_data_to_screen (chart2d, x_val, low, NULL, &sy_low);
            lrg_chart2d_data_to_screen (chart2d, x_val, close, NULL, &sy_close);

            /* Determine direction */
            is_up = (close >= open);
            color = is_up ? self->up_color : self->down_color;

            /* Calculate candle body */
            actual_candle_width = candle_spacing * self->candle_width;
            body_top = MIN (sy_open, sy_close);
            body_bottom = MAX (sy_open, sy_close);
            body_height = body_bottom - body_top;
            if (body_height < 1.0f) body_height = 1.0f;  /* Minimum height for doji */

            /* Draw wick (shadow) */
            grl_draw_line_ex (&(GrlVector2){ sx, sy_high },
                              &(GrlVector2){ sx, sy_low },
                              self->wick_width, color);

            /* Draw candle body */
            if (self->filled_candles && (!is_up || !self->hollow_up))
            {
                /* Filled candle */
                grl_draw_rectangle (sx - actual_candle_width / 2.0f,
                                    body_top,
                                    actual_candle_width,
                                    body_height,
                                    color);
            }
            else
            {
                /* Hollow candle (outline only) */
                GrlRectangle candle_rect;
                candle_rect.x = sx - actual_candle_width / 2.0f;
                candle_rect.y = body_top;
                candle_rect.width = actual_candle_width;
                candle_rect.height = body_height;
                grl_draw_rectangle_lines_ex (&candle_rect, self->wick_width, color);
            }
        }

        /* Draw volume bars if enabled */
        if (self->show_volume)
        {
            gdouble max_volume = 0;

            /* Find max volume */
            for (j = 0; j < point_count; j++)
            {
                const LrgChartDataPoint *point = lrg_chart_data_series_get_point (series, j);
                /* Use Z value for volume when W is used for high */
                gdouble vol = fabs (lrg_chart_data_point_get_w (point));
                if (vol > max_volume) max_volume = vol;
            }

            if (max_volume > 0)
            {
                gfloat volume_area_top = bounds.y + chart_height;
                gfloat volume_area_height = bounds.height * self->volume_height;

                for (j = 0; j < point_count; j++)
                {
                    const LrgChartDataPoint *point = lrg_chart_data_series_get_point (series, j);
                    gdouble x_val, open, high, low, close;
                    gfloat sx;
                    gdouble vol;
                    gfloat bar_height;
                    gboolean is_up;
                    g_autoptr(GrlColor) vol_color = NULL;
                    const GrlColor *base_vol_color = NULL;
                    gfloat vol_candle_width;
                    guint8 r, g, b, a;

                    x_val = lrg_chart_data_point_get_x (point);
                    get_ohlc (point, &open, &high, &low, &close);

                    lrg_chart2d_data_to_screen (chart2d, x_val, 0, &sx, NULL);

                    vol = fabs (lrg_chart_data_point_get_w (point));
                    bar_height = (gfloat)(vol / max_volume) * volume_area_height * 0.9f;

                    is_up = (close >= open);
                    base_vol_color = is_up ? self->up_color : self->down_color;

                    /* Create semi-transparent volume color */
                    r = grl_color_get_r (base_vol_color);
                    g = grl_color_get_g (base_vol_color);
                    b = grl_color_get_b (base_vol_color);
                    a = (guint8)(grl_color_get_a (base_vol_color) * 0.5f);
                    vol_color = grl_color_new (r, g, b, a);

                    vol_candle_width = candle_spacing * self->candle_width;
                    grl_draw_rectangle (sx - vol_candle_width / 2.0f,
                                        volume_area_top + volume_area_height - bar_height,
                                        vol_candle_width,
                                        bar_height,
                                        vol_color);
                }
            }
        }
    }
}

static gboolean
lrg_candlestick_chart2d_hit_test (LrgChart        *chart,
                                  gfloat           x,
                                  gfloat           y,
                                  LrgChartHitInfo *out_hit)
{
    LrgCandlestickChart2D *self = LRG_CANDLESTICK_CHART2D (chart);
    LrgChart2D *chart2d = LRG_CHART2D (chart);
    GrlRectangle bounds;
    guint series_count;
    guint i, j;
    gdouble x_min, x_max;

    if (out_hit != NULL)
        lrg_chart_hit_info_clear (out_hit);

    lrg_chart_get_content_bounds (chart, &bounds);
    series_count = lrg_chart_get_series_count (chart);

    x_min = lrg_chart2d_get_x_min (chart2d);
    x_max = lrg_chart2d_get_x_max (chart2d);

    for (i = 0; i < series_count; i++)
    {
        LrgChartDataSeries *series = lrg_chart_get_series (chart, i);
        guint point_count;
        gdouble x_range;
        gfloat candle_spacing;

        if (!lrg_chart_data_series_get_visible (series))
            continue;

        point_count = lrg_chart_data_series_get_point_count (series);
        if (point_count == 0)
            continue;

        x_range = x_max - x_min;
        if (x_range <= 0) x_range = point_count;

        candle_spacing = bounds.width / (gfloat)point_count;

        for (j = 0; j < point_count; j++)
        {
            const LrgChartDataPoint *point = lrg_chart_data_series_get_point (series, j);
            gdouble x_val, open, high, low, close;
            gfloat sx, sy_high, sy_low;
            gfloat actual_candle_width;
            gfloat hit_left, hit_right, hit_top, hit_bottom;

            x_val = lrg_chart_data_point_get_x (point);
            get_ohlc (point, &open, &high, &low, &close);

            lrg_chart2d_data_to_screen (chart2d, x_val, high, &sx, &sy_high);
            lrg_chart2d_data_to_screen (chart2d, x_val, low, NULL, &sy_low);

            actual_candle_width = candle_spacing * self->candle_width;
            hit_left = sx - actual_candle_width / 2.0f - self->hit_tolerance;
            hit_right = sx + actual_candle_width / 2.0f + self->hit_tolerance;
            hit_top = MIN (sy_high, sy_low) - self->hit_tolerance;
            hit_bottom = MAX (sy_high, sy_low) + self->hit_tolerance;

            if (x >= hit_left && x <= hit_right && y >= hit_top && y <= hit_bottom)
            {
                if (out_hit != NULL)
                {
                    GrlRectangle hit_bounds;

                    lrg_chart_hit_info_set_series_index (out_hit, i);
                    lrg_chart_hit_info_set_point_index (out_hit, j);
                    lrg_chart_hit_info_set_screen_x (out_hit, sx);
                    lrg_chart_hit_info_set_screen_y (out_hit, (sy_high + sy_low) / 2.0f);
                    lrg_chart_hit_info_set_data_point (out_hit, point);

                    hit_bounds.x = hit_left;
                    hit_bounds.y = hit_top;
                    hit_bounds.width = hit_right - hit_left;
                    hit_bounds.height = hit_bottom - hit_top;
                    lrg_chart_hit_info_set_bounds (out_hit, &hit_bounds);
                }
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
lrg_candlestick_chart2d_finalize (GObject *object)
{
    LrgCandlestickChart2D *self = LRG_CANDLESTICK_CHART2D (object);

    g_clear_pointer (&self->up_color, grl_color_free);
    g_clear_pointer (&self->down_color, grl_color_free);

    G_OBJECT_CLASS (lrg_candlestick_chart2d_parent_class)->finalize (object);
}

static void
lrg_candlestick_chart2d_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
    LrgCandlestickChart2D *self = LRG_CANDLESTICK_CHART2D (object);

    switch (prop_id)
    {
    case PROP_UP_COLOR:
        g_value_set_boxed (value, self->up_color);
        break;
    case PROP_DOWN_COLOR:
        g_value_set_boxed (value, self->down_color);
        break;
    case PROP_CANDLE_WIDTH:
        g_value_set_float (value, self->candle_width);
        break;
    case PROP_WICK_WIDTH:
        g_value_set_float (value, self->wick_width);
        break;
    case PROP_FILLED_CANDLES:
        g_value_set_boolean (value, self->filled_candles);
        break;
    case PROP_HOLLOW_UP:
        g_value_set_boolean (value, self->hollow_up);
        break;
    case PROP_SHOW_VOLUME:
        g_value_set_boolean (value, self->show_volume);
        break;
    case PROP_VOLUME_HEIGHT:
        g_value_set_float (value, self->volume_height);
        break;
    case PROP_HIT_TOLERANCE:
        g_value_set_float (value, self->hit_tolerance);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_candlestick_chart2d_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
    LrgCandlestickChart2D *self = LRG_CANDLESTICK_CHART2D (object);

    switch (prop_id)
    {
    case PROP_UP_COLOR:
        lrg_candlestick_chart2d_set_up_color (self, g_value_get_boxed (value));
        break;
    case PROP_DOWN_COLOR:
        lrg_candlestick_chart2d_set_down_color (self, g_value_get_boxed (value));
        break;
    case PROP_CANDLE_WIDTH:
        lrg_candlestick_chart2d_set_candle_width (self, g_value_get_float (value));
        break;
    case PROP_WICK_WIDTH:
        lrg_candlestick_chart2d_set_wick_width (self, g_value_get_float (value));
        break;
    case PROP_FILLED_CANDLES:
        lrg_candlestick_chart2d_set_filled_candles (self, g_value_get_boolean (value));
        break;
    case PROP_HOLLOW_UP:
        lrg_candlestick_chart2d_set_hollow_up (self, g_value_get_boolean (value));
        break;
    case PROP_SHOW_VOLUME:
        lrg_candlestick_chart2d_set_show_volume (self, g_value_get_boolean (value));
        break;
    case PROP_VOLUME_HEIGHT:
        lrg_candlestick_chart2d_set_volume_height (self, g_value_get_float (value));
        break;
    case PROP_HIT_TOLERANCE:
        lrg_candlestick_chart2d_set_hit_tolerance (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_candlestick_chart2d_class_init (LrgCandlestickChart2DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgChartClass *chart_class = LRG_CHART_CLASS (klass);
    LrgChart2DClass *chart2d_class = LRG_CHART2D_CLASS (klass);

    object_class->finalize = lrg_candlestick_chart2d_finalize;
    object_class->get_property = lrg_candlestick_chart2d_get_property;
    object_class->set_property = lrg_candlestick_chart2d_set_property;

    /* Override chart methods */
    chart_class->hit_test = lrg_candlestick_chart2d_hit_test;

    /* Override chart2d methods */
    chart2d_class->draw_data = lrg_candlestick_chart2d_draw_data;

    /* Properties */
    properties[PROP_UP_COLOR] =
        g_param_spec_boxed ("up-color",
                            "Up Color",
                            "Color for bullish candles",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_DOWN_COLOR] =
        g_param_spec_boxed ("down-color",
                            "Down Color",
                            "Color for bearish candles",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_CANDLE_WIDTH] =
        g_param_spec_float ("candle-width",
                            "Candle Width",
                            "Width as fraction of spacing",
                            0.1f, 1.0f, 0.8f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_WICK_WIDTH] =
        g_param_spec_float ("wick-width",
                            "Wick Width",
                            "Width of wicks in pixels",
                            0.5f, 5.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_FILLED_CANDLES] =
        g_param_spec_boolean ("filled-candles",
                              "Filled Candles",
                              "Fill candle bodies",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_HOLLOW_UP] =
        g_param_spec_boolean ("hollow-up",
                              "Hollow Up",
                              "Draw up candles as hollow",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_VOLUME] =
        g_param_spec_boolean ("show-volume",
                              "Show Volume",
                              "Show volume bars",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_VOLUME_HEIGHT] =
        g_param_spec_float ("volume-height",
                            "Volume Height",
                            "Height of volume area as fraction",
                            0.0f, 0.5f, 0.2f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_HIT_TOLERANCE] =
        g_param_spec_float ("hit-tolerance",
                            "Hit Tolerance",
                            "Hit test tolerance in pixels",
                            0.0f, 20.0f, 2.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_candlestick_chart2d_init (LrgCandlestickChart2D *self)
{
    self->up_color = grl_color_new (0, 200, 83, 255);    /* Green */
    self->down_color = grl_color_new (255, 82, 82, 255); /* Red */
    self->candle_width = 0.8f;
    self->wick_width = 1.0f;
    self->filled_candles = TRUE;
    self->hollow_up = FALSE;
    self->show_volume = FALSE;
    self->volume_height = 0.2f;
    self->hit_tolerance = 2.0f;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgCandlestickChart2D *
lrg_candlestick_chart2d_new (void)
{
    return g_object_new (LRG_TYPE_CANDLESTICK_CHART2D, NULL);
}

LrgCandlestickChart2D *
lrg_candlestick_chart2d_new_with_size (gfloat width,
                                       gfloat height)
{
    return g_object_new (LRG_TYPE_CANDLESTICK_CHART2D,
                         "width", width,
                         "height", height,
                         NULL);
}

/* ==========================================================================
 * Candlestick Style
 * ========================================================================== */

GrlColor *
lrg_candlestick_chart2d_get_up_color (LrgCandlestickChart2D *self)
{
    g_return_val_if_fail (LRG_IS_CANDLESTICK_CHART2D (self), NULL);
    return self->up_color;
}

void
lrg_candlestick_chart2d_set_up_color (LrgCandlestickChart2D *self,
                                      GrlColor              *color)
{
    g_return_if_fail (LRG_IS_CANDLESTICK_CHART2D (self));

    g_clear_pointer (&self->up_color, grl_color_free);
    if (color != NULL)
        self->up_color = grl_color_copy (color);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UP_COLOR]);
}

GrlColor *
lrg_candlestick_chart2d_get_down_color (LrgCandlestickChart2D *self)
{
    g_return_val_if_fail (LRG_IS_CANDLESTICK_CHART2D (self), NULL);
    return self->down_color;
}

void
lrg_candlestick_chart2d_set_down_color (LrgCandlestickChart2D *self,
                                        GrlColor              *color)
{
    g_return_if_fail (LRG_IS_CANDLESTICK_CHART2D (self));

    g_clear_pointer (&self->down_color, grl_color_free);
    if (color != NULL)
        self->down_color = grl_color_copy (color);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DOWN_COLOR]);
}

gfloat
lrg_candlestick_chart2d_get_candle_width (LrgCandlestickChart2D *self)
{
    g_return_val_if_fail (LRG_IS_CANDLESTICK_CHART2D (self), 0.0f);
    return self->candle_width;
}

void
lrg_candlestick_chart2d_set_candle_width (LrgCandlestickChart2D *self,
                                          gfloat                 width)
{
    g_return_if_fail (LRG_IS_CANDLESTICK_CHART2D (self));

    width = CLAMP (width, 0.1f, 1.0f);

    if (self->candle_width == width)
        return;

    self->candle_width = width;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CANDLE_WIDTH]);
}

gfloat
lrg_candlestick_chart2d_get_wick_width (LrgCandlestickChart2D *self)
{
    g_return_val_if_fail (LRG_IS_CANDLESTICK_CHART2D (self), 0.0f);
    return self->wick_width;
}

void
lrg_candlestick_chart2d_set_wick_width (LrgCandlestickChart2D *self,
                                        gfloat                 width)
{
    g_return_if_fail (LRG_IS_CANDLESTICK_CHART2D (self));

    if (self->wick_width == width)
        return;

    self->wick_width = width;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WICK_WIDTH]);
}

/* ==========================================================================
 * Display Options
 * ========================================================================== */

gboolean
lrg_candlestick_chart2d_get_filled_candles (LrgCandlestickChart2D *self)
{
    g_return_val_if_fail (LRG_IS_CANDLESTICK_CHART2D (self), FALSE);
    return self->filled_candles;
}

void
lrg_candlestick_chart2d_set_filled_candles (LrgCandlestickChart2D *self,
                                            gboolean               filled)
{
    g_return_if_fail (LRG_IS_CANDLESTICK_CHART2D (self));

    filled = !!filled;

    if (self->filled_candles == filled)
        return;

    self->filled_candles = filled;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILLED_CANDLES]);
}

gboolean
lrg_candlestick_chart2d_get_hollow_up (LrgCandlestickChart2D *self)
{
    g_return_val_if_fail (LRG_IS_CANDLESTICK_CHART2D (self), FALSE);
    return self->hollow_up;
}

void
lrg_candlestick_chart2d_set_hollow_up (LrgCandlestickChart2D *self,
                                       gboolean               hollow)
{
    g_return_if_fail (LRG_IS_CANDLESTICK_CHART2D (self));

    hollow = !!hollow;

    if (self->hollow_up == hollow)
        return;

    self->hollow_up = hollow;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HOLLOW_UP]);
}

/* ==========================================================================
 * Volume Bars
 * ========================================================================== */

gboolean
lrg_candlestick_chart2d_get_show_volume (LrgCandlestickChart2D *self)
{
    g_return_val_if_fail (LRG_IS_CANDLESTICK_CHART2D (self), FALSE);
    return self->show_volume;
}

void
lrg_candlestick_chart2d_set_show_volume (LrgCandlestickChart2D *self,
                                         gboolean               show)
{
    g_return_if_fail (LRG_IS_CANDLESTICK_CHART2D (self));

    show = !!show;

    if (self->show_volume == show)
        return;

    self->show_volume = show;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_VOLUME]);
}

gfloat
lrg_candlestick_chart2d_get_volume_height (LrgCandlestickChart2D *self)
{
    g_return_val_if_fail (LRG_IS_CANDLESTICK_CHART2D (self), 0.0f);
    return self->volume_height;
}

void
lrg_candlestick_chart2d_set_volume_height (LrgCandlestickChart2D *self,
                                           gfloat                 height)
{
    g_return_if_fail (LRG_IS_CANDLESTICK_CHART2D (self));

    height = CLAMP (height, 0.0f, 0.5f);

    if (self->volume_height == height)
        return;

    self->volume_height = height;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VOLUME_HEIGHT]);
}

/* ==========================================================================
 * Hit Testing
 * ========================================================================== */

gfloat
lrg_candlestick_chart2d_get_hit_tolerance (LrgCandlestickChart2D *self)
{
    g_return_val_if_fail (LRG_IS_CANDLESTICK_CHART2D (self), 0.0f);
    return self->hit_tolerance;
}

void
lrg_candlestick_chart2d_set_hit_tolerance (LrgCandlestickChart2D *self,
                                           gfloat                 tolerance)
{
    g_return_if_fail (LRG_IS_CANDLESTICK_CHART2D (self));

    if (self->hit_tolerance == tolerance)
        return;

    self->hit_tolerance = tolerance;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HIT_TOLERANCE]);
}
