/* lrg-shader-asset.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgShaderAsset: a user shader (`.rshader`) written against the
 * engine's GLSL 330 contract, with described uniforms, render state
 * defaults, compile diagnostics mapped to the user's own line numbers
 * and hot reload.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-light-set.h"

G_BEGIN_DECLS

/**
 * LRG_SHADER_ASSET_FIRST_TEXTURE_UNIT:
 *
 * The texture unit lrg_shader_asset_apply() starts binding sampler
 * uniforms from when the caller passes no unit.  Units 0 .. 6 hold the
 * material maps of the lit contract and 12 .. 15 the shadow and light
 * grid textures, so user samplers start at 7.
 */
#define LRG_SHADER_ASSET_FIRST_TEXTURE_UNIT (7)

/**
 * LRG_SHADER_ASSET_ERROR:
 *
 * Error domain of #LrgShaderAsset.  Errors are #LrgShaderAssetError
 * values.
 */
#define LRG_SHADER_ASSET_ERROR (lrg_shader_asset_error_quark ())

/**
 * lrg_shader_asset_error_quark:
 *
 * The error quark of #LRG_SHADER_ASSET_ERROR.
 *
 * Returns: the quark
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_shader_asset_error_quark (void);

/**
 * LrgShaderAssetError:
 * @LRG_SHADER_ASSET_ERROR_PARSE: the `.rshader` document is malformed
 *   (not YAML, wrong shape, unknown uniform type, bad value)
 * @LRG_SHADER_ASSET_ERROR_IO: a file (the document or a stage source)
 *   could not be read or written
 * @LRG_SHADER_ASSET_ERROR_NO_CONTEXT: compiling needs a graphics context
 *   (an open window) and there is none
 * @LRG_SHADER_ASSET_ERROR_COMPILE: a stage failed to compile; the message
 *   lists the diagnostics with user line numbers
 * @LRG_SHADER_ASSET_ERROR_LINK: the stages compiled but did not link
 * @LRG_SHADER_ASSET_ERROR_UNIFORM: a uniform value names no declared
 *   uniform or has the wrong type
 *
 * Errors reported by #LrgShaderAsset.
 */
typedef enum
{
	LRG_SHADER_ASSET_ERROR_PARSE,
	LRG_SHADER_ASSET_ERROR_IO,
	LRG_SHADER_ASSET_ERROR_NO_CONTEXT,
	LRG_SHADER_ASSET_ERROR_COMPILE,
	LRG_SHADER_ASSET_ERROR_LINK,
	LRG_SHADER_ASSET_ERROR_UNIFORM
} LrgShaderAssetError;

LRG_AVAILABLE_IN_ALL
GType lrg_shader_asset_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SHADER_ASSET_ERROR (lrg_shader_asset_error_get_type ())

/**
 * LrgShaderStage:
 * @LRG_SHADER_STAGE_VERTEX: the vertex stage
 * @LRG_SHADER_STAGE_FRAGMENT: the fragment stage
 *
 * A programmable pipeline stage of a shader asset.
 */
typedef enum
{
	LRG_SHADER_STAGE_VERTEX,
	LRG_SHADER_STAGE_FRAGMENT
} LrgShaderStage;

LRG_AVAILABLE_IN_ALL
GType lrg_shader_stage_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SHADER_STAGE (lrg_shader_stage_get_type ())

/**
 * LrgShaderUniformType:
 * @LRG_SHADER_UNIFORM_FLOAT: `float`
 * @LRG_SHADER_UNIFORM_VEC2: `vec2`
 * @LRG_SHADER_UNIFORM_VEC3: `vec3`
 * @LRG_SHADER_UNIFORM_VEC4: `vec4`
 * @LRG_SHADER_UNIFORM_COLOR: `vec4` shown as a colour picker (RGBA in
 *   0 .. 1, sRGB like `colDiffuse`)
 * @LRG_SHADER_UNIFORM_INT: `int`
 * @LRG_SHADER_UNIFORM_BOOL: `bool` (uploaded as an int 0 / 1)
 * @LRG_SHADER_UNIFORM_SAMPLER2D: `sampler2D`, bound to a texture unit
 *
 * The GLSL type of a described uniform.  The nick (`float`, `vec2`,
 * `color`, `sampler2D`...) is the `type:` value of the `.rshader` format.
 */
typedef enum
{
	LRG_SHADER_UNIFORM_FLOAT,
	LRG_SHADER_UNIFORM_VEC2,
	LRG_SHADER_UNIFORM_VEC3,
	LRG_SHADER_UNIFORM_VEC4,
	LRG_SHADER_UNIFORM_COLOR,
	LRG_SHADER_UNIFORM_INT,
	LRG_SHADER_UNIFORM_BOOL,
	LRG_SHADER_UNIFORM_SAMPLER2D
} LrgShaderUniformType;

LRG_AVAILABLE_IN_ALL
GType lrg_shader_uniform_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SHADER_UNIFORM_TYPE (lrg_shader_uniform_type_get_type ())

