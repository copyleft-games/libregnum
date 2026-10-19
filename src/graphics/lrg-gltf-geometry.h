/* lrg-gltf-geometry.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGltfGeometry - headless reading of glTF 2.0 / GLB static geometry:
 * every triangle primitive of the default scene flattened into scene
 * space (node transforms applied), with normals, texture coordinates,
 * vertex colours and indices, the materials' names, base colours, alpha
 * modes and base colour images. Needs no graphics context, so games can
 * merge glTF props into their own batches, build collision from them, or
 * convert them into their own mesh formats in tests and tools.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_GLTF_GEOMETRY (lrg_gltf_geometry_get_type ())

/**
 * LRG_GLTF_GEOMETRY_MAX_VERTICES:
 *
 * Upper bound on the vertices of one accessor (and the indices of one
 * primitive) the reader accepts, so a corrupt count cannot ask for
 * gigabytes.
 */
#define LRG_GLTF_GEOMETRY_MAX_VERTICES (16777216u)

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGltfGeometry, lrg_gltf_geometry, LRG, GLTF_GEOMETRY, GObject)

/**
 * lrg_gltf_geometry_new_from_file:
 * @path: (type filename): a .glb or .gltf file
 * @error: (nullable): return location for a #GError
 *
 * Reads the static geometry of a glTF file. External buffers and images
 * ("uri" naming a file) are resolved relative to the file's directory;
 * absolute paths and ".." components are refused. "data:" URIs and the
 * GLB binary chunk are read in place.
 *
 * The primitives come from the scene named by "scene" (scene 0 when
 * absent); a file without scenes uses every root node. Each node's mesh
 * is placed with the node's world transform; positions are transformed,
 * normals use the inverse transpose and are renormalized, and the
 * winding of a mirroring transform is reversed so front faces stay front
 * faces. Triangle lists, strips and fans become triangle lists; points
 * and lines are skipped (lrg_gltf_geometry_get_skipped_count()). Skins,
 * morph targets and animations are read as well
 * (lrg_gltf_geometry_pose()); the static getters give the rest pose.
 *
 * Errors: %G_FILE_ERROR when the file cannot be read, otherwise
 * %LRG_GLTF_ERROR (%LRG_GLTF_ERROR_INVALID_DATA for overrunning
 * accessors, %LRG_GLTF_ERROR_UNSUPPORTED for Draco and sparse data).
 *
 * Returns: (transfer full) (nullable): the geometry, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgGltfGeometry *
lrg_gltf_geometry_new_from_file (const gchar  *path,
                                 GError      **error);

/**
 * lrg_gltf_geometry_new_from_bytes:
 * @data: GLB or glTF JSON file contents
 * @base_dir: (type filename) (nullable): directory that external "uri"
 *   files are relative to; %NULL refuses external files with
 *   %LRG_GLTF_ERROR_UNSUPPORTED
 * @error: (nullable): return location for a #GError
 *
 * Same as lrg_gltf_geometry_new_from_file() for in-memory data.
 *
 * Returns: (transfer full) (nullable): the geometry, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgGltfGeometry *
lrg_gltf_geometry_new_from_bytes (GBytes       *data,
                                  const gchar  *base_dir,
                                  GError      **error);

/**
 * lrg_gltf_geometry_get_primitive_count:
 * @self: an #LrgGltfGeometry
 *
 * Returns: the number of triangle primitives placed in the scene (a mesh
 *   used by two nodes counts twice)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gltf_geometry_get_primitive_count (LrgGltfGeometry *self);

/**
 * lrg_gltf_geometry_get_skipped_count:
 * @self: an #LrgGltfGeometry
 *
 * Returns: the number of point and line primitives that were skipped
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gltf_geometry_get_skipped_count (LrgGltfGeometry *self);

/**
 * lrg_gltf_geometry_get_positions:
 * @self: an #LrgGltfGeometry
 * @primitive: primitive index
 * @n_vertices: (out) (optional): vertex count
 *
 * Returns: (transfer none) (array) (nullable): 3 floats per vertex in
 *   scene space, or %NULL when @primitive is out of range
 */
LRG_AVAILABLE_IN_ALL
const gfloat *
lrg_gltf_geometry_get_positions (LrgGltfGeometry *self,
                                 guint            primitive,
                                 guint           *n_vertices);

/**
 * lrg_gltf_geometry_get_normals:
 * @self: an #LrgGltfGeometry
 * @primitive: primitive index
 *
 * Returns: (transfer none) (array) (nullable): 3 floats per vertex, unit
 *   length in scene space, or %NULL when the primitive has no NORMAL
 */
