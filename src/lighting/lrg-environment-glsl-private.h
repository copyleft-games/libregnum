/* lrg-environment-glsl-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Lit-shader hook of #LrgEnvironment (0.9.0 M2 contract, see the light
 * set) plus the GPU and YAML glue shared by the environment, the
 * reflection probes and the level serializer.  Not installed, not
 * scanned for introspection.
 *
 * LRG_ENV_GLSL is pasted into the light set's GLSL library (after
 * LRG_PI / lrg_to_linear / lrg_to_display, before the model) and
 * defines:
 *
 * - void lrg_env_set_point (vec3 p): the draw-space point being shaded,
 *   for the probes' influence and box projection; the light model calls
 *   it before lrg_env_specular(); without it the binder's reference
 *   point (lrg_environment_set_reference_point()) is used;
 * - bool lrg_env_active (): false when no environment is bound, it is
 *   not baked or its image-based lighting is off (the shader then uses
 *   the flat `ambientColor`);
 * - vec3 lrg_env_diffuse (vec3 n): E (n) / pi from the L2 harmonics,
 *   times exposure and diffuse intensity (multiply by kd x albedo);
 * - vec3 lrg_env_specular (vec3 n, vec3 v, float rough): prefiltered
 *   radiance (linear) along reflect (-v, n) at the roughness' mip,
 *   blended with up to two box-projected reflection probes by their
 *   influence at the shaded point (multiply by F x brdf.x + brdf.y);
 * - vec2 lrg_env_brdf (float nv, float rough): the split-sum BRDF
 *   (scale, bias) from the lookup table, or Karis' analytic fit when no
 *   unit was left for the table.
 *
 * lrg_environment_bind_private() uploads the uniforms of one shader and
 * claims texture units from *texture_unit upward while
 * lrg_light_set_hook_unit_usable() allows: the prefiltered map (always
 * claimed, even inactive: every samplerCube points at it), then the
 * BRDF table and the two probe cubemaps.  On a 16-unit driver only the
 * prefiltered map is claimed (analytic BRDF, no probes) so the SSAO and
 * decal hooks keep units.  Unclaimed probe samplers share the
 * prefiltered map's unit, an unclaimed table sampler shares unit 0 with
 * texture0 (same sampler types).  Uniform locations are cached per
 * shader id (and re-checked against the program on every bind).
 */

#pragma once

#include <glib.h>
#include <yaml-glib.h>
#include "lrg-environment.h"
#include "lrg-reflection-probe.h"

G_BEGIN_DECLS

