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

G_BEGIN_DECLS

/**
 * LrgBlendTreeChild:
 * @clip: Animation clip
 * @threshold: 1D threshold value
 * @position_x: 2D X position
 * @position_y: 2D Y position
 * @weight: Direct weight (for DIRECT blend type)
 *
 * A child entry in a blend tree.
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

G_END_DECLS
