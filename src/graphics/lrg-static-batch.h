/* lrg-static-batch.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgStaticBatch - merges many static, pre-transformed meshes into a few
 * GPU meshes grouped by (material, layer). Each chunk stays within 16-bit
 * indices and keeps its bounds and layer, so a scene of thousands of
 * props costs a handful of draw calls. Merging is pure CPU work and fully
 * headless; only upload() and draw() need a GL context.
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

#define LRG_TYPE_STATIC_BATCH (lrg_static_batch_get_type ())

/**
 * LRG_STATIC_BATCH_MAX_VERTICES:
 *
 * Largest vertex count of one chunk: every index fits in 16 bits.
 */
#define LRG_STATIC_BATCH_MAX_VERTICES (65535)

/**
 * LRG_STATIC_BATCH_MAX_LAYERS:
 *
 * Layers are 0 .. 31 so a #guint32 selects them in lrg_static_batch_draw().
 */
#define LRG_STATIC_BATCH_MAX_LAYERS (32)

/**
 * LRG_STATIC_BATCH_ALL_LAYERS:
 *
 * Layer mask selecting every layer.
 */
#define LRG_STATIC_BATCH_ALL_LAYERS (0xFFFFFFFFu)

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgStaticBatch, lrg_static_batch, LRG, STATIC_BATCH, GObject)

/**
 * lrg_static_batch_new:
 *
 * Creates an empty batch.
 *
 * Returns: (transfer full): a new #LrgStaticBatch
 */
LRG_AVAILABLE_IN_ALL
LrgStaticBatch *
lrg_static_batch_new (void);

