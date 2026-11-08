/* lrg-physics-shape.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPhysicsShape: the immutable collision shape of a 3D rigid body.
 *
 * A shape is built once by one of the lrg_physics_shape_new_*()
 * constructors and is then shared (by reference) between any number of
 * bodies.  All geometry lives in the shape's local frame: Y is up, the
 * centre of a primitive is the local origin, and a rigid transform (a
 * column-major 4x4 matrix from lrg-math3d.h, rotation + translation, no
 * scale) places the shape in the world.
 *
 * Kinds:
 *  - sphere, capsule (segment along local Y + radius), cylinder (along
 *    local Y), box (half extents): analytic primitives;
 *  - convex hull: quickhull of a point cloud, coplanar triangles merged
 *    into polygon faces, optionally rounded by a convex radius;
 *  - triangle mesh: indexed triangles with an AABB tree, a material and a
 *    user id per triangle, and active-edge flags for internal-edge removal;
 *  - heightfield: a regular grid of heights with a scale, holes allowed;
 *  - compound: child shapes placed with a local translation and rotation;
 *  - plane: an infinite half space (solid below the surface).
 *
 * Meshes, heightfields and planes have no mass: they are for static (or
 * kinematic) bodies.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

/* ==========================================================================
 * Constants
 * ========================================================================== */

/**
 * LRG_PHYSICS_HEIGHTFIELD_HOLE:
 *
 * Height sample value that marks a hole in a heightfield.  Every grid cell
 * that touches a hole sample has no triangles.
 */
#define LRG_PHYSICS_HEIGHTFIELD_HOLE (G_MAXFLOAT)

/**
 * LRG_PHYSICS_PLANE_HALF_EXTENT:
 *
 * Half size of the square a plane shape reports as its bounds and draws as
 * its debug mesh.  Collision and ray casts treat the plane as infinite.
 */
#define LRG_PHYSICS_PLANE_HALF_EXTENT (1000.0f)

/**
 * LRG_PHYSICS_NO_BODY:
 *
 * Value of #LrgRayHit.body when no body is attached to the hit (the hit
 * came straight from a shape query).
 */
#define LRG_PHYSICS_NO_BODY (G_MAXUINT)

/**
 * LRG_PHYSICS_NO_SUB_SHAPE:
 *
 * Value of a sub-shape (triangle) id when the hit shape has no triangles.
 */
#define LRG_PHYSICS_NO_SUB_SHAPE (G_MAXUINT)

/**
 * LRG_PHYSICS_DEFAULT_ACTIVE_EDGE_ANGLE:
 *
 * Default threshold, in radians (5 degrees), for the active-edge test of
 * triangle meshes and heightfields: a convex edge between two triangles
 * whose normals differ by less than this angle is inactive (internal) and
 * never pushes a body sideways.
 */
#define LRG_PHYSICS_DEFAULT_ACTIVE_EDGE_ANGLE (0.0872664626f)

/* ==========================================================================
 * Enums and errors
 * ========================================================================== */

/**
 * LrgPhysicsShapeKind:
 * @LRG_PHYSICS_SHAPE_KIND_SPHERE: sphere of a radius around the origin
 * @LRG_PHYSICS_SHAPE_KIND_CAPSULE: segment from -half_height to
 *   +half_height on local Y, expanded by a radius
 * @LRG_PHYSICS_SHAPE_KIND_CYLINDER: cylinder along local Y
 * @LRG_PHYSICS_SHAPE_KIND_BOX: box of half extents
 * @LRG_PHYSICS_SHAPE_KIND_CONVEX_HULL: convex hull of points (+ radius)
 * @LRG_PHYSICS_SHAPE_KIND_TRIANGLE_MESH: indexed triangle soup with a BVH
 * @LRG_PHYSICS_SHAPE_KIND_HEIGHTFIELD: regular height grid
 * @LRG_PHYSICS_SHAPE_KIND_COMPOUND: children with local transforms
 * @LRG_PHYSICS_SHAPE_KIND_PLANE: infinite half space
 *
 * The geometry kind of an #LrgPhysicsShape.
 */
