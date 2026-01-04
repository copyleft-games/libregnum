/* lrg-chart-tooltip.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgChartTooltip - Tooltip for displaying chart data information.
 *
 * This is a derivable class that can be subclassed to customize
 * tooltip formatting and rendering for specific chart types.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-chart-hit-info.h"
#include "lrg-chart-data-series.h"

G_BEGIN_DECLS

#define LRG_TYPE_CHART_TOOLTIP (lrg_chart_tooltip_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgChartTooltip, lrg_chart_tooltip, LRG, CHART_TOOLTIP, GObject)

/**
 * LrgChartTooltipClass:
 * @parent_class: The parent class
 * @format_content: Virtual method to format tooltip text content
 * @draw: Virtual method to draw the tooltip
 *
 * Class structure for #LrgChartTooltip.
 *
 * Since: 1.0
 */
struct _LrgChartTooltipClass
{
    GObjectClass parent_class;

    /**
     * LrgChartTooltipClass::format_content:
     * @self: the tooltip
     * @series: the data series being hovered
     * @hit: hit info for the hovered element
     *
     * Formats the tooltip text content.
     *
     * Returns: (transfer full): formatted tooltip text
     */
    gchar * (*format_content) (LrgChartTooltip      *self,
                               LrgChartDataSeries   *series,
                               const LrgChartHitInfo *hit);

    /**
     * LrgChartTooltipClass::draw:
     * @self: the tooltip
     * @x: screen X position
     * @y: screen Y position
     * @content: formatted text content
     *
     * Draws the tooltip at the given position.
     */
    void (*draw) (LrgChartTooltip *self,
                  gfloat           x,
                  gfloat           y,
                  const gchar     *content);

