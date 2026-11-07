/* lrg-animation-layer.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Animation layer for layered blending.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-animation-state.h"
#include "lrg-bone-pose.h"
#include "lrg-skeleton.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_ANIMATION_LAYER (lrg_animation_layer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAnimationLayer, lrg_animation_layer, LRG, ANIMATION_LAYER, GObject)

/**
 * lrg_animation_layer_new:
 * @name: Layer name
 *
 * Creates a new animation layer.
 *
 * Returns: (transfer full): A new #LrgAnimationLayer
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationLayer * lrg_animation_layer_new             (const gchar        *name);

/**
 * lrg_animation_layer_get_name:
 * @self: A #LrgAnimationLayer
 *
 * Gets the layer name.
 *
 * Returns: (transfer none): The layer name
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_animation_layer_get_name        (LrgAnimationLayer  *self);

/**
 * lrg_animation_layer_get_weight:
 * @self: A #LrgAnimationLayer
 *
 * Gets the layer blend weight.
 *
 * Returns: The weight (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_animation_layer_get_weight      (LrgAnimationLayer  *self);

/**
 * lrg_animation_layer_set_weight:
 * @self: A #LrgAnimationLayer
 * @weight: The weight (0.0 to 1.0)
 *
 * Sets the layer blend weight.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_layer_set_weight      (LrgAnimationLayer  *self,
                                                          gfloat              weight);

/**
 * lrg_animation_layer_get_blend_mode:
 * @self: A #LrgAnimationLayer
 *
 * Gets the blend mode.
 *
 * Returns: The blend mode
 */
LRG_AVAILABLE_IN_ALL
LrgLayerBlendMode   lrg_animation_layer_get_blend_mode  (LrgAnimationLayer  *self);

/**
 * lrg_animation_layer_set_blend_mode:
 * @self: A #LrgAnimationLayer
 * @mode: The blend mode
 *
 * Sets the blend mode.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_layer_set_blend_mode  (LrgAnimationLayer  *self,
                                                          LrgLayerBlendMode   mode);

/**
 * lrg_animation_layer_get_state:
 * @self: A #LrgAnimationLayer
 *
 * Gets the current animation state.
 *
 * Returns: (transfer none) (nullable): The state
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationState * lrg_animation_layer_get_state       (LrgAnimationLayer  *self);

/**
 * lrg_animation_layer_set_state:
 * @self: A #LrgAnimationLayer
 * @state: (nullable): The animation state
 *
 * Sets the animation state for this layer.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_layer_set_state       (LrgAnimationLayer  *self,
                                                          LrgAnimationState  *state);

/**
 * lrg_animation_layer_add_mask:
 * @self: A #LrgAnimationLayer
 * @bone_name: Bone to include in mask
 *
 * Adds a bone to the layer mask. Only masked bones are affected.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_layer_add_mask        (LrgAnimationLayer  *self,
                                                          const gchar        *bone_name);

/**
 * lrg_animation_layer_remove_mask:
 * @self: A #LrgAnimationLayer
 * @bone_name: Bone to remove from mask
 *
 * Removes a bone from the layer mask.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_layer_remove_mask     (LrgAnimationLayer  *self,
                                                          const gchar        *bone_name);

/**
 * lrg_animation_layer_clear_mask:
 * @self: A #LrgAnimationLayer
 *
 * Clears the bone mask (affects all bones).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_layer_clear_mask      (LrgAnimationLayer  *self);

/**
 * lrg_animation_layer_is_bone_masked:
 * @self: A #LrgAnimationLayer
 * @bone_name: Bone to check
 *
 * Checks if a bone is in the mask.
 *
 * Returns: %TRUE if masked or no mask set
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_animation_layer_is_bone_masked  (LrgAnimationLayer  *self,
                                                          const gchar        *bone_name);

/**
 * lrg_animation_layer_get_enabled:
 * @self: A #LrgAnimationLayer
 *
 * Checks if the layer is enabled.
 *
 * Returns: %TRUE if enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_animation_layer_get_enabled     (LrgAnimationLayer  *self);

/**
 * lrg_animation_layer_set_enabled:
 * @self: A #LrgAnimationLayer
 * @enabled: Whether enabled
 *
 * Sets whether the layer is enabled.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_layer_set_enabled     (LrgAnimationLayer  *self,
                                                          gboolean            enabled);

/**
 * lrg_animation_layer_update:
 * @self: A #LrgAnimationLayer
 * @delta_time: Time since last frame
 *
 * Updates the layer.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_layer_update          (LrgAnimationLayer  *self,
                                                          gfloat              delta_time);

/**
 * lrg_animation_layer_apply:
 * @self: A #LrgAnimationLayer
 * @base_pose: (inout): The base pose to blend onto
 * @bone_name: Bone name
 *
 * Applies this layer's animation to the base pose.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_layer_apply           (LrgAnimationLayer  *self,
                                                          LrgBonePose        *base_pose,
                                                          const gchar        *bone_name);

/* ==========================================================================
 * 0.9.0: a state machine per layer, ordered and recursive masks, whole
 * skeleton blending
 * ========================================================================== */

