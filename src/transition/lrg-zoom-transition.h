/* lrg-zoom-transition.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Zoom in/out transition effect.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-transition.h"

G_BEGIN_DECLS

#define LRG_TYPE_ZOOM_TRANSITION (lrg_zoom_transition_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgZoomTransition, lrg_zoom_transition, LRG, ZOOM_TRANSITION, LrgTransition)

/**
 * lrg_zoom_transition_new:
 *
 * Creates a new zoom transition with default settings (zoom in).
 *
 * Returns: (transfer full): A new #LrgZoomTransition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgZoomTransition * lrg_zoom_transition_new                 (void);

/**
 * lrg_zoom_transition_new_with_direction:
 * @direction: The zoom direction (in or out)
 *
 * Creates a new zoom transition with the specified direction.
 *
 * Returns: (transfer full): A new #LrgZoomTransition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgZoomTransition * lrg_zoom_transition_new_with_direction  (LrgZoomDirection direction);

/**
 * lrg_zoom_transition_get_direction:
 * @self: A #LrgZoomTransition
 *
 * Gets the zoom direction.
 *
 * Returns: The #LrgZoomDirection
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgZoomDirection    lrg_zoom_transition_get_direction       (LrgZoomTransition *self);

/**
 * lrg_zoom_transition_set_direction:
 * @self: A #LrgZoomTransition
 * @direction: The zoom direction
 *
 * Sets the zoom direction.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_zoom_transition_set_direction       (LrgZoomTransition *self,
                                                             LrgZoomDirection   direction);

/**
 * lrg_zoom_transition_get_scale:
 * @self: A #LrgZoomTransition
 *
 * Gets the maximum zoom scale.
 *
 * Returns: The scale value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_zoom_transition_get_scale           (LrgZoomTransition *self);

/**
 * lrg_zoom_transition_set_scale:
 * @self: A #LrgZoomTransition
 * @scale: Maximum zoom scale (e.g., 2.0 = zoom to 2x size)
 *
 * Sets the maximum zoom scale.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_zoom_transition_set_scale           (LrgZoomTransition *self,
                                                             gfloat             scale);

/**
 * lrg_zoom_transition_get_center_x:
 * @self: A #LrgZoomTransition
 *
 * Gets the zoom center X coordinate (0.0-1.0, normalized).
 *
 * Returns: The center X coordinate
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_zoom_transition_get_center_x        (LrgZoomTransition *self);

/**
 * lrg_zoom_transition_get_center_y:
 * @self: A #LrgZoomTransition
 *
 * Gets the zoom center Y coordinate (0.0-1.0, normalized).
 *
 * Returns: The center Y coordinate
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_zoom_transition_get_center_y        (LrgZoomTransition *self);

/**
 * lrg_zoom_transition_set_center:
 * @self: A #LrgZoomTransition
 * @x: Center X (0.0-1.0, normalized)
 * @y: Center Y (0.0-1.0, normalized)
 *
 * Sets the zoom center point.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_zoom_transition_set_center          (LrgZoomTransition *self,
                                                             gfloat             x,
                                                             gfloat             y);

G_END_DECLS