/**
 * lrg_static_batch_set_max_chunk_vertices:
 * @self: an #LrgStaticBatch
 * @max_vertices: chunk vertex limit, clamped to [3,
 *   %LRG_STATIC_BATCH_MAX_VERTICES]
 *
 * Lowers the per-chunk vertex limit (smaller chunks cull better). Applies
 * to meshes added afterwards.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_static_batch_set_max_chunk_vertices (LrgStaticBatch *self,
                                         guint           max_vertices);

/**
 * lrg_static_batch_get_max_chunk_vertices:
 * @self: an #LrgStaticBatch
 *
 * Returns: the per-chunk vertex limit
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_static_batch_get_max_chunk_vertices (LrgStaticBatch *self);

/**
 * lrg_static_batch_add_mesh: (skip)
 * @self: an #LrgStaticBatch
 * @positions: 3 floats per vertex
 * @normals: (nullable): 3 floats per vertex; %NULL gives +Y normals
 * @texcoords: (nullable): 2 floats per vertex; %NULL gives (0, 0)
 * @colors: (nullable): 4 bytes (RGBA) per vertex; %NULL gives white
 * @n_vertices: vertex count
 * @indices: (nullable): triangle list indices; %NULL means the vertices
 *   already form a triangle list
 * @n_indices: index count (a multiple of 3), ignored when @indices is
 *   %NULL
 * @transform: (nullable): world transform applied to positions (normals
 *   use its inverse transpose); %NULL is identity
 * @material: caller material id; chunks never mix materials
 * @layer: layer 0 .. 31; chunks never mix layers
 * @error: (nullable): return location for a #GError
 *
 * Transforms and appends a mesh. Vertices go into the current chunk of
 * (@material, @layer) while it has room, otherwise a new chunk starts; a
 * mesh larger than the chunk limit is split at triangle granularity, with
 * shared vertices duplicated across chunks. Mirroring transforms
 * (negative determinant) flip the triangle winding so front faces stay
 * front faces.
 *
 * Errors: %G_IO_ERROR_INVALID_ARGUMENT for a layer out of range, index
 * counts that are not multiples of 3, out-of-range indices, non-finite
 * positions or a non-finite / singular transform. Nothing is added then.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_static_batch_add_mesh (LrgStaticBatch  *self,
                           const gfloat    *positions,
                           const gfloat    *normals,
                           const gfloat    *texcoords,
                           const guint8    *colors,
                           guint            n_vertices,
                           const guint32   *indices,
                           guint            n_indices,
                           const GrlMatrix *transform,
                           guint            material,
                           guint            layer,
                           GError         **error);

/**
 * lrg_static_batch_add_grl_mesh:
 * @self: an #LrgStaticBatch
 * @mesh: a mesh whose CPU data is still present
 * @transform: (nullable): world transform; %NULL is identity
 * @material: caller material id
 * @layer: layer 0 .. 31
 * @error: (nullable): return location for a #GError
 *
 * Adds a #GrlMesh's CPU-side vertices (positions, normals, texcoords,
 * colours, 16-bit indices). Does not need a graphics context.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_static_batch_add_grl_mesh (LrgStaticBatch  *self,
                               GrlMesh         *mesh,
                               const GrlMatrix *transform,
                               guint            material,
                               guint            layer,
                               GError         **error);

/**
 * lrg_static_batch_add_model:
 * @self: an #LrgStaticBatch
 * @model: a static (unskinned) model
 * @transform: (nullable): world transform, applied after the model's own
 *   transform; %NULL is identity
 * @layer: layer 0 .. 31
 * @error: (nullable): return location for a #GError
 *
 * Adds every mesh of @model. Each mesh's material id is the GL id of its
 * material's albedo texture (raylib's default white texture for untextured
 * materials), and that texture is remembered for drawing, borrowed from
 * the model: keep @model alive while the batch draws. The material's albedo
 * colour (glTF baseColorFactor) is multiplied into the merged vertex
 * colours, since chunks are keyed by texture alone. Do not mix these ids
 * with your own material ids.
 *
 * Returns: %TRUE on success; on error, meshes added before the failing
 *   one remain
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_static_batch_add_model (LrgStaticBatch  *self,
                            GrlModel        *model,
                            const GrlMatrix *transform,
                            guint            layer,
                            GError         **error);

/**
 * lrg_static_batch_clear:
 * @self: an #LrgStaticBatch
 *
 * Removes every chunk (unloading GPU data). Material textures, layer tints
 * and the shader are kept.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_static_batch_clear (LrgStaticBatch *self);

/**
 * lrg_static_batch_get_chunk_count:
 * @self: an #LrgStaticBatch
 *
 * Returns: the number of chunks
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_static_batch_get_chunk_count (LrgStaticBatch *self);

/**
 * lrg_static_batch_get_vertex_count:
 * @self: an #LrgStaticBatch
 *
 * Returns: total vertices over all chunks
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_static_batch_get_vertex_count (LrgStaticBatch *self);

/**
 * lrg_static_batch_get_triangle_count:
 * @self: an #LrgStaticBatch
 *
 * Returns: total triangles over all chunks
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_static_batch_get_triangle_count (LrgStaticBatch *self);

/**
 * lrg_static_batch_get_chunk_material:
 * @self: an #LrgStaticBatch
 * @chunk: chunk index
 *
 * Returns: the chunk's material id (0 when out of range)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_static_batch_get_chunk_material (LrgStaticBatch *self,
                                     guint           chunk);

/**
 * lrg_static_batch_get_chunk_layer:
 * @self: an #LrgStaticBatch
 * @chunk: chunk index
 *
 * Returns: the chunk's layer (0 when out of range)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_static_batch_get_chunk_layer (LrgStaticBatch *self,
                                  guint           chunk);

/**
 * lrg_static_batch_get_chunk_vertex_count:
 * @self: an #LrgStaticBatch
 * @chunk: chunk index
 *
 * Returns: the chunk's vertex count (0 when out of range)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_static_batch_get_chunk_vertex_count (LrgStaticBatch *self,
                                         guint           chunk);

/**
 * lrg_static_batch_get_chunk_index_count:
 * @self: an #LrgStaticBatch
 * @chunk: chunk index
 *
 * Returns: the chunk's index count (0 when out of range)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_static_batch_get_chunk_index_count (LrgStaticBatch *self,
                                        guint           chunk);

/**
 * lrg_static_batch_get_chunk_bounds:
 * @self: an #LrgStaticBatch
 * @chunk: chunk index
 * @min_x: (out) (optional): minimum X
 * @min_y: (out) (optional): minimum Y
 * @min_z: (out) (optional): minimum Z
 * @max_x: (out) (optional): maximum X
 * @max_y: (out) (optional): maximum Y
 * @max_z: (out) (optional): maximum Z
 *
 * World-space axis-aligned bounds of the chunk's vertices, for culling.
 *
 * Returns: %TRUE if @chunk exists
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_static_batch_get_chunk_bounds (LrgStaticBatch *self,
                                   guint           chunk,
                                   gfloat         *min_x,
                                   gfloat         *min_y,
                                   gfloat         *min_z,
                                   gfloat         *max_x,
                                   gfloat         *max_y,
                                   gfloat         *max_z);

/**
 * lrg_static_batch_get_chunk_positions:
 * @self: an #LrgStaticBatch
 * @chunk: chunk index
 * @n_floats: (out): number of floats (3 per vertex)
 *
 * Returns: (transfer none) (array length=n_floats) (nullable): the merged,
 *   transformed positions
 */
LRG_AVAILABLE_IN_ALL
const gfloat *
lrg_static_batch_get_chunk_positions (LrgStaticBatch *self,
                                      guint           chunk,
                                      guint          *n_floats);

