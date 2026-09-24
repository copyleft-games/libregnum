/* lrg-talent-tree.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTalentNode / LrgTalentTree - classic tiered talent trees.
 *
 * A talent tree is a grid of ranked nodes. Rows are tiers; spending in
 * tier t requires t * tier-gate points already spent in the same tree.
 * A node may name a prerequisite node in a lower tier which must be at
 * its maximum rank, may grant an ability once ranked, and may carry a
 * per-rank value for a game-interpreted effect key.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * LRG_TALENT_TREE_DEFAULT_TIER_GATE:
 *
 * Default number of points per tier required to unlock the next tier.
 */
#define LRG_TALENT_TREE_DEFAULT_TIER_GATE (5)

/**
 * LRG_TALENT_TREE_MAX_TIER_GATE:
 *
 * Largest permitted tier gate.
 */
#define LRG_TALENT_TREE_MAX_TIER_GATE (1000)

/**
 * LRG_TALENT_ID_MAX_LENGTH:
 *
 * Maximum length in bytes of any talent, tree, class or ability id.
 */
#define LRG_TALENT_ID_MAX_LENGTH (128)

/**
 * LRG_TALENT_MAX_RANK:
 *
 * Largest permitted maximum rank of a talent node.
 */
#define LRG_TALENT_MAX_RANK (100)

#define LRG_TYPE_TALENT_NODE (lrg_talent_node_get_type ())
#define LRG_TYPE_TALENT_TREE (lrg_talent_tree_get_type ())

/**
 * LrgTalentNode:
 * @id: unique node id within its tree
 * @name: (nullable): display name
 * @description: (nullable): display description
 * @tier: 0-based row; spending requires tier * tier-gate points in the tree
 * @column: column within the tier; (tier, column) is unique in a valid tree
 * @max_rank: maximum rank, 1..%LRG_TALENT_MAX_RANK
 * @prerequisite: (nullable): node id in the same tree, in a lower tier, that
 *   must be at its maximum rank before this node can be ranked
 * @grants_ability: (nullable): ability id granted while this node has rank >= 1
 * @effect: (nullable): game-interpreted effect or stat key
 * @rank_values: (element-type gdouble): value per rank; length is 0 or @max_rank
 *
 * A single talent. Nodes are plain boxed data and are owned by an
 * #LrgTalentTree once added.
 */
struct _LrgTalentNode
{
    gchar   *id;
    gchar   *name;
    gchar   *description;
    guint    tier;
    guint    column;
    guint    max_rank;
    gchar   *prerequisite;
    gchar   *grants_ability;
    gchar   *effect;
    GArray  *rank_values;
};

LRG_AVAILABLE_IN_ALL
GType lrg_talent_node_get_type (void) G_GNUC_CONST;

/**
 * lrg_talent_node_new:
 * @id: node id (non-empty UTF-8, at most %LRG_TALENT_ID_MAX_LENGTH bytes)
 * @tier: 0-based tier (row)
 * @column: column within the tier
 * @max_rank: maximum rank, at least 1
 *
 * Creates a talent node with no prerequisite, ability, effect or rank values.
 *
 * Returns: (transfer full): a new #LrgTalentNode
 */
LRG_AVAILABLE_IN_ALL
LrgTalentNode *lrg_talent_node_new (const gchar *id,
                                    guint        tier,
                                    guint        column,
                                    guint        max_rank);

/**
 * lrg_talent_node_copy:
 * @self: a #LrgTalentNode
 *
 * Deep-copies a node including its rank values.
 *
 * Returns: (transfer full): the copy
 */
LRG_AVAILABLE_IN_ALL
LrgTalentNode *lrg_talent_node_copy (const LrgTalentNode *self);

/**
 * lrg_talent_node_free:
 * @self: (nullable): a #LrgTalentNode
 *
 * Frees a node and all of its strings.
 */
LRG_AVAILABLE_IN_ALL
void lrg_talent_node_free (LrgTalentNode *self);

/**
 * lrg_talent_node_add_rank_value:
 * @self: a #LrgTalentNode
 * @value: value at the next rank (first call is rank 1)
 *
 * Appends a per-rank value. A valid tree requires either no values or
 * exactly @max_rank values.
 */
LRG_AVAILABLE_IN_ALL
void lrg_talent_node_add_rank_value (LrgTalentNode *self,
                                     gdouble        value);

/**
 * lrg_talent_node_get_value:
 * @self: a #LrgTalentNode
 * @rank: rank to query
 *
 * Gets the value at @rank. Rank 0 and nodes without values yield 0.
 * Ranks beyond the last provided value clamp to the last value.
 *
 * Returns: the value for @rank
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_talent_node_get_value (const LrgTalentNode *self,
                                   guint                rank);

/**
 * lrg_talent_node_set_name:
 * @self: a #LrgTalentNode
 * @name: (nullable): display name
 *
 * Replaces the display name (binding-friendly field setter).
 */
