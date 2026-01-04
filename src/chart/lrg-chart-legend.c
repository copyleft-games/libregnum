/* lrg-chart-legend.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-chart-legend.h"
#include <graylib.h>

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_CHART
#include "../lrg-log.h"

struct _LrgChartLegend
{
    GObject parent_instance;

    gboolean              visible;
    LrgLegendPosition     position;
    LrgLegendOrientation  orientation;

    GrlColor             *background_color;
    GrlColor             *text_color;
    GrlColor             *border_color;

    gfloat                padding;
    gfloat                item_spacing;
    gfloat                symbol_size;
    gfloat                symbol_spacing;
    gfloat                border_width;

    gint                  font_size;
};

G_DEFINE_TYPE (LrgChartLegend, lrg_chart_legend, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_VISIBLE,
    PROP_POSITION,
    PROP_ORIENTATION,
    PROP_BACKGROUND_COLOR,
    PROP_TEXT_COLOR,
    PROP_BORDER_COLOR,
    PROP_PADDING,
    PROP_ITEM_SPACING,
    PROP_SYMBOL_SIZE,
    PROP_SYMBOL_SPACING,
    PROP_BORDER_WIDTH,
    PROP_FONT_SIZE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

static void
draw_series_symbol (LrgChartDataSeries *series,
                    gfloat              x,
                    gfloat              y,
                    gfloat              size)
{
    const GrlColor *color;
    LrgChartMarker marker;
    gfloat half_size;
    gfloat center_x;
    gfloat center_y;

    color = lrg_chart_data_series_get_color (series);
    marker = lrg_chart_data_series_get_marker (series);
    half_size = size / 2.0f;
    center_x = x + half_size;
    center_y = y + half_size;

    switch (marker)
    {
    case LRG_CHART_MARKER_NONE:
        /* Draw a filled rectangle for no-marker series (like bars) */
        {
            g_autoptr(GrlRectangle) rect = grl_rectangle_new (x, y, size, size);
            grl_draw_rectangle_rec (rect, color);
        }
        break;

    case LRG_CHART_MARKER_CIRCLE:
        grl_draw_circle (center_x, center_y, half_size, color);
        break;

    case LRG_CHART_MARKER_SQUARE:
        {
            g_autoptr(GrlRectangle) rect = grl_rectangle_new (x, y, size, size);
            grl_draw_rectangle_rec (rect, color);
        }
        break;

    case LRG_CHART_MARKER_DIAMOND:
        {
            g_autoptr(GrlVector2) v1 = grl_vector2_new (center_x, y);
            g_autoptr(GrlVector2) v2 = grl_vector2_new (x + size, center_y);
            g_autoptr(GrlVector2) v3 = grl_vector2_new (center_x, y + size);
            g_autoptr(GrlVector2) v4 = grl_vector2_new (x, center_y);

            grl_draw_triangle (v1, v2, v4, color);
            grl_draw_triangle (v2, v3, v4, color);
        }
        break;

    case LRG_CHART_MARKER_TRIANGLE:
        {
            g_autoptr(GrlVector2) v1 = grl_vector2_new (center_x, y);
            g_autoptr(GrlVector2) v2 = grl_vector2_new (x + size, y + size);
            g_autoptr(GrlVector2) v3 = grl_vector2_new (x, y + size);

            grl_draw_triangle (v1, v2, v3, color);
        }
        break;

    case LRG_CHART_MARKER_CROSS:
        {
            gfloat thickness = size * 0.2f;
            g_autoptr(GrlVector2) h_start = grl_vector2_new (x, center_y);
            g_autoptr(GrlVector2) h_end = grl_vector2_new (x + size, center_y);
            g_autoptr(GrlVector2) v_start = grl_vector2_new (center_x, y);
            g_autoptr(GrlVector2) v_end = grl_vector2_new (center_x, y + size);
            grl_draw_line_ex (h_start, h_end, thickness, color);
            grl_draw_line_ex (v_start, v_end, thickness, color);
        }
        break;

    case LRG_CHART_MARKER_X:
        {
            gfloat thickness = size * 0.2f;
            g_autoptr(GrlVector2) d1_start = grl_vector2_new (x, y);
            g_autoptr(GrlVector2) d1_end = grl_vector2_new (x + size, y + size);
            g_autoptr(GrlVector2) d2_start = grl_vector2_new (x, y + size);
            g_autoptr(GrlVector2) d2_end = grl_vector2_new (x + size, y);
            grl_draw_line_ex (d1_start, d1_end, thickness, color);
            grl_draw_line_ex (d2_start, d2_end, thickness, color);
        }
        break;

    default:
        {
            g_autoptr(GrlRectangle) rect = grl_rectangle_new (x, y, size, size);
            grl_draw_rectangle_rec (rect, color);
        }
        break;
    }
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_chart_legend_finalize (GObject *object)
{
    LrgChartLegend *self = LRG_CHART_LEGEND (object);

    g_clear_pointer (&self->background_color, grl_color_free);
    g_clear_pointer (&self->text_color, grl_color_free);
    g_clear_pointer (&self->border_color, grl_color_free);

    G_OBJECT_CLASS (lrg_chart_legend_parent_class)->finalize (object);
}

