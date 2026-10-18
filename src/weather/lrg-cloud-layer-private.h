/* lrg-cloud-layer-private.h - the cloud noise shared by CPU and GLSL
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Value noise on an integer lattice that repeats every 256 cells, with
 * an integer hash, so the CPU sampler (shadows on objects, tests) and
 * the shaders (the cloud slices, the ground overlay) compute the same
 * clouds.  The shaders take the noise coordinate of draw-space origin
 * folded into [0, 256) by the CPU in double precision, so far-away
 * floating origins keep full float precision on the GPU.
 */

#pragma once

#include <glib.h>
#include <math.h>

#include "weather/lrg-cloud-layer.h"

G_BEGIN_DECLS

/* LRG_CLOUD_NOISE_PERIOD:
 * Lattice cells before the noise repeats (a power of two). */
#define LRG_CLOUD_NOISE_PERIOD (256)

/* LRG_CLOUD_SOFTNESS:
 * Noise range over which a cloud fades from clear to solid. */
#define LRG_CLOUD_SOFTNESS (0.35f)

static inline guint32
lrg_cloud_hash (gint x,
                gint y)
{
    guint32 h;

    h = ((guint32)x & 255u) * 374761393u + ((guint32)y & 255u) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static inline gdouble
lrg_cloud_value (gint x,
                 gint y)
{
    return (gdouble)(lrg_cloud_hash (x, y) & 65535u) / 65535.0;
}

static inline gdouble
lrg_cloud_noise (gdouble px,
                 gdouble py)
{
    gdouble ix = floor (px);
    gdouble iy = floor (py);
    gdouble fx = px - ix;
    gdouble fy = py - iy;
    gint    cx = (gint)ix;
    gint    cy = (gint)iy;
    gdouble a;
    gdouble b;

    fx = fx * fx * (3.0 - 2.0 * fx);
    fy = fy * fy * (3.0 - 2.0 * fy);
    a = lrg_cloud_value (cx, cy) + (lrg_cloud_value (cx + 1, cy) - lrg_cloud_value (cx, cy)) * fx;
    b = lrg_cloud_value (cx, cy + 1)
        + (lrg_cloud_value (cx + 1, cy + 1) - lrg_cloud_value (cx, cy + 1)) * fx;
    return a + (b - a) * fy;
}

static inline gdouble
lrg_cloud_fbm (gdouble px,
               gdouble py)
{
    gdouble sum = 0.0;
    gdouble amp = 0.5;
    gint    o;

    for (o = 0; o < 4; o++)
    {
        sum += amp * lrg_cloud_noise (px + 17.0 * o, py + 31.0 * o);
        px *= 2.0;
        py *= 2.0;
        amp *= 0.5;
    }
    return sum / 0.9375;
}

static inline gdouble
lrg_cloud_density_at (gdouble px,
                      gdouble py,
                      gdouble coverage)
{
    gdouble d;

    if (coverage <= 0.0)
        return 0.0;
    d = (lrg_cloud_fbm (px, py) - (1.0 - coverage)) / LRG_CLOUD_SOFTNESS;
    return CLAMP (d, 0.0, 1.0);
}

/* the same in GLSL 330 */
#define LRG_CLOUD_NOISE_GLSL \
    "uint lrg_cloud_hash(ivec2 c) {\n" \
    "    uint h = uint(c.x & 255) * 374761393u + uint(c.y & 255) * 668265263u;\n" \
    "    h = (h ^ (h >> 13u)) * 1274126177u;\n" \
    "    return h ^ (h >> 16u);\n" \
    "}\n" \
    "float lrg_cloud_value(ivec2 c) {\n" \
    "    return float(lrg_cloud_hash(c) & 65535u) / 65535.0;\n" \
    "}\n" \
    "float lrg_cloud_noise(vec2 p) {\n" \
    "    vec2 i = floor(p);\n" \
    "    vec2 f = p - i;\n" \
    "    ivec2 c = ivec2(i);\n" \
    "    f = f * f * (3.0 - 2.0 * f);\n" \
    "    float a = mix(lrg_cloud_value(c), lrg_cloud_value(c + ivec2(1, 0)), f.x);\n" \
    "    float b = mix(lrg_cloud_value(c + ivec2(0, 1)), lrg_cloud_value(c + ivec2(1, 1)), f.x);\n" \
    "    return mix(a, b, f.y);\n" \
    "}\n" \
    "float lrg_cloud_fbm(vec2 p) {\n" \
    "    float s = 0.0;\n" \
    "    float a = 0.5;\n" \
    "    for (int o = 0; o < 4; o++) {\n" \
    "        s += a * lrg_cloud_noise(p + vec2(17.0, 31.0) * float(o));\n" \
    "        p *= 2.0;\n" \
    "        a *= 0.5;\n" \
    "    }\n" \
    "    return s / 0.9375;\n" \
    "}\n" \
    "float lrg_cloud_density(vec2 p, float coverage) {\n" \
    "    if (coverage <= 0.0) return 0.0;\n" \
    "    return clamp((lrg_cloud_fbm(p) - (1.0 - coverage)) / 0.35, 0.0, 1.0);\n" \
    "}\n"

/*
 * lrg_cloud_layer_get_noise_point:
 * @wx: absolute world x (metres)
 * @wz: absolute world z
 * @out: (out caller-allocates): 2 floats, the noise coordinate of that
 *   point folded into [0, LRG_CLOUD_NOISE_PERIOD)
 */
void
lrg_cloud_layer_get_noise_point (LrgCloudLayer *self,
                                 gdouble        wx,
                                 gdouble        wz,
                                 gfloat        *out);

G_END_DECLS
