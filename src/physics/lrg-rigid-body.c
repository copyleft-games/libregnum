/* lrg-rigid-body.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Rigid body for physics simulation implementation.
 */

#include "config.h"
#include "lrg-rigid-body.h"
#include "lrg-log.h"
#include <math.h>

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PHYSICS

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

#pragma GCC visibility push(default)
G_DEFINE_TYPE_WITH_PRIVATE (LrgRigidBody, lrg_rigid_body, G_TYPE_OBJECT)
#pragma GCC visibility pop

enum
{
    PROP_0,
    PROP_BODY_TYPE,
    PROP_MASS,
    PROP_RESTITUTION,
    PROP_FRICTION,
    PROP_LINEAR_DAMPING,
    PROP_ANGULAR_DAMPING,
    PROP_GRAVITY_SCALE,
    PROP_IS_TRIGGER,
    N_PROPS
};

enum
{
    SIGNAL_COLLISION,
    SIGNAL_TRIGGER_ENTER,
    SIGNAL_TRIGGER_EXIT,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

/* Sleep threshold: velocity below this for SLEEP_TIME_THRESHOLD seconds */
#define SLEEP_VELOCITY_THRESHOLD    0.01f
#define SLEEP_TIME_THRESHOLD        1.0f

static void
lrg_rigid_body_get_property (GObject    *object,
                             guint       property_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgRigidBody *self = LRG_RIGID_BODY (object);
    LrgRigidBodyPrivate *priv = lrg_rigid_body_get_instance_private (self);

    switch (property_id)
    {
    case PROP_BODY_TYPE:
        g_value_set_enum (value, priv->body_type);
        break;
    case PROP_MASS:
        g_value_set_float (value, priv->mass);
        break;
    case PROP_RESTITUTION:
        g_value_set_float (value, priv->restitution);
        break;
    case PROP_FRICTION:
        g_value_set_float (value, priv->friction);
        break;
    case PROP_LINEAR_DAMPING:
        g_value_set_float (value, priv->linear_damping);
        break;
    case PROP_ANGULAR_DAMPING:
        g_value_set_float (value, priv->angular_damping);
        break;
    case PROP_GRAVITY_SCALE:
        g_value_set_float (value, priv->gravity_scale);
        break;
    case PROP_IS_TRIGGER:
        g_value_set_boolean (value, priv->is_trigger);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
lrg_rigid_body_set_property (GObject      *object,
                             guint         property_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgRigidBody *self = LRG_RIGID_BODY (object);

    switch (property_id)
    {
    case PROP_BODY_TYPE:
        lrg_rigid_body_set_body_type (self, g_value_get_enum (value));
        break;
    case PROP_MASS:
        lrg_rigid_body_set_mass (self, g_value_get_float (value));
        break;
    case PROP_RESTITUTION:
        lrg_rigid_body_set_restitution (self, g_value_get_float (value));
        break;
    case PROP_FRICTION:
        lrg_rigid_body_set_friction (self, g_value_get_float (value));
        break;
    case PROP_LINEAR_DAMPING:
        lrg_rigid_body_set_linear_damping (self, g_value_get_float (value));
        break;
    case PROP_ANGULAR_DAMPING:
        lrg_rigid_body_set_angular_damping (self, g_value_get_float (value));
        break;
    case PROP_GRAVITY_SCALE:
        lrg_rigid_body_set_gravity_scale (self, g_value_get_float (value));
        break;
    case PROP_IS_TRIGGER:
        lrg_rigid_body_set_is_trigger (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
lrg_rigid_body_class_init (LrgRigidBodyClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_rigid_body_get_property;
    object_class->set_property = lrg_rigid_body_set_property;

    /**
     * LrgRigidBody:body-type:
     *
     * The type of rigid body (dynamic, kinematic, static).
     */
    properties[PROP_BODY_TYPE] =
        g_param_spec_enum ("body-type",
                           "Body Type",
                           "Type of rigid body",
                           LRG_TYPE_RIGID_BODY_TYPE,
                           LRG_RIGID_BODY_DYNAMIC,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRigidBody:mass:
     *
     * The mass of the body in kg.
     */
    properties[PROP_MASS] =
        g_param_spec_float ("mass",
                            "Mass",
                            "Mass in kg",
                            0.001f, G_MAXFLOAT, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRigidBody:restitution:
     *
     * The bounciness coefficient (0.0 - 1.0).
     */
    properties[PROP_RESTITUTION] =
        g_param_spec_float ("restitution",
                            "Restitution",
                            "Bounciness coefficient",
                            0.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRigidBody:friction:
     *
     * The friction coefficient (0.0 - 1.0).
     */
    properties[PROP_FRICTION] =
        g_param_spec_float ("friction",
                            "Friction",
                            "Friction coefficient",
                            0.0f, 1.0f, 0.5f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRigidBody:linear-damping:
     *
     * Linear velocity damping (0.0 - 1.0).
     */
    properties[PROP_LINEAR_DAMPING] =
        g_param_spec_float ("linear-damping",
                            "Linear Damping",
                            "Linear velocity damping",
                            0.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRigidBody:angular-damping:
     *
     * Angular velocity damping (0.0 - 1.0).
     */
    properties[PROP_ANGULAR_DAMPING] =
        g_param_spec_float ("angular-damping",
                            "Angular Damping",
                            "Angular velocity damping",
                            0.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRigidBody:gravity-scale:
     *
     * Gravity multiplier (1.0 = normal, 0.0 = no gravity).
     */
    properties[PROP_GRAVITY_SCALE] =
        g_param_spec_float ("gravity-scale",
                            "Gravity Scale",
                            "Gravity multiplier",
                            -G_MAXFLOAT, G_MAXFLOAT, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRigidBody:is-trigger:
     *
     * Whether this body is a trigger (no physical response).
     */
    properties[PROP_IS_TRIGGER] =
        g_param_spec_boolean ("is-trigger",
                              "Is Trigger",
                              "Whether this is a trigger volume",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgRigidBody::collision:
     * @self: The body that collided
     * @other: The other body
     * @normal_x: Collision normal X
     * @normal_y: Collision normal Y
     *
     * Emitted when this body collides with another.
     */
    signals[SIGNAL_COLLISION] =
        g_signal_new ("collision",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgRigidBodyClass, on_collision),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE,
                      3,
                      LRG_TYPE_RIGID_BODY,
                      G_TYPE_FLOAT,
                      G_TYPE_FLOAT);

    /**
     * LrgRigidBody::trigger-enter:
     * @self: The trigger body
     * @other: The body entering
     *
     * Emitted when a body enters this trigger.
     */
    signals[SIGNAL_TRIGGER_ENTER] =
        g_signal_new ("trigger-enter",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgRigidBodyClass, on_trigger),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE,
                      2,
                      LRG_TYPE_RIGID_BODY,
                      G_TYPE_BOOLEAN);

    /**
     * LrgRigidBody::trigger-exit:
     * @self: The trigger body
     * @other: The body exiting
     *
     * Emitted when a body exits this trigger.
     */
    signals[SIGNAL_TRIGGER_EXIT] =
        g_signal_new ("trigger-exit",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE,
                      1,
                      LRG_TYPE_RIGID_BODY);
}

static void
lrg_rigid_body_init (LrgRigidBody *self)
{
    LrgRigidBodyPrivate *priv = lrg_rigid_body_get_instance_private (self);

    priv->body_type = LRG_RIGID_BODY_DYNAMIC;
    priv->mass = 1.0f;
    priv->inv_mass = 1.0f;
    priv->restitution = 0.0f;
    priv->friction = 0.5f;
    priv->linear_damping = 0.0f;
    priv->angular_damping = 0.0f;
    priv->gravity_scale = 1.0f;
    priv->is_trigger = FALSE;

    priv->pos_x = 0.0f;
    priv->pos_y = 0.0f;
    priv->rotation = 0.0f;

    priv->vel_x = 0.0f;
    priv->vel_y = 0.0f;
    priv->angular_velocity = 0.0f;

    priv->force_x = 0.0f;
    priv->force_y = 0.0f;
    priv->torque = 0.0f;

    priv->shape_type = LRG_COLLISION_SHAPE_BOX;
    priv->shape_width = 1.0f;
    priv->shape_height = 1.0f;
    priv->shape_radius = 0.5f;

    priv->sleeping = FALSE;
    priv->sleep_time = 0.0f;
}

/**
 * lrg_rigid_body_new:
 * @body_type: The type of rigid body
 *
 * Creates a new rigid body.
 *
 * Returns: (transfer full): A new #LrgRigidBody
 */
LrgRigidBody *
lrg_rigid_body_new (LrgRigidBodyType body_type)
{
    return g_object_new (LRG_TYPE_RIGID_BODY,
                         "body-type", body_type,
                         NULL);
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

LrgRigidBodyType
lrg_rigid_body_get_body_type (LrgRigidBody *self)
{
    LrgRigidBodyPrivate *priv;

    g_return_val_if_fail (LRG_IS_RIGID_BODY (self), LRG_RIGID_BODY_STATIC);

    priv = lrg_rigid_body_get_instance_private (self);
    return priv->body_type;
}

void
lrg_rigid_body_set_body_type (LrgRigidBody     *self,
                              LrgRigidBodyType  body_type)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));

    priv = lrg_rigid_body_get_instance_private (self);

    if (priv->body_type != body_type)
    {
        priv->body_type = body_type;

        /* Static and kinematic bodies have infinite mass */
        if (body_type != LRG_RIGID_BODY_DYNAMIC)
        {
            priv->inv_mass = 0.0f;
        }
        else
        {
            priv->inv_mass = (priv->mass > 0.0f) ? (1.0f / priv->mass) : 0.0f;
        }

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BODY_TYPE]);
    }
}

gfloat
lrg_rigid_body_get_mass (LrgRigidBody *self)
{
    LrgRigidBodyPrivate *priv;

    g_return_val_if_fail (LRG_IS_RIGID_BODY (self), 0.0f);

    priv = lrg_rigid_body_get_instance_private (self);
    return priv->mass;
}

void
lrg_rigid_body_set_mass (LrgRigidBody *self,
                         gfloat        mass)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));
    g_return_if_fail (mass > 0.0f);

    priv = lrg_rigid_body_get_instance_private (self);

    if (priv->mass != mass)
    {
        priv->mass = mass;
        priv->inv_mass = (priv->body_type == LRG_RIGID_BODY_DYNAMIC)
                         ? (1.0f / mass)
                         : 0.0f;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MASS]);
    }
}

gfloat
lrg_rigid_body_get_restitution (LrgRigidBody *self)
{
    LrgRigidBodyPrivate *priv;

    g_return_val_if_fail (LRG_IS_RIGID_BODY (self), 0.0f);

    priv = lrg_rigid_body_get_instance_private (self);
    return priv->restitution;
}

void
lrg_rigid_body_set_restitution (LrgRigidBody *self,
                                gfloat        restitution)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));

