/* lrg-collider-component3d.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgColliderComponent3D: one collision shape on an #LrgEntity3D.
 *
 * A collider describes a shape (kind, dimensions, a local offset and
 * rotation, or a mesh taken from the entity's model), a trigger flag, an
 * optional per-collider material and an optional collision layer.  It does
 * not simulate anything on its own:
 *
 * - colliders on an entity that has an #LrgRigidBodyComponent are merged
 *   into that body's shape (one shape, or a compound of all of them);
 * - colliders on an entity without a rigid body become one static body
 *   per entity (a wall, a floor, a level mesh);
 * - a trigger collider always gets its own sensor body that follows the
 *   entity, and emits #LrgColliderComponent3D::trigger-entered and
 *   #LrgColliderComponent3D::trigger-exited.
 *
 * Bodies exist only while the entity is in an #LrgWorld3D that has a
 * physics world (lrg_world3d_set_physics()).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../ecs/lrg-component.h"
#include "../../physics/lrg-physics-shape.h"

G_BEGIN_DECLS

/**
 * LrgCollider3DKind:
 * @LRG_COLLIDER3D_SPHERE: a sphere of #LrgColliderComponent3D:radius
 * @LRG_COLLIDER3D_CAPSULE: a capsule along local Y: #LrgColliderComponent3D:radius
 *   and #LrgColliderComponent3D:half-height (half the distance between the
 *   two cap centres)
 * @LRG_COLLIDER3D_CYLINDER: a cylinder along local Y: radius and half-height
 *   (half the full height)
 * @LRG_COLLIDER3D_BOX: a box of half extents
 *   #LrgColliderComponent3D:half-extent-x / -y / -z
 * @LRG_COLLIDER3D_CONVEX: the convex hull of a mesh's vertices (the
 *   entity's model, or #LrgColliderComponent3D:mesh)
 * @LRG_COLLIDER3D_MESH: a triangle mesh (the entity's model, or
 *   #LrgColliderComponent3D:mesh); static and kinematic bodies only
 * @LRG_COLLIDER3D_PLANE: an infinite plane through the offset whose normal is
 *   the collider's local +Y; static only
 *
 * The shape of an #LrgColliderComponent3D.
 */
typedef enum
{
	LRG_COLLIDER3D_SPHERE,
	LRG_COLLIDER3D_CAPSULE,
	LRG_COLLIDER3D_CYLINDER,
	LRG_COLLIDER3D_BOX,
	LRG_COLLIDER3D_CONVEX,
	LRG_COLLIDER3D_MESH,
	LRG_COLLIDER3D_PLANE
} LrgCollider3DKind;

LRG_AVAILABLE_IN_ALL
GType lrg_collider3d_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_COLLIDER3D_KIND (lrg_collider3d_kind_get_type ())

#define LRG_TYPE_COLLIDER_COMPONENT3D (lrg_collider_component3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgColliderComponent3D, lrg_collider_component3d, LRG, COLLIDER_COMPONENT3D, LrgComponent)

/**
 * lrg_collider_component3d_new:
 * @kind: the shape kind
 *
 * Creates a collider with the default dimensions (unit box, sphere and
 * capsule radius 0.5, half-height 0.5), no offset, not a trigger, the body's
 * material and layer.
 *
 * Returns: (transfer full): a new #LrgColliderComponent3D
 */
LRG_AVAILABLE_IN_ALL
LrgColliderComponent3D * lrg_collider_component3d_new (LrgCollider3DKind kind);

/**
 * lrg_collider_component3d_new_box:
 * @hx: half extent along local X
 * @hy: half extent along local Y
 * @hz: half extent along local Z
 *
 * Returns: (transfer full): a new box collider
 */
LRG_AVAILABLE_IN_ALL
LrgColliderComponent3D * lrg_collider_component3d_new_box (gfloat hx,
                                                           gfloat hy,
                                                           gfloat hz);

