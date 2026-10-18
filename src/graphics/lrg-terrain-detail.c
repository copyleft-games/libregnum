/* lrg-terrain-detail.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Landform detail: fractal value noise (rolling relief) blended with a
 * ridged multifractal (crests and gullies), both on integer hashes of
 * the lattice (lrg-hash-noise-private.h), so the result depends only on
 * the seed, the parameters and the coordinates.
 */

#include "config.h"

#include <math.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "graphics/lrg-terrain-detail.h"
#include "graphics/lrg-hash-noise-private.h"

/* the ridged octaves: weight carried to the next octave */
#define RIDGE_GAIN (2.0)

struct _LrgTerrainDetail
{
    GObject  parent_instance;

    guint32  seed;
    gfloat   amplitude;
    gdouble  wavelength;
    guint    octaves;
    gfloat   ridge;
    gfloat   warp;
};

G_DEFINE_FINAL_TYPE (LrgTerrainDetail, lrg_terrain_detail, G_TYPE_OBJECT)

static void
lrg_terrain_detail_class_init (LrgTerrainDetailClass *klass)
{
}

static void
lrg_terrain_detail_init (LrgTerrainDetail *self)
{
    self->amplitude = 1.0f;
    self->wavelength = 64.0;
    self->octaves = 4;
    self->ridge = 0.5f;
    self->warp = 0.25f;
}

LrgTerrainDetail *
lrg_terrain_detail_new (guint32 seed)
{
    LrgTerrainDetail *self;

    self = g_object_new (LRG_TYPE_TERRAIN_DETAIL, NULL);
    self->seed = seed;
    return self;
}

guint32
lrg_terrain_detail_get_seed (LrgTerrainDetail *self)
{
    g_return_val_if_fail (LRG_IS_TERRAIN_DETAIL (self), 0);
    return self->seed;
}

void
lrg_terrain_detail_set_amplitude (LrgTerrainDetail *self,
                                  gfloat            amplitude)
{
    g_return_if_fail (LRG_IS_TERRAIN_DETAIL (self));
    self->amplitude = isfinite (amplitude) ? MAX (amplitude, 0.0f) : 0.0f;
}

gfloat
lrg_terrain_detail_get_amplitude (LrgTerrainDetail *self)
{
    g_return_val_if_fail (LRG_IS_TERRAIN_DETAIL (self), 0.0f);
    return self->amplitude;
}

void
lrg_terrain_detail_set_wavelength (LrgTerrainDetail *self,
                                   gdouble           wavelength)
{
    g_return_if_fail (LRG_IS_TERRAIN_DETAIL (self));
    if (isfinite (wavelength) && wavelength > 0.0)
        self->wavelength = wavelength;
}

gdouble
lrg_terrain_detail_get_wavelength (LrgTerrainDetail *self)
{
    g_return_val_if_fail (LRG_IS_TERRAIN_DETAIL (self), 0.0);
    return self->wavelength;
}

void
lrg_terrain_detail_set_octaves (LrgTerrainDetail *self,
                                guint             octaves)
{
    g_return_if_fail (LRG_IS_TERRAIN_DETAIL (self));
    self->octaves = CLAMP (octaves, 1u, (guint)LRG_TERRAIN_DETAIL_MAX_OCTAVES);
}

guint
lrg_terrain_detail_get_octaves (LrgTerrainDetail *self)
{
    g_return_val_if_fail (LRG_IS_TERRAIN_DETAIL (self), 0);
    return self->octaves;
}

void
lrg_terrain_detail_set_ridge (LrgTerrainDetail *self,
                              gfloat            ridge)
{
    g_return_if_fail (LRG_IS_TERRAIN_DETAIL (self));
    self->ridge = isfinite (ridge) ? CLAMP (ridge, 0.0f, 1.0f) : 0.0f;
}

gfloat
lrg_terrain_detail_get_ridge (LrgTerrainDetail *self)
{
    g_return_val_if_fail (LRG_IS_TERRAIN_DETAIL (self), 0.0f);
    return self->ridge;
}

void
lrg_terrain_detail_set_warp (LrgTerrainDetail *self,
                             gfloat            warp)
{
    g_return_if_fail (LRG_IS_TERRAIN_DETAIL (self));
    self->warp = isfinite (warp) ? CLAMP (warp, 0.0f, 1.0f) : 0.0f;
}

