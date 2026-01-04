/* lrg-gauge-chart2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-gauge-chart2d.h"
#include "lrg-chart-private.h"
#include <math.h>
#include <stdio.h>

#ifndef G_PI
#define G_PI 3.14159265358979323846
#endif

/* ==========================================================================
 * Zone Structure
 * ========================================================================== */

typedef struct
{
    gdouble   start;
    gdouble   end;
    GrlColor *color;
} GaugeZone;

static GaugeZone *
gauge_zone_new (gdouble start, gdouble end, GrlColor *color)
{
    GaugeZone *zone = g_new0 (GaugeZone, 1);
    zone->start = start;
    zone->end = end;
    zone->color = (color != NULL) ? grl_color_copy (color) : NULL;
    return zone;
}

static void
gauge_zone_free (gpointer data)
{
    GaugeZone *zone = data;
    if (zone == NULL)
        return;
    g_clear_pointer (&zone->color, grl_color_free);
    g_free (zone);
}

/* ==========================================================================
 * Structure Definition
 * ========================================================================== */

struct _LrgGaugeChart2D
{
    LrgChart2D parent_instance;

    /* Value */
    gdouble            value;
    gdouble            min_value;
    gdouble            max_value;

    /* Style */
    LrgChartGaugeStyle style;
    gfloat             start_angle;
    gfloat             sweep_angle;

    /* Colors */
    GrlColor          *needle_color;
    GrlColor          *track_color;
    GrlColor          *fill_color;

    /* Display */
    gfloat             arc_width;
    gboolean           show_value;
    gchar             *value_format;
    gboolean           show_ticks;
    guint              tick_count;

    /* Zones */
    GPtrArray         *zones;
};

G_DEFINE_TYPE (LrgGaugeChart2D, lrg_gauge_chart2d, LRG_TYPE_CHART2D)

/* ==========================================================================
 * Properties
 * ========================================================================== */

