/* lrg-mesh-simplify.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMeshSimplify: quadric error metric (QEM) edge-collapse mesh
 * simplification, written in C for libregnum. Works on plain index and
 * position arrays (headless) and on raylib Mesh / GrlMesh data, keeps the
 * original vertices (every attribute survives untouched), never tears
 * UV/normal seams, can pin borders and seams, and stops at a target
 * triangle ratio or a target geometric error, whichever comes first.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LRG_MESH_SIMPLIFY_ERROR:
 *
 * Error domain of #LrgMeshSimplify.
 */
#define LRG_MESH_SIMPLIFY_ERROR (lrg_mesh_simplify_error_quark ())

/**
 * lrg_mesh_simplify_error_quark:
 *
 * Returns: the #GQuark of %LRG_MESH_SIMPLIFY_ERROR
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_mesh_simplify_error_quark (void);

/**
 * LrgMeshSimplifyError:
 * @LRG_MESH_SIMPLIFY_ERROR_INVALID_INPUT: the index or vertex data is
 *   malformed (index count not a multiple of 3, index out of range,
 *   missing positions, stride below 3)
 * @LRG_MESH_SIMPLIFY_ERROR_NO_GPU: the call has to upload a mesh and no
 *   graphics context is ready
 *
 * Errors reported in %LRG_MESH_SIMPLIFY_ERROR.
 */
typedef enum
{
	LRG_MESH_SIMPLIFY_ERROR_INVALID_INPUT,
	LRG_MESH_SIMPLIFY_ERROR_NO_GPU
} LrgMeshSimplifyError;

LRG_AVAILABLE_IN_ALL
GType lrg_mesh_simplify_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MESH_SIMPLIFY_ERROR (lrg_mesh_simplify_error_get_type ())

/**
 * LrgMeshVertexKind:
 * @LRG_MESH_VERTEX_KIND_MANIFOLD: interior vertex with a single wedge;
 *   may collapse onto any neighbour
 * @LRG_MESH_VERTEX_KIND_BORDER: on an open boundary of the surface; may
 *   only slide along the boundary (or is pinned with
 *   #LrgMeshSimplify:lock-border)
 * @LRG_MESH_VERTEX_KIND_SEAM: on a UV/normal seam (two vertices share the
 *   position with matching seam edges); both wedges slide along the seam
 *   together (or are pinned with #LrgMeshSimplify:lock-seams)
 * @LRG_MESH_VERTEX_KIND_LOCKED: never moves (seam corners, non-manifold
 *   fans, pinned borders and seams)
 *
 * How the simplifier classified a vertex; see
 * lrg_mesh_simplify_classify().
 */
typedef enum
{
	LRG_MESH_VERTEX_KIND_MANIFOLD,
	LRG_MESH_VERTEX_KIND_BORDER,
	LRG_MESH_VERTEX_KIND_SEAM,
	LRG_MESH_VERTEX_KIND_LOCKED
} LrgMeshVertexKind;

LRG_AVAILABLE_IN_ALL
GType lrg_mesh_vertex_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MESH_VERTEX_KIND (lrg_mesh_vertex_kind_get_type ())

#define LRG_TYPE_MESH_SIMPLIFY (lrg_mesh_simplify_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMeshSimplify, lrg_mesh_simplify, LRG, MESH_SIMPLIFY, GObject)

/**
 * lrg_mesh_simplify_new:
 *
 * Creates a simplifier with target ratio 0.5, target error 0.01 (1% of
 * the mesh extent), borders locked and seams sliding.
 *
 * Returns: (transfer full): a new #LrgMeshSimplify
 */
LRG_AVAILABLE_IN_ALL
LrgMeshSimplify * lrg_mesh_simplify_new (void);

/* --------------------------------------------------------------------------
 * Settings
 * -------------------------------------------------------------------------- */

