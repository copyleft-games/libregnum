/* lrg-reel-light-leak-effect.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelLightLeakEffect - animated warm radial light-leak overlay.
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

#define LRG_TYPE_REEL_LIGHT_LEAK_EFFECT (lrg_reel_light_leak_effect_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelLightLeakEffect, lrg_reel_light_leak_effect,
                      LRG, REEL_LIGHT_LEAK_EFFECT, LrgReelEffect)

/**
 * lrg_reel_light_leak_effect_new:
 *
 * Creates a new #LrgReelLightLeakEffect with default settings:
 * warm-orange color, intensity 0.25.
 *
 * Returns: (transfer full): a new #LrgReelLightLeakEffect
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelLightLeakEffect *
lrg_reel_light_leak_effect_new (void);

/**
 * lrg_reel_light_leak_effect_get_color:
 * @self: a #LrgReelLightLeakEffect
 * @out_color: (out caller-allocates): return location for the leak color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_light_leak_effect_get_color (LrgReelLightLeakEffect *self,
                                       GrlColor               *out_color);

/**
 * lrg_reel_light_leak_effect_set_color:
 * @self: a #LrgReelLightLeakEffect
 * @color: (not nullable): new light-leak tint color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_light_leak_effect_set_color (LrgReelLightLeakEffect *self,
                                       const GrlColor         *color);

/**
 * lrg_reel_light_leak_effect_get_intensity:
 * @self: a #LrgReelLightLeakEffect
 *
 * Returns: the additive blend intensity [0, 1]
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_light_leak_effect_get_intensity (LrgReelLightLeakEffect *self);

/**
 * lrg_reel_light_leak_effect_set_intensity:
 * @self: a #LrgReelLightLeakEffect
 * @intensity: additive blend intensity [0, 1]
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_light_leak_effect_set_intensity (LrgReelLightLeakEffect *self,
                                           gdouble                 intensity);

G_END_DECLS
