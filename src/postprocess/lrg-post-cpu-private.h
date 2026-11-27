/* lrg-post-cpu-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Private helpers for the CPU paths of the post effects
 * (LrgPostEffectClass.apply_cpu): texel fetches and bilinear samples that
 * behave like the GL samplers the shaders read through (clamp to edge,
 * texel centres at half-integer coordinates), and the GLSL built-ins the
 * shaders use. Everything is static inline so the effects can share it
 * without new exported symbols.
 *
 * Coordinates: "texel space" is the GL texture coordinate times the
 * texture size minus 0.5, so (x, y) is the centre of texel (x, y), the
 * same convention as GL's bilinear filter. A shader's uv + offset * texel
 * for the fragment of pixel (x, y) is texel-space (x + offset.x, y +
 * offset.y) when the sampled texture has the target's size.
 *
 * Rows: the buffers are top-down (row 0 at the top). The GPU stores
 * render textures bottom-up and draws them flipped, so uv-space and the
 * CPU rows differ by a vertical mirror; every shader ported with these
 * helpers is symmetric under that mirror except the ones anchored to
 * gl_FragCoord (film grain, palettize), which convert explicitly.
 */

#pragma once

#include <glib.h>
#include <math.h>
#include <string.h>

G_BEGIN_DECLS

/* clamps a texel index into [0, n - 1] (GL_CLAMP_TO_EDGE) */
static inline gint
lrg_post_cpu_clamp_index (gint i,
                          guint n)
{
    if (i < 0)
        return 0;
    if (i >= (gint)n)
        return (gint)n - 1;
    return i;
}

/* the texel (x, y) of a @channels-wide top-down image, clamped to the edge */
static inline const gfloat *
lrg_post_cpu_texel (const gfloat *img,
                    guint         channels,
                    guint         width,
                    guint         height,
                    gint          x,
                    gint          y)
{
    x = lrg_post_cpu_clamp_index (x, width);
    y = lrg_post_cpu_clamp_index (y, height);
    return img + ((gsize)y * width + (gsize)x) * channels;
}

/*
 * A bilinear sample of the first three channels at texel-space (fx, fy),
 * clamped to the edge: what texture() returns from a GL_LINEAR,
 * GL_CLAMP_TO_EDGE texture (without the GPU's reduced filter precision).
 */
static inline void
lrg_post_cpu_bilinear (const gfloat *img,
                       guint         channels,
                       guint         width,
                       guint         height,
                       gfloat        fx,
                       gfloat        fy,
                       gfloat       *out)
{
    gfloat x0f = floorf (fx);
    gfloat y0f = floorf (fy);
    gfloat tx = fx - x0f;
    gfloat ty = fy - y0f;
    gint   x0 = (gint)x0f;
    gint   y0 = (gint)y0f;
    const gfloat *a = lrg_post_cpu_texel (img, channels, width, height, x0, y0);
    const gfloat *b = lrg_post_cpu_texel (img, channels, width, height, x0 + 1, y0);
    const gfloat *c = lrg_post_cpu_texel (img, channels, width, height, x0, y0 + 1);
    const gfloat *d = lrg_post_cpu_texel (img, channels, width, height, x0 + 1, y0 + 1);
    guint k;

    for (k = 0; k < 3; k++)
    {
        gfloat top = a[k] + (b[k] - a[k]) * tx;
        gfloat bottom = c[k] + (d[k] - c[k]) * tx;

        out[k] = top + (bottom - top) * ty;
    }
}

/*
 * A bilinear sample of a texture of @sw x @sh texels at the GL texture
 * coordinate (u, v): texel space is (u * sw - 0.5, v * sh - 0.5).
 */
static inline void
lrg_post_cpu_sample_uv (const gfloat *img,
                        guint         channels,
                        guint         sw,
                        guint         sh,
                        gfloat        u,
                        gfloat        v,
                        gfloat       *out)
{
    lrg_post_cpu_bilinear (img, channels, sw, sh,
                           u * (gfloat)sw - 0.5f, v * (gfloat)sh - 0.5f, out);
}

/* GLSL smoothstep() */
static inline gfloat
lrg_post_cpu_smoothstep (gfloat edge0,
                         gfloat edge1,
                         gfloat x)
{
    gfloat t = (x - edge0) / (edge1 - edge0);

    t = CLAMP (t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* the Rec. 601 luma the FXAA and grain shaders use */
static inline gfloat
lrg_post_cpu_luma601 (const gfloat *rgb)
{
    return rgb[0] * 0.299f + rgb[1] * 0.587f + rgb[2] * 0.114f;
}

/*
 * A copy of a @width x @height RGBA float image, for the effects that
 * read neighbours (the shader reads its source while writing the other
 * ping-pong target). Free with g_free().
 */
static inline gfloat *
lrg_post_cpu_dup (const gfloat *rgba,
                  guint         width,
                  guint         height)
{
    return (gfloat *)g_memdup2 (rgba, (gsize)width * height * 4 * sizeof (gfloat));
}

G_END_DECLS