/**
 * lrg_mesh_simplify_get_target_ratio:
 * @self: an #LrgMeshSimplify
 *
 * Returns: the fraction of triangles to keep, 0..1
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_mesh_simplify_get_target_ratio (LrgMeshSimplify *self);

/**
 * lrg_mesh_simplify_set_target_ratio:
 * @self: an #LrgMeshSimplify
 * @ratio: fraction of triangles to keep, clamped to 0..1
 *
 * Sets #LrgMeshSimplify:target-ratio. The simplifier stops as soon as
 * the triangle count is at or below ratio times the input count.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_simplify_set_target_ratio (LrgMeshSimplify *self,
                                         gfloat           ratio);

/**
 * lrg_mesh_simplify_get_target_error:
 * @self: an #LrgMeshSimplify
 *
 * Returns: the largest allowed geometric error, relative to the mesh
 *   extent
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_mesh_simplify_get_target_error (LrgMeshSimplify *self);

/**
 * lrg_mesh_simplify_set_target_error:
 * @self: an #LrgMeshSimplify
 * @error: largest allowed deviation from the original surface as a
 *   fraction of the largest bounding-box side (0.01 = 1%); values >= 1
 *   effectively disable the limit
 *
 * Sets #LrgMeshSimplify:target-error. No collapse whose quadric error
 * exceeds this distance is performed, so the result may keep more
 * triangles than #LrgMeshSimplify:target-ratio asks for.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_simplify_set_target_error (LrgMeshSimplify *self,
                                         gfloat           error);

/**
 * lrg_mesh_simplify_get_lock_border:
 * @self: an #LrgMeshSimplify
 *
 * Returns: whether open-boundary vertices are pinned
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mesh_simplify_get_lock_border (LrgMeshSimplify *self);

/**
 * lrg_mesh_simplify_set_lock_border:
 * @self: an #LrgMeshSimplify
 * @lock: %TRUE to pin every vertex on an open boundary
 *
 * Sets #LrgMeshSimplify:lock-border (default %TRUE). Pinned borders let
 * pieces that are simplified separately (terrain tiles, model parts)
 * still meet without cracks. Unpinned borders slide along themselves.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_simplify_set_lock_border (LrgMeshSimplify *self,
                                        gboolean         lock);

/**
 * lrg_mesh_simplify_get_lock_seams:
 * @self: an #LrgMeshSimplify
 *
 * Returns: whether UV/normal seam vertices are pinned
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mesh_simplify_get_lock_seams (LrgMeshSimplify *self);

/**
 * lrg_mesh_simplify_set_lock_seams:
 * @self: an #LrgMeshSimplify
 * @lock: %TRUE to pin every vertex on a UV/normal seam
 *
 * Sets #LrgMeshSimplify:lock-seams (default %FALSE). Seams are never torn
 * open either way; unpinned seam vertices collapse along the seam with
 * both of their wedges, pinned ones keep the exact seam polyline.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_simplify_set_lock_seams (LrgMeshSimplify *self,
                                       gboolean         lock);

/**
 * lrg_mesh_simplify_get_result_error:
 * @self: an #LrgMeshSimplify
 *
 * Returns: the relative error of the last simplification (largest
 *   collapse error, as a fraction of the mesh extent), 0 before any run
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_mesh_simplify_get_result_error (LrgMeshSimplify *self);

/**
 * lrg_mesh_simplify_get_result_ratio:
 * @self: an #LrgMeshSimplify
 *
 * Returns: output triangles divided by input triangles of the last run
 *   (1 before any run)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_mesh_simplify_get_result_ratio (LrgMeshSimplify *self);

/* --------------------------------------------------------------------------
 * Simplification (headless)
 * -------------------------------------------------------------------------- */

