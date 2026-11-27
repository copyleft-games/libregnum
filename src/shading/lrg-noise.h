/* lrg-noise.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Seeded gradient and value noise for CPU shading, with band-limited
 * fractal Brownian motion (fBm).
 *
 * Every function is pure: the same coordinates and seed give the same
 * value on every machine and thread (integer hashing, no global random
 * state, no tables built at run time).  The fBm functions take a pixel
 * footprint and fade out the octaves too fine to be represented at
 * that footprint, which is what removes moire from noise-textured
 * shading (supersampling alone only pushes it to higher frequencies).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LRG_NOISE_MAX_OCTAVES:
 *
 * The largest octave count the fBm functions evaluate; larger requests
 * are clamped.
 *
 * Since: 0.14
 */
#define LRG_NOISE_MAX_OCTAVES (32)

/**
 * LRG_NOISE_MAX_COORD:
 *
 * The largest coordinate magnitude the noise functions accept.  Beyond
 * it (and for NaN or infinite coordinates) they return 0: a float has
 * no fractional bits left there anyway.
 *
 * Since: 0.14
 */
#define LRG_NOISE_MAX_COORD (1e15)

/**
 * lrg_noise_value2:
 * @x: the x coordinate
 * @y: the y coordinate
 * @seed: the seed; different seeds give unrelated fields
 *
 * 2D value noise: random values on the integer lattice, blended with
 * the quintic fade 6t^5 - 15t^4 + 10t^3 (C2 continuous).  The lattice
 * hash is the engine's shared lowbias32 hash, so with the same seed
 * the field matches the one used by the terrain and foliage helpers
 * (rescaled to [-1, 1]).
 *
 * Returns: the noise value in [-1, 1]
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_noise_value2 (gfloat  x,
                  gfloat  y,
                  guint32 seed);

/**
 * lrg_noise_value3:
 * @x: the x coordinate
 * @y: the y coordinate
 * @z: the z coordinate
 * @seed: the seed
 *
 * 3D value noise: random lattice values, trilinear with the quintic
 * fade.
 *
 * Returns: the noise value in [-1, 1]
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_noise_value3 (gfloat  x,
                  gfloat  y,
                  gfloat  z,
                  guint32 seed);

/**
 * lrg_noise_simplex2:
 * @x: the x coordinate
 * @y: the y coordinate
 * @seed: the seed
 *
 * 2D simplex noise (Ken Perlin's simplex lattice, after Stefan
 * Gustavson's reference implementation) with hashed unit gradients in
 * 16 directions.  It is zero on the lattice points, smoother and less
 * axis-aligned than value noise, and its features are about one unit
 * across.  The result is scaled to reach close to +-1 and clamped.
 *
 * Returns: the noise value in [-1, 1]
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_noise_simplex2 (gfloat  x,
                    gfloat  y,
                    guint32 seed);

/**
 * lrg_noise_simplex3:
 * @x: the x coordinate
 * @y: the y coordinate
 * @z: the z coordinate
 * @seed: the seed
 *
 * 3D simplex noise with hashed gradients towards the 12 cube edges.
 * Scaled to reach close to +-1 and clamped.
 *
 * Returns: the noise value in [-1, 1]
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_noise_simplex3 (gfloat  x,
                    gfloat  y,
                    gfloat  z,
                    guint32 seed);

/**
 * lrg_noise_fbm2:
 * @x: the x coordinate
 * @y: the y coordinate
 * @octaves: the number of octaves (1 .. %LRG_NOISE_MAX_OCTAVES); 0 or
 *   less returns 0
 * @lacunarity: the frequency ratio between octaves (2 is usual)
 * @gain: the amplitude ratio between octaves (0.5 is usual)
 * @footprint: the size of one output pixel in noise space (the same
 *   units as @x and @y); 0 or less disables band-limiting
 * @seed: the seed
 *
 * Band-limited fractal Brownian motion of 2D simplex noise.  Octave i
 * has frequency lacunarity^i and amplitude gain^i, its own seed and a
 * coordinate offset (so the octaves do not share lattice zeros); the
 * sum is divided by the sum of all amplitudes.
 *
 * Band-limiting: one pixel of size @footprint can only represent
 * wavelengths of at least 2 @footprint (Nyquist).  Each octave is
 * weighted by 1 - smoothstep (0.25, 0.5, frequency x @footprint): full
 * strength while its wavelength is at least 4 pixels, fading smoothly
 * to nothing at 2 pixels, and skipped below that.  A faded octave
 * falls back to its mean (0), which is what averaging it over the
 * pixel would give, so the result approaches a properly filtered
 * (supersampled) image instead of aliasing into moire.  Because the
 * weights change smoothly with @footprint, detail fades in and out
 * without popping as the footprint changes (zoom, distance).  For a
 * flat image @footprint is the noise scale times the pixel size;
 * across a surface use the larger screen-space derivative.
 *
 * Returns: the noise value in [-1, 1] (narrower once octaves fade)
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_noise_fbm2 (gfloat  x,
                gfloat  y,
                gint    octaves,
                gfloat  lacunarity,
                gfloat  gain,
                gfloat  footprint,
                guint32 seed);

/**
 * lrg_noise_fbm3:
 * @x: the x coordinate
 * @y: the y coordinate
 * @z: the z coordinate
 * @octaves: the number of octaves (1 .. %LRG_NOISE_MAX_OCTAVES)
 * @lacunarity: the frequency ratio between octaves
 * @gain: the amplitude ratio between octaves
 * @footprint: the size of one output pixel in noise space; 0 or less
 *   disables band-limiting
 * @seed: the seed
 *
 * Band-limited fBm of 3D simplex noise, with the same octave weighting
 * as lrg_noise_fbm2().  Use it for solid textures evaluated on SDF
 * surfaces (pass the world-space size of a pixel at the hit distance).
 *
 * Returns: the noise value in [-1, 1]
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_noise_fbm3 (gfloat  x,
                gfloat  y,
                gfloat  z,
                gint    octaves,
                gfloat  lacunarity,
                gfloat  gain,
                gfloat  footprint,
                guint32 seed);

G_END_DECLS
