/* lrg-talent-loadout.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTalentLoadout - the points one character has spent across the
 * talent trees of its class (one "spec" of a talent book).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-talent-tree.h"

G_BEGIN_DECLS

/**
 * LRG_TALENT_LOADOUT_DEFAULT_FIRST_LEVEL:
 *
 * Default level at which the first talent point is granted.
 */
#define LRG_TALENT_LOADOUT_DEFAULT_FIRST_LEVEL (10)

/**
 * LRG_TALENT_LOADOUT_MAX_ROWS:
 *
 * Maximum number of (tree, node) rows accepted by
 * lrg_talent_loadout_new_from_variant().
 */
#define LRG_TALENT_LOADOUT_MAX_ROWS (512)

/**
 * LRG_TALENT_LOADOUT_VARIANT_TYPE:
 *
 * GVariant type string of a persisted loadout:
 * (class_id, maybe spec tree id, array of (tree id, node id, rank)).
 */
#define LRG_TALENT_LOADOUT_VARIANT_TYPE "(smsa(ssu))"

#define LRG_TYPE_TALENT_LOADOUT (lrg_talent_loadout_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTalentLoadout, lrg_talent_loadout, LRG, TALENT_LOADOUT, GObject)

/**
 * lrg_talent_loadout_new:
 * @class_id: class id (construct-only); only trees of this class accept points
 *
 * Creates an empty loadout with first-level 10 and one point per level.
 *
 * Returns: (transfer full): a new #LrgTalentLoadout
 */
LRG_AVAILABLE_IN_ALL
LrgTalentLoadout *lrg_talent_loadout_new (const gchar *class_id);

/**
 * lrg_talent_loadout_get_class_id:
 * @self: a #LrgTalentLoadout
 *
 * Returns: (transfer none): the class id
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_talent_loadout_get_class_id (LrgTalentLoadout *self);

/**
 * lrg_talent_loadout_get_first_level:
 * @self: a #LrgTalentLoadout
 *
 * Returns: the level granting the first point
 */
LRG_AVAILABLE_IN_ALL
guint lrg_talent_loadout_get_first_level (LrgTalentLoadout *self);

/**
 * lrg_talent_loadout_set_first_level:
 * @self: a #LrgTalentLoadout
 * @first_level: level granting the first point, at least 1
 *
 * Sets the first talent level. Not persisted by
 * lrg_talent_loadout_to_variant(): it is a rule of the game.
 */
LRG_AVAILABLE_IN_ALL
void lrg_talent_loadout_set_first_level (LrgTalentLoadout *self,
                                         guint             first_level);

/**
 * lrg_talent_loadout_get_points_per_level:
 * @self: a #LrgTalentLoadout
 *
 * Returns: points granted per level from first-level on
 */
LRG_AVAILABLE_IN_ALL
guint lrg_talent_loadout_get_points_per_level (LrgTalentLoadout *self);

/**
 * lrg_talent_loadout_set_points_per_level:
 * @self: a #LrgTalentLoadout
 * @points_per_level: points granted per level (0..100)
 *
 * Sets the points granted per level. Not persisted.
 */
LRG_AVAILABLE_IN_ALL
void lrg_talent_loadout_set_points_per_level (LrgTalentLoadout *self,
                                              guint             points_per_level);

/**
 * lrg_talent_loadout_get_points_total:
 * @self: a #LrgTalentLoadout
 * @level: character level
 *
 * Computes the points granted at @level: level - first-level + 1 times
 * points-per-level when @level is at least first-level, otherwise 0.
 * The result saturates at %G_MAXUINT.
 *
 * Returns: total points granted at @level
 */
LRG_AVAILABLE_IN_ALL
guint lrg_talent_loadout_get_points_total (LrgTalentLoadout *self,
                                           guint             level);

/**
 * lrg_talent_loadout_get_points_spent:
 * @self: a #LrgTalentLoadout
 *
 * Returns: total points spent across all trees
 */
