/* lrg-material.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMaterial: the runtime material asset (`.rmat`).
 *
 * A material is a shading model, a set of PBR parameters
 * (metallic-roughness), texture slots referenced by path or `guid:`,
 * render state (blend, cull, depth) and a render queue hint, plus a table
 * of custom uniforms for user shaders. Materials load from and save to a
 * small YAML format. An instance (lrg_material_new_instance()) overrides
 * some values of a parent and inherits everything else. At draw time
 * lrg_material_apply() uploads the lit shader's material uniforms and
 * binds the textures, lrg_material_apply_state() sets the rlgl render
 * state and lrg_material_restore_state() puts it back.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Constants
 * ========================================================================== */

/**
 * LRG_MATERIAL_FORMAT_VERSION:
 *
 * The `version:` written to `.rmat` files. Files with a higher version
 * are rejected with %LRG_MATERIAL_ERROR_VERSION.
 */
#define LRG_MATERIAL_FORMAT_VERSION (1)

/**
 * LRG_MATERIAL_UNIT_ALBEDO:
 *
 * Texture unit of the albedo map (`texture0`, raylib's
 * MATERIAL_MAP_ALBEDO).
 */
#define LRG_MATERIAL_UNIT_ALBEDO (0)

/**
 * LRG_MATERIAL_UNIT_METALLIC_GLOSS:
 *
 * Texture unit of the legacy metallic (R) / smoothness (A) map
 * (`metallicMap`, raylib's MATERIAL_MAP_METALNESS).
 */
#define LRG_MATERIAL_UNIT_METALLIC_GLOSS (1)

/**
 * LRG_MATERIAL_UNIT_NORMAL:
 *
 * Texture unit of the tangent-space normal map (`normalMap`, raylib's
 * MATERIAL_MAP_NORMAL).
 */
#define LRG_MATERIAL_UNIT_NORMAL (2)

/**
 * LRG_MATERIAL_UNIT_ORM:
 *
 * Texture unit of the occlusion/roughness/metallic map (`ormMap`, glTF
 * layout R = occlusion, G = roughness, B = metallic; raylib's
 * MATERIAL_MAP_OCCLUSION slot).
 */
#define LRG_MATERIAL_UNIT_ORM (4)

/**
 * LRG_MATERIAL_UNIT_EMISSION:
 *
 * Texture unit of the emission map (`emissionMap`, raylib's
 * MATERIAL_MAP_EMISSION). Always bound: the map, or a 1x1 white texture.
 */
#define LRG_MATERIAL_UNIT_EMISSION (5)

/**
 * LRG_MATERIAL_UNIT_HEIGHT:
 *
 * Texture unit of the parallax height map (`heightMap`, raylib's
 * MATERIAL_MAP_HEIGHT).
 */
#define LRG_MATERIAL_UNIT_HEIGHT (6)

/**
 * LRG_MATERIAL_CUSTOM_TEXTURE_UNIT:
 *
 * First texture unit custom texture uniforms may use, the same as
 * LRG_SHADER_ASSET_FIRST_TEXTURE_UNIT (units 0 .. 6 are the material
 * maps). The budget: unlit programs use 7 .. GL_MAX_TEXTURE_IMAGE_UNITS -
 * 1 (at most 31); lit programs (declaring the light set's uniforms) use
 * 7 .. 10 on a driver with more than 16 units (11 .. 15 are the light
 * set's shadow and grid units, 16 and up the environment/SSAO/decal
 * hooks) and nothing on a 16-unit driver (the hooks take 7 .. 10 there).
 * A sampler the program already points at an allowed unit (a shader
 * asset's own allocation) keeps that unit.
 */
#define LRG_MATERIAL_CUSTOM_TEXTURE_UNIT (7)

/**
 * LRG_MATERIAL_MAP_FLAG_NORMAL:
 *
 * `mapFlags` bit: a normal map is bound.
 */
#define LRG_MATERIAL_MAP_FLAG_NORMAL (1)

/**
 * LRG_MATERIAL_MAP_FLAG_METALLIC_GLOSS:
 *
 * `mapFlags` bit: a legacy metallic/gloss map is bound (overrides the
 * metallic and roughness factors).
 */
#define LRG_MATERIAL_MAP_FLAG_METALLIC_GLOSS (2)

/**
 * LRG_MATERIAL_MAP_FLAG_HEIGHT:
 *
 * `mapFlags` bit: a height map is bound (parallax offset).
 */
#define LRG_MATERIAL_MAP_FLAG_HEIGHT (4)

/**
 * LRG_MATERIAL_MAP_FLAG_ORM:
 *
 * `mapFlags` bit: an ORM map is bound (scales the factors).
 */
#define LRG_MATERIAL_MAP_FLAG_ORM (8)

/**
 * LRG_MATERIAL_MAP_FLAG_EMISSION:
 *
 * `mapFlags` bit: a real emission map is bound (not the white fallback).
 */
#define LRG_MATERIAL_MAP_FLAG_EMISSION (16)

/**
 * LRG_MATERIAL_MAP_FLAG_UNLIT:
 *
 * `mapFlags` bit: skip lighting (albedo * colour + emission; fog applies).
 */
#define LRG_MATERIAL_MAP_FLAG_UNLIT (32)

/**
 * LRG_MATERIAL_DEFAULT_METALLIC:
 *
 * Metallic factor of a material that does not set one (and of draws
 * without a material, see lrg_material_reset_uniforms()).
 */
#define LRG_MATERIAL_DEFAULT_METALLIC (0.0f)

/**
 * LRG_MATERIAL_DEFAULT_ROUGHNESS:
 *
 * Roughness factor of a material that does not set one.
 */
#define LRG_MATERIAL_DEFAULT_ROUGHNESS (0.5f)

/**
 * LRG_MATERIAL_DEFAULT_OCCLUSION_STRENGTH:
 *
 * Occlusion strength of a material that does not set one.
 */
