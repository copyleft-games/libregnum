/* lrg-shadow-renderer-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal glue between LrgShadowRenderer and LrgLightSet's shader. Not
 * installed, not scanned for introspection. LRG_SHADOW_GLSL is the
 * lookup side of the passes LrgShadowRenderer renders: the cascades of
 * the directional light (gl_FragCoord.z of the light's orthographic
 * view) in one atlas, the six faces of each shadowed point light
 * (distance / radius) in another.
 */

#pragma once

#include <glib-object.h>
#include <raylib.h>
#include "lighting/lrg-shadow-renderer.h"

G_BEGIN_DECLS

/*
 * LRG_SHADOW_GLSL:
 *
 * GLSL 330 shadow uniforms plus
 * - float lrg_sun_shadow (vec3 p, vec3 n): 1 lit .. 0 hidden from the
 *   directional light, at draw-space @p with unit normal @n (normal
 *   offset against acne), 1 outside every cascade;
 * - float lrg_point_shadow (int slot, vec3 p, vec3 n): the same for the
 *   point light in shadow slot @slot (-1: always 1);
 * - lrg_sun_shadow_f / lrg_point_shadow_f: the same with an explicit
 *   filter radius (the volumetric air marches with 0, one tap).
 * Both are already mixed toward 1 by shadowStrength.
 */
#define LRG_SHADOW_GLSL \
    "uniform int shadowCascades;\n" \
    "uniform mat4 shadowMatrix[4];\n" \
    "uniform vec4 shadowTile[4];\n" \
    "uniform vec4 shadowCascadeInfo[4];\n" \
    "uniform vec4 shadowParams;\n" \
    "uniform int shadowFilter;\n" \
    "uniform float shadowStrength;\n" \
    "uniform sampler2D sunShadowMap;\n" \
    "uniform int pointShadowCount;\n" \
    "uniform vec4 pointShadowLight[8];\n" \
    "uniform vec4 pointShadowParams;\n" \
    "uniform sampler2D pointShadowMap;\n" \
    "float lrg_sun_shadow_f(vec3 p, vec3 n, int filt) {\n" \
    "    if (shadowCascades <= 0) return 1.0;\n" \
    "    for (int i = 0; i < 4; i++) {\n" \
    "        if (i >= shadowCascades) break;\n" \
    "        vec3 q = p + n * (shadowCascadeInfo[i].x * shadowParams.w);\n" \
    "        vec4 c = shadowMatrix[i] * vec4(q, 1.0);\n" \
    "        vec3 s = c.xyz / c.w * 0.5 + 0.5;\n" \
    "        float m = (float(filt) + 1.0) * shadowCascadeInfo[i].z;\n" \
    "        if (s.x < m || s.y < m || s.x > 1.0 - m || s.y > 1.0 - m || s.z > 1.0) continue;\n" \
    "        vec2 uv = shadowTile[i].xy + s.xy * shadowTile[i].zw;\n" \
    "        float z = s.z - shadowParams.z * shadowCascadeInfo[i].y;\n" \
    "        float lit = 0.0;\n" \
    "        float taps = 0.0;\n" \
    "        for (int y = -2; y <= 2; y++) {\n" \
    "            for (int x = -2; x <= 2; x++) {\n" \
    "                if (abs(x) > filt || abs(y) > filt) continue;\n" \
    "                float d = texture(sunShadowMap, uv + vec2(float(x), float(y)) * shadowParams.xy).r;\n" \
    "                lit += z <= d ? 1.0 : 0.0;\n" \
    "                taps += 1.0;\n" \
    "            }\n" \
    "        }\n" \
    "        return mix(1.0, lit / taps, shadowStrength);\n" \
    "    }\n" \
    "    return 1.0;\n" \
    "}\n" \
    "float lrg_point_shadow_f(int slot, vec3 p, vec3 n, int filt) {\n" \
    "    if (slot < 0 || slot >= pointShadowCount) return 1.0;\n" \
    "    vec4 l = pointShadowLight[slot];\n" \
    "    vec3 d0 = p - l.xyz;\n" \
    "    float dist0 = length(d0);\n" \
    "    if (dist0 >= l.w) return 1.0;\n" \
    "    float texel = 2.0 * dist0 / pointShadowParams.x;\n" \
    "    vec3 d = d0 + n * (texel * pointShadowParams.w);\n" \
    "    vec3 a = abs(d);\n" \
    "    int face;\n" \
    "    vec3 F;\n" \
    "    vec3 U = vec3(0.0, 1.0, 0.0);\n" \
    "    if (a.x >= a.y && a.x >= a.z) { face = d.x > 0.0 ? 0 : 1; F = vec3(sign(d.x), 0.0, 0.0); }\n" \
    "    else if (a.y >= a.z) { face = d.y > 0.0 ? 2 : 3; F = vec3(0.0, sign(d.y), 0.0); U = vec3(0.0, 0.0, 1.0); }\n" \
    "    else { face = d.z > 0.0 ? 4 : 5; F = vec3(0.0, 0.0, sign(d.z)); }\n" \
    "    vec3 vz = -F;\n" \
    "    vec3 vx = normalize(cross(U, vz));\n" \
    "    vec3 vy = cross(vz, vx);\n" \
    "    float fd = dot(d, F);\n" \
    "    vec2 ndc = vec2(dot(d, vx), dot(d, vy)) / fd;\n" \
    "    vec2 st = ndc * 0.5 + 0.5;\n" \
    "    float res = pointShadowParams.x;\n" \
    "    vec2 tile = vec2(float(face - (face / 3) * 3), float(slot * 2 + face / 3)) * res;\n" \
    "    float z = (length(d) - shadowParams.z) / l.w;\n" \
    "    float lit = 0.0;\n" \
    "    float taps = 0.0;\n" \
    "    for (int y = -2; y <= 2; y++) {\n" \
    "        for (int x = -2; x <= 2; x++) {\n" \
    "            if (abs(x) > filt || abs(y) > filt) continue;\n" \
    "            vec2 px = clamp(st * res + vec2(float(x), float(y)), vec2(0.5), vec2(res - 0.5));\n" \
    "            vec2 uv = (tile + px) / pointShadowParams.yz;\n" \
    "            float stored = texture(pointShadowMap, uv).r;\n" \
    "            lit += z <= stored ? 1.0 : 0.0;\n" \
    "            taps += 1.0;\n" \
    "        }\n" \
    "    }\n" \
    "    return mix(1.0, lit / taps, shadowStrength);\n" \
    "}\n" \
    "float lrg_sun_shadow(vec3 p, vec3 n) { return lrg_sun_shadow_f(p, n, shadowFilter); }\n" \
    "float lrg_point_shadow(int slot, vec3 p, vec3 n) { return lrg_point_shadow_f(slot, p, n, shadowFilter); }\n"

