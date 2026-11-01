/* lrg-solid-geometry.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSolidGeometry - retained immutable 3D solids. Sphere, box, sphyl
 * (capsule along local +Y), convex hull, flat cylinder along local +Z
 * and aggregate. Queries report outside, touching and inside, not
 * triangle-surface hits. A cylinder is not a sphyl.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_SOLID_GEOMETRY (lrg_solid_geometry_get_type ())

/**
 * LRG_SOLID_MAX_CONVEX_POINTS:
 *
 * Most points lrg_solid_geometry_new_convex() accepts.
 */
#define LRG_SOLID_MAX_CONVEX_POINTS (32u)

/**
 * LRG_SOLID_MAX_AGGREGATE_CHILDREN:
 *
 * Most direct children lrg_solid_geometry_new_aggregate() accepts.
 */
#define LRG_SOLID_MAX_AGGREGATE_CHILDREN (64u)

/**
 * LRG_SOLID_MAX_PRIMITIVES:
 *
 * Most flattened primitives one geometry may retain.
 */
#define LRG_SOLID_MAX_PRIMITIVES (256u)

/**
 * LRG_SOLID_MAX_DEPTH:
 *
 * Most nested aggregate levels, counting the leaf as one.
 */
#define LRG_SOLID_MAX_DEPTH (8u)

/**
 * LrgSolidShape:
 * @LRG_SOLID_SHAPE_SPHERE: one sphere
 * @LRG_SOLID_SHAPE_BOX: one oriented box
 * @LRG_SOLID_SHAPE_SPHYL: one capsule, cylinder axis local +Y
 * @LRG_SOLID_SHAPE_CONVEX: one convex hull
 * @LRG_SOLID_SHAPE_AGGREGATE: union of retained children
 * @LRG_SOLID_SHAPE_CYLINDER: one flat-ended cylinder, axis local +Z
 *
 * The kind the constructor admitted. An aggregate of one sphere stays
 * an aggregate. A cylinder is not a sphyl: the ends are flat disks.
 */
typedef enum {
    LRG_SOLID_SHAPE_SPHERE = 1,
    LRG_SOLID_SHAPE_BOX = 2,
    LRG_SOLID_SHAPE_SPHYL = 3,
    LRG_SOLID_SHAPE_CONVEX = 4,
    LRG_SOLID_SHAPE_AGGREGATE = 5,
    LRG_SOLID_SHAPE_CYLINDER = 6
} LrgSolidShape;

/**
 * LrgSolidClass:
 * @LRG_SOLID_CLASS_OUTSIDE: strictly outside the solid
 * @LRG_SOLID_CLASS_TOUCHING: within 1e-5 of the surface
 * @LRG_SOLID_CLASS_INSIDE: strictly inside the solid
 *
 * Point classification against a closed solid. Touching is not outside.
 */
typedef enum {
    LRG_SOLID_CLASS_OUTSIDE = 0,
    LRG_SOLID_CLASS_TOUCHING = 1,
    LRG_SOLID_CLASS_INSIDE = 2
} LrgSolidClass;

/**
 * LrgSolidContact:
 * @point: (array fixed-size=3): a point on the first solid's surface
 * @normal: (array fixed-size=3): unit direction that translates the
 *   second solid out of the first
 * @depth: penetration distance along @normal, 0 when the solids touch
 * @primitive_a: flattened primitive index in the first geometry
 * @primitive_b: flattened primitive index in the second geometry
 *
 * One contact. Plain value type: copy it freely. Equal depths keep the
 * lower primitive index, first geometry then second.
 */
typedef struct
{
    gfloat point[3];
    gfloat normal[3];
    gfloat depth;
    guint  primitive_a;
    guint  primitive_b;
} LrgSolidContact;

/**
 * LrgSolidHit:
 * @position: (array fixed-size=3): contact point on the static surface
 * @normal: (array fixed-size=3): unit direction that pushes the moving
 *   sphere back out of the static solid
 * @distance: distance the sphere center travels before contact
 * @time: fraction of the motion, in [0, 1]
 * @primitive: flattened primitive of the static geometry
 *
 * Earliest sphere sweep hit. A tie keeps the lower primitive index.
 */
typedef struct
{
    gfloat position[3];
    gfloat normal[3];
    gfloat distance;
    gfloat time;
    guint  primitive;
} LrgSolidHit;