typedef enum
{
	LRG_PHYSICS_SHAPE_KIND_SPHERE = 0,
	LRG_PHYSICS_SHAPE_KIND_CAPSULE,
	LRG_PHYSICS_SHAPE_KIND_CYLINDER,
	LRG_PHYSICS_SHAPE_KIND_BOX,
	LRG_PHYSICS_SHAPE_KIND_CONVEX_HULL,
	LRG_PHYSICS_SHAPE_KIND_TRIANGLE_MESH,
	LRG_PHYSICS_SHAPE_KIND_HEIGHTFIELD,
	LRG_PHYSICS_SHAPE_KIND_COMPOUND,
	LRG_PHYSICS_SHAPE_KIND_PLANE
} LrgPhysicsShapeKind;

/**
 * lrg_physics_shape_kind_get_type:
 *
 * Returns: the #GType of #LrgPhysicsShapeKind
 */
LRG_AVAILABLE_IN_ALL
GType lrg_physics_shape_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PHYSICS_SHAPE_KIND (lrg_physics_shape_kind_get_type ())

/**
 * LRG_PHYSICS_SHAPE_ERROR:
 *
 * Error domain of the shape constructors that take point data.
 */
#define LRG_PHYSICS_SHAPE_ERROR (lrg_physics_shape_error_quark ())

/**
 * LrgPhysicsShapeError:
 * @LRG_PHYSICS_SHAPE_ERROR_INVALID: an argument is non-finite, negative or
 *   the arrays have the wrong length
 * @LRG_PHYSICS_SHAPE_ERROR_DEGENERATE: the input has no volume (convex
 *   hull of coplanar points) or no usable triangles
 *
 * Errors of #LRG_PHYSICS_SHAPE_ERROR.
 */
typedef enum
{
	LRG_PHYSICS_SHAPE_ERROR_INVALID = 1,
	LRG_PHYSICS_SHAPE_ERROR_DEGENERATE
} LrgPhysicsShapeError;

/**
 * lrg_physics_shape_error_get_type:
 *
 * Returns: the #GType of #LrgPhysicsShapeError
 */
LRG_AVAILABLE_IN_ALL
GType lrg_physics_shape_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PHYSICS_SHAPE_ERROR (lrg_physics_shape_error_get_type ())

/**
 * lrg_physics_shape_error_quark:
 *
 * Returns: the error quark of #LRG_PHYSICS_SHAPE_ERROR
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_physics_shape_error_quark (void);

/* ==========================================================================
 * LrgRayHit
 * ========================================================================== */

#define LRG_TYPE_RAY_HIT (lrg_ray_hit_get_type ())

/**
 * LrgRayHit:
 * @fraction: hit parameter t: the hit point is origin + t * direction (for a
 *   shape cast: the time of impact in [0, 1])
 * @point: hit point (local space for a shape query, world space for a
 *   world or lrg_collide_*() query)
 * @normal: unit surface normal at the hit, pointing out of the hit shape
 * @body: body id filled in by #LrgDynamicsWorld queries, otherwise
 *   %LRG_PHYSICS_NO_BODY
 * @child: index of the compound child that was hit (top level), or -1
 * @sub_shape: triangle index in a mesh or heightfield, or
 *   %LRG_PHYSICS_NO_SUB_SHAPE
 * @material: per-triangle material id of a mesh hit (0 otherwise)
 * @user_id: per-triangle user id of a mesh hit (0 otherwise)
 *
 * Result of a ray cast or shape cast.  A plain boxed struct: copy it by
 * value.
 */
typedef struct
{
	gfloat fraction;
	gfloat point[3];
	gfloat normal[3];
	guint  body;
	gint   child;
	guint  sub_shape;
	guint  material;
	guint  user_id;
} LrgRayHit;

/**
 * lrg_ray_hit_get_type:
 *
 * Returns: the boxed #GType of #LrgRayHit
 */
LRG_AVAILABLE_IN_ALL
GType lrg_ray_hit_get_type (void) G_GNUC_CONST;

/**
 * lrg_ray_hit_new:
 *
 * Allocates a hit reset with lrg_ray_hit_reset().
 *
 * Returns: (transfer full): a new #LrgRayHit
 */
LRG_AVAILABLE_IN_ALL
LrgRayHit * lrg_ray_hit_new (void);