LRG_AVAILABLE_IN_ALL
const gfloat *
lrg_gltf_geometry_get_normals (LrgGltfGeometry *self,
                               guint            primitive);

/**
 * lrg_gltf_geometry_get_texcoords:
 * @self: an #LrgGltfGeometry
 * @primitive: primitive index
 *
 * TEXCOORD_0 as floats (normalized integer data is converted). glTF puts
 * the origin at the image's top-left corner, V growing downwards, the
 * same order raylib uploads images in.
 *
 * Returns: (transfer none) (array) (nullable): 2 floats per vertex, or
 *   %NULL when the primitive has no TEXCOORD_0
 */
LRG_AVAILABLE_IN_ALL
const gfloat *
lrg_gltf_geometry_get_texcoords (LrgGltfGeometry *self,
                                 guint            primitive);

/**
 * lrg_gltf_geometry_get_colors:
 * @self: an #LrgGltfGeometry
 * @primitive: primitive index
 *
 * COLOR_0 as RGBA bytes (RGB data gets alpha 255).
 *
 * Returns: (transfer none) (array) (nullable): 4 bytes per vertex, or
 *   %NULL when the primitive has no COLOR_0
 */
LRG_AVAILABLE_IN_ALL
const guint8 *
lrg_gltf_geometry_get_colors (LrgGltfGeometry *self,
                              guint            primitive);

/**
 * lrg_gltf_geometry_get_indices:
 * @self: an #LrgGltfGeometry
 * @primitive: primitive index
 * @n_indices: (out) (optional): index count, a multiple of 3
 *
 * The primitive as a triangle list, counter-clockwise front faces as
 * glTF defines them. Non-indexed primitives get sequential indices;
 * strips and fans are unrolled; degenerate triangles are kept.
 *
 * Returns: (transfer none) (array) (nullable): the indices, or %NULL
 *   when @primitive is out of range
 */
LRG_AVAILABLE_IN_ALL
const guint32 *
lrg_gltf_geometry_get_indices (LrgGltfGeometry *self,
                               guint            primitive,
                               guint           *n_indices);

/**
 * lrg_gltf_geometry_get_primitive_material:
 * @self: an #LrgGltfGeometry
 * @primitive: primitive index
 *
 * Returns: the glTF material index, or -1 for none (or out of range)
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_gltf_geometry_get_primitive_material (LrgGltfGeometry *self,
                                          guint            primitive);

/**
 * lrg_gltf_geometry_get_primitive_node_name:
 * @self: an #LrgGltfGeometry
 * @primitive: primitive index
 *
 * Returns: (transfer none) (nullable): the name of the node that placed
 *   the primitive, or %NULL when unnamed or out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_gltf_geometry_get_primitive_node_name (LrgGltfGeometry *self,
                                           guint            primitive);

/**
 * lrg_gltf_geometry_get_triangle_count:
 * @self: an #LrgGltfGeometry
 *
 * Returns: triangles over all primitives
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gltf_geometry_get_triangle_count (LrgGltfGeometry *self);

/**
 * lrg_gltf_geometry_get_bounds:
 * @self: an #LrgGltfGeometry
 * @min: (out caller-allocates) (array fixed-size=3): minimum corner
 * @max: (out caller-allocates) (array fixed-size=3): maximum corner
 *
 * Scene-space bounds of every placed vertex.
 *
 * Returns: %FALSE (and zero corners) when there is no vertex
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_gltf_geometry_get_bounds (LrgGltfGeometry *self,
                              gfloat          *min,
                              gfloat          *max);

/**
 * lrg_gltf_geometry_get_material_count:
 * @self: an #LrgGltfGeometry
 *
 * Returns: the number of glTF materials
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gltf_geometry_get_material_count (LrgGltfGeometry *self);

/**
 * lrg_gltf_geometry_get_material_name:
 * @self: an #LrgGltfGeometry
 * @material: material index
 *
 * Returns: (transfer none) (nullable): the material's "name", or %NULL
 *   when unnamed or out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_gltf_geometry_get_material_name (LrgGltfGeometry *self,
                                     guint            material);

/**
 * lrg_gltf_geometry_get_material_color:
 * @self: an #LrgGltfGeometry
 * @material: material index
 * @rgba: (out caller-allocates) (array fixed-size=4): base colour factor
 *
 * The material's pbrMetallicRoughness.baseColorFactor (white when
 * absent).
 *
 * Returns: %FALSE (and white) when @material is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_gltf_geometry_get_material_color (LrgGltfGeometry *self,
                                      guint            material,
                                      gfloat          *rgba);

/**
 * lrg_gltf_geometry_get_material_image:
 * @self: an #LrgGltfGeometry
 * @material: material index
 *
 * The image of the material's base colour texture (the texture's
 * "source").
 *
 * Returns: the image index, or -1 when the material has no base colour
 *   texture (or is out of range)
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_gltf_geometry_get_material_image (LrgGltfGeometry *self,
                                      guint            material);

/**
 * lrg_gltf_geometry_get_material_alpha_mode:
 * @self: an #LrgGltfGeometry
 * @material: material index
 * @cutoff: (out) (optional): "alphaCutoff" (0.5 when absent)
 *
 * Returns: the material's alpha mode (%LRG_GLTF_ALPHA_MODE_OPAQUE when
 *   out of range)
 */
