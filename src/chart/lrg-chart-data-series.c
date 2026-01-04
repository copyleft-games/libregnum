/* lrg-chart-data-series.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-chart-data-series.h"

/* ==========================================================================
 * Default Colors
 * ========================================================================== */

static const GrlColor DEFAULT_SERIES_COLOR = { 100, 149, 237, 255 }; /* Cornflower blue */

/* ==========================================================================
 * Structure Definition
 * ========================================================================== */

struct _LrgChartDataSeries
{
    GObject parent_instance;

    gchar            *name;
    GrlColor          color;
    gfloat            line_width;
    LrgChartLineStyle line_style;
    LrgChartMarker    marker;
    gfloat            marker_size;
    gboolean          visible;
    gboolean          show_in_legend;

    GPtrArray        *points;  /* Array of LrgChartDataPoint* */
};

G_DEFINE_TYPE (LrgChartDataSeries, lrg_chart_data_series, G_TYPE_OBJECT)

/* ==========================================================================
 * Signals
 * ========================================================================== */

enum
{
    SIGNAL_POINT_ADDED,
    SIGNAL_POINT_REMOVED,
    SIGNAL_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Properties
 * ========================================================================== */

enum
{
    PROP_0,
    PROP_NAME,
    PROP_COLOR,
    PROP_LINE_WIDTH,
    PROP_LINE_STYLE,
    PROP_MARKER,
    PROP_MARKER_SIZE,
    PROP_VISIBLE,
    PROP_SHOW_IN_LEGEND,
    PROP_POINT_COUNT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

static void
lrg_chart_data_series_emit_changed (LrgChartDataSeries *self)
{
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_chart_data_series_finalize (GObject *object)
{
    LrgChartDataSeries *self = LRG_CHART_DATA_SERIES (object);

    g_free (self->name);
    g_ptr_array_unref (self->points);

    G_OBJECT_CLASS (lrg_chart_data_series_parent_class)->finalize (object);
}

static void
lrg_chart_data_series_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
    LrgChartDataSeries *self = LRG_CHART_DATA_SERIES (object);

    switch (prop_id)
    {
    case PROP_NAME:
        g_value_set_string (value, self->name);
        break;
    case PROP_COLOR:
        g_value_set_boxed (value, &self->color);
        break;
    case PROP_LINE_WIDTH:
        g_value_set_float (value, self->line_width);
        break;
    case PROP_LINE_STYLE:
        g_value_set_enum (value, self->line_style);
        break;
    case PROP_MARKER:
        g_value_set_enum (value, self->marker);
        break;
    case PROP_MARKER_SIZE:
        g_value_set_float (value, self->marker_size);
        break;
    case PROP_VISIBLE:
        g_value_set_boolean (value, self->visible);
        break;
    case PROP_SHOW_IN_LEGEND:
        g_value_set_boolean (value, self->show_in_legend);
        break;
    case PROP_POINT_COUNT:
        g_value_set_uint (value, self->points->len);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_chart_data_series_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
    LrgChartDataSeries *self = LRG_CHART_DATA_SERIES (object);

    switch (prop_id)
    {
    case PROP_NAME:
        lrg_chart_data_series_set_name (self, g_value_get_string (value));
        break;
    case PROP_COLOR:
        lrg_chart_data_series_set_color (self, g_value_get_boxed (value));
        break;
    case PROP_LINE_WIDTH:
        lrg_chart_data_series_set_line_width (self, g_value_get_float (value));
        break;
    case PROP_LINE_STYLE:
        lrg_chart_data_series_set_line_style (self, g_value_get_enum (value));
        break;
    case PROP_MARKER:
        lrg_chart_data_series_set_marker (self, g_value_get_enum (value));
        break;
    case PROP_MARKER_SIZE:
        lrg_chart_data_series_set_marker_size (self, g_value_get_float (value));
        break;
    case PROP_VISIBLE:
        lrg_chart_data_series_set_visible (self, g_value_get_boolean (value));
        break;
    case PROP_SHOW_IN_LEGEND:
        lrg_chart_data_series_set_show_in_legend (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_chart_data_series_class_init (LrgChartDataSeriesClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_chart_data_series_finalize;
    object_class->get_property = lrg_chart_data_series_get_property;
    object_class->set_property = lrg_chart_data_series_set_property;

    /* Properties */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Series name for legend",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_COLOR] =
        g_param_spec_boxed ("color",
                            "Color",
                            "Series color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_LINE_WIDTH] =
        g_param_spec_float ("line-width",
                            "Line Width",
                            "Line width for line charts",
                            0.0f, 100.0f, 2.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_LINE_STYLE] =
        g_param_spec_enum ("line-style",
                           "Line Style",
                           "Line style",
                           LRG_TYPE_CHART_LINE_STYLE,
                           LRG_CHART_LINE_SOLID,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_MARKER] =
        g_param_spec_enum ("marker",
                           "Marker",
                           "Marker style",
                           LRG_TYPE_CHART_MARKER,
                           LRG_CHART_MARKER_NONE,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_MARKER_SIZE] =
        g_param_spec_float ("marker-size",
                            "Marker Size",
                            "Marker size",
                            0.0f, 100.0f, 6.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_VISIBLE] =
        g_param_spec_boolean ("visible",
                              "Visible",
                              "Whether the series is visible",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_IN_LEGEND] =
        g_param_spec_boolean ("show-in-legend",
                              "Show in Legend",
                              "Whether to show in legend",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_POINT_COUNT] =
        g_param_spec_uint ("point-count",
                           "Point Count",
                           "Number of data points",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /* Signals */
    /**
     * LrgChartDataSeries::point-added:
     * @self: the series
     * @index: the index of the added point
     *
     * Emitted when a point is added to the series.
     */
    signals[SIGNAL_POINT_ADDED] =
        g_signal_new ("point-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_UINT);

    /**
     * LrgChartDataSeries::point-removed:
     * @self: the series
     * @index: the index of the removed point
     *
     * Emitted when a point is removed from the series.
     */
    signals[SIGNAL_POINT_REMOVED] =
        g_signal_new ("point-removed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_UINT);

    /**
     * LrgChartDataSeries::changed:
     * @self: the series
     *
     * Emitted when the series data or styling changes.
     */
    signals[SIGNAL_CHANGED] =
        g_signal_new ("changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_chart_data_series_init (LrgChartDataSeries *self)
{
    self->name = NULL;
    self->color = DEFAULT_SERIES_COLOR;
    self->line_width = 2.0f;
    self->line_style = LRG_CHART_LINE_SOLID;
    self->marker = LRG_CHART_MARKER_NONE;
    self->marker_size = 6.0f;
    self->visible = TRUE;
    self->show_in_legend = TRUE;

    /* Create array with free function for owned data points */
    self->points = g_ptr_array_new_with_free_func ((GDestroyNotify)lrg_chart_data_point_free);
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgChartDataSeries *
lrg_chart_data_series_new (const gchar *name)
{
    LrgChartDataSeries *self;

    self = g_object_new (LRG_TYPE_CHART_DATA_SERIES, NULL);

    if (name != NULL)
        self->name = g_strdup (name);

    return self;
}

LrgChartDataSeries *
lrg_chart_data_series_new_with_color (const gchar    *name,
                                       const GrlColor *color)
{
    LrgChartDataSeries *self;

    g_return_val_if_fail (color != NULL, NULL);

    self = lrg_chart_data_series_new (name);
    self->color = *color;

    return self;
}

/* ==========================================================================
 * Name
 * ========================================================================== */

const gchar *
lrg_chart_data_series_get_name (LrgChartDataSeries *self)
{
    g_return_val_if_fail (LRG_IS_CHART_DATA_SERIES (self), NULL);
    return self->name;
}

void
lrg_chart_data_series_set_name (LrgChartDataSeries *self,
                                 const gchar        *name)
{
    g_return_if_fail (LRG_IS_CHART_DATA_SERIES (self));

    if (g_strcmp0 (self->name, name) == 0)
        return;

    g_free (self->name);
    self->name = g_strdup (name);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
    lrg_chart_data_series_emit_changed (self);
}

/* ==========================================================================
 * Styling
 * ========================================================================== */

const GrlColor *
lrg_chart_data_series_get_color (LrgChartDataSeries *self)
{
    g_return_val_if_fail (LRG_IS_CHART_DATA_SERIES (self), NULL);
    return &self->color;
}

void
lrg_chart_data_series_set_color (LrgChartDataSeries *self,
                                  const GrlColor     *color)
{
    g_return_if_fail (LRG_IS_CHART_DATA_SERIES (self));
    g_return_if_fail (color != NULL);

    if (self->color.r == color->r &&
        self->color.g == color->g &&
        self->color.b == color->b &&
        self->color.a == color->a)
        return;

    self->color = *color;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR]);
    lrg_chart_data_series_emit_changed (self);
}

gfloat
lrg_chart_data_series_get_line_width (LrgChartDataSeries *self)
{
    g_return_val_if_fail (LRG_IS_CHART_DATA_SERIES (self), 0.0f);
    return self->line_width;
}

void
lrg_chart_data_series_set_line_width (LrgChartDataSeries *self,
                                       gfloat              width)
{
    g_return_if_fail (LRG_IS_CHART_DATA_SERIES (self));

    if (self->line_width == width)
        return;

    self->line_width = width;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LINE_WIDTH]);
    lrg_chart_data_series_emit_changed (self);
}

LrgChartLineStyle
lrg_chart_data_series_get_line_style (LrgChartDataSeries *self)
{
    g_return_val_if_fail (LRG_IS_CHART_DATA_SERIES (self), LRG_CHART_LINE_SOLID);
    return self->line_style;
}

void
lrg_chart_data_series_set_line_style (LrgChartDataSeries *self,
                                       LrgChartLineStyle   style)
{
    g_return_if_fail (LRG_IS_CHART_DATA_SERIES (self));

    if (self->line_style == style)
        return;

    self->line_style = style;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LINE_STYLE]);
    lrg_chart_data_series_emit_changed (self);
}

LrgChartMarker
lrg_chart_data_series_get_marker (LrgChartDataSeries *self)
{
    g_return_val_if_fail (LRG_IS_CHART_DATA_SERIES (self), LRG_CHART_MARKER_NONE);
    return self->marker;
}

void
lrg_chart_data_series_set_marker (LrgChartDataSeries *self,
                                   LrgChartMarker      marker)
{
    g_return_if_fail (LRG_IS_CHART_DATA_SERIES (self));

    if (self->marker == marker)
        return;

    self->marker = marker;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARKER]);
    lrg_chart_data_series_emit_changed (self);
}

gfloat
lrg_chart_data_series_get_marker_size (LrgChartDataSeries *self)
{
    g_return_val_if_fail (LRG_IS_CHART_DATA_SERIES (self), 0.0f);
    return self->marker_size;
}

void
lrg_chart_data_series_set_marker_size (LrgChartDataSeries *self,
                                        gfloat              size)
{
    g_return_if_fail (LRG_IS_CHART_DATA_SERIES (self));

    if (self->marker_size == size)
        return;

    self->marker_size = size;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARKER_SIZE]);
    lrg_chart_data_series_emit_changed (self);
}

/* ==========================================================================
 * Visibility
 * ========================================================================== */

gboolean
lrg_chart_data_series_get_visible (LrgChartDataSeries *self)
{
    g_return_val_if_fail (LRG_IS_CHART_DATA_SERIES (self), FALSE);
    return self->visible;
}

void
lrg_chart_data_series_set_visible (LrgChartDataSeries *self,
                                    gboolean            visible)
{
    g_return_if_fail (LRG_IS_CHART_DATA_SERIES (self));

    visible = !!visible;

    if (self->visible == visible)
        return;

    self->visible = visible;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE]);
    lrg_chart_data_series_emit_changed (self);
}

gboolean
lrg_chart_data_series_get_show_in_legend (LrgChartDataSeries *self)
{
    g_return_val_if_fail (LRG_IS_CHART_DATA_SERIES (self), FALSE);
    return self->show_in_legend;
}

void
lrg_chart_data_series_set_show_in_legend (LrgChartDataSeries *self,
                                           gboolean            show)
{
    g_return_if_fail (LRG_IS_CHART_DATA_SERIES (self));

    show = !!show;

    if (self->show_in_legend == show)
        return;

    self->show_in_legend = show;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_IN_LEGEND]);
    lrg_chart_data_series_emit_changed (self);
}

