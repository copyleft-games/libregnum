/* lrg-collision-mesh.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCollisionMesh - a static triangle soup in a bounding volume
 * hierarchy, with transformed instances of other meshes for objects that
 * move. Ray casts, sphere and capsule sweeps and capsule overlap tests;
 * the collision layer under LrgCharacterController.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_COLLISION_MESH (lrg_collision_mesh_get_type ())

/**
 * LRG_COLLISION_MESH_INVALID_INSTANCE:
 *
 * Returned by lrg_collision_mesh_add_instance() when the input is
 * rejected.
 */
#define LRG_COLLISION_MESH_INVALID_INSTANCE (G_MAXUINT)

/**
 * LRG_COLLISION_MESH_MAX_TRIANGLES:
 *
 * Largest triangle count lrg_collision_mesh_set_triangles() accepts
 * (indices are 32-bit and the hierarchy needs headroom).
 */
#define LRG_COLLISION_MESH_MAX_TRIANGLES (G_MAXUINT32 / 4u)

/**
 * LrgCollisionHit:
 * @position: (array fixed-size=3): contact point on the surface hit
 * @normal: (array fixed-size=3): unit normal at the contact, facing the
 *   query (triangles are double-sided): for a ray it opposes the ray
 *   direction, for a sweep it points from the surface towards the
 *   swept shape
 * @distance: distance travelled along the ray or motion before contact
 * @triangle: index of the triangle hit within the mesh that owns it
 *   (the instance's shape for instanced triangles)
 * @tag: the triangle's tag given to lrg_collision_mesh_set_triangles()
 * @instance_id: the id given to lrg_collision_mesh_add_instance() for
 *   instanced triangles, 0 for the mesh's own triangles
 *
 * Result of a ray cast or sweep. Plain value type: copy it freely.
 */
typedef struct
{
    gfloat   position[3];
    gfloat   normal[3];
    gfloat   distance;
    guint    triangle;
    guint32  tag;
    guint64  instance_id;
} LrgCollisionHit;

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCollisionMesh, lrg_collision_mesh, LRG, COLLISION_MESH, GObject)

/**
 * lrg_collision_mesh_new:
 *
 * Creates an empty mesh: no triangles, no instances, every query misses.
 *
 * Returns: (transfer full): a new #LrgCollisionMesh
 */
LRG_AVAILABLE_IN_ALL
LrgCollisionMesh *
lrg_collision_mesh_new (void);

