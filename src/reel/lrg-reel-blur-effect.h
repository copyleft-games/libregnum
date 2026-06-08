/* lrg-reel-blur-effect.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelBlurEffect - box-blur filter for a reel clip layer.
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

#define LRG_TYPE_REEL_BLUR_EFFECT (lrg_reel_blur_effect_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelBlurEffect, lrg_reel_blur_effect,
                      LRG, REEL_BLUR_EFFECT, LrgReelEffect)

/**
 * lrg_reel_blur_effect_new:
 * @radius: blur radius in pixels (<=0 is a no-op)
 *
 * Creates a new #LrgReelBlurEffect with the given @radius.
 *
 * Returns: (transfer full): a new #LrgReelBlurEffect
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelBlurEffect *
lrg_reel_blur_effect_new (gint radius);

/**
 * lrg_reel_blur_effect_get_radius:
 * @self: a #LrgReelBlurEffect
 *
 * Returns: the blur radius
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_blur_effect_get_radius (LrgReelBlurEffect *self);

/**
 * lrg_reel_blur_effect_set_radius:
 * @self: a #LrgReelBlurEffect
 * @radius: new blur radius (<=0 disables the effect)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_blur_effect_set_radius (LrgReelBlurEffect *self,
                                  gint               radius);

G_END_DECLS