/* ==========================================================================
 * Data Points
 * ========================================================================== */

guint
lrg_chart_data_series_get_point_count (LrgChartDataSeries *self)
{
    g_return_val_if_fail (LRG_IS_CHART_DATA_SERIES (self), 0);
    return self->points->len;
}

const LrgChartDataPoint *
lrg_chart_data_series_get_point (LrgChartDataSeries *self,
                                  guint               index)
{
    g_return_val_if_fail (LRG_IS_CHART_DATA_SERIES (self), NULL);
    g_return_val_if_fail (index < self->points->len, NULL);

    return g_ptr_array_index (self->points, index);
}

guint
lrg_chart_data_series_add_point (LrgChartDataSeries *self,
                                  gdouble             x,
                                  gdouble             y)
{
    LrgChartDataPoint *point;
    guint index;

    g_return_val_if_fail (LRG_IS_CHART_DATA_SERIES (self), 0);

    point = lrg_chart_data_point_new (x, y);
    g_ptr_array_add (self->points, point);

    index = self->points->len - 1;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POINT_COUNT]);
    g_signal_emit (self, signals[SIGNAL_POINT_ADDED], 0, index);
    lrg_chart_data_series_emit_changed (self);

    return index;
}

guint
lrg_chart_data_series_add_point_labeled (LrgChartDataSeries *self,
                                          gdouble             x,
                                          gdouble             y,
                                          const gchar        *label)
{
    LrgChartDataPoint *point;
    guint index;

    g_return_val_if_fail (LRG_IS_CHART_DATA_SERIES (self), 0);

    point = lrg_chart_data_point_new_labeled (x, y, label);
    g_ptr_array_add (self->points, point);

    index = self->points->len - 1;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POINT_COUNT]);
    g_signal_emit (self, signals[SIGNAL_POINT_ADDED], 0, index);
    lrg_chart_data_series_emit_changed (self);

    return index;
}