/**
 * lrg_static_batch_get_chunk_normals:
 * @self: an #LrgStaticBatch
 * @chunk: chunk index
 * @n_floats: (out): number of floats (3 per vertex)
 *
 * Returns: (transfer none) (array length=n_floats) (nullable): the merged,
 *   transformed unit normals
 */
LRG_AVAILABLE_IN_ALL
const gfloat *
lrg_static_batch_get_chunk_normals (LrgStaticBatch *self,
                                    guint           chunk,
                                    guint          *n_floats);

/**
 * lrg_static_batch_get_chunk_colors:
 * @self: an #LrgStaticBatch
 * @chunk: chunk index
 * @n_bytes: (out): number of bytes (4 per vertex)
 *
 * Returns: (transfer none) (array length=n_bytes) (nullable): the merged
 *   RGBA vertex colours
 */
LRG_AVAILABLE_IN_ALL
const guint8 *
lrg_static_batch_get_chunk_colors (LrgStaticBatch *self,
                                   guint           chunk,
                                   guint          *n_bytes);

/**
 * lrg_static_batch_get_chunk_indices:
 * @self: an #LrgStaticBatch
 * @chunk: chunk index
 * @n_indices: (out): number of indices
 *
 * Returns: (transfer none) (array length=n_indices) (nullable): the
 *   chunk-local 16-bit triangle indices
 */
LRG_AVAILABLE_IN_ALL
const guint16 *
lrg_static_batch_get_chunk_indices (LrgStaticBatch *self,
                                    guint           chunk,
                                    guint          *n_indices);

/**
 * lrg_static_batch_set_material_texture:
 * @self: an #LrgStaticBatch
 * @material: material id
 * @texture: (nullable): albedo texture for the material, %NULL to unset
 *
 * Sets the texture drawn for @material (the batch keeps a reference).
 * Materials without a texture draw with raylib's white texture.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_static_batch_set_material_texture (LrgStaticBatch *self,
                                       guint           material,
                                       GrlTexture     *texture);

/**
 * lrg_static_batch_set_layer_tint:
 * @self: an #LrgStaticBatch
 * @layer: layer 0 .. 31
 * @tint: (nullable): colour (alpha included) multiplied into the layer's
 *   chunks; %NULL restores white
 *
 * Per-layer tint, e.g. to fade a roof layer when the player walks inside.
 * Alpha below 255 relies on the current blend mode; draw such layers
 * after opaque ones.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_static_batch_set_layer_tint (LrgStaticBatch *self,
                                 guint           layer,
                                 const GrlColor *tint);

/**
 * lrg_static_batch_set_shader:
 * @self: an #LrgStaticBatch
 * @shader: (nullable): shader for every chunk, %NULL for raylib's default
 *
 * The batch keeps a reference to @shader.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_static_batch_set_shader (LrgStaticBatch *self,
                             GrlShader      *shader);

/**
 * lrg_static_batch_upload:
 * @self: an #LrgStaticBatch
 * @error: (nullable): return location for a #GError
 *
 * Uploads new or changed chunks to the GPU. CPU data is kept, so chunks
 * stay inspectable and can be re-uploaded. Adding meshes afterwards marks
 * the affected chunks for re-upload.
 *
 * Errors: %G_IO_ERROR_NOT_INITIALIZED without a graphics context.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_static_batch_upload (LrgStaticBatch  *self,
                         GError         **error);

/**
 * lrg_static_batch_is_uploaded:
 * @self: an #LrgStaticBatch
 *
 * Returns: %TRUE when every chunk is on the GPU and current
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_static_batch_is_uploaded (LrgStaticBatch *self);

/**
 * lrg_static_batch_unload:
 * @self: an #LrgStaticBatch
 *
 * Releases GPU buffers, keeping CPU data (call before closing the
 * window if the batch outlives it).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_static_batch_unload (LrgStaticBatch *self);

/**
 * lrg_static_batch_draw:
 * @self: an #LrgStaticBatch
 * @layer_mask: bit N selects layer N (%LRG_STATIC_BATCH_ALL_LAYERS for all)
 *
 * Draws every uploaded chunk whose layer is selected, in chunk order, with
 * its material texture, its layer tint and the batch shader. Must be
 * called inside a 3D mode. Chunks not yet uploaded are skipped.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_static_batch_draw (LrgStaticBatch *self,
                       guint32         layer_mask);

/**
 * lrg_static_batch_draw_chunk:
 * @self: an #LrgStaticBatch
 * @chunk: chunk index
 *
 * Draws one uploaded chunk (for callers that cull with
 * lrg_static_batch_get_chunk_bounds()).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_static_batch_draw_chunk (LrgStaticBatch *self,
                             guint           chunk);

G_END_DECLS