/**
 * lrg_ray_hit_copy:
 * @self: a #LrgRayHit
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgRayHit * lrg_ray_hit_copy (const LrgRayHit *self);

/**
 * lrg_ray_hit_free:
 * @self: (nullable): a #LrgRayHit
 *
 * Frees a hit made by lrg_ray_hit_new() or lrg_ray_hit_copy().
 */
LRG_AVAILABLE_IN_ALL
void lrg_ray_hit_free (LrgRayHit *self);

/**
 * lrg_ray_hit_reset:
 * @self: a #LrgRayHit
 *
 * Resets @self to "no hit": fraction %G_MAXFLOAT, zero point and normal,
 * body %LRG_PHYSICS_NO_BODY, child -1, sub-shape %LRG_PHYSICS_NO_SUB_SHAPE.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ray_hit_reset (LrgRayHit *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgRayHit, lrg_ray_hit_free)

/* ==========================================================================
 * LrgPhysicsShape
 * ========================================================================== */

#define LRG_TYPE_PHYSICS_SHAPE (lrg_physics_shape_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPhysicsShape, lrg_physics_shape, LRG, PHYSICS_SHAPE, GObject)

/* --- Constructors --- */

/**
 * lrg_physics_shape_new_sphere:
 * @radius: radius, > 0
 *
 * Creates a sphere centred on the local origin.  A non-finite or
 * non-positive radius is clamped to 0.001.
 *
 * Returns: (transfer full): a new sphere shape
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsShape * lrg_physics_shape_new_sphere (gfloat radius);

/**
 * lrg_physics_shape_new_capsule:
 * @radius: radius, > 0
 * @half_height: half length of the inner segment along local Y, >= 0 (the
 *   capsule's total height is 2 * (half_height + radius))
 *
 * Creates a capsule along local Y.  Invalid values are clamped (radius to
 * 0.001, half height to 0).
 *
 * Returns: (transfer full): a new capsule shape
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsShape * lrg_physics_shape_new_capsule (gfloat radius,
                                                 gfloat half_height);

/**
 * lrg_physics_shape_new_cylinder:
 * @radius: radius, > 0
 * @half_height: half height along local Y, > 0
 *
 * Creates a flat-capped cylinder along local Y.  Invalid values are
 * clamped to 0.001.
 *
 * Returns: (transfer full): a new cylinder shape
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsShape * lrg_physics_shape_new_cylinder (gfloat radius,
                                                  gfloat half_height);

/**
 * lrg_physics_shape_new_box:
 * @half_x: half extent along local X, > 0
 * @half_y: half extent along local Y, > 0
 * @half_z: half extent along local Z, > 0
 *
 * Creates a box centred on the local origin.  Invalid extents are clamped
 * to 0.001.
 *
 * Returns: (transfer full): a new box shape
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsShape * lrg_physics_shape_new_box (gfloat half_x,
                                             gfloat half_y,
                                             gfloat half_z);

/**
 * lrg_physics_shape_new_convex_hull:
 * @points: (array length=n_floats): point cloud, 3 floats per point
 * @n_floats: number of floats in @points (a multiple of 3, at least 12)
 * @radius: convex radius added around the hull (0 for sharp edges)
 * @error: return location for a #GError
 *
 * Builds the convex hull of @points with quickhull.  Points inside the
 * hull are dropped; coplanar triangles are merged into polygon faces (a
 * cube gives 8 vertices and 6 quads).  The hull keeps the input frame: it
 * is not recentred on its centre of mass (see
 * lrg_physics_shape_get_mass_properties()).
 *
 * Returns: (transfer full) (nullable): a new hull, or %NULL with @error set
 *   (%LRG_PHYSICS_SHAPE_ERROR_DEGENERATE for coplanar or too few points)
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsShape * lrg_physics_shape_new_convex_hull (const gfloat  *points,
                                                     guint          n_floats,
                                                     gfloat         radius,
                                                     GError       **error);

/**
 * lrg_physics_shape_new_triangle_mesh:
 * @vertices: (array length=n_floats): vertex positions, 3 floats each
 * @n_floats: number of floats in @vertices
 * @indices: (array length=n_indices): 3 vertex indices per triangle
 * @n_indices: number of indices (a multiple of 3)
 * @error: return location for a #GError
 *
 * Creates a triangle mesh with material and user id 0 on every triangle
 * and the default active-edge angle.  See
 * lrg_physics_shape_new_triangle_mesh_full().
 *
 * Returns: (transfer full) (nullable): a new mesh, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsShape * lrg_physics_shape_new_triangle_mesh (const gfloat  *vertices,
                                                       guint          n_floats,
                                                       const guint   *indices,
                                                       guint          n_indices,
                                                       GError       **error);

/**
 * lrg_physics_shape_new_triangle_mesh_full:
 * @vertices: (array length=n_floats): vertex positions, 3 floats each
 * @n_floats: number of floats in @vertices
 * @indices: (array length=n_indices): 3 vertex indices per triangle
 * @n_indices: number of indices (a multiple of 3)
 * @materials: (array length=n_triangles) (nullable): material id per
 *   triangle, or %NULL for 0
 * @user_ids: (array length=n_triangles) (nullable): user id per triangle,
 *   or %NULL for 0
 * @n_triangles: length of @materials / @user_ids (n_indices / 3, or 0 when
 *   both are %NULL)
 * @active_edge_angle: threshold angle in radians for active edges (see
 *   %LRG_PHYSICS_DEFAULT_ACTIVE_EDGE_ANGLE)
 * @error: return location for a #GError
 *
 * Creates a triangle mesh.  Degenerate triangles (zero area) are dropped;
 * the triangle index reported by queries is the index into the kept
 * triangles in input order.  Vertices with identical positions are welded
 * for the edge adjacency used by the active-edge flags: an edge is active
 * when it is a boundary or non-manifold edge, or a convex edge sharper than
 * @active_edge_angle; concave and near-flat edges are inactive and never
 * produce sideways ("ghost") contact normals.  An AABB tree over the
 * triangles serves every query.  Triangles are two-sided.
 *
 * Returns: (transfer full) (nullable): a new mesh, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsShape * lrg_physics_shape_new_triangle_mesh_full (const gfloat  *vertices,
                                                            guint          n_floats,
                                                            const guint   *indices,
                                                            guint          n_indices,
                                                            const guint   *materials,
                                                            const guint   *user_ids,
                                                            guint          n_triangles,
                                                            gfloat         active_edge_angle,
                                                            GError       **error);

/**
 * lrg_physics_shape_new_heightfield:
 * @heights: (array length=n_heights): height samples, row-major
 *   (index = z * @n_x + x); %LRG_PHYSICS_HEIGHTFIELD_HOLE marks a hole
 * @n_heights: number of samples (@n_x * @n_z)
 * @n_x: samples along local X, >= 2
 * @n_z: samples along local Z, >= 2
 * @scale_x: spacing between samples along X, > 0
 * @scale_y: multiplier applied to every height, > 0
 * @scale_z: spacing between samples along Z, > 0
 * @error: return location for a #GError
 *
 * Creates a heightfield.  Sample (x, z) sits at local
 * (x * @scale_x, height * @scale_y, z * @scale_z): the grid starts at the
 * local origin and grows towards +X and +Z.  Each cell is split into two
 * triangles along the diagonal from (x, z) to (x + 1, z + 1); triangle id
 * = (z * (n_x - 1) + x) * 2 + (0 or 1).  Internal edges get the same
 * active-edge treatment as meshes.
 *
 * Returns: (transfer full) (nullable): a new heightfield, or %NULL with
 *   @error set
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsShape * lrg_physics_shape_new_heightfield (const gfloat  *heights,
                                                     guint          n_heights,
                                                     guint          n_x,
                                                     guint          n_z,
                                                     gfloat         scale_x,
                                                     gfloat         scale_y,
                                                     gfloat         scale_z,
                                                     GError       **error);

/**
 * lrg_physics_shape_new_compound:
 *
 * Creates an empty compound.  Add children with
 * lrg_physics_shape_compound_add_child() before giving the compound to a
 * body; a compound is immutable once it is in use.
 *
 * Returns: (transfer full): a new, empty compound shape
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsShape * lrg_physics_shape_new_compound (void);

/**
 * lrg_physics_shape_compound_add_child:
 * @self: a compound shape
 * @child: the child shape (any kind but plane; compounds may nest)
 * @position: (array fixed-size=3): child translation in the compound frame
 * @rotation: (array fixed-size=4) (nullable): child rotation quaternion
 *   (x, y, z, w), or %NULL for identity
 *
 * Appends a child.  The compound holds a reference on @child.
 *
 * Returns: the child index, or -1 if @self is not a compound, @child is a
 *   plane, @child is @self, or a value is non-finite
 */
