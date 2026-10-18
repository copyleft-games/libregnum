/* lrg-terrain-detail.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTerrainDetail - deterministic heightfield detail ("landforms") to
 * add on top of a coarse heightfield: fractal value noise blended with
 * a ridged, erosion-like multifractal, with optional domain warping.
 * Pure CPU arithmetic on integer hashes, so the same seed and
 * coordinates give the same heights on every machine and thread; a
 * game applies it once where it builds its heights, and collision and
 * drawing then read the same numbers.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_TERRAIN_DETAIL (lrg_terrain_detail_get_type ())

/**
 * LRG_TERRAIN_DETAIL_MAX_OCTAVES:
 *
 * The most noise octaves a #LrgTerrainDetail sums.
 */
#define LRG_TERRAIN_DETAIL_MAX_OCTAVES (8)

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTerrainDetail, lrg_terrain_detail, LRG, TERRAIN_DETAIL, GObject)

/**
 * lrg_terrain_detail_new:
 * @seed: the noise seed
 *
 * Creates a detail generator with amplitude 1, wavelength 64 world
 * units, 4 octaves, ridge 0.5 and warp 0.25.
 *
 * Returns: (transfer full): a new #LrgTerrainDetail
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainDetail *
lrg_terrain_detail_new (guint32 seed);

/**
 * lrg_terrain_detail_get_seed:
 * @self: an #LrgTerrainDetail
 *
 * Returns: the noise seed
 */
LRG_AVAILABLE_IN_ALL
guint32
lrg_terrain_detail_get_seed (LrgTerrainDetail *self);

/**
 * lrg_terrain_detail_set_amplitude:
 * @self: an #LrgTerrainDetail
 * @amplitude: the largest offset in world units (>= 0)
 *
 * lrg_terrain_detail_sample() stays within [-@amplitude, @amplitude].
 */
LRG_AVAILABLE_IN_ALL
void
lrg_terrain_detail_set_amplitude (LrgTerrainDetail *self,
                                  gfloat            amplitude);

/**
 * lrg_terrain_detail_get_amplitude:
 * @self: an #LrgTerrainDetail
 *
 * Returns: the amplitude
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_terrain_detail_get_amplitude (LrgTerrainDetail *self);

/**
 * lrg_terrain_detail_set_wavelength:
 * @self: an #LrgTerrainDetail
 * @wavelength: the size of the largest features in world units (> 0)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_terrain_detail_set_wavelength (LrgTerrainDetail *self,
                                   gdouble           wavelength);

/**
 * lrg_terrain_detail_get_wavelength:
 * @self: an #LrgTerrainDetail
 *
 * Returns: the wavelength of the first octave
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_terrain_detail_get_wavelength (LrgTerrainDetail *self);

/**
 * lrg_terrain_detail_set_octaves:
 * @self: an #LrgTerrainDetail
 * @octaves: noise octaves, 1 .. %LRG_TERRAIN_DETAIL_MAX_OCTAVES
 *
 * Each octave halves the wavelength and the weight of the previous one.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_terrain_detail_set_octaves (LrgTerrainDetail *self,
                                guint             octaves);

/**
 * lrg_terrain_detail_get_octaves:
 * @self: an #LrgTerrainDetail
 *
 * Returns: the octave count
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_terrain_detail_get_octaves (LrgTerrainDetail *self);

/**
 * lrg_terrain_detail_set_ridge:
 * @self: an #LrgTerrainDetail
 * @ridge: 0 (rolling fractal noise) .. 1 (sharp ridges and gullies)
 *
 * The ridged part is a multifractal whose finer octaves are weighted by
 * the coarser ones, so detail gathers on crests and fades in the
 * valleys, as erosion leaves it.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_terrain_detail_set_ridge (LrgTerrainDetail *self,
                              gfloat            ridge);

/**
 * lrg_terrain_detail_get_ridge:
 * @self: an #LrgTerrainDetail
 *
 * Returns: the ridge blend
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_terrain_detail_get_ridge (LrgTerrainDetail *self);

/**
 * lrg_terrain_detail_set_warp:
 * @self: an #LrgTerrainDetail
 * @warp: domain warp as a share of the wavelength, 0 .. 1
 *
 * Shifts the sample point by low-frequency noise first, which bends
 * the ridges instead of lining them up on the noise lattice.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_terrain_detail_set_warp (LrgTerrainDetail *self,
                             gfloat            warp);

/**
 * lrg_terrain_detail_get_warp:
 * @self: an #LrgTerrainDetail
 *
 * Returns: the warp
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_terrain_detail_get_warp (LrgTerrainDetail *self);

/**
 * lrg_terrain_detail_sample:
 * @self: an #LrgTerrainDetail
 * @x: world X
 * @z: world Z
 *
 * The detail offset at (@x, @z): continuous, deterministic, within
 * [-amplitude, amplitude].
 *
 * Returns: the height offset in world units
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_terrain_detail_sample (LrgTerrainDetail *self,
                           gdouble           x,
                           gdouble           z);

/**
 * lrg_terrain_detail_apply:
 * @self: an #LrgTerrainDetail
 * @heights: (array length=n_heights): a row-major heightfield,
 *   @columns x @rows, changed in place
 * @n_heights: number of entries in @heights (columns * rows)
 * @columns: samples per row
 * @rows: rows
 * @origin_x: world X of sample (0, 0)
 * @origin_z: world Z of sample (0, 0)
 * @step_x: world X between columns
 * @step_z: world Z between rows (may be negative)
 * @weights: (array length=n_heights) (nullable): per-sample weights
 *   (0 keeps a height, 1 takes the full detail), or %NULL for 1
 * @scale: multiplies every offset (for heightfields in other units,
 *   e.g. 1 / max height for normalised ones)
 *
 * heights[r * columns + c] += @scale * weight * sample (origin +
 * (c * @step_x, r * @step_z)).  Nothing changes when @n_heights is not
 * @columns * @rows.
 *
 * Returns: %TRUE when the heightfield was changed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_terrain_detail_apply (LrgTerrainDetail *self,
                          gfloat           *heights,
                          gsize             n_heights,
                          guint             columns,
                          guint             rows,
                          gdouble           origin_x,
                          gdouble           origin_z,
                          gdouble           step_x,
                          gdouble           step_z,
                          const gfloat     *weights,
                          gfloat            scale);

G_END_DECLS
