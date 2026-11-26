/* lrg-glsl-private.h - the GLSL dialect of the built-in shaders
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Every shader the engine compiles from source starts with
 * LRG_GLSL_HEADER instead of a literal "#version" line, so one source
 * serves both GL flavours raylib can run on:
 *
 *   desktop (OpenGL 3.3)  "#version 330"
 *   Android (OpenGL ES 3) "#version 300 es" plus default precisions; ES
 *                         fragment shaders have no default float
 *                         precision, and samplers other than sampler2D
 *                         and samplerCube have none in either stage
 *
 * GLSL ES 3.00 is GLSL 3.30 minus a few conveniences, so the shaders are
 * written in the common subset: no implicit int-to-float conversions,
 * no double, no geometry stages.  Where a shader must differ, it tests
 * the GL_ES macro every ES compiler predefines.  The rules are in
 * docs/guides/rendering-pipeline.org, "ES compatibility", and
 * docs/modules/graphics/shaders.org.
 *
 * The two header texts are defined here once.  LRG_GLSL_HEADER picks
 * the build's own (the shader literals and build-aux/check-glsl.sh
 * see it), and the public lrg_glsl_get_header() returns one of the
 * same two strings at run time.  Every built-in shader is loaded
 * through lrg_glsl_load_shader() or lrg_glsl_shader_new_from_memory(),
 * which pass both stages through lrg_glsl_prepare_source(): the
 * literal's own "#version" line is replaced by the run-time header, so
 * lrg_glsl_set_dialect() reaches the built-ins too.
 *
 * LRG_GLSL_ES forces the ES header on desktop at compile time (the
 * shader tests use it to validate the ES text without a phone).
 *
 * Internal header; not installed.
 */

#pragma once

#include <glib.h>
#include <raylib.h>

#include "lrg-glsl.h"

/* the desktop header: GLSL 3.30 */
#define LRG_GLSL_HEADER_DESKTOP "#version 330\n"
#define LRG_GLSL_HEADER_DESKTOP_LINES (1)

/* the OpenGL ES 3 header: GLSL ES 3.00 and every default precision the
 * shaders rely on */
#define LRG_GLSL_HEADER_ES \
	"#version 300 es\n" \
	"precision highp float;\n" \
	"precision highp int;\n" \
	"precision highp sampler2D;\n" \
	"precision highp samplerCube;\n" \
	"precision highp sampler3D;\n" \
	"precision highp sampler2DShadow;\n" \
	"precision highp sampler2DArray;\n"
#define LRG_GLSL_HEADER_ES_LINES (8)

#if defined(LRG_PLATFORM_ANDROID) || defined(LRG_GLSL_ES)

#define LRG_GLSL_HEADER LRG_GLSL_HEADER_ES
/* how many lines LRG_GLSL_HEADER is (compile errors map back past it) */
#define LRG_GLSL_HEADER_LINES LRG_GLSL_HEADER_ES_LINES
/* the dialect a build uses until lrg_glsl_set_dialect() says otherwise */
#define LRG_GLSL_BUILD_IS_ES (1)

#else

#define LRG_GLSL_HEADER LRG_GLSL_HEADER_DESKTOP
#define LRG_GLSL_HEADER_LINES LRG_GLSL_HEADER_DESKTOP_LINES
#define LRG_GLSL_BUILD_IS_ES (0)

#endif

/*
 * lrg_glsl_load_shader:
 * @vs: (nullable): vertex stage text, or NULL for raylib's default
 * @fs: (nullable): fragment stage text, or NULL for raylib's default
 *
 * LoadShaderFromMemory() with both stages passed through
 * lrg_glsl_prepare_source() first: the raylib Shader of a built-in.
 * Failure behaves like LoadShaderFromMemory() (raylib logs it and
 * returns its default shader or an id of 0).
 *
 * Returns: the raylib shader
 */
G_GNUC_INTERNAL
Shader
lrg_glsl_load_shader (const gchar *vs,
                      const gchar *fs);