#define LRG_MATERIAL_DEFAULT_OCCLUSION_STRENGTH (1.0f)

/**
 * LRG_MATERIAL_DEFAULT_NORMAL_SCALE:
 *
 * Normal map scale of a material that does not set one.
 */
#define LRG_MATERIAL_DEFAULT_NORMAL_SCALE (1.0f)

/**
 * LRG_MATERIAL_DEFAULT_ALPHA_CUTOFF:
 *
 * Alpha cutoff of a %LRG_MATERIAL_BLEND_CUTOUT material that does not
 * set one.
 */
#define LRG_MATERIAL_DEFAULT_ALPHA_CUTOFF (0.5f)

/**
 * LRG_MATERIAL_DEFAULT_HEIGHT_SCALE:
 *
 * Parallax height scale of a material that does not set one.
 */
#define LRG_MATERIAL_DEFAULT_HEIGHT_SCALE (0.05f)

/* ==========================================================================
 * Enumerations
 * ========================================================================== */

/**
 * LrgShadingModel:
 * @LRG_SHADING_MODEL_LIT: the engine's lit metallic-roughness shader
 * @LRG_SHADING_MODEL_UNLIT: albedo times colour plus emission, no lighting
 *   (sets %LRG_MATERIAL_MAP_FLAG_UNLIT)
 * @LRG_SHADING_MODEL_CUSTOM: a user shader asset named by
 *   #LrgMaterial:shader (`.rshader`, by path or `guid:`), fed the custom
 *   uniform table
 *
 * How a material is shaded.
 */
typedef enum
{
	LRG_SHADING_MODEL_LIT,
	LRG_SHADING_MODEL_UNLIT,
	LRG_SHADING_MODEL_CUSTOM
} LrgShadingModel;

/**
 * lrg_shading_model_get_type:
 *
 * Returns: the #GType of #LrgShadingModel
 */
LRG_AVAILABLE_IN_ALL
GType lrg_shading_model_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SHADING_MODEL (lrg_shading_model_get_type ())

/**
 * LrgMaterialBlendMode:
 * @LRG_MATERIAL_BLEND_OPAQUE: no blending, depth written
 * @LRG_MATERIAL_BLEND_CUTOUT: alpha tested against #LrgMaterial:alpha-cutoff
 * @LRG_MATERIAL_BLEND_ALPHA: alpha blended (sorted back to front)
 * @LRG_MATERIAL_BLEND_ADDITIVE: added to the frame (glows, fire)
 * @LRG_MATERIAL_BLEND_MULTIPLY: multiplied with the frame (stains, tint glass)
 *
 * How a material's fragments combine with the frame.
 */
typedef enum
{
	LRG_MATERIAL_BLEND_OPAQUE,
	LRG_MATERIAL_BLEND_CUTOUT,
	LRG_MATERIAL_BLEND_ALPHA,
	LRG_MATERIAL_BLEND_ADDITIVE,
	LRG_MATERIAL_BLEND_MULTIPLY
} LrgMaterialBlendMode;

/**
 * lrg_material_blend_mode_get_type:
 *
 * Returns: the #GType of #LrgMaterialBlendMode
 */
LRG_AVAILABLE_IN_ALL
GType lrg_material_blend_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MATERIAL_BLEND_MODE (lrg_material_blend_mode_get_type ())

/**
 * LrgMaterialCullMode:
 * @LRG_MATERIAL_CULL_BACK: cull back faces (default)
 * @LRG_MATERIAL_CULL_FRONT: cull front faces
 * @LRG_MATERIAL_CULL_NONE: draw both sides
 *
 * Face culling of a material. #LrgMaterial:double-sided forces
 * %LRG_MATERIAL_CULL_NONE.
 */
typedef enum
{
	LRG_MATERIAL_CULL_BACK,
	LRG_MATERIAL_CULL_FRONT,
	LRG_MATERIAL_CULL_NONE
} LrgMaterialCullMode;

/**
 * lrg_material_cull_mode_get_type:
 *
 * Returns: the #GType of #LrgMaterialCullMode
 */
LRG_AVAILABLE_IN_ALL
GType lrg_material_cull_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MATERIAL_CULL_MODE (lrg_material_cull_mode_get_type ())

/**
 * LrgMaterialRenderQueue:
 * @LRG_MATERIAL_RENDER_QUEUE_AUTO: derived from the blend mode (opaque ->
 *   opaque, cutout -> cutout, everything else -> transparent)
 * @LRG_MATERIAL_RENDER_QUEUE_OPAQUE: the opaque list (front to back)
 * @LRG_MATERIAL_RENDER_QUEUE_CUTOUT: the alpha-tested list
 * @LRG_MATERIAL_RENDER_QUEUE_TRANSPARENT: the transparent list (back to
 *   front)
 *
 * Which #LrgWorld3D render list a material's items go to.
 */
typedef enum
{
	LRG_MATERIAL_RENDER_QUEUE_AUTO,
	LRG_MATERIAL_RENDER_QUEUE_OPAQUE,
	LRG_MATERIAL_RENDER_QUEUE_CUTOUT,
	LRG_MATERIAL_RENDER_QUEUE_TRANSPARENT
} LrgMaterialRenderQueue;

/**
 * lrg_material_render_queue_get_type:
 *
 * Returns: the #GType of #LrgMaterialRenderQueue
 */
LRG_AVAILABLE_IN_ALL
GType lrg_material_render_queue_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MATERIAL_RENDER_QUEUE (lrg_material_render_queue_get_type ())