    /**
     * LrgChartTooltipClass::get_size:
     * @self: the tooltip
     * @content: formatted text content
     * @out_width: (out): width of tooltip
     * @out_height: (out): height of tooltip
     *
     * Gets the size of the tooltip for the given content.
     */
    void (*get_size) (LrgChartTooltip *self,
                      const gchar     *content,
                      gfloat          *out_width,
                      gfloat          *out_height);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_chart_tooltip_new:
 *
 * Creates a new chart tooltip with default settings.
 *
 * Returns: (transfer full): a new #LrgChartTooltip
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartTooltip *
lrg_chart_tooltip_new (void);

/* ==========================================================================
 * Visibility
 * ========================================================================== */

/**
 * lrg_chart_tooltip_get_visible:
 * @self: an #LrgChartTooltip
 *
 * Gets whether the tooltip is visible.
 *
 * Returns: %TRUE if visible
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart_tooltip_get_visible (LrgChartTooltip *self);

/**
 * lrg_chart_tooltip_set_visible:
 * @self: an #LrgChartTooltip
 * @visible: whether tooltip is visible
 *
 * Sets tooltip visibility.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_tooltip_set_visible (LrgChartTooltip *self,
                               gboolean         visible);

/* ==========================================================================
 * Colors
 * ========================================================================== */

/**
 * lrg_chart_tooltip_get_background_color:
 * @self: an #LrgChartTooltip
 *
 * Gets the background color.
 *
 * Returns: (transfer none): the background color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_chart_tooltip_get_background_color (LrgChartTooltip *self);

/**
 * lrg_chart_tooltip_set_background_color:
 * @self: an #LrgChartTooltip
 * @color: the background color
 *
 * Sets the background color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_tooltip_set_background_color (LrgChartTooltip *self,
                                        GrlColor        *color);

/**
 * lrg_chart_tooltip_get_text_color:
 * @self: an #LrgChartTooltip
 *
 * Gets the text color.
 *
 * Returns: (transfer none): the text color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_chart_tooltip_get_text_color (LrgChartTooltip *self);

/**
 * lrg_chart_tooltip_set_text_color:
 * @self: an #LrgChartTooltip
 * @color: the text color
 *
 * Sets the text color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_tooltip_set_text_color (LrgChartTooltip *self,
                                  GrlColor        *color);

/**
 * lrg_chart_tooltip_get_border_color:
 * @self: an #LrgChartTooltip
 *
 * Gets the border color.
 *
 * Returns: (transfer none): the border color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_chart_tooltip_get_border_color (LrgChartTooltip *self);

/**
 * lrg_chart_tooltip_set_border_color:
 * @self: an #LrgChartTooltip
 * @color: the border color
 *
 * Sets the border color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_tooltip_set_border_color (LrgChartTooltip *self,
                                    GrlColor        *color);

/* ==========================================================================
 * Dimensions
 * ========================================================================== */

/**
 * lrg_chart_tooltip_get_padding:
 * @self: an #LrgChartTooltip
 *
 * Gets the internal padding.
 *
 * Returns: padding in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart_tooltip_get_padding (LrgChartTooltip *self);

/**
 * lrg_chart_tooltip_set_padding:
 * @self: an #LrgChartTooltip
 * @padding: padding in pixels
 *
 * Sets the internal padding.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_tooltip_set_padding (LrgChartTooltip *self,
                               gfloat           padding);

/**
 * lrg_chart_tooltip_get_corner_radius:
 * @self: an #LrgChartTooltip
 *
 * Gets the corner radius.
 *
 * Returns: corner radius in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart_tooltip_get_corner_radius (LrgChartTooltip *self);

/**
 * lrg_chart_tooltip_set_corner_radius:
 * @self: an #LrgChartTooltip
 * @radius: corner radius in pixels
 *
 * Sets the corner radius.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_tooltip_set_corner_radius (LrgChartTooltip *self,
                                     gfloat           radius);

/**
 * lrg_chart_tooltip_get_border_width:
 * @self: an #LrgChartTooltip
 *
 * Gets the border width.
 *
 * Returns: border width in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart_tooltip_get_border_width (LrgChartTooltip *self);

/**
 * lrg_chart_tooltip_set_border_width:
 * @self: an #LrgChartTooltip
 * @width: border width in pixels
 *
 * Sets the border width.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_tooltip_set_border_width (LrgChartTooltip *self,
                                    gfloat           width);

/* ==========================================================================
 * Text Settings
 * ========================================================================== */

/**
 * lrg_chart_tooltip_get_font_size:
 * @self: an #LrgChartTooltip
 *
 * Gets the font size.
 *
 * Returns: font size in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_chart_tooltip_get_font_size (LrgChartTooltip *self);

/**
 * lrg_chart_tooltip_set_font_size:
 * @self: an #LrgChartTooltip
 * @size: font size in pixels
 *
 * Sets the font size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_tooltip_set_font_size (LrgChartTooltip *self,
                                 gint             size);

/* ==========================================================================
 * Format Settings
 * ========================================================================== */

/**
 * lrg_chart_tooltip_get_show_series_name:
 * @self: an #LrgChartTooltip
 *
 * Gets whether the series name is shown in the tooltip.
 *
 * Returns: %TRUE if series name is shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart_tooltip_get_show_series_name (LrgChartTooltip *self);

/**
 * lrg_chart_tooltip_set_show_series_name:
 * @self: an #LrgChartTooltip
 * @show: whether to show series name
 *
 * Sets whether to show the series name in the tooltip.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_tooltip_set_show_series_name (LrgChartTooltip *self,
                                        gboolean         show);

/**
 * lrg_chart_tooltip_get_value_format:
 * @self: an #LrgChartTooltip
 *
 * Gets the printf format string for values.
 *
 * Returns: (nullable): format string or %NULL for default
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_chart_tooltip_get_value_format (LrgChartTooltip *self);

/**
 * lrg_chart_tooltip_set_value_format:
 * @self: an #LrgChartTooltip
 * @format: (nullable): printf format string for values
 *
 * Sets the printf format string for displaying values.
 * Use %NULL for default formatting.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_tooltip_set_value_format (LrgChartTooltip *self,
                                    const gchar     *format);

/* ==========================================================================
 * Operations
 * ========================================================================== */

/**
 * lrg_chart_tooltip_format_content:
 * @self: an #LrgChartTooltip
 * @series: the data series being hovered
 * @hit: hit info for the hovered element
 *
 * Formats the tooltip content for the given data.
 * This calls the virtual format_content method.
 *
 * Returns: (transfer full): formatted tooltip text
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_chart_tooltip_format_content (LrgChartTooltip       *self,
                                  LrgChartDataSeries    *series,
                                  const LrgChartHitInfo *hit);

/**
 * lrg_chart_tooltip_draw:
 * @self: an #LrgChartTooltip
 * @x: screen X position
 * @y: screen Y position
 * @content: formatted text content
 *
 * Draws the tooltip at the given position.
 * This calls the virtual draw method.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_tooltip_draw (LrgChartTooltip *self,
                        gfloat           x,
                        gfloat           y,
                        const gchar     *content);

/**
 * lrg_chart_tooltip_get_size:
 * @self: an #LrgChartTooltip
 * @content: formatted text content
 * @out_width: (out): width of tooltip
 * @out_height: (out): height of tooltip
 *
 * Gets the size of the tooltip for the given content.
 * This calls the virtual get_size method.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_tooltip_get_size (LrgChartTooltip *self,
                            const gchar     *content,
                            gfloat          *out_width,
                            gfloat          *out_height);

G_END_DECLS
