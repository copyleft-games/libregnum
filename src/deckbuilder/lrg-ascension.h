/* lrg-ascension.h
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAscension - Challenge mode configuration.
 *
 * Ascension levels add increasing difficulty modifiers to runs.
 * Inspired by Slay the Spire's ascension system where each level
 * adds cumulative challenges.
 *
 * Standard ascension levels (like StS):
 * - A1: Elites drop worse rewards
 * - A2: Start with 1 less max HP
 * - A3: Rare card pity timer increased
 * - A4: Start with 2 less max HP (A2 becomes 3 less total)
 * - A5: Heal 25% less at rest sites
 * - ...up to A20
 *
 * This type allows configuration of modifiers per level.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_ASCENSION (lrg_ascension_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAscension, lrg_ascension, LRG, ASCENSION, GObject)

/**
 * LRG_ASCENSION_MAX_LEVEL:
 *
 * Maximum ascension level (like Slay the Spire).
 *
 * Since: 1.0
 */
#define LRG_ASCENSION_MAX_LEVEL 20

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_ascension_new:
 * @level: ascension level (0 = normal, 1-20 = ascension)
 *
 * Creates a new ascension configuration for a level.
 *
 * Returns: (transfer full): a new #LrgAscension
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAscension * lrg_ascension_new (gint level);

/**
 * lrg_ascension_new_default:
 * @level: ascension level
 *
 * Creates a new ascension with default modifiers for that level.
 * Uses standard StS-style ascension progression.
 *
 * Returns: (transfer full): a new #LrgAscension with defaults
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAscension * lrg_ascension_new_default (gint level);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_ascension_get_level:
 * @self: a #LrgAscension
 *
 * Gets the ascension level.
 *
 * Returns: the level (0-20)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_ascension_get_level (LrgAscension *self);

/**
 * lrg_ascension_get_name:
 * @self: a #LrgAscension
 *
 * Gets the display name (e.g., "Ascension 5").
 *
 * Returns: (transfer none): the name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_ascension_get_name (LrgAscension *self);

/**
 * lrg_ascension_get_description:
 * @self: a #LrgAscension
 *
 * Gets the description of this level's modifiers.
 *
 * Returns: (transfer none) (nullable): the description
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_ascension_get_description (LrgAscension *self);

/**
 * lrg_ascension_set_description:
 * @self: a #LrgAscension
 * @description: (nullable): the description
 *
 * Sets the description.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_ascension_set_description (LrgAscension *self,
                                     const gchar  *description);

/**
 * lrg_ascension_get_modifiers:
 * @self: a #LrgAscension
 *
 * Gets the active modifier flags.
 *
 * Returns: the #LrgAscensionModifier flags
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAscensionModifier lrg_ascension_get_modifiers (LrgAscension *self);

/**
 * lrg_ascension_has_modifier:
 * @self: a #LrgAscension
 * @modifier: modifier to check
 *
 * Checks if a specific modifier is active.
 *
 * Returns: %TRUE if modifier is active
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ascension_has_modifier (LrgAscension         *self,
                                      LrgAscensionModifier  modifier);

/**
 * lrg_ascension_add_modifier:
 * @self: a #LrgAscension
 * @modifier: modifier to add
 *
 * Adds a modifier.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_ascension_add_modifier (LrgAscension         *self,
                                  LrgAscensionModifier  modifier);

/* ==========================================================================
 * Numeric Modifiers
 * ========================================================================== */

