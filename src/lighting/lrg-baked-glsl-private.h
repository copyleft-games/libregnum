/* lrg-baked-glsl-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Lit-shader hook for baked lighting (0.9.0 M3 contract): lightmaps
 * (#LrgLightmap3D) and SH probe grids (#LrgProbeGrid).  Not installed,
 * not scanned for introspection.
 *
 * LRG_BAKED_GLSL is pasted into the light set's GLSL library right after
 * LRG_ENV_GLSL (before the model) and defines:
 *
 * - void lrg_baked_set_uv2 (vec2 uv2): the fragment's second texture
 *   coordinate (the vertex shader's fragTexCoord2, raylib's
 *   vertexTexCoord2 attribute); the built-in main() calls it, a user lit
 *   shader that wants lightmaps calls it too;
 * - bool lrg_baked_active (): this draw has baked indirect light (a
 *   lightmap entry or a probe grid);
 * - bool lrg_baked_lightmapped (): this draw reads a lightmap (lights in
 *   %LRG_LIGHT_MODE_BAKED mode are then skipped: their direct light is in
 *   the lightmap);
 * - vec3 lrg_baked_diffuse (vec3 p, vec3 n): irradiance / pi (linear)
 *   from the lightmap at the uv2, else from the probe grid's L2 SH at the
 *   draw-space point @p and normal @n; zero when inactive.
 *
 * Uniforms:
 * - lrgBakedMode (int): 0 inactive, 1 lightmap, 2 probe grid;
 * - lrgBakedLightmap (sampler2D), lrgBakedScaleOffset (vec4: uv2 scale
 *   xy, offset zw), lrgBakedLightmapIntensity (float);
 * - lrgBakedProbes (sampler3D: nx x ny x 7 nz RGBA16F texels, group g
 *   of a probe is the slab of layers g nz ..), lrgBakedProbeMin /
 *   lrgBakedProbeMax (vec3, draw space), lrgBakedProbeRes (vec3: nx, ny,
 *   nz), lrgBakedProbeIntensity (float);
 * - lrgBakedSun (int): 1 when the directional light is in BAKED mode.
 *
 * Baked lights: the light set uploads a light in BAKED mode with its
 * point shadow slot plus LRG_BAKED_SLOT_OFFSET (lightShadow[] and the
 * grid's colour texel w); lrg_light_one() strips the offset and skips the
 * light when lrg_baked_lightmapped().
 *
 * Binding:
 * - lrg_baked_lighting_bind_private() is the hook binder called by every
 *   lrg_light_set_apply() / bind_shader() (after the environment): it
 *   claims two texture units (the probe sampler3D first, then the
 *   lightmap sampler2D) while lrg_light_set_hook_unit_usable() allows,
 *   points the samplers at them, remembers them per program id and binds
 *   the inactive state.  An unclaimed lightmap sampler shares unit 0 with
 *   texture0 (same sampler type).  The sampler3D always gets a unit of
 *   its own: the hook window has at least four units and the environment
 *   (bound first) claims one; should none be usable (no context) the
 *   program keeps the hook inactive.
 * - lrg_baked_lighting_set_draw_private() selects one draw's source: a
 *   lightmap page + scale / offset, else a probe grid, else nothing.
 *   The world renderer calls it per item, with the shader enabled.
 */

#pragma once

#include <glib.h>
#include "lrg-lightmap3d.h"
#include "lrg-probe-grid.h"

G_BEGIN_DECLS

/* added to a baked light's point shadow slot in the light uniforms */
#define LRG_BAKED_SLOT_OFFSET (64)