#define LRG_ENV_GLSL \
    "uniform int lrgEnvActive;\n" \
    "uniform vec3 lrgEnvSh[9];\n" \
    "uniform samplerCube lrgEnvSpecularMap;\n" \
    "uniform sampler2D lrgEnvBrdfLut;\n" \
    "uniform vec4 lrgEnvParams;\n" \
    "uniform vec2 lrgEnvRotation;\n" \
    "uniform int lrgEnvProbeCount;\n" \
    "uniform samplerCube lrgEnvProbeMap0;\n" \
    "uniform samplerCube lrgEnvProbeMap1;\n" \
    "uniform vec3 lrgEnvProbeCenter[2];\n" \
    "uniform vec3 lrgEnvProbeExtent[2];\n" \
    "uniform vec4 lrgEnvProbeParams[2];\n" \
    "uniform vec3 lrgEnvReference;\n" \
    "uniform int lrgEnvBrdfMode;\n" \
    "vec3 gLrgEnvPoint = vec3(0.0);\n" \
    "bool gLrgEnvPointSet = false;\n" \
    "void lrg_env_set_point(vec3 p) { gLrgEnvPoint = p; gLrgEnvPointSet = true; }\n" \
    "bool lrg_env_active() { return lrgEnvActive != 0; }\n" \
    "vec3 lrg_env_rot(vec3 d) {\n" \
    "    return vec3(lrgEnvRotation.x * d.x - lrgEnvRotation.y * d.z, d.y,\n" \
    "                lrgEnvRotation.y * d.x + lrgEnvRotation.x * d.z);\n" \
    "}\n" \
    "vec3 lrg_env_diffuse(vec3 n) {\n" \
    "    vec3 d = lrg_env_rot(normalize(n));\n" \
    "    vec3 e = lrgEnvSh[0] * 0.282095\n" \
    "        + lrgEnvSh[1] * (0.488603 * d.y) + lrgEnvSh[2] * (0.488603 * d.z)\n" \
    "        + lrgEnvSh[3] * (0.488603 * d.x) + lrgEnvSh[4] * (1.092548 * d.x * d.y)\n" \
    "        + lrgEnvSh[5] * (1.092548 * d.y * d.z) + lrgEnvSh[6] * (0.315392 * (3.0 * d.z * d.z - 1.0))\n" \
    "        + lrgEnvSh[7] * (1.092548 * d.x * d.z) + lrgEnvSh[8] * (0.546274 * (d.x * d.x - d.y * d.y));\n" \
    "    return max(e, vec3(0.0)) * lrgEnvParams.x;\n" \
    "}\n" \
    "float lrg_env_probe_weight(int i, vec3 p) {\n" \
    "    vec3 d = lrgEnvProbeExtent[i] - abs(p - lrgEnvProbeCenter[i]);\n" \
    "    float m = min(d.x, min(d.y, d.z));\n" \
    "    if (m < 0.0) return 0.0;\n" \
    "    float b = lrgEnvProbeParams[i].x;\n" \
    "    return b > 0.0 ? clamp(m / b, 0.0, 1.0) : 1.0;\n" \
    "}\n" \
    "vec3 lrg_env_probe_dir(int i, vec3 p, vec3 r) {\n" \
    "    if (lrgEnvProbeParams[i].z < 0.5) return r;\n" \
    "    vec3 rs = vec3(abs(r.x) < 1e-6 ? 1e-6 : r.x, abs(r.y) < 1e-6 ? 1e-6 : r.y, abs(r.z) < 1e-6 ? 1e-6 : r.z);\n" \
    "    vec3 c = lrgEnvProbeCenter[i];\n" \
    "    vec3 t1 = (c + lrgEnvProbeExtent[i] - p) / rs;\n" \
    "    vec3 t2 = (c - lrgEnvProbeExtent[i] - p) / rs;\n" \
    "    vec3 tf = max(t1, t2);\n" \
    "    float t = max(min(tf.x, min(tf.y, tf.z)), 0.0);\n" \
    "    return p + r * t - c;\n" \
    "}\n" \
    "vec3 lrg_env_specular(vec3 n, vec3 v, float rough) {\n" \
    "    vec3 r = reflect(-normalize(v), normalize(n));\n" \
    "    vec3 g = textureLod(lrgEnvSpecularMap, lrg_env_rot(r), clamp(rough, 0.0, 1.0) * lrgEnvParams.z).rgb * lrgEnvParams.y;\n" \
    "    if (lrgEnvProbeCount <= 0) return g;\n" \
    "    vec3 p = gLrgEnvPointSet ? gLrgEnvPoint : lrgEnvReference;\n" \
    "    float w0 = lrg_env_probe_weight(0, p);\n" \
    "    float w1 = lrgEnvProbeCount > 1 ? lrg_env_probe_weight(1, p) : 0.0;\n" \
    "    vec3 c = vec3(0.0);\n" \
    "    if (w0 > 0.0) c += w0 * lrgEnvProbeParams[0].y * textureLod(lrgEnvProbeMap0,\n" \
    "        lrg_env_probe_dir(0, p, r), clamp(rough, 0.0, 1.0) * lrgEnvProbeParams[0].w).rgb;\n" \
    "    if (w1 > 0.0) c += w1 * lrgEnvProbeParams[1].y * textureLod(lrgEnvProbeMap1,\n" \
    "        lrg_env_probe_dir(1, p, r), clamp(rough, 0.0, 1.0) * lrgEnvProbeParams[1].w).rgb;\n" \
    "    float ws = w0 + w1;\n" \
    "    if (ws > 1.0) { c /= ws; ws = 1.0; }\n" \
    "    return c + g * (1.0 - ws);\n" \
    "}\n" \
    "vec2 lrg_env_brdf(float nv, float rough) {\n" \
    "    if (lrgEnvBrdfMode != 0) {\n" \
    "        vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);\n" \
    "        vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);\n" \
    "        vec4 r = rough * c0 + c1;\n" \
    "        float a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;\n" \
    "        return vec2(a004 * -1.04 + r.z, a004 * 1.04 + r.w);\n" \
    "    }\n" \
    "    return texture(lrgEnvBrdfLut, vec2(clamp(nv, 0.0, 1.0), clamp(rough, 0.0, 1.0))).rg;\n" \
    "}\n"