/**
 * LrgMaterialSlot:
 * @LRG_MATERIAL_SLOT_ALBEDO: base colour map (`textures.albedo`)
 * @LRG_MATERIAL_SLOT_NORMAL: tangent-space normal map (`textures.normal`)
 * @LRG_MATERIAL_SLOT_ORM: occlusion/roughness/metallic map (`textures.orm`)
 * @LRG_MATERIAL_SLOT_EMISSION: emission map (`textures.emission`)
 * @LRG_MATERIAL_SLOT_HEIGHT: parallax height map (`textures.height`)
 * @LRG_MATERIAL_SLOT_METALLIC_GLOSS: legacy metallic (R) / smoothness (A)
 *   map (`textures.metallic_gloss`)
 * @LRG_MATERIAL_N_SLOTS: number of slots
 *
 * The texture slots of a material.
 */
typedef enum
{
	LRG_MATERIAL_SLOT_ALBEDO,
	LRG_MATERIAL_SLOT_NORMAL,
	LRG_MATERIAL_SLOT_ORM,
	LRG_MATERIAL_SLOT_EMISSION,
	LRG_MATERIAL_SLOT_HEIGHT,
	LRG_MATERIAL_SLOT_METALLIC_GLOSS,
	LRG_MATERIAL_N_SLOTS
} LrgMaterialSlot;

/**
 * lrg_material_slot_get_type:
 *
 * Returns: the #GType of #LrgMaterialSlot
 */
LRG_AVAILABLE_IN_ALL
GType lrg_material_slot_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MATERIAL_SLOT (lrg_material_slot_get_type ())

/**
 * LrgMaterialUniformType:
 * @LRG_MATERIAL_UNIFORM_FLOAT: one float
 * @LRG_MATERIAL_UNIFORM_VEC2: two floats
 * @LRG_MATERIAL_UNIFORM_VEC3: three floats
 * @LRG_MATERIAL_UNIFORM_VEC4: four floats
 * @LRG_MATERIAL_UNIFORM_COLOR: an RGBA colour, 0 .. 1, uploaded as a vec4
 * @LRG_MATERIAL_UNIFORM_INT: one integer (stored as a float)
 * @LRG_MATERIAL_UNIFORM_TEXTURE: a sampler2D, the value is a texture
 *   reference (path or `guid:`)
 *
 * The type of a custom uniform.
 */
typedef enum
{
	LRG_MATERIAL_UNIFORM_FLOAT,
	LRG_MATERIAL_UNIFORM_VEC2,
	LRG_MATERIAL_UNIFORM_VEC3,
	LRG_MATERIAL_UNIFORM_VEC4,
	LRG_MATERIAL_UNIFORM_COLOR,
	LRG_MATERIAL_UNIFORM_INT,
	LRG_MATERIAL_UNIFORM_TEXTURE
} LrgMaterialUniformType;

/**
 * lrg_material_uniform_type_get_type:
 *
 * Returns: the #GType of #LrgMaterialUniformType
 */
LRG_AVAILABLE_IN_ALL
GType lrg_material_uniform_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MATERIAL_UNIFORM_TYPE (lrg_material_uniform_type_get_type ())

/**
 * LrgMaterialError:
 * @LRG_MATERIAL_ERROR_PARSE: the YAML is malformed or not a mapping
 * @LRG_MATERIAL_ERROR_INVALID: a value has the wrong type or range
 * @LRG_MATERIAL_ERROR_VERSION: the file's version is newer than this
 *   library understands
 * @LRG_MATERIAL_ERROR_NOT_FOUND: a parent or texture reference does not
 *   resolve
 * @LRG_MATERIAL_ERROR_CYCLE: the parent chain loops
 *
 * Errors of the material loader.
 */
typedef enum
{
	LRG_MATERIAL_ERROR_PARSE,
	LRG_MATERIAL_ERROR_INVALID,
	LRG_MATERIAL_ERROR_VERSION,
	LRG_MATERIAL_ERROR_NOT_FOUND,
	LRG_MATERIAL_ERROR_CYCLE
} LrgMaterialError;

/**
 * lrg_material_error_get_type:
 *
 * Returns: the #GType of #LrgMaterialError
 */
LRG_AVAILABLE_IN_ALL
GType lrg_material_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MATERIAL_ERROR (lrg_material_error_get_type ())

/**
 * LRG_MATERIAL_ERROR:
 *
 * Error domain of the material loader.
 */
#define LRG_MATERIAL_ERROR (lrg_material_error_quark ())

/**
 * lrg_material_error_quark:
 *
 * Returns: the error quark of #LrgMaterialError
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_material_error_quark (void);

/* ==========================================================================
 * Type
 * ========================================================================== */

#define LRG_TYPE_MATERIAL (lrg_material_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMaterial, lrg_material, LRG, MATERIAL, GObject)

/* ==========================================================================
 * Construction, files
 * ========================================================================== */

/**
 * lrg_material_new:
 *
 * Creates a lit, opaque material with every value at its default: white
 * base colour, metallic 0, roughness 0.5, occlusion strength 1, normal
 * scale 1, no emission, no textures, back-face culling, depth test and
 * write on.
 *
 * Returns: (transfer full): a new #LrgMaterial
 */
LRG_AVAILABLE_IN_ALL
LrgMaterial * lrg_material_new (void);

/**
 * lrg_material_new_instance:
 * @parent: the material to inherit from
 *
 * Creates an instance of @parent: a material with nothing set, so every
 * value reads through to @parent (and its parents) until it is
 * overridden. The instance keeps a reference on @parent, re-emits
 * #LrgMaterial::changed when the parent changes, and copies its asset
 * manager and base directory.
 *
 * Returns: (transfer full): a new #LrgMaterial
 */
LRG_AVAILABLE_IN_ALL
LrgMaterial * lrg_material_new_instance (LrgMaterial *parent);