/**
 * lrg_mesh_simplify_indices:
 * @self: an #LrgMeshSimplify
 * @indices: (array length=n_indices): triangle list indices
 * @n_indices: number of indices, a multiple of 3
 * @positions: (array length=n_floats): vertex data; the first three
 *   floats of each vertex are its position
 * @n_floats: number of floats in @positions (vertex count times @stride)
 * @stride: floats per vertex, at least 3
 * @out_indices: (out) (transfer full) (array length=out_n_indices): the
 *   simplified triangle list, indexing the same vertices
 * @out_n_indices: (out): number of indices in @out_indices
 * @error: return location for a #GError
 *
 * Simplifies a triangle list. Vertices are never moved or created: the
 * output indexes a subset of the input vertices, so every attribute
 * (normals, UVs, colours, skin weights) stays valid as is. Vertices with
 * different indices at the same position form a seam and are kept
 * consistent. Updates the result error and ratio.
 *
 * Returns: %TRUE on success, %FALSE with @error set on malformed input
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mesh_simplify_indices (LrgMeshSimplify  *self,
                                    const guint32    *indices,
                                    gsize             n_indices,
                                    const gfloat     *positions,
                                    gsize             n_floats,
                                    guint             stride,
                                    guint32         **out_indices,
                                    gsize            *out_n_indices,
                                    GError          **error);

/**
 * lrg_mesh_simplify_classify:
 * @self: an #LrgMeshSimplify
 * @indices: (array length=n_indices): triangle list indices
 * @n_indices: number of indices, a multiple of 3
 * @positions: (array length=n_floats): vertex data, position first
 * @n_floats: number of floats in @positions
 * @stride: floats per vertex, at least 3
 * @out_kinds: (out) (transfer full) (array length=out_n_kinds): one
 *   #LrgMeshVertexKind per vertex (as guint8)
 * @out_n_kinds: (out): number of vertices
 * @error: return location for a #GError
 *
 * Reports how the simplifier sees each vertex of a mesh with the current
 * lock settings (for debugging seams, and for editor overlays).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mesh_simplify_classify (LrgMeshSimplify  *self,
                                     const guint32    *indices,
                                     gsize             n_indices,
                                     const gfloat     *positions,
                                     gsize             n_floats,
                                     guint             stride,
                                     guint8          **out_kinds,
                                     gsize            *out_n_kinds,
                                     GError          **error);

/**
 * lrg_mesh_simplify_raylib_mesh:
 * @self: an #LrgMeshSimplify
 * @src: a pointer to a raylib `Mesh` with CPU-side vertex data
 *   (`vertices` at least; indexed or not)
 * @dst: a pointer to a raylib `Mesh` that receives the result
 * @error: return location for a #GError
 *
 * Simplifies raylib mesh data on the CPU without touching the GPU.
 * Unindexed meshes are welded first (vertices with identical attributes
 * merge). @dst is overwritten (not freed first) with freshly
 * `RL_MALLOC`-ed arrays holding only the used vertices with every
 * attribute of @src (positions, UVs, second UVs, normals, tangents,
 * colours, bone indices and weights); `vaoId` and `vboId` are zero, so
 * the caller uploads it (`UploadMesh()`) and frees it (`UnloadMesh()`).
 * The result is indexed with 16-bit indices when it has at most 65535
 * vertices and unindexed otherwise. Animated CPU-skinning buffers are
 * not copied.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mesh_simplify_raylib_mesh (LrgMeshSimplify  *self,
                                        gconstpointer     src,
                                        gpointer          dst,
                                        GError          **error);

/* --------------------------------------------------------------------------
 * Simplification (GPU objects)
 * -------------------------------------------------------------------------- */

/**
 * lrg_mesh_simplify_mesh:
 * @self: an #LrgMeshSimplify
 * @mesh: the source mesh (its CPU data must still be present)
 * @error: return location for a #GError
 *
 * Simplifies @mesh into a new uploaded #GrlMesh with all of its
 * attributes. Needs a graphics context.
 *
 * Returns: (transfer full) (nullable): the simplified mesh, or %NULL
 *   with @error set
 */
LRG_AVAILABLE_IN_ALL
GrlMesh * lrg_mesh_simplify_mesh (LrgMeshSimplify  *self,
                                  GrlMesh          *mesh,
                                  GError          **error);

/**
 * lrg_mesh_simplify_wrap_raylib_mesh:
 * @mesh: a pointer to a raylib `Mesh` with CPU data and no GPU buffers,
 *   such as the output of lrg_mesh_simplify_raylib_mesh(); taken over
 *   (the struct is zeroed on return)
 * @error: return location for a #GError
 *
 * Uploads raylib mesh data and wraps it in a #GrlMesh that owns it
 * (finalizing the #GrlMesh unloads it). Needs a graphics context.
 *
 * Returns: (transfer full) (nullable): the new mesh, or %NULL with
 *   @error set (the data is freed in that case)
 */
LRG_AVAILABLE_IN_ALL
GrlMesh * lrg_mesh_simplify_wrap_raylib_mesh (gpointer   mesh,
                                              GError   **error);

G_END_DECLS