/**
 * lrg_shader_uniform_type_get_components:
 * @type: an #LrgShaderUniformType
 *
 * The number of float components a value of @type holds (1 for float,
 * int, bool and sampler2D; 2, 3 or 4 for vectors; 4 for colours).
 *
 * Returns: 1 .. 4
 */
LRG_AVAILABLE_IN_ALL
guint lrg_shader_uniform_type_get_components (LrgShaderUniformType type);

/**
 * lrg_shader_uniform_type_get_glsl:
 * @type: an #LrgShaderUniformType
 *
 * The GLSL type name a uniform of @type is declared with (`vec4` for a
 * colour).
 *
 * Returns: (transfer none): a static string
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_uniform_type_get_glsl (LrgShaderUniformType type);

/* ========================================================================== */
/* LrgShaderUniform                                                           */
/* ========================================================================== */

/**
 * LrgShaderUniform:
 *
 * The description of one uniform a shader asset exposes: its name, GLSL
 * type, default value, optional range, an inspector hint and, for
 * samplers, a default texture path.  It feeds the uniform inspector and
 * the declarations the asset prepends to the stage sources.  A boxed,
 * copyable value; an asset keeps its own copies.
 */
typedef struct _LrgShaderUniform LrgShaderUniform;

LRG_AVAILABLE_IN_ALL
GType lrg_shader_uniform_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SHADER_UNIFORM (lrg_shader_uniform_get_type ())

/**
 * lrg_shader_uniform_new:
 * @name: the GLSL identifier
 * @type: the uniform type
 *
 * Creates a uniform description with a zero default (white `1, 1, 1, 1`
 * for colours), no range, no hint and no default texture.
 *
 * Returns: (transfer full): a new #LrgShaderUniform
 */
LRG_AVAILABLE_IN_ALL
LrgShaderUniform * lrg_shader_uniform_new (const gchar          *name,
                                           LrgShaderUniformType  type);

/**
 * lrg_shader_uniform_copy:
 * @self: an #LrgShaderUniform
 *
 * Returns: (transfer full): a deep copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgShaderUniform * lrg_shader_uniform_copy (const LrgShaderUniform *self);

/**
 * lrg_shader_uniform_free:
 * @self: (nullable): an #LrgShaderUniform
 *
 * Frees @self.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shader_uniform_free (LrgShaderUniform *self);

/**
 * lrg_shader_uniform_get_name:
 * @self: an #LrgShaderUniform
 *
 * Returns: (transfer none): the GLSL identifier
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_uniform_get_name (const LrgShaderUniform *self);

/**
 * lrg_shader_uniform_get_uniform_type:
 * @self: an #LrgShaderUniform
 *
 * Returns: the uniform type
 */
LRG_AVAILABLE_IN_ALL
LrgShaderUniformType lrg_shader_uniform_get_uniform_type (const LrgShaderUniform *self);

/**
 * lrg_shader_uniform_set_default:
 * @self: an #LrgShaderUniform
 * @values: (array length=n_values): the default components
 * @n_values: how many components @values holds (missing ones keep
 *   their current value, extra ones are ignored)
 *
 * Sets the default value.  Ints and bools are stored as floats and
 * rounded when uploaded.  Ignored for samplers (use
 * lrg_shader_uniform_set_default_texture()).
 */
LRG_AVAILABLE_IN_ALL
void lrg_shader_uniform_set_default (LrgShaderUniform *self,
                                     const gfloat     *values,
                                     guint             n_values);

/**
 * lrg_shader_uniform_get_default:
 * @self: an #LrgShaderUniform
 * @out_values: (out caller-allocates) (array fixed-size=4): receives
 *   the four default components (unused ones are 0)
 *
 * Reads the default value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shader_uniform_get_default (const LrgShaderUniform *self,
                                     gfloat                 *out_values);

/**
 * lrg_shader_uniform_set_range:
 * @self: an #LrgShaderUniform
 * @min: the smallest value the inspector offers
 * @max: the largest value the inspector offers
 *
 * Sets the inspector range applied to every component.  The range is a
 * hint for editing; values set from code are not clamped.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shader_uniform_set_range (LrgShaderUniform *self,
                                   gfloat            min,
                                   gfloat            max);

/**
 * lrg_shader_uniform_clear_range:
 * @self: an #LrgShaderUniform
 *
 * Removes the range.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shader_uniform_clear_range (LrgShaderUniform *self);

/**
 * lrg_shader_uniform_get_range:
 * @self: an #LrgShaderUniform
 * @out_min: (out) (optional): the minimum
 * @out_max: (out) (optional): the maximum
 *
 * Returns: %TRUE when the uniform has a range (the outputs are only
 *   written then)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_uniform_get_range (const LrgShaderUniform *self,
                                       gfloat                 *out_min,
                                       gfloat                 *out_max);

/**
 * lrg_shader_uniform_set_hint:
 * @self: an #LrgShaderUniform
 * @hint: (nullable): an inspector hint (`slider`, `drag`, `color`,
 *   `direction`, `toggle`, `hidden`, or any word a custom inspector
 *   understands), or %NULL for none
 *
 * Sets how the uniform inspector shows the value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shader_uniform_set_hint (LrgShaderUniform *self,
                                  const gchar      *hint);

/**
 * lrg_shader_uniform_get_hint:
 * @self: an #LrgShaderUniform
 *
 * Returns: (transfer none) (nullable): the inspector hint
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_uniform_get_hint (const LrgShaderUniform *self);

/**
 * lrg_shader_uniform_set_default_texture:
 * @self: an #LrgShaderUniform
 * @path: (nullable): an image path (relative to the `.rshader` file) or
 *   a `guid:` reference, or %NULL for a white 1 x 1 texture
 *
 * Sets the texture a sampler uniform shows until a texture is set.
 * Plain paths load when the asset compiles; `guid:` references are left
 * for the caller (the material or the asset manager) to resolve.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shader_uniform_set_default_texture (LrgShaderUniform *self,
                                             const gchar      *path);

/**
 * lrg_shader_uniform_get_default_texture:
 * @self: an #LrgShaderUniform
 *
 * Returns: (transfer none) (nullable): the default texture reference
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_uniform_get_default_texture (const LrgShaderUniform *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgShaderUniform, lrg_shader_uniform_free)

/* ========================================================================== */
/* LrgShaderDiagnostic                                                        */
/* ========================================================================== */