/**
 * lrg_material_new_from_file:
 * @path: (type filename): a `.rmat` file
 * @manager: (nullable): asset manager used to resolve `guid:` and
 *   search-path references (parent, textures, shader); also used to load
 *   the parent through lrg_asset_manager_load_material() so instances
 *   share it
 * @error: (nullable): return location for an error
 *
 * Loads a material. Without @manager, references resolve relative to the
 * directory of @path and `guid:` references fail with
 * %LRG_MATERIAL_ERROR_NOT_FOUND. Textures are not loaded here (no
 * graphics context needed): they load on first use or with
 * lrg_material_load_textures().
 *
 * Returns: (transfer full) (nullable): the material, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgMaterial * lrg_material_new_from_file (const gchar      *path,
                                          LrgAssetManager  *manager,
                                          GError          **error);

/**
 * lrg_material_new_from_data:
 * @data: `.rmat` YAML text
 * @length: length of @data, or -1 when NUL-terminated
 * @base_dir: (nullable) (type filename): directory relative references
 *   resolve against
 * @manager: (nullable): asset manager for references (see
 *   lrg_material_new_from_file())
 * @error: (nullable): return location for an error
 *
 * Parses a material from memory.
 *
 * Returns: (transfer full) (nullable): the material, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgMaterial * lrg_material_new_from_data (const gchar      *data,
                                          gssize            length,
                                          const gchar      *base_dir,
                                          LrgAssetManager  *manager,
                                          GError          **error);

/**
 * lrg_material_save_to_data:
 * @self: an #LrgMaterial
 * @length: (out) (optional): length of the result
 *
 * Serializes the values this material sets itself (an instance writes
 * only its overrides plus `parent:`), its custom uniforms (sorted by
 * name) and any unknown top-level keys it was loaded with.
 *
 * Returns: (transfer full): the `.rmat` YAML text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_material_save_to_data (LrgMaterial *self,
                                   gsize       *length);

/**
 * lrg_material_save_to_file:
 * @self: an #LrgMaterial
 * @path: (type filename): the file to write
 * @error: (nullable): return location for an error
 *
 * Writes lrg_material_save_to_data() to @path and remembers @path as
 * #LrgMaterial:path.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_material_save_to_file (LrgMaterial  *self,
                                    const gchar  *path,
                                    GError      **error);

/**
 * lrg_material_copy:
 * @self: an #LrgMaterial
 *
 * Copies the values @self sets (not the inherited ones), its parent link,
 * custom uniforms, runtime textures, unknown keys, name, asset manager
 * and base directory.
 *
 * Returns: (transfer full): a new #LrgMaterial
 */
LRG_AVAILABLE_IN_ALL
LrgMaterial * lrg_material_copy (LrgMaterial *self);

/**
 * lrg_material_get_path:
 * @self: an #LrgMaterial
 *
 * Returns: (transfer none) (nullable) (type filename): the file the
 *   material was loaded from or last saved to
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_material_get_path (LrgMaterial *self);

/**
 * lrg_material_get_name:
 * @self: an #LrgMaterial
 *
 * Returns: (transfer none) (nullable): the display name (`name:`)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_material_get_name (LrgMaterial *self);

/**
 * lrg_material_set_name:
 * @self: an #LrgMaterial
 * @name: (nullable): a display name
 *
 * Sets #LrgMaterial:name. Names are not inherited.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_name (LrgMaterial *self,
                            const gchar *name);

/* ==========================================================================
 * Reference resolution
 * ========================================================================== */

/**
 * lrg_material_get_asset_manager:
 * @self: an #LrgMaterial
 *
 * Returns: (transfer none) (nullable): the asset manager references
 *   resolve through (held weakly)
 */
LRG_AVAILABLE_IN_ALL
LrgAssetManager * lrg_material_get_asset_manager (LrgMaterial *self);

/**
 * lrg_material_set_asset_manager:
 * @self: an #LrgMaterial
 * @manager: (nullable): an asset manager
 *
 * Sets the asset manager texture and parent references resolve through.
 * Textures then load with lrg_asset_manager_load_texture(), so the
 * `.meta` import settings (mipmaps, filter, wrap, max size) apply. The
 * manager is held weakly (it caches materials itself). Already loaded
 * textures are dropped and load again on next use.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_asset_manager (LrgMaterial     *self,
                                     LrgAssetManager *manager);

/**
 * lrg_material_get_base_dir:
 * @self: an #LrgMaterial
 *
 * Returns: (transfer none) (nullable) (type filename): the directory
 *   relative references resolve against without an asset manager
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_material_get_base_dir (LrgMaterial *self);

/**
 * lrg_material_set_base_dir:
 * @self: an #LrgMaterial
 * @base_dir: (nullable) (type filename): a directory
 *
 * Sets the fallback resolver's directory: without an asset manager, a
 * relative reference `textures/a.png` resolves to `@base_dir/textures/a.png`
 * (the current directory when %NULL). Loading from a file sets it to the
 * file's directory.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_base_dir (LrgMaterial *self,
                                const gchar *base_dir);

/**
 * lrg_material_resolve_reference:
 * @self: an #LrgMaterial
 * @ref: a reference: `guid:<uuid>`, a name relative to a search path or
 *   the base directory, or an absolute path
 *
 * Resolves @ref the way the material resolves its parent and textures:
 * through the asset manager when one is set
 * (lrg_asset_manager_resolve_reference()), otherwise against the base
 * directory (absolute paths as they are; `guid:` cannot resolve).
 *
 * Returns: (transfer full) (nullable) (type filename): the path, or %NULL
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_material_resolve_reference (LrgMaterial *self,
                                        const gchar *ref);

/* ==========================================================================
 * Instances
 * ========================================================================== */

/**
 * lrg_material_get_parent:
 * @self: an #LrgMaterial
 *
 * Returns: (transfer none) (nullable): the material unset values fall
 *   through to
 */
LRG_AVAILABLE_IN_ALL
LrgMaterial * lrg_material_get_parent (LrgMaterial *self);