LRG_AVAILABLE_IN_ALL
gint lrg_physics_shape_compound_add_child (LrgPhysicsShape *self,
                                           LrgPhysicsShape *child,
                                           const gfloat    *position,
                                           const gfloat    *rotation);

/**
 * lrg_physics_shape_new_plane:
 * @normal: (array fixed-size=3): plane normal (normalised internally)
 * @distance: signed distance of the plane from the origin along @normal
 *
 * Creates an infinite plane: the surface is normal . p = distance and the
 * solid side is normal . p < distance.  A zero normal becomes +Y.
 *
 * Returns: (transfer full): a new plane shape
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsShape * lrg_physics_shape_new_plane (const gfloat *normal,
                                               gfloat        distance);

/* --- Description --- */

/**
 * lrg_physics_shape_get_kind:
 * @self: a #LrgPhysicsShape
 *
 * Returns: the geometry kind
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsShapeKind lrg_physics_shape_get_kind (LrgPhysicsShape *self);

/**
 * lrg_physics_shape_is_convex:
 * @self: a #LrgPhysicsShape
 *
 * Returns: %TRUE for sphere, capsule, cylinder, box and convex hull (the
 *   kinds with a support function)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_shape_is_convex (LrgPhysicsShape *self);

/**
 * lrg_physics_shape_get_radius:
 * @self: a #LrgPhysicsShape
 *
 * Returns: the radius of a sphere, capsule or cylinder, the convex radius
 *   of a hull, 0 for the other kinds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_physics_shape_get_radius (LrgPhysicsShape *self);

/**
 * lrg_physics_shape_get_half_height:
 * @self: a #LrgPhysicsShape
 *
 * Returns: the half height of a capsule's segment or of a cylinder, 0
 *   otherwise
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_physics_shape_get_half_height (LrgPhysicsShape *self);

/**
 * lrg_physics_shape_get_half_extents:
 * @self: a #LrgPhysicsShape
 * @out: (out caller-allocates) (array fixed-size=3): half extents of a
 *   box; for other kinds the half size of the local bounds
 *
 * Reads the box half extents.
 */
