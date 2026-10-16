/* lrg-light-set-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal fog helpers shared by LrgLightSet, LrgBillboardBatch and
 * LrgFog. Not installed, not scanned for introspection. The GLSL
 * snippet and lrg_fog_amount() are the same model, so the CPU answer
 * (lrg_light_set_sample_fog(), lrg_fog_get_amount()) is what a fragment
 * at that position gets.
 */

#pragma once

#include <glib-object.h>
#include <raylib.h>
#include "graphics/lrg-light-set.h"

G_BEGIN_DECLS

/*
 * LRG_FOG_GLSL:
 *
 * GLSL 330 fog uniforms plus lrg_fog_amount (vec3 p), the share of the
 * fog colour a fragment at draw-space position @p takes (0 = clear).
 * fogType is -1 (off) or an LrgFogType; fogColor.a caps the amount;
 * fogParams = (density, start, end, height falloff); fogBase is the
 * height fog's base in draw space; fogCamera the eye in draw space.
 */
#define LRG_FOG_GLSL \
    "uniform int fogType;\n" \
    "uniform vec4 fogColor;\n" \
    "uniform vec4 fogParams;\n" \
    "uniform float fogBase;\n" \
    "uniform vec3 fogCamera;\n" \
    "float lrg_fog_amount(vec3 p) {\n" \
    "    if (fogType < 0) return 0.0;\n" \
    "    float d = distance(p, fogCamera);\n" \
    "    float f;\n" \
    "    if (fogType == 0) {\n" \
    "        f = fogParams.x;\n" \
    "    } else if (fogType == 1) {\n" \
    "        if (fogParams.z > fogParams.y)\n" \
    "            f = (d - fogParams.y) / (fogParams.z - fogParams.y);\n" \
    "        else\n" \
    "            f = d >= fogParams.z ? 1.0 : 0.0;\n" \
    "    } else {\n" \
    "        f = 1.0 - exp(-fogParams.x * d);\n" \
    "        if (fogType == 3)\n" \
    "            f *= exp(-fogParams.w * max(p.y - fogBase, 0.0));\n" \
    "    }\n" \
    "    return clamp(f, 0.0, 1.0) * fogColor.a;\n" \
    "}\n"

/*
 * LrgLightSetFogLocations:
 *
 * Uniform locations of LRG_FOG_GLSL in one shader (-1 when the
 * compiler dropped an unused uniform).
 */
typedef struct
{
    gint type;
    gint color;
    gint params;
    gint base;
    gint camera;
} LrgLightSetFogLocations;

/*
 * lrg_fog_amount:
 *
 * The fog model on the CPU: the share of the fog colour at @distance
 * from the eye and world @height, before the opacity cap (colour
 * alpha). LINEAR with end <= start is a hard edge at @end; UNIFORM is
 * @density everywhere; HEIGHT thins EXPONENTIAL above @base_height by
 * exp (-@falloff * (height - base)). Always in [0, 1].
 */
G_GNUC_INTERNAL
gfloat
lrg_fog_amount (LrgFogType type,
                gfloat     density,
                gfloat     start,
                gfloat     end,
                gfloat     base_height,
                gfloat     falloff,
                gfloat     distance,
                gfloat     height);

/*
 * lrg_light_set_fog_locations:
 *
 * Looks up the LRG_FOG_GLSL uniforms of @shader.
 */
G_GNUC_INTERNAL
void
lrg_light_set_fog_locations (Shader                   shader,
                             LrgLightSetFogLocations *out_locs);

/*
 * lrg_light_set_upload_fog:
 *
 * Uploads @self's fog (or fogType -1 when @self is NULL or has none) to
 * @shader, with the eye of the last selection in draw space. The
 * shader must be the active one or raylib's SetShaderValue() target.
 */
G_GNUC_INTERNAL
void
lrg_light_set_upload_fog (LrgLightSet                   *self,
                          Shader                         shader,
                          const LrgLightSetFogLocations *locs);

G_END_DECLS