/**
 * LrgShaderDiagnostic:
 *
 * One message of a GLSL compiler or linker log, with its position mapped
 * back from the generated text the driver saw to the user's own source.
 * Boxed and immutable.
 */
typedef struct _LrgShaderDiagnostic LrgShaderDiagnostic;

LRG_AVAILABLE_IN_ALL
GType lrg_shader_diagnostic_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SHADER_DIAGNOSTIC (lrg_shader_diagnostic_get_type ())

/**
 * lrg_shader_diagnostic_copy:
 * @self: an #LrgShaderDiagnostic
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgShaderDiagnostic * lrg_shader_diagnostic_copy (const LrgShaderDiagnostic *self);

/**
 * lrg_shader_diagnostic_free:
 * @self: (nullable): an #LrgShaderDiagnostic
 *
 * Frees @self.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shader_diagnostic_free (LrgShaderDiagnostic *self);

/**
 * lrg_shader_diagnostic_get_stage:
 * @self: an #LrgShaderDiagnostic
 *
 * Returns: the stage the message is about (link messages report the
 *   fragment stage)
 */
LRG_AVAILABLE_IN_ALL
LrgShaderStage lrg_shader_diagnostic_get_stage (const LrgShaderDiagnostic *self);

/**
 * lrg_shader_diagnostic_get_source:
 * @self: an #LrgShaderDiagnostic
 *
 * The name of the text the line refers to: the stage file path, or
 * `<document>#vertex` / `<document>#fragment` for inline sources, or
 * `<preamble>` when the line falls in the generated preamble (the
 * engine declarations and the lit library).
 *
 * Returns: (transfer none): the source name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_diagnostic_get_source (const LrgShaderDiagnostic *self);

/**
 * lrg_shader_diagnostic_get_line:
 * @self: an #LrgShaderDiagnostic
 *
 * Returns: the 1-based line in the user's source (or in the preamble
 *   when lrg_shader_diagnostic_is_in_preamble()), 0 when the log gave
 *   no position
 */
LRG_AVAILABLE_IN_ALL
guint lrg_shader_diagnostic_get_line (const LrgShaderDiagnostic *self);

/**
 * lrg_shader_diagnostic_get_column:
 * @self: an #LrgShaderDiagnostic
 *
 * Returns: the 1-based column, 0 when the driver gave none
 */
LRG_AVAILABLE_IN_ALL
guint lrg_shader_diagnostic_get_column (const LrgShaderDiagnostic *self);

/**
 * lrg_shader_diagnostic_is_error:
 * @self: an #LrgShaderDiagnostic
 *
 * Returns: %TRUE for an error, %FALSE for a warning
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_diagnostic_is_error (const LrgShaderDiagnostic *self);

/**
 * lrg_shader_diagnostic_is_in_preamble:
 * @self: an #LrgShaderDiagnostic
 *
 * Returns: %TRUE when the line lies in the generated preamble rather
 *   than in the user's source
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_diagnostic_is_in_preamble (const LrgShaderDiagnostic *self);

/**
 * lrg_shader_diagnostic_get_message:
 * @self: an #LrgShaderDiagnostic
 *
 * Returns: (transfer none): the driver's message without its position
 *   prefix
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_diagnostic_get_message (const LrgShaderDiagnostic *self);

/**
 * lrg_shader_diagnostic_to_string:
 * @self: an #LrgShaderDiagnostic
 *
 * Formats the diagnostic as `source:line:column: error: message`
 * (`warning:` for warnings; the column, or line and column, are left
 * out when unknown), the form compilers use and editors jump to.
 *
 * Returns: (transfer full): the formatted text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_shader_diagnostic_to_string (const LrgShaderDiagnostic *self);

/**
 * lrg_shader_parse_info_log:
 * @log: a GLSL compiler or linker info log
 * @stage: the stage the log belongs to
 * @preamble_lines: how many lines were prepended to the user's source
 * @source_name: the name diagnostics in the user's source report
 *
 * Parses a driver log into diagnostics.  Mesa (`0:12(5): error: ...`),
 * NVIDIA (`0(12) : error C1008: ...`) and AMD / Intel Windows
 * (`ERROR: 0:12: ...`) positions are recognised; lines beyond
 * @preamble_lines are reported against @source_name with the preamble
 * subtracted, earlier ones against `<preamble>`.  Lines the parser does
 * not recognise become positionless errors.  Headless.
 *
 * Returns: (transfer full) (element-type LrgShaderDiagnostic): the
 *   diagnostics in log order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_shader_parse_info_log (const gchar    *log,
                                       LrgShaderStage  stage,
                                       guint           preamble_lines,
                                       const gchar    *source_name);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgShaderDiagnostic, lrg_shader_diagnostic_free)

/* ========================================================================== */
/* LrgShaderAsset                                                             */
/* ========================================================================== */

