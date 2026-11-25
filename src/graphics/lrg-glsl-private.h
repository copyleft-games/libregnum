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
 * docs/guides/rendering-pipeline.org, "ES compatibility".
 *
 * LRG_GLSL_ES forces the ES header on desktop (the shader tests use it
 * to validate the ES text without a phone).
 *
 * Internal header; not installed.
 */

#pragma once

#if defined(LRG_PLATFORM_ANDROID) || defined(LRG_GLSL_ES)

#define LRG_GLSL_HEADER \
	"#version 300 es\n" \
	"precision highp float;\n" \
	"precision highp int;\n" \
	"precision highp sampler2D;\n" \
	"precision highp samplerCube;\n" \
	"precision highp sampler3D;\n" \
	"precision highp sampler2DShadow;\n" \
	"precision highp sampler2DArray;\n"

/* how many lines LRG_GLSL_HEADER is (compile errors map back past it) */
#define LRG_GLSL_HEADER_LINES (8)

#else

#define LRG_GLSL_HEADER "#version 330\n"
#define LRG_GLSL_HEADER_LINES (1)

#endif