enum
{
    PROP_0,
    PROP_VALUE,
    PROP_MIN_VALUE,
    PROP_MAX_VALUE,
    PROP_STYLE,
    PROP_START_ANGLE,
    PROP_SWEEP_ANGLE,
    PROP_NEEDLE_COLOR,
    PROP_TRACK_COLOR,
    PROP_FILL_COLOR,
    PROP_ARC_WIDTH,
    PROP_SHOW_VALUE,
    PROP_VALUE_FORMAT,
    PROP_SHOW_TICKS,
    PROP_TICK_COUNT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

static gfloat
degrees_to_radians (gfloat degrees)
{
    return degrees * (G_PI / 180.0f);
}

static void
get_point_on_arc (gfloat  center_x,
                  gfloat  center_y,
                  gfloat  radius,
                  gfloat  angle_deg,
                  gfloat *out_x,
                  gfloat *out_y)
{
    gfloat rad = degrees_to_radians (angle_deg);
    if (out_x) *out_x = center_x + radius * cosf (rad);
    if (out_y) *out_y = center_y + radius * sinf (rad);
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

static void
lrg_gauge_chart2d_draw_axes (LrgChart2D *chart2d)
{
    /* Gauge doesn't use standard axes */
}

static void
lrg_gauge_chart2d_draw_grid (LrgChart2D *chart2d)
{
    /* Gauge doesn't use standard grid */
}

static void
lrg_gauge_chart2d_draw_data (LrgChart2D *chart2d)
{
    LrgGaugeChart2D *self = LRG_GAUGE_CHART2D (chart2d);
    LrgChart *chart = LRG_CHART (chart2d);
    GrlRectangle bounds;
    gfloat center_x, center_y, radius;
    gdouble normalized;
    gfloat value_angle;
    guint i;
    GrlVector2 center_vec;

    lrg_chart_get_content_bounds (chart, &bounds);
    center_x = bounds.x + bounds.width / 2.0f;
    center_y = bounds.y + bounds.height / 2.0f;
    center_vec.x = center_x;
    center_vec.y = center_y;
    radius = MIN (bounds.width, bounds.height) / 2.0f * 0.9f;

    /* Calculate normalized value and angle */
    if (self->max_value > self->min_value)
        normalized = (self->value - self->min_value) / (self->max_value - self->min_value);
    else
        normalized = 0;

    normalized = CLAMP (normalized, 0.0, 1.0);
    value_angle = self->start_angle + normalized * self->sweep_angle;

    /* Draw based on style */
    switch (self->style)
    {
    case LRG_CHART_GAUGE_NEEDLE:
        {
            /* Draw zones first */
            for (i = 0; i < self->zones->len; i++)
            {
                GaugeZone *zone = g_ptr_array_index (self->zones, i);
                gdouble zone_start_norm = (zone->start - self->min_value) / (self->max_value - self->min_value);
                gdouble zone_end_norm = (zone->end - self->min_value) / (self->max_value - self->min_value);
                gfloat zone_start_angle = self->start_angle + zone_start_norm * self->sweep_angle;
                gfloat zone_end_angle = self->start_angle + zone_end_norm * self->sweep_angle;

                grl_draw_ring (&center_vec,
                               radius * 0.7f, radius * 0.85f,
                               degrees_to_radians (zone_start_angle),
                               degrees_to_radians (zone_end_angle),
                               32, zone->color);
            }

            /* Draw track arc */
            grl_draw_ring (&center_vec,
                           radius * 0.85f, radius * 0.9f,
                           degrees_to_radians (self->start_angle),
                           degrees_to_radians (self->start_angle + self->sweep_angle),
                           64, self->track_color);

            /* Draw ticks */
            if (self->show_ticks && self->tick_count > 1)
            {
                GrlColor tick_color = { 200, 200, 200, 255 };

                for (i = 0; i <= self->tick_count; i++)
                {
                    gdouble t = (gdouble)i / self->tick_count;
                    gfloat tick_angle = self->start_angle + t * self->sweep_angle;
                    gfloat x1, y1, x2, y2;

                    get_point_on_arc (center_x, center_y, radius * 0.9f, tick_angle, &x1, &y1);
                    get_point_on_arc (center_x, center_y, radius * 0.95f, tick_angle, &x2, &y2);

                    grl_draw_line_ex (&(GrlVector2){ x1, y1 },
                                      &(GrlVector2){ x2, y2 },
                                      2.0f, &tick_color);
                }
            }

            /* Draw needle */
            {
                gfloat nx, ny;
                gfloat base_x1, base_y1, base_x2, base_y2;

                get_point_on_arc (center_x, center_y, radius * 0.8f, value_angle, &nx, &ny);
                get_point_on_arc (center_x, center_y, radius * 0.1f, value_angle - 90, &base_x1, &base_y1);
                get_point_on_arc (center_x, center_y, radius * 0.1f, value_angle + 90, &base_x2, &base_y2);

                /* Draw needle as triangle */
                grl_draw_triangle (&(GrlVector2){ nx, ny },
                                   &(GrlVector2){ base_x1, base_y1 },
                                   &(GrlVector2){ base_x2, base_y2 },
                                   self->needle_color);

                /* Draw center cap */
                grl_draw_circle (center_x, center_y, radius * 0.08f, self->needle_color);
            }
        }
        break;

    case LRG_CHART_GAUGE_ARC:
        {
            /* Draw track arc */
            grl_draw_ring (&center_vec,
                           radius - self->arc_width, radius,
                           degrees_to_radians (self->start_angle),
                           degrees_to_radians (self->start_angle + self->sweep_angle),
                           64, self->track_color);

            /* Draw zones */
            for (i = 0; i < self->zones->len; i++)
            {
                GaugeZone *zone = g_ptr_array_index (self->zones, i);
                gdouble zone_start_norm = (zone->start - self->min_value) / (self->max_value - self->min_value);
                gdouble zone_end_norm = (zone->end - self->min_value) / (self->max_value - self->min_value);
                gfloat zone_start_angle = self->start_angle + zone_start_norm * self->sweep_angle;
                gfloat zone_end_angle = self->start_angle + zone_end_norm * self->sweep_angle;

                grl_draw_ring (&center_vec,
                               radius - self->arc_width, radius,
                               degrees_to_radians (zone_start_angle),
                               degrees_to_radians (zone_end_angle),
                               32, zone->color);
            }

            /* Draw value arc (on top) */
            grl_draw_ring (&center_vec,
                           radius - self->arc_width, radius,
                           degrees_to_radians (self->start_angle),
                           degrees_to_radians (value_angle),
                           64, self->fill_color);
        }
        break;

    case LRG_CHART_GAUGE_DIGITAL:
        {
            /* Digital display - just show the value */
            gchar value_str[64];
            gint text_width;
            g_autoptr(GrlColor) bg_color = grl_color_new (30, 30, 30, 255);
            gfloat box_width = radius * 1.5f;
            gfloat box_height = radius * 0.6f;
            GrlRectangle border_rect;
            const gchar *fmt;

            /* Draw background box */
            grl_draw_rectangle (center_x - box_width / 2.0f,
                                center_y - box_height / 2.0f,
                                box_width, box_height,
                                bg_color);

            /* Draw border */
            border_rect.x = center_x - box_width / 2.0f;
            border_rect.y = center_y - box_height / 2.0f;
            border_rect.width = box_width;
            border_rect.height = box_height;
            grl_draw_rectangle_lines_ex (&border_rect, 2.0f, self->track_color);

            /* Draw value text */
            fmt = self->value_format;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
            g_snprintf (value_str, sizeof (value_str), fmt, self->value);
#pragma GCC diagnostic pop
            text_width = grl_measure_text (value_str, 32);
            grl_draw_text (value_str,
                           (gint)(center_x - text_width / 2),
                           (gint)(center_y - 16),
                           32, self->fill_color);

            /* Draw min/max labels */
            g_snprintf (value_str, sizeof (value_str), "%.0f", self->min_value);
            text_width = grl_measure_text (value_str, 14);
            grl_draw_text (value_str,
                           (gint)(center_x - box_width / 2.0f + 5),
                           (gint)(center_y + box_height / 2.0f - 20),
                           14, self->track_color);

            g_snprintf (value_str, sizeof (value_str), "%.0f", self->max_value);
            text_width = grl_measure_text (value_str, 14);
            grl_draw_text (value_str,
                           (gint)(center_x + box_width / 2.0f - text_width - 5),
                           (gint)(center_y + box_height / 2.0f - 20),
                           14, self->track_color);
        }
        break;
    }

    /* Draw value text (for needle and arc styles) */
    if (self->show_value && self->style != LRG_CHART_GAUGE_DIGITAL)
    {
        gchar value_str[64];
        gint text_width;
        gfloat text_y;
        const gchar *fmt;
        g_autoptr(GrlColor) text_color = grl_color_new (255, 255, 255, 255);

        fmt = self->value_format;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
        g_snprintf (value_str, sizeof (value_str), fmt, self->value);
#pragma GCC diagnostic pop
        text_width = grl_measure_text (value_str, 20);

        /* Position below center for half-circle gauges */
        text_y = center_y + radius * 0.3f;
        grl_draw_text (value_str,
                       (gint)(center_x - text_width / 2),
                       (gint)text_y,
                       20, text_color);
    }
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_gauge_chart2d_finalize (GObject *object)
{
    LrgGaugeChart2D *self = LRG_GAUGE_CHART2D (object);

    g_clear_pointer (&self->needle_color, grl_color_free);
    g_clear_pointer (&self->track_color, grl_color_free);
    g_clear_pointer (&self->fill_color, grl_color_free);
    g_clear_pointer (&self->value_format, g_free);
    g_clear_pointer (&self->zones, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_gauge_chart2d_parent_class)->finalize (object);
}

static void
lrg_gauge_chart2d_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgGaugeChart2D *self = LRG_GAUGE_CHART2D (object);

    switch (prop_id)
    {
    case PROP_VALUE:
        g_value_set_double (value, self->value);
        break;
    case PROP_MIN_VALUE:
        g_value_set_double (value, self->min_value);
        break;
    case PROP_MAX_VALUE:
        g_value_set_double (value, self->max_value);
        break;
    case PROP_STYLE:
        g_value_set_enum (value, self->style);
        break;
    case PROP_START_ANGLE:
        g_value_set_float (value, self->start_angle);
        break;
    case PROP_SWEEP_ANGLE:
        g_value_set_float (value, self->sweep_angle);
        break;
    case PROP_NEEDLE_COLOR:
        g_value_set_boxed (value, self->needle_color);
        break;
    case PROP_TRACK_COLOR:
        g_value_set_boxed (value, self->track_color);
        break;
    case PROP_FILL_COLOR:
        g_value_set_boxed (value, self->fill_color);
        break;
    case PROP_ARC_WIDTH:
        g_value_set_float (value, self->arc_width);
        break;
    case PROP_SHOW_VALUE:
        g_value_set_boolean (value, self->show_value);
        break;
    case PROP_VALUE_FORMAT:
        g_value_set_string (value, self->value_format);
        break;
    case PROP_SHOW_TICKS:
        g_value_set_boolean (value, self->show_ticks);
        break;
    case PROP_TICK_COUNT:
        g_value_set_uint (value, self->tick_count);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_gauge_chart2d_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgGaugeChart2D *self = LRG_GAUGE_CHART2D (object);

    switch (prop_id)
    {
    case PROP_VALUE:
        lrg_gauge_chart2d_set_value (self, g_value_get_double (value));
        break;
    case PROP_MIN_VALUE:
        lrg_gauge_chart2d_set_min_value (self, g_value_get_double (value));
        break;
    case PROP_MAX_VALUE:
        lrg_gauge_chart2d_set_max_value (self, g_value_get_double (value));
        break;
    case PROP_STYLE:
        lrg_gauge_chart2d_set_style (self, g_value_get_enum (value));
        break;
    case PROP_START_ANGLE:
        lrg_gauge_chart2d_set_start_angle (self, g_value_get_float (value));
        break;
    case PROP_SWEEP_ANGLE:
        lrg_gauge_chart2d_set_sweep_angle (self, g_value_get_float (value));
        break;
    case PROP_NEEDLE_COLOR:
        lrg_gauge_chart2d_set_needle_color (self, g_value_get_boxed (value));
        break;
    case PROP_TRACK_COLOR:
        lrg_gauge_chart2d_set_track_color (self, g_value_get_boxed (value));
        break;
    case PROP_FILL_COLOR:
        lrg_gauge_chart2d_set_fill_color (self, g_value_get_boxed (value));
        break;
    case PROP_ARC_WIDTH:
        lrg_gauge_chart2d_set_arc_width (self, g_value_get_float (value));
        break;
    case PROP_SHOW_VALUE:
        lrg_gauge_chart2d_set_show_value (self, g_value_get_boolean (value));
        break;
    case PROP_VALUE_FORMAT:
        lrg_gauge_chart2d_set_value_format (self, g_value_get_string (value));
        break;
    case PROP_SHOW_TICKS:
        lrg_gauge_chart2d_set_show_ticks (self, g_value_get_boolean (value));
        break;
    case PROP_TICK_COUNT:
        lrg_gauge_chart2d_set_tick_count (self, g_value_get_uint (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_gauge_chart2d_class_init (LrgGaugeChart2DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgChart2DClass *chart2d_class = LRG_CHART2D_CLASS (klass);

    object_class->finalize = lrg_gauge_chart2d_finalize;
    object_class->get_property = lrg_gauge_chart2d_get_property;
    object_class->set_property = lrg_gauge_chart2d_set_property;

    /* Override chart2d methods */
    chart2d_class->draw_axes = lrg_gauge_chart2d_draw_axes;
    chart2d_class->draw_grid = lrg_gauge_chart2d_draw_grid;
    chart2d_class->draw_data = lrg_gauge_chart2d_draw_data;

    /* Properties */
    properties[PROP_VALUE] =
        g_param_spec_double ("value",
                             "Value",
                             "Current gauge value",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_MIN_VALUE] =
        g_param_spec_double ("min-value",
                             "Min Value",
                             "Minimum value",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_VALUE] =
        g_param_spec_double ("max-value",
                             "Max Value",
                             "Maximum value",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 100.0,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_STYLE] =
        g_param_spec_enum ("style",
                           "Style",
                           "Gauge display style",
                           LRG_TYPE_CHART_GAUGE_STYLE,
                           LRG_CHART_GAUGE_NEEDLE,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_START_ANGLE] =
        g_param_spec_float ("start-angle",
                            "Start Angle",
                            "Start angle in degrees",
                            0.0f, 360.0f, 135.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SWEEP_ANGLE] =
        g_param_spec_float ("sweep-angle",
                            "Sweep Angle",
                            "Sweep angle in degrees",
                            0.0f, 360.0f, 270.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_NEEDLE_COLOR] =
        g_param_spec_boxed ("needle-color",
                            "Needle Color",
                            "Color of the needle",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_TRACK_COLOR] =
        g_param_spec_boxed ("track-color",
                            "Track Color",
                            "Color of the track",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_FILL_COLOR] =
        g_param_spec_boxed ("fill-color",
                            "Fill Color",
                            "Color of the fill",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_ARC_WIDTH] =
        g_param_spec_float ("arc-width",
                            "Arc Width",
                            "Width of arc for arc style",
                            1.0f, 100.0f, 20.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_VALUE] =
        g_param_spec_boolean ("show-value",
                              "Show Value",
                              "Show numeric value",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_VALUE_FORMAT] =
        g_param_spec_string ("value-format",
                             "Value Format",
                             "Printf format for value",
                             "%.1f",
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_TICKS] =
        g_param_spec_boolean ("show-ticks",
                              "Show Ticks",
                              "Show tick marks",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_TICK_COUNT] =
        g_param_spec_uint ("tick-count",
                           "Tick Count",
                           "Number of major ticks",
                           2, 20, 10,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_gauge_chart2d_init (LrgGaugeChart2D *self)
{
    self->value = 0.0;
    self->min_value = 0.0;
    self->max_value = 100.0;
    self->style = LRG_CHART_GAUGE_NEEDLE;
    self->start_angle = 135.0f;  /* Bottom-left */
    self->sweep_angle = 270.0f;  /* Three-quarter circle */
    self->needle_color = grl_color_new (255, 80, 80, 255);
    self->track_color = grl_color_new (60, 60, 60, 255);
    self->fill_color = grl_color_new (50, 200, 100, 255);
    self->arc_width = 20.0f;
    self->show_value = TRUE;
    self->value_format = g_strdup ("%.1f");
    self->show_ticks = TRUE;
    self->tick_count = 10;
    self->zones = g_ptr_array_new_with_free_func (gauge_zone_free);
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgGaugeChart2D *
lrg_gauge_chart2d_new (void)
{
    return g_object_new (LRG_TYPE_GAUGE_CHART2D, NULL);
}

LrgGaugeChart2D *
lrg_gauge_chart2d_new_with_size (gfloat width,
                                 gfloat height)
{
    return g_object_new (LRG_TYPE_GAUGE_CHART2D,
                         "width", width,
                         "height", height,
                         NULL);
}

/* ==========================================================================
 * Value
 * ========================================================================== */

gdouble
lrg_gauge_chart2d_get_value (LrgGaugeChart2D *self)
{
    g_return_val_if_fail (LRG_IS_GAUGE_CHART2D (self), 0.0);
    return self->value;
}

void
lrg_gauge_chart2d_set_value (LrgGaugeChart2D *self,
                             gdouble          value)
{
    g_return_if_fail (LRG_IS_GAUGE_CHART2D (self));

    if (self->value == value)
        return;

    self->value = value;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VALUE]);
}

gdouble
lrg_gauge_chart2d_get_min_value (LrgGaugeChart2D *self)
{
    g_return_val_if_fail (LRG_IS_GAUGE_CHART2D (self), 0.0);
    return self->min_value;
}

void
lrg_gauge_chart2d_set_min_value (LrgGaugeChart2D *self,
                                 gdouble          min)
{
    g_return_if_fail (LRG_IS_GAUGE_CHART2D (self));

    if (self->min_value == min)
        return;

    self->min_value = min;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_VALUE]);
}

gdouble
lrg_gauge_chart2d_get_max_value (LrgGaugeChart2D *self)
{
    g_return_val_if_fail (LRG_IS_GAUGE_CHART2D (self), 0.0);
    return self->max_value;
}

void
lrg_gauge_chart2d_set_max_value (LrgGaugeChart2D *self,
                                 gdouble          max)
{
    g_return_if_fail (LRG_IS_GAUGE_CHART2D (self));

    if (self->max_value == max)
        return;

    self->max_value = max;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_VALUE]);
}

/* ==========================================================================
 * Style
 * ========================================================================== */

LrgChartGaugeStyle
lrg_gauge_chart2d_get_style (LrgGaugeChart2D *self)
{
    g_return_val_if_fail (LRG_IS_GAUGE_CHART2D (self), LRG_CHART_GAUGE_NEEDLE);
    return self->style;
}

void
lrg_gauge_chart2d_set_style (LrgGaugeChart2D    *self,
                             LrgChartGaugeStyle  style)
{
    g_return_if_fail (LRG_IS_GAUGE_CHART2D (self));

    if (self->style == style)
        return;

    self->style = style;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STYLE]);
}

gfloat
lrg_gauge_chart2d_get_start_angle (LrgGaugeChart2D *self)
{
    g_return_val_if_fail (LRG_IS_GAUGE_CHART2D (self), 0.0f);
    return self->start_angle;
}

void
lrg_gauge_chart2d_set_start_angle (LrgGaugeChart2D *self,
                                   gfloat           angle)
{
    g_return_if_fail (LRG_IS_GAUGE_CHART2D (self));

    if (self->start_angle == angle)
        return;

    self->start_angle = angle;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_START_ANGLE]);
}

