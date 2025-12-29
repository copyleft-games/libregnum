/* lrg-unlock-tree.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgUnlockTree - Tree of unlockable content for progression systems.
 *
 * Manages a directed graph of unlocks where nodes can have
 * prerequisites and resource costs.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-big-number.h"

G_BEGIN_DECLS

#define LRG_TYPE_UNLOCK_TREE (lrg_unlock_tree_get_type ())
#define LRG_TYPE_UNLOCK_NODE (lrg_unlock_node_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgUnlockTree, lrg_unlock_tree, LRG, UNLOCK_TREE, GObject)

/**
 * LrgUnlockNode:
 * @id: Unique identifier
 * @name: Display name
 * @description: Optional description
 * @icon: Optional icon name
 * @cost: Resource cost to unlock
 * @unlocked: Whether node is unlocked
 * @unlock_time: Unix timestamp when unlocked
 * @tier: Tier/level of this node
 *
 * A boxed type representing a node in the unlock tree.
 */
typedef struct _LrgUnlockNode LrgUnlockNode;

struct _LrgUnlockNode
{
    gchar        *id;
    gchar        *name;
    gchar        *description;
    gchar        *icon;
    LrgBigNumber *cost;
    gboolean      unlocked;
    gint64        unlock_time;
    gint          tier;
};

LRG_AVAILABLE_IN_ALL
GType lrg_unlock_node_get_type (void) G_GNUC_CONST;

/* Node construction */

/**
 * lrg_unlock_node_new:
 * @id: Unique identifier
 * @name: Display name
 *
 * Creates a new unlock node.
 *
 * Returns: (transfer full): A new #LrgUnlockNode
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgUnlockNode *
lrg_unlock_node_new (const gchar *id,
                     const gchar *name);

/**
 * lrg_unlock_node_copy:
 * @self: an #LrgUnlockNode
 *
 * Creates a deep copy of a node.
 *
 * Returns: (transfer full): A copy of @self
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgUnlockNode *
lrg_unlock_node_copy (const LrgUnlockNode *self);

/**
 * lrg_unlock_node_free:
 * @self: an #LrgUnlockNode
 *
 * Frees a node.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_unlock_node_free (LrgUnlockNode *self);

/* Node accessors */

