/* lrg-physics-shape-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal view of LrgPhysicsShape shared by lrg-physics-shape.c and the
 * narrowphase in lrg-collide.c: the instance layout, the core support
 * function (without convex radius), supporting faces for manifold
 * clipping, and the triangle iterator of meshes and heightfields.
 */

#pragma once

#include <glib-object.h>
#include "physics/lrg-physics-shape.h"

G_BEGIN_DECLS

/* Largest polygon a supporting face may return (hull faces with more
 * vertices are truncated to this many, which only loses contact points). */
#define LRG_SHAPE_MAX_FACE_POINTS (32)

/* Number of polygon points used for a cylinder cap supporting face. */
#define LRG_SHAPE_CYLINDER_CAP_POINTS (8)

/*
 * LrgShapeBvhNode:
 *
 * One node of the AABB tree over mesh triangles.  An internal node has
 * count == 0 and its children at nodes[first] and nodes[first + 1]; a leaf
 * has count > 0 triangles at tri_order[first .. first + count).
 */
typedef struct
{
	gfloat  min[3];
	gfloat  max[3];
	guint32 first;
	guint32 count;
} LrgShapeBvhNode;

/*
 * LrgShapeChild:
 *
 * One compound child: the shape, its translation and rotation, the
 * composed rigid matrix and the child's bounds in the compound frame.
 */
typedef struct
{
	LrgPhysicsShape *shape;
	gfloat           position[3];
	gfloat           rotation[4];
	gfloat           xf[16];
	gfloat           bmin[3];
	gfloat           bmax[3];
} LrgShapeChild;

struct _LrgPhysicsShape
{
	GObject             parent_instance;

	LrgPhysicsShapeKind kind;

	/* primitives: radius (sphere, capsule, cylinder, hull convex radius),
	 * capsule/cylinder half height, box half extents */
	gfloat              radius;
	gfloat              half_height;
	gfloat              half[3];

	/* tight local bounds */
	gfloat              bmin[3];
	gfloat              bmax[3];

	/* largest distance of any surface point from the local origin */
	gfloat              max_extent;

	/* convex hull: vertices (3 floats each), faces as index runs into
	 * face_indices, face planes (unit normal + distance), and the
	 * precomputed density-1 mass properties */
	gfloat             *hull_verts;
	guint               n_hull_verts;
	guint              *face_indices;
	guint              *face_start;
	guint              *face_count;
	gfloat             *face_planes;
	guint               n_faces;
	gfloat              hull_volume;
	gfloat              hull_com[3];
	gfloat              hull_inertia[9];
	gfloat              hull_inner_radius;

	/* triangle mesh: welded-free vertex array, 3 indices per kept
	 * triangle, per-triangle material / user id / active-edge bits, and
	 * the AABB tree */
	gfloat             *mesh_verts;
	guint               n_mesh_verts;
	guint              *tris;
	guint               n_tris;
	guint              *tri_material;
	guint              *tri_user;
	guint8             *tri_active;
	LrgShapeBvhNode    *nodes;
	guint               n_nodes;
	guint              *tri_order;

	/* heightfield: scaled heights (row-major), sample counts, scale and
	 * active-edge bits per triangle */
	gfloat             *heights;
	guint               hf_nx;
	guint               hf_nz;
	gfloat              hf_scale[3];
	guint8             *hf_active;

	/* compound children */
	GArray             *children;

	/* plane */
	gfloat              plane_n[3];
	gfloat              plane_d;
};

/*
 * LrgShapeTriangleFunc:
 * @index: triangle id (mesh order, or heightfield cell * 2 + half)
 * @v: the 9 local coordinates of the corners
 * @material: per-triangle material id
 * @user_id: per-triangle user id
 * @active_edges: active-edge bits (bit 0 v0-v1, bit 1 v1-v2, bit 2 v2-v0)
 * @user_data: closure data
 *
 * Returns: %FALSE to stop the iteration
 */
typedef gboolean (*LrgShapeTriangleFunc) (guint         index,
                                          const gfloat *v,
                                          guint         material,
                                          guint         user_id,
                                          guint         active_edges,
                                          gpointer      user_data);

/*
 * lrg_physics_shape_foreach_triangle:
 *
 * Calls @func for every mesh or heightfield triangle whose bounds overlap
 * the local box [@bmin, @bmax], in a deterministic order.
 */
G_GNUC_INTERNAL
void lrg_physics_shape_foreach_triangle (LrgPhysicsShape      *self,
                                         const gfloat         *bmin,
                                         const gfloat         *bmax,
                                         LrgShapeTriangleFunc  func,
                                         gpointer              user_data);

/*
 * lrg_physics_shape_support_core:
 *
 * Support point of the shape's core (the shape shrunk by
 * lrg_physics_shape_core_radius()): a point for a sphere, the segment for
 * a capsule, the hull without its convex radius, the full box/cylinder.
 */
G_GNUC_INTERNAL
void lrg_physics_shape_support_core (LrgPhysicsShape *self,
                                     const gfloat    *dir,
                                     gfloat          *out);

/*
 * lrg_physics_shape_core_radius:
 *
 * Returns: the radius that lrg_physics_shape_support_core() leaves out
 */
G_GNUC_INTERNAL
gfloat lrg_physics_shape_core_radius (LrgPhysicsShape *self);

/*
 * lrg_physics_shape_supporting_face:
 * @self: a convex shape
 * @dir: direction (local)
 * @out: (out caller-allocates): up to LRG_SHAPE_MAX_FACE_POINTS * 3 floats
 * @face_id: (out): id of the face for contact feature ids
 *
 * Writes the polygon of the full shape (convex radius included) whose
 * outward normal points most along @dir, counter-clockwise seen from
 * outside: 1 point (sphere, a capsule end, a cylinder rim point), 2 points
 * (capsule side, cylinder side), or a polygon.
 *
 * Returns: the number of points written
 */
G_GNUC_INTERNAL
guint lrg_physics_shape_supporting_face (LrgPhysicsShape *self,
                                         const gfloat    *dir,
                                         gfloat          *out,
                                         guint           *face_id);

/*
 * lrg_physics_shape_get_child_internal:
 *
 * Returns: (transfer none) (nullable): child @index of a compound
 */
G_GNUC_INTERNAL
const LrgShapeChild * lrg_physics_shape_get_child_internal (LrgPhysicsShape *self,
                                                            guint            index);

/*
 * lrg_physics_shape_edge_is_active:
 *
 * Active-edge test shared by meshes and heightfields: @n1 is the unit
 * normal of the first triangle, @p1 a point on it, @n2 the normal of the
 * neighbour and @q the neighbour's vertex that is not on the edge.
 */
G_GNUC_INTERNAL
gboolean lrg_physics_shape_edge_is_active (const gfloat *n1,
                                           const gfloat *p1,
                                           const gfloat *n2,
                                           const gfloat *q,
                                           gfloat        cos_threshold);

/*
 * lrg_collide_internal_core_distance:
 *
 * Distance from the local point @point to the core of the convex @shape
 * (0 when inside), with the closest core point in @closest.  Implemented
 * by the GJK in lrg-collide.c; used for rounded-hull ray casts.
 */
G_GNUC_INTERNAL
gfloat lrg_collide_internal_core_distance (LrgPhysicsShape *shape,
                                           const gfloat    *point,
                                           gfloat          *closest);

G_END_DECLS
