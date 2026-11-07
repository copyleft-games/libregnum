/* lrg-decal-glsl-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Lit-shader hook (0.9.0 M2 contract, see the light set): the GLSL
 * functions LRG_DECAL_GLSL defines are pasted into the light set's fragment
 * shader, and lrg_decal_set_bind_private() binds their uniforms and
 * textures before a draw.  A NULL object binds the inactive state.
 */

#pragma once

#include <glib.h>
#include "lrg-decal.h"

G_BEGIN_DECLS

/*
 * Decal data texture (RGBA32F, point sampled, MAX (LRG_DECAL_DATA_TEXELS,
 * cells * slots / 4) wide): rows 0 .. decals - 1 hold one selected decal
 * each in application order, and the grid's cell lists follow from row
 * lrgDecalCellRow on (one texture, so the hook needs only two units:
 * this and the atlas).  A decal row:
 *   0..2  rows of the draw-space -> unit-box matrix (dot(row.xyz, p) + row.w)
 *   3     colour (rgba)
 *   4     atlas rect within a band (u0, v0, du, dv; the uv rect folded in)
 *   5     opacity, cos(fade angle), cos(fade angle + width), flags
 *   6     normal strength, roughness, metallic, blend mode
 *   7     layer mask low 16 bits, high 16 bits, depth fade, 0
 * flags: 1 albedo map, 2 normal map, 4 ORM map, 8 write albedo,
 *        16 write normal, 32 write roughness, 64 write metallic.  The
 *        map bits are ignored while lrgDecalTextures is 0 (no atlas bound).
 */
#define LRG_DECAL_DATA_TEXELS (8)

/*
 * LRG_DECAL_GLSL:
 *
 * lrg_decals_apply(p, ng, albedo, n, rough, metal) paints every decal
 * covering draw-space position @p, in sort order: with a grid
 * (lrgDecalGrid = 1) it walks the list of @p's cluster (same layout as
 * the light grid: lrgDecalGridParams = (corner X, corner Z, cell size,
 * cells per side) in draw space, lrgDecalGridLayers = (layer 0 base,
 * layer height, layers, slots), cell lists of four ids per texel ending
 * at -1); without one it tests the first lrgDecalCount decals.  The
 * atlas holds three bands stacked vertically (albedo, normal, ORM), so a
 * band-local v maps to (band + v) / 3; lookups are clamped half a texel
 * inside the tile and read level 0 (the atlas has no mipmaps).
 *
 * The incoming albedo is LINEAR (the light set's LrgSurface); the decal
 * colour and albedo texture are sRGB-encoded like material colours and
 * are converted with lrg_to_linear(), which the light set defines
 * before the hooks (shaders other than the light set's must define
 * `vec3 lrg_to_linear(vec3)` before pasting this).
 */