guint
lrg_chart_data_series_add_point_full (LrgChartDataSeries *self,
                                       LrgChartDataPoint  *point)
{
    guint index;

    g_return_val_if_fail (LRG_IS_CHART_DATA_SERIES (self), 0);
    g_return_val_if_fail (point != NULL, 0);

    /* Takes ownership of point */
    g_ptr_array_add (self->points, point);

    index = self->points->len - 1;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POINT_COUNT]);
    g_signal_emit (self, signals[SIGNAL_POINT_ADDED], 0, index);
    lrg_chart_data_series_emit_changed (self);

    return index;
}

void
lrg_chart_data_series_insert_point (LrgChartDataSeries *self,
                                     guint               index,
                                     gdouble             x,
                                     gdouble             y)
{
    LrgChartDataPoint *point;

    g_return_if_fail (LRG_IS_CHART_DATA_SERIES (self));
    g_return_if_fail (index <= self->points->len);

    point = lrg_chart_data_point_new (x, y);
    g_ptr_array_insert (self->points, index, point);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POINT_COUNT]);
    g_signal_emit (self, signals[SIGNAL_POINT_ADDED], 0, index);
    lrg_chart_data_series_emit_changed (self);
}

gboolean
lrg_chart_data_series_remove_point (LrgChartDataSeries *self,
                                     guint               index)
{
    g_return_val_if_fail (LRG_IS_CHART_DATA_SERIES (self), FALSE);

    if (index >= self->points->len)
        return FALSE;

    g_ptr_array_remove_index (self->points, index);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POINT_COUNT]);
    g_signal_emit (self, signals[SIGNAL_POINT_REMOVED], 0, index);
    lrg_chart_data_series_emit_changed (self);

    return TRUE;
}

