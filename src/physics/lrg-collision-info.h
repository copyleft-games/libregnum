/* lrg-collision-info.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Collision information structure (boxed type).
 */

#ifndef LRG_COLLISION_INFO_H
#define LRG_COLLISION_INFO_H

#include <glib-object.h>
#include "lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_COLLISION_INFO (lrg_collision_info_get_type ())

/**
 * LrgCollisionInfo:
 *
 * Contains information about a collision between two bodies.
 * This is a boxed type for use in signals and callbacks.
 */
typedef struct _LrgCollisionInfo LrgCollisionInfo;

LRG_AVAILABLE_IN_ALL
GType               lrg_collision_info_get_type          (void) G_GNUC_CONST;

/**
 * lrg_collision_info_new:
 * @body_a: (transfer none): First body in collision
 * @body_b: (transfer none): Second body in collision
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
LRG_AVAILABLE_IN_ALL
LrgCollisionInfo *  lrg_collision_info_new               (GObject  *body_a,
                                                          GObject  *body_b,
                                                          gfloat    normal_x,
                                                          gfloat    normal_y,
                                                          gfloat    penetration,
                                                          gfloat    contact_x,
                                                          gfloat    contact_y);

/**
 * lrg_collision_info_copy:
 * @self: an #LrgCollisionInfo
 *
 * Creates a copy of the collision info.
 *
 * Returns: (transfer full): A copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgCollisionInfo *  lrg_collision_info_copy              (const LrgCollisionInfo *self);

/**
 * lrg_collision_info_free:
 * @self: (transfer full): an #LrgCollisionInfo
 *
 * Frees the collision info.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_collision_info_free              (LrgCollisionInfo *self);

/**
 * lrg_collision_info_get_body_a:
 * @self: an #LrgCollisionInfo
 *
 * Gets the first body in the collision.
 *
 * Returns: (transfer none) (nullable): The first body
 */
LRG_AVAILABLE_IN_ALL
GObject *           lrg_collision_info_get_body_a        (const LrgCollisionInfo *self);

/**
 * lrg_collision_info_get_body_b:
 * @self: an #LrgCollisionInfo
 *
 * Gets the second body in the collision.
 *
 * Returns: (transfer none) (nullable): The second body
 */
LRG_AVAILABLE_IN_ALL
GObject *           lrg_collision_info_get_body_b        (const LrgCollisionInfo *self);

/**
 * lrg_collision_info_get_normal:
 * @self: an #LrgCollisionInfo
 * @out_x: (out): Normal X component
 * @out_y: (out): Normal Y component
 *
 * Gets the collision normal (from body A to body B).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_collision_info_get_normal        (const LrgCollisionInfo *self,
                                                          gfloat                 *out_x,
                                                          gfloat                 *out_y);

/**
 * lrg_collision_info_get_penetration:
 * @self: an #LrgCollisionInfo
 *
 * Gets the penetration depth.
 *
 * Returns: The penetration depth
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_collision_info_get_penetration   (const LrgCollisionInfo *self);

/**
 * lrg_collision_info_get_contact_point:
 * @self: an #LrgCollisionInfo
 * @out_x: (out): Contact point X
 * @out_y: (out): Contact point Y
 *
 * Gets the contact point between the bodies.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_collision_info_get_contact_point (const LrgCollisionInfo *self,
                                                          gfloat                 *out_x,
                                                          gfloat                 *out_y);

G_END_DECLS

#endif /* LRG_COLLISION_INFO_H */
