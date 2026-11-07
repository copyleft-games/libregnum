/* lrg-collide.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Narrowphase collision between two #LrgPhysicsShape instances placed by
 * rigid world transforms: contact manifolds (GJK + EPA, specialised
 * primitive pairs, face clipping, feature ids, convex vs triangle
 * mesh/heightfield with internal-edge removal), closest points,
 * overlap tests, world ray casts and shape casts (time of impact) for
 * continuous collision detection.
 *
 * Every transform is a column-major 4x4 matrix (lrg-math3d.h layout) with
 * a pure rotation and a translation.  Scale is not supported: bake it
 * into the shape.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-physics-shape.h"


G_BEGIN_DECLS

/**
 * LRG_CONTACT_MANIFOLD_MAX_POINTS:
 *
 * Maximum number of contact points in one #LrgContactManifold.
 */
#define LRG_CONTACT_MANIFOLD_MAX_POINTS (4)

#define LRG_TYPE_CONTACT_MANIFOLD (lrg_contact_manifold_get_type ())

/**
 * LrgContactManifold:
 * @normal: unit world-space contact normal pointing from shape A to shape
 *   B: moving B along +normal (or A along -normal) separates the pair
 * @n_points: number of valid points, 0..%LRG_CONTACT_MANIFOLD_MAX_POINTS
 * @points_a: world-space contact points on the surface of A (3 floats per
 *   point)
 * @points_b: world-space contact points on the surface of B
 * @depths: penetration depth per point along @normal: positive when
 *   overlapping, negative (down to -speculative distance) for a
 *   speculative contact that is still apart
 * @feature_ids: per point, an id built from the features (faces, edges,
 *   vertices, triangle) that produced it; it stays the same from step to
 *   step while the same features touch, so a solver can match points and
 *   warm start
 * @child_a: compound child index of A that touches (top level), or -1
 * @child_b: compound child index of B, or -1
 * @sub_shape_a: triangle index when A is a mesh or heightfield, else
 *   %LRG_PHYSICS_NO_SUB_SHAPE
 * @sub_shape_b: triangle index when B is a mesh or heightfield, else
 *   %LRG_PHYSICS_NO_SUB_SHAPE
 * @material_a: per-triangle material id of @sub_shape_a (0 otherwise)
 * @material_b: per-triangle material id of @sub_shape_b (0 otherwise)
 *
 * A set of contact points that share one normal.  A plain boxed struct.
 */
typedef struct
{
	gfloat  normal[3];
	guint   n_points;
	gfloat  points_a[12];
	gfloat  points_b[12];
	gfloat  depths[4];
	guint32 feature_ids[4];
	gint    child_a;
	gint    child_b;
	guint   sub_shape_a;
	guint   sub_shape_b;
	guint   material_a;
	guint   material_b;
} LrgContactManifold;

/**
 * lrg_contact_manifold_get_type:
 *
 * Returns: the boxed #GType of #LrgContactManifold
 */
LRG_AVAILABLE_IN_ALL
GType lrg_contact_manifold_get_type (void) G_GNUC_CONST;

/**
 * lrg_contact_manifold_new:
 *
 * Returns: (transfer full): a new manifold reset with
 *   lrg_contact_manifold_reset()
 */
LRG_AVAILABLE_IN_ALL
LrgContactManifold * lrg_contact_manifold_new (void);

/**
 * lrg_contact_manifold_copy:
 * @self: a #LrgContactManifold
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgContactManifold * lrg_contact_manifold_copy (const LrgContactManifold *self);

/**
 * lrg_contact_manifold_free:
 * @self: (nullable): a #LrgContactManifold
 *
 * Frees a manifold from lrg_contact_manifold_new() or _copy().
 */
LRG_AVAILABLE_IN_ALL
void lrg_contact_manifold_free (LrgContactManifold *self);

/**
 * lrg_contact_manifold_reset:
 * @self: a #LrgContactManifold
 *
 * Empties @self: zero points, normal +Y, children -1, sub-shapes
 * %LRG_PHYSICS_NO_SUB_SHAPE, materials 0.
 */
LRG_AVAILABLE_IN_ALL
void lrg_contact_manifold_reset (LrgContactManifold *self);

/**
 * lrg_contact_manifold_get_point:
 * @self: a #LrgContactManifold
 * @index: point index (< n_points)
 * @point_a: (out caller-allocates) (array fixed-size=3) (optional): point
 *   on A
 * @point_b: (out caller-allocates) (array fixed-size=3) (optional): point
 *   on B
 * @depth: (out) (optional): penetration depth
 * @feature_id: (out) (optional): feature id
 *
 * Reads one point (a binding-friendly accessor for the flat arrays).
 *
 * Returns: %FALSE if @index is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_contact_manifold_get_point (const LrgContactManifold *self,
                                         guint                     index,
                                         gfloat                   *point_a,
                                         gfloat                   *point_b,
                                         gfloat                   *depth,
                                         guint32                  *feature_id);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgContactManifold, lrg_contact_manifold_free)

/* ==========================================================================
 * Contact generation
 * ========================================================================== */

