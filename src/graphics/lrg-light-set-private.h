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

/*
 * LRG_LIGHT_CLUSTER_GLSL:
 * The light grid's uniforms and lrg_cluster_base (vec3 p, out ivec2
 * base): FALSE outside the grid, else the cell texel of @p's first four
 * light indices (the cell's lights continue in the next texels of the
 * row, gridLayers.w / 4 in all).  gridParams = (corner X, corner Z, cell
 * size, cells per side) in draw space; gridLayers = (lowest layer's
 * base height, layer height, layers, slots per cell).  With one layer
 * the height is ignored; with more, @p's layer is clamped into range
 * (the outer layers reach on without end), as in build_grid().
 */
#define LRG_LIGHT_CLUSTER_GLSL \
    "uniform vec4 gridParams;\n" \
    "uniform vec4 gridLayers;\n" \
    "bool lrg_cluster_base(vec3 p, out ivec2 base) {\n" \
    "    vec2 cell = floor((p.xz - gridParams.xy) / gridParams.z);\n" \
    "    base = ivec2(0);\n" \
    "    if (cell.x < 0.0 || cell.y < 0.0 || cell.x >= gridParams.w || cell.y >= gridParams.w) return false;\n" \
    "    float layer = 0.0;\n" \
    "    if (gridLayers.z > 1.0) layer = clamp(floor((p.y - gridLayers.x) / gridLayers.y), 0.0, gridLayers.z - 1.0);\n" \
    "    int per = int(gridLayers.w + 0.5) / 4;\n" \
    "    base = ivec2(int(cell.x) * per, int(layer * gridParams.w + cell.y));\n" \
    "    return true;\n" \
    "}\n"

/*
 * LRG_LIGHT_SAMPLE_GLSL:
 * GLSL 330 light uniforms plus lrg_light_sample (vec3 p), the light
 * reaching draw-space position @p from every direction (no normal, so
 * no Lambert term): the ambient colour, the directional colour (zero
 * without a directional light), the point lights of @p's grid cell (or
 * the active list without a grid), through the soft light limit.  This
 * is lrg_light_set_sample() with a %NULL normal, evaluated on the GPU;
 * LrgBillboardBatch lights its static quads with it in the vertex
 * shader.  The grid textures are on LRG_LIGHT_SET_GRID_DATA_UNIT and
 * LRG_LIGHT_SET_GRID_CELLS_UNIT.
 */
#define LRG_LIGHT_SAMPLE_GLSL \
    "uniform vec3 ambientColor;\n" \
    "uniform vec3 sunColor;\n" \
    "uniform int lightCount;\n" \
    "uniform vec3 lightPosition[16];\n" \
    "uniform vec3 lightColor[16];\n" \
    "uniform float lightRadius[16];\n" \
    "uniform int useGrid;\n" \
    "uniform sampler2D lightData;\n" \
    "uniform sampler2D lightGrid;\n" \
    LRG_LIGHT_CLUSTER_GLSL \
    "uniform float lightLimit;\n" \
    "vec3 lrg_point_reach(vec3 p, vec3 lp, float r, vec3 c) {\n" \
    "    float k = clamp(1.0 - length(lp - p) / r, 0.0, 1.0);\n" \
    "    return c * (k * k);\n" \
    "}\n" \
    "vec3 lrg_light_sample(vec3 p) {\n" \
    "    vec3 light = ambientColor + sunColor;\n" \
    "    if (useGrid == 1) {\n" \
    "        ivec2 base;\n" \
    "        if (lrg_cluster_base(p, base)) {\n" \
    "            int per = int(gridLayers.w + 0.5) / 4;\n" \
    "            bool done = false;\n" \
    "            for (int t = 0; t < 8 && !done; t++) {\n" \
    "                if (t >= per) break;\n" \
    "                vec4 ids = texelFetch(lightGrid, base + ivec2(t, 0), 0);\n" \
    "                for (int j = 0; j < 4; j++) {\n" \
    "                    if (ids[j] < 0.0) { done = true; break; }\n" \
    "                    int id = int(ids[j] + 0.5);\n" \
    "                    vec4 pr = texelFetch(lightData, ivec2(0, id), 0);\n" \
    "                    vec4 col = texelFetch(lightData, ivec2(1, id), 0);\n" \
    "                    light += lrg_point_reach(p, pr.xyz, pr.w, col.rgb);\n" \
    "                }\n" \
    "            }\n" \
    "        }\n" \
    "    } else {\n" \
    "        for (int i = 0; i < 16; i++) {\n" \
    "            if (i >= lightCount) break;\n" \
    "            light += lrg_point_reach(p, lightPosition[i], lightRadius[i], lightColor[i]);\n" \
    "        }\n" \
    "    }\n" \
    "    if (lightLimit > 0.0) {\n" \
    "        float knee = lightLimit * 0.75;\n" \
    "        vec3 over = max(light - knee, 0.0);\n" \
    "        light = min(light, vec3(knee)) + (lightLimit - knee) * (1.0 - exp(-over / (lightLimit - knee)));\n" \
    "    }\n" \
    "    return light;\n" \
    "}\n"

/* texture units of the light grid textures: above raylib's material maps */
#define LRG_LIGHT_SET_GRID_DATA_UNIT  (14)
#define LRG_LIGHT_SET_GRID_CELLS_UNIT (15)

/*
 * LrgLightSetSampleLocations:
 * Uniform locations of LRG_LIGHT_SAMPLE_GLSL in one shader (-1 when the
 * compiler dropped an unused uniform).
 */