/**
 * lrg_animation_layer_get_state_machine:
 * @self: A #LrgAnimationLayer
 *
 * Returns: (transfer none) (nullable): the layer's state machine
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationStateMachine * lrg_animation_layer_get_state_machine (LrgAnimationLayer *self);

/**
 * lrg_animation_layer_set_state_machine:
 * @self: A #LrgAnimationLayer
 * @machine: (nullable): the machine the layer plays
 *
 * Makes the layer play a state machine (an #LrgAnimGraph layer always
 * has one). It takes precedence over lrg_animation_layer_set_state().
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_layer_set_state_machine (LrgAnimationLayer        *self,
                                                           LrgAnimationStateMachine *machine);

/**
 * lrg_animation_layer_get_mask:
 * @self: A #LrgAnimationLayer
 *
 * Returns: (transfer full) (array zero-terminated=1) (nullable): the
 *   mask's bone names in the order they were added, %NULL without a mask
 */
LRG_AVAILABLE_IN_ALL
gchar **            lrg_animation_layer_get_mask        (LrgAnimationLayer  *self);

/**
 * lrg_animation_layer_get_mask_recursive:
 * @self: A #LrgAnimationLayer
 *
 * Returns: whether a masked bone also covers its descendants
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_animation_layer_get_mask_recursive (LrgAnimationLayer *self);

/**
 * lrg_animation_layer_set_mask_recursive:
 * @self: A #LrgAnimationLayer
 * @recursive: whether masked bones cover their descendants
 *
 * With a recursive mask (the default) listing `spine` affects the spine
 * and everything below it (arms, head); otherwise only the listed bones.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_layer_set_mask_recursive (LrgAnimationLayer *self,
                                                            gboolean           recursive);

/**
 * lrg_animation_layer_get_bone_weights:
 * @self: A #LrgAnimationLayer
 * @skeleton: the skeleton
 * @out_weights: (array length=n_weights) (out caller-allocates): one
 *   factor per bone index, 1 inside the mask and 0 outside
 * @n_weights: number of entries
 *
 * Resolves the mask against a skeleton (the editor paints masked bones
 * with it). Without a mask every factor is 1.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_layer_get_bone_weights (LrgAnimationLayer *self,
                                                          LrgSkeleton       *skeleton,
                                                          gfloat            *out_weights,
                                                          guint              n_weights);

/**
 * lrg_animation_layer_sample_pose:
 * @self: A #LrgAnimationLayer
 * @skeleton: the skeleton
 * @poses: (array length=n_poses) (out caller-allocates): the layer's own
 *   pose
 * @n_poses: number of entries
 *
 * Samples the layer's machine (or state) without blending it.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_layer_sample_pose     (LrgAnimationLayer  *self,
                                                         LrgSkeleton        *skeleton,
                                                         LrgBonePose        *poses,
                                                         guint               n_poses);

/**
 * lrg_animation_layer_blend_pose:
 * @self: A #LrgAnimationLayer
 * @skeleton: the skeleton
 * @poses: (array length=n_poses) (inout): the pose of the layers below,
 *   blended in place
 * @n_poses: number of entries
 *
 * Samples the layer and blends it over @poses: override layers lerp by
 * weight x mask, additive layers add their difference from the bind
 * pose times weight x mask. A disabled or zero-weight layer leaves
 * @poses alone.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_layer_blend_pose      (LrgAnimationLayer  *self,
                                                         LrgSkeleton        *skeleton,
                                                         LrgBonePose        *poses,
                                                         guint               n_poses);

G_END_DECLS
