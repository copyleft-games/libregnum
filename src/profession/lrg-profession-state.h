/* lrg-profession-state.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgProfessionState - a character's professions, skill levels, trainer
 * caps and known recipes.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-skill-band.h"
#include "lrg-profession-def.h"
#include "lrg-recipe-def.h"
#include "lrg-gather-node-def.h"

G_BEGIN_DECLS

/**
 * LRG_PROFESSION_STATE_MAX_PROFESSIONS:
 *
 * Maximum number of professions one state may hold (primary and secondary
 * combined), and the upper bound of #LrgProfessionState:max-primary.
 */
#define LRG_PROFESSION_STATE_MAX_PROFESSIONS (64)

/**
 * LRG_PROFESSION_STATE_MAX_RECIPES:
 *
 * Maximum number of known recipes one state may hold.
 */
#define LRG_PROFESSION_STATE_MAX_RECIPES (4096)

/**
 * LRG_PROFESSION_STATE_VARIANT_TYPE:
 *
 * GVariant type string of lrg_profession_state_to_variant():
 * `(max_primary, [(profession_id, skill, max_skill)], [recipe_id])`.
 */
#define LRG_PROFESSION_STATE_VARIANT_TYPE "(ua(suu)as)"

/**
 * LrgItemCountFunc:
 * @item_id: item identifier to count
 * @user_data: (closure): caller data
 *
 * Reports how many of @item_id the crafter can use. Lets crafting checks
 * run against any storage (bags, bank, a server-side item table).
 *
 * Returns: the available quantity
 */
typedef guint (*LrgItemCountFunc) (const gchar *item_id,
                                   gpointer     user_data);

#define LRG_TYPE_PROFESSION_STATE (lrg_profession_state_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgProfessionState, lrg_profession_state, LRG, PROFESSION_STATE, GObject)

/**
 * lrg_profession_state_new:
 * @max_primary: number of primary professions allowed, 0..%LRG_PROFESSION_STATE_MAX_PROFESSIONS
 *
 * Creates an empty profession state.
 *
 * Returns: (transfer full): a new #LrgProfessionState
 */
LRG_AVAILABLE_IN_ALL
LrgProfessionState *lrg_profession_state_new (guint max_primary);

/**
 * lrg_profession_state_get_max_primary:
 * @self: an #LrgProfessionState
 *
 * Returns: the number of primary professions allowed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_profession_state_get_max_primary (LrgProfessionState *self);

/**
 * lrg_profession_state_set_max_primary:
 * @self: an #LrgProfessionState
 * @max_primary: new limit, 0..%LRG_PROFESSION_STATE_MAX_PROFESSIONS
 *
 * Changes the primary limit. Lowering it never removes professions already
 * known; it only blocks further primary learns.
 */
LRG_AVAILABLE_IN_ALL
void lrg_profession_state_set_max_primary (LrgProfessionState *self,
                                           guint               max_primary);

/**
 * lrg_profession_state_get_train_margin:
 * @self: an #LrgProfessionState
 *
 * Returns: how far below the current cap a skill may be while training the next tier
 */
LRG_AVAILABLE_IN_ALL
guint lrg_profession_state_get_train_margin (LrgProfessionState *self);

/**
 * lrg_profession_state_set_train_margin:
 * @self: an #LrgProfessionState
 * @margin: margin, 0..%LRG_PROFESSION_SKILL_LIMIT
 *
 * Sets the training margin: the next tier can be trained once
 * `skill >= current_cap - margin` (saturating at 0).
 */
LRG_AVAILABLE_IN_ALL
void lrg_profession_state_set_train_margin (LrgProfessionState *self,
                                            guint               margin);