void
lrg_chart_data_series_clear (LrgChartDataSeries *self)
{
    guint old_len;

    g_return_if_fail (LRG_IS_CHART_DATA_SERIES (self));

    old_len = self->points->len;
    if (old_len == 0)
        return;

    g_ptr_array_set_size (self->points, 0);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POINT_COUNT]);
    lrg_chart_data_series_emit_changed (self);
}

void
lrg_chart_data_series_set_point_value (LrgChartDataSeries *self,
                                        guint               index,
                                        gdouble             x,
                                        gdouble             y)
{
    LrgChartDataPoint *point;

    g_return_if_fail (LRG_IS_CHART_DATA_SERIES (self));
    g_return_if_fail (index < self->points->len);

    point = g_ptr_array_index (self->points, index);
    lrg_chart_data_point_set_x (point, x);
    lrg_chart_data_point_set_y (point, y);

    lrg_chart_data_series_emit_changed (self);
}

/* ==========================================================================
 * Data Range
 * ========================================================================== */

void
lrg_chart_data_series_get_x_range (LrgChartDataSeries *self,
                                    gdouble            *min,
                                    gdouble            *max)
{
    gdouble x_min = G_MAXDOUBLE;
    gdouble x_max = -G_MAXDOUBLE;
    guint i;

    g_return_if_fail (LRG_IS_CHART_DATA_SERIES (self));

    if (self->points->len == 0)
    {
        if (min != NULL) *min = 0.0;
        if (max != NULL) *max = 0.0;
        return;
    }

    for (i = 0; i < self->points->len; i++)
    {
        const LrgChartDataPoint *point = g_ptr_array_index (self->points, i);
        gdouble x = lrg_chart_data_point_get_x (point);

        if (x < x_min) x_min = x;
        if (x > x_max) x_max = x;
    }

    if (min != NULL) *min = x_min;
    if (max != NULL) *max = x_max;
}

