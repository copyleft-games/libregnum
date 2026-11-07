/* lrg-blend-tree.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Blend tree for parameter-driven animation blending.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-animation-clip.h"
#include "lrg-bone-pose.h"
#include "lrg-skeleton.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * LrgBlendTreeChild:
 * @clip: Animation clip
 * @threshold: 1D threshold value
 * @position_x: 2D X position
 * @position_y: 2D Y position
 * @weight: Direct weight (for DIRECT blend type)
 * @speed: Playback speed multiplier of the child
 *
 * A child entry in a blend tree: a clip, or (0.9.0) a nested blend tree
 * (lrg_blend_tree_child_set_subtree()). The private fields hold the
 * computed weight, the child's playback times, the clip key and direct
 * weight parameter used by .ranim files and the format's unknown keys.
 */
typedef struct _LrgBlendTreeChild LrgBlendTreeChild;

struct _LrgBlendTreeChild
{
    LrgAnimationClip *clip;
    gfloat            threshold;
    gfloat            position_x;
    gfloat            position_y;
    gfloat            weight;
    gfloat            speed;

    /*< private >*/
    gfloat            computed_weight;
    gfloat            time;
    gfloat            previous_time;
    gchar            *clip_name;
    gchar            *weight_parameter;
    LrgBlendTree     *subtree;
    gpointer          extra;
};

#define LRG_TYPE_BLEND_TREE_CHILD (lrg_blend_tree_child_get_type ())

LRG_AVAILABLE_IN_ALL
GType               lrg_blend_tree_child_get_type   (void) G_GNUC_CONST;

LRG_AVAILABLE_IN_ALL
LrgBlendTreeChild * lrg_blend_tree_child_new        (LrgAnimationClip *clip);

LRG_AVAILABLE_IN_ALL
LrgBlendTreeChild * lrg_blend_tree_child_copy       (const LrgBlendTreeChild *child);

LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_child_free       (LrgBlendTreeChild *child);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgBlendTreeChild, lrg_blend_tree_child_free)


#define LRG_TYPE_BLEND_TREE (lrg_blend_tree_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgBlendTree, lrg_blend_tree, LRG, BLEND_TREE, GObject)

/**
 * LrgBlendTreeClass:
 * @parent_class: Parent class
 * @update: Update blend weights
 * @sample: Sample blended pose
 *
 * Class structure for #LrgBlendTree.
 */
struct _LrgBlendTreeClass
{
    GObjectClass parent_class;

