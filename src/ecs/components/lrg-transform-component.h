/* lrg-transform-component.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Transform component with parent/child hierarchy.
 *
 * LrgTransformComponent provides hierarchical transforms where child
 * transforms are relative to their parent. This allows for scene graphs
 * where moving a parent automatically moves all children.
 *
 * The component stores local position, rotation, and scale. World-space
 * values are computed by combining with parent transforms.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>

#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../lrg-component.h"

G_BEGIN_DECLS

#define LRG_TYPE_TRANSFORM_COMPONENT (lrg_transform_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgTransformComponent, lrg_transform_component,
                          LRG, TRANSFORM_COMPONENT, LrgComponent)

/**
 * LrgTransformComponentClass:
 * @parent_class: The parent class
 *
 * The class structure for #LrgTransformComponent.
 */
struct _LrgTransformComponentClass
{
    LrgComponentClass parent_class;

    /*< private >*/
    gpointer _reserved[8];
};

/*
 * Construction
 */

/**
 * lrg_transform_component_new:
 *
 * Creates a new transform component at position (0, 0).
 *
 * Returns: (transfer full): A new #LrgTransformComponent
 */
LRG_AVAILABLE_IN_ALL
LrgTransformComponent * lrg_transform_component_new         (void);

/**
 * lrg_transform_component_new_at:
 * @x: Initial local X position
 * @y: Initial local Y position
 *
 * Creates a new transform component at the specified local position.
 *
 * Returns: (transfer full): A new #LrgTransformComponent
 */
LRG_AVAILABLE_IN_ALL
LrgTransformComponent * lrg_transform_component_new_at      (gfloat x,
                                                             gfloat y);

/*
 * Local Transform (relative to parent)
 */

/**
 * lrg_transform_component_get_local_position:
 * @self: an #LrgTransformComponent
 *
 * Gets the local position relative to parent.
 *
 * Returns: (transfer full): A new #GrlVector2 with the local position
 */
LRG_AVAILABLE_IN_ALL
GrlVector2 * lrg_transform_component_get_local_position     (LrgTransformComponent *self);

/**
 * lrg_transform_component_set_local_position:
 * @self: an #LrgTransformComponent
 * @position: The local position
 *
 * Sets the local position relative to parent.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_transform_component_set_local_position     (LrgTransformComponent *self,
                                                             GrlVector2            *position);

/**
 * lrg_transform_component_set_local_position_xy:
 * @self: an #LrgTransformComponent
 * @x: Local X position
 * @y: Local Y position
 *
 * Sets the local position using X and Y coordinates.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_transform_component_set_local_position_xy  (LrgTransformComponent *self,
                                                             gfloat                 x,
                                                             gfloat                 y);

/**
 * lrg_transform_component_get_local_x:
 * @self: an #LrgTransformComponent
 *
 * Gets the local X position.
 *
 * Returns: The local X coordinate
 */
LRG_AVAILABLE_IN_ALL
gfloat       lrg_transform_component_get_local_x            (LrgTransformComponent *self);

/**
 * lrg_transform_component_set_local_x:
 * @self: an #LrgTransformComponent
 * @x: The local X coordinate
 *
 * Sets the local X position.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_transform_component_set_local_x            (LrgTransformComponent *self,
                                                             gfloat                 x);

/**
 * lrg_transform_component_get_local_y:
 * @self: an #LrgTransformComponent
 *
 * Gets the local Y position.
 *
 * Returns: The local Y coordinate
 */
LRG_AVAILABLE_IN_ALL
gfloat       lrg_transform_component_get_local_y            (LrgTransformComponent *self);

/**
 * lrg_transform_component_set_local_y:
 * @self: an #LrgTransformComponent
 * @y: The local Y coordinate
 *
 * Sets the local Y position.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_transform_component_set_local_y            (LrgTransformComponent *self,
                                                             gfloat                 y);

/**
 * lrg_transform_component_get_local_rotation:
 * @self: an #LrgTransformComponent
 *
 * Gets the local rotation in degrees.
 *
 * Returns: The local rotation in degrees
 */