/**
 * lrg_material_set_parent:
 * @self: an #LrgMaterial
 * @parent: (nullable): the new parent, %NULL for none
 *
 * Re-parents @self (a reference is kept). A parent that would make the
 * chain loop is refused (critical warning). The parent reference written
 * on save becomes @parent's guid (when the asset manager knows its file)
 * or its path; set it explicitly with lrg_material_set_parent_ref().
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_parent (LrgMaterial *self,
                              LrgMaterial *parent);

/**
 * lrg_material_get_parent_ref:
 * @self: an #LrgMaterial
 *
 * Returns: (transfer none) (nullable): the `parent:` reference (path or
 *   `guid:`) written on save
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_material_get_parent_ref (LrgMaterial *self);

/**
 * lrg_material_set_parent_ref:
 * @self: an #LrgMaterial
 * @ref: (nullable): a path or `guid:` reference
 * @error: (nullable): return location for an error
 *
 * Resolves and loads @ref (through the asset manager's cache when there
 * is one) and makes it the parent; %NULL clears the parent.
 *
 * Returns: %TRUE when the parent was loaded (or cleared)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_material_set_parent_ref (LrgMaterial  *self,
                                      const gchar  *ref,
                                      GError      **error);

/**
 * lrg_material_is_overridden:
 * @self: an #LrgMaterial
 * @name: a value key: a `.rmat` key (`base_color`, `metallic`,
 *   `roughness`, `occlusion_strength`, `normal_scale`, `emission_color`,
 *   `emission_strength`, `alpha_cutoff`, `height_scale`, `shading_model`,
 *   `shader`, `blend`, `cull`, `depth_test`, `depth_write`, `double_sided`,
 *   `render_queue`), `textures.<slot>` or `uniforms.<name>`
 *
 * Whether @self sets @name itself rather than inheriting it (from the
 * parent, or the default for a material without one).
 *
 * Returns: %TRUE if @self sets @name
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_material_is_overridden (LrgMaterial *self,
                                     const gchar *name);

/**
 * lrg_material_revert:
 * @self: an #LrgMaterial
 * @name: a value key (see lrg_material_is_overridden())
 *
 * Drops @self's own value of @name so it inherits again. Emits
 * #LrgMaterial::changed when something was dropped.
 *
 * Returns: %TRUE if @name was overridden
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_material_revert (LrgMaterial *self,
                              const gchar *name);

/**
 * lrg_material_list_overrides:
 * @self: an #LrgMaterial
 *
 * Lists the keys @self sets, in `.rmat` order: values, then
 * `textures.<slot>`, then `uniforms.<name>` sorted by name.
 *
 * Returns: (transfer full) (array zero-terminated=1): the keys
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_material_list_overrides (LrgMaterial *self);

/* ==========================================================================
 * Shading model and parameters (effective values: own, else inherited)
 * ========================================================================== */

/**
 * lrg_material_get_shading_model:
 * @self: an #LrgMaterial
 *
 * Returns: the effective shading model
 */
LRG_AVAILABLE_IN_ALL
LrgShadingModel lrg_material_get_shading_model (LrgMaterial *self);

/**
 * lrg_material_set_shading_model:
 * @self: an #LrgMaterial
 * @model: the shading model
 *
 * Sets #LrgMaterial:shading-model.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_shading_model (LrgMaterial     *self,
                                     LrgShadingModel  model);

/**
 * lrg_material_get_shader:
 * @self: an #LrgMaterial
 *
 * Returns: (transfer none) (nullable): the effective custom shader asset
 *   reference (`.rshader` path or `guid:`)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_material_get_shader (LrgMaterial *self);

/**
 * lrg_material_set_shader:
 * @self: an #LrgMaterial
 * @shader_ref: (nullable): a `.rshader` path or `guid:` reference
 *
 * Sets #LrgMaterial:shader, the user shader of a
 * %LRG_SHADING_MODEL_CUSTOM material. LrgMaterial only stores the
 * reference; the shader asset API compiles it and feeds it the custom
 * uniforms.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_shader (LrgMaterial *self,
                              const gchar *shader_ref);

/**
 * lrg_material_get_base_color:
 * @self: an #LrgMaterial
 * @out: (out caller-allocates) (array fixed-size=4): RGBA, 0 .. 1, sRGB
 *
 * Gets the effective base colour (`colDiffuse`).
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_get_base_color (LrgMaterial *self,
                                  gfloat      *out);

/**
 * lrg_material_set_base_color:
 * @self: an #LrgMaterial
 * @r: red, 0 .. 1
 * @g: green, 0 .. 1
 * @b: blue, 0 .. 1
 * @a: alpha, 0 .. 1
 *
 * Sets the base colour in sRGB (display) space, like a colour picker
 * shows it; the shader linearizes it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_base_color (LrgMaterial *self,
                                  gfloat       r,
                                  gfloat       g,
                                  gfloat       b,
                                  gfloat       a);

/**
 * lrg_material_get_metallic:
 * @self: an #LrgMaterial
 *
 * Returns: the effective metallic factor, 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_material_get_metallic (LrgMaterial *self);

/**
 * lrg_material_set_metallic:
 * @self: an #LrgMaterial
 * @metallic: 0 (dielectric) .. 1 (metal); clamped
 *
 * Sets #LrgMaterial:metallic (multiplied by the ORM map's blue channel).
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_metallic (LrgMaterial *self,
                                gfloat       metallic);

/**
 * lrg_material_get_roughness:
 * @self: an #LrgMaterial
 *
 * Returns: the effective roughness factor, 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_material_get_roughness (LrgMaterial *self);

/**
 * lrg_material_set_roughness:
 * @self: an #LrgMaterial
 * @roughness: 0 (mirror) .. 1 (fully rough); clamped
 *
 * Sets #LrgMaterial:roughness (multiplied by the ORM map's green channel).
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_roughness (LrgMaterial *self,
                                 gfloat       roughness);

/**
 * lrg_material_get_occlusion_strength:
 * @self: an #LrgMaterial
 *
 * Returns: the effective occlusion strength, 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_material_get_occlusion_strength (LrgMaterial *self);

/**
 * lrg_material_set_occlusion_strength:
 * @self: an #LrgMaterial
 * @strength: 0 (ignore the ORM red channel) .. 1 (full); clamped
 *
 * Sets #LrgMaterial:occlusion-strength.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_occlusion_strength (LrgMaterial *self,
                                          gfloat       strength);

/**
 * lrg_material_get_normal_scale:
 * @self: an #LrgMaterial
 *
 * Returns: the effective normal map scale
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_material_get_normal_scale (LrgMaterial *self);

/**
 * lrg_material_set_normal_scale:
 * @self: an #LrgMaterial
 * @scale: scale of the normal map's XY (glTF normalTexture.scale), 0 .. 8
 *
 * Sets #LrgMaterial:normal-scale.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_normal_scale (LrgMaterial *self,
                                    gfloat       scale);

/**
 * lrg_material_get_emission_color:
 * @self: an #LrgMaterial
 * @out: (out caller-allocates) (array fixed-size=3): RGB, 0 .. 1, sRGB
 *
 * Gets the effective emission colour.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_get_emission_color (LrgMaterial *self,
                                      gfloat      *out);

/**
 * lrg_material_set_emission_color:
 * @self: an #LrgMaterial
 * @r: red, 0 .. 1
 * @g: green, 0 .. 1
 * @b: blue, 0 .. 1
 *
 * Sets the emission colour (times #LrgMaterial:emission-strength, times
 * the emission map when there is one).
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_emission_color (LrgMaterial *self,
                                      gfloat       r,
                                      gfloat       g,
                                      gfloat       b);

/**
 * lrg_material_get_emission_strength:
 * @self: an #LrgMaterial
 *
 * Returns: the effective emission strength (0 = no glow)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_material_get_emission_strength (LrgMaterial *self);

/**
 * lrg_material_set_emission_strength:
 * @self: an #LrgMaterial
 * @strength: emission multiplier, 0 or more
 *
 * Sets #LrgMaterial:emission-strength.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_emission_strength (LrgMaterial *self,
                                         gfloat       strength);

/**
 * lrg_material_get_alpha_cutoff:
 * @self: an #LrgMaterial
 *
 * Returns: the effective alpha cutoff (used by %LRG_MATERIAL_BLEND_CUTOUT)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_material_get_alpha_cutoff (LrgMaterial *self);

/**
 * lrg_material_set_alpha_cutoff:
 * @self: an #LrgMaterial
 * @cutoff: 0 .. 1; fragments with a lower alpha are discarded
 *
 * Sets #LrgMaterial:alpha-cutoff. Only cutout materials upload it
 * (`alphaCutoff`); other blend modes upload 0.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_alpha_cutoff (LrgMaterial *self,
                                    gfloat       cutoff);

/**
 * lrg_material_get_height_scale:
 * @self: an #LrgMaterial
 *
 * Returns: the effective parallax height scale
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_material_get_height_scale (LrgMaterial *self);

/**
 * lrg_material_set_height_scale:
 * @self: an #LrgMaterial
 * @scale: parallax depth in UV units, 0 .. 1
 *
 * Sets #LrgMaterial:height-scale (`heightScale`).
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_height_scale (LrgMaterial *self,
                                    gfloat       scale);

/* ==========================================================================
 * Render state
 * ========================================================================== */