/**
 * lrg_collide_shapes:
 * @a: shape A
 * @xf_a: (array fixed-size=16): rigid world transform of A
 * @b: shape B
 * @xf_b: (array fixed-size=16): rigid world transform of B
 * @out: (out caller-allocates): the manifold
 * @speculative_distance: also report pairs that are apart by up to this
 *   distance (speculative contacts, negative depth); 0 for touching only
 *
 * Collides two shapes and returns the deepest manifold.  Pairs that can
 * produce several manifolds (a box over two non-coplanar triangles, a
 * compound) should use lrg_collide_shapes_all(); this function returns the
 * first manifold of that list.  Plane, mesh and heightfield never collide
 * with each other (static geometry).
 *
 * Returns: %TRUE if @out has at least one point (@out is reset otherwise)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_collide_shapes (LrgPhysicsShape    *a,
                             const gfloat       *xf_a,
                             LrgPhysicsShape    *b,
                             const gfloat       *xf_b,
                             LrgContactManifold *out,
                             gfloat              speculative_distance);

/**
 * lrg_collide_shapes_all:
 * @a: shape A
 * @xf_a: (array fixed-size=16): rigid world transform of A
 * @b: shape B
 * @xf_b: (array fixed-size=16): rigid world transform of B
 * @speculative_distance: speculative distance (see lrg_collide_shapes())
 * @out: (element-type LrgContactManifold): array that receives the
 *   manifolds (appended; not cleared)
 *
 * Collides two shapes and appends every manifold.  Convex pairs produce at
 * most one.  Compounds produce one per touching child pair.  Convex vs
 * mesh/heightfield collides the convex shape with each nearby triangle,
 * replaces normals that come from inactive (internal) edges by the face
 * normal, then merges manifolds whose normals agree within about 2.5
 * degrees and reduces each merged manifold to 4 points.  The order is
 * deterministic (deepest first).
 *
 * Returns: the number of manifolds appended
 */
LRG_AVAILABLE_IN_ALL
guint lrg_collide_shapes_all (LrgPhysicsShape *a,
                              const gfloat    *xf_a,
                              LrgPhysicsShape *b,
                              const gfloat    *xf_b,
                              gfloat           speculative_distance,
                              GArray          *out);

/**
 * lrg_collide_closest_points:
 * @a: convex shape A
 * @xf_a: (array fixed-size=16): rigid world transform of A
 * @b: convex shape B
 * @xf_b: (array fixed-size=16): rigid world transform of B
 * @distance: (out) (optional): separation distance, or minus the
 *   penetration depth when the shapes overlap
 * @point_a: (out caller-allocates) (array fixed-size=3) (optional):
 *   closest (or deepest) point on A, world space
 * @point_b: (out caller-allocates) (array fixed-size=3) (optional):
 *   closest (or deepest) point on B, world space
 * @normal: (out caller-allocates) (array fixed-size=3) (optional): unit
 *   direction from A to B (separating axis, or the direction to move B
 *   out of A)
 *
 * Runs GJK on the shapes (cores plus convex radius) and, when they
 * overlap, EPA for the penetration depth.
 *
 * Returns: %FALSE if either shape is not convex (outputs untouched)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_collide_closest_points (LrgPhysicsShape *a,
                                     const gfloat    *xf_a,
                                     LrgPhysicsShape *b,
                                     const gfloat    *xf_b,
                                     gfloat          *distance,
                                     gfloat          *point_a,
                                     gfloat          *point_b,
                                     gfloat          *normal);

/**
 * lrg_collide_overlap:
 * @a: shape A
 * @xf_a: (array fixed-size=16): rigid world transform of A
 * @b: shape B
 * @xf_b: (array fixed-size=16): rigid world transform of B
 *
 * Boolean overlap test (touching counts).  Works for every pair that
 * lrg_collide_shapes() supports.
 *
 * Returns: %TRUE if the shapes overlap
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_collide_overlap (LrgPhysicsShape *a,
                              const gfloat    *xf_a,
                              LrgPhysicsShape *b,
                              const gfloat    *xf_b);

/* ==========================================================================
 * Rays and casts
 * ========================================================================== */

/**
 * lrg_collide_ray:
 * @shape: a #LrgPhysicsShape
 * @xf: (array fixed-size=16): rigid world transform of @shape
 * @origin: (array fixed-size=3): world ray origin
 * @dir: (array fixed-size=3): world ray direction (its length is the unit
 *   of the fraction)
 * @max_fraction: largest t to accept
 * @hit: (out caller-allocates) (optional): world-space hit
 *
 * World-space wrapper around lrg_physics_shape_cast_ray().
 *
 * Returns: %TRUE on a hit
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_collide_ray (LrgPhysicsShape *shape,
                          const gfloat    *xf,
                          const gfloat    *origin,
                          const gfloat    *dir,
                          gfloat           max_fraction,
                          LrgRayHit       *hit);

/**
 * lrg_collide_cast:
 * @shape: the moving convex or compound shape
 * @xf_from: (array fixed-size=16): its transform at t = 0
 * @xf_to: (array fixed-size=16): its transform at t = 1
 * @target: the static target shape (any kind)
 * @xf_target: (array fixed-size=16): transform of @target
 * @toi: (out) (optional): time of impact in [0, 1]
 * @hit: (out caller-allocates) (optional): contact at the time of impact:
 *   fraction = toi, world point between the two surfaces, normal pointing
 *   from @target towards @shape, sub-shape/child of the target
 *
 * Sweeps @shape from @xf_from to @xf_to (translation interpolated
 * linearly, rotation by slerp) and finds the first time it comes within a
 * small tolerance (1 mm) of @target, by conservative advancement on GJK
 * distances.  Meshes and heightfields are swept triangle by triangle.  A
 * pair that already overlaps at t = 0 reports toi 0.
 *
 * Returns: %TRUE if the shapes touch during the sweep
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_collide_cast (LrgPhysicsShape *shape,
                           const gfloat    *xf_from,
                           const gfloat    *xf_to,
                           LrgPhysicsShape *target,
                           const gfloat    *xf_target,
                           gfloat          *toi,
                           LrgRayHit       *hit);

G_END_DECLS