#define LRG_TYPE_SHADER_ASSET (lrg_shader_asset_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgShaderAsset, lrg_shader_asset, LRG, SHADER_ASSET, GObject)

/**
 * lrg_shader_asset_new:
 *
 * Creates an empty unlit asset named `untitled` with no sources (the
 * default vertex stage and a fragment stage that must be set before
 * compiling), opaque back-face-culled depth-tested render state.
 *
 * Returns: (transfer full): a new #LrgShaderAsset
 */
LRG_AVAILABLE_IN_ALL
LrgShaderAsset * lrg_shader_asset_new (void);

/**
 * lrg_shader_asset_new_from_file:
 * @path: a `.rshader` file
 * @error: return location for a #GError, or %NULL
 *
 * Loads a `.rshader` document.  Stage paths resolve against the
 * document's directory; their text is read now.  Headless: nothing is
 * compiled until lrg_shader_asset_compile().
 *
 * Returns: (transfer full) (nullable): the asset, or %NULL with @error
 *   set (%LRG_SHADER_ASSET_ERROR_PARSE or %LRG_SHADER_ASSET_ERROR_IO)
 */
LRG_AVAILABLE_IN_ALL
LrgShaderAsset * lrg_shader_asset_new_from_file (const gchar  *path,
                                                 GError      **error);

/**
 * lrg_shader_asset_new_from_data:
 * @data: `.rshader` text
 * @length: length of @data in bytes, or -1 when nul-terminated
 * @base_dir: (nullable): the directory stage paths resolve against
 *   (%NULL: the current directory)
 * @error: return location for a #GError, or %NULL
 *
 * Loads a `.rshader` document from memory (the editor validates the
 * text of an open document with it).
 *
 * Returns: (transfer full) (nullable): the asset, or %NULL with @error
 *   set
 */
LRG_AVAILABLE_IN_ALL
LrgShaderAsset * lrg_shader_asset_new_from_data (const gchar  *data,
                                                 gssize        length,
                                                 const gchar  *base_dir,
                                                 GError      **error);

/**
 * lrg_shader_asset_save_to_data:
 * @self: an #LrgShaderAsset
 * @length: (out) (optional): the length of the text
 * @error: return location for a #GError, or %NULL
 *
 * Writes the asset as a `.rshader` document: stages loaded from paths
 * keep their paths, inline stages are written as literal blocks,
 * defaults are left out and unknown top-level keys of the loaded
 * document are written back.  Byte-stable on reload.
 *
 * Returns: (transfer full) (nullable): the document text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_shader_asset_save_to_data (LrgShaderAsset  *self,
                                       gsize           *length,
                                       GError         **error);

/**
 * lrg_shader_asset_save_to_file:
 * @self: an #LrgShaderAsset
 * @path: the file to write (atomically)
 * @error: return location for a #GError, or %NULL
 *
 * Writes lrg_shader_asset_save_to_data() to @path.  Stage files are not
 * written; only the document.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_save_to_file (LrgShaderAsset  *self,
                                        const gchar     *path,
                                        GError         **error);

/**
 * lrg_shader_asset_get_path:
 * @self: an #LrgShaderAsset
 *
 * Returns: (transfer none) (nullable): the `.rshader` file the asset was
 *   loaded from, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_asset_get_path (LrgShaderAsset *self);

/**
 * lrg_shader_asset_get_name:
 * @self: an #LrgShaderAsset
 *
 * Returns: (transfer none): the display name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_asset_get_name (LrgShaderAsset *self);

/**
 * lrg_shader_asset_set_name:
 * @self: an #LrgShaderAsset
 * @name: the display name
 *
 * Sets the display name (the `name:` key).
 */
LRG_AVAILABLE_IN_ALL
void lrg_shader_asset_set_name (LrgShaderAsset *self,
                                const gchar    *name);