#define LRG_DECAL_GLSL \
	"uniform int lrgDecalOn;\n" \
	"uniform int lrgDecalCount;\n" \
	"uniform int lrgDecalGrid;\n" \
	"uniform int lrgDecalReceiver;\n" \
	"uniform sampler2D lrgDecalData;\n" \
	"uniform int lrgDecalCellRow;\n" \
	"uniform int lrgDecalTextures;\n" \
	"uniform sampler2D lrgDecalAtlas;\n" \
	"uniform vec4 lrgDecalGridParams;\n" \
	"uniform vec4 lrgDecalGridLayers;\n" \
	"uniform vec2 lrgDecalAtlasTexel;\n" \
	"vec4 lrg_decal_tex(vec4 rect, vec2 uv, float band) {\n" \
	"    vec2 lo = rect.xy + lrgDecalAtlasTexel * 0.5;\n" \
	"    vec2 hi = rect.xy + rect.zw - lrgDecalAtlasTexel * 0.5;\n" \
	"    vec2 t = clamp(rect.xy + uv * rect.zw, lo, max(lo, hi));\n" \
	"    return textureLod(lrgDecalAtlas, vec2(t.x, (band + t.y) / 3.0), 0.0);\n" \
	"}\n" \
	"void lrg_decal_one(int id, vec3 p, vec3 ng, inout vec4 albedo, inout vec3 n, inout float rough, inout float metal) {\n" \
	"    vec4 r0 = texelFetch(lrgDecalData, ivec2(0, id), 0);\n" \
	"    vec4 r1 = texelFetch(lrgDecalData, ivec2(1, id), 0);\n" \
	"    vec4 r2 = texelFetch(lrgDecalData, ivec2(2, id), 0);\n" \
	"    vec3 l = vec3(dot(r0.xyz, p) + r0.w, dot(r1.xyz, p) + r1.w, dot(r2.xyz, p) + r2.w);\n" \
	"    if (any(greaterThan(abs(l), vec3(0.5)))) return;\n" \
	"    vec4 pm = texelFetch(lrgDecalData, ivec2(5, id), 0);\n" \
	"    vec4 lm = texelFetch(lrgDecalData, ivec2(7, id), 0);\n" \
	"    int mask = int(lm.x + 0.5) | (int(lm.y + 0.5) << 16);\n" \
	"    if ((mask & lrgDecalReceiver) == 0) return;\n" \
	"    vec3 up = normalize(r1.xyz);\n" \
	"    float a = pm.x * smoothstep(pm.z - 1e-4, pm.y, dot(ng, up));\n" \
	"    if (lm.z > 0.0) a *= 1.0 - smoothstep(1.0 - lm.z, 1.0, abs(l.y) * 2.0);\n" \
	"    if (a <= 0.0) return;\n" \
	"    int flags = int(pm.w + 0.5);\n" \
	"    if (lrgDecalTextures == 0) flags &= ~7;\n" \
	"    vec4 col = texelFetch(lrgDecalData, ivec2(3, id), 0);\n" \
	"    vec4 rect = texelFetch(lrgDecalData, ivec2(4, id), 0);\n" \
	"    vec4 pr = texelFetch(lrgDecalData, ivec2(6, id), 0);\n" \
	"    vec2 uv = l.xz + 0.5;\n" \
	"    if ((flags & 1) != 0) col *= lrg_decal_tex(rect, uv, 0.0);\n" \
	"    col.rgb = lrg_to_linear(col.rgb);\n" \
	"    float w = clamp(a * col.a, 0.0, 1.0);\n" \
	"    if (w <= 0.0) return;\n" \
	"    if ((flags & 8) != 0) {\n" \
	"        int blend = int(pr.w + 0.5);\n" \
	"        if (blend == 1) albedo.rgb *= mix(vec3(1.0), col.rgb, w);\n" \
	"        else if (blend == 2) albedo.rgb += col.rgb * w;\n" \
	"        else albedo.rgb = mix(albedo.rgb, col.rgb, w);\n" \
	"    }\n" \
	"    if ((flags & 18) == 18) {\n" \
	"        vec3 tn = lrg_decal_tex(rect, uv, 1.0).xyz * 2.0 - 1.0;\n" \
	"        vec3 dn = normalize(normalize(r0.xyz) * tn.x - normalize(r2.xyz) * tn.y + up * tn.z);\n" \
	"        n = normalize(mix(n, dn, w * pr.x));\n" \
	"    }\n" \
	"    if ((flags & 96) != 0) {\n" \
	"        vec2 rm = pr.yz;\n" \
	"        if ((flags & 4) != 0) rm = lrg_decal_tex(rect, uv, 2.0).gb;\n" \
	"        if ((flags & 32) != 0) rough = mix(rough, rm.x, w);\n" \
	"        if ((flags & 64) != 0) metal = mix(metal, rm.y, w);\n" \
	"    }\n" \
	"}\n" \
	"void lrg_decals_apply(vec3 p, vec3 ng, inout vec4 albedo, inout vec3 n, inout float rough, inout float metal) {\n" \
	"    if (lrgDecalOn == 0) return;\n" \
	"    if (lrgDecalGrid == 1) {\n" \
	"        vec2 cell = floor((p.xz - lrgDecalGridParams.xy) / lrgDecalGridParams.z);\n" \
	"        if (cell.x < 0.0 || cell.y < 0.0 || cell.x >= lrgDecalGridParams.w || cell.y >= lrgDecalGridParams.w) return;\n" \
	"        float layer = 0.0;\n" \
	"        if (lrgDecalGridLayers.z > 1.0) layer = clamp(floor((p.y - lrgDecalGridLayers.x) / lrgDecalGridLayers.y), 0.0, lrgDecalGridLayers.z - 1.0);\n" \
	"        int per = int(lrgDecalGridLayers.w + 0.5) / 4;\n" \
	"        ivec2 base = ivec2(int(cell.x) * per, int(layer * lrgDecalGridParams.w + cell.y));\n" \
	"        for (int t = 0; t < 8; t++) {\n" \
	"            if (t >= per) break;\n" \
	"            vec4 ids = texelFetch(lrgDecalData, base + ivec2(t, lrgDecalCellRow), 0);\n" \
	"            for (int j = 0; j < 4; j++) {\n" \
	"                if (ids[j] < 0.0) return;\n" \
	"                lrg_decal_one(int(ids[j] + 0.5), p, ng, albedo, n, rough, metal);\n" \
	"            }\n" \
	"        }\n" \
	"    } else {\n" \
	"        for (int i = 0; i < 64; i++) {\n" \
	"            if (i >= lrgDecalCount) break;\n" \
	"            lrg_decal_one(i, p, ng, albedo, n, rough, metal);\n" \
	"        }\n" \
	"    }\n" \
	"}\n"

/*
 * lrg_decal_set_bind_private:
 * @decals: (nullable): the decal set, or NULL for the inactive state
 * @shader_id: the GL program containing LRG_DECAL_GLSL
 * @texture_unit: (inout): first free texture unit; advanced by the units
 *   bound (two when active: data + cells, atlas; none when inactive or when
 *   lrg_light_set_hook_unit_usable() refuses the units)
 *
 * Uploads the selection if it changed (lrg_decal_set_upload()), looks
 * up the hook's uniforms (cached per program id) and binds the textures
 * and uniforms.  Makes @shader_id the current program.
 */
void lrg_decal_set_bind_private (LrgDecalSet *decals,
                                 guint        shader_id,
                                 gint        *texture_unit);

G_END_DECLS