/**
 * lrg_ascension_get_hp_reduction:
 * @self: a #LrgAscension
 *
 * Gets the starting HP reduction.
 *
 * Returns: HP to subtract from starting max HP
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_ascension_get_hp_reduction (LrgAscension *self);

/**
 * lrg_ascension_set_hp_reduction:
 * @self: a #LrgAscension
 * @reduction: HP reduction amount
 *
 * Sets the starting HP reduction.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_ascension_set_hp_reduction (LrgAscension *self,
                                      gint          reduction);

/**
 * lrg_ascension_get_gold_reduction:
 * @self: a #LrgAscension
 *
 * Gets the starting gold reduction.
 *
 * Returns: gold to subtract from starting gold
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_ascension_get_gold_reduction (LrgAscension *self);

/**
 * lrg_ascension_set_gold_reduction:
 * @self: a #LrgAscension
 * @reduction: gold reduction amount
 *
 * Sets the starting gold reduction.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_ascension_set_gold_reduction (LrgAscension *self,
                                        gint          reduction);

/**
 * lrg_ascension_get_heal_reduction_percent:
 * @self: a #LrgAscension
 *
 * Gets the healing reduction percentage.
 *
 * Returns: percentage of healing lost (0-100)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_ascension_get_heal_reduction_percent (LrgAscension *self);

/**
 * lrg_ascension_set_heal_reduction_percent:
 * @self: a #LrgAscension
 * @percent: healing reduction percentage
 *
 * Sets the healing reduction percentage.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_ascension_set_heal_reduction_percent (LrgAscension *self,
                                                gint          percent);

/**
 * lrg_ascension_get_enemy_hp_increase_percent:
 * @self: a #LrgAscension
 *
 * Gets the enemy HP increase percentage.
 *
 * Returns: percentage increase in enemy HP
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_ascension_get_enemy_hp_increase_percent (LrgAscension *self);

/**
 * lrg_ascension_set_enemy_hp_increase_percent:
 * @self: a #LrgAscension
 * @percent: enemy HP increase percentage
 *
 * Sets the enemy HP increase percentage.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_ascension_set_enemy_hp_increase_percent (LrgAscension *self,
                                                   gint          percent);

/**
 * lrg_ascension_get_enemy_damage_increase_percent:
 * @self: a #LrgAscension
 *
 * Gets the enemy damage increase percentage.
 *
 * Returns: percentage increase in enemy damage
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_ascension_get_enemy_damage_increase_percent (LrgAscension *self);

/**
 * lrg_ascension_set_enemy_damage_increase_percent:
 * @self: a #LrgAscension
 * @percent: enemy damage increase percentage
 *
 * Sets the enemy damage increase percentage.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_ascension_set_enemy_damage_increase_percent (LrgAscension *self,
                                                       gint          percent);

/* ==========================================================================
 * Application
 * ========================================================================== */

/**
 * lrg_ascension_apply_hp:
 * @self: a #LrgAscension
 * @base_hp: base HP before modifiers
 *
 * Applies HP modifiers.
 *
 * Returns: modified HP value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_ascension_apply_hp (LrgAscension *self,
                              gint          base_hp);

/**
 * lrg_ascension_apply_gold:
 * @self: a #LrgAscension
 * @base_gold: base gold before modifiers
 *
 * Applies gold modifiers.
 *
 * Returns: modified gold value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_ascension_apply_gold (LrgAscension *self,
                                gint          base_gold);

/**
 * lrg_ascension_apply_heal:
 * @self: a #LrgAscension
 * @base_heal: base heal amount before modifiers
 *
 * Applies healing modifiers.
 *
 * Returns: modified heal amount
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_ascension_apply_heal (LrgAscension *self,
                                gint          base_heal);

/**
 * lrg_ascension_apply_enemy_hp:
 * @self: a #LrgAscension
 * @base_hp: base enemy HP
 *
 * Applies enemy HP modifiers.
 *
 * Returns: modified enemy HP
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_ascension_apply_enemy_hp (LrgAscension *self,
                                    gint          base_hp);

/**
 * lrg_ascension_apply_enemy_damage:
 * @self: a #LrgAscension
 * @base_damage: base enemy damage
 *
 * Applies enemy damage modifiers.
 *
 * Returns: modified enemy damage
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_ascension_apply_enemy_damage (LrgAscension *self,
                                        gint          base_damage);

G_END_DECLS