/**
 * lrg_shader_asset_get_lit:
 * @self: an #LrgShaderAsset
 *
 * Returns: whether the fragment stage gets the lit library
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_get_lit (LrgShaderAsset *self);

/**
 * lrg_shader_asset_set_lit:
 * @self: an #LrgShaderAsset
 * @lit: %TRUE to prepend the lit library to the fragment stage
 *
 * A lit asset's fragment stage is prefixed with the light set's GLSL
 * library (lrg_light_set_get_glsl_library(): every lighting uniform and
 * `vec3 lrg_light_pbr (vec3 p, vec3 n, vec3 v, vec4 albedo, float
 * metal, float rough, float ao)`), and an asset without its own vertex
 * stage uses the light set's vertex stage.  Takes effect at the next
 * compile.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shader_asset_set_lit (LrgShaderAsset *self,
                               gboolean        lit);

/**
 * lrg_shader_asset_set_library_source:
 * @self: an #LrgShaderAsset
 * @glsl: (nullable): GLSL to use as the lit library, or %NULL for
 *   lrg_light_set_get_glsl_library()
 *
 * Replaces the lit library for this asset: a custom lighting model, or
 * a fixed library in tests (it is the documented seam that lets the lit
 * path be tested independently of the light set).  The text must not
 * contain `#version` or `main`.  Takes effect at the next compile.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shader_asset_set_library_source (LrgShaderAsset *self,
                                          const gchar    *glsl);

/**
 * lrg_shader_asset_get_library_source:
 * @self: an #LrgShaderAsset
 *
 * Returns: (transfer none): the lit library the next compile uses (the
 *   override, else lrg_light_set_get_glsl_library())
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_asset_get_library_source (LrgShaderAsset *self);

/**
 * lrg_shader_asset_set_source:
 * @self: an #LrgShaderAsset
 * @stage: the stage
 * @source: (nullable): GLSL body of the stage (without `#version`; a
 *   leading `#version` line is blanked), or %NULL to clear it (vertex:
 *   use the default vertex stage)
 *
 * Sets an inline stage source (written inline by the writer).  Clears
 * the stage's path.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shader_asset_set_source (LrgShaderAsset *self,
                                  LrgShaderStage  stage,
                                  const gchar    *source);

/**
 * lrg_shader_asset_get_source:
 * @self: an #LrgShaderAsset
 * @stage: the stage
 *
 * Returns: (transfer none) (nullable): the stage's user source (inline
 *   text, or the text read from its path), %NULL when unset
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_asset_get_source (LrgShaderAsset *self,
                                           LrgShaderStage  stage);

/**
 * lrg_shader_asset_set_source_path:
 * @self: an #LrgShaderAsset
 * @stage: the stage
 * @path: the GLSL file, as written in the document (relative paths
 *   resolve against the document's directory)
 * @error: return location for a #GError, or %NULL
 *
 * Points the stage at a file and reads it now.  Watched by hot reload.
 *
 * Returns: %TRUE when the file was read (on failure nothing changes)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_set_source_path (LrgShaderAsset  *self,
                                           LrgShaderStage   stage,
                                           const gchar     *path,
                                           GError         **error);

/**
 * lrg_shader_asset_get_source_path:
 * @self: an #LrgShaderAsset
 * @stage: the stage
 *
 * Returns: (transfer none) (nullable): the stage's path as written in
 *   the document, or %NULL for an inline or unset stage
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_asset_get_source_path (LrgShaderAsset *self,
                                                LrgShaderStage  stage);

/* Render state defaults */

/**
 * lrg_shader_asset_get_blend_mode:
 * @self: an #LrgShaderAsset
 *
 * Returns: (transfer none): the default blend mode: `opaque`, `cutout`,
 *   `alpha`, `additive` or `multiply`
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_asset_get_blend_mode (LrgShaderAsset *self);

/**
 * lrg_shader_asset_set_blend_mode:
 * @self: an #LrgShaderAsset
 * @mode: `opaque`, `cutout`, `alpha`, `additive` or `multiply`
 *
 * Sets the blend mode a material using this shader starts from (the
 * names match the material blend mode nicks).
 *
 * Returns: %FALSE when @mode is not a known mode (nothing changes)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_set_blend_mode (LrgShaderAsset *self,
                                          const gchar    *mode);

/**
 * lrg_shader_asset_get_cull_mode:
 * @self: an #LrgShaderAsset
 *
 * Returns: (transfer none): the default cull mode: `back`, `front` or
 *   `none`
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_asset_get_cull_mode (LrgShaderAsset *self);

/**
 * lrg_shader_asset_set_cull_mode:
 * @self: an #LrgShaderAsset
 * @mode: `back`, `front` or `none`
 *
 * Sets the default face culling.
 *
 * Returns: %FALSE when @mode is not a known mode (nothing changes)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_set_cull_mode (LrgShaderAsset *self,
                                         const gchar    *mode);

/**
 * lrg_shader_asset_get_depth_test:
 * @self: an #LrgShaderAsset
 *
 * Returns: whether depth testing is on by default
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_get_depth_test (LrgShaderAsset *self);

/**
 * lrg_shader_asset_set_depth_test:
 * @self: an #LrgShaderAsset
 * @depth_test: the default depth test
 *
 * Sets whether depth testing is on by default.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shader_asset_set_depth_test (LrgShaderAsset *self,
                                      gboolean        depth_test);

/**
 * lrg_shader_asset_get_depth_write:
 * @self: an #LrgShaderAsset
 *
 * Returns: whether depth writes are on by default
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_get_depth_write (LrgShaderAsset *self);

/**
 * lrg_shader_asset_set_depth_write:
 * @self: an #LrgShaderAsset
 * @depth_write: the default depth write
 *
 * Sets whether depth writes are on by default.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shader_asset_set_depth_write (LrgShaderAsset *self,
                                       gboolean        depth_write);

/**
 * lrg_shader_asset_get_alpha_cutoff:
 * @self: an #LrgShaderAsset
 *
 * Returns: the default alpha cutoff of the `cutout` blend mode
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shader_asset_get_alpha_cutoff (LrgShaderAsset *self);

/**
 * lrg_shader_asset_set_alpha_cutoff:
 * @self: an #LrgShaderAsset
 * @cutoff: 0 .. 1
 *
 * Sets the default alpha cutoff.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shader_asset_set_alpha_cutoff (LrgShaderAsset *self,
                                        gfloat          cutoff);

/* Uniform descriptions */