/**
 * lrg_material_get_blend_mode:
 * @self: an #LrgMaterial
 *
 * Returns: the effective blend mode
 */
LRG_AVAILABLE_IN_ALL
LrgMaterialBlendMode lrg_material_get_blend_mode (LrgMaterial *self);

/**
 * lrg_material_set_blend_mode:
 * @self: an #LrgMaterial
 * @mode: the blend mode
 *
 * Sets #LrgMaterial:blend-mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_blend_mode (LrgMaterial          *self,
                                  LrgMaterialBlendMode  mode);

/**
 * lrg_material_get_cull_mode:
 * @self: an #LrgMaterial
 *
 * Returns: the effective cull mode (%LRG_MATERIAL_CULL_NONE when
 *   double-sided)
 */
LRG_AVAILABLE_IN_ALL
LrgMaterialCullMode lrg_material_get_cull_mode (LrgMaterial *self);

/**
 * lrg_material_set_cull_mode:
 * @self: an #LrgMaterial
 * @mode: the cull mode
 *
 * Sets #LrgMaterial:cull-mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_cull_mode (LrgMaterial         *self,
                                 LrgMaterialCullMode  mode);

/**
 * lrg_material_get_depth_test:
 * @self: an #LrgMaterial
 *
 * Returns: whether fragments are depth tested
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_material_get_depth_test (LrgMaterial *self);

/**
 * lrg_material_set_depth_test:
 * @self: an #LrgMaterial
 * @depth_test: whether to depth test
 *
 * Sets #LrgMaterial:depth-test.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_depth_test (LrgMaterial *self,
                                  gboolean     depth_test);

/**
 * lrg_material_get_depth_write:
 * @self: an #LrgMaterial
 *
 * Returns: whether fragments write depth
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_material_get_depth_write (LrgMaterial *self);

/**
 * lrg_material_set_depth_write:
 * @self: an #LrgMaterial
 * @depth_write: whether to write depth
 *
 * Sets #LrgMaterial:depth-write.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_depth_write (LrgMaterial *self,
                                   gboolean     depth_write);

/**
 * lrg_material_get_double_sided:
 * @self: an #LrgMaterial
 *
 * Returns: whether both faces are drawn
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_material_get_double_sided (LrgMaterial *self);

/**
 * lrg_material_set_double_sided:
 * @self: an #LrgMaterial
 * @double_sided: whether to draw both faces
 *
 * Sets #LrgMaterial:double-sided (culling off).
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_double_sided (LrgMaterial *self,
                                    gboolean     double_sided);

/**
 * lrg_material_get_render_queue:
 * @self: an #LrgMaterial
 *
 * Returns: the effective render queue as set (may be
 *   %LRG_MATERIAL_RENDER_QUEUE_AUTO)
 */
