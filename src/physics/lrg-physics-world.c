/* lrg-physics-world.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Physics simulation world implementation.
 */

#include "config.h"
#include "lrg-physics-world.h"
#include "lrg-log.h"
#include <math.h>

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PHYSICS

typedef struct
{
    /* World settings */
    gfloat    gravity_x;
    gfloat    gravity_y;
    gfloat    time_step;
    guint     velocity_iterations;
    guint     position_iterations;

    /* Bodies */
    GPtrArray *bodies;

    /* Simulation state */
    gboolean  paused;
    gfloat    accumulator;  /* Time accumulator for fixed timestep */
} LrgPhysicsWorldPrivate;

#pragma GCC visibility push(default)
G_DEFINE_TYPE_WITH_PRIVATE (LrgPhysicsWorld, lrg_physics_world, G_TYPE_OBJECT)
#pragma GCC visibility pop

enum
{
    PROP_0,
    PROP_PAUSED,
    N_PROPS
};

enum
{
    SIGNAL_COLLISION,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

/* Default physics settings */
#define DEFAULT_GRAVITY_X         0.0f
#define DEFAULT_GRAVITY_Y         9.81f    /* Downward gravity */
#define DEFAULT_TIME_STEP         (1.0f / 60.0f)  /* 60 Hz */
#define DEFAULT_VELOCITY_ITERS    8
#define DEFAULT_POSITION_ITERS    3

static void
lrg_physics_world_dispose (GObject *object)
{
    LrgPhysicsWorld *self = LRG_PHYSICS_WORLD (object);
    LrgPhysicsWorldPrivate *priv = lrg_physics_world_get_instance_private (self);

    if (priv->bodies)
    {
        g_ptr_array_unref (priv->bodies);
        priv->bodies = NULL;
    }

    G_OBJECT_CLASS (lrg_physics_world_parent_class)->dispose (object);
}

static void
lrg_physics_world_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgPhysicsWorld *self = LRG_PHYSICS_WORLD (object);
    LrgPhysicsWorldPrivate *priv = lrg_physics_world_get_instance_private (self);

