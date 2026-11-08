/* lrg-bake-bvh.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBakeBvh - a static triangle bounding volume hierarchy for CPU ray
 * casting.  The lightmap baker traces every shadow, bounce and probe ray
 * through one; tools can use it for any offline ray query (picking on a
 * huge static mesh, visibility tests, ambient occlusion).
 *
 * The tree is built once over a triangle soup with a binned surface area
 * heuristic (16 bins per axis, leaves of at most 4 triangles), stored
 * flat (32-byte nodes, children of an interior node side by side) with
 * the triangles reordered and pre-transformed into (v0, edge1, edge2)
 * for the Moller-Trumbore test.  After lrg_bake_bvh_build() the object is
 * read-only, so any number of threads may query it at once.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LRG_BAKE_BVH_MAX_LEAF_TRIANGLES:
 *
 * Most triangles a leaf of an #LrgBakeBvh holds.
 */
#define LRG_BAKE_BVH_MAX_LEAF_TRIANGLES (4)

/* ==========================================================================
 * LrgBakeHit
 * ========================================================================== */

typedef struct _LrgBakeHit LrgBakeHit;

/**
 * LrgBakeHit:
 * @t: distance along the ray (in units of the direction's length)
 * @triangle: index of the hit triangle in the order it was given to
 *   lrg_bake_bvh_build()
 * @u: barycentric weight of the triangle's second vertex
 * @v: barycentric weight of the triangle's third vertex (the first
 *   vertex weighs 1 - u - v)
 * @backface: %TRUE when the ray hit the back of the triangle (the side
 *   its counter-clockwise winding faces away from)
 *
 * The closest intersection lrg_bake_bvh_intersect() found.
 */
struct _LrgBakeHit
{
	gfloat   t;
	guint    triangle;
	gfloat   u;
	gfloat   v;
	gboolean backface;
};

LRG_AVAILABLE_IN_ALL
GType lrg_bake_hit_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BAKE_HIT (lrg_bake_hit_get_type ())

/**
 * lrg_bake_hit_copy:
 * @hit: an #LrgBakeHit
 *
 * Returns: (transfer full): a heap copy of @hit
 */
LRG_AVAILABLE_IN_ALL
LrgBakeHit * lrg_bake_hit_copy (const LrgBakeHit *hit);

/**
 * lrg_bake_hit_free:
 * @hit: (nullable): a heap #LrgBakeHit
 *
 * Frees a hit made by lrg_bake_hit_copy().
 */
LRG_AVAILABLE_IN_ALL
void lrg_bake_hit_free (LrgBakeHit *hit);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgBakeHit, lrg_bake_hit_free)

/* ==========================================================================
 * LrgBakeBvh
 * ========================================================================== */

#define LRG_TYPE_BAKE_BVH (lrg_bake_bvh_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBakeBvh, lrg_bake_bvh, LRG, BAKE_BVH, GObject)

/**
 * lrg_bake_bvh_new:
 *
 * Creates an empty hierarchy (every query misses until
 * lrg_bake_bvh_build()).
 *
 * Returns: (transfer full): a new #LrgBakeBvh
 */
LRG_AVAILABLE_IN_ALL
LrgBakeBvh * lrg_bake_bvh_new (void);

/**
 * lrg_bake_bvh_build:
 * @self: an #LrgBakeBvh
 * @positions: (array length=n_floats): vertex positions, 3 floats each
 * @n_floats: number of floats in @positions (3 x the vertex count)
 * @indices: (array length=n_indices) (nullable): triangle list indices
 *   into @positions, or %NULL for a non-indexed soup (every 3 vertices
 *   form a triangle)
 * @n_indices: number of indices (a multiple of 3), ignored without
 *   @indices
 *
 * Builds the hierarchy over the triangles, replacing any previous one.
 * The data is copied.  Degenerate triangles are kept (they never hit).
 * Indices outside @positions make the call fail.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_bake_bvh_build (LrgBakeBvh    *self,
                             const gfloat  *positions,
                             gsize          n_floats,
                             const guint32 *indices,
                             gsize          n_indices);

/**
 * lrg_bake_bvh_get_n_triangles:
 * @self: an #LrgBakeBvh
 *
 * Returns: the number of triangles of the last build
 */
LRG_AVAILABLE_IN_ALL
guint lrg_bake_bvh_get_n_triangles (LrgBakeBvh *self);

/**
 * lrg_bake_bvh_get_n_nodes:
 * @self: an #LrgBakeBvh
 *
 * Returns: the number of tree nodes (interior and leaves) of the last
 *   build
 */
LRG_AVAILABLE_IN_ALL
guint lrg_bake_bvh_get_n_nodes (LrgBakeBvh *self);

/**
 * lrg_bake_bvh_get_bounds:
 * @self: an #LrgBakeBvh
 * @out_min: (out caller-allocates) (array fixed-size=3): the smallest
 *   corner of every triangle
 * @out_max: (out caller-allocates) (array fixed-size=3): the largest
 *   corner
 *
 * Returns: %FALSE (outputs zero) when the hierarchy is empty
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_bake_bvh_get_bounds (LrgBakeBvh *self,
                                  gfloat     *out_min,
                                  gfloat     *out_max);

/**
 * lrg_bake_bvh_intersect:
 * @self: an #LrgBakeBvh
 * @origin: (array fixed-size=3): ray origin
 * @direction: (array fixed-size=3): ray direction (need not be unit
 *   length; distances are in units of its length)
 * @t_min: nearest accepted distance (an offset that skips the surface
 *   the ray leaves)
 * @t_max: farthest accepted distance (%G_MAXFLOAT for an unbounded ray)
 * @out_hit: (out caller-allocates) (optional): the closest hit
 *
 * Finds the closest triangle the ray hits from either side within
 * (@t_min, @t_max).  Thread-safe.
 *
 * Returns: %TRUE on a hit
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_bake_bvh_intersect (LrgBakeBvh   *self,
                                 const gfloat *origin,
                                 const gfloat *direction,
                                 gfloat        t_min,
                                 gfloat        t_max,
                                 LrgBakeHit   *out_hit);

/**
 * lrg_bake_bvh_occluded:
 * @self: an #LrgBakeBvh
 * @origin: (array fixed-size=3): ray origin
 * @direction: (array fixed-size=3): ray direction
 * @t_min: nearest accepted distance
 * @t_max: farthest accepted distance (the distance to the light for a
 *   shadow ray)
 *
 * Any-hit query: stops at the first triangle found in (@t_min,
 * @t_max), so it is cheaper than lrg_bake_bvh_intersect() for shadow
 * rays.  Thread-safe.
 *
 * Returns: %TRUE when something blocks the segment
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_bake_bvh_occluded (LrgBakeBvh   *self,
                                const gfloat *origin,
                                const gfloat *direction,
                                gfloat        t_min,
                                gfloat        t_max);

G_END_DECLS
