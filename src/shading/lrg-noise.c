/* lrg-noise.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Seeded value and simplex noise and band-limited fBm.  The simplex
 * code follows Stefan Gustavson's "Simplex noise demystified" (2005),
 * with the permutation table replaced by the engine's integer hash so
 * any 32-bit seed works and nothing is built at run time.
 */

#include "config.h"

#include <math.h>

#include "lrg-noise.h"
#include "../graphics/lrg-hash-noise-private.h"

/*
 * Output scales, measured so the extremes land just inside +-1 (the
 * result is clamped as well).  2D: unit gradients, (0.5 - r^2)^4
 * falloff; 3D: cube-edge gradients of length sqrt 2, (0.6 - r^2)^4.
 */
#define SIMPLEX2_SCALE (99.0)
#define SIMPLEX3_SCALE (32.0)

/* the 2D gradients: 16 unit vectors at multiples of 22.5 degrees */
static const gdouble grad2[16][2] = {
	{  1.000000000,  0.000000000 }, {  0.923879533,  0.382683432 },
	{  0.707106781,  0.707106781 }, {  0.382683432,  0.923879533 },
	{  0.000000000,  1.000000000 }, { -0.382683432,  0.923879533 },
	{ -0.707106781,  0.707106781 }, { -0.923879533,  0.382683432 },
	{ -1.000000000,  0.000000000 }, { -0.923879533, -0.382683432 },
	{ -0.707106781, -0.707106781 }, { -0.382683432, -0.923879533 },
	{  0.000000000, -1.000000000 }, {  0.382683432, -0.923879533 },
	{  0.707106781, -0.707106781 }, {  0.923879533, -0.382683432 }
};

/* the 3D gradients: the 12 cube edge midpoints (Perlin's set) */
static const gdouble grad3[12][3] = {
	{  1,  1,  0 }, { -1,  1,  0 }, {  1, -1,  0 }, { -1, -1,  0 },
	{  1,  0,  1 }, { -1,  0,  1 }, {  1,  0, -1 }, { -1,  0, -1 },
	{  0,  1,  1 }, {  0, -1,  1 }, {  0,  1, -1 }, {  0, -1, -1 }
};

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* coordinates the lattice arithmetic can handle (rejects NaN and inf) */
static inline gboolean
coord_ok (gdouble v)
{
	return fabs (v) < LRG_NOISE_MAX_COORD;
}

/* hash of a seed and three signed lattice coordinates */
static inline guint32
hash_3d (guint32 seed,
         gint64  x,
         gint64  y,
         gint64  z)
{
	guint32 h;

	h = lrg_hash_2d (seed, x, y);
	h = lrg_hash_mix (h ^ (guint32)(z & 0xffffffff) ^ 0x27d4eb2fU);
	h = lrg_hash_mix (h ^ (guint32)((guint64)z >> 32) ^ 0x165667b1U);
	return h;
}

static inline gdouble
lerp (gdouble a,
      gdouble b,
      gdouble t)
{
	return a + (b - a) * t;
}

static inline gfloat
clamp_unit (gdouble v)
{
	return (gfloat)CLAMP (v, -1.0, 1.0);
}

/* ==========================================================================
 * Value noise
 * ========================================================================== */

gfloat
lrg_noise_value2 (gfloat  x,
                  gfloat  y,
                  guint32 seed)
{
	if (!coord_ok (x) || !coord_ok (y))
		return 0.0f;

	/* the shared field is in [0, 1]: rescale */
	return clamp_unit (2.0 * lrg_value_noise (seed, (gdouble)x, (gdouble)y) - 1.0);
}

gfloat
lrg_noise_value3 (gfloat  x,
                  gfloat  y,
                  gfloat  z,
                  guint32 seed)
{
	gdouble fx;
	gdouble fy;
	gdouble fz;
	gint64 ix;
	gint64 iy;
	gint64 iz;
	gdouble u;
	gdouble v;
	gdouble w;
	gdouble c[2][2][2];
	gint a;
	gint b;
	gint d;

	if (!coord_ok (x) || !coord_ok (y) || !coord_ok (z))
		return 0.0f;

	/* lattice cell and the faded position inside it */
	fx = floor ((gdouble)x);
	fy = floor ((gdouble)y);
	fz = floor ((gdouble)z);
	ix = (gint64)fx;
	iy = (gint64)fy;
	iz = (gint64)fz;
	u = lrg_hash_fade ((gdouble)x - fx);
	v = lrg_hash_fade ((gdouble)y - fy);
	w = lrg_hash_fade ((gdouble)z - fz);

	/* the eight corner values, then trilinear blending */
	for (a = 0; a < 2; a++)
		for (b = 0; b < 2; b++)
			for (d = 0; d < 2; d++)
				c[a][b][d] = lrg_hash_unit (hash_3d (seed, ix + a, iy + b, iz + d));

	return clamp_unit (2.0 * lerp (lerp (lerp (c[0][0][0], c[1][0][0], u),
	                                     lerp (c[0][1][0], c[1][1][0], u), v),
	                               lerp (lerp (c[0][0][1], c[1][0][1], u),
	                                     lerp (c[0][1][1], c[1][1][1], u), v),
	                               w) - 1.0);
}