/**
 * lrg_collider_component3d_new_sphere:
 * @radius: sphere radius
 *
 * Returns: (transfer full): a new sphere collider
 */
LRG_AVAILABLE_IN_ALL
LrgColliderComponent3D * lrg_collider_component3d_new_sphere (gfloat radius);

/**
 * lrg_collider_component3d_new_capsule:
 * @radius: capsule radius
 * @half_height: half the distance between the cap centres (local Y)
 *
 * Returns: (transfer full): a new capsule collider
 */
LRG_AVAILABLE_IN_ALL
LrgColliderComponent3D * lrg_collider_component3d_new_capsule (gfloat radius,
                                                               gfloat half_height);

/**
 * lrg_collider_component3d_get_kind:
 * @self: an #LrgColliderComponent3D
 *
 * Returns: the shape kind
 */
LRG_AVAILABLE_IN_ALL
LrgCollider3DKind lrg_collider_component3d_get_kind (LrgColliderComponent3D *self);

/**
 * lrg_collider_component3d_set_kind:
 * @self: an #LrgColliderComponent3D
 * @kind: the shape kind
 *
 * Changes the shape kind; the body is rebuilt on the next fixed update.
 */
LRG_AVAILABLE_IN_ALL
void lrg_collider_component3d_set_kind (LrgColliderComponent3D *self,
                                        LrgCollider3DKind       kind);

/**
 * lrg_collider_component3d_get_half_extents:
 * @self: an #LrgColliderComponent3D
 * @out: (out caller-allocates) (array fixed-size=3): the box half extents
 */
LRG_AVAILABLE_IN_ALL
void lrg_collider_component3d_get_half_extents (LrgColliderComponent3D *self,
                                                gfloat                 *out);

/**
 * lrg_collider_component3d_set_half_extents:
 * @self: an #LrgColliderComponent3D
 * @hx: half extent along local X (> 0)
 * @hy: half extent along local Y (> 0)
 * @hz: half extent along local Z (> 0)
 *
 * Non-positive values are clamped to 0.001.
 */
LRG_AVAILABLE_IN_ALL
void lrg_collider_component3d_set_half_extents (LrgColliderComponent3D *self,
                                                gfloat                  hx,
                                                gfloat                  hy,
                                                gfloat                  hz);

/**
 * lrg_collider_component3d_get_radius:
 * @self: an #LrgColliderComponent3D
 *
 * Returns: the sphere, capsule or cylinder radius
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_collider_component3d_get_radius (LrgColliderComponent3D *self);

/**
 * lrg_collider_component3d_set_radius:
 * @self: an #LrgColliderComponent3D
 * @radius: the radius (> 0, clamped to 0.001)
 */
LRG_AVAILABLE_IN_ALL
void lrg_collider_component3d_set_radius (LrgColliderComponent3D *self,
                                          gfloat                  radius);

/**
 * lrg_collider_component3d_get_half_height:
 * @self: an #LrgColliderComponent3D
 *
 * Returns: the capsule or cylinder half height
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_collider_component3d_get_half_height (LrgColliderComponent3D *self);

/**
 * lrg_collider_component3d_set_half_height:
 * @self: an #LrgColliderComponent3D
 * @half_height: half height (>= 0)
 */
LRG_AVAILABLE_IN_ALL
void lrg_collider_component3d_set_half_height (LrgColliderComponent3D *self,
                                               gfloat                  half_height);

/**
 * lrg_collider_component3d_get_offset:
 * @self: an #LrgColliderComponent3D
 * @out: (out caller-allocates) (array fixed-size=3): the local offset
 */
LRG_AVAILABLE_IN_ALL
void lrg_collider_component3d_get_offset (LrgColliderComponent3D *self,
                                          gfloat                 *out);

/**
 * lrg_collider_component3d_set_offset:
 * @self: an #LrgColliderComponent3D
 * @x: local X offset from the entity origin
 * @y: local Y offset
 * @z: local Z offset
 */
