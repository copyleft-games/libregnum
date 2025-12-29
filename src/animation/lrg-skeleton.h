/* lrg-skeleton.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Skeletal hierarchy for skeletal animation.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-bone.h"
#include "lrg-bone-pose.h"

G_BEGIN_DECLS

#define LRG_TYPE_SKELETON (lrg_skeleton_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgSkeleton, lrg_skeleton, LRG, SKELETON, GObject)

/**
 * LrgSkeletonClass:
 * @parent_class: Parent class
 * @calculate_world_poses: Virtual method to calculate world poses
 * @update: Virtual method called each frame
 *
 * The class structure for #LrgSkeleton.
 */
struct _LrgSkeletonClass
{
    GObjectClass parent_class;

    /* Virtual methods */

    /**
     * LrgSkeletonClass::calculate_world_poses:
     * @self: A #LrgSkeleton
     *
     * Calculates world poses for all bones based on local poses
     * and the bone hierarchy.
     */
    void    (*calculate_world_poses)    (LrgSkeleton    *self);

    /**
     * LrgSkeletonClass::update:
     * @self: A #LrgSkeleton
     * @delta_time: Time since last frame
     *
     * Updates the skeleton (called each frame).
     */
    void    (*update)                   (LrgSkeleton    *self,
                                         gfloat          delta_time);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_skeleton_new:
 *
 * Creates a new empty skeleton.
 *
 * Returns: (transfer full): A new #LrgSkeleton
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton *   lrg_skeleton_new                    (void);

/**
 * lrg_skeleton_add_bone:
 * @self: A #LrgSkeleton
 * @bone: (transfer none): Bone to add
 *
 * Adds a bone to the skeleton.
 * The bone's index should be unique within the skeleton.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_skeleton_add_bone               (LrgSkeleton        *self,
                                                     LrgBone            *bone);

/**
 * lrg_skeleton_remove_bone:
 * @self: A #LrgSkeleton
 * @bone: Bone to remove
 *
 * Removes a bone from the skeleton.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_skeleton_remove_bone            (LrgSkeleton        *self,
                                                     LrgBone            *bone);

/**
 * lrg_skeleton_get_bone:
 * @self: A #LrgSkeleton
 * @index: Bone index
 *
 * Gets a bone by index.
 *
 * Returns: (transfer none) (nullable): The bone, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgBone *       lrg_skeleton_get_bone               (LrgSkeleton        *self,
                                                     gint                index);

/**
 * lrg_skeleton_get_bone_by_name:
 * @self: A #LrgSkeleton
 * @name: Bone name
 *
 * Gets a bone by name.
 *
 * Returns: (transfer none) (nullable): The bone, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgBone *       lrg_skeleton_get_bone_by_name       (LrgSkeleton        *self,
                                                     const gchar        *name);

/**
 * lrg_skeleton_get_bone_count:
 * @self: A #LrgSkeleton
 *
 * Gets the number of bones.
 *
 * Returns: The bone count
 */
LRG_AVAILABLE_IN_ALL
guint           lrg_skeleton_get_bone_count         (LrgSkeleton        *self);

/**
 * lrg_skeleton_get_bones:
 * @self: A #LrgSkeleton
 *
 * Gets all bones.
 *
 * Returns: (transfer none) (element-type LrgBone): The bone list
 */
LRG_AVAILABLE_IN_ALL
GList *         lrg_skeleton_get_bones              (LrgSkeleton        *self);

/**
 * lrg_skeleton_get_root_bones:
 * @self: A #LrgSkeleton
 *
 * Gets all root bones (bones with no parent).
 *
 * Returns: (transfer container) (element-type LrgBone): List of root bones
 */
LRG_AVAILABLE_IN_ALL
GList *         lrg_skeleton_get_root_bones         (LrgSkeleton        *self);

/**
 * lrg_skeleton_get_children:
 * @self: A #LrgSkeleton
 * @bone: Parent bone
 *
 * Gets all direct children of a bone.
 *
 * Returns: (transfer container) (element-type LrgBone): List of child bones
 */
LRG_AVAILABLE_IN_ALL
GList *         lrg_skeleton_get_children           (LrgSkeleton        *self,
                                                     LrgBone            *bone);

/**
 * lrg_skeleton_calculate_world_poses:
 * @self: A #LrgSkeleton
 *
 * Calculates world poses for all bones.
 * Should be called after changing local poses.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_skeleton_calculate_world_poses  (LrgSkeleton        *self);

/**
 * lrg_skeleton_update:
 * @self: A #LrgSkeleton
 * @delta_time: Time since last frame
 *
 * Updates the skeleton.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_skeleton_update                 (LrgSkeleton        *self,
                                                     gfloat              delta_time);

/**
 * lrg_skeleton_reset_to_bind:
 * @self: A #LrgSkeleton
 *
 * Resets all bones to their bind poses.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_skeleton_reset_to_bind          (LrgSkeleton        *self);

/**
 * lrg_skeleton_set_pose:
 * @self: A #LrgSkeleton
 * @bone_index: Bone index
 * @pose: The pose to apply
 *
 * Sets the local pose for a specific bone.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_skeleton_set_pose               (LrgSkeleton        *self,
                                                     gint                bone_index,
                                                     const LrgBonePose  *pose);

/**
 * lrg_skeleton_blend_pose:
 * @self: A #LrgSkeleton
 * @bone_index: Bone index
 * @pose: The pose to blend
 * @weight: Blend weight (0.0 to 1.0)
 *
 * Blends a pose with the current pose for a bone.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_skeleton_blend_pose             (LrgSkeleton        *self,
                                                     gint                bone_index,
                                                     const LrgBonePose  *pose,
                                                     gfloat              weight);

/**
 * lrg_skeleton_get_name:
 * @self: A #LrgSkeleton
 *
 * Gets the skeleton name.
 *
 * Returns: (transfer none) (nullable): The name
 */
LRG_AVAILABLE_IN_ALL
const gchar *   lrg_skeleton_get_name               (LrgSkeleton        *self);

/**
 * lrg_skeleton_set_name:
 * @self: A #LrgSkeleton
 * @name: (nullable): The skeleton name
 *
 * Sets the skeleton name.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_skeleton_set_name               (LrgSkeleton        *self,
                                                     const gchar        *name);

/**
 * lrg_skeleton_copy:
 * @self: A #LrgSkeleton
 *
 * Creates a deep copy of the skeleton.
 *
 * Returns: (transfer full): A new skeleton copy
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton *   lrg_skeleton_copy                   (LrgSkeleton        *self);

G_END_DECLS