LRG_AVAILABLE_IN_ALL
LrgGltfAlphaMode
lrg_gltf_geometry_get_material_alpha_mode (LrgGltfGeometry *self,
                                           guint            material,
                                           gfloat          *cutoff);

/**
 * lrg_gltf_geometry_get_material_double_sided:
 * @self: an #LrgGltfGeometry
 * @material: material index
 *
 * Returns: the material's "doubleSided" flag
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_gltf_geometry_get_material_double_sided (LrgGltfGeometry *self,
                                             guint            material);

/**
 * lrg_gltf_geometry_get_image_count:
 * @self: an #LrgGltfGeometry
 *
 * Returns: the number of glTF images
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gltf_geometry_get_image_count (LrgGltfGeometry *self);

/**
 * lrg_gltf_geometry_get_image_data:
 * @self: an #LrgGltfGeometry
 * @image: image index
 * @mime_type: (out) (optional) (transfer none) (nullable): the image's
 *   "mimeType", or a type guessed from the data or file name
 *   ("image/png", "image/jpeg"), %NULL when unknown
 * @error: (nullable): return location for a #GError
 *
 * The encoded image file: a buffer view slice, a decoded "data:" URI, or
 * the external file's contents.
 *
 * Returns: (transfer full) (nullable): the encoded bytes, or %NULL on
 *   error
 */
LRG_AVAILABLE_IN_ALL
GBytes *
lrg_gltf_geometry_get_image_data (LrgGltfGeometry  *self,
                                  guint             image,
                                  const gchar     **mime_type,
                                  GError          **error);