LRG_AVAILABLE_IN_ALL
LrgMaterialRenderQueue lrg_material_get_render_queue (LrgMaterial *self);

/**
 * lrg_material_set_render_queue:
 * @self: an #LrgMaterial
 * @queue: the queue hint
 *
 * Sets #LrgMaterial:render-queue.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_render_queue (LrgMaterial            *self,
                                    LrgMaterialRenderQueue  queue);

/**
 * lrg_material_resolve_render_queue:
 * @self: an #LrgMaterial
 *
 * The queue items with this material belong to: the render queue, or
 * for %LRG_MATERIAL_RENDER_QUEUE_AUTO the one the blend mode implies.
 *
 * Returns: opaque, cutout or transparent (never auto)
 */
LRG_AVAILABLE_IN_ALL
LrgMaterialRenderQueue lrg_material_resolve_render_queue (LrgMaterial *self);

/* ==========================================================================
 * Textures
 * ========================================================================== */

/**
 * lrg_material_get_texture_ref:
 * @self: an #LrgMaterial
 * @slot: a texture slot
 *
 * Returns: (transfer none) (nullable): the effective reference (path or
 *   `guid:`) of @slot
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_material_get_texture_ref (LrgMaterial     *self,
                                            LrgMaterialSlot  slot);

/**
 * lrg_material_set_texture_ref:
 * @self: an #LrgMaterial
 * @slot: a texture slot
 * @ref: (nullable): a texture path or `guid:` reference; %NULL sets the
 *   slot to "no texture" (an override that hides the parent's map; use
 *   lrg_material_revert() to inherit again)
 *
 * Sets the texture of @slot by reference. The texture loads on first
 * use.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_texture_ref (LrgMaterial     *self,
                                   LrgMaterialSlot  slot,
                                   const gchar     *ref);

/**
 * lrg_material_set_texture:
 * @self: an #LrgMaterial
 * @slot: a texture slot
 * @texture: (nullable): a loaded texture (a reference is kept), %NULL to
 *   drop the runtime texture
 *
 * Gives @slot a runtime texture (render targets, procedural maps). It
 * wins over the slot's reference and is not saved.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_texture (LrgMaterial     *self,
                               LrgMaterialSlot  slot,
                               GrlTexture      *texture);

/**
 * lrg_material_get_texture:
 * @self: an #LrgMaterial
 * @slot: a texture slot
 *
 * The texture @slot draws with: the runtime texture, otherwise the
 * effective reference loaded (once; needs a graphics context, a failed
 * load is not retried until the reference changes) through the asset
 * manager or from the resolved file.
 *
 * Returns: (transfer none) (nullable): the texture, or %NULL
 */
LRG_AVAILABLE_IN_ALL
GrlTexture * lrg_material_get_texture (LrgMaterial     *self,
                                       LrgMaterialSlot  slot);

/**
 * lrg_material_load_textures:
 * @self: an #LrgMaterial
 * @error: (nullable): return location for the first error
 *
 * Loads every referenced texture now (slots and texture uniforms,
 * including inherited ones) instead of at the first draw.
 *
 * Returns: %TRUE when every reference loaded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_material_load_textures (LrgMaterial  *self,
                                     GError      **error);

/* ==========================================================================
 * Custom uniforms
 * ========================================================================== */

/**
 * lrg_material_set_uniform:
 * @self: an #LrgMaterial
 * @name: the GLSL uniform name
 * @type: a value type (not %LRG_MATERIAL_UNIFORM_TEXTURE)
 * @values: (array length=n_values): the components
 * @n_values: number of components (1 .. 4; missing ones are 0)
 *
 * Sets a custom uniform uploaded by lrg_material_apply() (any shading
 * model; a shader without the uniform ignores it).
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_uniform (LrgMaterial            *self,
                               const gchar            *name,
                               LrgMaterialUniformType  type,
                               const gfloat           *values,
                               guint                   n_values);

/**
 * lrg_material_set_uniform_float:
 * @self: an #LrgMaterial
 * @name: the GLSL uniform name
 * @value: the value
 *
 * Shorthand for a %LRG_MATERIAL_UNIFORM_FLOAT uniform.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_uniform_float (LrgMaterial *self,
                                     const gchar *name,
                                     gfloat       value);

/**
 * lrg_material_set_uniform_texture:
 * @self: an #LrgMaterial
 * @name: the GLSL sampler2D name
 * @ref: a texture path or `guid:` reference
 *
 * Sets a texture uniform. At apply time it keeps the unit the program's
 * sampler already points at when that unit is in the custom budget (see
 * %LRG_MATERIAL_CUSTOM_TEXTURE_UNIT), otherwise it takes the next free
 * budget unit in name order.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_set_uniform_texture (LrgMaterial *self,
                                       const gchar *name,
                                       const gchar *ref);

/**
 * lrg_material_get_uniform:
 * @self: an #LrgMaterial
 * @name: the uniform name
 * @type: (out) (optional): the uniform type
 * @values: (out caller-allocates) (array fixed-size=4) (optional): the
 *   components (zeros for a texture)
 *
 * Looks up the effective custom uniform @name (own, else inherited).
 *
 * Returns: %TRUE if the uniform exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_material_get_uniform (LrgMaterial            *self,
                                   const gchar            *name,
                                   LrgMaterialUniformType *type,
                                   gfloat                 *values);

/**
 * lrg_material_get_uniform_texture_ref:
 * @self: an #LrgMaterial
 * @name: the uniform name
 *
 * Returns: (transfer none) (nullable): the texture reference of a texture
 *   uniform
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_material_get_uniform_texture_ref (LrgMaterial *self,
                                                    const gchar *name);

/**
 * lrg_material_list_uniforms:
 * @self: an #LrgMaterial
 *
 * Lists the effective custom uniform names (own and inherited), sorted.
 *
 * Returns: (transfer full) (array zero-terminated=1): the names
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_material_list_uniforms (LrgMaterial *self);

/* ==========================================================================
 * Binding (the lit shader contract)
 * ========================================================================== */