#define LRG_BAKED_GLSL \
    "uniform int lrgBakedMode;\n" \
    "uniform int lrgBakedSun;\n" \
    "uniform sampler2D lrgBakedLightmap;\n" \
    "uniform vec4 lrgBakedScaleOffset;\n" \
    "uniform float lrgBakedLightmapIntensity;\n" \
    "uniform sampler3D lrgBakedProbes;\n" \
    "uniform vec3 lrgBakedProbeMin;\n" \
    "uniform vec3 lrgBakedProbeMax;\n" \
    "uniform vec3 lrgBakedProbeRes;\n" \
    "uniform float lrgBakedProbeIntensity;\n" \
    "vec2 gLrgUv2 = vec2(0.0);\n" \
    "void lrg_baked_set_uv2(vec2 uv2) { gLrgUv2 = uv2; }\n" \
    "bool lrg_baked_active() { return lrgBakedMode != 0; }\n" \
    "bool lrg_baked_lightmapped() { return lrgBakedMode == 1; }\n" \
    "vec4 lrg_baked_probe_group(vec3 f, float g) {\n" \
    "    vec3 c = vec3((f.xy + 0.5) / lrgBakedProbeRes.xy,\n" \
    "                  (g * lrgBakedProbeRes.z + f.z + 0.5) / (7.0 * lrgBakedProbeRes.z));\n" \
    "    return texture(lrgBakedProbes, c);\n" \
    "}\n" \
    "vec3 lrg_baked_probe_diffuse(vec3 p, vec3 n) {\n" \
    "    vec3 ext = max(lrgBakedProbeMax - lrgBakedProbeMin, vec3(1e-6));\n" \
    "    vec3 f = clamp((p - lrgBakedProbeMin) / ext, 0.0, 1.0) * max(lrgBakedProbeRes - 1.0, vec3(0.0));\n" \
    "    vec4 g0 = lrg_baked_probe_group(f, 0.0);\n" \
    "    vec4 g1 = lrg_baked_probe_group(f, 1.0);\n" \
    "    vec4 g2 = lrg_baked_probe_group(f, 2.0);\n" \
    "    vec4 g3 = lrg_baked_probe_group(f, 3.0);\n" \
    "    vec4 g4 = lrg_baked_probe_group(f, 4.0);\n" \
    "    vec4 g5 = lrg_baked_probe_group(f, 5.0);\n" \
    "    vec4 g6 = lrg_baked_probe_group(f, 6.0);\n" \
    "    vec3 c0 = g0.xyz;\n" \
    "    vec3 c1 = vec3(g0.w, g1.xy);\n" \
    "    vec3 c2 = vec3(g1.zw, g2.x);\n" \
    "    vec3 c3 = g2.yzw;\n" \
    "    vec3 c4 = g3.xyz;\n" \
    "    vec3 c5 = vec3(g3.w, g4.xy);\n" \
    "    vec3 c6 = vec3(g4.zw, g5.x);\n" \
    "    vec3 c7 = g5.yzw;\n" \
    "    vec3 c8 = g6.xyz;\n" \
    "    vec3 d = normalize(n);\n" \
    "    vec3 e = c0 * 0.282095\n" \
    "        + c1 * (0.488603 * d.y) + c2 * (0.488603 * d.z) + c3 * (0.488603 * d.x)\n" \
    "        + c4 * (1.092548 * d.x * d.y) + c5 * (1.092548 * d.y * d.z)\n" \
    "        + c6 * (0.315392 * (3.0 * d.z * d.z - 1.0)) + c7 * (1.092548 * d.x * d.z)\n" \
    "        + c8 * (0.546274 * (d.x * d.x - d.y * d.y));\n" \
    "    return max(e, vec3(0.0)) * lrgBakedProbeIntensity;\n" \
    "}\n" \
    "vec3 lrg_baked_diffuse(vec3 p, vec3 n) {\n" \
    "    if (lrgBakedMode == 1)\n" \
    "        return texture(lrgBakedLightmap, gLrgUv2 * lrgBakedScaleOffset.xy + lrgBakedScaleOffset.zw).rgb\n" \
    "               * lrgBakedLightmapIntensity;\n" \
    "    if (lrgBakedMode == 2) return lrg_baked_probe_diffuse(p, n);\n" \
    "    return vec3(0.0);\n" \
    "}\n"

/*
 * lrg_baked_lighting_bind_private:
 * @shader_id: a program built from the lit library, enabled
 * @texture_unit: (inout): the next free hook unit; advanced past the
 *   units claimed
 *
 * The hook binder (see the header comment).  Binds the inactive state.
 */
G_GNUC_INTERNAL
void lrg_baked_lighting_bind_private (guint  shader_id,
                                      gint  *texture_unit);

/*
 * lrg_baked_lighting_set_draw_private:
 * @shader_id: the lit program the next draw uses (enabled or not: the
 *   uniforms are set through rlgl on it)
 * @lightmap: (nullable): the lightmap of the draw's object
 * @page: the entry's page
 * @scale_offset: (nullable): the entry's uv2 scale / offset; %NULL
 *   (or @lightmap %NULL) means no lightmap
 * @grid: (nullable): the probe grid lighting the draw when it has no
 *   lightmap entry
 * @origin: (nullable): the draw-space origin (3 doubles) subtracted from
 *   the grid's world box; %NULL for none
 *
 * Selects the draw's baked source (uploading the lightmap / grid when
 * their GPU copy is stale).  Returns the mode bound (0, 1, 2).  Does
 * nothing (returns 0) for a program the binder never saw or one without
 * the hook uniforms.
 */
G_GNUC_INTERNAL
gint lrg_baked_lighting_set_draw_private (guint                shader_id,
                                          LrgLightmap3D       *lightmap,
                                          guint                page,
                                          const gfloat        *scale_offset,
                                          LrgProbeGrid        *grid,
                                          const gdouble       *origin);

/*
 * lrg_baked_lighting_set_sun_private:
 * Uploads lrgBakedSun (the directional light's BAKED flag) to a program
 * the binder knows.  Called by the light set's upload.
 */
G_GNUC_INTERNAL
void lrg_baked_lighting_set_sun_private (guint    shader_id,
                                         gboolean sun_baked);

/*
 * lrg_baked_bind_texture3d_private:
 * Binds the 3D texture @texture to unit @unit (glActiveTexture /
 * glBindTexture through rlGetProcAddress) and returns the active unit to
 * 0.  Needs a GL context (does nothing otherwise).
 */
G_GNUC_INTERNAL
void lrg_baked_bind_texture3d_private (gint  unit,
                                       guint texture);

G_END_DECLS
