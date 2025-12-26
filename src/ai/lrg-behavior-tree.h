/* lrg-behavior-tree.h
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Behavior tree for AI decision making.
 */

#ifndef LRG_BEHAVIOR_TREE_H
#define LRG_BEHAVIOR_TREE_H

#include <glib-object.h>
#include "lrg-version.h"
#include "lrg-types.h"
#include "lrg-enums.h"
#include "lrg-blackboard.h"
#include "lrg-bt-node.h"

G_BEGIN_DECLS

#define LRG_TYPE_BEHAVIOR_TREE (lrg_behavior_tree_get_type ())

G_DECLARE_FINAL_TYPE (LrgBehaviorTree, lrg_behavior_tree, LRG, BEHAVIOR_TREE, GObject)

/**
 * lrg_behavior_tree_new:
 *
 * Creates a new empty behavior tree.
 *
 * Returns: (transfer full): A new #LrgBehaviorTree
 */
LRG_AVAILABLE_IN_ALL
LrgBehaviorTree *   lrg_behavior_tree_new            (void);

/**
 * lrg_behavior_tree_new_with_root:
 * @root: (transfer none): The root node
 *
 * Creates a new behavior tree with a root node.
 *
 * Returns: (transfer full): A new #LrgBehaviorTree
 */
LRG_AVAILABLE_IN_ALL
LrgBehaviorTree *   lrg_behavior_tree_new_with_root  (LrgBTNode *root);

/**
 * lrg_behavior_tree_get_root:
 * @self: an #LrgBehaviorTree
 *
 * Gets the root node of the tree.
 *
 * Returns: (transfer none) (nullable): The root node
 */
LRG_AVAILABLE_IN_ALL
LrgBTNode *         lrg_behavior_tree_get_root       (LrgBehaviorTree *self);

/**
 * lrg_behavior_tree_set_root:
 * @self: an #LrgBehaviorTree
 * @root: (nullable) (transfer none): The root node
 *
 * Sets the root node of the tree.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_behavior_tree_set_root       (LrgBehaviorTree *self,
                                                      LrgBTNode       *root);

/**
 * lrg_behavior_tree_get_blackboard:
 * @self: an #LrgBehaviorTree
 *
 * Gets the blackboard for this tree.
 *
 * Returns: (transfer none): The blackboard
 */
LRG_AVAILABLE_IN_ALL
LrgBlackboard *     lrg_behavior_tree_get_blackboard (LrgBehaviorTree *self);

/**
 * lrg_behavior_tree_tick:
 * @self: an #LrgBehaviorTree
 * @delta_time: Time since last tick
 *
 * Executes one tick of the behavior tree.
 *
 * Returns: The result status
 */
LRG_AVAILABLE_IN_ALL
LrgBTStatus         lrg_behavior_tree_tick           (LrgBehaviorTree *self,
                                                      gfloat           delta_time);

/**
 * lrg_behavior_tree_reset:
 * @self: an #LrgBehaviorTree
 *
 * Resets the tree to its initial state.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_behavior_tree_reset          (LrgBehaviorTree *self);

/**
 * lrg_behavior_tree_abort:
 * @self: an #LrgBehaviorTree
 *
 * Aborts any running nodes in the tree.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_behavior_tree_abort          (LrgBehaviorTree *self);

/**
 * lrg_behavior_tree_get_status:
 * @self: an #LrgBehaviorTree
 *
 * Gets the current status of the tree.
 *
 * Returns: The tree status
 */
LRG_AVAILABLE_IN_ALL
LrgBTStatus         lrg_behavior_tree_get_status     (LrgBehaviorTree *self);

/**
 * lrg_behavior_tree_is_running:
 * @self: an #LrgBehaviorTree
 *
 * Checks if the tree is currently running.
 *
 * Returns: %TRUE if running
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_behavior_tree_is_running     (LrgBehaviorTree *self);

G_END_DECLS

#endif /* LRG_BEHAVIOR_TREE_H */