static void
lrg_chart_legend_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgChartLegend *self = LRG_CHART_LEGEND (object);

    switch (prop_id)
    {
    case PROP_VISIBLE:
        g_value_set_boolean (value, self->visible);
        break;
    case PROP_POSITION:
        g_value_set_enum (value, self->position);
        break;
    case PROP_ORIENTATION:
        g_value_set_enum (value, self->orientation);
        break;
    case PROP_BACKGROUND_COLOR:
        g_value_set_boxed (value, self->background_color);
        break;
    case PROP_TEXT_COLOR:
        g_value_set_boxed (value, self->text_color);
        break;
    case PROP_BORDER_COLOR:
        g_value_set_boxed (value, self->border_color);
        break;
    case PROP_PADDING:
        g_value_set_float (value, self->padding);
        break;
    case PROP_ITEM_SPACING:
        g_value_set_float (value, self->item_spacing);
        break;
    case PROP_SYMBOL_SIZE:
        g_value_set_float (value, self->symbol_size);
        break;
    case PROP_SYMBOL_SPACING:
        g_value_set_float (value, self->symbol_spacing);
        break;
    case PROP_BORDER_WIDTH:
        g_value_set_float (value, self->border_width);
        break;
    case PROP_FONT_SIZE:
        g_value_set_int (value, self->font_size);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_chart_legend_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgChartLegend *self = LRG_CHART_LEGEND (object);

    switch (prop_id)
    {
    case PROP_VISIBLE:
        lrg_chart_legend_set_visible (self, g_value_get_boolean (value));
        break;
    case PROP_POSITION:
        lrg_chart_legend_set_position (self, g_value_get_enum (value));
        break;
    case PROP_ORIENTATION:
        lrg_chart_legend_set_orientation (self, g_value_get_enum (value));
        break;
    case PROP_BACKGROUND_COLOR:
        lrg_chart_legend_set_background_color (self, g_value_get_boxed (value));
        break;
    case PROP_TEXT_COLOR:
        lrg_chart_legend_set_text_color (self, g_value_get_boxed (value));
        break;
    case PROP_BORDER_COLOR:
        lrg_chart_legend_set_border_color (self, g_value_get_boxed (value));
        break;
    case PROP_PADDING:
        lrg_chart_legend_set_padding (self, g_value_get_float (value));
        break;
    case PROP_ITEM_SPACING:
        lrg_chart_legend_set_item_spacing (self, g_value_get_float (value));
        break;
    case PROP_SYMBOL_SIZE:
        lrg_chart_legend_set_symbol_size (self, g_value_get_float (value));
        break;
    case PROP_SYMBOL_SPACING:
        lrg_chart_legend_set_symbol_spacing (self, g_value_get_float (value));
        break;
    case PROP_BORDER_WIDTH:
        lrg_chart_legend_set_border_width (self, g_value_get_float (value));
        break;
    case PROP_FONT_SIZE:
        lrg_chart_legend_set_font_size (self, g_value_get_int (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_chart_legend_class_init (LrgChartLegendClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_chart_legend_finalize;
    object_class->get_property = lrg_chart_legend_get_property;
    object_class->set_property = lrg_chart_legend_set_property;

    /**
     * LrgChartLegend:visible:
     *
     * Whether the legend is visible.
     *
     * Since: 1.0
     */
    properties[PROP_VISIBLE] =
        g_param_spec_boolean ("visible", NULL, NULL,
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartLegend:position:
     *
     * Position of the legend relative to the chart.
     *
     * Since: 1.0
     */
    properties[PROP_POSITION] =
        g_param_spec_enum ("position", NULL, NULL,
                           LRG_TYPE_LEGEND_POSITION,
                           LRG_LEGEND_RIGHT,
                           G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartLegend:orientation:
     *
     * Layout orientation of legend items.
     *
     * Since: 1.0
     */
    properties[PROP_ORIENTATION] =
        g_param_spec_enum ("orientation", NULL, NULL,
                           LRG_TYPE_LEGEND_ORIENTATION,
                           LRG_LEGEND_VERTICAL,
                           G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartLegend:background-color:
     *
     * The legend background color.
     *
     * Since: 1.0
     */
    properties[PROP_BACKGROUND_COLOR] =
        g_param_spec_boxed ("background-color", NULL, NULL,
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartLegend:text-color:
     *
     * The legend text color.
     *
     * Since: 1.0
     */
    properties[PROP_TEXT_COLOR] =
        g_param_spec_boxed ("text-color", NULL, NULL,
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartLegend:border-color:
     *
     * The legend border color.
     *
     * Since: 1.0
     */
    properties[PROP_BORDER_COLOR] =
        g_param_spec_boxed ("border-color", NULL, NULL,
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartLegend:padding:
     *
     * Internal padding in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_PADDING] =
        g_param_spec_float ("padding", NULL, NULL,
                            0.0f, 100.0f, 8.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartLegend:item-spacing:
     *
     * Spacing between legend items.
     *
     * Since: 1.0
     */
    properties[PROP_ITEM_SPACING] =
        g_param_spec_float ("item-spacing", NULL, NULL,
                            0.0f, 100.0f, 12.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartLegend:symbol-size:
     *
     * Size of legend symbols.
     *
     * Since: 1.0
     */
    properties[PROP_SYMBOL_SIZE] =
        g_param_spec_float ("symbol-size", NULL, NULL,
                            4.0f, 50.0f, 12.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartLegend:symbol-spacing:
     *
     * Spacing between symbol and text.
     *
     * Since: 1.0
     */
    properties[PROP_SYMBOL_SPACING] =
        g_param_spec_float ("symbol-spacing", NULL, NULL,
                            0.0f, 50.0f, 6.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartLegend:border-width:
     *
     * Border line width in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_BORDER_WIDTH] =
        g_param_spec_float ("border-width", NULL, NULL,
                            0.0f, 10.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartLegend:font-size:
     *
     * Font size in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_FONT_SIZE] =
        g_param_spec_int ("font-size", NULL, NULL,
                          6, 48, 12,
                          G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_chart_legend_init (LrgChartLegend *self)
{
    self->visible = TRUE;
    self->position = LRG_LEGEND_RIGHT;
    self->orientation = LRG_LEGEND_VERTICAL;

    self->background_color = grl_color_new (255, 255, 255, 200);
    self->text_color = grl_color_new (60, 60, 60, 255);
    self->border_color = grl_color_new (180, 180, 180, 255);

    self->padding = 8.0f;
    self->item_spacing = 12.0f;
    self->symbol_size = 12.0f;
    self->symbol_spacing = 6.0f;
    self->border_width = 1.0f;

    self->font_size = 12;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgChartLegend *
lrg_chart_legend_new (void)
{
    return g_object_new (LRG_TYPE_CHART_LEGEND, NULL);
}

gboolean
lrg_chart_legend_get_visible (LrgChartLegend *self)
{
    g_return_val_if_fail (LRG_IS_CHART_LEGEND (self), FALSE);
    return self->visible;
}

void
lrg_chart_legend_set_visible (LrgChartLegend *self,
                              gboolean        visible)
{
    g_return_if_fail (LRG_IS_CHART_LEGEND (self));

    visible = !!visible;
    if (self->visible != visible)
    {
        self->visible = visible;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE]);
    }
}

LrgLegendPosition
lrg_chart_legend_get_position (LrgChartLegend *self)
{
    g_return_val_if_fail (LRG_IS_CHART_LEGEND (self), LRG_LEGEND_RIGHT);
    return self->position;
}

void
lrg_chart_legend_set_position (LrgChartLegend    *self,
                               LrgLegendPosition  position)
{
    g_return_if_fail (LRG_IS_CHART_LEGEND (self));

    if (self->position != position)
    {
        self->position = position;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POSITION]);
    }
}

LrgLegendOrientation
lrg_chart_legend_get_orientation (LrgChartLegend *self)
{
    g_return_val_if_fail (LRG_IS_CHART_LEGEND (self), LRG_LEGEND_VERTICAL);
    return self->orientation;
}

void
lrg_chart_legend_set_orientation (LrgChartLegend       *self,
                                  LrgLegendOrientation  orientation)
{
    g_return_if_fail (LRG_IS_CHART_LEGEND (self));

    if (self->orientation != orientation)
    {
        self->orientation = orientation;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ORIENTATION]);
    }
}

GrlColor *
lrg_chart_legend_get_background_color (LrgChartLegend *self)
{
    g_return_val_if_fail (LRG_IS_CHART_LEGEND (self), NULL);
    return self->background_color;
}

void
lrg_chart_legend_set_background_color (LrgChartLegend *self,
                                       GrlColor       *color)
{
    g_return_if_fail (LRG_IS_CHART_LEGEND (self));

    g_clear_pointer (&self->background_color, grl_color_free);
    if (color != NULL)
    {
        self->background_color = grl_color_copy (color);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BACKGROUND_COLOR]);
}

GrlColor *
lrg_chart_legend_get_text_color (LrgChartLegend *self)
{
    g_return_val_if_fail (LRG_IS_CHART_LEGEND (self), NULL);
    return self->text_color;
}

void
lrg_chart_legend_set_text_color (LrgChartLegend *self,
                                 GrlColor       *color)
{
    g_return_if_fail (LRG_IS_CHART_LEGEND (self));

    g_clear_pointer (&self->text_color, grl_color_free);
    if (color != NULL)
    {
        self->text_color = grl_color_copy (color);
    }
    else
    {
        self->text_color = grl_color_new (60, 60, 60, 255);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT_COLOR]);
}

GrlColor *
lrg_chart_legend_get_border_color (LrgChartLegend *self)
{
    g_return_val_if_fail (LRG_IS_CHART_LEGEND (self), NULL);
    return self->border_color;
}

void
lrg_chart_legend_set_border_color (LrgChartLegend *self,
                                   GrlColor       *color)
{
    g_return_if_fail (LRG_IS_CHART_LEGEND (self));

    g_clear_pointer (&self->border_color, grl_color_free);
    if (color != NULL)
    {
        self->border_color = grl_color_copy (color);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER_COLOR]);
}

gfloat
lrg_chart_legend_get_padding (LrgChartLegend *self)
{
    g_return_val_if_fail (LRG_IS_CHART_LEGEND (self), 0.0f);
    return self->padding;
}

void
lrg_chart_legend_set_padding (LrgChartLegend *self,
                              gfloat          padding)
{
    g_return_if_fail (LRG_IS_CHART_LEGEND (self));

    if (self->padding != padding)
    {
        self->padding = padding;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PADDING]);
    }
}

gfloat
lrg_chart_legend_get_item_spacing (LrgChartLegend *self)
{
    g_return_val_if_fail (LRG_IS_CHART_LEGEND (self), 0.0f);
    return self->item_spacing;
}

void
lrg_chart_legend_set_item_spacing (LrgChartLegend *self,
                                   gfloat          spacing)
{
    g_return_if_fail (LRG_IS_CHART_LEGEND (self));

    if (self->item_spacing != spacing)
    {
        self->item_spacing = spacing;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ITEM_SPACING]);
    }
}

gfloat
lrg_chart_legend_get_symbol_size (LrgChartLegend *self)
{
    g_return_val_if_fail (LRG_IS_CHART_LEGEND (self), 0.0f);
    return self->symbol_size;
}

void
lrg_chart_legend_set_symbol_size (LrgChartLegend *self,
                                  gfloat          size)
{
    g_return_if_fail (LRG_IS_CHART_LEGEND (self));

    if (self->symbol_size != size)
    {
        self->symbol_size = size;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SYMBOL_SIZE]);
    }
}