    priv = lrg_rigid_body_get_instance_private (self);
    restitution = CLAMP (restitution, 0.0f, 1.0f);

    if (priv->restitution != restitution)
    {
        priv->restitution = restitution;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RESTITUTION]);
    }
}

gfloat
lrg_rigid_body_get_friction (LrgRigidBody *self)
{
    LrgRigidBodyPrivate *priv;

    g_return_val_if_fail (LRG_IS_RIGID_BODY (self), 0.0f);

    priv = lrg_rigid_body_get_instance_private (self);
    return priv->friction;
}

void
lrg_rigid_body_set_friction (LrgRigidBody *self,
                             gfloat        friction)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));

    priv = lrg_rigid_body_get_instance_private (self);
    friction = CLAMP (friction, 0.0f, 1.0f);

    if (priv->friction != friction)
    {
        priv->friction = friction;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FRICTION]);
    }
}

gfloat
lrg_rigid_body_get_linear_damping (LrgRigidBody *self)
{
    LrgRigidBodyPrivate *priv;

    g_return_val_if_fail (LRG_IS_RIGID_BODY (self), 0.0f);

    priv = lrg_rigid_body_get_instance_private (self);
    return priv->linear_damping;
}

void
lrg_rigid_body_set_linear_damping (LrgRigidBody *self,
                                   gfloat        damping)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));

    priv = lrg_rigid_body_get_instance_private (self);
    damping = CLAMP (damping, 0.0f, 1.0f);

    if (priv->linear_damping != damping)
    {
        priv->linear_damping = damping;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LINEAR_DAMPING]);
    }
}