gfloat
lrg_gauge_chart2d_get_sweep_angle (LrgGaugeChart2D *self)
{
    g_return_val_if_fail (LRG_IS_GAUGE_CHART2D (self), 0.0f);
    return self->sweep_angle;
}

void
lrg_gauge_chart2d_set_sweep_angle (LrgGaugeChart2D *self,
                                   gfloat           angle)
{
    g_return_if_fail (LRG_IS_GAUGE_CHART2D (self));

    if (self->sweep_angle == angle)
        return;

    self->sweep_angle = angle;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SWEEP_ANGLE]);
}

/* ==========================================================================
 * Colors
 * ========================================================================== */

GrlColor *
lrg_gauge_chart2d_get_needle_color (LrgGaugeChart2D *self)
{
    g_return_val_if_fail (LRG_IS_GAUGE_CHART2D (self), NULL);
    return self->needle_color;
}

void
lrg_gauge_chart2d_set_needle_color (LrgGaugeChart2D *self,
                                    GrlColor        *color)
{
    g_return_if_fail (LRG_IS_GAUGE_CHART2D (self));

    g_clear_pointer (&self->needle_color, grl_color_free);
    if (color != NULL)
        self->needle_color = grl_color_copy (color);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NEEDLE_COLOR]);
}

GrlColor *
lrg_gauge_chart2d_get_track_color (LrgGaugeChart2D *self)
{
    g_return_val_if_fail (LRG_IS_GAUGE_CHART2D (self), NULL);
    return self->track_color;
}

