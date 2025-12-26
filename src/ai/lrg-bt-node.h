/* lrg-bt-node.h
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Base class for behavior tree nodes.
 */

#ifndef LRG_BT_NODE_H
#define LRG_BT_NODE_H

#include <glib-object.h>
#include "lrg-version.h"
#include "lrg-types.h"
#include "lrg-enums.h"
#include "lrg-blackboard.h"

G_BEGIN_DECLS

#define LRG_TYPE_BT_NODE (lrg_bt_node_get_type ())

G_DECLARE_DERIVABLE_TYPE (LrgBTNode, lrg_bt_node, LRG, BT_NODE, GObject)

/**
 * LrgBTNodeClass:
 * @tick: Execute one tick of the node
 * @reset: Reset node state
 * @abort: Abort running node
 *
 * The class structure for #LrgBTNode.
 */
struct _LrgBTNodeClass
{
    GObjectClass parent_class;

    /* Virtual methods */
    LrgBTStatus  (*tick)   (LrgBTNode     *self,
                            LrgBlackboard *blackboard,
                            gfloat         delta_time);

    void         (*reset)  (LrgBTNode     *self);

    void         (*abort)  (LrgBTNode     *self);

    /* Reserved for future expansion */
    gpointer _reserved[8];
};

/**
 * lrg_bt_node_get_name:
 * @self: an #LrgBTNode
 *
 * Gets the node name.
 *
 * Returns: (transfer none) (nullable): The node name
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_bt_node_get_name             (LrgBTNode *self);

/**
 * lrg_bt_node_set_name:
 * @self: an #LrgBTNode
 * @name: (nullable): The node name
 *
 * Sets the node name.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_bt_node_set_name             (LrgBTNode   *self,
                                                      const gchar *name);

/**
 * lrg_bt_node_get_status:
 * @self: an #LrgBTNode
 *
 * Gets the current status of the node.
 *
 * Returns: The node status
 */
LRG_AVAILABLE_IN_ALL
LrgBTStatus         lrg_bt_node_get_status           (LrgBTNode *self);

/**
 * lrg_bt_node_tick:
 * @self: an #LrgBTNode
 * @blackboard: The shared blackboard
 * @delta_time: Time since last tick
 *
 * Executes one tick of the node.
 *
 * Returns: The result status
 */
LRG_AVAILABLE_IN_ALL
LrgBTStatus         lrg_bt_node_tick                 (LrgBTNode     *self,
                                                      LrgBlackboard *blackboard,
                                                      gfloat         delta_time);

/**
 * lrg_bt_node_reset:
 * @self: an #LrgBTNode
 *
 * Resets the node to its initial state.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_bt_node_reset                (LrgBTNode *self);

/**
 * lrg_bt_node_abort:
 * @self: an #LrgBTNode
 *
 * Aborts a running node.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_bt_node_abort                (LrgBTNode *self);

/**
 * lrg_bt_node_is_running:
 * @self: an #LrgBTNode
 *
 * Checks if the node is currently running.
 *
 * Returns: %TRUE if running
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_bt_node_is_running           (LrgBTNode *self);

G_END_DECLS

#endif /* LRG_BT_NODE_H */