LRG_AVAILABLE_IN_ALL
GType lrg_solid_shape_get_type (void);
#define LRG_TYPE_SOLID_SHAPE (lrg_solid_shape_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_solid_class_get_type (void);
#define LRG_TYPE_SOLID_CLASS (lrg_solid_class_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSolidGeometry, lrg_solid_geometry, LRG, SOLID_GEOMETRY, GObject)

/**
 * lrg_solid_geometry_new_sphere:
 * @radius: positive finite radius
 * @transform: (array fixed-size=16) (nullable): column-major model
 *   transform, or %NULL for identity. Uniform positive scale, rotation
 *   and translation only
 * @error: (nullable): return location for a #GError
 *
 * Retains one sphere. The engine's 3D axis is Y-up, matching
 * #LrgCollisionMesh capsules. A nonuniform scale, reflection, shear or
 * non-finite value is rejected and no object is returned.
 *
 * Returns: (transfer full): a new #LrgSolidGeometry
 */
LRG_AVAILABLE_IN_ALL
LrgSolidGeometry *
lrg_solid_geometry_new_sphere (gfloat        radius,
                               const gfloat *transform,
                               GError      **error);

/**
 * lrg_solid_geometry_new_box:
 * @half_extents: (array fixed-size=3): positive finite half extents
 *   along local X, Y and Z
 * @transform: (array fixed-size=16) (nullable): column-major model
 *   transform, or %NULL for identity
 * @error: (nullable): return location for a #GError
 *
 * Retains one oriented box. The local transform is baked in. The same
 * scale rules as lrg_solid_geometry_new_sphere() apply.
 *
 * Returns: (transfer full): a new #LrgSolidGeometry
 */
LRG_AVAILABLE_IN_ALL
LrgSolidGeometry *
lrg_solid_geometry_new_box (const gfloat *half_extents,
                            const gfloat *transform,
                            GError      **error);

/**
 * lrg_solid_geometry_new_sphyl:
 * @radius: positive finite cap radius
 * @half_length: finite cylindrical half-length along local +Y, at
 *   least 0. Zero is a sphere
 * @transform: (array fixed-size=16) (nullable): column-major model
 *   transform, or %NULL for identity
 * @error: (nullable): return location for a #GError
 *
 * Retains one sphyl: the Minkowski sum of a Y segment and a sphere.
 *
 * Returns: (transfer full): a new #LrgSolidGeometry
 */
LRG_AVAILABLE_IN_ALL
LrgSolidGeometry *
lrg_solid_geometry_new_sphyl (gfloat        radius,
                              gfloat        half_length,
                              const gfloat *transform,
                              GError      **error);

/**
 * lrg_solid_geometry_new_cylinder:
 * @radius: positive finite radius of the flat disk
 * @half_height: positive finite half-height along local +Z
 * @transform: (array fixed-size=16) (nullable): column-major model
 *   transform, or %NULL for identity
 * @error: (nullable): return location for a #GError
 *
 * Retains one flat-ended cylinder. The local axis is +Z. This is not a
 * sphyl and it is not moved onto the engine's +Y up axis. A zero
 * half-height is rejected. The same scale rules as
 * lrg_solid_geometry_new_sphere() apply.
 *
 * Returns: (transfer full): a new #LrgSolidGeometry
 */
LRG_AVAILABLE_IN_ALL
LrgSolidGeometry *
lrg_solid_geometry_new_cylinder (gfloat        radius,
                                 gfloat        half_height,
                                 const gfloat *transform,
                                 GError      **error);

/**
 * lrg_solid_geometry_new_convex:
 * @points: (array length=n_floats): x, y, z triples in local space
 * @n_floats: number of floats, a multiple of three, 4 to
 *   %LRG_SOLID_MAX_CONVEX_POINTS points after duplicate removal
 * @transform: (array fixed-size=16) (nullable): column-major model
 *   transform, or %NULL for identity
 * @error: (nullable): return location for a #GError
 *
 * Builds a convex hull and retains its vertices, outward faces and
 * planes. Coplanar or duplicate input is reduced. A flat set is
 * rejected. The previous caller state is irrelevant: failure returns
 * %NULL and no geometry.
 *
 * Returns: (transfer full): a new #LrgSolidGeometry
 */
LRG_AVAILABLE_IN_ALL
LrgSolidGeometry *
lrg_solid_geometry_new_convex (const gfloat *points,
                               guint         n_floats,
                               const gfloat *transform,
                               GError      **error);