/**
 * lrg_unlock_node_get_id:
 * @self: an #LrgUnlockNode
 *
 * Gets the node ID.
 *
 * Returns: (transfer none): The ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_unlock_node_get_id (const LrgUnlockNode *self);

/**
 * lrg_unlock_node_get_name:
 * @self: an #LrgUnlockNode
 *
 * Gets the display name.
 *
 * Returns: (transfer none): The name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_unlock_node_get_name (const LrgUnlockNode *self);

/**
 * lrg_unlock_node_get_description:
 * @self: an #LrgUnlockNode
 *
 * Gets the description.
 *
 * Returns: (transfer none) (nullable): The description
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_unlock_node_get_description (const LrgUnlockNode *self);

/**
 * lrg_unlock_node_set_description:
 * @self: an #LrgUnlockNode
 * @description: (nullable): New description
 *
 * Sets the description.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_unlock_node_set_description (LrgUnlockNode *self,
                                 const gchar   *description);

/**
 * lrg_unlock_node_get_icon:
 * @self: an #LrgUnlockNode
 *
 * Gets the icon path.
 *
 * Returns: (transfer none) (nullable): The icon
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_unlock_node_get_icon (const LrgUnlockNode *self);

/**
 * lrg_unlock_node_set_icon:
 * @self: an #LrgUnlockNode
 * @icon: (nullable): Icon path
 *
 * Sets the icon path.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_unlock_node_set_icon (LrgUnlockNode *self,
                          const gchar   *icon);

/**
 * lrg_unlock_node_get_cost:
 * @self: an #LrgUnlockNode
 *
 * Gets the unlock cost.
 *
 * Returns: (transfer none): The cost
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const LrgBigNumber *
lrg_unlock_node_get_cost (const LrgUnlockNode *self);

/**
 * lrg_unlock_node_set_cost:
 * @self: an #LrgUnlockNode
 * @cost: Unlock cost
 *
 * Sets the unlock cost.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_unlock_node_set_cost (LrgUnlockNode      *self,
                          const LrgBigNumber *cost);

/**
 * lrg_unlock_node_set_cost_simple:
 * @self: an #LrgUnlockNode
 * @cost: Cost as double
 *
 * Sets the cost with a simple value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_unlock_node_set_cost_simple (LrgUnlockNode *self,
                                 gdouble        cost);

/**
 * lrg_unlock_node_is_unlocked:
 * @self: an #LrgUnlockNode
 *
 * Checks if the node is unlocked.
 *
 * Returns: %TRUE if unlocked
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_unlock_node_is_unlocked (const LrgUnlockNode *self);

/**
 * lrg_unlock_node_get_unlock_time:
 * @self: an #LrgUnlockNode
 *
 * Gets when the node was unlocked.
 *
 * Returns: Unix timestamp, or 0 if not unlocked
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_unlock_node_get_unlock_time (const LrgUnlockNode *self);

/**
 * lrg_unlock_node_get_tier:
 * @self: an #LrgUnlockNode
 *
 * Gets the tier/level of this unlock.
 *
 * Returns: Tier number (0 = root)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_unlock_node_get_tier (const LrgUnlockNode *self);

/**
 * lrg_unlock_node_set_tier:
 * @self: an #LrgUnlockNode
 * @tier: Tier number
 *
 * Sets the tier.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_unlock_node_set_tier (LrgUnlockNode *self,
                          gint           tier);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgUnlockNode, lrg_unlock_node_free)

/* Tree construction */

/**
 * lrg_unlock_tree_new:
 *
 * Creates a new unlock tree.
 *
 * Returns: (transfer full): A new #LrgUnlockTree
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgUnlockTree *
lrg_unlock_tree_new (void);

/* Node management */

/**
 * lrg_unlock_tree_add_node:
 * @self: an #LrgUnlockTree
 * @node: (transfer none): Node to add
 *
 * Adds a node to the tree. The tree takes a copy.
 *
 * Returns: %TRUE if added, %FALSE if ID already exists
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_unlock_tree_add_node (LrgUnlockTree       *self,
                          const LrgUnlockNode *node);

/**
 * lrg_unlock_tree_get_node:
 * @self: an #LrgUnlockTree
 * @id: Node ID to find
 *
 * Gets a node by ID.
 *
 * Returns: (transfer none) (nullable): The node, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgUnlockNode *
lrg_unlock_tree_get_node (LrgUnlockTree *self,
                          const gchar   *id);

/**
 * lrg_unlock_tree_remove_node:
 * @self: an #LrgUnlockTree
 * @id: Node ID to remove
 *
 * Removes a node and all its requirements.
 *
 * Returns: %TRUE if removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_unlock_tree_remove_node (LrgUnlockTree *self,
                             const gchar   *id);

/**
 * lrg_unlock_tree_get_all_nodes:
 * @self: an #LrgUnlockTree
 *
 * Gets all nodes in the tree.
 *
 * Returns: (transfer container) (element-type LrgUnlockNode): Array of nodes
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_unlock_tree_get_all_nodes (LrgUnlockTree *self);

/* Requirements */