LRG_AVAILABLE_IN_ALL
guint lrg_talent_loadout_get_points_spent (LrgTalentLoadout *self);

/**
 * lrg_talent_loadout_get_points_available:
 * @self: a #LrgTalentLoadout
 * @level: character level
 *
 * Returns: points total minus points spent, saturating at 0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_talent_loadout_get_points_available (LrgTalentLoadout *self,
                                               guint             level);

/**
 * lrg_talent_loadout_get_points_in_tree:
 * @self: a #LrgTalentLoadout
 * @tree_id: tree id
 *
 * Returns: points spent in @tree_id (0 if none)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_talent_loadout_get_points_in_tree (LrgTalentLoadout *self,
                                             const gchar      *tree_id);

/**
 * lrg_talent_loadout_get_rank:
 * @self: a #LrgTalentLoadout
 * @tree_id: tree id
 * @node_id: node id
 *
 * Returns: current rank of the node (0 if unranked)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_talent_loadout_get_rank (LrgTalentLoadout *self,
                                   const gchar      *tree_id,
                                   const gchar      *node_id);

/**
 * lrg_talent_loadout_get_trees:
 * @self: a #LrgTalentLoadout
 *
 * Gets the ids of the trees with at least one spent point.
 *
 * Returns: (transfer container) (element-type utf8): sorted tree ids,
 *   strings owned by @self
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_talent_loadout_get_trees (LrgTalentLoadout *self);

/**
 * lrg_talent_loadout_can_spend:
 * @self: a #LrgTalentLoadout
 * @tree: the tree containing the node
 * @node_id: node to rank up
 * @level: character level
 * @error: (nullable): return location for an error
 *
 * Checks whether one point can be spent in @node_id, in this order:
 *
 * 1. tree class differs from the loadout class: %LRG_PROGRESSION_ERROR_INVALID
 * 2. node not in @tree: %LRG_PROGRESSION_ERROR_INVALID
 * 3. node already at max rank: %LRG_PROGRESSION_ERROR_LIMIT
 * 4. no unspent points at @level: %LRG_PROGRESSION_ERROR_LIMIT
 * 5. fewer than tier * tier-gate points already spent in @tree:
 *    %LRG_PROGRESSION_ERROR_REQUIREMENT
 * 6. prerequisite node not at its max rank (or missing from @tree):
 *    %LRG_PROGRESSION_ERROR_REQUIREMENT
 *
 * Returns: %TRUE if a point can be spent
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_talent_loadout_can_spend (LrgTalentLoadout  *self,
                                       LrgTalentTree     *tree,
                                       const gchar       *node_id,
                                       guint              level,
                                       GError           **error);

/**
 * lrg_talent_loadout_spend:
 * @self: a #LrgTalentLoadout
 * @tree: the tree containing the node
 * @node_id: node to rank up
 * @level: character level
 * @error: (nullable): return location for an error
 *
 * Spends one point in @node_id after lrg_talent_loadout_can_spend()
 * succeeds. On failure the loadout is unchanged.
 *
 * Returns: %TRUE if the point was spent
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_talent_loadout_spend (LrgTalentLoadout  *self,
                                   LrgTalentTree     *tree,
                                   const gchar       *node_id,
                                   guint              level,
                                   GError           **error);

/**
 * lrg_talent_loadout_reset:
 * @self: a #LrgTalentLoadout
 *
 * Refunds every spent point. The spec is kept.
 */
LRG_AVAILABLE_IN_ALL
void lrg_talent_loadout_reset (LrgTalentLoadout *self);

/**
 * lrg_talent_loadout_get_spec:
 * @self: a #LrgTalentLoadout
 *
 * Returns: (transfer none) (nullable): the primary tree id
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_talent_loadout_get_spec (LrgTalentLoadout *self);

/**
 * lrg_talent_loadout_set_spec:
 * @self: a #LrgTalentLoadout
 * @tree_id: (nullable): primary tree id
 *
 * Sets the primary tree (specialisation). Checked against the trees by
 * lrg_talent_loadout_validate().
 */