void
lrg_chart_data_series_get_y_range (LrgChartDataSeries *self,
                                    gdouble            *min,
                                    gdouble            *max)
{
    gdouble y_min = G_MAXDOUBLE;
    gdouble y_max = -G_MAXDOUBLE;
    guint i;

    g_return_if_fail (LRG_IS_CHART_DATA_SERIES (self));

    if (self->points->len == 0)
    {
        if (min != NULL) *min = 0.0;
        if (max != NULL) *max = 0.0;
        return;
    }

    for (i = 0; i < self->points->len; i++)
    {
        const LrgChartDataPoint *point = g_ptr_array_index (self->points, i);
        gdouble y = lrg_chart_data_point_get_y (point);

        if (y < y_min) y_min = y;
        if (y > y_max) y_max = y;
    }

    if (min != NULL) *min = y_min;
    if (max != NULL) *max = y_max;
}

gdouble
lrg_chart_data_series_get_y_sum (LrgChartDataSeries *self)
{
    gdouble sum = 0.0;
    guint i;

    g_return_val_if_fail (LRG_IS_CHART_DATA_SERIES (self), 0.0);

    for (i = 0; i < self->points->len; i++)
    {
        const LrgChartDataPoint *point = g_ptr_array_index (self->points, i);
        sum += lrg_chart_data_point_get_y (point);
    }

    return sum;
}