void
lrg_gauge_chart2d_set_track_color (LrgGaugeChart2D *self,
                                   GrlColor        *color)
{
    g_return_if_fail (LRG_IS_GAUGE_CHART2D (self));

    g_clear_pointer (&self->track_color, grl_color_free);
    if (color != NULL)
        self->track_color = grl_color_copy (color);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRACK_COLOR]);
}

GrlColor *
lrg_gauge_chart2d_get_fill_color (LrgGaugeChart2D *self)
{
    g_return_val_if_fail (LRG_IS_GAUGE_CHART2D (self), NULL);
    return self->fill_color;
}

void
lrg_gauge_chart2d_set_fill_color (LrgGaugeChart2D *self,
                                  GrlColor        *color)
{
    g_return_if_fail (LRG_IS_GAUGE_CHART2D (self));

    g_clear_pointer (&self->fill_color, grl_color_free);
    if (color != NULL)
        self->fill_color = grl_color_copy (color);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILL_COLOR]);
}

/* ==========================================================================
 * Display Options
 * ========================================================================== */

gfloat
lrg_gauge_chart2d_get_arc_width (LrgGaugeChart2D *self)
{
    g_return_val_if_fail (LRG_IS_GAUGE_CHART2D (self), 0.0f);
    return self->arc_width;
}

