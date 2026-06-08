/* lrg-reel-drop-shadow-effect.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelDropShadowEffect - offset drop-shadow for a reel clip layer.
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

#define LRG_TYPE_REEL_DROP_SHADOW_EFFECT (lrg_reel_drop_shadow_effect_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelDropShadowEffect, lrg_reel_drop_shadow_effect,
                      LRG, REEL_DROP_SHADOW_EFFECT, LrgReelEffect)

/**
 * lrg_reel_drop_shadow_effect_new:
 *
 * Creates a new #LrgReelDropShadowEffect with default settings:
 * offset (4, 4), blur_radius 6, shadow color semi-transparent black.
 *
 * Returns: (transfer full): a new #LrgReelDropShadowEffect
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelDropShadowEffect *
lrg_reel_drop_shadow_effect_new (void);

/**
 * lrg_reel_drop_shadow_effect_get_offset_x:
 * @self: a #LrgReelDropShadowEffect
 *
 * Returns: the horizontal shadow offset in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_drop_shadow_effect_get_offset_x (LrgReelDropShadowEffect *self);

/**
 * lrg_reel_drop_shadow_effect_set_offset_x:
 * @self: a #LrgReelDropShadowEffect
 * @offset_x: horizontal shadow offset in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_drop_shadow_effect_set_offset_x (LrgReelDropShadowEffect *self,
                                           gint                     offset_x);

/**
 * lrg_reel_drop_shadow_effect_get_offset_y:
 * @self: a #LrgReelDropShadowEffect
 *
 * Returns: the vertical shadow offset in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_drop_shadow_effect_get_offset_y (LrgReelDropShadowEffect *self);

/**
 * lrg_reel_drop_shadow_effect_set_offset_y:
 * @self: a #LrgReelDropShadowEffect
 * @offset_y: vertical shadow offset in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_drop_shadow_effect_set_offset_y (LrgReelDropShadowEffect *self,
                                           gint                     offset_y);

/**
 * lrg_reel_drop_shadow_effect_get_blur_radius:
 * @self: a #LrgReelDropShadowEffect
 *
 * Returns: the shadow blur radius
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_drop_shadow_effect_get_blur_radius (LrgReelDropShadowEffect *self);

/**
 * lrg_reel_drop_shadow_effect_set_blur_radius:
 * @self: a #LrgReelDropShadowEffect
 * @blur_radius: shadow blur radius (0 = sharp)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_drop_shadow_effect_set_blur_radius (LrgReelDropShadowEffect *self,
                                              gint                     blur_radius);

/**
 * lrg_reel_drop_shadow_effect_get_shadow_color:
 * @self: a #LrgReelDropShadowEffect
 * @out_color: (out caller-allocates): return location for the shadow color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_drop_shadow_effect_get_shadow_color (LrgReelDropShadowEffect *self,
                                               GrlColor                *out_color);

/**
 * lrg_reel_drop_shadow_effect_set_shadow_color:
 * @self: a #LrgReelDropShadowEffect
 * @color: (not nullable): new shadow color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_drop_shadow_effect_set_shadow_color (LrgReelDropShadowEffect *self,
                                               const GrlColor          *color);

G_END_DECLS
