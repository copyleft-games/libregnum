/* lrg-card-instance.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCardInstance - Runtime instance of a card.
 *
 * A card instance represents a specific card in a deck/hand/pile during
 * gameplay. It references a card definition and tracks instance-specific
 * state like upgrade tier, temporary modifiers, and current zone.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-card-def.h"

G_BEGIN_DECLS

#define LRG_TYPE_CARD_INSTANCE (lrg_card_instance_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCardInstance, lrg_card_instance, LRG, CARD_INSTANCE, GObject)

/* Construction */

/**
 * lrg_card_instance_new:
 * @def: the card definition
 *
 * Creates a new card instance from a definition.
 *
 * Returns: (transfer full): A new #LrgCardInstance
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardInstance *
lrg_card_instance_new (LrgCardDef *def);

/**
 * lrg_card_instance_new_upgraded:
 * @def: the card definition
 * @upgrade_tier: the initial upgrade tier
 *
 * Creates a new card instance with a specific upgrade tier.
 *
 * Returns: (transfer full): A new #LrgCardInstance
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardInstance *
lrg_card_instance_new_upgraded (LrgCardDef        *def,
                                LrgCardUpgradeTier upgrade_tier);

/* Card Definition */

/**
 * lrg_card_instance_get_def:
 * @self: an #LrgCardInstance
 *
 * Gets the card definition for this instance.
 *
 * Returns: (transfer none): the card definition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardDef *
lrg_card_instance_get_def (LrgCardInstance *self);

/**
 * lrg_card_instance_get_id:
 * @self: an #LrgCardInstance
 *
 * Gets the ID of the card definition.
 *
 * Returns: (transfer none): the card ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_card_instance_get_id (LrgCardInstance *self);

/* Instance State */

/**
 * lrg_card_instance_get_upgrade_tier:
 * @self: an #LrgCardInstance
 *
 * Gets the upgrade tier of this card.
 *
 * Returns: the #LrgCardUpgradeTier
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardUpgradeTier
lrg_card_instance_get_upgrade_tier (LrgCardInstance *self);

/**
 * lrg_card_instance_set_upgrade_tier:
 * @self: an #LrgCardInstance
 * @tier: the new upgrade tier
 *
 * Sets the upgrade tier.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_instance_set_upgrade_tier (LrgCardInstance    *self,
                                    LrgCardUpgradeTier  tier);

/**
 * lrg_card_instance_upgrade:
 * @self: an #LrgCardInstance
 *
 * Upgrades the card to the next tier if possible.
 *
 * Returns: %TRUE if the card was upgraded
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_card_instance_upgrade (LrgCardInstance *self);

/**
 * lrg_card_instance_get_zone:
 * @self: an #LrgCardInstance
 *
 * Gets the current zone this card is in.
 *
 * Returns: the #LrgCardZone
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardZone
lrg_card_instance_get_zone (LrgCardInstance *self);

/**
 * lrg_card_instance_set_zone:
 * @self: an #LrgCardInstance
 * @zone: the new zone
 *
 * Sets the current zone. This is typically called by pile/hand operations.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_instance_set_zone (LrgCardInstance *self,
                            LrgCardZone      zone);

/* Temporary Modifiers */