/**
 * lrg_gltf_geometry_load_image:
 * @self: an #LrgGltfGeometry
 * @image: image index
 * @error: (nullable): return location for a #GError
 *
 * Decodes the image (PNG, JPEG, or anything else graylib reads) into a
 * CPU #GrlImage; works without a graphics context.
 *
 * Errors: %LRG_GLTF_ERROR_INVALID_DATA when the bytes do not decode.
 *
 * Returns: (transfer full) (nullable): the image, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GrlImage *
lrg_gltf_geometry_load_image (LrgGltfGeometry  *self,
                              guint             image,
                              GError          **error);

/**
 * lrg_gltf_geometry_is_animated:
 * @self: an #LrgGltfGeometry
 *
 * Returns: whether the file has animations, skinned primitives or morph
 *   targets (then lrg_gltf_geometry_pose() and
 *   lrg_gltf_geometry_get_posed() give moving geometry)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_gltf_geometry_is_animated (LrgGltfGeometry *self);

/**
 * lrg_gltf_geometry_get_animation_count:
 * @self: an #LrgGltfGeometry
 *
 * Returns: the number of animations (clips) of the file
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gltf_geometry_get_animation_count (LrgGltfGeometry *self);

/**
 * lrg_gltf_geometry_get_animation_name:
 * @self: an #LrgGltfGeometry
 * @animation: animation index
 *
 * Returns: (transfer none) (nullable): the clip's name, %NULL when
 *   unnamed or out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_gltf_geometry_get_animation_name (LrgGltfGeometry *self,
                                      guint            animation);

/**
 * lrg_gltf_geometry_get_animation_duration:
 * @self: an #LrgGltfGeometry
 * @animation: animation index
 *
 * Returns: the clip's length in seconds (the last key of any sampler),
 *   0 when out of range
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_gltf_geometry_get_animation_duration (LrgGltfGeometry *self,
                                          guint            animation);

/**
 * lrg_gltf_geometry_get_skin_count:
 * @self: an #LrgGltfGeometry
 *
 * Returns: the number of skins of the file
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gltf_geometry_get_skin_count (LrgGltfGeometry *self);

/**
 * lrg_gltf_geometry_get_primitive_skin:
 * @self: an #LrgGltfGeometry
 * @primitive: primitive index
 *
 * Returns: the skin that deforms the primitive, -1 for a rigid one
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_gltf_geometry_get_primitive_skin (LrgGltfGeometry *self,
                                      guint            primitive);

/**
 * lrg_gltf_geometry_get_primitive_morph_target_count:
 * @self: an #LrgGltfGeometry
 * @primitive: primitive index
 *
 * Returns: the primitive's morph targets (blend shapes)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gltf_geometry_get_primitive_morph_target_count (LrgGltfGeometry *self,
                                                    guint            primitive);

/**
 * lrg_gltf_geometry_pose:
 * @self: an #LrgGltfGeometry
 * @animation: animation index, or -1 for the rest pose
 * @time: seconds into the clip
 * @loop: %TRUE to wrap @time into the clip's length (else it is clamped
 *   at the end)
 *
 * Evaluates the file's pose: every channel of the clip (translation,
 * rotation, scale and morph weights; linear, step and cubic spline
 * interpolation, rotations spherically) over the node graph, then the
 * skins' joint matrices.  The result is kept in @self for
 * lrg_gltf_geometry_get_posed(); the object is not safe to pose from two
 * threads at once.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gltf_geometry_pose (LrgGltfGeometry *self,
                        gint             animation,
                        gdouble          time,
                        gboolean         loop);

/**
 * lrg_gltf_geometry_get_posed:
 * @self: an #LrgGltfGeometry
 * @primitive: primitive index
 * @out_positions: (out caller-allocates) (array): 3 floats per vertex
 *   (lrg_gltf_geometry_get_positions()'s vertex count)
 * @out_normals: (out caller-allocates) (nullable) (array): 3 floats per
 *   vertex, written only when the primitive has normals
 *
 * The primitive as the last lrg_gltf_geometry_pose() left it, in scene
 * space: morph targets mixed with their weights, then skinned by the
 * joints (a skinned primitive's own node transform is ignored, as the
 * specification says) or placed by its node.  Before any pose the rest
 * pose.  A primitive of a file without animation data copies its static
 * geometry.
 *
 * Returns: %FALSE when @primitive is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_gltf_geometry_get_posed (LrgGltfGeometry *self,
                             guint            primitive,
                             gfloat          *out_positions,
                             gfloat          *out_normals);

/**
 * lrg_gltf_geometry_get_material_normal_image:
 * @self: an #LrgGltfGeometry
 * @material: material index
 * @scale: (out) (optional): the normalTexture's scale (1 when absent)
 *
 * Returns: the image of the material's normal map, -1 for none
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_gltf_geometry_get_material_normal_image (LrgGltfGeometry *self,
                                             guint            material,
                                             gfloat          *scale);

/**
 * lrg_gltf_geometry_get_material_metallic_image:
 * @self: an #LrgGltfGeometry
 * @material: material index
 * @metallic: (out) (optional): metallicFactor (1 when absent)
 * @roughness: (out) (optional): roughnessFactor (1 when absent)
 *
 * Returns: the image of the metallicRoughnessTexture (green roughness,
 *   blue metallic), -1 for none
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_gltf_geometry_get_material_metallic_image (LrgGltfGeometry *self,
                                               guint            material,
                                               gfloat          *metallic,
                                               gfloat          *roughness);

/**
 * lrg_gltf_geometry_get_material_occlusion_image:
 * @self: an #LrgGltfGeometry
 * @material: material index
 * @strength: (out) (optional): the occlusionTexture's strength (1)
 *
 * Returns: the image of the occlusion map (red channel), -1 for none
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_gltf_geometry_get_material_occlusion_image (LrgGltfGeometry *self,
                                                guint            material,
                                                gfloat          *strength);

/**
 * lrg_gltf_geometry_get_material_emissive_image:
 * @self: an #LrgGltfGeometry
 * @material: material index
 * @rgb: (out caller-allocates) (optional) (array fixed-size=3): the
 *   emissiveFactor (black when absent), times KHR_materials_emissive_strength
 *
 * Returns: the image of the emissive map, -1 for none
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_gltf_geometry_get_material_emissive_image (LrgGltfGeometry *self,
                                               guint            material,
                                               gfloat          *rgb);

/**
 * lrg_gltf_geometry_get_material_uv_transform:
 * @self: an #LrgGltfGeometry
 * @material: material index
 * @matrix: (out caller-allocates) (array fixed-size=6): the base colour
 *   texture's KHR_texture_transform as u' = m0 u + m1 v + m2,
 *   v' = m3 u + m4 v + m5 (the identity when the material has none)
 *
 * Returns: whether the material has a texture transform
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_gltf_geometry_get_material_uv_transform (LrgGltfGeometry *self,
                                             guint            material,
                                             gfloat          *matrix);

G_END_DECLS
