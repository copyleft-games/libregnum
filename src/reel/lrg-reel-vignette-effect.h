/* lrg-reel-vignette-effect.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelVignetteEffect - dark-edge vignette overlay for a reel clip layer.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-reel-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_REEL_VIGNETTE_EFFECT (lrg_reel_vignette_effect_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelVignetteEffect, lrg_reel_vignette_effect,
                      LRG, REEL_VIGNETTE_EFFECT, LrgReelEffect)

/**
 * lrg_reel_vignette_effect_new:
 *
 * Creates a new #LrgReelVignetteEffect with default settings
 * (intensity=0.5, radius=0.6).
 *
 * Returns: (transfer full): a new #LrgReelVignetteEffect
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelVignetteEffect *
lrg_reel_vignette_effect_new (void);

/**
 * lrg_reel_vignette_effect_get_intensity:
 * @self: a #LrgReelVignetteEffect
 *
 * Returns: the vignette intensity [0, 1]
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_vignette_effect_get_intensity (LrgReelVignetteEffect *self);

/**
 * lrg_reel_vignette_effect_set_intensity:
 * @self: a #LrgReelVignetteEffect
 * @intensity: darkening strength [0, 1]
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_vignette_effect_set_intensity (LrgReelVignetteEffect *self,
                                         gdouble                intensity);

/**
 * lrg_reel_vignette_effect_get_radius:
 * @self: a #LrgReelVignetteEffect
 *
 * Returns: the normalized inner radius where darkening begins [0, 1]
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_vignette_effect_get_radius (LrgReelVignetteEffect *self);

/**
 * lrg_reel_vignette_effect_set_radius:
 * @self: a #LrgReelVignetteEffect
 * @radius: normalized inner radius [0, 1]
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_vignette_effect_set_radius (LrgReelVignetteEffect *self,
                                      gdouble                radius);

G_END_DECLS