void
lrg_gauge_chart2d_set_arc_width (LrgGaugeChart2D *self,
                                 gfloat           width)
{
    g_return_if_fail (LRG_IS_GAUGE_CHART2D (self));

    if (self->arc_width == width)
        return;

    self->arc_width = width;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ARC_WIDTH]);
}

gboolean
lrg_gauge_chart2d_get_show_value (LrgGaugeChart2D *self)
{
    g_return_val_if_fail (LRG_IS_GAUGE_CHART2D (self), FALSE);
    return self->show_value;
}

void
lrg_gauge_chart2d_set_show_value (LrgGaugeChart2D *self,
                                  gboolean         show)
{
    g_return_if_fail (LRG_IS_GAUGE_CHART2D (self));

    show = !!show;

    if (self->show_value == show)
        return;

    self->show_value = show;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_VALUE]);
}

const gchar *
lrg_gauge_chart2d_get_value_format (LrgGaugeChart2D *self)
{
    g_return_val_if_fail (LRG_IS_GAUGE_CHART2D (self), NULL);
    return self->value_format;
}

void
lrg_gauge_chart2d_set_value_format (LrgGaugeChart2D *self,
                                    const gchar     *format)
{
    g_return_if_fail (LRG_IS_GAUGE_CHART2D (self));

    if (g_strcmp0 (self->value_format, format) == 0)
        return;

    g_free (self->value_format);
    self->value_format = g_strdup (format);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VALUE_FORMAT]);
}

