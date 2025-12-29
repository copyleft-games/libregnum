/* lrg-ik-chain.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * IK bone chain for inverse kinematics.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-skeleton.h"

G_BEGIN_DECLS

#define LRG_TYPE_IK_CHAIN (lrg_ik_chain_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgIKChain, lrg_ik_chain, LRG, IK_CHAIN, GObject)

/**
 * lrg_ik_chain_new:
 * @skeleton: The skeleton containing the bones
 *
 * Creates a new IK chain.
 *
 * Returns: (transfer full): A new #LrgIKChain
 */
LRG_AVAILABLE_IN_ALL
LrgIKChain *    lrg_ik_chain_new                (LrgSkeleton    *skeleton);

/**
 * lrg_ik_chain_get_skeleton:
 * @self: A #LrgIKChain
 *
 * Gets the skeleton.
 *
 * Returns: (transfer none) (nullable): The skeleton
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton *   lrg_ik_chain_get_skeleton       (LrgIKChain     *self);

/**
 * lrg_ik_chain_add_bone:
 * @self: A #LrgIKChain
 * @bone_name: Bone name to add to chain
 *
 * Adds a bone to the chain. Bones should be added from
 * root to tip (base to end effector).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_chain_add_bone           (LrgIKChain     *self,
                                                  const gchar    *bone_name);

/**
 * lrg_ik_chain_clear_bones:
 * @self: A #LrgIKChain
 *
 * Removes all bones from the chain.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_chain_clear_bones        (LrgIKChain     *self);

/**
 * lrg_ik_chain_get_bone_count:
 * @self: A #LrgIKChain
 *
 * Gets the number of bones in the chain.
 *
 * Returns: The bone count
 */
LRG_AVAILABLE_IN_ALL
guint           lrg_ik_chain_get_bone_count     (LrgIKChain     *self);

/**
 * lrg_ik_chain_get_bone_name:
 * @self: A #LrgIKChain
 * @index: Bone index in chain
 *
 * Gets the bone name at an index.
 *
 * Returns: (transfer none) (nullable): The bone name
 */
LRG_AVAILABLE_IN_ALL
const gchar *   lrg_ik_chain_get_bone_name      (LrgIKChain     *self,
                                                  guint           index);

/**
 * lrg_ik_chain_get_bone:
 * @self: A #LrgIKChain
 * @index: Bone index in chain
 *
 * Gets the bone at an index.
 *
 * Returns: (transfer none) (nullable): The bone
 */
LRG_AVAILABLE_IN_ALL
LrgBone *       lrg_ik_chain_get_bone           (LrgIKChain     *self,
                                                  guint           index);

/**
 * lrg_ik_chain_get_target_position:
 * @self: A #LrgIKChain
 * @x: (out): X position
 * @y: (out): Y position
 * @z: (out): Z position
 *
 * Gets the target position for the end effector.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_chain_get_target_position (LrgIKChain    *self,
                                                   gfloat        *x,
                                                   gfloat        *y,
                                                   gfloat        *z);

/**
 * lrg_ik_chain_set_target_position:
 * @self: A #LrgIKChain
 * @x: X position
 * @y: Y position
 * @z: Z position
 *
 * Sets the target position for the end effector.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_chain_set_target_position (LrgIKChain    *self,
                                                   gfloat         x,
                                                   gfloat         y,
                                                   gfloat         z);

/**
 * lrg_ik_chain_get_pole_position:
 * @self: A #LrgIKChain
 * @x: (out): X position
 * @y: (out): Y position
 * @z: (out): Z position
 *
 * Gets the pole vector position (for knee/elbow direction).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_chain_get_pole_position  (LrgIKChain     *self,
                                                  gfloat         *x,
                                                  gfloat         *y,
                                                  gfloat         *z);

/**
 * lrg_ik_chain_set_pole_position:
 * @self: A #LrgIKChain
 * @x: X position
 * @y: Y position
 * @z: Z position
 *
 * Sets the pole vector position.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_chain_set_pole_position  (LrgIKChain     *self,
                                                  gfloat          x,
                                                  gfloat          y,
                                                  gfloat          z);

/**
 * lrg_ik_chain_get_total_length:
 * @self: A #LrgIKChain
 *
 * Gets the total length of the chain.
 *
 * Returns: Total bone lengths
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_ik_chain_get_total_length   (LrgIKChain     *self);

/**
 * lrg_ik_chain_get_end_effector_position:
 * @self: A #LrgIKChain
 * @x: (out): X position
 * @y: (out): Y position
 * @z: (out): Z position
 *
 * Gets the current end effector position in world space.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_chain_get_end_effector_position (LrgIKChain *self,
                                                         gfloat     *x,
                                                         gfloat     *y,
                                                         gfloat     *z);

/**
 * lrg_ik_chain_apply_to_skeleton:
 * @self: A #LrgIKChain
 *
 * Applies the chain's solved poses to the skeleton.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_chain_apply_to_skeleton  (LrgIKChain     *self);

G_END_DECLS
