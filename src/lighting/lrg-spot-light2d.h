/* lrg-spot-light2d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Spot light for 2D scenes.
 *
 * Directional cone light with adjustable angle.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-light2d.h"

G_BEGIN_DECLS

#define LRG_TYPE_SPOT_LIGHT2D (lrg_spot_light2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSpotLight2D, lrg_spot_light2d, LRG, SPOT_LIGHT2D, LrgLight2D)

LRG_AVAILABLE_IN_ALL
LrgSpotLight2D * lrg_spot_light2d_new (void);

LRG_AVAILABLE_IN_ALL
gfloat lrg_spot_light2d_get_radius (LrgSpotLight2D *self);

LRG_AVAILABLE_IN_ALL
void lrg_spot_light2d_set_radius (LrgSpotLight2D *self, gfloat radius);

LRG_AVAILABLE_IN_ALL
gfloat lrg_spot_light2d_get_angle (LrgSpotLight2D *self);

LRG_AVAILABLE_IN_ALL
void lrg_spot_light2d_set_angle (LrgSpotLight2D *self, gfloat angle);

LRG_AVAILABLE_IN_ALL
gfloat lrg_spot_light2d_get_direction (LrgSpotLight2D *self);

LRG_AVAILABLE_IN_ALL
void lrg_spot_light2d_set_direction (LrgSpotLight2D *self, gfloat direction);

LRG_AVAILABLE_IN_ALL
gfloat lrg_spot_light2d_get_inner_angle (LrgSpotLight2D *self);

LRG_AVAILABLE_IN_ALL
void lrg_spot_light2d_set_inner_angle (LrgSpotLight2D *self, gfloat angle);

G_END_DECLS