/**
 * lrg_material_get_map_flags:
 * @self: an #LrgMaterial
 *
 * The `mapFlags` lrg_material_apply() uploads, from the texture slots
 * that have a texture now (headless: only runtime textures and already
 * loaded ones count) and the shading model.
 *
 * Returns: a combination of the LRG_MATERIAL_MAP_FLAG_* bits
 */
LRG_AVAILABLE_IN_ALL
gint lrg_material_get_map_flags (LrgMaterial *self);

/**
 * lrg_material_get_pbr_params:
 * @self: an #LrgMaterial
 * @out: (out caller-allocates) (array fixed-size=4): metallic, roughness,
 *   occlusion strength, normal scale
 *
 * The `pbrParams` lrg_material_apply() uploads. Headless.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_get_pbr_params (LrgMaterial *self,
                                  gfloat      *out);

/**
 * lrg_material_get_emission_own:
 * @self: an #LrgMaterial
 * @out: (out caller-allocates) (array fixed-size=4): emission colour times
 *   strength (sRGB), then 1
 *
 * The `emissionOwn` lrg_material_apply() uploads. Headless.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_get_emission_own (LrgMaterial *self,
                                    gfloat      *out);

/**
 * lrg_material_apply:
 * @self: an #LrgMaterial
 * @shader_id: the GL program to bind into (for example the lit shader of
 *   lrg_light_set_get_shader())
 *
 * Uploads the material uniforms of the lit shader contract to
 * @shader_id and binds the textures (needs a graphics context; leaves
 * @shader_id enabled):
 *
 * - textures: albedo -> unit 0 (`texture0`, white when unset), legacy
 *   metallic/gloss -> 1 (`metallicMap`), normal -> 2 (`normalMap`), ORM ->
 *   4 (`ormMap`), emission -> 5 (`emissionMap`, a 1x1 white texture when
 *   unset), height -> 6 (`heightMap`); custom texture uniforms from
 *   %LRG_MATERIAL_CUSTOM_TEXTURE_UNIT; sampler uniforms present in the
 *   program are pointed at those units
 * - `colDiffuse` vec4: the base colour (sRGB)
 * - `pbrParams` vec4: lrg_material_get_pbr_params()
 * - `mapFlags` int: lrg_material_get_map_flags()
 * - `emissionOwn` vec4: lrg_material_get_emission_own() (`emissionColor`,
 *   the light set's global tint, is not touched)
 * - `alphaCutoff` float: the cutoff for cutout materials, else 0
 * - `heightScale` float: the height scale
 * - the custom uniforms (lrg_material_apply_custom_uniforms())
 *
 * Uniforms the program lacks are skipped. Locations are looked up by name
 * once per program. A raylib DrawMesh() afterwards rebinds the maps of
 * its own raylib Material, so give it the same maps with
 * lrg_material_fill_raylib_maps() (the engine's batch, terrain and
 * skinned mesh do this). After drawing, call lrg_material_reset_uniforms()
 * before draws without a material.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_apply (LrgMaterial *self,
                         guint        shader_id);

/**
 * lrg_material_apply_custom_uniforms:
 * @self: an #LrgMaterial
 * @shader_id: the GL program
 * @texture_unit: (inout): the next texture unit to try for samplers the
 *   program does not already point at a budget unit; advanced past the
 *   units taken
 *
 * Uploads only the custom uniform table. Texture uniforms follow the
 * budget of %LRG_MATERIAL_CUSTOM_TEXTURE_UNIT: a sampler already set to
 * an allowed unit (lrg_shader_asset_apply() assigns its samplers from
 * 7) is rebound in place; others take allowed units from @texture_unit
 * up. When the budget is used up the sampler is not bound and a warning
 * names it. Needs a graphics context.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_apply_custom_uniforms (LrgMaterial *self,
                                         guint        shader_id,
                                         gint        *texture_unit);

/**
 * lrg_material_reset_uniforms:
 * @shader_id: the GL program
 *
 * Restores the "no material" uniforms of the lit shader on @shader_id:
 * `pbrParams` = (0, 0.5, 1, 1), `mapFlags` = 0, `emissionOwn` = (0, 0, 0,
 * 0) (back to the light set's global emission) and `alphaCutoff` = 0,
 * and unbinds the textures of units 1, 2, 4, 5 and 6 (an empty unit reads
 * black; the lit shader always samples `emissionMap`). Needs a graphics
 * context.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_reset_uniforms (guint shader_id);

/**
 * lrg_material_apply_state:
 * @self: an #LrgMaterial
 *
 * Sets the rlgl render state of @self: blend mode (opaque and cutout use
 * alpha blending with opaque alpha, alpha/additive/multiply map to
 * raylib's BLEND_ALPHA/ADDITIVE/MULTIPLIED), face culling, depth test and
 * depth write. Flushes the rlgl batch first. Needs a graphics context.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_apply_state (LrgMaterial *self);

/**
 * lrg_material_restore_state:
 * @self: (nullable): the #LrgMaterial whose state is applied, or %NULL
 *
 * Puts back raylib's 3D-mode defaults: BLEND_ALPHA, back-face culling,
 * depth test and depth write on. Flushes the rlgl batch first. Needs a
 * graphics context.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_restore_state (LrgMaterial *self);

/**
 * lrg_material_fill_raylib_maps: (skip)
 * @self: an #LrgMaterial
 * @maps: a raylib `MaterialMap` array of at least MAX_MATERIAL_MAPS (12)
 *   entries
 *
 * Fills raylib material maps the way lrg_material_apply() binds them
 * (texture ids per unit, the albedo colour as bytes), so a DrawMesh()
 * after lrg_material_apply() binds the same textures. Entries for empty
 * slots are left as they are. C only.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material_fill_raylib_maps (LrgMaterial *self,
                                    gpointer     maps);

G_END_DECLS