LRG_AVAILABLE_IN_ALL
void lrg_physics_shape_get_half_extents (LrgPhysicsShape *self,
                                         gfloat          *out);

/**
 * lrg_physics_shape_get_plane:
 * @self: a plane shape
 * @normal: (out caller-allocates) (array fixed-size=3): unit normal
 * @distance: (out) (optional): distance along the normal
 *
 * Returns: %TRUE if @self is a plane (the outputs are untouched otherwise)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_shape_get_plane (LrgPhysicsShape *self,
                                      gfloat          *normal,
                                      gfloat          *distance);

/**
 * lrg_physics_shape_get_hull_vertices:
 * @self: a convex hull shape
 * @n_floats: (out): number of floats returned (3 per vertex)
 *
 * Returns: (transfer none) (array length=n_floats) (nullable): the hull
 *   vertices, or %NULL (and 0) if @self is not a hull
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_physics_shape_get_hull_vertices (LrgPhysicsShape *self,
                                                    guint           *n_floats);

/**
 * lrg_physics_shape_get_hull_face_count:
 * @self: a convex hull shape
 *
 * Returns: number of polygon faces of the hull (0 for other kinds)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_physics_shape_get_hull_face_count (LrgPhysicsShape *self);

/**
 * lrg_physics_shape_get_hull_face:
 * @self: a convex hull shape
 * @index: face index
 * @n_indices: (out): number of vertex indices in the face
 * @plane: (out caller-allocates) (array fixed-size=4) (optional): face
 *   plane (unit outward normal xyz, distance w)
 *
 * Reads one face: vertex indices into lrg_physics_shape_get_hull_vertices()
 * in counter-clockwise order seen from outside.
 *
 * Returns: (transfer none) (array length=n_indices) (nullable): the face's
 *   vertex indices, or %NULL if out of range
 */