LRG_AVAILABLE_IN_ALL
gfloat       lrg_transform_component_get_local_rotation     (LrgTransformComponent *self);

/**
 * lrg_transform_component_set_local_rotation:
 * @self: an #LrgTransformComponent
 * @rotation: Local rotation in degrees
 *
 * Sets the local rotation in degrees.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_transform_component_set_local_rotation     (LrgTransformComponent *self,
                                                             gfloat                 rotation);

/**
 * lrg_transform_component_get_local_scale:
 * @self: an #LrgTransformComponent
 *
 * Gets the local scale.
 *
 * Returns: (transfer full): A new #GrlVector2 with the local scale (x, y)
 */
LRG_AVAILABLE_IN_ALL
GrlVector2 * lrg_transform_component_get_local_scale        (LrgTransformComponent *self);

/**
 * lrg_transform_component_set_local_scale:
 * @self: an #LrgTransformComponent
 * @scale: The local scale (x, y)
 *
 * Sets the local scale.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_transform_component_set_local_scale        (LrgTransformComponent *self,
                                                             GrlVector2            *scale);

/**
 * lrg_transform_component_set_local_scale_xy:
 * @self: an #LrgTransformComponent
 * @scale_x: X scale factor
 * @scale_y: Y scale factor
 *
 * Sets the local scale using separate X and Y factors.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_transform_component_set_local_scale_xy     (LrgTransformComponent *self,
                                                             gfloat                 scale_x,
                                                             gfloat                 scale_y);

/**
 * lrg_transform_component_set_local_scale_uniform:
 * @self: an #LrgTransformComponent
 * @scale: Uniform scale factor
 *
 * Sets uniform scale for both X and Y.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_transform_component_set_local_scale_uniform (LrgTransformComponent *self,
                                                              gfloat                 scale);

/*
 * World Transform (absolute, computed from hierarchy)
 */

/**
 * lrg_transform_component_get_world_position:
 * @self: an #LrgTransformComponent
 *
 * Gets the world-space position (combining all parent transforms).
 *
 * Returns: (transfer full): A new #GrlVector2 with the world position
 */
LRG_AVAILABLE_IN_ALL
GrlVector2 * lrg_transform_component_get_world_position     (LrgTransformComponent *self);

/**
 * lrg_transform_component_get_world_rotation:
 * @self: an #LrgTransformComponent
 *
 * Gets the world-space rotation in degrees (combining all parent rotations).
 *
 * Returns: The world rotation in degrees
 */
LRG_AVAILABLE_IN_ALL
gfloat       lrg_transform_component_get_world_rotation     (LrgTransformComponent *self);

/**
 * lrg_transform_component_get_world_scale:
 * @self: an #LrgTransformComponent
 *
 * Gets the world-space scale (combining all parent scales).
 *
 * Returns: (transfer full): A new #GrlVector2 with the world scale
 */
LRG_AVAILABLE_IN_ALL
GrlVector2 * lrg_transform_component_get_world_scale        (LrgTransformComponent *self);

/*
 * Parent/Child Hierarchy
 */

/**
 * lrg_transform_component_get_parent:
 * @self: an #LrgTransformComponent
 *
 * Gets the parent transform.
 *
 * Returns: (transfer none) (nullable): The parent transform, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgTransformComponent * lrg_transform_component_get_parent  (LrgTransformComponent *self);

/**
 * lrg_transform_component_set_parent:
 * @self: an #LrgTransformComponent
 * @parent: (nullable): The new parent transform, or %NULL to unparent
 *
 * Sets the parent transform. The local position becomes relative to the parent.
 * Setting to %NULL removes the parent relationship.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_transform_component_set_parent             (LrgTransformComponent *self,
                                                             LrgTransformComponent *parent);

/**
 * lrg_transform_component_get_children:
 * @self: an #LrgTransformComponent
 *
 * Gets a list of all child transforms.
 *
 * Returns: (transfer container) (element-type LrgTransformComponent): List of children
 */