gfloat
lrg_rigid_body_get_angular_damping (LrgRigidBody *self)
{
    LrgRigidBodyPrivate *priv;

    g_return_val_if_fail (LRG_IS_RIGID_BODY (self), 0.0f);

    priv = lrg_rigid_body_get_instance_private (self);
    return priv->angular_damping;
}

void
lrg_rigid_body_set_angular_damping (LrgRigidBody *self,
                                    gfloat        damping)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));

    priv = lrg_rigid_body_get_instance_private (self);
    damping = CLAMP (damping, 0.0f, 1.0f);

    if (priv->angular_damping != damping)
    {
        priv->angular_damping = damping;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANGULAR_DAMPING]);
    }
}

gboolean
lrg_rigid_body_get_is_trigger (LrgRigidBody *self)
{
    LrgRigidBodyPrivate *priv;

    g_return_val_if_fail (LRG_IS_RIGID_BODY (self), FALSE);

    priv = lrg_rigid_body_get_instance_private (self);
    return priv->is_trigger;
}

void
lrg_rigid_body_set_is_trigger (LrgRigidBody *self,
                               gboolean      is_trigger)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));

    priv = lrg_rigid_body_get_instance_private (self);

    if (priv->is_trigger != is_trigger)
    {
        priv->is_trigger = is_trigger;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_TRIGGER]);
    }
}

