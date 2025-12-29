/* lrg-trigger-rect.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Rectangular trigger zone.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include "lrg-trigger2d.h"

G_BEGIN_DECLS

#define LRG_TYPE_TRIGGER_RECT (lrg_trigger_rect_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTriggerRect, lrg_trigger_rect, LRG, TRIGGER_RECT, LrgTrigger2D)

/**
 * lrg_trigger_rect_new:
 * @x: X coordinate of the rectangle origin
 * @y: Y coordinate of the rectangle origin
 * @width: Width of the rectangle
 * @height: Height of the rectangle
 *
 * Creates a new rectangular trigger zone.
 *
 * Returns: (transfer full): A new #LrgTriggerRect
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTriggerRect *    lrg_trigger_rect_new            (gfloat x,
                                                     gfloat y,
                                                     gfloat width,
                                                     gfloat height);

/**
 * lrg_trigger_rect_new_with_id:
 * @id: Unique identifier for the trigger
 * @x: X coordinate of the rectangle origin
 * @y: Y coordinate of the rectangle origin
 * @width: Width of the rectangle
 * @height: Height of the rectangle
 *
 * Creates a new rectangular trigger zone with an ID.
 *
 * Returns: (transfer full): A new #LrgTriggerRect
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTriggerRect *    lrg_trigger_rect_new_with_id    (const gchar *id,
                                                     gfloat       x,
                                                     gfloat       y,
                                                     gfloat       width,
                                                     gfloat       height);

/* Position */

/**
 * lrg_trigger_rect_get_x:
 * @self: A #LrgTriggerRect
 *
 * Gets the X coordinate of the rectangle origin.
 *
 * Returns: The X coordinate
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_trigger_rect_get_x          (LrgTriggerRect *self);

/**
 * lrg_trigger_rect_set_x:
 * @self: A #LrgTriggerRect
 * @x: The X coordinate
 *
 * Sets the X coordinate of the rectangle origin.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_rect_set_x          (LrgTriggerRect *self,
                                                     gfloat          x);

/**
 * lrg_trigger_rect_get_y:
 * @self: A #LrgTriggerRect
 *
 * Gets the Y coordinate of the rectangle origin.
 *
 * Returns: The Y coordinate
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_trigger_rect_get_y          (LrgTriggerRect *self);

/**
 * lrg_trigger_rect_set_y:
 * @self: A #LrgTriggerRect
 * @y: The Y coordinate
 *
 * Sets the Y coordinate of the rectangle origin.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_rect_set_y          (LrgTriggerRect *self,
                                                     gfloat          y);

/**
 * lrg_trigger_rect_set_position:
 * @self: A #LrgTriggerRect
 * @x: The X coordinate
 * @y: The Y coordinate
 *
 * Sets the position of the rectangle origin.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_rect_set_position   (LrgTriggerRect *self,
                                                     gfloat          x,
                                                     gfloat          y);

/* Size */

/**
 * lrg_trigger_rect_get_width:
 * @self: A #LrgTriggerRect
 *
 * Gets the width of the rectangle.
 *
 * Returns: The width
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_trigger_rect_get_width      (LrgTriggerRect *self);

/**
 * lrg_trigger_rect_set_width:
 * @self: A #LrgTriggerRect
 * @width: The width
 *
 * Sets the width of the rectangle.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_rect_set_width      (LrgTriggerRect *self,
                                                     gfloat          width);

/**
 * lrg_trigger_rect_get_height:
 * @self: A #LrgTriggerRect
 *
 * Gets the height of the rectangle.
 *
 * Returns: The height
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_trigger_rect_get_height     (LrgTriggerRect *self);

/**
 * lrg_trigger_rect_set_height:
 * @self: A #LrgTriggerRect
 * @height: The height
 *
 * Sets the height of the rectangle.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_rect_set_height     (LrgTriggerRect *self,
                                                     gfloat          height);

/**
 * lrg_trigger_rect_set_size:
 * @self: A #LrgTriggerRect
 * @width: The width
 * @height: The height
 *
 * Sets the size of the rectangle.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_rect_set_size       (LrgTriggerRect *self,
                                                     gfloat          width,
                                                     gfloat          height);

/**
 * lrg_trigger_rect_set_rect:
 * @self: A #LrgTriggerRect
 * @x: The X coordinate
 * @y: The Y coordinate
 * @width: The width
 * @height: The height
 *
 * Sets all rectangle properties at once.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_rect_set_rect       (LrgTriggerRect *self,
                                                     gfloat          x,
                                                     gfloat          y,
                                                     gfloat          width,
                                                     gfloat          height);

/* Center point */

/**
 * lrg_trigger_rect_get_center:
 * @self: A #LrgTriggerRect
 * @out_x: (out) (nullable): Return location for center X
 * @out_y: (out) (nullable): Return location for center Y
 *
 * Gets the center point of the rectangle.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_rect_get_center     (LrgTriggerRect *self,
                                                     gfloat         *out_x,
                                                     gfloat         *out_y);

/**
 * lrg_trigger_rect_set_center:
 * @self: A #LrgTriggerRect
 * @x: The center X coordinate
 * @y: The center Y coordinate
 *
 * Sets the position so that the center is at the given coordinates.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_rect_set_center     (LrgTriggerRect *self,
                                                     gfloat          x,
                                                     gfloat          y);

G_END_DECLS