    switch (property_id)
    {
    case PROP_PAUSED:
        g_value_set_boolean (value, priv->paused);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
lrg_physics_world_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgPhysicsWorld *self = LRG_PHYSICS_WORLD (object);

    switch (property_id)
    {
    case PROP_PAUSED:
        lrg_physics_world_set_paused (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
lrg_physics_world_class_init (LrgPhysicsWorldClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_physics_world_dispose;
    object_class->get_property = lrg_physics_world_get_property;
    object_class->set_property = lrg_physics_world_set_property;

    /**
     * LrgPhysicsWorld:paused:
     *
     * Whether the physics simulation is paused.
     */
    properties[PROP_PAUSED] =
        g_param_spec_boolean ("paused",
                              "Paused",
                              "Whether simulation is paused",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgPhysicsWorld::collision:
     * @self: The physics world
     * @info: The collision info
     *
     * Emitted when a collision is detected.
     */
    signals[SIGNAL_COLLISION] =
        g_signal_new ("collision",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE,
                      1,
                      LRG_TYPE_COLLISION_INFO);
}

static void
lrg_physics_world_init (LrgPhysicsWorld *self)
{
    LrgPhysicsWorldPrivate *priv = lrg_physics_world_get_instance_private (self);

    priv->gravity_x = DEFAULT_GRAVITY_X;
    priv->gravity_y = DEFAULT_GRAVITY_Y;
    priv->time_step = DEFAULT_TIME_STEP;
    priv->velocity_iterations = DEFAULT_VELOCITY_ITERS;
    priv->position_iterations = DEFAULT_POSITION_ITERS;

    priv->bodies = g_ptr_array_new_with_free_func (g_object_unref);

    priv->paused = FALSE;
    priv->accumulator = 0.0f;

    lrg_debug (LRG_LOG_DOMAIN, "Created physics world");
}

/**
 * lrg_physics_world_new:
 *
 * Creates a new physics world with default settings.
 *
 * Returns: (transfer full): A new #LrgPhysicsWorld
 */
LrgPhysicsWorld *
lrg_physics_world_new (void)
{
    return g_object_new (LRG_TYPE_PHYSICS_WORLD, NULL);
}

/* ==========================================================================
 * World Properties
 * ========================================================================== */

void
lrg_physics_world_get_gravity (LrgPhysicsWorld *self,
                               gfloat          *out_x,
                               gfloat          *out_y)
{
    LrgPhysicsWorldPrivate *priv;

    g_return_if_fail (LRG_IS_PHYSICS_WORLD (self));

    priv = lrg_physics_world_get_instance_private (self);

    if (out_x)
        *out_x = priv->gravity_x;
    if (out_y)
        *out_y = priv->gravity_y;
}

void
lrg_physics_world_set_gravity (LrgPhysicsWorld *self,
                               gfloat           x,
                               gfloat           y)
{
    LrgPhysicsWorldPrivate *priv;

    g_return_if_fail (LRG_IS_PHYSICS_WORLD (self));

    priv = lrg_physics_world_get_instance_private (self);
    priv->gravity_x = x;
    priv->gravity_y = y;

    lrg_debug (LRG_LOG_DOMAIN, "Set gravity to (%.2f, %.2f)", x, y);
}

gfloat
lrg_physics_world_get_time_step (LrgPhysicsWorld *self)
{
    LrgPhysicsWorldPrivate *priv;

    g_return_val_if_fail (LRG_IS_PHYSICS_WORLD (self), DEFAULT_TIME_STEP);

    priv = lrg_physics_world_get_instance_private (self);
    return priv->time_step;
}

void
lrg_physics_world_set_time_step (LrgPhysicsWorld *self,
                                 gfloat           time_step)
{
    LrgPhysicsWorldPrivate *priv;

    g_return_if_fail (LRG_IS_PHYSICS_WORLD (self));
    g_return_if_fail (time_step > 0.0f);

    priv = lrg_physics_world_get_instance_private (self);
    priv->time_step = time_step;
}

guint
lrg_physics_world_get_velocity_iterations (LrgPhysicsWorld *self)
{
    LrgPhysicsWorldPrivate *priv;

    g_return_val_if_fail (LRG_IS_PHYSICS_WORLD (self), DEFAULT_VELOCITY_ITERS);

    priv = lrg_physics_world_get_instance_private (self);
    return priv->velocity_iterations;
}

void
lrg_physics_world_set_velocity_iterations (LrgPhysicsWorld *self,
                                           guint            iterations)
{
    LrgPhysicsWorldPrivate *priv;

    g_return_if_fail (LRG_IS_PHYSICS_WORLD (self));

    priv = lrg_physics_world_get_instance_private (self);
    priv->velocity_iterations = iterations;
}

guint
lrg_physics_world_get_position_iterations (LrgPhysicsWorld *self)
{
    LrgPhysicsWorldPrivate *priv;

    g_return_val_if_fail (LRG_IS_PHYSICS_WORLD (self), DEFAULT_POSITION_ITERS);

    priv = lrg_physics_world_get_instance_private (self);
    return priv->position_iterations;
}

void
lrg_physics_world_set_position_iterations (LrgPhysicsWorld *self,
                                           guint            iterations)
{
    LrgPhysicsWorldPrivate *priv;

    g_return_if_fail (LRG_IS_PHYSICS_WORLD (self));

    priv = lrg_physics_world_get_instance_private (self);
    priv->position_iterations = iterations;
}

/* ==========================================================================
 * Body Management
 * ========================================================================== */

void
lrg_physics_world_add_body (LrgPhysicsWorld *self,
                            LrgRigidBody    *body)
{
    LrgPhysicsWorldPrivate *priv;

    g_return_if_fail (LRG_IS_PHYSICS_WORLD (self));
    g_return_if_fail (LRG_IS_RIGID_BODY (body));

    priv = lrg_physics_world_get_instance_private (self);

    g_ptr_array_add (priv->bodies, g_object_ref (body));

    lrg_debug (LRG_LOG_DOMAIN, "Added body to physics world (count: %u)",
               priv->bodies->len);
}

gboolean
lrg_physics_world_remove_body (LrgPhysicsWorld *self,
                               LrgRigidBody    *body)
{
    LrgPhysicsWorldPrivate *priv;
    gboolean removed;

    g_return_val_if_fail (LRG_IS_PHYSICS_WORLD (self), FALSE);
    g_return_val_if_fail (LRG_IS_RIGID_BODY (body), FALSE);

    priv = lrg_physics_world_get_instance_private (self);

    removed = g_ptr_array_remove (priv->bodies, body);

    if (removed)
    {
        lrg_debug (LRG_LOG_DOMAIN, "Removed body from physics world (count: %u)",
                   priv->bodies->len);
    }

    return removed;
}

guint
lrg_physics_world_get_body_count (LrgPhysicsWorld *self)
{
    LrgPhysicsWorldPrivate *priv;

    g_return_val_if_fail (LRG_IS_PHYSICS_WORLD (self), 0);

    priv = lrg_physics_world_get_instance_private (self);
    return priv->bodies->len;
}

GPtrArray *
lrg_physics_world_get_bodies (LrgPhysicsWorld *self)
{
    LrgPhysicsWorldPrivate *priv;

    g_return_val_if_fail (LRG_IS_PHYSICS_WORLD (self), NULL);

    priv = lrg_physics_world_get_instance_private (self);
    return priv->bodies;
}

void
lrg_physics_world_clear (LrgPhysicsWorld *self)
{
    LrgPhysicsWorldPrivate *priv;

    g_return_if_fail (LRG_IS_PHYSICS_WORLD (self));

    priv = lrg_physics_world_get_instance_private (self);

    g_ptr_array_set_size (priv->bodies, 0);

    lrg_debug (LRG_LOG_DOMAIN, "Cleared physics world");
}

/* ==========================================================================
 * Simulation Helpers
 * ========================================================================== */

/*
 * Integrate forces for a single body.
 * Uses semi-implicit Euler integration.
 */
static void
integrate_body (LrgRigidBody *body,
                gfloat        gravity_x,
                gfloat        gravity_y,
                gfloat        dt)
{
    gfloat pos_x, pos_y;
    gfloat vel_x, vel_y;
    gfloat angular_vel;
    gfloat rotation;
    gfloat gravity_scale;
    gfloat linear_damping;
    gfloat angular_damping;
    gfloat damping_factor;
    LrgRigidBodyType body_type;

    body_type = lrg_rigid_body_get_body_type (body);

    /* Only dynamic bodies move */
    if (body_type != LRG_RIGID_BODY_DYNAMIC)
        return;

    /* Skip sleeping bodies */
    if (lrg_rigid_body_is_sleeping (body))
        return;

    /* Get current state */
    lrg_rigid_body_get_position (body, &pos_x, &pos_y);
    lrg_rigid_body_get_velocity (body, &vel_x, &vel_y);
    angular_vel = lrg_rigid_body_get_angular_velocity (body);
    rotation = lrg_rigid_body_get_rotation (body);
    gravity_scale = lrg_rigid_body_get_gravity_scale (body);
    linear_damping = lrg_rigid_body_get_linear_damping (body);
    angular_damping = lrg_rigid_body_get_angular_damping (body);

    /* Apply gravity */
    vel_x += gravity_x * gravity_scale * dt;
    vel_y += gravity_y * gravity_scale * dt;

    /* Apply damping */
    damping_factor = 1.0f - linear_damping;
    vel_x *= damping_factor;
    vel_y *= damping_factor;
    angular_vel *= (1.0f - angular_damping);

    /* Integrate position */
    pos_x += vel_x * dt;
    pos_y += vel_y * dt;
    rotation += angular_vel * dt;

    /* Update body */
    lrg_rigid_body_set_position (body, pos_x, pos_y);
    lrg_rigid_body_set_velocity (body, vel_x, vel_y);
    lrg_rigid_body_set_angular_velocity (body, angular_vel);
    lrg_rigid_body_set_rotation (body, rotation);

    /* Clear forces for next frame */
    lrg_rigid_body_clear_forces (body);
}

/*
 * Check AABB overlap between two bodies.
 */
static gboolean
check_aabb_overlap (LrgRigidBody *a,
                    LrgRigidBody *b)
{
    gfloat a_x, a_y, a_w, a_h;
    gfloat b_x, b_y, b_w, b_h;

    lrg_rigid_body_get_position (a, &a_x, &a_y);
    lrg_rigid_body_get_shape_bounds (a, &a_w, &a_h);

    lrg_rigid_body_get_position (b, &b_x, &b_y);
    lrg_rigid_body_get_shape_bounds (b, &b_w, &b_h);

    /* Half-widths */
    a_w *= 0.5f;
    a_h *= 0.5f;
    b_w *= 0.5f;
    b_h *= 0.5f;

    /* Check overlap */
    if (fabsf (a_x - b_x) > (a_w + b_w))
        return FALSE;
    if (fabsf (a_y - b_y) > (a_h + b_h))
        return FALSE;

    return TRUE;
}

/*
 * Perform a single fixed timestep simulation.
 */
static void
do_physics_step (LrgPhysicsWorld *self,
                 gfloat           dt)
{
    LrgPhysicsWorldPrivate *priv = lrg_physics_world_get_instance_private (self);
    LrgPhysicsWorldClass *klass = LRG_PHYSICS_WORLD_GET_CLASS (self);
    guint i, j;

    /* Pre-step callback */
    if (klass->pre_step)
        klass->pre_step (self, dt);

    /* Integrate forces */
    for (i = 0; i < priv->bodies->len; i++)
    {
        LrgRigidBody *body = g_ptr_array_index (priv->bodies, i);
        integrate_body (body, priv->gravity_x, priv->gravity_y, dt);
    }

    /* Broad phase collision detection (simple N^2 for now) */
    for (i = 0; i < priv->bodies->len; i++)
    {
        LrgRigidBody *body_a = g_ptr_array_index (priv->bodies, i);

        for (j = i + 1; j < priv->bodies->len; j++)
        {
            LrgRigidBody *body_b = g_ptr_array_index (priv->bodies, j);

            /* Skip if both are static/kinematic */
            if (lrg_rigid_body_get_body_type (body_a) != LRG_RIGID_BODY_DYNAMIC &&
                lrg_rigid_body_get_body_type (body_b) != LRG_RIGID_BODY_DYNAMIC)
                continue;

            if (!lrg_rigid_body_can_collide (body_a, body_b))
                continue;

            /* Check AABB overlap */
            if (check_aabb_overlap (body_a, body_b))
            {
                gfloat a_x, a_y, b_x, b_y;
                gfloat dx, dy;
                gfloat len;
                gfloat nx, ny;
                LrgCollisionInfo *info;

                /* Get positions */
                lrg_rigid_body_get_position (body_a, &a_x, &a_y);
                lrg_rigid_body_get_position (body_b, &b_x, &b_y);

                /* Compute simple normal (from A to B) */
                dx = b_x - a_x;
                dy = b_y - a_y;
                len = sqrtf (dx * dx + dy * dy);

                if (len > 0.0001f)
                {
                    nx = dx / len;
                    ny = dy / len;
                }
                else
                {
                    nx = 1.0f;
                    ny = 0.0f;
                }

                /* Emit collision signal for triggers, or apply response */
                if (lrg_rigid_body_get_is_trigger (body_a) ||
                    lrg_rigid_body_get_is_trigger (body_b))
                {
                    /* Trigger: just emit signal */
                    g_signal_emit (body_a, g_signal_lookup ("collision", LRG_TYPE_RIGID_BODY),
                                   0, body_b, nx, ny);
                    g_signal_emit (body_b, g_signal_lookup ("collision", LRG_TYPE_RIGID_BODY),
                                   0, body_a, -nx, -ny);
                }
                else
                {
                    /* Physical collision: emit world signal */
                    info = lrg_collision_info_new (G_OBJECT (body_a),
                                                   G_OBJECT (body_b),
                                                   nx, ny,
                                                   0.0f,  /* penetration */
                                                   (a_x + b_x) * 0.5f,
                                                   (a_y + b_y) * 0.5f);
                    g_signal_emit (self, signals[SIGNAL_COLLISION], 0, info);
                    lrg_collision_info_free (info);

                    /* Emit on bodies */
                    g_signal_emit (body_a, g_signal_lookup ("collision", LRG_TYPE_RIGID_BODY),
                                   0, body_b, nx, ny);
                    g_signal_emit (body_b, g_signal_lookup ("collision", LRG_TYPE_RIGID_BODY),
                                   0, body_a, -nx, -ny);
                }
            }
        }
    }

    /* Post-step callback */
    if (klass->post_step)
        klass->post_step (self, dt);
}

/* ==========================================================================
 * Simulation
 * ========================================================================== */

void
lrg_physics_world_step (LrgPhysicsWorld *self,
                        gfloat           delta_time)
{
    LrgPhysicsWorldPrivate *priv;

    g_return_if_fail (LRG_IS_PHYSICS_WORLD (self));

    priv = lrg_physics_world_get_instance_private (self);

    if (priv->paused)
        return;

    /* Fixed timestep with accumulator */
    priv->accumulator += delta_time;

    while (priv->accumulator >= priv->time_step)
    {
        do_physics_step (self, priv->time_step);
        priv->accumulator -= priv->time_step;
    }
}

gboolean
lrg_physics_world_is_paused (LrgPhysicsWorld *self)
{
    LrgPhysicsWorldPrivate *priv;

    g_return_val_if_fail (LRG_IS_PHYSICS_WORLD (self), TRUE);

    priv = lrg_physics_world_get_instance_private (self);
    return priv->paused;
}

void
lrg_physics_world_set_paused (LrgPhysicsWorld *self,
                              gboolean         paused)
{
    LrgPhysicsWorldPrivate *priv;

    g_return_if_fail (LRG_IS_PHYSICS_WORLD (self));

    priv = lrg_physics_world_get_instance_private (self);

    if (priv->paused != paused)
    {
        priv->paused = paused;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PAUSED]);

        lrg_debug (LRG_LOG_DOMAIN, "Physics world %s", paused ? "paused" : "resumed");
    }
}

/* ==========================================================================
 * Queries
 * ========================================================================== */

static gboolean
raycast_impl (LrgPhysicsWorld *  self,
              gfloat             start_x,
              gfloat             start_y,
              gfloat             end_x,
              gfloat             end_y,
              guint32            layer_mask,
              gboolean           filter_layers,
              gboolean           include_triggers,
              LrgRigidBody *     ignore_body,
              LrgRigidBody **    out_hit_body,
              gfloat *           out_hit_x,
              gfloat *           out_hit_y,
              gfloat *           out_hit_normal_x,
              gfloat *           out_hit_normal_y)
{
    LrgPhysicsWorldPrivate *priv;
    LrgRigidBody *best_body = NULL;
    gdouble best_t = 1.0;
    gdouble origin[2] = { start_x, start_y };
    gdouble direction[2] = { (gdouble) end_x - start_x, (gdouble) end_y - start_y };
    gfloat best_normal[2] = { 0, 0 };
    guint i;

    if (out_hit_body) *out_hit_body = NULL;
    if (out_hit_x) *out_hit_x = 0;
    if (out_hit_y) *out_hit_y = 0;
    if (out_hit_normal_x) *out_hit_normal_x = 0;
    if (out_hit_normal_y) *out_hit_normal_y = 0;
    g_return_val_if_fail (LRG_IS_PHYSICS_WORLD (self), FALSE);
    g_return_val_if_fail (ignore_body == NULL || LRG_IS_RIGID_BODY (ignore_body), FALSE);
    if (!isfinite (start_x) || !isfinite (start_y) ||
        !isfinite (end_x) || !isfinite (end_y) ||
        (direction[0] == 0 && direction[1] == 0))
        return FALSE;
    priv = lrg_physics_world_get_instance_private (self);

    for (i = 0; i < priv->bodies->len; i++)
    {
        LrgRigidBody *body = g_ptr_array_index (priv->bodies, i);
        gfloat position[2];
        gfloat extent[2];
        gfloat normal[2] = { 0, 0 };
        gdouble enter = 0;
        gdouble leave = 1;
        gboolean intersects = TRUE;
        guint axis;

        if (body == ignore_body ||
            (!include_triggers && lrg_rigid_body_get_is_trigger (body)) ||
            (filter_layers && (layer_mask & lrg_rigid_body_get_collision_layer (body)) == 0))
            continue;
        lrg_rigid_body_get_position (body, &position[0], &position[1]);
        lrg_rigid_body_get_shape_bounds (body, &extent[0], &extent[1]);
        for (axis = 0; axis < 2; axis++)
        {
            gdouble lower;
            gdouble upper;
            gdouble near_t;
            gdouble far_t;
            gfloat sign = -1;

            if (!isfinite (position[axis]) || !isfinite (extent[axis]) || extent[axis] < 0)
            {
                intersects = FALSE;
                break;
            }
            lower = (gdouble) position[axis] - (gdouble) extent[axis] * 0.5;
            upper = (gdouble) position[axis] + (gdouble) extent[axis] * 0.5;
            if (direction[axis] == 0)
            {
                if (origin[axis] < lower || origin[axis] > upper)
                    intersects = FALSE;
                continue;
            }
            near_t = (lower - origin[axis]) / direction[axis];
            far_t = (upper - origin[axis]) / direction[axis];
            if (near_t > far_t)
            {
                gdouble swap = near_t;

                near_t = far_t;
                far_t = swap;
                sign = 1;
            }
            if (near_t > enter)
            {
                enter = near_t;
                normal[0] = normal[1] = 0;
                normal[axis] = sign;
            }
            leave = MIN (leave, far_t);
            if (enter > leave)
                intersects = FALSE;
        }
        if (intersects && (best_body == NULL || enter < best_t))
        {
            best_body = body;
            best_t = enter;
            best_normal[0] = normal[0];
            best_normal[1] = normal[1];
        }
    }
    if (best_body == NULL)
        return FALSE;
    if (out_hit_body) *out_hit_body = best_body;
    if (out_hit_x) *out_hit_x = origin[0] + direction[0] * best_t;
    if (out_hit_y) *out_hit_y = origin[1] + direction[1] * best_t;
    if (out_hit_normal_x) *out_hit_normal_x = best_normal[0];
    if (out_hit_normal_y) *out_hit_normal_y = best_normal[1];
    return TRUE;
}

gboolean
lrg_physics_world_raycast (LrgPhysicsWorld *self,
                           gfloat           start_x,
                           gfloat           start_y,
                           gfloat           end_x,
                           gfloat           end_y,
                           LrgRigidBody   **out_hit_body,
                           gfloat          *out_hit_x,
                           gfloat          *out_hit_y,
                           gfloat          *out_hit_normal_x,
                           gfloat          *out_hit_normal_y)
{
    return raycast_impl (self, start_x, start_y, end_x, end_y,
                         G_MAXUINT32, FALSE, TRUE, NULL,
                         out_hit_body, out_hit_x, out_hit_y,
                         out_hit_normal_x, out_hit_normal_y);
}

gboolean
lrg_physics_world_raycast_filtered (LrgPhysicsWorld *  self,
                                    gfloat             start_x,
                                    gfloat             start_y,
                                    gfloat             end_x,
                                    gfloat             end_y,
                                    guint32            layer_mask,
                                    gboolean           include_triggers,
                                    LrgRigidBody *     ignore_body,
                                    LrgRigidBody **    out_hit_body,
                                    gfloat *           out_hit_x,
                                    gfloat *           out_hit_y,
                                    gfloat *           out_hit_normal_x,
                                    gfloat *           out_hit_normal_y)
{
    return raycast_impl (self, start_x, start_y, end_x, end_y,
                         layer_mask, TRUE, include_triggers, ignore_body,
                         out_hit_body, out_hit_x, out_hit_y,
                         out_hit_normal_x, out_hit_normal_y);
}

GPtrArray *
lrg_physics_world_query_aabb (LrgPhysicsWorld *self,
                              gfloat           min_x,
                              gfloat           min_y,
                              gfloat           max_x,
                              gfloat           max_y)
{
    LrgPhysicsWorldPrivate *priv;
    GPtrArray *result;
    guint i;

    g_return_val_if_fail (LRG_IS_PHYSICS_WORLD (self), NULL);

    priv = lrg_physics_world_get_instance_private (self);
    result = g_ptr_array_new ();

    for (i = 0; i < priv->bodies->len; i++)
    {
        LrgRigidBody *body = g_ptr_array_index (priv->bodies, i);
        gfloat pos_x, pos_y;
        gfloat half_w, half_h;
        gfloat body_min_x, body_max_x, body_min_y, body_max_y;

        lrg_rigid_body_get_position (body, &pos_x, &pos_y);
        lrg_rigid_body_get_shape_bounds (body, &half_w, &half_h);
        half_w *= 0.5f;
        half_h *= 0.5f;

        body_min_x = pos_x - half_w;
        body_max_x = pos_x + half_w;
        body_min_y = pos_y - half_h;
        body_max_y = pos_y + half_h;

        /* Check overlap */
        if (body_max_x >= min_x && body_min_x <= max_x &&
            body_max_y >= min_y && body_min_y <= max_y)
        {
            g_ptr_array_add (result, body);
        }
    }

    return result;
}

GPtrArray *
lrg_physics_world_query_point (LrgPhysicsWorld *self,
                               gfloat           x,
                               gfloat           y)
{
    LrgPhysicsWorldPrivate *priv;
    GPtrArray *result;
    guint i;

    g_return_val_if_fail (LRG_IS_PHYSICS_WORLD (self), NULL);

    priv = lrg_physics_world_get_instance_private (self);
    result = g_ptr_array_new ();

    for (i = 0; i < priv->bodies->len; i++)
    {
        LrgRigidBody *body = g_ptr_array_index (priv->bodies, i);
        gfloat pos_x, pos_y;
        gfloat half_w, half_h;

        lrg_rigid_body_get_position (body, &pos_x, &pos_y);
        lrg_rigid_body_get_shape_bounds (body, &half_w, &half_h);
        half_w *= 0.5f;
        half_h *= 0.5f;

        /* Check if point is inside AABB */
        if (x >= pos_x - half_w && x <= pos_x + half_w &&
            y >= pos_y - half_h && y <= pos_y + half_h)
        {
            g_ptr_array_add (result, body);
        }
    }

    return result;
}