LRG_AVAILABLE_IN_ALL
void lrg_talent_loadout_set_spec (LrgTalentLoadout *self,
                                  const gchar      *tree_id);

/**
 * lrg_talent_loadout_get_granted_abilities:
 * @self: a #LrgTalentLoadout
 * @trees: (element-type utf8 LrgTalentTree): trees by id
 *
 * Collects the ability ids granted by nodes with rank >= 1. Trees not
 * present in @trees are skipped.
 *
 * Returns: (transfer full) (element-type utf8): sorted, de-duplicated ids
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_talent_loadout_get_granted_abilities (LrgTalentLoadout *self,
                                                     GHashTable       *trees);

/**
 * lrg_talent_loadout_sum_effect:
 * @self: a #LrgTalentLoadout
 * @trees: (element-type utf8 LrgTalentTree): trees by id
 * @effect: effect key
 *
 * Sums lrg_talent_node_get_value() at the current rank over every ranked
 * node whose effect equals @effect.
 *
 * Returns: the summed value
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_talent_loadout_sum_effect (LrgTalentLoadout *self,
                                       GHashTable       *trees,
                                       const gchar      *effect);

/**
 * lrg_talent_loadout_validate:
 * @self: a #LrgTalentLoadout
 * @trees: (element-type utf8 LrgTalentTree): trees by id
 * @level: character level
 * @error: (nullable): return location for an error
 *
 * Re-validates every spent point by replaying them into an empty loadout
 * with the same rules, tree by tree (sorted by id) and node by node in
 * (tier, column, id) order, using lrg_talent_loadout_can_spend(). This
 * rejects tampered snapshots that skip tier gates, exceed max rank or the
 * points available at @level, miss prerequisites or target another class.
 * A tree id missing from @trees (or mapped to a tree with another id)
 * yields %LRG_PROGRESSION_ERROR_NOT_FOUND; a spec that is not a known
 * tree of this class yields %LRG_PROGRESSION_ERROR_INVALID.
 * @self is never modified.
 *
 * Returns: %TRUE if the loadout is legal
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_talent_loadout_validate (LrgTalentLoadout  *self,
                                      GHashTable        *trees,
                                      guint              level,
                                      GError           **error);

/**
 * lrg_talent_loadout_to_variant:
 * @self: a #LrgTalentLoadout
 *
 * Serialises to %LRG_TALENT_LOADOUT_VARIANT_TYPE "(smsa(ssu))":
 * class id, optional spec, and (tree, node, rank) rows sorted by tree then
 * node. first-level and points-per-level are game rules and are not stored.
 *
 * Returns: (transfer full): a non-floating variant
 */
LRG_AVAILABLE_IN_ALL
GVariant *lrg_talent_loadout_to_variant (LrgTalentLoadout *self);

/**
 * lrg_talent_loadout_new_from_variant:
 * @variant: a "(smsa(ssu))" variant
 * @error: (nullable): return location for an error
 *
 * Restores a loadout. Rejects with %LRG_PROGRESSION_ERROR_INVALID: a wrong
 * type string, non-normal form, empty/overlong/non-UTF-8 ids, more than
 * %LRG_TALENT_LOADOUT_MAX_ROWS rows, ranks outside 1..%LRG_TALENT_MAX_RANK
 * and duplicate (tree, node) rows. Rule legality against the trees is
 * checked separately by lrg_talent_loadout_validate(); call it after
 * applying the game's first-level and points-per-level.
 *
 * Returns: (transfer full) (nullable): the loadout or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgTalentLoadout *lrg_talent_loadout_new_from_variant (GVariant  *variant,
                                                       GError   **error);

G_END_DECLS