LRG_AVAILABLE_IN_ALL
void lrg_collider_component3d_set_offset (LrgColliderComponent3D *self,
                                          gfloat                  x,
                                          gfloat                  y,
                                          gfloat                  z);

/**
 * lrg_collider_component3d_get_rotation:
 * @self: an #LrgColliderComponent3D
 * @out: (out caller-allocates) (array fixed-size=3): local Euler rotation in
 *   degrees (R = Rz * Ry * Rx, the node convention)
 */
LRG_AVAILABLE_IN_ALL
void lrg_collider_component3d_get_rotation (LrgColliderComponent3D *self,
                                            gfloat                 *out);

/**
 * lrg_collider_component3d_set_rotation:
 * @self: an #LrgColliderComponent3D
 * @x: rotation about local X in degrees
 * @y: rotation about local Y in degrees
 * @z: rotation about local Z in degrees
 */
LRG_AVAILABLE_IN_ALL
void lrg_collider_component3d_set_rotation (LrgColliderComponent3D *self,
                                            gfloat                  x,
                                            gfloat                  y,
                                            gfloat                  z);

/**
 * lrg_collider_component3d_get_trigger:
 * @self: an #LrgColliderComponent3D
 *
 * Returns: %TRUE for a trigger (sensor) collider
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_collider_component3d_get_trigger (LrgColliderComponent3D *self);

/**
 * lrg_collider_component3d_set_trigger:
 * @self: an #LrgColliderComponent3D
 * @trigger: %TRUE for a sensor that reports overlaps and does not collide
 */
LRG_AVAILABLE_IN_ALL
void lrg_collider_component3d_set_trigger (LrgColliderComponent3D *self,
                                           gboolean                trigger);

/**
 * lrg_collider_component3d_get_friction:
 * @self: an #LrgColliderComponent3D
 *
 * Returns: the friction override, negative to use the body's
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_collider_component3d_get_friction (LrgColliderComponent3D *self);

/**
 * lrg_collider_component3d_set_friction:
 * @self: an #LrgColliderComponent3D
 * @friction: friction coefficient, negative to use the body's (or the
 *   default 0.5 for a body made only of colliders)
 */
LRG_AVAILABLE_IN_ALL
void lrg_collider_component3d_set_friction (LrgColliderComponent3D *self,
                                            gfloat                  friction);

/**
 * lrg_collider_component3d_get_restitution:
 * @self: an #LrgColliderComponent3D
 *
 * Returns: the restitution override, negative to use the body's
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_collider_component3d_get_restitution (LrgColliderComponent3D *self);

/**
 * lrg_collider_component3d_set_restitution:
 * @self: an #LrgColliderComponent3D
 * @restitution: bounciness 0..1, negative to use the body's (default 0)
 */
LRG_AVAILABLE_IN_ALL
void lrg_collider_component3d_set_restitution (LrgColliderComponent3D *self,
                                               gfloat                  restitution);

/**
 * lrg_collider_component3d_get_layer:
 * @self: an #LrgColliderComponent3D
 *
 * Returns: (transfer none) (nullable): the collision layer name, %NULL to use
 *   the body's
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_collider_component3d_get_layer (LrgColliderComponent3D *self);

/**
 * lrg_collider_component3d_set_layer:
 * @self: an #LrgColliderComponent3D
 * @layer: (nullable): a layer name from the physics world's layer table, or
 *   %NULL to use the body's.  Only colliders with a body of their own
 *   (triggers, static colliders) use it: the colliders merged into a rigid
 *   body share the body's layer.
 */
LRG_AVAILABLE_IN_ALL
void lrg_collider_component3d_set_layer (LrgColliderComponent3D *self,
                                         const gchar            *layer);