/**
 * lrg_shader_asset_add_uniform:
 * @self: an #LrgShaderAsset
 * @uniform: the description (copied)
 *
 * Adds or replaces (by name, keeping its position) a uniform
 * description.  Its current value becomes the default.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shader_asset_add_uniform (LrgShaderAsset         *self,
                                   const LrgShaderUniform *uniform);

/**
 * lrg_shader_asset_remove_uniform:
 * @self: an #LrgShaderAsset
 * @name: a uniform name
 *
 * Returns: %TRUE when a uniform was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_remove_uniform (LrgShaderAsset *self,
                                          const gchar    *name);

/**
 * lrg_shader_asset_get_n_uniforms:
 * @self: an #LrgShaderAsset
 *
 * Returns: the number of described uniforms
 */
LRG_AVAILABLE_IN_ALL
guint lrg_shader_asset_get_n_uniforms (LrgShaderAsset *self);

/**
 * lrg_shader_asset_get_uniform_at:
 * @self: an #LrgShaderAsset
 * @index: 0 .. lrg_shader_asset_get_n_uniforms() - 1
 *
 * Returns: (transfer none) (nullable): the description at @index, in
 *   document order
 */
LRG_AVAILABLE_IN_ALL
const LrgShaderUniform * lrg_shader_asset_get_uniform_at (LrgShaderAsset *self,
                                                          guint           index);

/**
 * lrg_shader_asset_get_uniform:
 * @self: an #LrgShaderAsset
 * @name: a uniform name
 *
 * Returns: (transfer none) (nullable): the description, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const LrgShaderUniform * lrg_shader_asset_get_uniform (LrgShaderAsset *self,
                                                       const gchar    *name);

/* Uniform values */

/**
 * lrg_shader_asset_set_uniform_float:
 * @self: an #LrgShaderAsset
 * @name: a `float`, `int` or `bool` uniform
 * @value: the value
 *
 * Sets a stored value uploaded by lrg_shader_asset_apply().
 *
 * Returns: %FALSE when @name is unknown or not a one-component number
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_set_uniform_float (LrgShaderAsset *self,
                                             const gchar    *name,
                                             gfloat          value);

/**
 * lrg_shader_asset_set_uniform_int:
 * @self: an #LrgShaderAsset
 * @name: an `int`, `bool` or `float` uniform
 * @value: the value
 *
 * Sets a stored integer value.
 *
 * Returns: %FALSE when @name is unknown or not a one-component number
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_set_uniform_int (LrgShaderAsset *self,
                                           const gchar    *name,
                                           gint            value);

/**
 * lrg_shader_asset_set_uniform_vec2:
 * @self: an #LrgShaderAsset
 * @name: a `vec2` uniform
 * @x: first component
 * @y: second component
 *
 * Sets a stored `vec2` value.
 *
 * Returns: %FALSE when @name is unknown or not a `vec2`
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_set_uniform_vec2 (LrgShaderAsset *self,
                                            const gchar    *name,
                                            gfloat          x,
                                            gfloat          y);

/**
 * lrg_shader_asset_set_uniform_vec3:
 * @self: an #LrgShaderAsset
 * @name: a `vec3` uniform
 * @x: first component
 * @y: second component
 * @z: third component
 *
 * Sets a stored `vec3` value.
 *
 * Returns: %FALSE when @name is unknown or not a `vec3`
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_set_uniform_vec3 (LrgShaderAsset *self,
                                            const gchar    *name,
                                            gfloat          x,
                                            gfloat          y,
                                            gfloat          z);

/**
 * lrg_shader_asset_set_uniform_vec4:
 * @self: an #LrgShaderAsset
 * @name: a `vec4` or `color` uniform
 * @x: first component (red)
 * @y: second component (green)
 * @z: third component (blue)
 * @w: fourth component (alpha)
 *
 * Sets a stored `vec4` or colour value.
 *
 * Returns: %FALSE when @name is unknown or not four components
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_set_uniform_vec4 (LrgShaderAsset *self,
                                            const gchar    *name,
                                            gfloat          x,
                                            gfloat          y,
                                            gfloat          z,
                                            gfloat          w);

/**
 * lrg_shader_asset_set_uniform_texture:
 * @self: an #LrgShaderAsset
 * @name: a `sampler2D` uniform
 * @texture: (nullable): the texture (a reference is kept), or %NULL for
 *   the default texture
 *
 * Sets the texture a sampler uniform binds.
 *
 * Returns: %FALSE when @name is unknown or not a sampler
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_set_uniform_texture (LrgShaderAsset *self,
                                               const gchar    *name,
                                               GrlTexture     *texture);

/**
 * lrg_shader_asset_get_uniform_value:
 * @self: an #LrgShaderAsset
 * @name: a uniform name
 * @out_values: (out caller-allocates) (array fixed-size=4): receives the
 *   stored components (unused ones 0; samplers read 0)
 *
 * Reads a stored value.
 *
 * Returns: %FALSE when @name is unknown (@out_values is zeroed)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_get_uniform_value (LrgShaderAsset *self,
                                             const gchar    *name,
                                             gfloat         *out_values);

/**
 * lrg_shader_asset_reset_uniforms:
 * @self: an #LrgShaderAsset
 *
 * Returns every stored value to its default and drops set textures.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shader_asset_reset_uniforms (LrgShaderAsset *self);

/**
 * lrg_shader_asset_set_time:
 * @self: an #LrgShaderAsset
 * @seconds: the value of the built-in `lrgTime` uniform
 *
 * Sets the time lrg_shader_asset_apply() uploads as `lrgTime`.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shader_asset_set_time (LrgShaderAsset *self,
                                gfloat          seconds);

/**
 * lrg_shader_asset_get_time:
 * @self: an #LrgShaderAsset
 *
 * Returns: the `lrgTime` value
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shader_asset_get_time (LrgShaderAsset *self);

/* Compiling and binding */