/**
 * lrg_unlock_tree_add_requirement:
 * @self: an #LrgUnlockTree
 * @node_id: Node that requires something
 * @required_id: Node that is required
 *
 * Adds a prerequisite requirement.
 *
 * Returns: %TRUE if added, %FALSE if would create cycle
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_unlock_tree_add_requirement (LrgUnlockTree *self,
                                 const gchar   *node_id,
                                 const gchar   *required_id);

/**
 * lrg_unlock_tree_remove_requirement:
 * @self: an #LrgUnlockTree
 * @node_id: Node with requirement
 * @required_id: Required node to remove
 *
 * Removes a prerequisite requirement.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_unlock_tree_remove_requirement (LrgUnlockTree *self,
                                    const gchar   *node_id,
                                    const gchar   *required_id);

/**
 * lrg_unlock_tree_get_requirements:
 * @self: an #LrgUnlockTree
 * @node_id: Node to query
 *
 * Gets all requirements for a node.
 *
 * Returns: (transfer container) (element-type utf8): Array of required IDs
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_unlock_tree_get_requirements (LrgUnlockTree *self,
                                  const gchar   *node_id);

/**
 * lrg_unlock_tree_get_dependents:
 * @self: an #LrgUnlockTree
 * @node_id: Node to query
 *
 * Gets all nodes that depend on this node.
 *
 * Returns: (transfer container) (element-type utf8): Array of dependent IDs
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_unlock_tree_get_dependents (LrgUnlockTree *self,
                                const gchar   *node_id);

/* Unlock operations */

/**
 * lrg_unlock_tree_can_unlock:
 * @self: an #LrgUnlockTree
 * @node_id: Node to check
 * @available_points: Points available for purchase
 *
 * Checks if a node can be unlocked.
 * Requirements must be met and points must be sufficient.
 *
 * Returns: %TRUE if can unlock
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_unlock_tree_can_unlock (LrgUnlockTree      *self,
                            const gchar        *node_id,
                            const LrgBigNumber *available_points);

/**
 * lrg_unlock_tree_unlock:
 * @self: an #LrgUnlockTree
 * @node_id: Node to unlock
 *
 * Unlocks a node. Does NOT deduct cost (caller must do that).
 *
 * Returns: %TRUE if newly unlocked
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_unlock_tree_unlock (LrgUnlockTree *self,
                        const gchar   *node_id);

/**
 * lrg_unlock_tree_is_unlocked:
 * @self: an #LrgUnlockTree
 * @node_id: Node to check
 *
 * Checks if a node is unlocked.
 *
 * Returns: %TRUE if unlocked
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_unlock_tree_is_unlocked (LrgUnlockTree *self,
                             const gchar   *node_id);

/**
 * lrg_unlock_tree_lock:
 * @self: an #LrgUnlockTree
 * @node_id: Node to lock
 *
 * Locks a node (revokes unlock).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_unlock_tree_lock (LrgUnlockTree *self,
                      const gchar   *node_id);

/* Queries */

/**
 * lrg_unlock_tree_get_available:
 * @self: an #LrgUnlockTree
 * @available_points: Points available
 *
 * Gets all nodes that can currently be unlocked.
 *
 * Returns: (transfer container) (element-type LrgUnlockNode): Available nodes
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_unlock_tree_get_available (LrgUnlockTree      *self,
                               const LrgBigNumber *available_points);

/**
 * lrg_unlock_tree_get_unlocked:
 * @self: an #LrgUnlockTree
 *
 * Gets all unlocked nodes.
 *
 * Returns: (transfer container) (element-type LrgUnlockNode): Unlocked nodes
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_unlock_tree_get_unlocked (LrgUnlockTree *self);

/**
 * lrg_unlock_tree_get_locked:
 * @self: an #LrgUnlockTree
 *
 * Gets all locked nodes.
 *
 * Returns: (transfer container) (element-type LrgUnlockNode): Locked nodes
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_unlock_tree_get_locked (LrgUnlockTree *self);

/**
 * lrg_unlock_tree_get_progress:
 * @self: an #LrgUnlockTree
 *
 * Gets unlock progress (unlocked / total).
 *
 * Returns: Progress (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_unlock_tree_get_progress (LrgUnlockTree *self);

/**
 * lrg_unlock_tree_reset:
 * @self: an #LrgUnlockTree
 *
 * Resets all unlocks (locks everything).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_unlock_tree_reset (LrgUnlockTree *self);

G_END_DECLS