    /* Virtual methods */
    void (*update)  (LrgBlendTree *self,
                     gfloat        delta_time);
    void (*sample)  (LrgBlendTree  *self,
                     LrgBonePose   *out_pose,
                     const gchar   *bone_name);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_blend_tree_new:
 * @blend_type: Type of blending
 *
 * Creates a new blend tree.
 *
 * Returns: (transfer full): A new #LrgBlendTree
 */
LRG_AVAILABLE_IN_ALL
LrgBlendTree *      lrg_blend_tree_new              (LrgBlendType    blend_type);

/**
 * lrg_blend_tree_get_blend_type:
 * @self: A #LrgBlendTree
 *
 * Gets the blend type.
 *
 * Returns: The blend type
 */
LRG_AVAILABLE_IN_ALL
LrgBlendType        lrg_blend_tree_get_blend_type   (LrgBlendTree   *self);

/**
 * lrg_blend_tree_add_child:
 * @self: A #LrgBlendTree
 * @clip: Animation clip
 * @threshold: Threshold for 1D blending
 *
 * Adds a child for 1D blending.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_add_child        (LrgBlendTree     *self,
                                                      LrgAnimationClip *clip,
                                                      gfloat            threshold);

/**
 * lrg_blend_tree_add_child_2d:
 * @self: A #LrgBlendTree
 * @clip: Animation clip
 * @x: X position
 * @y: Y position
 *
 * Adds a child for 2D blending.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_add_child_2d     (LrgBlendTree     *self,
                                                      LrgAnimationClip *clip,
                                                      gfloat            x,
                                                      gfloat            y);

/**
 * lrg_blend_tree_clear_children:
 * @self: A #LrgBlendTree
 *
 * Removes all children.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_clear_children   (LrgBlendTree   *self);

/**
 * lrg_blend_tree_get_children:
 * @self: A #LrgBlendTree
 *
 * Gets all children.
 *
 * Returns: (transfer none) (element-type LrgBlendTreeChild): The children
 */
LRG_AVAILABLE_IN_ALL
GList *             lrg_blend_tree_get_children     (LrgBlendTree   *self);

/**
 * lrg_blend_tree_set_parameter:
 * @self: A #LrgBlendTree
 * @value: The blend parameter
 *
 * Sets the 1D blend parameter.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_set_parameter    (LrgBlendTree   *self,
                                                      gfloat          value);

/**
 * lrg_blend_tree_get_parameter:
 * @self: A #LrgBlendTree
 *
 * Gets the 1D blend parameter.
 *
 * Returns: The parameter value
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_blend_tree_get_parameter    (LrgBlendTree   *self);

/**
 * lrg_blend_tree_set_parameter_2d:
 * @self: A #LrgBlendTree
 * @x: X parameter
 * @y: Y parameter
 *
 * Sets the 2D blend parameters.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_set_parameter_2d (LrgBlendTree   *self,
                                                      gfloat          x,
                                                      gfloat          y);

/**
 * lrg_blend_tree_get_parameter_x:
 * @self: A #LrgBlendTree
 *
 * Gets the X blend parameter.
 *
 * Returns: The X parameter value
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_blend_tree_get_parameter_x  (LrgBlendTree   *self);

/**
 * lrg_blend_tree_get_parameter_y:
 * @self: A #LrgBlendTree
 *
 * Gets the Y blend parameter.
 *
 * Returns: The Y parameter value
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_blend_tree_get_parameter_y  (LrgBlendTree   *self);

/**
 * lrg_blend_tree_update:
 * @self: A #LrgBlendTree
 * @delta_time: Time since last update
 *
 * Updates the blend tree.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_update           (LrgBlendTree   *self,
                                                      gfloat          delta_time);

/**
 * lrg_blend_tree_sample:
 * @self: A #LrgBlendTree
 * @out_pose: (out): Output pose
 * @bone_name: Bone to sample
 *
 * Samples the blended pose for a bone.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_sample           (LrgBlendTree   *self,
                                                      LrgBonePose    *out_pose,
                                                      const gchar    *bone_name);

/**
 * lrg_blend_tree_get_time:
 * @self: A #LrgBlendTree
 *
 * Gets the current blend tree time.
 *
 * Returns: Time in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_blend_tree_get_time         (LrgBlendTree   *self);

/**
 * lrg_blend_tree_set_time:
 * @self: A #LrgBlendTree
 * @time: Time in seconds
 *
 * Sets the blend tree time.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_set_time         (LrgBlendTree   *self,
                                                      gfloat          time);

/* ==========================================================================
 * 0.9.0: nested trees, named parameters, synchronised playback and whole
 * skeleton sampling
 * ========================================================================== */

/**
 * lrg_blend_tree_child_get_subtree:
 * @child: a blend tree child
 *
 * Returns: (transfer none) (nullable): the nested tree the child plays
 *   instead of a clip
 */
LRG_AVAILABLE_IN_ALL
LrgBlendTree *      lrg_blend_tree_child_get_subtree (const LrgBlendTreeChild *child);

/**
 * lrg_blend_tree_child_set_subtree:
 * @child: a blend tree child
 * @subtree: (nullable): a nested tree
 *
 * Makes the child play @subtree (its weight then scales the subtree's
 * blended pose).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_child_set_subtree (LrgBlendTreeChild *child,
                                                      LrgBlendTree      *subtree);

/**
 * lrg_blend_tree_child_get_clip_name:
 * @child: a blend tree child
 *
 * Returns: (transfer none) (nullable): the clip key the child refers to
 *   in an #LrgAnimGraph
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_blend_tree_child_get_clip_name (const LrgBlendTreeChild *child);

/**
 * lrg_blend_tree_child_set_clip_name:
 * @child: a blend tree child
 * @clip_name: (nullable): the clip key
 *
 * Sets the clip key (resolved by lrg_anim_graph_set_clip()).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_child_set_clip_name (LrgBlendTreeChild *child,
                                                        const gchar       *clip_name);

/**
 * lrg_blend_tree_child_get_weight_parameter:
 * @child: a blend tree child
 *
 * Returns: (transfer none) (nullable): the float parameter a direct tree
 *   reads this child's weight from
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_blend_tree_child_get_weight_parameter (const LrgBlendTreeChild *child);

/**
 * lrg_blend_tree_child_set_weight_parameter:
 * @child: a blend tree child
 * @name: (nullable): a float parameter
 *
 * For %LRG_BLEND_TYPE_DIRECT trees: the child's weight follows the
 * parameter (see lrg_blend_tree_pull_parameters()).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_child_set_weight_parameter (LrgBlendTreeChild *child,
                                                               const gchar       *name);

/**
 * lrg_blend_tree_add_child_full:
 * @self: A #LrgBlendTree
 * @child: a child description (copied)
 *
 * Appends a copy of @child (clip or subtree, threshold, position, speed,
 * clip key, weight parameter).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_add_child_full   (LrgBlendTree            *self,
                                                      const LrgBlendTreeChild *child);

/**
 * lrg_blend_tree_get_child_count:
 * @self: A #LrgBlendTree
 *
 * Returns: the number of children
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_blend_tree_get_child_count  (LrgBlendTree   *self);

/**
 * lrg_blend_tree_get_child:
 * @self: A #LrgBlendTree
 * @index: a child index
 *
 * Returns: (transfer none) (nullable): the child (owned by the tree; it
 *   may be edited in place), %NULL out of range
 */
LRG_AVAILABLE_IN_ALL
LrgBlendTreeChild * lrg_blend_tree_get_child        (LrgBlendTree   *self,
                                                      guint           index);

/**
 * lrg_blend_tree_remove_child:
 * @self: A #LrgBlendTree
 * @index: a child index
 *
 * Returns: %TRUE if a child was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_blend_tree_remove_child     (LrgBlendTree   *self,
                                                      guint           index);

/**
 * lrg_blend_tree_get_child_weight:
 * @self: A #LrgBlendTree
 * @index: a child index
 *
 * Gets a child's weight as last computed (by an update or
 * lrg_blend_tree_compute_weights()); the weights sum to 1.
 *
 * Returns: the weight, 0 out of range
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_blend_tree_get_child_weight (LrgBlendTree   *self,
                                                      guint           index);

/**
 * lrg_blend_tree_compute_weights:
 * @self: A #LrgBlendTree
 *
 * Computes the children's weights from the current parameters without
 * advancing time (the blend space editor's preview uses it): 1D lerps
 * between the two neighbouring thresholds; 2D directional and 2D
 * cartesian use gradient band interpolation (in polar and cartesian
 * space respectively), so the weights are exact at every child's
 * position and never reach children on the far side; direct normalizes
 * the children's weights.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_compute_weights  (LrgBlendTree   *self);

/**
 * lrg_blend_tree_get_parameter_name:
 * @self: A #LrgBlendTree
 *
 * Returns: (transfer none) (nullable): the float parameter driving the
 *   blend (X for 2D trees)
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_blend_tree_get_parameter_name (LrgBlendTree *self);

/**
 * lrg_blend_tree_set_parameter_name:
 * @self: A #LrgBlendTree
 * @name: (nullable): a float parameter of the owning state machine
 *
 * Names the parameter lrg_blend_tree_pull_parameters() reads into the
 * blend position (X for 2D).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_set_parameter_name (LrgBlendTree *self,
                                                       const gchar  *name);

/**
 * lrg_blend_tree_get_parameter_y_name:
 * @self: A #LrgBlendTree
 *
 * Returns: (transfer none) (nullable): the Y parameter of a 2D tree
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_blend_tree_get_parameter_y_name (LrgBlendTree *self);

/**
 * lrg_blend_tree_set_parameter_y_name:
 * @self: A #LrgBlendTree
 * @name: (nullable): a float parameter
 *
 * Names the Y parameter of a 2D tree.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_set_parameter_y_name (LrgBlendTree *self,
                                                         const gchar  *name);

/**
 * lrg_blend_tree_pull_parameters:
 * @self: A #LrgBlendTree
 * @machine: the machine whose parameters to read
 *
 * Copies the named parameters into the blend position, the direct
 * children's weights and (recursively) the subtrees' positions. The
 * state machine calls it before each update of a blend tree state.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_pull_parameters  (LrgBlendTree             *self,
                                                      LrgAnimationStateMachine *machine);

/**
 * lrg_blend_tree_get_sync:
 * @self: A #LrgBlendTree
 *
 * Returns: whether the children play in step (the default)
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_blend_tree_get_sync         (LrgBlendTree   *self);

/**
 * lrg_blend_tree_set_sync:
 * @self: A #LrgBlendTree
 * @sync: whether to synchronise the children
 *
 * With sync (the default) all children share one normalized phase and
 * the tree's cycle length is the weight-blended length of its children,
 * so a walk and a run with different lengths stay on the same foot.
 * Without it every child keeps its own clock.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_set_sync         (LrgBlendTree   *self,
                                                      gboolean        sync);

/**
 * lrg_blend_tree_get_duration:
 * @self: A #LrgBlendTree
 *
 * Returns: the weighted cycle length in seconds (each child's length
 *   divided by its speed, blended by weight)
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_blend_tree_get_duration     (LrgBlendTree   *self);

/**
 * lrg_blend_tree_get_normalized_time:
 * @self: A #LrgBlendTree
 *
 * Returns: the tree's progress in cycles (not wrapped: 1.5 is half way
 *   through the second cycle)
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_blend_tree_get_normalized_time (LrgBlendTree *self);

/**
 * lrg_blend_tree_set_normalized_time:
 * @self: A #LrgBlendTree
 * @normalized_time: progress in cycles
 *
 * Scrubs a synchronised tree (every child jumps to the same phase).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_set_normalized_time (LrgBlendTree *self,
                                                        gfloat        normalized_time);

/**
 * lrg_blend_tree_reset:
 * @self: A #LrgBlendTree
 *
 * Rewinds the tree and every child to time 0.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_reset            (LrgBlendTree   *self);

/**
 * lrg_blend_tree_get_dominant_clip:
 * @self: A #LrgBlendTree
 * @out_previous_time: (out) (optional): the clip's time before the last
 *   update
 * @out_time: (out) (optional): the clip's time now
 *
 * Finds the clip with the largest effective weight (descending into
 * subtrees). Animation events of a blend tree come from this clip only,
 * so footsteps do not fire twice while walk and run blend.
 *
 * Returns: (transfer none) (nullable): the clip
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationClip *  lrg_blend_tree_get_dominant_clip (LrgBlendTree  *self,
                                                       gfloat        *out_previous_time,
                                                       gfloat        *out_time);

/**
 * lrg_blend_tree_sample_pose:
 * @self: A #LrgBlendTree
 * @skeleton: the skeleton the pose is for
 * @poses: (array length=n_poses) (out caller-allocates): local poses by
 *   bone index
 * @n_poses: number of entries
 *
 * Samples every weighted child and blends them: translation and scale
 * by weighted sum, rotation by a hemisphere-aligned weighted sum
 * renormalized (nlerp).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_sample_pose      (LrgBlendTree   *self,
                                                      LrgSkeleton    *skeleton,
                                                      LrgBonePose    *poses,
                                                      guint           n_poses);

/**
 * lrg_blend_tree_get_root_motion:
 * @self: A #LrgBlendTree
 * @bone_name: the root motion bone
 * @out_translation: (out caller-allocates) (array fixed-size=3): the
 *   weighted translation change over the last update
 * @out_yaw: (out) (optional): the weighted turn about +Y
 *
 * Blends the children's root motion over the last update by weight.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blend_tree_get_root_motion  (LrgBlendTree   *self,
                                                      const gchar    *bone_name,
                                                      gfloat         *out_translation,
                                                      gfloat         *out_yaw);

G_END_DECLS