/* ==========================================================================
 * Simplex noise
 * ========================================================================== */

/* one simplex corner's contribution in 2D */
static inline gdouble
corner2 (guint32 seed,
         gint64  i,
         gint64  j,
         gdouble x,
         gdouble y)
{
	gdouble t;
	const gdouble *g;

	t = 0.5 - x * x - y * y;
	if (t <= 0.0)
		return 0.0;
	g = grad2[lrg_hash_2d (seed, i, j) >> 28];
	t *= t;
	return t * t * (g[0] * x + g[1] * y);
}

gfloat
lrg_noise_simplex2 (gfloat  x,
                    gfloat  y,
                    guint32 seed)
{
	const gdouble f2 = 0.36602540378443865;	/* (sqrt 3 - 1) / 2 */
	const gdouble g2 = 0.21132486540518713;	/* (3 - sqrt 3) / 6 */
	gdouble xd;
	gdouble yd;
	gdouble s;
	gdouble t;
	gdouble x0;
	gdouble y0;
	gint64 i;
	gint64 j;
	gint i1;
	gint j1;
	gdouble n;

	if (!coord_ok (x) || !coord_ok (y))
		return 0.0f;
	xd = (gdouble)x;
	yd = (gdouble)y;

	/* skew into the simplex lattice to find the cell */
	s = (xd + yd) * f2;
	i = (gint64)floor (xd + s);
	j = (gint64)floor (yd + s);

	/* unskew: the position relative to the cell's first corner */
	t = (gdouble)(i + j) * g2;
	x0 = xd - ((gdouble)i - t);
	y0 = yd - ((gdouble)j - t);

	/* which of the two triangles of the rhombus */
	i1 = x0 > y0 ? 1 : 0;
	j1 = 1 - i1;

	/* the three corners */
	n = corner2 (seed, i, j, x0, y0);
	n += corner2 (seed, i + i1, j + j1, x0 - i1 + g2, y0 - j1 + g2);
	n += corner2 (seed, i + 1, j + 1, x0 - 1.0 + 2.0 * g2, y0 - 1.0 + 2.0 * g2);
	return clamp_unit (SIMPLEX2_SCALE * n);
}

/* one simplex corner's contribution in 3D */
static inline gdouble
corner3 (guint32 seed,
         gint64  i,
         gint64  j,
         gint64  k,
         gdouble x,
         gdouble y,
         gdouble z)
{
	gdouble t;
	const gdouble *g;

	t = 0.6 - x * x - y * y - z * z;
	if (t <= 0.0)
		return 0.0;
	/* (h >> 8) % 12: 2^24 is not a multiple of 12, but the bias is below 1e-6 */
	g = grad3[(hash_3d (seed, i, j, k) >> 8) % 12];
	t *= t;
	return t * t * (g[0] * x + g[1] * y + g[2] * z);
}

