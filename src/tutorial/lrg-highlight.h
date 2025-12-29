/* lrg-highlight.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Highlight widget for tutorial system.
 *
 * This widget draws visual highlights around UI elements to
 * draw player attention during tutorials.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "../ui/lrg-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_HIGHLIGHT (lrg_highlight_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgHighlight, lrg_highlight, LRG, HIGHLIGHT, LrgWidget)

/**
 * lrg_highlight_new:
 *
 * Creates a new highlight widget.
 *
 * Returns: (transfer full): A new #LrgHighlight
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgHighlight *      lrg_highlight_new                   (void);

/**
 * lrg_highlight_new_with_style:
 * @style: The highlight style to use
 *
 * Creates a new highlight widget with the specified style.
 *
 * Returns: (transfer full): A new #LrgHighlight
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgHighlight *      lrg_highlight_new_with_style        (LrgHighlightStyle style);

/* Target */

/**
 * lrg_highlight_get_target:
 * @self: An #LrgHighlight
 *
 * Gets the target widget being highlighted.
 *
 * Returns: (transfer none) (nullable): The target widget
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgWidget *         lrg_highlight_get_target            (LrgHighlight *self);

/**
 * lrg_highlight_set_target:
 * @self: An #LrgHighlight
 * @target: (nullable): The widget to highlight
 *
 * Sets the target widget to highlight. The highlight will
 * automatically track the target's position and size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_highlight_set_target            (LrgHighlight *self,
                                                         LrgWidget    *target);

/**
 * lrg_highlight_set_target_rect:
 * @self: An #LrgHighlight
 * @x: Target X position
 * @y: Target Y position
 * @width: Target width
 * @height: Target height
 *
 * Sets a manual target rectangle when not highlighting a widget.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_highlight_set_target_rect       (LrgHighlight *self,
                                                         gfloat        x,
                                                         gfloat        y,
                                                         gfloat        width,
                                                         gfloat        height);

/* Style */

/**
 * lrg_highlight_get_style:
 * @self: An #LrgHighlight
 *
 * Gets the highlight style.
 *
 * Returns: The highlight style
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgHighlightStyle   lrg_highlight_get_style             (LrgHighlight *self);

/**
 * lrg_highlight_set_style:
 * @self: An #LrgHighlight
 * @style: The highlight style
 *
 * Sets the highlight style.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_highlight_set_style             (LrgHighlight     *self,
                                                         LrgHighlightStyle style);

/* Appearance */

/**
 * lrg_highlight_get_color:
 * @self: An #LrgHighlight
 *
 * Gets the highlight color.
 *
 * Returns: (transfer none): The color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const GrlColor *    lrg_highlight_get_color             (LrgHighlight *self);

/**
 * lrg_highlight_set_color:
 * @self: An #LrgHighlight
 * @color: The highlight color
 *
 * Sets the highlight color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_highlight_set_color             (LrgHighlight   *self,
                                                         const GrlColor *color);

/**
 * lrg_highlight_get_padding:
 * @self: An #LrgHighlight
 *
 * Gets the padding around the target.
 *
 * Returns: The padding in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_highlight_get_padding           (LrgHighlight *self);

/**
 * lrg_highlight_set_padding:
 * @self: An #LrgHighlight
 * @padding: The padding in pixels
 *
 * Sets the padding around the target.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_highlight_set_padding           (LrgHighlight *self,
                                                         gfloat        padding);

/**
 * lrg_highlight_get_outline_thickness:
 * @self: An #LrgHighlight
 *
 * Gets the outline thickness for outline style.
 *
 * Returns: The thickness in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_highlight_get_outline_thickness (LrgHighlight *self);

/**
 * lrg_highlight_set_outline_thickness:
 * @self: An #LrgHighlight
 * @thickness: The thickness in pixels
 *
 * Sets the outline thickness for outline style.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_highlight_set_outline_thickness (LrgHighlight *self,
                                                         gfloat        thickness);

/**
 * lrg_highlight_get_corner_radius:
 * @self: An #LrgHighlight
 *
 * Gets the corner radius for rounded highlights.
 *
 * Returns: The radius in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_highlight_get_corner_radius     (LrgHighlight *self);

/**
 * lrg_highlight_set_corner_radius:
 * @self: An #LrgHighlight
 * @radius: The radius in pixels
 *
 * Sets the corner radius for rounded highlights.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_highlight_set_corner_radius     (LrgHighlight *self,
                                                         gfloat        radius);

/* Animation */

/**
 * lrg_highlight_get_animated:
 * @self: An #LrgHighlight
 *
 * Gets whether the highlight is animated.
 *
 * Returns: %TRUE if animated
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_highlight_get_animated          (LrgHighlight *self);

/**
 * lrg_highlight_set_animated:
 * @self: An #LrgHighlight
 * @animated: Whether to animate
 *
 * Sets whether the highlight should animate (pulse, etc.).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_highlight_set_animated          (LrgHighlight *self,
                                                         gboolean      animated);

/**
 * lrg_highlight_get_pulse_speed:
 * @self: An #LrgHighlight
 *
 * Gets the animation pulse speed.
 *
 * Returns: The pulse speed (cycles per second)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_highlight_get_pulse_speed       (LrgHighlight *self);

/**
 * lrg_highlight_set_pulse_speed:
 * @self: An #LrgHighlight
 * @speed: The pulse speed (cycles per second)
 *
 * Sets the animation pulse speed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_highlight_set_pulse_speed       (LrgHighlight *self,
                                                         gfloat        speed);

/* Update */

/**
 * lrg_highlight_update:
 * @self: An #LrgHighlight
 * @delta_time: Time since last update in seconds
 *
 * Updates the highlight animation state.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_highlight_update                (LrgHighlight *self,
                                                         gfloat        delta_time);

G_END_DECLS