gfloat
lrg_chart_legend_get_symbol_spacing (LrgChartLegend *self)
{
    g_return_val_if_fail (LRG_IS_CHART_LEGEND (self), 0.0f);
    return self->symbol_spacing;
}

void
lrg_chart_legend_set_symbol_spacing (LrgChartLegend *self,
                                     gfloat          spacing)
{
    g_return_if_fail (LRG_IS_CHART_LEGEND (self));

    if (self->symbol_spacing != spacing)
    {
        self->symbol_spacing = spacing;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SYMBOL_SPACING]);
    }
}

gfloat
lrg_chart_legend_get_border_width (LrgChartLegend *self)
{
    g_return_val_if_fail (LRG_IS_CHART_LEGEND (self), 0.0f);
    return self->border_width;
}

void
lrg_chart_legend_set_border_width (LrgChartLegend *self,
                                   gfloat          width)
{
    g_return_if_fail (LRG_IS_CHART_LEGEND (self));

    if (self->border_width != width)
    {
        self->border_width = width;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER_WIDTH]);
    }
}

gint
lrg_chart_legend_get_font_size (LrgChartLegend *self)
{
    g_return_val_if_fail (LRG_IS_CHART_LEGEND (self), 12);
    return self->font_size;
}

void
lrg_chart_legend_set_font_size (LrgChartLegend *self,
                                gint            size)
{
    g_return_if_fail (LRG_IS_CHART_LEGEND (self));

    if (self->font_size != size)
    {
        self->font_size = CLAMP (size, 6, 48);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_SIZE]);
    }
}

