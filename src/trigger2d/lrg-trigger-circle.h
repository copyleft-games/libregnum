/* lrg-trigger-circle.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Circular trigger zone.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include "lrg-trigger2d.h"

G_BEGIN_DECLS

#define LRG_TYPE_TRIGGER_CIRCLE (lrg_trigger_circle_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTriggerCircle, lrg_trigger_circle, LRG, TRIGGER_CIRCLE, LrgTrigger2D)

/**
 * lrg_trigger_circle_new:
 * @center_x: X coordinate of the circle center
 * @center_y: Y coordinate of the circle center
 * @radius: Radius of the circle
 *
 * Creates a new circular trigger zone.
 *
 * Returns: (transfer full): A new #LrgTriggerCircle
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTriggerCircle *  lrg_trigger_circle_new          (gfloat center_x,
                                                     gfloat center_y,
                                                     gfloat radius);

/**
 * lrg_trigger_circle_new_with_id:
 * @id: Unique identifier for the trigger
 * @center_x: X coordinate of the circle center
 * @center_y: Y coordinate of the circle center
 * @radius: Radius of the circle
 *
 * Creates a new circular trigger zone with an ID.
 *
 * Returns: (transfer full): A new #LrgTriggerCircle
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTriggerCircle *  lrg_trigger_circle_new_with_id  (const gchar *id,
                                                     gfloat       center_x,
                                                     gfloat       center_y,
                                                     gfloat       radius);

/* Center position */

/**
 * lrg_trigger_circle_get_center_x:
 * @self: A #LrgTriggerCircle
 *
 * Gets the X coordinate of the circle center.
 *
 * Returns: The center X coordinate
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_trigger_circle_get_center_x (LrgTriggerCircle *self);

/**
 * lrg_trigger_circle_set_center_x:
 * @self: A #LrgTriggerCircle
 * @x: The center X coordinate
 *
 * Sets the X coordinate of the circle center.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_circle_set_center_x (LrgTriggerCircle *self,
                                                     gfloat            x);

/**
 * lrg_trigger_circle_get_center_y:
 * @self: A #LrgTriggerCircle
 *
 * Gets the Y coordinate of the circle center.
 *
 * Returns: The center Y coordinate
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_trigger_circle_get_center_y (LrgTriggerCircle *self);

/**
 * lrg_trigger_circle_set_center_y:
 * @self: A #LrgTriggerCircle
 * @y: The center Y coordinate
 *
 * Sets the Y coordinate of the circle center.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_circle_set_center_y (LrgTriggerCircle *self,
                                                     gfloat            y);

/**
 * lrg_trigger_circle_get_center:
 * @self: A #LrgTriggerCircle
 * @out_x: (out) (nullable): Return location for center X
 * @out_y: (out) (nullable): Return location for center Y
 *
 * Gets the center point of the circle.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_circle_get_center   (LrgTriggerCircle *self,
                                                     gfloat           *out_x,
                                                     gfloat           *out_y);

/**
 * lrg_trigger_circle_set_center:
 * @self: A #LrgTriggerCircle
 * @x: The center X coordinate
 * @y: The center Y coordinate
 *
 * Sets the center point of the circle.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_circle_set_center   (LrgTriggerCircle *self,
                                                     gfloat            x,
                                                     gfloat            y);

/* Radius */

/**
 * lrg_trigger_circle_get_radius:
 * @self: A #LrgTriggerCircle
 *
 * Gets the radius of the circle.
 *
 * Returns: The radius
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_trigger_circle_get_radius   (LrgTriggerCircle *self);

/**
 * lrg_trigger_circle_set_radius:
 * @self: A #LrgTriggerCircle
 * @radius: The radius
 *
 * Sets the radius of the circle.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_circle_set_radius   (LrgTriggerCircle *self,
                                                     gfloat            radius);

/* Utility */

/**
 * lrg_trigger_circle_get_diameter:
 * @self: A #LrgTriggerCircle
 *
 * Gets the diameter of the circle (2 * radius).
 *
 * Returns: The diameter
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_trigger_circle_get_diameter (LrgTriggerCircle *self);

/**
 * lrg_trigger_circle_distance_to_point:
 * @self: A #LrgTriggerCircle
 * @x: X coordinate of the point
 * @y: Y coordinate of the point
 *
 * Gets the distance from the circle edge to a point.
 * Negative values indicate the point is inside the circle.
 *
 * Returns: The signed distance
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_trigger_circle_distance_to_point (LrgTriggerCircle *self,
                                                          gfloat            x,
                                                          gfloat            y);

G_END_DECLS
