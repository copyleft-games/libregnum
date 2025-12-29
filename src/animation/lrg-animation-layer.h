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

G_END_DECLS