LRG_AVAILABLE_IN_ALL
void lrg_talent_node_set_name (LrgTalentNode *self,
                               const gchar   *name);

/**
 * lrg_talent_node_set_description:
 * @self: a #LrgTalentNode
 * @description: (nullable): display description
 *
 * Replaces the description (binding-friendly field setter).
 */
LRG_AVAILABLE_IN_ALL
void lrg_talent_node_set_description (LrgTalentNode *self,
                                      const gchar   *description);

/**
 * lrg_talent_node_set_prerequisite:
 * @self: a #LrgTalentNode
 * @prerequisite: (nullable): prerequisite node id
 *
 * Replaces the prerequisite node id (binding-friendly field setter).
 */
LRG_AVAILABLE_IN_ALL
void lrg_talent_node_set_prerequisite (LrgTalentNode *self,
                                       const gchar   *prerequisite);

/**
 * lrg_talent_node_set_grants_ability:
 * @self: a #LrgTalentNode
 * @ability_id: (nullable): granted ability id
 *
 * Replaces the granted ability id (binding-friendly field setter).
 */
LRG_AVAILABLE_IN_ALL
void lrg_talent_node_set_grants_ability (LrgTalentNode *self,
                                         const gchar   *ability_id);

/**
 * lrg_talent_node_set_effect:
 * @self: a #LrgTalentNode
 * @effect: (nullable): effect key
 *
 * Replaces the effect key (binding-friendly field setter).
 */
LRG_AVAILABLE_IN_ALL
void lrg_talent_node_set_effect (LrgTalentNode *self,
                                 const gchar   *effect);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgTalentNode, lrg_talent_node_free)

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTalentTree, lrg_talent_tree, LRG, TALENT_TREE, GObject)

/**
 * lrg_talent_tree_new:
 * @id: tree id (construct-only)
 * @class_id: (nullable): owning class id
 *
 * Creates an empty talent tree with the default tier gate of
 * %LRG_TALENT_TREE_DEFAULT_TIER_GATE.
 *
 * Returns: (transfer full): a new #LrgTalentTree
 */
LRG_AVAILABLE_IN_ALL
LrgTalentTree *lrg_talent_tree_new (const gchar *id,
                                    const gchar *class_id);

/**
 * lrg_talent_tree_get_id:
 * @self: a #LrgTalentTree
 *
 * Returns: (transfer none): the tree id
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_talent_tree_get_id (LrgTalentTree *self);

/**
 * lrg_talent_tree_get_name:
 * @self: a #LrgTalentTree
 *
 * Returns: (transfer none) (nullable): the display name
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_talent_tree_get_name (LrgTalentTree *self);

/**
 * lrg_talent_tree_set_name:
 * @self: a #LrgTalentTree
 * @name: (nullable): display name
 *
 * Sets the display name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_talent_tree_set_name (LrgTalentTree *self,
                               const gchar   *name);

/**
 * lrg_talent_tree_get_description:
 * @self: a #LrgTalentTree
 *
 * Returns: (transfer none) (nullable): the description
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_talent_tree_get_description (LrgTalentTree *self);

/**
 * lrg_talent_tree_set_description:
 * @self: a #LrgTalentTree
 * @description: (nullable): description
 *
 * Sets the description.
 */
LRG_AVAILABLE_IN_ALL
void lrg_talent_tree_set_description (LrgTalentTree *self,
                                      const gchar   *description);

/**
 * lrg_talent_tree_get_icon:
 * @self: a #LrgTalentTree
 *
 * Returns: (transfer none) (nullable): the icon key
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_talent_tree_get_icon (LrgTalentTree *self);

/**
 * lrg_talent_tree_set_icon:
 * @self: a #LrgTalentTree
 * @icon: (nullable): icon key
 *
 * Sets the icon key.
 */
LRG_AVAILABLE_IN_ALL
void lrg_talent_tree_set_icon (LrgTalentTree *self,
                               const gchar   *icon);

/**
 * lrg_talent_tree_get_class_id:
 * @self: a #LrgTalentTree
 *
 * Returns: (transfer none) (nullable): the owning class id
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_talent_tree_get_class_id (LrgTalentTree *self);

/**
 * lrg_talent_tree_set_class_id:
 * @self: a #LrgTalentTree
 * @class_id: (nullable): owning class id
 *
 * Sets the owning class id.
 */
LRG_AVAILABLE_IN_ALL
void lrg_talent_tree_set_class_id (LrgTalentTree *self,
                                   const gchar   *class_id);