/**
 * lrg_profession_state_learn:
 * @self: an #LrgProfessionState
 * @def: profession to learn
 * @level: character level
 * @error: (nullable): return location for a #GError
 *
 * Learns a profession at skill 1 with the first tier's cap as maximum.
 * Checks, in order: %LRG_PROGRESSION_ERROR_INVALID for a bad identifier or
 * a profession without tiers; %LRG_PROGRESSION_ERROR_DUPLICATE when already
 * known; %LRG_PROGRESSION_ERROR_LIMIT when a primary profession would
 * exceed #LrgProfessionState:max-primary or the state already holds
 * %LRG_PROFESSION_STATE_MAX_PROFESSIONS; %LRG_PROGRESSION_ERROR_REQUIREMENT
 * when @level is below tier 0's required level. State is unchanged on
 * failure.
 *
 * Returns: %TRUE if learned
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_profession_state_learn (LrgProfessionState  *self,
                                     LrgProfessionDef    *def,
                                     guint                level,
                                     GError             **error);

/**
 * lrg_profession_state_unlearn:
 * @self: an #LrgProfessionState
 * @profession_id: profession identifier
 *
 * Forgets a profession, its skill and every recipe learned for it.
 *
 * Returns: %TRUE if the profession was known
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_profession_state_unlearn (LrgProfessionState *self,
                                       const gchar        *profession_id);

/**
 * lrg_profession_state_knows:
 * @self: an #LrgProfessionState
 * @profession_id: profession identifier
 *
 * Returns: %TRUE if the profession is known
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_profession_state_knows (LrgProfessionState *self,
                                     const gchar        *profession_id);

/**
 * lrg_profession_state_get_skill:
 * @self: an #LrgProfessionState
 * @profession_id: profession identifier
 *
 * Returns: current skill, or 0 if unknown
 */
LRG_AVAILABLE_IN_ALL
guint lrg_profession_state_get_skill (LrgProfessionState *self,
                                      const gchar        *profession_id);

/**
 * lrg_profession_state_get_max_skill:
 * @self: an #LrgProfessionState
 * @profession_id: profession identifier
 *
 * Returns: current skill cap (trained tier), or 0 if unknown
 */
LRG_AVAILABLE_IN_ALL
guint lrg_profession_state_get_max_skill (LrgProfessionState *self,
                                          const gchar        *profession_id);

/**
 * lrg_profession_state_get_primary_count:
 * @self: an #LrgProfessionState
 * @defs: (nullable) (element-type utf8 LrgProfessionDef): profession definitions by id
 *
 * Counts known primary professions. The kind comes from @defs when the
 * profession is present there, otherwise from the kind recorded when it
 * was learned. A profession whose kind is unknown (restored from a
 * snapshot without definitions and absent from @defs) counts as primary so
 * the limit fails closed.
 *
 * Returns: number of primary professions
 */
LRG_AVAILABLE_IN_ALL
guint lrg_profession_state_get_primary_count (LrgProfessionState *self,
                                              GHashTable         *defs);

/**
 * lrg_profession_state_get_professions:
 * @self: an #LrgProfessionState
 *
 * Returns: (transfer container) (element-type utf8): known profession ids
 *   sorted with g_strcmp0(); strings are owned by @self
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_profession_state_get_professions (LrgProfessionState *self);

/**
 * lrg_profession_state_can_train:
 * @self: an #LrgProfessionState
 * @def: profession definition
 * @level: character level
 * @out_tier: (out) (optional) (nullable) (transfer none): the tier that would be trained
 * @error: (nullable): return location for a #GError
 *
 * Checks whether the next tier (first cap above the current cap) can be
 * trained. Use the returned tier's cost to charge the player before
 * calling lrg_profession_state_train(). Errors, in order:
 * %LRG_PROGRESSION_ERROR_NOT_FOUND when the profession is not known;
 * %LRG_PROGRESSION_ERROR_LIMIT at the top tier;
 * %LRG_PROGRESSION_ERROR_REQUIREMENT when @level is below the tier's
 * required level or the skill is below `cap - train-margin`.
 *
 * Returns: %TRUE if training is possible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_profession_state_can_train (LrgProfessionState       *self,
                                         LrgProfessionDef         *def,
                                         guint                     level,
                                         const LrgProfessionTier **out_tier,
                                         GError                  **error);

/**
 * lrg_profession_state_train:
 * @self: an #LrgProfessionState
 * @def: profession definition
 * @level: character level
 * @error: (nullable): return location for a #GError
 *
 * Raises the skill cap to the next tier's cap after the same checks as
 * lrg_profession_state_can_train(). The skill itself is unchanged.
 *
 * Returns: %TRUE if trained
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_profession_state_train (LrgProfessionState  *self,
                                     LrgProfessionDef    *def,
                                     guint                level,
                                     GError             **error);

/**
 * lrg_profession_state_learn_recipe:
 * @self: an #LrgProfessionState
 * @recipe: recipe to learn
 * @error: (nullable): return location for a #GError
 *
 * Learns a recipe. Errors, in order: %LRG_PROGRESSION_ERROR_INVALID for a
 * bad recipe or profession identifier; %LRG_PROGRESSION_ERROR_DUPLICATE
 * when already known; %LRG_PROGRESSION_ERROR_REQUIREMENT when the
 * profession is unknown or the skill is below the recipe's required skill;
 * %LRG_PROGRESSION_ERROR_LIMIT at %LRG_PROFESSION_STATE_MAX_RECIPES.
 * The recipe's #LrgRecipeDef:required-level is not checked here.
 *
 * Returns: %TRUE if learned
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_profession_state_learn_recipe (LrgProfessionState  *self,
                                            LrgRecipeDef        *recipe,
                                            GError             **error);

/**
 * lrg_profession_state_learn_starter_recipes:
 * @self: an #LrgProfessionState
 * @recipes: (element-type LrgRecipeDef): candidate recipes
 *
 * Learns every %LRG_RECIPE_SOURCE_STARTER recipe from @recipes that
 * lrg_profession_state_learn_recipe() accepts (profession known, skill high
 * enough, not yet known). Call after learning or training a profession.
 *
 * Returns: number of recipes learned
 */