gfloat
lrg_rigid_body_get_gravity_scale (LrgRigidBody *self)
{
    LrgRigidBodyPrivate *priv;

    g_return_val_if_fail (LRG_IS_RIGID_BODY (self), 1.0f);

    priv = lrg_rigid_body_get_instance_private (self);
    return priv->gravity_scale;
}

void
lrg_rigid_body_set_gravity_scale (LrgRigidBody *self,
                                  gfloat        scale)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));

    priv = lrg_rigid_body_get_instance_private (self);

    if (priv->gravity_scale != scale)
    {
        priv->gravity_scale = scale;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GRAVITY_SCALE]);
    }
}

/* ==========================================================================
 * Position and Motion
 * ========================================================================== */

void
lrg_rigid_body_get_position (LrgRigidBody *self,
                             gfloat       *out_x,
                             gfloat       *out_y)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));

    priv = lrg_rigid_body_get_instance_private (self);

    if (out_x)
        *out_x = priv->pos_x;
    if (out_y)
        *out_y = priv->pos_y;
}

void
lrg_rigid_body_set_position (LrgRigidBody *self,
                             gfloat        x,
                             gfloat        y)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));

    priv = lrg_rigid_body_get_instance_private (self);
    priv->pos_x = x;
    priv->pos_y = y;

    /* Teleporting wakes the body */
    priv->sleeping = FALSE;
    priv->sleep_time = 0.0f;
}

gfloat
lrg_rigid_body_get_rotation (LrgRigidBody *self)
{
    LrgRigidBodyPrivate *priv;

    g_return_val_if_fail (LRG_IS_RIGID_BODY (self), 0.0f);

    priv = lrg_rigid_body_get_instance_private (self);
    return priv->rotation;
}

void
lrg_rigid_body_set_rotation (LrgRigidBody *self,
                             gfloat        rotation)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));

    priv = lrg_rigid_body_get_instance_private (self);
    priv->rotation = rotation;
}

void
lrg_rigid_body_get_velocity (LrgRigidBody *self,
                             gfloat       *out_x,
                             gfloat       *out_y)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));

    priv = lrg_rigid_body_get_instance_private (self);

    if (out_x)
        *out_x = priv->vel_x;
    if (out_y)
        *out_y = priv->vel_y;
}

void
lrg_rigid_body_set_velocity (LrgRigidBody *self,
                             gfloat        x,
                             gfloat        y)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));

    priv = lrg_rigid_body_get_instance_private (self);
    priv->vel_x = x;
    priv->vel_y = y;

    /* Wakes the body */
    priv->sleeping = FALSE;
    priv->sleep_time = 0.0f;
}

gfloat
lrg_rigid_body_get_angular_velocity (LrgRigidBody *self)
{
    LrgRigidBodyPrivate *priv;

    g_return_val_if_fail (LRG_IS_RIGID_BODY (self), 0.0f);

    priv = lrg_rigid_body_get_instance_private (self);
    return priv->angular_velocity;
}

void
lrg_rigid_body_set_angular_velocity (LrgRigidBody *self,
                                     gfloat        velocity)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));

    priv = lrg_rigid_body_get_instance_private (self);
    priv->angular_velocity = velocity;
}

/* ==========================================================================
 * Forces and Impulses
 * ========================================================================== */

void
lrg_rigid_body_add_force (LrgRigidBody *self,
                          gfloat        force_x,
                          gfloat        force_y,
                          LrgForceMode  mode)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));

    priv = lrg_rigid_body_get_instance_private (self);

    if (priv->body_type != LRG_RIGID_BODY_DYNAMIC)
        return;

    switch (mode)
    {
    case LRG_FORCE_MODE_FORCE:
        priv->force_x += force_x;
        priv->force_y += force_y;
        break;

    case LRG_FORCE_MODE_IMPULSE:
        priv->vel_x += force_x * priv->inv_mass;
        priv->vel_y += force_y * priv->inv_mass;
        break;

    case LRG_FORCE_MODE_ACCELERATION:
        priv->force_x += force_x * priv->mass;
        priv->force_y += force_y * priv->mass;
        break;

    case LRG_FORCE_MODE_VELOCITY_CHANGE:
        priv->vel_x += force_x;
        priv->vel_y += force_y;
        break;
    }

    /* Force wakes the body */
    priv->sleeping = FALSE;
    priv->sleep_time = 0.0f;
}

