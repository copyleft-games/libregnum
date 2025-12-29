/* lrg-directional-light2d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Directional light for 2D scenes.
 *
 * Parallel light rays from infinite distance (like sunlight).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-light2d.h"

G_BEGIN_DECLS

#define LRG_TYPE_DIRECTIONAL_LIGHT2D (lrg_directional_light2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDirectionalLight2D, lrg_directional_light2d, LRG, DIRECTIONAL_LIGHT2D, LrgLight2D)

LRG_AVAILABLE_IN_ALL
LrgDirectionalLight2D * lrg_directional_light2d_new (void);

LRG_AVAILABLE_IN_ALL
gfloat lrg_directional_light2d_get_direction (LrgDirectionalLight2D *self);

LRG_AVAILABLE_IN_ALL
void lrg_directional_light2d_set_direction (LrgDirectionalLight2D *self, gfloat direction);

LRG_AVAILABLE_IN_ALL
gfloat lrg_directional_light2d_get_shadow_length (LrgDirectionalLight2D *self);

LRG_AVAILABLE_IN_ALL
void lrg_directional_light2d_set_shadow_length (LrgDirectionalLight2D *self, gfloat length);

G_END_DECLS
