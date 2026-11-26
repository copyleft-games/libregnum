/* lrg-glsl.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The GLSL dialect of the platform: one shader source for desktop
 * OpenGL 3.3 (`#version 330`) and OpenGL ES 3 (`#version 300 es` plus
 * default precisions, Android).
 *
 * A shader body written in the common GLSL 3.30 / ES 3.00 subset goes
 * through lrg_glsl_prepare_source(), which puts the platform's
 * `#version` line and precisions in front of it (replacing a
 * `#version` line the body already has, so a legacy `#version 330`
 * shader works unchanged).  The engine's own built-in shaders go
 * through the same function; lrg_glsl_set_dialect() therefore reaches
 * every shader compiled after it.  The rules for the common subset are
 * in docs/modules/graphics/shaders.org; build-aux/check-glsl.sh
 * validates shader files against both dialects with glslangValidator.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * lrg_glsl_get_dialect:
 *
 * The dialect shaders are written in now: the one set with
 * lrg_glsl_set_dialect(), else the build's own (%LRG_GLSL_DIALECT_GLSL_ES_300
 * on Android, %LRG_GLSL_DIALECT_GLSL_330 elsewhere).  Never
 * %LRG_GLSL_DIALECT_AUTO.
 *
 * Returns: the effective dialect
 */
LRG_AVAILABLE_IN_ALL
LrgGlslDialect
lrg_glsl_get_dialect (void);

/**
 * lrg_glsl_set_dialect:
 * @dialect: the dialect to write shaders in, or %LRG_GLSL_DIALECT_AUTO
 *   for the build's own
 *
 * Chooses the dialect for every shader prepared after this call
 * (process wide, thread safe).  The build's own dialect is right for a
 * normal run; this is for tests that check the ES text on a desktop
 * build and for a desktop build running on an OpenGL ES 3 context
 * (ANGLE, a GLES driver).  Shaders already compiled keep their text.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_glsl_set_dialect (LrgGlslDialect dialect);

/**
 * lrg_glsl_is_es:
 *
 * Whether the effective dialect is GLSL ES 3.00.
 *
 * Returns: %TRUE for `#version 300 es`, %FALSE for `#version 330`
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_glsl_is_es (void);

/**
 * lrg_glsl_get_header:
 *
 * The text lrg_glsl_prepare_source() puts in front of a shader in the
 * effective dialect: `"#version 330\n"`, or `"#version 300 es\n"`
 * followed by `precision highp` statements for `float`, `int`,
 * `sampler2D`, `samplerCube`, `sampler3D`, `sampler2DShadow` and
 * `sampler2DArray`.  Every line ends with a newline.
 *
 * Returns: (transfer none): the header, a static string
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_glsl_get_header (void);

/**
 * lrg_glsl_get_header_lines:
 *
 * How many lines lrg_glsl_get_header() is (1 or 8), for code that maps a
 * compiler's line numbers back to the body.
 *
 * Returns: the header's line count
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_glsl_get_header_lines (void);

/**
 * lrg_glsl_prepare_source:
 * @body: (nullable): the shader text; %NULL is the empty string
 * @defines: (nullable): text inserted after the header, before @body,
 *   usually `#define` lines; a missing final newline is added
 *
 * Builds the source of one shader stage: lrg_glsl_get_header(), then
 * @defines, then @body.
 *
 * When @body already starts with a `#version` directive (after any
 * whitespace and comments, which is the only place GLSL allows one),
 * that directive is replaced: its text is blanked and its newline kept,
 * so the comments before it stay and every body line keeps its place.
 * A line of @body therefore always lands at line
 * `lrg_glsl_get_header_lines() + (lines of @defines) + n` of the result.
 * Precision statements and `#extension` lines of @body are kept.  A
 * `#version` anywhere else is left alone (the compiler rejects it).
 *
 * A @body that already starts with exactly lrg_glsl_get_header() (a
 * source prepared before, or a literal written for this dialect) keeps
 * that header: @defines go after it and nothing else changes, so
 * preparing twice gives the same text.
 *
 * Returns: (transfer full): the newly allocated source
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_glsl_prepare_source (const gchar *body,
                         const gchar *defines);

/**
 * lrg_glsl_shader_new_from_memory:
 * @vs_code: (nullable): vertex stage text, or %NULL for raylib's default
 * @fs_code: (nullable): fragment stage text, or %NULL for raylib's default
 * @error: (nullable): return location for a #GError
 *
 * grl_shader_new_from_memory() with each given stage passed through
 * lrg_glsl_prepare_source() (no defines) first, so the same text
 * compiles on desktop GL and OpenGL ES.  Needs a current GL context.
 *
 * Returns: (transfer full) (nullable): the shader, or %NULL with
 *   @error set when it does not compile
 */
LRG_AVAILABLE_IN_ALL
GrlShader *
lrg_glsl_shader_new_from_memory (const gchar  *vs_code,
                                 const gchar  *fs_code,
                                 GError      **error);

G_END_DECLS