/**
 * lrg_collider_component3d_get_mesh:
 * @self: an #LrgColliderComponent3D
 *
 * Returns: (transfer none) (nullable): the mesh source of a convex or mesh
 *   collider, %NULL for the entity's own model
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_collider_component3d_get_mesh (LrgColliderComponent3D *self);

/**
 * lrg_collider_component3d_set_mesh:
 * @self: an #LrgColliderComponent3D
 * @mesh: (nullable): a model path (glTF/OBJ/...) whose meshes make the shape,
 *   or %NULL for the model of the entity's #LrgMeshRendererComponent
 */
LRG_AVAILABLE_IN_ALL
void lrg_collider_component3d_set_mesh (LrgColliderComponent3D *self,
                                        const gchar            *mesh);

/**
 * lrg_collider_component3d_set_mesh_data:
 * @self: an #LrgColliderComponent3D
 * @vertices: (array length=n_floats): vertex positions, xyz triples, in the
 *   entity's local space (before the collider offset)
 * @n_floats: number of floats in @vertices (3 per vertex)
 * @indices: (array length=n_indices) (nullable): triangle indices, %NULL for
 *   a convex collider built from the points
 * @n_indices: number of indices (3 per triangle)
 *
 * Gives a convex or mesh collider explicit geometry (procedural levels,
 * tests); it wins over #LrgColliderComponent3D:mesh and the entity's model.
 */
LRG_AVAILABLE_IN_ALL
void lrg_collider_component3d_set_mesh_data (LrgColliderComponent3D *self,
                                             const gfloat           *vertices,
                                             gsize                   n_floats,
                                             const guint32          *indices,
                                             gsize                   n_indices);

/**
 * lrg_collider_component3d_build_shape:
 * @self: an #LrgColliderComponent3D
 * @scale: (array fixed-size=3) (nullable): the entity's world scale, baked
 *   into the dimensions (%NULL for 1, 1, 1)
 *
 * Builds the collider's shape in its own frame (without the offset and
 * rotation: those are returned by lrg_collider_component3d_get_local_transform()).
 *
 * Returns: (transfer full) (nullable): the shape, %NULL when a mesh collider
 *   has no geometry yet
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsShape * lrg_collider_component3d_build_shape (LrgColliderComponent3D *self,
                                                        const gfloat           *scale);

/**
 * lrg_collider_component3d_get_local_transform:
 * @self: an #LrgColliderComponent3D
 * @scale: (array fixed-size=3) (nullable): the entity's world scale applied
 *   to the offset (%NULL for 1, 1, 1)
 * @position: (out caller-allocates) (array fixed-size=3): the offset
 * @rotation: (out caller-allocates) (array fixed-size=4): the rotation as a
 *   quaternion (x, y, z, w)
 */
LRG_AVAILABLE_IN_ALL
void lrg_collider_component3d_get_local_transform (LrgColliderComponent3D *self,
                                                   const gfloat           *scale,
                                                   gfloat                 *position,
                                                   gfloat                 *rotation);

/**
 * lrg_collider_component3d_get_body_id:
 * @self: an #LrgColliderComponent3D
 *
 * The body this collider is part of: the rigid body's, its own static body,
 * or its own sensor body for a trigger.
 *
 * Returns: the body id in the world's #LrgDynamicsWorld,
 *   %LRG_PHYSICS_NO_BODY when there is none
 */
LRG_AVAILABLE_IN_ALL
guint lrg_collider_component3d_get_body_id (LrgColliderComponent3D *self);

/**
 * lrg_collider_component3d_get_n_overlaps:
 * @self: an #LrgColliderComponent3D
 *
 * Returns: the number of entities inside a trigger collider
 */
LRG_AVAILABLE_IN_ALL
guint lrg_collider_component3d_get_n_overlaps (LrgColliderComponent3D *self);

/**
 * lrg_collider_component3d_is_overlapping:
 * @self: an #LrgColliderComponent3D
 * @entity: an entity
 *
 * Returns: %TRUE when @entity is inside the trigger
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_collider_component3d_is_overlapping (LrgColliderComponent3D *self,
                                                  LrgEntity3D            *entity);

G_END_DECLS