/**
 * lrg_solid_geometry_new_aggregate:
 * @children: (array length=n_children): geometries to retain by
 *   reference. A shared child is one solid, not a copy of its identity
 * @n_children: at least one, at most %LRG_SOLID_MAX_AGGREGATE_CHILDREN
 * @error: (nullable): return location for a #GError
 *
 * Retains the union of @children. Queries flatten primitives in child
 * order. Depth, primitive count and a cyclic child graph are rejected
 * before any reference is taken. Children stay immutable.
 *
 * Returns: (transfer full): a new #LrgSolidGeometry
 */
LRG_AVAILABLE_IN_ALL
LrgSolidGeometry *
lrg_solid_geometry_new_aggregate (LrgSolidGeometry *const *children,
                                  guint                    n_children,
                                  GError                 **error);

/**
 * lrg_solid_geometry_get_shape:
 * @self: an #LrgSolidGeometry
 *
 * Returns the constructor kind. An aggregate of one sphere stays
 * %LRG_SOLID_SHAPE_AGGREGATE.
 *
 * Returns: the admitted shape
 */
LRG_AVAILABLE_IN_ALL
LrgSolidShape lrg_solid_geometry_get_shape (LrgSolidGeometry *self);

/**
 * lrg_solid_geometry_get_primitive_count:
 * @self: an #LrgSolidGeometry
 *
 * Returns how many flattened primitives queries walk. An aggregate
 * counts its children, not itself.
 *
 * Returns: primitive count, at least one
 */
LRG_AVAILABLE_IN_ALL
guint lrg_solid_geometry_get_primitive_count (LrgSolidGeometry *self);

/**
 * lrg_solid_geometry_classify_point:
 * @self: an #LrgSolidGeometry
 * @point: (array fixed-size=3): point in model space
 * @result: (out): classification
 * @error: (nullable): return location for a #GError
 *
 * Classifies @point against the closed solid. An aggregate is inside
 * when any primitive is inside, otherwise touching when any primitive
 * is touching.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_solid_geometry_classify_point (LrgSolidGeometry *self,
                                   const gfloat     *point,
                                   LrgSolidClass    *result,
                                   GError          **error);

/**
 * lrg_solid_geometry_overlap:
 * @self: an #LrgSolidGeometry
 * @other: an #LrgSolidGeometry
 * @contact: (out caller-allocates) (nullable): contact when overlapping
 * @overlapping: (out): %TRUE when the solids touch or penetrate
 * @error: (nullable): return location for a #GError
 *
 * Reports one contact. Supported pairs are sphere/sphere, sphere/box,
 * sphere/sphyl, sphere/convex, box/box, box/convex, sphyl/sphyl and
 * convex/convex. Any other primitive pair, including a pair hidden
 * inside an aggregate, fails with %G_IO_ERROR_NOT_SUPPORTED and leaves
 * @overlapping and @contact unchanged.
 *
 * Returns: %TRUE on success, including a clean separation
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_solid_geometry_overlap (LrgSolidGeometry *self,
                            LrgSolidGeometry *other,
                            LrgSolidContact  *contact,
                            gboolean         *overlapping,
                            GError          **error);

/**
 * lrg_solid_geometry_contains:
 * @self: the outer solid
 * @inner: the solid that must lie inside @self
 * @contained: (out): %TRUE when @inner is contained
 * @error: (nullable): return location for a #GError
 *
 * Exact for one outer primitive: vertices of a polyhedron, or both
 * endpoint spheres of a sphyl, must lie in the outer solid. An
 * aggregate outer is a union and is rejected. An aggregate inner is
 * contained only when every one of its primitives is contained.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_solid_geometry_contains (LrgSolidGeometry *self,
                             LrgSolidGeometry *inner,
                             gboolean         *contained,
                             GError          **error);

/**
 * lrg_solid_geometry_sweep_sphere:
 * @self: static solid in model space
 * @center: (array fixed-size=3): sphere center at the start
 * @radius: positive finite radius
 * @delta: (array fixed-size=3): motion of the center
 * @hit: (out caller-allocates) (nullable): earliest hit
 * @did_hit: (out): %TRUE when the sphere touches @self during the motion
 * @error: (nullable): return location for a #GError
 *
 * Sweeps one free sphere. An initial overlap is a hit at time 0.
 * Sphere against sphere, box, sphyl and convex is supported.
 *
 * Returns: %TRUE on success, including a miss
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_solid_geometry_sweep_sphere (LrgSolidGeometry *self,
                                 const gfloat     *center,
                                 gfloat            radius,
                                 const gfloat     *delta,
                                 LrgSolidHit      *hit,
                                 gboolean         *did_hit,
                                 GError          **error);

G_END_DECLS