/**
 * lrg_shader_asset_compile:
 * @self: an #LrgShaderAsset
 * @error: return location for a #GError, or %NULL
 *
 * Builds both stages (preamble + user source), compiles and links them
 * on the current GL context.  On failure the previous program (if any)
 * stays in use, lrg_shader_asset_get_diagnostics() holds the mapped
 * messages and @error carries them one per line as
 * `source:line:column: error: message`.  Default textures of sampler
 * uniforms load on success.
 *
 * Returns: %TRUE when the new program is in use
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_compile (LrgShaderAsset  *self,
                                   GError         **error);

/**
 * lrg_shader_asset_is_compiled:
 * @self: an #LrgShaderAsset
 *
 * Returns: %TRUE when a program exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_is_compiled (LrgShaderAsset *self);

/**
 * lrg_shader_asset_get_shader:
 * @self: an #LrgShaderAsset
 *
 * The compiled program as a graylib shader, for raylib materials
 * (`material.shader`), lrg_static_batch_set_shader() and friends.  The
 * object is replaced by a successful reload: look it up again after
 * #LrgShaderAsset::reloaded instead of caching it.
 *
 * Returns: (transfer none) (nullable): the shader, or %NULL before a
 *   successful compile
 */
LRG_AVAILABLE_IN_ALL
GrlShader * lrg_shader_asset_get_shader (LrgShaderAsset *self);

/**
 * lrg_shader_asset_get_shader_id:
 * @self: an #LrgShaderAsset
 *
 * Returns: the GL program id, 0 before a successful compile
 */
LRG_AVAILABLE_IN_ALL
guint lrg_shader_asset_get_shader_id (LrgShaderAsset *self);

