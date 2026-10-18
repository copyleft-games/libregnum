/* lrg-tree-mesh.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTreeMesh - procedural low-poly trees: conifers, broadleaf trees,
 * palms, dead trees and shrubs as small flat-shaded, vertex-coloured
 * meshes, generated from a seed (the same seed always gives the same
 * tree). The geometry lives on the CPU; lrg_tree_mesh_add_to_batch()
 * merges a placed copy into an #LrgStaticBatch for drawing.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-static-batch.h"

G_BEGIN_DECLS

/**
 * LrgTreeShape:
 * @LRG_TREE_SHAPE_CONIFER: a trunk under three stacked cones
 * @LRG_TREE_SHAPE_BROADLEAF: a trunk under one to three rounded crowns
 * @LRG_TREE_SHAPE_PALM: a leaning, curved trunk with drooping fronds
 * @LRG_TREE_SHAPE_DEAD: a bare trunk with a few branches
 * @LRG_TREE_SHAPE_SHRUB: two or three rounded clumps on the ground
 *
 * The family of a generated tree.
 */
typedef enum
{
    LRG_TREE_SHAPE_CONIFER,
    LRG_TREE_SHAPE_BROADLEAF,
    LRG_TREE_SHAPE_PALM,
    LRG_TREE_SHAPE_DEAD,
    LRG_TREE_SHAPE_SHRUB
} LrgTreeShape;

LRG_AVAILABLE_IN_ALL
GType lrg_tree_shape_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TREE_SHAPE (lrg_tree_shape_get_type ())

#define LRG_TYPE_TREE_MESH (lrg_tree_mesh_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTreeMesh, lrg_tree_mesh, LRG, TREE_MESH, GObject)

/**
 * lrg_tree_mesh_new:
 * @shape: the tree family
 * @seed: the generation seed
 * @height: the tree's height in world units (> 0; base at y = 0)
 *
 * Generates a tree in its default colours.  The tree stands on the
 * origin, grows along +Y and stays within its bounds
 * (lrg_tree_mesh_get_bounds()): its top is at @height, its foliage
 * reaches at most @height * 0.6 from the axis.
 *
 * Returns: (transfer full): a new #LrgTreeMesh
 */
LRG_AVAILABLE_IN_ALL
LrgTreeMesh *
lrg_tree_mesh_new (LrgTreeShape shape,
                   guint32      seed,
                   gfloat       height);

/**
 * lrg_tree_mesh_set_colors:
 * @self: an #LrgTreeMesh
 * @trunk: (nullable): the trunk and branch colour, or %NULL for the default
 * @foliage: (nullable): the leaf colour, or %NULL for the shape's default
 *
 * Regenerates the tree in new colours (each face is shaded a little
 * lighter or darker by the seed); the shape is unchanged.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_tree_mesh_set_colors (LrgTreeMesh    *self,
                          const GrlColor *trunk,
                          const GrlColor *foliage);

/**
 * lrg_tree_mesh_get_shape:
 * @self: an #LrgTreeMesh
 *
 * Returns: the tree family
 */
LRG_AVAILABLE_IN_ALL
LrgTreeShape
lrg_tree_mesh_get_shape (LrgTreeMesh *self);

/**
 * lrg_tree_mesh_get_height:
 * @self: an #LrgTreeMesh
 *
 * Returns: the height the tree was generated for
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_tree_mesh_get_height (LrgTreeMesh *self);

/**
 * lrg_tree_mesh_get_vertex_count:
 * @self: an #LrgTreeMesh
 *
 * Every triangle has its own three vertices (flat shading).
 *
 * Returns: the vertex count
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_tree_mesh_get_vertex_count (LrgTreeMesh *self);

/**
 * lrg_tree_mesh_get_triangle_count:
 * @self: an #LrgTreeMesh
 *
 * Returns: the triangle count
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_tree_mesh_get_triangle_count (LrgTreeMesh *self);

/**
 * lrg_tree_mesh_get_positions:
 * @self: an #LrgTreeMesh
 * @n_floats: (out) (optional): the number of floats (3 per vertex)
 *
 * Returns: (array length=n_floats) (transfer none): the positions
 */
LRG_AVAILABLE_IN_ALL
const gfloat *
lrg_tree_mesh_get_positions (LrgTreeMesh *self,
                             guint       *n_floats);

/**
 * lrg_tree_mesh_get_normals:
 * @self: an #LrgTreeMesh
 * @n_floats: (out) (optional): the number of floats (3 per vertex)
 *
 * Returns: (array length=n_floats) (transfer none): the unit face normals
 */
LRG_AVAILABLE_IN_ALL
const gfloat *
lrg_tree_mesh_get_normals (LrgTreeMesh *self,
                           guint       *n_floats);

/**
 * lrg_tree_mesh_get_colors:
 * @self: an #LrgTreeMesh
 * @n_bytes: (out) (optional): the number of bytes (RGBA per vertex)
 *
 * Returns: (array length=n_bytes) (transfer none): the vertex colours
 */
LRG_AVAILABLE_IN_ALL
const guint8 *
lrg_tree_mesh_get_colors (LrgTreeMesh *self,
                          guint       *n_bytes);

/**
 * lrg_tree_mesh_get_indices:
 * @self: an #LrgTreeMesh
 * @n_indices: (out) (optional): the number of indices (3 per triangle)
 *
 * Returns: (array length=n_indices) (transfer none): the triangle indices
 */
LRG_AVAILABLE_IN_ALL
const guint32 *
lrg_tree_mesh_get_indices (LrgTreeMesh *self,
                           guint       *n_indices);

/**
 * lrg_tree_mesh_get_bounds:
 * @self: an #LrgTreeMesh
 * @out_min: (out caller-allocates) (array fixed-size=3): the box minimum
 * @out_max: (out caller-allocates) (array fixed-size=3): the box maximum
 */
LRG_AVAILABLE_IN_ALL
void
lrg_tree_mesh_get_bounds (LrgTreeMesh *self,
                          gfloat      *out_min,
                          gfloat      *out_max);

/**
 * lrg_tree_mesh_add_to_batch:
 * @self: an #LrgTreeMesh
 * @batch: the batch to merge into
 * @transform: (nullable): the placement (rotation, uniform scale and
 *   translation), or %NULL for none
 * @material: the batch material (its texture multiplies the vertex
 *   colours; leave it without a texture for plain colours)
 * @layer: the batch layer
 * @error: return location for a #GError
 *
 * Returns: %TRUE when the copy was merged
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_tree_mesh_add_to_batch (LrgTreeMesh     *self,
                            LrgStaticBatch  *batch,
                            const GrlMatrix *transform,
                            guint            material,
                            guint            layer,
                            GError         **error);

G_END_DECLS
