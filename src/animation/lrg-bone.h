/* lrg-bone.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Individual bone in a skeleton hierarchy.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-bone-pose.h"

G_BEGIN_DECLS

#define LRG_TYPE_BONE (lrg_bone_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBone, lrg_bone, LRG, BONE, GObject)

/**
 * lrg_bone_new:
 * @name: Bone name
 * @index: Bone index in the skeleton
 *
 * Creates a new bone with the given name and index.
 *
 * Returns: (transfer full): A new #LrgBone
 */
LRG_AVAILABLE_IN_ALL
LrgBone *       lrg_bone_new                    (const gchar        *name,
                                                 gint                index);

/**
 * lrg_bone_get_name:
 * @self: A #LrgBone
 *
 * Gets the bone name.
 *
 * Returns: (transfer none): The bone name
 */
LRG_AVAILABLE_IN_ALL
const gchar *   lrg_bone_get_name               (LrgBone            *self);

/**
 * lrg_bone_get_index:
 * @self: A #LrgBone
 *
 * Gets the bone index in the skeleton.
 *
 * Returns: The bone index
 */
LRG_AVAILABLE_IN_ALL
gint            lrg_bone_get_index              (LrgBone            *self);

/**
 * lrg_bone_get_parent_index:
 * @self: A #LrgBone
 *
 * Gets the parent bone index, or -1 if this is a root bone.
 *
 * Returns: The parent index, or -1
 */
LRG_AVAILABLE_IN_ALL
gint            lrg_bone_get_parent_index       (LrgBone            *self);

/**
 * lrg_bone_set_parent_index:
 * @self: A #LrgBone
 * @parent_index: Parent bone index, or -1 for root
 *
 * Sets the parent bone index.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_bone_set_parent_index       (LrgBone            *self,
                                                 gint                parent_index);

/**
 * lrg_bone_is_root:
 * @self: A #LrgBone
 *
 * Checks if this is a root bone (no parent).
 *
 * Returns: %TRUE if root bone
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_bone_is_root                (LrgBone            *self);

/**
 * lrg_bone_get_bind_pose:
 * @self: A #LrgBone
 *
 * Gets the bone's bind pose (rest pose).
 *
 * Returns: (transfer none): The bind pose
 */
LRG_AVAILABLE_IN_ALL
const LrgBonePose * lrg_bone_get_bind_pose      (LrgBone            *self);

/**
 * lrg_bone_set_bind_pose:
 * @self: A #LrgBone
 * @pose: The bind pose to set
 *
 * Sets the bone's bind pose.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_bone_set_bind_pose          (LrgBone            *self,
                                                 const LrgBonePose  *pose);

/**
 * lrg_bone_get_local_pose:
 * @self: A #LrgBone
 *
 * Gets the bone's current local pose.
 *
 * Returns: (transfer none): The local pose
 */
LRG_AVAILABLE_IN_ALL
const LrgBonePose * lrg_bone_get_local_pose     (LrgBone            *self);

/**
 * lrg_bone_set_local_pose:
 * @self: A #LrgBone
 * @pose: The local pose to set
 *
 * Sets the bone's current local pose.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_bone_set_local_pose         (LrgBone            *self,
                                                 const LrgBonePose  *pose);

/**
 * lrg_bone_get_world_pose:
 * @self: A #LrgBone
 *
 * Gets the bone's world (accumulated) pose.
 * This includes all parent transformations.
 *
 * Returns: (transfer none): The world pose
 */
LRG_AVAILABLE_IN_ALL
const LrgBonePose * lrg_bone_get_world_pose     (LrgBone            *self);

/**
 * lrg_bone_set_world_pose:
 * @self: A #LrgBone
 * @pose: The world pose to set
 *
 * Sets the bone's world pose directly.
 * Normally set by the skeleton during pose calculation.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_bone_set_world_pose         (LrgBone            *self,
                                                 const LrgBonePose  *pose);

/**
 * lrg_bone_reset_to_bind:
 * @self: A #LrgBone
 *
 * Resets the bone's local pose to the bind pose.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_bone_reset_to_bind          (LrgBone            *self);

/**
 * lrg_bone_get_length:
 * @self: A #LrgBone
 *
 * Gets the bone length (distance to first child or end effector).
 *
 * Returns: The bone length
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_bone_get_length             (LrgBone            *self);

/**
 * lrg_bone_set_length:
 * @self: A #LrgBone
 * @length: The bone length
 *
 * Sets the bone length.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_bone_set_length             (LrgBone            *self,
                                                 gfloat              length);

G_END_DECLS