LRG_AVAILABLE_IN_ALL
const guint * lrg_physics_shape_get_hull_face (LrgPhysicsShape *self,
                                               guint            index,
                                               guint           *n_indices,
                                               gfloat          *plane);

/**
 * lrg_physics_shape_get_triangle_count:
 * @self: a mesh or heightfield shape
 *
 * Returns: the number of triangles (heightfields count hole cells too:
 *   their triangles are reported as absent by
 *   lrg_physics_shape_get_triangle()); 0 for other kinds
 */
LRG_AVAILABLE_IN_ALL
guint lrg_physics_shape_get_triangle_count (LrgPhysicsShape *self);

/**
 * lrg_physics_shape_get_triangle:
 * @self: a mesh or heightfield shape
 * @index: triangle index
 * @vertices: (out caller-allocates) (array fixed-size=9): the three
 *   corners in local space
 * @material: (out) (optional): per-triangle material id
 * @user_id: (out) (optional): per-triangle user id
 * @active_edges: (out) (optional): active-edge bits (bit 0 edge v0-v1,
 *   bit 1 edge v1-v2, bit 2 edge v2-v0)
 *
 * Reads one triangle.
 *
 * Returns: %TRUE if the triangle exists (not out of range, not in a hole)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_shape_get_triangle (LrgPhysicsShape *self,
                                         guint            index,
                                         gfloat          *vertices,
                                         guint           *material,
                                         guint           *user_id,
                                         guint           *active_edges);

/**
 * lrg_physics_shape_get_heightfield_size:
 * @self: a heightfield shape
 * @n_x: (out) (optional): samples along X
 * @n_z: (out) (optional): samples along Z
 * @scale: (out caller-allocates) (array fixed-size=3) (optional): the
 *   (x, y, z) scale
 *
 * Returns: %TRUE if @self is a heightfield
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_shape_get_heightfield_size (LrgPhysicsShape *self,
                                                 guint           *n_x,
                                                 guint           *n_z,
                                                 gfloat          *scale);

/**
 * lrg_physics_shape_get_heightfield_height:
 * @self: a heightfield shape
 * @x: sample column
 * @z: sample row
 *
 * Returns: the scaled local height of sample (@x, @z), or
 *   %LRG_PHYSICS_HEIGHTFIELD_HOLE for a hole or an out-of-range sample
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_physics_shape_get_heightfield_height (LrgPhysicsShape *self,
                                                 guint            x,
                                                 guint            z);

/**
 * lrg_physics_shape_get_child_count:
 * @self: a compound shape
 *
 * Returns: number of children (0 for other kinds)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_physics_shape_get_child_count (LrgPhysicsShape *self);

/**
 * lrg_physics_shape_get_child:
 * @self: a compound shape
 * @index: child index
 * @position: (out caller-allocates) (array fixed-size=3) (optional): child
 *   translation
 * @rotation: (out caller-allocates) (array fixed-size=4) (optional): child
 *   rotation quaternion (x, y, z, w)
 *
 * Returns: (transfer none) (nullable): the child shape, or %NULL if out of
 *   range
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsShape * lrg_physics_shape_get_child (LrgPhysicsShape *self,
                                               guint            index,
                                               gfloat          *position,
                                               gfloat          *rotation);

/* --- Mass, bounds, support, rays --- */

/**
 * lrg_physics_shape_get_volume:
 * @self: a #LrgPhysicsShape
 *
 * Returns: the solid volume (0 for mesh, heightfield and plane; a hull's
 *   convex radius is not included)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_physics_shape_get_volume (LrgPhysicsShape *self);

/**
 * lrg_physics_shape_get_mass_properties:
 * @self: a #LrgPhysicsShape
 * @density: mass per unit volume, > 0
 * @mass: (out) (optional): density * volume
 * @com: (out caller-allocates) (array fixed-size=3) (optional): centre of
 *   mass in the local frame
 * @inertia: (out caller-allocates) (array fixed-size=9) (optional):
 *   inertia tensor about the centre of mass, local axes, column-major 3x3
 *   (symmetric, so row-major reads the same)
 *
 * Computes uniform-density mass properties.  Primitives use the closed
 * forms; hulls integrate tetrahedra from the centroid; compounds sum their
 * children with the parallel axis theorem.  Outputs are untouched on
 * failure.
 *
 * Returns: %FALSE for mesh, heightfield, plane, an empty compound or a
 *   non-positive density
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_shape_get_mass_properties (LrgPhysicsShape *self,
                                                gfloat           density,
                                                gfloat          *mass,
                                                gfloat          *com,
                                                gfloat          *inertia);

/**
 * lrg_physics_shape_get_local_bounds:
 * @self: a #LrgPhysicsShape
 * @min: (out caller-allocates) (array fixed-size=3): bounds minimum
 * @max: (out caller-allocates) (array fixed-size=3): bounds maximum
 *
 * Reads the tight local-space AABB (planes use
 * %LRG_PHYSICS_PLANE_HALF_EXTENT).
 */