LRG_AVAILABLE_IN_ALL
GList *      lrg_transform_component_get_children           (LrgTransformComponent *self);

/**
 * lrg_transform_component_get_child_count:
 * @self: an #LrgTransformComponent
 *
 * Gets the number of child transforms.
 *
 * Returns: The child count
 */
LRG_AVAILABLE_IN_ALL
guint        lrg_transform_component_get_child_count        (LrgTransformComponent *self);

/**
 * lrg_transform_component_detach_children:
 * @self: an #LrgTransformComponent
 *
 * Removes all children from this transform.
 * Children become root transforms with their current world position.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_transform_component_detach_children        (LrgTransformComponent *self);

/*
 * Utility
 */

/**
 * lrg_transform_component_translate:
 * @self: an #LrgTransformComponent
 * @offset: Translation offset in local space
 *
 * Translates the transform by the given offset.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_transform_component_translate              (LrgTransformComponent *self,
                                                             GrlVector2            *offset);

/**
 * lrg_transform_component_rotate:
 * @self: an #LrgTransformComponent
 * @degrees: Rotation amount in degrees
 *
 * Rotates the transform by the given amount.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_transform_component_rotate                 (LrgTransformComponent *self,
                                                             gfloat                 degrees);

/**
 * lrg_transform_component_look_at:
 * @self: an #LrgTransformComponent
 * @target: World-space target position
 *
 * Rotates the transform to face the target position.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_transform_component_look_at                (LrgTransformComponent *self,
                                                             GrlVector2            *target);

/**
 * lrg_transform_component_sync_to_entity:
 * @self: an #LrgTransformComponent
 *
 * Syncs the world transform to the owning game object's entity transform.
 * Call this after modifying the transform to update the entity.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_transform_component_sync_to_entity         (LrgTransformComponent *self);

/*
 * 3D Rotation (Quaternion)
 */

/**
 * lrg_transform_component_get_rotation_quaternion:
 * @self: an #LrgTransformComponent
 *
 * Gets the local rotation as a quaternion.
 *
 * For 2D transforms, this returns a quaternion representing
 * rotation around the Z axis.
 *
 * Returns: (transfer full): A new #GrlQuaternion with the local rotation
 */
LRG_AVAILABLE_IN_ALL
GrlQuaternion * lrg_transform_component_get_rotation_quaternion (LrgTransformComponent *self);

/**
 * lrg_transform_component_set_rotation_quaternion:
 * @self: an #LrgTransformComponent
 * @quaternion: The rotation quaternion
 *
 * Sets the local rotation from a quaternion.
 *
 * The quaternion is converted to Euler angles internally.
 * For 2D transforms, only the Z rotation (roll) component is used.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_transform_component_set_rotation_quaternion (LrgTransformComponent *self,
                                                               const GrlQuaternion   *quaternion);

/**
 * lrg_transform_component_set_rotation_euler:
 * @self: an #LrgTransformComponent
 * @pitch: Rotation around X axis in radians
 * @yaw: Rotation around Y axis in radians
 * @roll: Rotation around Z axis in radians
 *
 * Sets the local rotation from Euler angles.
 *
 * For 2D transforms, only the roll (Z rotation) is used and is
 * converted to degrees for internal storage.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_transform_component_set_rotation_euler      (LrgTransformComponent *self,
                                                               gfloat                 pitch,
                                                               gfloat                 yaw,
                                                               gfloat                 roll);

/**
 * lrg_transform_component_slerp_rotation:
 * @self: an #LrgTransformComponent
 * @target: Target rotation quaternion
 * @amount: Interpolation amount (0.0 to 1.0)
 *
 * Spherically interpolates the local rotation toward the target.
 *
 * Uses SLERP (spherical linear interpolation) for smooth rotation
 * transitions that maintain constant angular velocity.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_transform_component_slerp_rotation          (LrgTransformComponent *self,
                                                               const GrlQuaternion   *target,
                                                               gfloat                 amount);

G_END_DECLS
