/* lrg-heat-haze.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Heat haze post-processing effect: a shimmering screen distortion in
 * a band about the horizon, for hot air over sand and stone.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-post-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_HEAT_HAZE (lrg_heat_haze_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgHeatHaze, lrg_heat_haze, LRG, HEAT_HAZE, LrgPostEffect)

/**
 * lrg_heat_haze_new:
 *
 * Creates a heat haze with strength 0.004 (of the screen), scale 9,
 * speed 2.2, the horizon at 0.55 and a band of 0.18.
 *
 * Returns: (transfer full): a new #LrgHeatHaze
 */
LRG_AVAILABLE_IN_ALL
LrgHeatHaze *
lrg_heat_haze_new (void);

/**
 * lrg_heat_haze_set_strength:
 * @self: an #LrgHeatHaze
 * @strength: the largest shift in screen units (0 = none, at most 0.05)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_heat_haze_set_strength (LrgHeatHaze *self,
                            gfloat       strength);

/**
 * lrg_heat_haze_get_strength:
 * @self: an #LrgHeatHaze
 *
 * Returns: the strength
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_heat_haze_get_strength (LrgHeatHaze *self);

/**
 * lrg_heat_haze_set_shape:
 * @self: an #LrgHeatHaze
 * @scale: ripples across the screen (> 0)
 * @speed: how fast they rise, in radians per second
 * @horizon: the screen height of the band's centre, 0 (bottom) .. 1 (top)
 * @band: the band's half height in screen units (> 0)
 *
 * The haze is strongest on the horizon line, fades faster above it
 * (thin air over the land) than below (the hot ground).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_heat_haze_set_shape (LrgHeatHaze *self,
                         gfloat       scale,
                         gfloat       speed,
                         gfloat       horizon,
                         gfloat       band);

/**
 * lrg_heat_haze_set_time:
 * @self: an #LrgHeatHaze
 * @time: seconds (the post processor's apply also advances it)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_heat_haze_set_time (LrgHeatHaze *self,
                        gfloat       time);

/**
 * lrg_heat_haze_get_offset:
 * @self: an #LrgHeatHaze
 * @u: screen X, 0 (left) .. 1 (right)
 * @v: screen Y, 0 (bottom) .. 1 (top)
 * @out_du: (out): the horizontal shift of the sample
 * @out_dv: (out): the vertical shift
 *
 * The shader's distortion on the CPU (scaled by the effect's
 * intensity); both shifts are within the strength.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_heat_haze_get_offset (LrgHeatHaze *self,
                          gfloat       u,
                          gfloat       v,
                          gfloat      *out_du,
                          gfloat      *out_dv);

/**
 * lrg_heat_haze_is_identity:
 * @self: an #LrgHeatHaze
 *
 * Returns: %TRUE when the haze shifts nothing (zero strength or intensity)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_heat_haze_is_identity (LrgHeatHaze *self);

/**
 * lrg_heat_haze_draw_texture:
 * @self: an #LrgHeatHaze
 * @texture: the texture to draw (a render texture's colour attachment)
 * @source: the part of @texture in texels (a negative height flips it)
 * @dest: where to draw it, in pixels of the current target
 *
 * Draws @texture through the haze into the current target. Without a
 * window or a compiled shader it draws @texture unchanged.
 *
 * Returns: %TRUE when the haze was applied (or is the identity)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_heat_haze_draw_texture (LrgHeatHaze        *self,
                            GrlTexture         *texture,
                            const GrlRectangle *source,
                            const GrlRectangle *dest);

G_END_DECLS