typedef struct
{
    gint ambient;
    gint sun_color;
    gint count;
    gint positions;
    gint colors;
    gint radii;
    gint use_grid;
    gint light_data;
    gint light_grid;
    gint grid_params;
    gint grid_layers;
    gint limit;
} LrgLightSetSampleLocations;

/*
 * lrg_light_set_sample_locations:
 * Looks up the LRG_LIGHT_SAMPLE_GLSL uniforms of @shader and points its
 * grid samplers at the grid texture units.
 */
G_GNUC_INTERNAL
void
lrg_light_set_sample_locations (Shader                      shader,
                                LrgLightSetSampleLocations *out_locs);

/*
 * lrg_light_set_upload_sample:
 * Uploads @self's lights to @shader's LRG_LIGHT_SAMPLE_GLSL uniforms
 * (the last selection, the built grid when its textures exist) and
 * binds the grid textures.  @shader must be the active rlgl shader.
 * Returns %FALSE when @self is %NULL (nothing uploaded).
 */
G_GNUC_INTERNAL
gboolean
lrg_light_set_upload_sample (LrgLightSet                      *self,
                             Shader                            shader,
                             const LrgLightSetSampleLocations *locs);

/*
 * LRG_LIGHT_AIR_GLSL:
 * The volumetric air (lrg_light_set_set_air()): uniforms and
 * float lrg_air (vec3 p, out vec3 ins), the transmittance from the eye
 * (airCamera) to draw-space @p and the light scattered toward the eye on
 * the way (@ins).  Needs LRG_SHADOW_GLSL and the light shader's
 * ambientColor, sunColor and sunDirection above it.  airParams =
 * (density, g, intensity, steps), airTint = (rgb, march distance);
 * airLightColor[k] is the colour of the light in shadow slot k.  Each
 * step's offset is jittered per pixel; lrg_light_set_sample_air() is the
 * same sum with the steps at their middles and no shadows.
 */
#define LRG_LIGHT_AIR_GLSL \
    "uniform vec4 airParams;\n" \
    "uniform vec4 airTint;\n" \
    "uniform vec3 airCamera;\n" \
    "uniform vec3 airLightColor[8];\n" \
    "float lrg_air_phase(float g, float c) {\n" \
    "    float g2 = g * g;\n" \
    "    return (1.0 - g2) / (12.566371 * pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5));\n" \
    "}\n" \
    "float lrg_air(vec3 p, out vec3 ins) {\n" \
    "    ins = vec3(0.0);\n" \
    "    vec3 ray = p - airCamera;\n" \
    "    float len = length(ray);\n" \
    "    if (len < 1e-4) return 1.0;\n" \
    "    vec3 dir = ray / len;\n" \
    "    float L = min(len, airTint.w);\n" \
    "    int steps = int(airParams.w + 0.5);\n" \
    "    float dt = L / float(steps);\n" \
    "    float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n" \
    "    float sunPhase = lrg_air_phase(airParams.y, dot(sunDirection, -dir));\n" \
    "    bool sunOn = dot(sunColor, sunColor) > 0.0;\n" \
    "    for (int i = 0; i < 32; i++) {\n" \
    "        if (i >= steps) break;\n" \
    "        float t = (float(i) + jitter) * dt;\n" \
    "        vec3 x = airCamera + dir * t;\n" \
    "        vec3 s = ambientColor * 0.0795775;\n" \
    "        if (sunOn) s += sunColor * sunPhase * lrg_sun_shadow_f(x, vec3(0.0), 0);\n" \
    "        for (int k = 0; k < 8; k++) {\n" \
    "            if (k >= pointShadowCount) break;\n" \
    "            vec4 l = pointShadowLight[k];\n" \
    "            vec3 v = x - l.xyz;\n" \
    "            float d = length(v);\n" \
    "            if (d >= l.w || d < 1e-4) continue;\n" \
    "            float a = 1.0 - d / l.w;\n" \
    "            s += airLightColor[k] * (a * a) * lrg_air_phase(airParams.y, dot(v / d, -dir))\n" \
    "                 * lrg_point_shadow_f(k, x, vec3(0.0), 0);\n" \
    "        }\n" \
    "        ins += s * exp(-airParams.x * t) * airParams.x * dt;\n" \
    "    }\n" \
    "    ins *= airParams.z * airTint.rgb;\n" \
    "    return exp(-airParams.x * L);\n" \
    "}\n"

/*
 * LrgLightSetShadowCandidate:
 * A point light that may get a shadow cube: its id, draw-space position,
 * radius, shadow priority and its distance key (distance from the
 * camera to the light's sphere).
 */
typedef struct
{
    guint  id;
    gfloat position[3];
    gfloat radius;
    guint  priority;
    gfloat key;
} LrgLightSetShadowCandidate;

/*
 * lrg_light_set_pick_shadow_lights:
 * Fills @out with up to @max enabled, lit lights of shadow priority > 0
 * (highest priority first, then smallest key, then lowest id), leaving
 * out lights whose key exceeds @max_distance (0: no limit).  @camera is
 * in world space.  Returns how many were picked.
 */
G_GNUC_INTERNAL
guint
lrg_light_set_pick_shadow_lights (LrgLightSet                *self,
                                  const gdouble              *camera,
                                  gfloat                      max_distance,
                                  guint                       max,
                                  LrgLightSetShadowCandidate *out);

G_END_DECLS
