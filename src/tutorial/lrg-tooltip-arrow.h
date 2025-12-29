/* lrg-tooltip-arrow.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tooltip arrow widget for tutorial system.
 *
 * This widget draws an animated arrow pointing at a target
 * to guide the player during tutorials.
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

#define LRG_TYPE_TOOLTIP_ARROW (lrg_tooltip_arrow_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTooltipArrow, lrg_tooltip_arrow, LRG, TOOLTIP_ARROW, LrgWidget)

/**
 * lrg_tooltip_arrow_new:
 *
 * Creates a new tooltip arrow widget.
 *
 * Returns: (transfer full): A new #LrgTooltipArrow
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTooltipArrow *   lrg_tooltip_arrow_new               (void);

/**
 * lrg_tooltip_arrow_new_with_direction:
 * @direction: The arrow direction
 *
 * Creates a new tooltip arrow with the specified direction.
 *
 * Returns: (transfer full): A new #LrgTooltipArrow
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTooltipArrow *   lrg_tooltip_arrow_new_with_direction (LrgArrowDirection direction);

/* Direction */

/**
 * lrg_tooltip_arrow_get_direction:
 * @self: An #LrgTooltipArrow
 *
 * Gets the arrow direction.
 *
 * Returns: The arrow direction
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgArrowDirection   lrg_tooltip_arrow_get_direction     (LrgTooltipArrow *self);

/**
 * lrg_tooltip_arrow_set_direction:
 * @self: An #LrgTooltipArrow
 * @direction: The arrow direction
 *
 * Sets the arrow direction.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tooltip_arrow_set_direction     (LrgTooltipArrow  *self,
                                                         LrgArrowDirection direction);

/* Target */

/**
 * lrg_tooltip_arrow_get_target:
 * @self: An #LrgTooltipArrow
 *
 * Gets the target widget the arrow points at.
 *
 * Returns: (transfer none) (nullable): The target widget
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgWidget *         lrg_tooltip_arrow_get_target        (LrgTooltipArrow *self);

/**
 * lrg_tooltip_arrow_set_target:
 * @self: An #LrgTooltipArrow
 * @target: (nullable): The widget to point at
 *
 * Sets the target widget. The arrow will automatically position
 * itself to point at this widget.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tooltip_arrow_set_target        (LrgTooltipArrow *self,
                                                         LrgWidget       *target);

/**
 * lrg_tooltip_arrow_set_target_position:
 * @self: An #LrgTooltipArrow
 * @x: Target X position
 * @y: Target Y position
 *
 * Sets a manual target position when not pointing at a widget.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tooltip_arrow_set_target_position (LrgTooltipArrow *self,
                                                           gfloat           x,
                                                           gfloat           y);

/* Appearance */

/**
 * lrg_tooltip_arrow_get_color:
 * @self: An #LrgTooltipArrow
 *
 * Gets the arrow color.
 *
 * Returns: (transfer none): The color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const GrlColor *    lrg_tooltip_arrow_get_color         (LrgTooltipArrow *self);

/**
 * lrg_tooltip_arrow_set_color:
 * @self: An #LrgTooltipArrow
 * @color: The arrow color
 *
 * Sets the arrow color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tooltip_arrow_set_color         (LrgTooltipArrow *self,
                                                         const GrlColor  *color);

/**
 * lrg_tooltip_arrow_get_size:
 * @self: An #LrgTooltipArrow
 *
 * Gets the arrow size.
 *
 * Returns: The size in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_tooltip_arrow_get_size          (LrgTooltipArrow *self);

/**
 * lrg_tooltip_arrow_set_size:
 * @self: An #LrgTooltipArrow
 * @size: The size in pixels
 *
 * Sets the arrow size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tooltip_arrow_set_size          (LrgTooltipArrow *self,
                                                         gfloat           size);

/**
 * lrg_tooltip_arrow_get_offset:
 * @self: An #LrgTooltipArrow
 *
 * Gets the offset distance from the target.
 *
 * Returns: The offset in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_tooltip_arrow_get_offset        (LrgTooltipArrow *self);

/**
 * lrg_tooltip_arrow_set_offset:
 * @self: An #LrgTooltipArrow
 * @offset: The offset in pixels
 *
 * Sets the offset distance from the target.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tooltip_arrow_set_offset        (LrgTooltipArrow *self,
                                                         gfloat           offset);

/* Animation */

/**
 * lrg_tooltip_arrow_get_animated:
 * @self: An #LrgTooltipArrow
 *
 * Gets whether the arrow is animated.
 *
 * Returns: %TRUE if animated
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tooltip_arrow_get_animated      (LrgTooltipArrow *self);

/**
 * lrg_tooltip_arrow_set_animated:
 * @self: An #LrgTooltipArrow
 * @animated: Whether to animate
 *
 * Sets whether the arrow should animate (bob up and down).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tooltip_arrow_set_animated      (LrgTooltipArrow *self,
                                                         gboolean         animated);

/**
 * lrg_tooltip_arrow_get_bounce_amount:
 * @self: An #LrgTooltipArrow
 *
 * Gets the bounce animation amount.
 *
 * Returns: The bounce amount in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_tooltip_arrow_get_bounce_amount (LrgTooltipArrow *self);

/**
 * lrg_tooltip_arrow_set_bounce_amount:
 * @self: An #LrgTooltipArrow
 * @amount: The bounce amount in pixels
 *
 * Sets the bounce animation amount.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tooltip_arrow_set_bounce_amount (LrgTooltipArrow *self,
                                                         gfloat           amount);

/**
 * lrg_tooltip_arrow_get_bounce_speed:
 * @self: An #LrgTooltipArrow
 *
 * Gets the bounce animation speed.
 *
 * Returns: The bounce speed (cycles per second)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_tooltip_arrow_get_bounce_speed  (LrgTooltipArrow *self);

/**
 * lrg_tooltip_arrow_set_bounce_speed:
 * @self: An #LrgTooltipArrow
 * @speed: The bounce speed (cycles per second)
 *
 * Sets the bounce animation speed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tooltip_arrow_set_bounce_speed  (LrgTooltipArrow *self,
                                                         gfloat           speed);

/* Update */

/**
 * lrg_tooltip_arrow_update:
 * @self: An #LrgTooltipArrow
 * @delta_time: Time since last update in seconds
 *
 * Updates the arrow animation state.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tooltip_arrow_update            (LrgTooltipArrow *self,
                                                         gfloat           delta_time);

G_END_DECLS
