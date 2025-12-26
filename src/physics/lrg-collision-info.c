/* lrg-collision-info.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Collision information structure implementation.
 */

#include "config.h"
#include "lrg-collision-info.h"

/**
 * LrgCollisionInfo:
 *
 * Structure containing collision information between two bodies.
 */
struct _LrgCollisionInfo
{
    GObject *body_a;        /* First body in collision */
    GObject *body_b;        /* Second body in collision */
    gfloat   normal_x;      /* Normal X (from A to B) */
    gfloat   normal_y;      /* Normal Y (from A to B) */
    gfloat   penetration;   /* Penetration depth */
    gfloat   contact_x;     /* Contact point X */
    gfloat   contact_y;     /* Contact point Y */
};

G_DEFINE_BOXED_TYPE (LrgCollisionInfo, lrg_collision_info,
                     lrg_collision_info_copy,
                     lrg_collision_info_free)

/**
 * lrg_collision_info_new:
 * @body_a: First body in collision
 * @body_b: Second body in collision
 * @normal_x: Normal X component (from A to B)
 * @normal_y: Normal Y component (from A to B)
 * @penetration: Penetration depth
 * @contact_x: Contact point X
 * @contact_y: Contact point Y
 *
 * Creates a new collision info structure.
 *
 * Returns: (transfer full): A new #LrgCollisionInfo
 */
LrgCollisionInfo *
lrg_collision_info_new (GObject *body_a,
                        GObject *body_b,
                        gfloat   normal_x,
                        gfloat   normal_y,
                        gfloat   penetration,
                        gfloat   contact_x,
                        gfloat   contact_y)
{
    LrgCollisionInfo *self;

    self = g_slice_new (LrgCollisionInfo);

    self->body_a = body_a ? g_object_ref (body_a) : NULL;
    self->body_b = body_b ? g_object_ref (body_b) : NULL;
    self->normal_x = normal_x;
    self->normal_y = normal_y;
    self->penetration = penetration;
    self->contact_x = contact_x;
    self->contact_y = contact_y;

    return self;
}

/**
 * lrg_collision_info_copy:
 * @self: an #LrgCollisionInfo
 *
 * Creates a copy of the collision info.
 *
 * Returns: (transfer full): A copy of @self
 */
LrgCollisionInfo *
lrg_collision_info_copy (const LrgCollisionInfo *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return lrg_collision_info_new (self->body_a,
                                   self->body_b,
                                   self->normal_x,
                                   self->normal_y,
                                   self->penetration,
                                   self->contact_x,
                                   self->contact_y);
}

/**
 * lrg_collision_info_free:
 * @self: an #LrgCollisionInfo
 *
 * Frees the collision info.
 */
void
lrg_collision_info_free (LrgCollisionInfo *self)
{
    if (self == NULL)
        return;

    g_clear_object (&self->body_a);
    g_clear_object (&self->body_b);
    g_slice_free (LrgCollisionInfo, self);
}

/**
 * lrg_collision_info_get_body_a:
 * @self: an #LrgCollisionInfo
 *
 * Gets the first body in the collision.
 *
 * Returns: (transfer none) (nullable): The first body
 */
GObject *
lrg_collision_info_get_body_a (const LrgCollisionInfo *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->body_a;
}

/**
 * lrg_collision_info_get_body_b:
 * @self: an #LrgCollisionInfo
 *
 * Gets the second body in the collision.
 *
 * Returns: (transfer none) (nullable): The second body
 */
GObject *
lrg_collision_info_get_body_b (const LrgCollisionInfo *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->body_b;
}

/**
 * lrg_collision_info_get_normal:
 * @self: an #LrgCollisionInfo
 * @out_x: (out): Normal X component
 * @out_y: (out): Normal Y component
 *
 * Gets the collision normal (from body A to body B).
 */
void
lrg_collision_info_get_normal (const LrgCollisionInfo *self,
                               gfloat                 *out_x,
                               gfloat                 *out_y)
{
    g_return_if_fail (self != NULL);

    if (out_x)
        *out_x = self->normal_x;
    if (out_y)
        *out_y = self->normal_y;
}

/**
 * lrg_collision_info_get_penetration:
 * @self: an #LrgCollisionInfo
 *
 * Gets the penetration depth.
 *
 * Returns: The penetration depth
 */
gfloat
lrg_collision_info_get_penetration (const LrgCollisionInfo *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);

    return self->penetration;
}

/**
 * lrg_collision_info_get_contact_point:
 * @self: an #LrgCollisionInfo
 * @out_x: (out): Contact point X
 * @out_y: (out): Contact point Y
 *
 * Gets the contact point between the bodies.
 */
void
lrg_collision_info_get_contact_point (const LrgCollisionInfo *self,
                                      gfloat                 *out_x,
                                      gfloat                 *out_y)
{
    g_return_if_fail (self != NULL);

    if (out_x)
        *out_x = self->contact_x;
    if (out_y)
        *out_y = self->contact_y;
}