gboolean
lrg_gauge_chart2d_get_show_ticks (LrgGaugeChart2D *self)
{
    g_return_val_if_fail (LRG_IS_GAUGE_CHART2D (self), FALSE);
    return self->show_ticks;
}

void
lrg_gauge_chart2d_set_show_ticks (LrgGaugeChart2D *self,
                                  gboolean         show)
{
    g_return_if_fail (LRG_IS_GAUGE_CHART2D (self));

    show = !!show;

    if (self->show_ticks == show)
        return;

    self->show_ticks = show;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_TICKS]);
}

guint
lrg_gauge_chart2d_get_tick_count (LrgGaugeChart2D *self)
{
    g_return_val_if_fail (LRG_IS_GAUGE_CHART2D (self), 0);
    return self->tick_count;
}

void
lrg_gauge_chart2d_set_tick_count (LrgGaugeChart2D *self,
                                  guint            count)
{
    g_return_if_fail (LRG_IS_GAUGE_CHART2D (self));

    if (self->tick_count == count)
        return;

    self->tick_count = count;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TICK_COUNT]);
}

/* ==========================================================================
 * Color Zones
 * ========================================================================== */

void
lrg_gauge_chart2d_add_zone (LrgGaugeChart2D *self,
                            gdouble          start,
                            gdouble          end,
                            GrlColor        *color)
{
    GaugeZone *zone;

    g_return_if_fail (LRG_IS_GAUGE_CHART2D (self));

    zone = gauge_zone_new (start, end, color);
    g_ptr_array_add (self->zones, zone);
}

void
lrg_gauge_chart2d_clear_zones (LrgGaugeChart2D *self)
{
    g_return_if_fail (LRG_IS_GAUGE_CHART2D (self));

    g_ptr_array_set_size (self->zones, 0);
}