void
lrg_chart_legend_measure (LrgChartLegend *self,
                          GPtrArray      *series,
                          gfloat         *out_width,
                          gfloat         *out_height)
{
    guint i;
    gfloat max_text_width;
    gfloat total_width;
    gfloat total_height;
    gfloat item_height;
    guint visible_count;

    g_return_if_fail (LRG_IS_CHART_LEGEND (self));

    if (series == NULL || series->len == 0)
    {
        if (out_width != NULL)
            *out_width = 0.0f;
        if (out_height != NULL)
            *out_height = 0.0f;
        return;
    }

    /* Height of one item */
    item_height = MAX (self->symbol_size, (gfloat)self->font_size);

    /* Measure all series names */
    max_text_width = 0.0f;
    total_width = 0.0f;
    visible_count = 0;

    for (i = 0; i < series->len; i++)
    {
        LrgChartDataSeries *s = g_ptr_array_index (series, i);
        const gchar *name;
        gint text_width;
        gfloat item_width;

        if (!lrg_chart_data_series_get_show_in_legend (s))
            continue;

        visible_count++;
        name = lrg_chart_data_series_get_name (s);
        text_width = grl_measure_text (name != NULL ? name : "", self->font_size);
        item_width = self->symbol_size + self->symbol_spacing + (gfloat)text_width;

        if (self->orientation == LRG_LEGEND_HORIZONTAL)
        {
            total_width += item_width;
            if (i > 0)
                total_width += self->item_spacing;
        }
        else
        {
            if (item_width > max_text_width)
                max_text_width = item_width;
        }
    }

    if (visible_count == 0)
    {
        if (out_width != NULL)
            *out_width = 0.0f;
        if (out_height != NULL)
            *out_height = 0.0f;
        return;
    }

    if (self->orientation == LRG_LEGEND_HORIZONTAL)
    {
        total_width += self->padding * 2.0f;
        total_height = item_height + self->padding * 2.0f;
    }
    else
    {
        total_width = max_text_width + self->padding * 2.0f;
        total_height = (item_height * (gfloat)visible_count) +
                       (self->item_spacing * ((gfloat)visible_count - 1.0f)) +
                       (self->padding * 2.0f);
    }

    if (out_width != NULL)
        *out_width = total_width;
    if (out_height != NULL)
        *out_height = total_height;
}