/**
 * lrg_shader_asset_get_generated_source:
 * @self: an #LrgShaderAsset
 * @stage: the stage
 * @out_preamble_lines: (out) (optional): how many lines precede the
 *   user's source
 *
 * Builds the full text a compile hands to the driver: `#version 330`,
 * the engine defines and declarations, the lit library for a lit
 * fragment stage, the declared uniforms, then the user's source.
 * Headless (the editor's "show generated code" view).
 *
 * Returns: (transfer full): the stage text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_shader_asset_get_generated_source (LrgShaderAsset *self,
                                               LrgShaderStage  stage,
                                               guint          *out_preamble_lines);

/**
 * lrg_shader_asset_get_diagnostics:
 * @self: an #LrgShaderAsset
 *
 * Returns: (transfer none) (element-type LrgShaderDiagnostic): the
 *   diagnostics of the last compile (errors after a failure; driver
 *   warnings, often none, after a success)
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_shader_asset_get_diagnostics (LrgShaderAsset *self);

/**
 * lrg_shader_asset_apply:
 * @self: an #LrgShaderAsset
 * @texture_unit: (inout) (optional): the first texture unit to bind
 *   samplers to, advanced past the units used; %NULL starts at
 *   %LRG_SHADER_ASSET_FIRST_TEXTURE_UNIT
 *
 * Uploads every stored uniform value and `lrgTime` to the program and
 * binds sampler textures (set textures, else defaults, else white).  The
 * program is left bound, like raylib's SetShaderValue().  Call before
 * drawing with lrg_shader_asset_get_shader(); a lit asset additionally
 * needs the light set's uniforms bound to the same program.
 *
 * Returns: %FALSE when nothing is compiled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_apply (LrgShaderAsset *self,
                                 gint           *texture_unit);

/**
 * lrg_shader_asset_apply_uniform:
 * @self: an #LrgShaderAsset
 * @name: a declared uniform (not a sampler)
 * @values: (array length=n_values): the components
 * @n_values: the number of components; must equal the uniform's
 *   component count
 * @error: return location for a #GError, or %NULL
 *
 * Uploads one value straight to the program without storing it: the
 * per-draw path a material uses to apply its own uniform table over the
 * asset's stored values (call after lrg_shader_asset_apply()).
 *
 * Returns: %TRUE when uploaded (%LRG_SHADER_ASSET_ERROR_UNIFORM for an
 *   unknown name or wrong component count)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_apply_uniform (LrgShaderAsset  *self,
                                         const gchar     *name,
                                         const gfloat    *values,
                                         guint            n_values,
                                         GError         **error);

/**
 * lrg_shader_asset_apply_uniform_texture:
 * @self: an #LrgShaderAsset
 * @name: a declared `sampler2D` uniform
 * @texture_id: the GL texture id (0: white)
 * @texture_unit: (inout) (optional): the unit to bind to, advanced by
 *   one; %NULL uses %LRG_SHADER_ASSET_FIRST_TEXTURE_UNIT
 * @error: return location for a #GError, or %NULL
 *
 * Binds a texture to a sampler uniform without storing it (per-draw
 * material textures).
 *
 * Returns: %TRUE when bound
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_apply_uniform_texture (LrgShaderAsset  *self,
                                                 const gchar     *name,
                                                 guint            texture_id,
                                                 gint            *texture_unit,
                                                 GError         **error);

/**
 * lrg_shader_asset_bind_lights:
 * @self: an #LrgShaderAsset
 * @lights: the light set to light the program with
 *
 * Uploads @lights' state to this asset's program with
 * lrg_light_set_bind_shader(): the lights of the last
 * lrg_light_set_apply(), the grid, shadows, fog, environment / SSAO /
 * decal hooks, and on first use the library's sampler units and the
 * material defaults.  Call after lrg_light_set_apply() each frame, before
 * drawing a lit asset; an unbound lit program draws nothing.  The asset
 * remembers @lights (weakly) and calls lrg_light_set_forget_shader() when
 * its program is replaced (compile, hot reload), released or finalized.
 *
 * Returns: %TRUE when uploaded (%FALSE before a compile or without a
 *   window)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_bind_lights (LrgShaderAsset *self,
                                       LrgLightSet    *lights);

/**
 * lrg_shader_asset_get_uniform_location:
 * @self: an #LrgShaderAsset
 * @name: any uniform name
 *
 * Returns: the program's location of @name (cached), -1 when absent or
 *   optimised out, or before a compile
 */
LRG_AVAILABLE_IN_ALL
gint lrg_shader_asset_get_uniform_location (LrgShaderAsset *self,
                                            const gchar    *name);

/**
 * lrg_shader_asset_release:
 * @self: an #LrgShaderAsset
 *
 * Frees the program and the loaded default textures.  Call before the
 * window closes; the description is kept and can be compiled again.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shader_asset_release (LrgShaderAsset *self);

/* Hot reload */

/**
 * lrg_shader_asset_reload:
 * @self: an #LrgShaderAsset
 * @error: return location for a #GError, or %NULL
 *
 * Reads the document and its stage files again and, when the asset was
 * compiled, recompiles.  Keep-last-good: on any failure the previous
 * description and program stay in use, #LrgShaderAsset::reload-failed
 * is emitted and %FALSE is returned; on success the new ones replace
 * them, stored values of uniforms that kept their name and type survive
 * and #LrgShaderAsset::reloaded is emitted.  An asset that was not
 * loaded from a file only recompiles.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_reload (LrgShaderAsset  *self,
                                  GError         **error);

/**
 * lrg_shader_asset_set_watch:
 * @self: an #LrgShaderAsset
 * @watch: %TRUE to reload automatically when the files change
 * @error: return location for a #GError, or %NULL
 *
 * Starts or stops watching the document and its stage files with
 * #GFileMonitor.  Changes are coalesced for 100 ms, then
 * lrg_shader_asset_reload() runs from the thread-default main context
 * that was current here; the game loop must iterate that context
 * (`g_main_context_iteration (NULL, FALSE)` once per frame) and own the
 * GL context.  Needs a document path.
 *
 * Returns: %TRUE when the watch state is as requested
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_set_watch (LrgShaderAsset  *self,
                                     gboolean         watch,
                                     GError         **error);

/**
 * lrg_shader_asset_get_watch:
 * @self: an #LrgShaderAsset
 *
 * Returns: whether the files are watched
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_asset_get_watch (LrgShaderAsset *self);

/**
 * lrg_shader_asset_get_default_vertex_source:
 *
 * The complete built-in vertex stage an unlit asset without a vertex
 * source uses (lit assets use lrg_light_set_get_vertex_source() when it
 * is not empty).  It writes the contract varyings `fragPosition` (world
 * space), `fragTexCoord`, `fragColor` and `fragNormal` (world space).
 *
 * Returns: (transfer none): GLSL 330 text including `#version`
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_asset_get_default_vertex_source (void);

G_END_DECLS
