/* lrg-bt-leaf.h
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Leaf nodes for behavior trees (action, condition).
 */

#ifndef LRG_BT_LEAF_H
#define LRG_BT_LEAF_H

#include <glib-object.h>
#include "lrg-version.h"
#include "lrg-bt-node.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Action Node - Performs an action
 * ========================================================================== */

#define LRG_TYPE_BT_ACTION (lrg_bt_action_get_type ())

G_DECLARE_FINAL_TYPE (LrgBTAction, lrg_bt_action, LRG, BT_ACTION, LrgBTNode)

/**
 * LrgBTActionFunc:
 * @blackboard: The shared blackboard
 * @delta_time: Time since last tick
 * @user_data: User data
 *
 * Callback function for action nodes.
 *
 * Returns: The action status
 */
typedef LrgBTStatus (*LrgBTActionFunc) (LrgBlackboard *blackboard,
                                        gfloat         delta_time,
                                        gpointer       user_data);

/**
 * lrg_bt_action_new:
 * @func: (scope notified): The action function
 * @user_data: (closure): User data for the function
 * @destroy: (nullable): Destroy function for user data
 *
 * Creates a new action node.
 *
 * Returns: (transfer full): A new #LrgBTAction
 */
LRG_AVAILABLE_IN_ALL
LrgBTAction *       lrg_bt_action_new                (LrgBTActionFunc  func,
                                                      gpointer         user_data,
                                                      GDestroyNotify   destroy);

/**
 * lrg_bt_action_new_simple:
 * @func: (scope notified): The action function
 *
 * Creates a new action node without user data.
 *
 * Returns: (transfer full): A new #LrgBTAction
 */
LRG_AVAILABLE_IN_ALL
LrgBTAction *       lrg_bt_action_new_simple         (LrgBTActionFunc  func);

/* ==========================================================================
 * Condition Node - Checks a condition
 * ========================================================================== */

#define LRG_TYPE_BT_CONDITION (lrg_bt_condition_get_type ())

G_DECLARE_FINAL_TYPE (LrgBTCondition, lrg_bt_condition, LRG, BT_CONDITION, LrgBTNode)

/**
 * LrgBTConditionFunc:
 * @blackboard: The shared blackboard
 * @user_data: User data
 *
 * Callback function for condition nodes.
 *
 * Returns: %TRUE if condition is met
 */
typedef gboolean (*LrgBTConditionFunc) (LrgBlackboard *blackboard,
                                        gpointer       user_data);

/**
 * lrg_bt_condition_new:
 * @func: (scope notified): The condition function
 * @user_data: (closure): User data for the function
 * @destroy: (nullable): Destroy function for user data
 *
 * Creates a new condition node.
 *
 * Returns: (transfer full): A new #LrgBTCondition
 */
LRG_AVAILABLE_IN_ALL
LrgBTCondition *    lrg_bt_condition_new             (LrgBTConditionFunc  func,
                                                      gpointer            user_data,
                                                      GDestroyNotify      destroy);

/**
 * lrg_bt_condition_new_simple:
 * @func: (scope notified): The condition function
 *
 * Creates a new condition node without user data.
 *
 * Returns: (transfer full): A new #LrgBTCondition
 */
LRG_AVAILABLE_IN_ALL
LrgBTCondition *    lrg_bt_condition_new_simple      (LrgBTConditionFunc  func);

/* ==========================================================================
 * Wait Node - Waits for a duration
 * ========================================================================== */

#define LRG_TYPE_BT_WAIT (lrg_bt_wait_get_type ())

G_DECLARE_FINAL_TYPE (LrgBTWait, lrg_bt_wait, LRG, BT_WAIT, LrgBTNode)

/**
 * lrg_bt_wait_new:
 * @duration: Duration to wait in seconds
 *
 * Creates a new wait node.
 *
 * Returns: (transfer full): A new #LrgBTWait
 */
LRG_AVAILABLE_IN_ALL
LrgBTWait *         lrg_bt_wait_new                  (gfloat duration);

/**
 * lrg_bt_wait_get_duration:
 * @self: an #LrgBTWait
 *
 * Gets the wait duration.
 *
 * Returns: Duration in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_bt_wait_get_duration         (LrgBTWait *self);

/**
 * lrg_bt_wait_set_duration:
 * @self: an #LrgBTWait
 * @duration: Duration in seconds
 *
 * Sets the wait duration.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_bt_wait_set_duration         (LrgBTWait *self,
                                                      gfloat     duration);

G_END_DECLS

#endif /* LRG_BT_LEAF_H */