void
lrg_chart_legend_draw (LrgChartLegend *self,
                       GPtrArray      *series,
                       gfloat          x,
                       gfloat          y)
{
    gfloat width;
    gfloat height;
    gfloat item_height;
    gfloat current_x;
    gfloat current_y;
    guint i;

    g_return_if_fail (LRG_IS_CHART_LEGEND (self));

    if (!self->visible || series == NULL || series->len == 0)
    {
        return;
    }

    /* Measure to get dimensions */
    lrg_chart_legend_measure (self, series, &width, &height);

    if (width <= 0.0f || height <= 0.0f)
    {
        return;
    }

    /* Draw background */
    if (self->background_color != NULL)
    {
        g_autoptr(GrlRectangle) bg_rect = grl_rectangle_new (x, y, width, height);
        grl_draw_rectangle_rec (bg_rect, self->background_color);
    }

    /* Draw border */
    if (self->border_color != NULL && self->border_width > 0.0f)
    {
        g_autoptr(GrlRectangle) border_rect = grl_rectangle_new (x, y, width, height);
        grl_draw_rectangle_lines_ex (border_rect, self->border_width, self->border_color);
    }

    /* Draw items */
    item_height = MAX (self->symbol_size, (gfloat)self->font_size);
    current_x = x + self->padding;
    current_y = y + self->padding;

    for (i = 0; i < series->len; i++)
    {
        LrgChartDataSeries *s = g_ptr_array_index (series, i);
        const gchar *name;
        gint text_width;
        gfloat symbol_y;
        gfloat text_y;

        if (!lrg_chart_data_series_get_show_in_legend (s))
            continue;

        name = lrg_chart_data_series_get_name (s);
        text_width = grl_measure_text (name != NULL ? name : "", self->font_size);

        /* Center symbol and text vertically */
        symbol_y = current_y + (item_height - self->symbol_size) / 2.0f;
        text_y = current_y + (item_height - (gfloat)self->font_size) / 2.0f;

        /* Draw symbol */
        draw_series_symbol (s, current_x, symbol_y, self->symbol_size);

        /* Draw text */
        grl_draw_text (name != NULL ? name : "",
                       (gint)(current_x + self->symbol_size + self->symbol_spacing),
                       (gint)text_y,
                       self->font_size,
                       self->text_color);

        /* Move to next item */
        if (self->orientation == LRG_LEGEND_HORIZONTAL)
        {
            current_x += self->symbol_size + self->symbol_spacing +
                         (gfloat)text_width + self->item_spacing;
        }
        else
        {
            current_y += item_height + self->item_spacing;
        }
    }
}

