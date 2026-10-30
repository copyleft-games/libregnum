/* lrg-procgen-noise.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Procedural generator: noise heightmap terrain.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-procgen-generator.h"

G_BEGIN_DECLS

#define LRG_TYPE_PROCGEN_NOISE (lrg_procgen_noise_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgProcgenNoise, lrg_procgen_noise, LRG, PROCGEN_NOISE, LrgProcgenGenerator)

/**
 * lrg_procgen_noise_new:
 * @width: grid width in cells
 * @height: grid height in cells
 *
 * Creates a generator of noise heightmap terrain. A fixed-point value-noise heightmap (bit-identical on every platform),
 * thresholded into water, floor and wall cells.
 *
 * Returns: (transfer full): a new #LrgProcgenNoise
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenNoise * lrg_procgen_noise_new (guint width,
                                         guint height);

/**
 * lrg_procgen_noise_get_octaves:
 * @self: a generator
 *
 * Returns: the #LrgProcgenNoise:octaves property
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_noise_get_octaves (LrgProcgenNoise *self);

/**
 * lrg_procgen_noise_set_octaves:
 * @self: a generator
 * @value: the new value
 *
 * Sets the #LrgProcgenNoise:octaves property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_noise_set_octaves (LrgProcgenNoise *self,
                                    guint value);

/**
 * lrg_procgen_noise_get_scale:
 * @self: a generator
 *
 * Returns: the #LrgProcgenNoise:scale property
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_noise_get_scale (LrgProcgenNoise *self);

/**
 * lrg_procgen_noise_set_scale:
 * @self: a generator
 * @value: the new value
 *
 * Sets the #LrgProcgenNoise:scale property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_noise_set_scale (LrgProcgenNoise *self,
                                  guint value);

/**
 * lrg_procgen_noise_get_water_percent:
 * @self: a generator
 *
 * Returns: the #LrgProcgenNoise:water-percent property
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_noise_get_water_percent (LrgProcgenNoise *self);

/**
 * lrg_procgen_noise_set_water_percent:
 * @self: a generator
 * @value: the new value
 *
 * Sets the #LrgProcgenNoise:water-percent property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_noise_set_water_percent (LrgProcgenNoise *self,
                                          guint value);

/**
 * lrg_procgen_noise_get_wall_percent:
 * @self: a generator
 *
 * Returns: the #LrgProcgenNoise:wall-percent property
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_noise_get_wall_percent (LrgProcgenNoise *self);

/**
 * lrg_procgen_noise_set_wall_percent:
 * @self: a generator
 * @value: the new value
 *
 * Sets the #LrgProcgenNoise:wall-percent property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_noise_set_wall_percent (LrgProcgenNoise *self,
                                         guint value);

G_END_DECLS