void lrg_environment_bind_private (LrgEnvironment *env,
                                   guint           shader_id,
                                   gint           *texture_unit);

/* ==========================================================================
 * GPU helpers shared by LrgEnvironment and LrgReflectionProbe
 * (lrg-environment.c).  Every cubemap is RGBA16F; every function needs a
 * GL context and restores the rlgl framebuffer, viewport and matrices.
 * ========================================================================== */

/*
 * LrgEnvCaptureFunc:
 * Draws one cube face with the face's view and projection already
 * loaded into rlgl (see LrgReflectionProbeDrawFunc).
 */
typedef void (*LrgEnvCaptureFunc) (guint         face,
                                   const gfloat *view,
                                   const gfloat *projection,
                                   gpointer      user_data);

/* A zero-filled RGBA16F cubemap of @size with @mips levels (0 on failure). */
G_GNUC_INTERNAL
guint lrg_env_gpu_cube_new (guint size,
                            guint mips);

/* Uploads six @size faces of linear RGB floats (see
 * lrg_environment_sh_project_cubemap() for the layout) into a new
 * single-level RGBA16F cubemap. */
G_GNUC_INTERNAL
guint lrg_env_gpu_cube_from_faces (const gfloat *faces,
                                   guint         size);

/*
 * Renders the six faces of a new @size cubemap from @position with a
 * depth buffer, each cleared to @clear_rgb and drawn by @func, then
 * decodes the (sRGB-encoded) result to linear.  Returns the cubemap id
 * (single level), 0 with @error on failure.
 */
G_GNUC_INTERNAL
guint lrg_env_gpu_capture (guint              size,
                           const gfloat      *position,
                           gfloat             near_plane,
                           gfloat             far_plane,
                           const gfloat      *clear_rgb,
                           LrgEnvCaptureFunc  func,
                           gpointer           user_data,
                           GError           **error);

/*
 * Prefilters the single-level radiance cubemap @radiance (edge
 * @radiance_size) into a new cubemap of edge @out_size with @out_mips
 * levels, level m holding roughness m / (out_mips - 1).  Returns its id,
 * 0 with @error on failure.
 */
G_GNUC_INTERNAL
guint lrg_env_gpu_prefilter (guint     radiance,
                             guint     radiance_size,
                             guint     out_size,
                             guint     out_mips,
                             GError  **error);

/* Reads face @face of mip @mip (edge @size >> @mip) of cubemap @cube as
 * linear RGB floats (g_free()), NULL on failure. */
G_GNUC_INTERNAL
gfloat *lrg_env_gpu_read_face (guint cube,
                               guint size,
                               guint mip,
                               guint face);

/* Mip levels of a prefiltered cubemap of edge @size: log2 (size) + 1, down to 1 x 1 (a GL-complete chain). */
G_GNUC_INTERNAL
guint lrg_env_mip_count_for (guint size);

/* Frees a texture made by the helpers above. */
G_GNUC_INTERNAL
void lrg_env_gpu_unload (guint texture);

/* Stores a captured, prefiltered cubemap in @probe (takes ownership of
 * @texture; frees the previous one). */
G_GNUC_INTERNAL
void lrg_reflection_probe_take_texture_private (LrgReflectionProbe *probe,
                                                guint               texture,
                                                guint               mips);

/* ==========================================================================
 * YAML glue for the level serializer
 * ========================================================================== */

/* The `environment:` mapping of @self (a new reference). */
G_GNUC_INTERNAL
YamlMapping *lrg_environment_to_yaml_private (LrgEnvironment *self);

/* Replaces the settings of @self with the mapping's (unknown keys kept). */
G_GNUC_INTERNAL
gboolean lrg_environment_from_yaml_private (LrgEnvironment  *self,
                                            YamlMapping     *map,
                                            GError         **error);

G_END_DECLS