gint
lrg_chart_legend_hit_test (LrgChartLegend *self,
                           GPtrArray      *series,
                           gfloat          legend_x,
                           gfloat          legend_y,
                           gfloat          test_x,
                           gfloat          test_y)
{
    gfloat item_height;
    gfloat current_x;
    gfloat current_y;
    guint i;
    gint visible_index;

    g_return_val_if_fail (LRG_IS_CHART_LEGEND (self), -1);

    if (!self->visible || series == NULL || series->len == 0)
    {
        return -1;
    }

    item_height = MAX (self->symbol_size, (gfloat)self->font_size);
    current_x = legend_x + self->padding;
    current_y = legend_y + self->padding;
    visible_index = 0;

    for (i = 0; i < series->len; i++)
    {
        LrgChartDataSeries *s = g_ptr_array_index (series, i);
        const gchar *name;
        gint text_width;
        gfloat item_width;

        if (!lrg_chart_data_series_get_show_in_legend (s))
            continue;

        name = lrg_chart_data_series_get_name (s);
        text_width = grl_measure_text (name != NULL ? name : "", self->font_size);
        item_width = self->symbol_size + self->symbol_spacing + (gfloat)text_width;

        /* Check if point is within this item */
        if (self->orientation == LRG_LEGEND_HORIZONTAL)
        {
            if (test_x >= current_x && test_x < current_x + item_width &&
                test_y >= current_y && test_y < current_y + item_height)
            {
                return (gint)i;
            }
            current_x += item_width + self->item_spacing;
        }
        else
        {
            gfloat legend_width;
            gfloat item_width_full;

            lrg_chart_legend_measure (self, series, &legend_width, NULL);
            item_width_full = legend_width - self->padding * 2.0f;

            if (test_x >= current_x && test_x < current_x + item_width_full &&
                test_y >= current_y && test_y < current_y + item_height)
            {
                return (gint)i;
            }
            current_y += item_height + self->item_spacing;
        }

        visible_index++;
    }

    return -1;
}