LRG_AVAILABLE_IN_ALL
guint lrg_profession_state_learn_starter_recipes (LrgProfessionState *self,
                                                  GPtrArray          *recipes);

/**
 * lrg_profession_state_knows_recipe:
 * @self: an #LrgProfessionState
 * @recipe_id: recipe identifier
 *
 * Returns: %TRUE if the recipe is known
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_profession_state_knows_recipe (LrgProfessionState *self,
                                            const gchar        *recipe_id);

/**
 * lrg_profession_state_get_recipes:
 * @self: an #LrgProfessionState
 * @profession_id: (nullable): profession filter, %NULL for every profession
 *
 * Returns: (transfer container) (element-type utf8): known recipe ids sorted
 *   with g_strcmp0(); strings are owned by @self
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_profession_state_get_recipes (LrgProfessionState *self,
                                             const gchar        *profession_id);

/**
 * lrg_profession_state_try_skill_up:
 * @self: an #LrgProfessionState
 * @profession_id: profession identifier
 * @band: band of the recipe or node just used
 * @roll: caller-supplied random value in [0, 1)
 *
 * Grants one skill point when `roll < lrg_skill_band_get_chance (band, skill)`
 * and the skill is below the current cap. Unknown professions, rolls
 * outside [0, 1) (including NaN and infinities) and invalid bands grant
 * nothing.
 *
 * Returns: points gained, 0 or 1
 */
LRG_AVAILABLE_IN_ALL
guint lrg_profession_state_try_skill_up (LrgProfessionState *self,
                                         const gchar        *profession_id,
                                         const LrgSkillBand *band,
                                         gdouble             roll);

/**
 * lrg_profession_state_set_skill:
 * @self: an #LrgProfessionState
 * @profession_id: valid profession identifier
 * @skill: new skill (clamped to @max_skill)
 * @max_skill: new cap, 1..%LRG_PROFESSION_SKILL_LIMIT
 *
 * Migration/admin override. Adds the profession when it is not known, with
 * an unknown kind (counted as primary by lrg_profession_state_get_primary_count()
 * unless definitions say otherwise). Bypasses the primary limit, levels and
 * tiers; recipes are kept.
 */
LRG_AVAILABLE_IN_ALL
void lrg_profession_state_set_skill (LrgProfessionState *self,
                                     const gchar        *profession_id,
                                     guint               skill,
                                     guint               max_skill);

