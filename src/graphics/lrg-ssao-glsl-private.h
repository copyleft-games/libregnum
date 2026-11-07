/* lrg-ssao-glsl-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Lit-shader hook (0.9.0 M2 contract, see the light set): the GLSL
 * functions LRG_SSAO_GLSL defines are pasted into the light set's fragment
 * shader, and lrg_ssao_bind_private() binds their uniforms and textures
 * before a draw.  A NULL object binds the inactive state.
 */

#pragma once

#include <glib.h>
#include "lrg-ssao.h"

G_BEGIN_DECLS

/*
 * LRG_SSAO_GLSL:
 *
 * float lrg_ssao(): the ambient visibility of the fragment (1.0 = open,
 * 0.0 = fully occluded), read from the SSAO result at gl_FragCoord
 * mapped through lrgSsaoViewport = (x, y, width, height) of the lit
 * pass in framebuffer pixels.  Outside the viewport, or with lrgSsaoOn
 * == 0 (no result bound), it returns 1.0.  The result is bilinearly
 * filtered, so a half-resolution result upsamples smoothly.
 */
#define LRG_SSAO_GLSL \
	"uniform int lrgSsaoOn;\n" \
	"uniform sampler2D lrgSsaoTexture;\n" \
	"uniform vec4 lrgSsaoViewport;\n" \
	"float lrg_ssao() {\n" \
	"    if (lrgSsaoOn == 0) return 1.0;\n" \
	"    vec2 uv = (gl_FragCoord.xy - lrgSsaoViewport.xy) / max(lrgSsaoViewport.zw, vec2(1.0));\n" \
	"    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return 1.0;\n" \
	"    return texture(lrgSsaoTexture, uv).r;\n" \
	"}\n"

/*
 * lrg_ssao_bind_private:
 * @ssao: (nullable): the SSAO pass, or NULL for the inactive state
 * @shader_id: the GL program containing LRG_SSAO_GLSL
 * @texture_unit: (inout): first free texture unit; advanced by the units
 *   bound (one when active, none when inactive)
 *
 * Looks up the hook's uniforms (cached per program id), binds the
 * result texture and sets the viewport and the on flag.  Makes
 * @shader_id the current program.
 */
void lrg_ssao_bind_private (LrgSsao *ssao,
                            guint    shader_id,
                            gint    *texture_unit);

G_END_DECLS
