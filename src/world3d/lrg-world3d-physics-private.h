/* lrg-world3d-physics-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Private bridge between LrgWorld3D and the physics components
 * (LrgRigidBodyComponent, LrgColliderComponent3D, LrgJointComponent).
 * Not installed.
 *
 * A component registers itself as a "physics client" while it is in a
 * world (world_entered / world_exited).  The world calls the client hooks
 * from lrg_world3d_fixed_update() in client priority order, then
 * registration order (deterministic for a fixed insertion order):
 *
 *   for every fixed substep:
 *       components' fixed_update ()          (game logic)
 *       client->pre_step (dt)                (entity -> body, build bodies)
 *       lrg_dynamics_world_step (dt)
 *       client->post_step (dt)               (body -> entity, events)
 *       transform-changed flush
 *   after the substeps:
 *       client->interpolate (alpha)          (render blend)
 *
 * client->physics_changed (old, new) runs from lrg_world3d_set_physics()
 * and when a client registers into a world that already has physics
 * (old = NULL then).  Clients drop everything they made in @old.
 */

#pragma once

#include <glib-object.h>
#include "lrg-world3d.h"
#include "../physics/lrg-physics-shape.h"

G_BEGIN_DECLS

/* Priorities: bodies first, then stand-alone colliders, then joints. */
#define LRG_PHYSICS_CLIENT_BODY     (0)
#define LRG_PHYSICS_CLIENT_COLLIDER (1)
#define LRG_PHYSICS_CLIENT_JOINT    (2)

typedef struct
{
	void (*physics_changed) (GObject          *client,
	                         LrgDynamicsWorld *old_physics,
	                         LrgDynamicsWorld *new_physics);
	void (*pre_step)        (GObject          *client,
	                         LrgDynamicsWorld *physics,
	                         gfloat            dt);
	void (*post_step)       (GObject          *client,
	                         LrgDynamicsWorld *physics,
	                         gfloat            dt);
	void (*interpolate)     (GObject          *client,
	                         LrgDynamicsWorld *physics,
	                         gfloat            alpha);
} LrgPhysicsClientClass;

void _lrg_world3d_physics_add_client    (LrgWorld3D                  *world,
                                         GObject                     *client,
                                         gint                         priority,
                                         const LrgPhysicsClientClass *klass);
void _lrg_world3d_physics_remove_client (LrgWorld3D                  *world,
                                         GObject                     *client);

/* Body id -> entity map, maintained by the clients. */
void _lrg_world3d_physics_map_body      (LrgWorld3D                  *world,
                                         guint                        body_id,
                                         LrgEntity3D                 *entity);
void _lrg_world3d_physics_unmap_body    (LrgWorld3D                  *world,
                                         guint                        body_id);

/* Appends to @out (guint) the bodies touching @body_id after the last
 * substep (contact manifolds with depth >= 0), in body id order. */
void _lrg_world3d_physics_body_contacts  (LrgWorld3D                 *world,
                                          guint                       body_id,
                                          GArray                     *out);

/* Within a physics hook: TRUE while the world itself writes entity
 * transforms (so clients can tell their own writes from game teleports). */
gboolean _lrg_world3d_physics_is_syncing (LrgWorld3D                 *world);

/* --------------------------------------------------------------------------
 * Body backend shared by the components (lrg-rigid-body-component.c)
 * -------------------------------------------------------------------------- */

typedef struct
{
	LrgDynamicsBodyKind  kind;
	gfloat               mass;
	gfloat               friction;
	gfloat               restitution;
	gfloat               linear_damping;
	gfloat               angular_damping;
	gfloat               gravity_factor;
	gboolean             ccd;
	gboolean             sensor;
	const gchar         *layer;     /* nullable: the default layer */
} LrgPhysicsBodyDesc;

/* Fills @desc with the component defaults (static, mass 1, friction 0.5). */
void      _lrg_physics_body_desc_init       (LrgPhysicsBodyDesc *desc);

/* Creates a body; LRG_PHYSICS_NO_BODY on failure (logged).  The world maps it to @entity. */
guint     _lrg_physics_body_create          (LrgWorld3D               *world,
                                             LrgDynamicsWorld         *physics,
                                             LrgEntity3D              *entity,
                                             LrgPhysicsShape          *shape,
                                             const LrgPhysicsBodyDesc *desc);

/* Removes a body (and its map entry); LRG_PHYSICS_NO_BODY is ignored. */
void      _lrg_physics_body_destroy         (LrgWorld3D               *world,
                                             LrgDynamicsWorld         *physics,
                                             guint                     body_id);

/* Teleports the body to the entity's world pose (centre of the shape
 * frame = entity origin). */
void      _lrg_physics_body_teleport        (LrgDynamicsWorld         *physics,
                                             guint                     body_id,
                                             LrgEntity3D              *entity);

/* Moves a kinematic body to the entity's pose over @dt. */
void      _lrg_physics_body_move_kinematic  (LrgDynamicsWorld         *physics,
                                             guint                     body_id,
                                             LrgEntity3D              *entity,
                                             gfloat                    dt);

/* Reads the body pose; FALSE when the body does not exist. */
gboolean  _lrg_physics_body_get_pose        (LrgDynamicsWorld         *physics,
                                             guint                     body_id,
                                             gfloat                   *position,
                                             gfloat                   *rotation);

/* The shape of the given colliders (one shape, or a compound with their
 * local transforms), with the entity's world scale baked in; NULL when no
 * collider produced a shape. */
LrgPhysicsShape *
          _lrg_physics_build_shape          (LrgEntity3D              *entity,
                                             GPtrArray                *colliders);

/* Appends to @out (guint) the bodies currently overlapping a sensor. */
void      _lrg_physics_sensor_overlaps      (LrgDynamicsWorld         *physics,
                                             guint                     sensor_id,
                                             GArray                   *out);

/* Tells the entity's rigid body (if any) that its colliders changed. */
void      _lrg_rigid_body_component_colliders_changed (LrgEntity3D *entity);

G_END_DECLS