/**
 * lrg_talent_tree_get_role:
 * @self: a #LrgTalentTree
 *
 * Returns: (transfer none) (nullable): the role hint ("tank", "healer", "damage")
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_talent_tree_get_role (LrgTalentTree *self);

/**
 * lrg_talent_tree_set_role:
 * @self: a #LrgTalentTree
 * @role: (nullable): role hint
 *
 * Sets the role hint.
 */
LRG_AVAILABLE_IN_ALL
void lrg_talent_tree_set_role (LrgTalentTree *self,
                               const gchar   *role);

/**
 * lrg_talent_tree_get_tier_gate:
 * @self: a #LrgTalentTree
 *
 * Returns: points per tier required to unlock the next tier
 */
LRG_AVAILABLE_IN_ALL
guint lrg_talent_tree_get_tier_gate (LrgTalentTree *self);

/**
 * lrg_talent_tree_set_tier_gate:
 * @self: a #LrgTalentTree
 * @tier_gate: points per tier (0..%LRG_TALENT_TREE_MAX_TIER_GATE); 0 disables gating
 *
 * Sets the tier gate. Spending in tier t requires t * @tier_gate points
 * already spent in this tree.
 */
LRG_AVAILABLE_IN_ALL
void lrg_talent_tree_set_tier_gate (LrgTalentTree *self,
                                    guint          tier_gate);

/**
 * lrg_talent_tree_add_node:
 * @self: a #LrgTalentTree
 * @node: (transfer full): node to add
 * @error: (nullable): return location for an error
 *
 * Adds a node. Ownership of @node is always taken, even on failure.
 * Fails with %LRG_PROGRESSION_ERROR_DUPLICATE if the id is already
 * present and %LRG_PROGRESSION_ERROR_INVALID if the id is empty, longer
 * than %LRG_TALENT_ID_MAX_LENGTH bytes or not UTF-8, or if max_rank is
 * outside 1..%LRG_TALENT_MAX_RANK. Structural checks spanning several
 * nodes are done by lrg_talent_tree_validate().
 *
 * Returns: %TRUE if the node was added
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_talent_tree_add_node (LrgTalentTree  *self,
                                   LrgTalentNode  *node,
                                   GError        **error);

/**
 * lrg_talent_tree_get_node:
 * @self: a #LrgTalentTree
 * @node_id: node id
 *
 * Returns: (transfer none) (nullable): the node or %NULL if unknown
 */
LRG_AVAILABLE_IN_ALL
const LrgTalentNode *lrg_talent_tree_get_node (LrgTalentTree *self,
                                               const gchar   *node_id);

/**
 * lrg_talent_tree_get_nodes:
 * @self: a #LrgTalentTree
 *
 * Gets every node sorted by (tier, column, id).
 *
 * Returns: (transfer none) (element-type LrgTalentNode): the nodes
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_talent_tree_get_nodes (LrgTalentTree *self);

/**
 * lrg_talent_tree_get_node_count:
 * @self: a #LrgTalentTree
 *
 * Returns: number of nodes
 */
LRG_AVAILABLE_IN_ALL
guint lrg_talent_tree_get_node_count (LrgTalentTree *self);

/**
 * lrg_talent_tree_get_tier_count:
 * @self: a #LrgTalentTree
 *
 * Returns: highest node tier + 1, or 0 for an empty tree
 */
LRG_AVAILABLE_IN_ALL
guint lrg_talent_tree_get_tier_count (LrgTalentTree *self);

/**
 * lrg_talent_tree_get_max_points:
 * @self: a #LrgTalentTree
 *
 * Returns: sum of every node's max_rank
 */
LRG_AVAILABLE_IN_ALL
guint lrg_talent_tree_get_max_points (LrgTalentTree *self);

/**
 * lrg_talent_tree_validate:
 * @self: a #LrgTalentTree
 * @error: (nullable): return location for an error
 *
 * Validates the whole tree structure:
 *
 * - every rank_values array has length 0 or max_rank
 *   (%LRG_PROGRESSION_ERROR_INVALID);
 * - every prerequisite names a node of this tree
 *   (%LRG_PROGRESSION_ERROR_NOT_FOUND) in a strictly lower tier
 *   (%LRG_PROGRESSION_ERROR_INVALID);
 * - (tier, column) positions are unique (%LRG_PROGRESSION_ERROR_DUPLICATE);
 * - every tier that holds nodes is reachable: the max points of all lower
 *   tiers are at least tier * tier-gate (%LRG_PROGRESSION_ERROR_REQUIREMENT).
 *
 * An empty tree is valid.
 *
 * Returns: %TRUE if the tree is valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_talent_tree_validate (LrgTalentTree  *self,
                                   GError        **error);

G_END_DECLS