gfloat
lrg_terrain_detail_get_warp (LrgTerrainDetail *self)
{
    g_return_val_if_fail (LRG_IS_TERRAIN_DETAIL (self), 0.0f);
    return self->warp;
}

/*
 * detail_unit:
 * The detail at lattice coordinates (u, v) (world / wavelength) in
 * [-1, 1]: the octaves of signed value noise (each half the wavelength
 * and half the weight of the last) and the ridged multifractal, each
 * normalised by its largest possible sum, then blended by the ridge.
 */
static gdouble
detail_unit (LrgTerrainDetail *self,
             gdouble           u,
             gdouble           v)
{
    gdouble fbm;
    gdouble fbm_norm;
    gdouble ridged;
    gdouble ridged_norm;
    gdouble weight;
    gdouble amp;
    gdouble freq;
    gdouble result;
    guint i;

    /* domain warp: two low-frequency noises shift the point */
    if (self->warp > 0.0f)
    {
        gdouble wu;
        gdouble wv;

        wu = lrg_value_noise (self->seed ^ 0x51ed270bU, u * 0.5 + 17.3, v * 0.5 - 4.1) * 2.0 - 1.0;
        wv = lrg_value_noise (self->seed ^ 0x2c1b3c6dU, u * 0.5 - 9.7, v * 0.5 + 31.9) * 2.0 - 1.0;
        u += wu * (gdouble)self->warp;
        v += wv * (gdouble)self->warp;
    }

    fbm = 0.0;
    fbm_norm = 0.0;
    ridged = 0.0;
    ridged_norm = 0.0;
    weight = 1.0;
    amp = 1.0;
    freq = 1.0;
    for (i = 0; i < self->octaves; i++)
    {
        guint32 octave_seed;
        gdouble n;
        gdouble signal;

        octave_seed = self->seed + 0x632be5abU * (i + 1);
        n = lrg_value_noise (octave_seed, u * freq, v * freq) * 2.0 - 1.0;
        fbm += n * amp;
        fbm_norm += amp;

        /* ridged: sharp at the noise's zero crossings, finer octaves
         * where the coarser ones are high */
        signal = 1.0 - fabs (n);
        signal *= signal;
        signal *= weight;
        weight = CLAMP (signal * RIDGE_GAIN, 0.0, 1.0);
        ridged += signal * amp;
        ridged_norm += amp;

        amp *= 0.5;
        freq *= 2.0;
    }
    fbm /= fbm_norm;
    ridged = ridged / ridged_norm * 2.0 - 1.0;
    result = fbm * (1.0 - (gdouble)self->ridge) + ridged * (gdouble)self->ridge;
    return CLAMP (result, -1.0, 1.0);
}

gfloat
lrg_terrain_detail_sample (LrgTerrainDetail *self,
                           gdouble           x,
                           gdouble           z)
{
    g_return_val_if_fail (LRG_IS_TERRAIN_DETAIL (self), 0.0f);

    if (self->amplitude <= 0.0f || !isfinite (x) || !isfinite (z))
        return 0.0f;
    return (gfloat)(detail_unit (self, x / self->wavelength, z / self->wavelength) *
                    (gdouble)self->amplitude);
}

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
                          gfloat            scale)
{
    guint r;
    guint c;

    g_return_val_if_fail (LRG_IS_TERRAIN_DETAIL (self), FALSE);
    g_return_val_if_fail (heights != NULL || n_heights == 0, FALSE);

    if (columns == 0 || rows == 0 || n_heights != (gsize)columns * rows ||
        self->amplitude <= 0.0f || !isfinite (scale) || scale == 0.0f)
        return FALSE;
    for (r = 0; r < rows; r++)
    {
        for (c = 0; c < columns; c++)
        {
            gsize i;
            gfloat w;

            i = (gsize)r * columns + c;
            w = weights != NULL ? weights[i] : 1.0f;
            if (!(w > 0.0f))
                continue;
            heights[i] += scale * w *
                          lrg_terrain_detail_sample (self, origin_x + (gdouble)c * step_x,
                                                     origin_z + (gdouble)r * step_z);
        }
    }
    return TRUE;
}