LRG_AVAILABLE_IN_ALL
void lrg_physics_shape_get_local_bounds (LrgPhysicsShape *self,
                                         gfloat          *min,
                                         gfloat          *max);

/**
 * lrg_physics_shape_get_world_bounds:
 * @self: a #LrgPhysicsShape
 * @xf: (array fixed-size=16): rigid world transform
 * @min: (out caller-allocates) (array fixed-size=3): bounds minimum
 * @max: (out caller-allocates) (array fixed-size=3): bounds maximum
 *
 * Computes a world AABB that contains the transformed shape: exact for
 * spheres and capsules, the AABB of the rotated local box otherwise.
 */
LRG_AVAILABLE_IN_ALL
void lrg_physics_shape_get_world_bounds (LrgPhysicsShape *self,
                                         const gfloat    *xf,
                                         gfloat          *min,
                                         gfloat          *max);

/**
 * lrg_physics_shape_get_inner_radius:
 * @self: a #LrgPhysicsShape
 *
 * Radius of the largest sphere around the centre that fits inside the
 * shape (half the smallest box extent, the radius of a capsule, ...).
 * Continuous collision uses it to decide when a body moves "too far" in a
 * step.  Mesh, heightfield and plane return 0.
 *
 * Returns: the inner radius
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_physics_shape_get_inner_radius (LrgPhysicsShape *self);

/**
 * lrg_physics_shape_support:
 * @self: a convex #LrgPhysicsShape
 * @dir: (array fixed-size=3): direction (need not be unit length)
 * @out: (out caller-allocates) (array fixed-size=3): the point of the
 *   shape furthest along @dir, local space, convex radius included
 *
 * Evaluates the support function used by GJK and EPA.
 *
 * Returns: %FALSE (and @out zeroed) if @self is not convex
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_shape_support (LrgPhysicsShape *self,
                                    const gfloat    *dir,
                                    gfloat          *out);

/**
 * lrg_physics_shape_cast_ray:
 * @self: a #LrgPhysicsShape
 * @origin: (array fixed-size=3): ray origin, local space
 * @dir: (array fixed-size=3): ray direction (its length is the unit of
 *   the fraction)
 * @max_fraction: largest t to accept
 * @hit: (out caller-allocates) (optional): the closest hit (local space)
 *
 * Casts the ray origin + t * dir, t in [0, @max_fraction], against the
 * shape in its local frame.  A ray that starts inside a solid convex
 * shape (or below a plane) hits at t = 0 with normal -dir.  Mesh and
 * heightfield triangles are two-sided.
 *
 * Returns: %TRUE on a hit (then @hit is filled; otherwise untouched)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_shape_cast_ray (LrgPhysicsShape *self,
                                     const gfloat    *origin,
                                     const gfloat    *dir,
                                     gfloat           max_fraction,
                                     LrgRayHit       *hit);

/**
 * lrg_physics_shape_build_debug_mesh:
 * @self: a #LrgPhysicsShape
 * @segments: tessellation of round shapes (circle segments, >= 3)
 *
 * Triangulates the shape for debug drawing and editor gizmos: a triangle
 * soup in local space, 9 floats per triangle, counter-clockwise seen from
 * outside.  Compound children are transformed into the compound frame;
 * planes become a square of %LRG_PHYSICS_PLANE_HALF_EXTENT.
 *
 * Returns: (transfer full) (element-type gfloat): the triangle soup
 */
LRG_AVAILABLE_IN_ALL
GArray * lrg_physics_shape_build_debug_mesh (LrgPhysicsShape *self,
                                             guint            segments);

G_END_DECLS