/*
 * LrgShadowLocations:
 * Uniform locations of LRG_SHADOW_GLSL in one shader (-1 when the
 * compiler dropped an unused uniform).
 */
typedef struct
{
    gint cascades;
    gint matrices;
    gint tiles;
    gint cascade_info;
    gint params;
    gint filter;
    gint strength;
    gint sun_map;
    gint point_count;
    gint point_lights;
    gint point_params;
    gint point_map;
} LrgShadowLocations;

/*
 * lrg_shadow_locations:
 * Looks up the LRG_SHADOW_GLSL uniforms of @shader and points its
 * samplers at LRG_SHADOW_SUN_UNIT and LRG_SHADOW_POINT_UNIT.
 */
G_GNUC_INTERNAL
void
lrg_shadow_locations (Shader              shader,
                      LrgShadowLocations *out_locs);

/*
 * lrg_shadow_renderer_upload:
 * Uploads @self's shadows to @shader and binds its atlases; with @self
 * NULL or not ready, uploads "no shadows" (zero cascades and points).
 * @shader must be the active rlgl shader or SetShaderValue()'s target.
 */
G_GNUC_INTERNAL
void
lrg_shadow_renderer_upload (LrgShadowRenderer        *self,
                            Shader                    shader,
                            const LrgShadowLocations *locs);

G_END_DECLS
