/* lrg-animation-state.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Animation state for state machines.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-animation-clip.h"

G_BEGIN_DECLS

#define LRG_TYPE_ANIMATION_STATE (lrg_animation_state_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgAnimationState, lrg_animation_state, LRG, ANIMATION_STATE, GObject)

/**
 * LrgAnimationStateClass:
 * @parent_class: Parent class
 * @enter: Called when entering this state
 * @exit: Called when exiting this state
 * @update: Called each frame while in this state
 * @sample: Sample the state's animation at current time
 *
 * Class structure for #LrgAnimationState.
 */
struct _LrgAnimationStateClass
{
    GObjectClass parent_class;

    /* Virtual methods */
    void (*enter)  (LrgAnimationState *self);
    void (*exit)   (LrgAnimationState *self);
    void (*update) (LrgAnimationState *self,
                    gfloat             delta_time);
    void (*sample) (LrgAnimationState *self,
                    LrgBonePose       *out_pose,
                    const gchar       *bone_name);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_animation_state_new:
 * @name: State name
 *
 * Creates a new animation state.
 *
 * Returns: (transfer full): A new #LrgAnimationState
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationState * lrg_animation_state_new             (const gchar        *name);

/**
 * lrg_animation_state_get_name:
 * @self: A #LrgAnimationState
 *
 * Gets the state name.
 *
 * Returns: (transfer none): The state name
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_animation_state_get_name        (LrgAnimationState  *self);

/**
 * lrg_animation_state_get_clip:
 * @self: A #LrgAnimationState
 *
 * Gets the animation clip for this state.
 *
 * Returns: (transfer none) (nullable): The animation clip
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationClip *  lrg_animation_state_get_clip        (LrgAnimationState  *self);

/**
 * lrg_animation_state_set_clip:
 * @self: A #LrgAnimationState
 * @clip: (nullable): The animation clip
 *
 * Sets the animation clip for this state.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_state_set_clip        (LrgAnimationState  *self,
                                                         LrgAnimationClip   *clip);

/**
 * lrg_animation_state_get_speed:
 * @self: A #LrgAnimationState
 *
 * Gets the playback speed multiplier.
 *
 * Returns: The speed multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_animation_state_get_speed       (LrgAnimationState  *self);

/**
 * lrg_animation_state_set_speed:
 * @self: A #LrgAnimationState
 * @speed: The speed multiplier
 *
 * Sets the playback speed.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_state_set_speed       (LrgAnimationState  *self,
                                                         gfloat              speed);

/**
 * lrg_animation_state_get_mirror:
 * @self: A #LrgAnimationState
 *
 * Gets whether the animation is mirrored.
 *
 * Returns: %TRUE if mirrored
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_animation_state_get_mirror      (LrgAnimationState  *self);

/**
 * lrg_animation_state_set_mirror:
 * @self: A #LrgAnimationState
 * @mirror: Whether to mirror
 *
 * Sets whether to mirror the animation.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_state_set_mirror      (LrgAnimationState  *self,
                                                         gboolean            mirror);

/**
 * lrg_animation_state_get_time:
 * @self: A #LrgAnimationState
 *
 * Gets the current playback time.
 *
 * Returns: The time in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_animation_state_get_time        (LrgAnimationState  *self);

/**
 * lrg_animation_state_set_time:
 * @self: A #LrgAnimationState
 * @time: The time in seconds
 *
 * Sets the playback time.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_state_set_time        (LrgAnimationState  *self,
                                                         gfloat              time);

/**
 * lrg_animation_state_get_normalized_time:
 * @self: A #LrgAnimationState
 *
 * Gets the normalized playback time (0.0 to 1.0).
 *
 * Returns: The normalized time
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_animation_state_get_normalized_time (LrgAnimationState *self);

/**
 * lrg_animation_state_enter:
 * @self: A #LrgAnimationState
 *
 * Called when entering this state.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_state_enter           (LrgAnimationState  *self);

/**
 * lrg_animation_state_exit:
 * @self: A #LrgAnimationState
 *
 * Called when exiting this state.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_state_exit            (LrgAnimationState  *self);

/**
 * lrg_animation_state_update:
 * @self: A #LrgAnimationState
 * @delta_time: Time since last frame
 *
 * Updates the state.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_state_update          (LrgAnimationState  *self,
                                                         gfloat              delta_time);

/**
 * lrg_animation_state_sample:
 * @self: A #LrgAnimationState
 * @out_pose: (out): Output pose
 * @bone_name: Bone name to sample
 *
 * Samples the animation for a specific bone.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_state_sample          (LrgAnimationState  *self,
                                                         LrgBonePose        *out_pose,
                                                         const gchar        *bone_name);

G_END_DECLS