void
lrg_rigid_body_add_force_at_point (LrgRigidBody *self,
                                   gfloat        force_x,
                                   gfloat        force_y,
                                   gfloat        point_x,
                                   gfloat        point_y,
                                   LrgForceMode  mode)
{
    LrgRigidBodyPrivate *priv;
    gfloat rx, ry;
    gfloat cross;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));

    priv = lrg_rigid_body_get_instance_private (self);

    if (priv->body_type != LRG_RIGID_BODY_DYNAMIC)
        return;

    /* Lever arm from center of mass to point */
    rx = point_x - priv->pos_x;
    ry = point_y - priv->pos_y;

    /* Cross product gives torque */
    cross = rx * force_y - ry * force_x;

    /* Add force and torque */
    lrg_rigid_body_add_force (self, force_x, force_y, mode);
    lrg_rigid_body_add_torque (self, cross, mode);
}

void
lrg_rigid_body_add_torque (LrgRigidBody *self,
                           gfloat        torque,
                           LrgForceMode  mode)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));

    priv = lrg_rigid_body_get_instance_private (self);

    if (priv->body_type != LRG_RIGID_BODY_DYNAMIC)
        return;

    switch (mode)
    {
    case LRG_FORCE_MODE_FORCE:
    case LRG_FORCE_MODE_ACCELERATION:
        priv->torque += torque;
        break;

    case LRG_FORCE_MODE_IMPULSE:
    case LRG_FORCE_MODE_VELOCITY_CHANGE:
        priv->angular_velocity += torque;
        break;
    }

    priv->sleeping = FALSE;
    priv->sleep_time = 0.0f;
}

void
lrg_rigid_body_clear_forces (LrgRigidBody *self)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));

    priv = lrg_rigid_body_get_instance_private (self);
    priv->force_x = 0.0f;
    priv->force_y = 0.0f;
    priv->torque = 0.0f;
}

/* ==========================================================================
 * Collision Shape
 * ========================================================================== */

void
lrg_rigid_body_set_box_shape (LrgRigidBody *self,
                              gfloat        width,
                              gfloat        height)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));
    g_return_if_fail (width > 0.0f);
    g_return_if_fail (height > 0.0f);

    priv = lrg_rigid_body_get_instance_private (self);
    priv->shape_type = LRG_COLLISION_SHAPE_BOX;
    priv->shape_width = width;
    priv->shape_height = height;
}

void
lrg_rigid_body_set_circle_shape (LrgRigidBody *self,
                                 gfloat        radius)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));
    g_return_if_fail (radius > 0.0f);

    priv = lrg_rigid_body_get_instance_private (self);
    priv->shape_type = LRG_COLLISION_SHAPE_CIRCLE;
    priv->shape_radius = radius;
    priv->shape_width = radius * 2.0f;
    priv->shape_height = radius * 2.0f;
}

LrgCollisionShape
lrg_rigid_body_get_shape_type (LrgRigidBody *self)
{
    LrgRigidBodyPrivate *priv;

    g_return_val_if_fail (LRG_IS_RIGID_BODY (self), LRG_COLLISION_SHAPE_BOX);

    priv = lrg_rigid_body_get_instance_private (self);
    return priv->shape_type;
}

void
lrg_rigid_body_get_shape_bounds (LrgRigidBody *self,
                                 gfloat       *out_width,
                                 gfloat       *out_height)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));

    priv = lrg_rigid_body_get_instance_private (self);

    if (out_width)
        *out_width = priv->shape_width;
    if (out_height)
        *out_height = priv->shape_height;
}

/* ==========================================================================
 * State
 * ========================================================================== */

gboolean
lrg_rigid_body_is_sleeping (LrgRigidBody *self)
{
    LrgRigidBodyPrivate *priv;

    g_return_val_if_fail (LRG_IS_RIGID_BODY (self), FALSE);

    priv = lrg_rigid_body_get_instance_private (self);
    return priv->sleeping;
}

void
lrg_rigid_body_wake_up (LrgRigidBody *self)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));

    priv = lrg_rigid_body_get_instance_private (self);
    priv->sleeping = FALSE;
    priv->sleep_time = 0.0f;
}

void
lrg_rigid_body_sleep (LrgRigidBody *self)
{
    LrgRigidBodyPrivate *priv;

    g_return_if_fail (LRG_IS_RIGID_BODY (self));

    priv = lrg_rigid_body_get_instance_private (self);
    priv->sleeping = TRUE;
    priv->vel_x = 0.0f;
    priv->vel_y = 0.0f;
    priv->angular_velocity = 0.0f;
}
