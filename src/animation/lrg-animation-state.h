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
#include "../lrg-types.h"
#include "lrg-animation-clip.h"
#include "lrg-skeleton.h"

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

/* ==========================================================================
 * Motion sources (0.9.0): a state plays a clip, a blend tree or a nested
 * state machine. Setting one does not clear the others; the first set of
 * sub-machine, blend tree, clip (in that order) is what the state plays.
 * ========================================================================== */

/**
 * lrg_animation_state_get_blend_tree:
 * @self: A #LrgAnimationState
 *
 * Gets the blend tree the state plays instead of a single clip.
 *
 * Returns: (transfer none) (nullable): the blend tree
 */
LRG_AVAILABLE_IN_ALL
LrgBlendTree *      lrg_animation_state_get_blend_tree  (LrgAnimationState  *self);

/**
 * lrg_animation_state_set_blend_tree:
 * @self: A #LrgAnimationState
 * @tree: (nullable): the blend tree to play
 *
 * Makes the state play @tree (a 1D/2D/direct blend of clips or nested
 * trees). The owning state machine feeds the tree's named parameters
 * (see lrg_blend_tree_set_parameter_name()) before each update.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_state_set_blend_tree  (LrgAnimationState  *self,
                                                         LrgBlendTree       *tree);

/**
 * lrg_animation_state_get_sub_machine:
 * @self: A #LrgAnimationState
 *
 * Gets the nested state machine the state runs.
 *
 * Returns: (transfer none) (nullable): the sub-machine
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationStateMachine * lrg_animation_state_get_sub_machine (LrgAnimationState *self);

/**
 * lrg_animation_state_set_sub_machine:
 * @self: A #LrgAnimationState
 * @machine: (nullable): the nested state machine
 *
 * Makes the state run @machine: entering the state starts it at its
 * default state, leaving stops it, and the state's pose is the
 * sub-machine's pose. A sub-machine reads the parameters of the machine
 * that owns the state.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_state_set_sub_machine (LrgAnimationState        *self,
                                                         LrgAnimationStateMachine *machine);

/**
 * lrg_animation_state_get_clip_name:
 * @self: A #LrgAnimationState
 *
 * Gets the clip key this state refers to in an #LrgAnimGraph (the name
 * the .ranim file uses under `clips:`). It survives a missing clip, so a
 * graph whose clips are not loaded yet still saves back unchanged.
 *
 * Returns: (transfer none) (nullable): the clip key
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_animation_state_get_clip_name   (LrgAnimationState  *self);

/**
 * lrg_animation_state_set_clip_name:
 * @self: A #LrgAnimationState
 * @clip_name: (nullable): the clip key
 *
 * Sets the clip key (see lrg_animation_state_get_clip_name()).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_state_set_clip_name   (LrgAnimationState  *self,
                                                         const gchar        *clip_name);

/**
 * lrg_animation_state_get_speed_parameter:
 * @self: A #LrgAnimationState
 *
 * Gets the float parameter that multiplies the state's speed.
 *
 * Returns: (transfer none) (nullable): the parameter name
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_animation_state_get_speed_parameter (LrgAnimationState *self);

/**
 * lrg_animation_state_set_speed_parameter:
 * @self: A #LrgAnimationState
 * @name: (nullable): a float parameter of the owning machine
 *
 * Makes the state's playback rate speed * the parameter's value (for
 * example a run cycle driven by the character's ground speed).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_state_set_speed_parameter (LrgAnimationState *self,
                                                             const gchar       *name);

/**
 * lrg_animation_state_get_previous_time:
 * @self: A #LrgAnimationState
 *
 * Gets the playback time before the last update; together with
 * lrg_animation_state_get_time() it is the interval the last update
 * covered (events and root motion are taken over it).
 *
 * Returns: the previous time in seconds (not wrapped)
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_animation_state_get_previous_time (LrgAnimationState *self);

/**
 * lrg_animation_state_get_duration:
 * @self: A #LrgAnimationState
 *
 * Gets the length of one cycle of the state: the clip's duration, the
 * blend tree's weighted duration or the sub-machine's current state's.
 *
 * Returns: the duration in seconds, 0 when unknown
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_animation_state_get_duration    (LrgAnimationState  *self);

/**
 * lrg_animation_state_sample_pose:
 * @self: A #LrgAnimationState
 * @skeleton: the skeleton the pose is for
 * @poses: (array length=n_poses): local poses, one per bone index
 * @n_poses: number of entries in @poses (normally the bone count)
 *
 * Samples the whole skeleton at the state's current time: entry i is the
 * local pose of the bone whose index is i. Bones the clip does not
 * animate get their bind pose. This is the fast path the state machine,
 * #LrgAnimGraph and #LrgAnimatorComponent3D use (one track lookup table
 * per clip and skeleton layout instead of a name search per bone). A
 * subclass that overrides the sample vfunc is sampled bone by bone.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_state_sample_pose     (LrgAnimationState  *self,
                                                         LrgSkeleton        *skeleton,
                                                         LrgBonePose        *poses,
                                                         guint               n_poses);

/**
 * lrg_animation_state_get_root_motion:
 * @self: A #LrgAnimationState
 * @bone_name: the root motion bone
 * @out_translation: (out caller-allocates) (array fixed-size=3): the
 *   bone's local translation change over the last update
 * @out_yaw: (out) (optional): the change of the bone's rotation about +Y,
 *   radians
 *
 * Measures how far @bone_name moved during the last update (between
 * lrg_animation_state_get_previous_time() and the current time), counting
 * whole loops of a repeating clip: a walk cycle that loops twice in one
 * update reports twice its stride. Blend trees blend their children's
 * deltas by weight.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_animation_state_get_root_motion (LrgAnimationState  *self,
                                                         const gchar        *bone_name,
                                                         gfloat             *out_translation,
                                                         gfloat             *out_yaw);

G_END_DECLS
