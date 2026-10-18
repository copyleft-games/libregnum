/* lrg-hash-noise-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Integer hashing and value noise shared by LrgTerrainDetail,
 * LrgGrassField and LrgTreeMesh. Not installed, not introspected.
 * Everything here is pure integer and float arithmetic, so a seed and
 * coordinates give the same answer on every machine and thread (no
 * global random state).
 */

#pragma once

#include <glib.h>
#include <math.h>

G_BEGIN_DECLS

/* a 32-bit finaliser (lowbias32): good avalanche, cheap */
static inline guint32
lrg_hash_mix (guint32 h)
{
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    h ^= h >> 16;
    return h;
}

/* hash of a seed and two signed lattice coordinates */
static inline guint32
lrg_hash_2d (guint32 seed,
             gint64  x,
             gint64  z)
{
    guint32 h;

    h = lrg_hash_mix (seed ^ 0x9e3779b9U);
    h = lrg_hash_mix (h ^ (guint32)(x & 0xffffffff));
    h = lrg_hash_mix (h ^ (guint32)((guint64)x >> 32) ^ 0x85ebca6bU);
    h = lrg_hash_mix (h ^ (guint32)(z & 0xffffffff));
    h = lrg_hash_mix (h ^ (guint32)((guint64)z >> 32) ^ 0xc2b2ae35U);
    return h;
}

/* [0, 1) from a hash */
static inline gfloat
lrg_hash_unit (guint32 h)
{
    return (gfloat)(h >> 8) * (1.0f / 16777216.0f);
}

/* a small sequential generator seeded from a hash (xorshift32) */
static inline guint32
lrg_hash_next (guint32 *state)
{
    guint32 x;

    x = *state != 0 ? *state : 0x6d2b79f5U;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static inline gfloat
lrg_hash_next_unit (guint32 *state)
{
    return lrg_hash_unit (lrg_hash_next (state));
}

/* quintic fade: C2 continuous interpolation */
static inline gdouble
lrg_hash_fade (gdouble t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

/* value noise in [0, 1] on the unit lattice */
static inline gdouble
lrg_value_noise (guint32 seed,
                 gdouble x,
                 gdouble z)
{
    gdouble fx;
    gdouble fz;
    gint64 ix;
    gint64 iz;
    gdouble u;
    gdouble v;
    gdouble a;
    gdouble b;
    gdouble c;
    gdouble d;

    fx = floor (x);
    fz = floor (z);
    ix = (gint64)fx;
    iz = (gint64)fz;
    u = lrg_hash_fade (x - fx);
    v = lrg_hash_fade (z - fz);
    a = lrg_hash_unit (lrg_hash_2d (seed, ix, iz));
    b = lrg_hash_unit (lrg_hash_2d (seed, ix + 1, iz));
    c = lrg_hash_unit (lrg_hash_2d (seed, ix, iz + 1));
    d = lrg_hash_unit (lrg_hash_2d (seed, ix + 1, iz + 1));
    return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v;
}

G_END_DECLS