gfloat
lrg_noise_simplex3 (gfloat  x,
                    gfloat  y,
                    gfloat  z,
                    guint32 seed)
{
	const gdouble f3 = 1.0 / 3.0;
	const gdouble g3 = 1.0 / 6.0;
	gdouble xd;
	gdouble yd;
	gdouble zd;
	gdouble s;
	gdouble t;
	gdouble x0;
	gdouble y0;
	gdouble z0;
	gint64 i;
	gint64 j;
	gint64 k;
	gint i1, j1, k1;
	gint i2, j2, k2;
	gdouble n;

	if (!coord_ok (x) || !coord_ok (y) || !coord_ok (z))
		return 0.0f;
	xd = (gdouble)x;
	yd = (gdouble)y;
	zd = (gdouble)z;

	/* skew to find the cell, unskew to get the local position */
	s = (xd + yd + zd) * f3;
	i = (gint64)floor (xd + s);
	j = (gint64)floor (yd + s);
	k = (gint64)floor (zd + s);
	t = (gdouble)(i + j + k) * g3;
	x0 = xd - ((gdouble)i - t);
	y0 = yd - ((gdouble)j - t);
	z0 = zd - ((gdouble)k - t);

	/* rank the local coordinates: which of the six tetrahedra */
	if (x0 >= y0)
	{
		if (y0 >= z0)
		{ i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
		else if (x0 >= z0)
		{ i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
		else
		{ i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
	}
	else
	{
		if (y0 < z0)
		{ i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
		else if (x0 < z0)
		{ i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
		else
		{ i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
	}

	/* the four corners */
	n = corner3 (seed, i, j, k, x0, y0, z0);
	n += corner3 (seed, i + i1, j + j1, k + k1,
	              x0 - i1 + g3, y0 - j1 + g3, z0 - k1 + g3);
	n += corner3 (seed, i + i2, j + j2, k + k2,
	              x0 - i2 + 2.0 * g3, y0 - j2 + 2.0 * g3, z0 - k2 + 2.0 * g3);
	n += corner3 (seed, i + 1, j + 1, k + 1,
	              x0 - 1.0 + 3.0 * g3, y0 - 1.0 + 3.0 * g3, z0 - 1.0 + 3.0 * g3);
	return clamp_unit (SIMPLEX3_SCALE * n);
}

/* ==========================================================================
 * Band-limited fBm
 * ========================================================================== */

/*
 * octave_weight:
 * The band-limiting weight of an octave of frequency @freq at pixel
 * size @footprint: 1 - smoothstep (0.25, 0.5, freq * footprint), i.e.
 * 1 while the wavelength is >= 4 pixels and 0 once it is <= 2.
 */
static inline gdouble
octave_weight (gdouble freq,
               gdouble footprint)
{
	gdouble s;

	if (!(footprint > 0.0))
		return 1.0;
	s = (freq * footprint - 0.25) / 0.25;
	s = CLAMP (s, 0.0, 1.0);
	return 1.0 - s * s * (3.0 - 2.0 * s);
}

/*
 * octave_setup:
 * The seed and coordinate offset of octave @i, both derived from the
 * caller's seed, so octaves are uncorrelated and do not share the
 * simplex lattice zeros at the origin.
 */
static inline guint32
octave_setup (guint32  seed,
              gint     i,
              gdouble  offset[3])
{
	guint32 h;

	h = lrg_hash_mix (seed ^ ((guint32)(i + 1) * 0x9e3779b9U));
	offset[0] = lrg_hash_unit (lrg_hash_mix (h ^ 0x68e31da4U)) * 4096.0;
	offset[1] = lrg_hash_unit (lrg_hash_mix (h ^ 0xb5297a4dU)) * 4096.0;
	offset[2] = lrg_hash_unit (lrg_hash_mix (h ^ 0x1b56c4e9U)) * 4096.0;
	return h;
}

/*
 * fbm:
 * Shared octave loop; @dims selects 2D or 3D simplex noise.
 */
static gfloat
fbm (gint     dims,
     gdouble  x,
     gdouble  y,
     gdouble  z,
     gint     octaves,
     gdouble  lacunarity,
     gdouble  gain,
     gdouble  footprint,
     guint32  seed)
{
	gdouble sum;
	gdouble norm;
	gdouble amp;
	gdouble freq;
	gint i;

	if (octaves <= 0 || isnan (lacunarity) || isnan (gain) ||
	    !coord_ok (x) || !coord_ok (y) || !coord_ok (z))
		return 0.0f;
	octaves = MIN (octaves, LRG_NOISE_MAX_OCTAVES);
	if (isnan (footprint))
		footprint = 0.0;

	sum = 0.0;
	norm = 0.0;
	amp = 1.0;
	freq = 1.0;
	for (i = 0; i < octaves; i++)
	{
		gdouble w;

		/* every octave counts towards the normalisation, faded or not */
		norm += fabs (amp);
		w = octave_weight (freq, footprint);
		if (w > 0.0)
		{
			gdouble off[3];
			guint32 s;
			gdouble n;

			s = octave_setup (seed, i, off);
			if (dims == 2)
				n = lrg_noise_simplex2 ((gfloat)(x * freq + off[0]),
				                        (gfloat)(y * freq + off[1]), s);
			else
				n = lrg_noise_simplex3 ((gfloat)(x * freq + off[0]),
				                        (gfloat)(y * freq + off[1]),
				                        (gfloat)(z * freq + off[2]), s);
			sum += amp * w * n;
		}
		else if (lacunarity > 1.0)
		{
			/* frequencies only grow from here: the rest are all 0 */
			gint j;

			amp *= gain;
			for (j = i + 1; j < octaves; j++)
			{
				norm += fabs (amp);
				amp *= gain;
			}
			break;
		}
		amp *= gain;
		freq *= lacunarity;
	}

	if (!(norm > 0.0) || !isfinite (norm))
		return 0.0f;
	return clamp_unit (sum / norm);
}

gfloat
lrg_noise_fbm2 (gfloat  x,
                gfloat  y,
                gint    octaves,
                gfloat  lacunarity,
                gfloat  gain,
                gfloat  footprint,
                guint32 seed)
{
	return fbm (2, x, y, 0.0, octaves, lacunarity, gain, footprint, seed);
}

gfloat
lrg_noise_fbm3 (gfloat  x,
                gfloat  y,
                gfloat  z,
                gint    octaves,
                gfloat  lacunarity,
                gfloat  gain,
                gfloat  footprint,
                guint32 seed)
{
	return fbm (3, x, y, z, octaves, lacunarity, gain, footprint, seed);
}
