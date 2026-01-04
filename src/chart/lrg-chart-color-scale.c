/* lrg-chart-color-scale.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-chart-color-scale.h"
#include "../lrg-log.h"

/*
 * Color stop structure for gradient mapping.
 */
typedef struct
{
    gdouble   position;  /* 0.0 to 1.0 */
    GrlColor *color;
} ColorStop;

struct _LrgChartColorScale
{
    GObject parent_instance;

    GPtrArray *stops;       /* Array of ColorStop pointers */
    gdouble    min_value;   /* Data minimum */
    gdouble    max_value;   /* Data maximum */
    gboolean   discrete;    /* Use discrete steps vs interpolation */
};

enum
{
    PROP_0,
    PROP_MIN_VALUE,
    PROP_MAX_VALUE,
    PROP_DISCRETE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (LrgChartColorScale, lrg_chart_color_scale, G_TYPE_OBJECT)

/* ==========================================================================
 * Internal Helpers
 * ========================================================================== */

static ColorStop *
color_stop_new (gdouble   position,
                GrlColor *color)
{
    ColorStop *stop;

    stop = g_new0 (ColorStop, 1);
    stop->position = CLAMP (position, 0.0, 1.0);
    stop->color = grl_color_copy (color);

    return stop;
}

static void
color_stop_free (gpointer data)
{
    ColorStop *stop = data;

    if (stop != NULL)
    {
        g_clear_pointer (&stop->color, grl_color_free);
        g_free (stop);
    }
}

static gint
color_stop_compare (gconstpointer a,
                    gconstpointer b)
{
    const ColorStop *stop_a = *(const ColorStop **)a;
    const ColorStop *stop_b = *(const ColorStop **)b;

    if (stop_a->position < stop_b->position)
        return -1;
    else if (stop_a->position > stop_b->position)
        return 1;
    return 0;
}

static guint8
lerp_u8 (guint8  a,
         guint8  b,
         gdouble t)
{
    return (guint8)(a + (b - a) * t);
}

static GrlColor *
interpolate_color (GrlColor *c1,
                   GrlColor *c2,
                   gdouble   t)
{
    guint8 r1, g1, b1, a1;
    guint8 r2, g2, b2, a2;

    r1 = grl_color_get_r (c1);
    g1 = grl_color_get_g (c1);
    b1 = grl_color_get_b (c1);
    a1 = grl_color_get_a (c1);

    r2 = grl_color_get_r (c2);
    g2 = grl_color_get_g (c2);
    b2 = grl_color_get_b (c2);
    a2 = grl_color_get_a (c2);

    return grl_color_new (
        lerp_u8 (r1, r2, t),
        lerp_u8 (g1, g2, t),
        lerp_u8 (b1, b2, t),
        lerp_u8 (a1, a2, t)
    );
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_chart_color_scale_finalize (GObject *object)
{
    LrgChartColorScale *self = LRG_CHART_COLOR_SCALE (object);

    g_clear_pointer (&self->stops, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_chart_color_scale_parent_class)->finalize (object);
}

static void
lrg_chart_color_scale_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    LrgChartColorScale *self = LRG_CHART_COLOR_SCALE (object);

    switch (prop_id)
    {
    case PROP_MIN_VALUE:
        g_value_set_double (value, self->min_value);
        break;
    case PROP_MAX_VALUE:
        g_value_set_double (value, self->max_value);
        break;
    case PROP_DISCRETE:
        g_value_set_boolean (value, self->discrete);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_chart_color_scale_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
    LrgChartColorScale *self = LRG_CHART_COLOR_SCALE (object);

    switch (prop_id)
    {
    case PROP_MIN_VALUE:
        lrg_chart_color_scale_set_min_value (self, g_value_get_double (value));
        break;
    case PROP_MAX_VALUE:
        lrg_chart_color_scale_set_max_value (self, g_value_get_double (value));
        break;
    case PROP_DISCRETE:
        lrg_chart_color_scale_set_discrete (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_chart_color_scale_class_init (LrgChartColorScaleClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_chart_color_scale_finalize;
    object_class->get_property = lrg_chart_color_scale_get_property;
    object_class->set_property = lrg_chart_color_scale_set_property;

    /**
     * LrgChartColorScale:min-value:
     *
     * Minimum data value (maps to position 0.0).
     */
    properties[PROP_MIN_VALUE] =
        g_param_spec_double ("min-value",
                             "Min Value",
                             "Minimum data value",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
                             G_PARAM_READWRITE |
                             G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartColorScale:max-value:
     *
     * Maximum data value (maps to position 1.0).
     */
    properties[PROP_MAX_VALUE] =
        g_param_spec_double ("max-value",
                             "Max Value",
                             "Maximum data value",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 1.0,
                             G_PARAM_READWRITE |
                             G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartColorScale:discrete:
     *
     * Whether to use discrete steps instead of interpolation.
     */
    properties[PROP_DISCRETE] =
        g_param_spec_boolean ("discrete",
                              "Discrete",
                              "Use discrete color steps",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_chart_color_scale_init (LrgChartColorScale *self)
{
    self->stops = g_ptr_array_new_with_free_func (color_stop_free);
    self->min_value = 0.0;
    self->max_value = 1.0;
    self->discrete = FALSE;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgChartColorScale *
lrg_chart_color_scale_new (void)
{
    return g_object_new (LRG_TYPE_CHART_COLOR_SCALE, NULL);
}

LrgChartColorScale *
lrg_chart_color_scale_new_gradient (GrlColor *min_color,
                                    GrlColor *max_color)
{
    LrgChartColorScale *self;

    g_return_val_if_fail (min_color != NULL, NULL);
    g_return_val_if_fail (max_color != NULL, NULL);

    self = lrg_chart_color_scale_new ();
    lrg_chart_color_scale_add_stop (self, 0.0, min_color);
    lrg_chart_color_scale_add_stop (self, 1.0, max_color);

    return self;
}

LrgChartColorScale *
lrg_chart_color_scale_new_heat (void)
{
    LrgChartColorScale *self;
    g_autoptr(GrlColor) blue = NULL;
    g_autoptr(GrlColor) cyan = NULL;
    g_autoptr(GrlColor) green = NULL;
    g_autoptr(GrlColor) yellow = NULL;
    g_autoptr(GrlColor) red = NULL;

    self = lrg_chart_color_scale_new ();

    blue = grl_color_new (0, 0, 255, 255);
    cyan = grl_color_new (0, 255, 255, 255);
    green = grl_color_new (0, 255, 0, 255);
    yellow = grl_color_new (255, 255, 0, 255);
    red = grl_color_new (255, 0, 0, 255);

    lrg_chart_color_scale_add_stop (self, 0.0, blue);
    lrg_chart_color_scale_add_stop (self, 0.25, cyan);
    lrg_chart_color_scale_add_stop (self, 0.5, green);
    lrg_chart_color_scale_add_stop (self, 0.75, yellow);
    lrg_chart_color_scale_add_stop (self, 1.0, red);

    return self;
}

LrgChartColorScale *
lrg_chart_color_scale_new_cool (void)
{
    LrgChartColorScale *self;
    g_autoptr(GrlColor) purple = NULL;
    g_autoptr(GrlColor) blue = NULL;
    g_autoptr(GrlColor) cyan = NULL;

    self = lrg_chart_color_scale_new ();

    purple = grl_color_new (128, 0, 255, 255);
    blue = grl_color_new (0, 128, 255, 255);
    cyan = grl_color_new (0, 255, 255, 255);

    lrg_chart_color_scale_add_stop (self, 0.0, purple);
    lrg_chart_color_scale_add_stop (self, 0.5, blue);
    lrg_chart_color_scale_add_stop (self, 1.0, cyan);

    return self;
}

LrgChartColorScale *
lrg_chart_color_scale_new_viridis (void)
{
    LrgChartColorScale *self;
    g_autoptr(GrlColor) dark_purple = NULL;
    g_autoptr(GrlColor) blue = NULL;
    g_autoptr(GrlColor) teal = NULL;
    g_autoptr(GrlColor) green = NULL;
    g_autoptr(GrlColor) yellow = NULL;

    self = lrg_chart_color_scale_new ();

    /* Viridis-inspired palette */
    dark_purple = grl_color_new (68, 1, 84, 255);
    blue = grl_color_new (59, 82, 139, 255);
    teal = grl_color_new (33, 145, 140, 255);
    green = grl_color_new (94, 201, 98, 255);
    yellow = grl_color_new (253, 231, 37, 255);

    lrg_chart_color_scale_add_stop (self, 0.0, dark_purple);
    lrg_chart_color_scale_add_stop (self, 0.25, blue);
    lrg_chart_color_scale_add_stop (self, 0.5, teal);
    lrg_chart_color_scale_add_stop (self, 0.75, green);
    lrg_chart_color_scale_add_stop (self, 1.0, yellow);

    return self;
}

/* ==========================================================================
 * Color Stops
 * ========================================================================== */

void
lrg_chart_color_scale_add_stop (LrgChartColorScale *self,
                                gdouble             position,
                                GrlColor           *color)
{
    ColorStop *stop;

    g_return_if_fail (LRG_IS_CHART_COLOR_SCALE (self));
    g_return_if_fail (color != NULL);

    stop = color_stop_new (position, color);
    g_ptr_array_add (self->stops, stop);

    /* Keep sorted by position */
    g_ptr_array_sort (self->stops, color_stop_compare);
}

void
lrg_chart_color_scale_clear_stops (LrgChartColorScale *self)
{
    g_return_if_fail (LRG_IS_CHART_COLOR_SCALE (self));

    g_ptr_array_set_size (self->stops, 0);
}

guint
lrg_chart_color_scale_get_stop_count (LrgChartColorScale *self)
{
    g_return_val_if_fail (LRG_IS_CHART_COLOR_SCALE (self), 0);

    return self->stops->len;
}

/* ==========================================================================
 * Value Range
 * ========================================================================== */

gdouble
lrg_chart_color_scale_get_min_value (LrgChartColorScale *self)
{
    g_return_val_if_fail (LRG_IS_CHART_COLOR_SCALE (self), 0.0);

    return self->min_value;
}

void
lrg_chart_color_scale_set_min_value (LrgChartColorScale *self,
                                     gdouble             min)
{
    g_return_if_fail (LRG_IS_CHART_COLOR_SCALE (self));

    if (self->min_value != min)
    {
        self->min_value = min;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_VALUE]);
    }
}

gdouble
lrg_chart_color_scale_get_max_value (LrgChartColorScale *self)
{
    g_return_val_if_fail (LRG_IS_CHART_COLOR_SCALE (self), 1.0);

    return self->max_value;
}

void
lrg_chart_color_scale_set_max_value (LrgChartColorScale *self,
                                     gdouble             max)
{
    g_return_if_fail (LRG_IS_CHART_COLOR_SCALE (self));

    if (self->max_value != max)
    {
        self->max_value = max;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_VALUE]);
    }
}

void
lrg_chart_color_scale_set_range (LrgChartColorScale *self,
                                 gdouble             min,
                                 gdouble             max)
{
    g_return_if_fail (LRG_IS_CHART_COLOR_SCALE (self));

    lrg_chart_color_scale_set_min_value (self, min);
    lrg_chart_color_scale_set_max_value (self, max);
}

/* ==========================================================================
 * Color Mapping
 * ========================================================================== */

GrlColor *
lrg_chart_color_scale_get_color (LrgChartColorScale *self,
                                 gdouble             value)
{
    gdouble position;
    gdouble range;

    g_return_val_if_fail (LRG_IS_CHART_COLOR_SCALE (self), NULL);

    range = self->max_value - self->min_value;
    if (range <= 0.0)
        range = 1.0;

    /* Normalize value to 0.0-1.0 */
    position = (value - self->min_value) / range;
    position = CLAMP (position, 0.0, 1.0);

    return lrg_chart_color_scale_get_color_at (self, position);
}

GrlColor *
lrg_chart_color_scale_get_color_at (LrgChartColorScale *self,
                                    gdouble             position)
{
    guint i;
    ColorStop *prev_stop;
    ColorStop *next_stop;
    gdouble t;

    g_return_val_if_fail (LRG_IS_CHART_COLOR_SCALE (self), NULL);

    position = CLAMP (position, 0.0, 1.0);

    /* No stops - return gray */
    if (self->stops->len == 0)
        return grl_color_new (128, 128, 128, 255);

    /* Single stop - return that color */
    if (self->stops->len == 1)
    {
        ColorStop *stop = g_ptr_array_index (self->stops, 0);
        return grl_color_copy (stop->color);
    }

    /* Find surrounding stops */
    prev_stop = g_ptr_array_index (self->stops, 0);
    next_stop = g_ptr_array_index (self->stops, self->stops->len - 1);

    for (i = 0; i < self->stops->len - 1; i++)
    {
        ColorStop *s1 = g_ptr_array_index (self->stops, i);
        ColorStop *s2 = g_ptr_array_index (self->stops, i + 1);

        if (position >= s1->position && position <= s2->position)
        {
            prev_stop = s1;
            next_stop = s2;
            break;
        }
    }

    /* Handle edge cases */
    if (position <= prev_stop->position)
        return grl_color_copy (prev_stop->color);
    if (position >= next_stop->position)
        return grl_color_copy (next_stop->color);

    /* Discrete mode - snap to nearest */
    if (self->discrete)
    {
        gdouble mid = (prev_stop->position + next_stop->position) / 2.0;
        if (position < mid)
            return grl_color_copy (prev_stop->color);
        else
            return grl_color_copy (next_stop->color);
    }

    /* Linear interpolation */
    t = (position - prev_stop->position) / (next_stop->position - prev_stop->position);
    return interpolate_color (prev_stop->color, next_stop->color, t);
}

/* ==========================================================================
 * Display Options
 * ========================================================================== */

gboolean
lrg_chart_color_scale_get_discrete (LrgChartColorScale *self)
{
    g_return_val_if_fail (LRG_IS_CHART_COLOR_SCALE (self), FALSE);

    return self->discrete;
}

void
lrg_chart_color_scale_set_discrete (LrgChartColorScale *self,
                                    gboolean            discrete)
{
    g_return_if_fail (LRG_IS_CHART_COLOR_SCALE (self));

    discrete = !!discrete;
    if (self->discrete != discrete)
    {
        self->discrete = discrete;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DISCRETE]);
    }
}
