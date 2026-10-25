/* lrg-rigid-body-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRigidBody instance data, shared with LrgPhysicsWorld so the step's
 * broadphase reads body state directly instead of through type-checked
 * getters per pair. Not installed, not introspected.
 */

#pragma once

#include "lrg-rigid-body.h"

G_BEGIN_DECLS

typedef struct
{
    /* Body properties */
    LrgRigidBodyType  body_type;
    gfloat            mass;
    gfloat            inv_mass;       /* 1/mass, cached */
    gfloat            restitution;
    gfloat            friction;
    gfloat            linear_damping;
    gfloat            angular_damping;
    gfloat            gravity_scale;
    gboolean          is_trigger;
    guint32           collision_layer;
    guint32           collision_mask;

    /* Transform */
    gfloat            pos_x;
    gfloat            pos_y;
    gfloat            rotation;

    /* Motion */
    gfloat            vel_x;
    gfloat            vel_y;
    gfloat            angular_velocity;

    /* Forces (accumulated each frame) */
    gfloat            force_x;
    gfloat            force_y;
    gfloat            torque;

    /* Collision shape */
    LrgCollisionShape shape_type;
    gfloat            shape_width;
    gfloat            shape_height;
    gfloat            shape_radius;

    /* State */
    gboolean          sleeping;
    gfloat            sleep_time;     /* Time with low motion */
} LrgRigidBodyPrivate;

/*
 * lrg_rigid_body_peek_private:
 * @self: an #LrgRigidBody (not type-checked)
 *
 * Returns: (transfer none): the body's instance data
 */
G_GNUC_INTERNAL
LrgRigidBodyPrivate *lrg_rigid_body_peek_private (LrgRigidBody *self);

/*
 * lrg_rigid_body_get_collision_signal_id:
 *
 * The id of #LrgRigidBody::collision; valid once the class exists (any
 * body has been created).
 *
 * Returns: the signal id
 */
G_GNUC_INTERNAL
guint lrg_rigid_body_get_collision_signal_id (void);

G_END_DECLS
