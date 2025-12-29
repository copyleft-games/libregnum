/* lrg-point-light2d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Point light for 2D scenes.
 *
 * Omnidirectional light that radiates from a single point.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-light2d.h"

G_BEGIN_DECLS

#define LRG_TYPE_POINT_LIGHT2D (lrg_point_light2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPointLight2D, lrg_point_light2d, LRG, POINT_LIGHT2D, LrgLight2D)

/**
 * lrg_point_light2d_new:
 *
 * Creates a new point light.
 *
 * Returns: (transfer full): A new #LrgPointLight2D
 */
LRG_AVAILABLE_IN_ALL
LrgPointLight2D * lrg_point_light2d_new (void);

/**
 * lrg_point_light2d_get_radius:
 * @self: an #LrgPointLight2D
 *
 * Gets the light radius.
 *
 * Returns: Light radius in pixels
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_point_light2d_get_radius (LrgPointLight2D *self);

/**
 * lrg_point_light2d_set_radius:
 * @self: an #LrgPointLight2D
 * @radius: radius in pixels
 *
 * Sets the light radius.
 */
LRG_AVAILABLE_IN_ALL
void lrg_point_light2d_set_radius (LrgPointLight2D *self,
                                   gfloat           radius);

/**
 * lrg_point_light2d_get_inner_radius:
 * @self: an #LrgPointLight2D
 *
 * Gets the inner radius (full intensity zone).
 *
 * Returns: Inner radius in pixels
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_point_light2d_get_inner_radius (LrgPointLight2D *self);

/**
 * lrg_point_light2d_set_inner_radius:
 * @self: an #LrgPointLight2D
 * @radius: inner radius in pixels
 *
 * Sets the inner radius where intensity is at maximum.
 */
LRG_AVAILABLE_IN_ALL
void lrg_point_light2d_set_inner_radius (LrgPointLight2D *self,
                                         gfloat           radius);

/**
 * lrg_point_light2d_get_flicker_enabled:
 * @self: an #LrgPointLight2D
 *
 * Gets whether flickering is enabled.
 *
 * Returns: %TRUE if flickering
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_point_light2d_get_flicker_enabled (LrgPointLight2D *self);

/**
 * lrg_point_light2d_set_flicker_enabled:
 * @self: an #LrgPointLight2D
 * @enabled: whether to enable flickering
 *
 * Enables or disables light flickering.
 */
LRG_AVAILABLE_IN_ALL
void lrg_point_light2d_set_flicker_enabled (LrgPointLight2D *self,
                                            gboolean         enabled);

/**
 * lrg_point_light2d_get_flicker_speed:
 * @self: an #LrgPointLight2D
 *
 * Gets the flicker speed.
 *
 * Returns: Flicker speed multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_point_light2d_get_flicker_speed (LrgPointLight2D *self);

/**
 * lrg_point_light2d_set_flicker_speed:
 * @self: an #LrgPointLight2D
 * @speed: flicker speed
 *
 * Sets the flicker speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_point_light2d_set_flicker_speed (LrgPointLight2D *self,
                                          gfloat           speed);

/**
 * lrg_point_light2d_get_flicker_amount:
 * @self: an #LrgPointLight2D
 *
 * Gets the flicker intensity variation.
 *
 * Returns: Flicker amount (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_point_light2d_get_flicker_amount (LrgPointLight2D *self);

/**
 * lrg_point_light2d_set_flicker_amount:
 * @self: an #LrgPointLight2D
 * @amount: flicker intensity variation
 *
 * Sets the flicker intensity variation.
 */
LRG_AVAILABLE_IN_ALL
void lrg_point_light2d_set_flicker_amount (LrgPointLight2D *self,
                                           gfloat           amount);

G_END_DECLS