/**
 * lrg_profession_state_can_craft:
 * @self: an #LrgProfessionState
 * @recipe: recipe to craft
 * @quantity: number of crafts, at least 1
 * @count_func: (scope call) (closure user_data) (nullable): item counter; %NULL skips item checks
 * @user_data: (nullable): data for @count_func
 * @error: (nullable): return location for a #GError
 *
 * Checks that the recipe is known, its profession skill is at least the
 * required skill and, with @count_func, that the recipe's tool (if any) is
 * present and each reagent is available `count * quantity` times (computed
 * in 64 bits, no overflow). Errors: %LRG_PROGRESSION_ERROR_INVALID for
 * @quantity 0 or a bad id; %LRG_PROGRESSION_ERROR_REQUIREMENT otherwise.
 * Station proximity and required level are checked by the caller.
 *
 * Returns: %TRUE if the craft may proceed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_profession_state_can_craft (LrgProfessionState  *self,
                                         LrgRecipeDef        *recipe,
                                         guint                quantity,
                                         LrgItemCountFunc     count_func,
                                         gpointer             user_data,
                                         GError             **error);

/**
 * lrg_profession_state_can_gather:
 * @self: an #LrgProfessionState
 * @node: gather node definition
 * @error: (nullable): return location for a #GError
 *
 * Checks that the node's profession is known and the skill reaches the
 * node's required skill. The tool is not checked; see
 * lrg_profession_state_can_gather_with(). Errors:
 * %LRG_PROGRESSION_ERROR_INVALID for a bad profession id,
 * %LRG_PROGRESSION_ERROR_REQUIREMENT otherwise.
 *
 * Returns: %TRUE if the node can be gathered
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_profession_state_can_gather (LrgProfessionState  *self,
                                          LrgGatherNodeDef    *node,
                                          GError             **error);

/**
 * lrg_profession_state_can_gather_with:
 * @self: an #LrgProfessionState
 * @node: gather node definition
 * @count_func: (scope call) (closure user_data) (nullable): item counter used for the required tool
 * @user_data: (nullable): data for @count_func
 * @error: (nullable): return location for a #GError
 *
 * lrg_profession_state_can_gather() plus, when @count_func is given and the
 * node has a required tool, a check that at least one tool is present
 * (%LRG_PROGRESSION_ERROR_REQUIREMENT).
 *
 * Returns: %TRUE if the node can be gathered
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_profession_state_can_gather_with (LrgProfessionState  *self,
                                               LrgGatherNodeDef    *node,
                                               LrgItemCountFunc     count_func,
                                               gpointer             user_data,
                                               GError             **error);

/**
 * lrg_profession_state_inventory_count:
 * @item_id: item identifier
 * @user_data: an #LrgInventory
 *
 * Ready-made #LrgItemCountFunc for #LrgInventory: returns
 * lrg_inventory_count_item() (the total quantity across all slots). Pass it
 * with the inventory as user data to lrg_profession_state_can_craft().
 *
 * Returns: quantity of @item_id in the inventory
 */
LRG_AVAILABLE_IN_ALL
guint lrg_profession_state_inventory_count (const gchar *item_id,
                                            gpointer     user_data);

/**
 * lrg_profession_state_to_variant:
 * @self: an #LrgProfessionState
 *
 * Serialises the state as %LRG_PROFESSION_STATE_VARIANT_TYPE
 * `(ua(suu)as)`: max-primary, professions sorted by id as
 * `(id, skill, max_skill)`, and known recipe ids sorted. The train margin
 * is configuration and is not persisted.
 *
 * Returns: (transfer full): a non-floating #GVariant
 */
LRG_AVAILABLE_IN_ALL
GVariant *lrg_profession_state_to_variant (LrgProfessionState *self);

/**
 * lrg_profession_state_new_from_variant:
 * @variant: a `(ua(suu)as)` variant
 * @recipes: (nullable) (element-type utf8 LrgRecipeDef): recipe definitions by id
 * @error: (nullable): return location for a #GError
 *
 * Restores a state. Equivalent to
 * lrg_profession_state_new_from_variant_full() without profession
 * definitions.
 *
 * Returns: (transfer full) (nullable): the state, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgProfessionState *lrg_profession_state_new_from_variant (GVariant    *variant,
                                                           GHashTable  *recipes,
                                                           GError     **error);

/**
 * lrg_profession_state_new_from_variant_full:
 * @variant: a `(ua(suu)as)` variant
 * @professions: (nullable) (element-type utf8 LrgProfessionDef): profession definitions by id
 * @recipes: (nullable) (element-type utf8 LrgRecipeDef): recipe definitions by id
 * @error: (nullable): return location for a #GError
 *
 * Restores a state, rejecting the whole snapshot on any problem. Always
 * checked (%LRG_PROGRESSION_ERROR_INVALID): exact type string, normal form,
 * max-primary <= 64, at most 64 professions and 4096 recipes, ids
 * non-empty, <= 128 bytes and valid UTF-8, no duplicate ids, max skill in
 * 1..%LRG_PROFESSION_SKILL_LIMIT, skill <= max skill, and each recipe's
 * profession present in the snapshot. Recipe ids missing from @recipes (or
 * any recipe when @recipes is %NULL) give %LRG_PROGRESSION_ERROR_NOT_FOUND.
 *
 * With @professions: unknown profession ids give
 * %LRG_PROGRESSION_ERROR_NOT_FOUND, a max skill above the definition's
 * top cap or more primaries than max-primary give
 * %LRG_PROGRESSION_ERROR_INVALID, and every profession's kind is resolved
 * so the primary limit is exact afterwards.
 *
 * Returns: (transfer full) (nullable): the state, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgProfessionState *lrg_profession_state_new_from_variant_full (GVariant    *variant,
                                                                GHashTable  *professions,
                                                                GHashTable  *recipes,
                                                                GError     **error);

G_END_DECLS