/**
 * lrg_card_instance_get_cost_modifier:
 * @self: an #LrgCardInstance
 *
 * Gets the temporary cost modifier for this combat.
 *
 * Returns: the cost modifier (added to base cost)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_card_instance_get_cost_modifier (LrgCardInstance *self);

/**
 * lrg_card_instance_set_cost_modifier:
 * @self: an #LrgCardInstance
 * @modifier: the cost modifier
 *
 * Sets a temporary cost modifier.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_instance_set_cost_modifier (LrgCardInstance *self,
                                     gint             modifier);

/**
 * lrg_card_instance_add_cost_modifier:
 * @self: an #LrgCardInstance
 * @modifier: the modifier to add
 *
 * Adds to the temporary cost modifier.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_instance_add_cost_modifier (LrgCardInstance *self,
                                     gint             modifier);

/**
 * lrg_card_instance_get_temporary_keywords:
 * @self: an #LrgCardInstance
 *
 * Gets temporary keywords added to this card instance.
 *
 * Returns: the temporary #LrgCardKeyword flags
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardKeyword
lrg_card_instance_get_temporary_keywords (LrgCardInstance *self);

/**
 * lrg_card_instance_add_temporary_keyword:
 * @self: an #LrgCardInstance
 * @keyword: the keyword to add
 *
 * Adds a temporary keyword for this combat.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_instance_add_temporary_keyword (LrgCardInstance *self,
                                         LrgCardKeyword   keyword);

/**
 * lrg_card_instance_remove_temporary_keyword:
 * @self: an #LrgCardInstance
 * @keyword: the keyword to remove
 *
 * Removes a temporary keyword.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_instance_remove_temporary_keyword (LrgCardInstance *self,
                                            LrgCardKeyword   keyword);

/**
 * lrg_card_instance_clear_temporary_modifiers:
 * @self: an #LrgCardInstance
 *
 * Clears all temporary modifiers. Called at end of combat.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_instance_clear_temporary_modifiers (LrgCardInstance *self);

/* Keyword Checking (combines def + temporary) */

/**
 * lrg_card_instance_has_keyword:
 * @self: an #LrgCardInstance
 * @keyword: the keyword to check
 *
 * Checks if the card has a keyword (from def or temporary).
 *
 * Returns: %TRUE if the keyword is present
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_card_instance_has_keyword (LrgCardInstance *self,
                               LrgCardKeyword   keyword);

/**
 * lrg_card_instance_get_all_keywords:
 * @self: an #LrgCardInstance
 *
 * Gets all keywords (definition + temporary combined).
 *
 * Returns: combined #LrgCardKeyword flags
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardKeyword
lrg_card_instance_get_all_keywords (LrgCardInstance *self);

/* Cost Calculation */

/**
 * lrg_card_instance_get_effective_cost:
 * @self: an #LrgCardInstance
 * @ctx: (nullable): the combat context
 *
 * Gets the effective cost after all modifiers.
 *
 * Returns: the effective energy cost
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_card_instance_get_effective_cost (LrgCardInstance  *self,
                                      LrgCombatContext *ctx);

/* Play Count Tracking */

/**
 * lrg_card_instance_get_times_played:
 * @self: an #LrgCardInstance
 *
 * Gets the number of times this card has been played this combat.
 *
 * Returns: the play count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_card_instance_get_times_played (LrgCardInstance *self);

/**
 * lrg_card_instance_increment_play_count:
 * @self: an #LrgCardInstance
 *
 * Increments the play count. Called when card is played.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_instance_increment_play_count (LrgCardInstance *self);

/**
 * lrg_card_instance_reset_play_count:
 * @self: an #LrgCardInstance
 *
 * Resets the play count. Called at start of combat.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_instance_reset_play_count (LrgCardInstance *self);

/* Scoring Properties (Balatro-style) */

/**
 * lrg_card_instance_get_bonus_chips:
 * @self: an #LrgCardInstance
 *
 * Gets bonus chips added to this card instance.
 *
 * Returns: the bonus chips
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_card_instance_get_bonus_chips (LrgCardInstance *self);

/**
 * lrg_card_instance_set_bonus_chips:
 * @self: an #LrgCardInstance
 * @chips: the bonus chips
 *
 * Sets bonus chips for this card.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_instance_set_bonus_chips (LrgCardInstance *self,
                                   gint             chips);

/**
 * lrg_card_instance_add_bonus_chips:
 * @self: an #LrgCardInstance
 * @chips: chips to add
 *
 * Adds bonus chips to this card.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_instance_add_bonus_chips (LrgCardInstance *self,
                                   gint             chips);

/**
 * lrg_card_instance_get_total_chip_value:
 * @self: an #LrgCardInstance
 *
 * Gets the total chip value (base + bonus).
 *
 * Returns: total chip value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_card_instance_get_total_chip_value (LrgCardInstance *self);

/* Unique Instance ID */

/**
 * lrg_card_instance_get_instance_id:
 * @self: an #LrgCardInstance
 *
 * Gets a unique ID for this specific card instance.
 * Useful for tracking individual cards across zones.
 *
 * Returns: the unique instance ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_card_instance_get_instance_id (LrgCardInstance *self);

G_END_DECLS