/**
 * lrg_collision_mesh_set_triangles:
 * @self: an #LrgCollisionMesh
 * @vertices: (array length=n_floats) (nullable): nine floats per
 *   triangle (three corners, x y z each); %NULL with @n_floats 0 clears
 * @n_floats: number of floats in @vertices, a multiple of nine
 * @tags: (array) (nullable): one tag per triangle, %NULL for all 0
 * @error: (nullable): return location for a #GError
 *
 * Replaces the mesh's own triangles with a copy of @vertices and builds
 * the bounding volume hierarchy (binned surface area heuristic, at most
 * four triangles per leaf). Instances are kept. Triangles with zero
 * area are kept out of the hierarchy (they can never be hit) but keep
 * their index. The build is deterministic: the same input always gives
 * the same hierarchy and the same query results.
 *
 * Errors (%G_IO_ERROR_INVALID_ARGUMENT): @n_floats is not a multiple of
 * nine, too many triangles, or a coordinate is not finite. On error
 * the previous triangles are kept.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_collision_mesh_set_triangles (LrgCollisionMesh  *self,
                                  const gfloat      *vertices,
                                  guint              n_floats,
                                  const guint32     *tags,
                                  GError           **error);

/**
 * lrg_collision_mesh_get_triangle_count:
 * @self: an #LrgCollisionMesh
 *
 * Returns: the number of the mesh's own triangles (instances excluded)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_collision_mesh_get_triangle_count (LrgCollisionMesh *self);

/**
 * lrg_collision_mesh_get_node_count:
 * @self: an #LrgCollisionMesh
 *
 * Returns: the number of hierarchy nodes over the own triangles (0 when
 *   empty); exposed for tests and statistics
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_collision_mesh_get_node_count (LrgCollisionMesh *self);

/**
 * lrg_collision_mesh_get_bounds:
 * @self: an #LrgCollisionMesh
 * @min: (out caller-allocates) (array fixed-size=3): minimum corner
 * @max: (out caller-allocates) (array fixed-size=3): maximum corner
 *
 * World bounds of the own triangles and every instance.
 *
 * Returns: %FALSE (and zero corners) when the mesh holds nothing
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_collision_mesh_get_bounds (LrgCollisionMesh *self,
                               gfloat           *min,
                               gfloat           *max);

/**
 * lrg_collision_mesh_translate:
 * @self: an #LrgCollisionMesh
 * @offset: (array fixed-size=3): displacement
 *
 * Moves the own triangles and every instance by @offset (a floating
 * origin shift). Cheaper than rebuilding: the hierarchy is shifted in
 * place.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_collision_mesh_translate (LrgCollisionMesh *self,
                              const gfloat     *offset);

/**
 * lrg_collision_mesh_add_instance:
 * @self: an #LrgCollisionMesh
 * @shape: a model-space mesh; a reference is taken, and later changes
 *   to it are seen by @self
 * @transform: (array fixed-size=16): model to world transform, 16
 *   floats, column-major (m[12..14] is the translation); must be finite
 *   and invertible
 * @instance_id: caller identifier reported in #LrgCollisionHit
 *
 * Places @shape in @self. Instances are tested after the own triangles
 * in ascending instance order. @shape must not be @self.
 *
 * Returns: an instance handle (stable until removed), or
 *   %LRG_COLLISION_MESH_INVALID_INSTANCE
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_collision_mesh_add_instance (LrgCollisionMesh *self,
                                 LrgCollisionMesh *shape,
                                 const gfloat     *transform,
                                 guint64           instance_id);

/**
 * lrg_collision_mesh_set_instance_transform:
 * @self: an #LrgCollisionMesh
 * @instance: a handle from lrg_collision_mesh_add_instance()
 * @transform: (array fixed-size=16): the new model to world transform
 *
 * Moves an instance (an opening door, a lift). A non-finite or
 * singular @transform is ignored.
 *
 * Returns: %TRUE when the instance exists and the transform was valid
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_collision_mesh_set_instance_transform (LrgCollisionMesh *self,
                                           guint             instance,
                                           const gfloat     *transform);

/**
 * lrg_collision_mesh_remove_instance:
 * @self: an #LrgCollisionMesh
 * @instance: a handle from lrg_collision_mesh_add_instance()
 *
 * Drops the instance and its reference on the shape. The handle may be
 * reused by a later lrg_collision_mesh_add_instance().
 *
 * Returns: %TRUE when the instance existed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_collision_mesh_remove_instance (LrgCollisionMesh *self,
                                    guint             instance);

/**
 * lrg_collision_mesh_get_instance_count:
 * @self: an #LrgCollisionMesh
 *
 * Returns: the number of live instances
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_collision_mesh_get_instance_count (LrgCollisionMesh *self);

/**
 * lrg_collision_mesh_raycast:
 * @self: an #LrgCollisionMesh
 * @origin: (array fixed-size=3): ray origin
 * @direction: (array fixed-size=3): ray direction, any non-zero length
 * @max_distance: longest hit accepted (world units)
 * @out_hit: (out caller-allocates) (optional): the nearest hit
 *
 * Nearest intersection with a triangle (double-sided) within
 * @max_distance along the normalised @direction. Ties keep the first
 * triangle found in the deterministic traversal order.
 *
 * Returns: %TRUE when something was hit
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_collision_mesh_raycast (LrgCollisionMesh *self,
                            const gfloat     *origin,
                            const gfloat     *direction,
                            gfloat            max_distance,
                             LrgCollisionHit  *out_hit);

/**
 * lrg_collision_mesh_sweep_box:
 * @self: an #LrgCollisionMesh
 * @center: (array fixed-size=3): starting world-space box center
 * @half_extents: (array fixed-size=3): nonnegative world-axis half sizes
 * @motion: (array fixed-size=3): complete world-space displacement
 * @out_hit: (out caller-allocates) (optional): nearest surface contact
 * @out_fraction: (out) (optional): normalized contact time in [0, 1]
 * @error: (nullable): return location for malformed query errors
 *
 * Continuously sweeps an axis-aligned box against BVH-selected world-space
 * triangles using thirteen-axis separating intervals. Instance transforms,
 * including mirrors and nonuniform scale, apply to triangles, not the box.
 * Initially touching geometry blocks only inward motion; initial strict
 * intersection reports time zero. End-time contact counts. Point, line and
 * rectangular boxes are permitted; an all-zero box delegates to raycast
 * semantics, while a stationary point misses. A stationary non-point box
 * reports only strict intersection. Bounds are finite and at most 1e12 in
 * magnitude. Outputs remain untouched on error or miss. Ties retain the first
 * triangle in deterministic BVH traversal order. No query allocation scales
 * with the complete triangle count.
 *
 * Returns: %TRUE on contact; %FALSE on miss or error (inspect @error)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_collision_mesh_sweep_box (LrgCollisionMesh *self,
                              const gfloat     *center,
                              const gfloat     *half_extents,
                              const gfloat     *motion,
                              LrgCollisionHit  *out_hit,
                              gdouble          *out_fraction,
                              GError          **error);

/**
 * lrg_collision_mesh_sweep_sphere:
 * @self: an #LrgCollisionMesh
 * @center: (array fixed-size=3): sphere centre at the start
 * @radius: sphere radius (> 0)
 * @motion: (array fixed-size=3): displacement to sweep along
 * @out_hit: (out caller-allocates) (optional): the first contact
 *
 * Moves a sphere along @motion and reports the first contact. A
 * triangle the sphere already touches at the start blocks only motion
 * that goes deeper into it (distance 0); motion away from or along it
 * passes, so a shape resting on a floor can slide over it.
 *
 * Returns: %TRUE when the motion is blocked before its end
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_collision_mesh_sweep_sphere (LrgCollisionMesh *self,
                                 const gfloat     *center,
                                 gfloat            radius,
                                 const gfloat     *motion,
                                 LrgCollisionHit  *out_hit);

/**
 * lrg_collision_mesh_sweep_capsule:
 * @self: an #LrgCollisionMesh
 * @base: (array fixed-size=3): bottom of the upright capsule (its
 *   lowest point, the feet of a character)
 * @radius: capsule radius (> 0)
 * @height: total capsule height from @base to its top; values below
 *   2 * @radius are treated as a sphere
 * @motion: (array fixed-size=3): displacement to sweep along
 * @out_hit: (out caller-allocates) (optional): the first contact
 *
 * Moves a Y-up capsule along @motion; contact semantics as
 * lrg_collision_mesh_sweep_sphere(). The time of impact comes from
 * conservative advancement on the convex segment-to-triangle distance,
 * so a fast capsule never tunnels through thin geometry.
 *
 * Returns: %TRUE when the motion is blocked before its end
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_collision_mesh_sweep_capsule (LrgCollisionMesh *self,
                                  const gfloat     *base,
                                  gfloat            radius,
                                  gfloat            height,
                                  const gfloat     *motion,
                                  LrgCollisionHit  *out_hit);

/**
 * lrg_collision_mesh_overlap_capsule:
 * @self: an #LrgCollisionMesh
 * @base: (array fixed-size=3): bottom of the upright capsule
 * @radius: capsule radius (> 0)
 * @height: total capsule height
 *
 * Returns: %TRUE when any triangle comes strictly closer than @radius
 *   to the capsule's axis (touching does not count)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_collision_mesh_overlap_capsule (LrgCollisionMesh *self,
                                    const gfloat     *base,
                                    gfloat            radius,
                                    gfloat            height);

G_END_DECLS
