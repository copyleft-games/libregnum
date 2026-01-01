/* lrg-combat-rules.h
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef LRG_COMBAT_RULES_H
#define LRG_COMBAT_RULES_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-combatant.h"

G_BEGIN_DECLS

#define LRG_TYPE_COMBAT_RULES (lrg_combat_rules_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgCombatRules, lrg_combat_rules, LRG, COMBAT_RULES, GObject)

/**
 * LrgCombatRulesInterface:
 * @parent_iface: parent interface
 * @calculate_damage: calculate final damage after modifiers
 * @calculate_block: calculate final block after modifiers
 * @calculate_healing: calculate final healing after modifiers
 * @get_energy_per_turn: get energy granted per turn
 * @get_cards_per_turn: get cards drawn per turn
 * @get_hand_size_limit: get maximum hand size
 * @can_escape: check if escape from combat is allowed
 *
 * Interface for customizing combat mechanics.
 * Games can implement this to modify damage formulas,
 * status effect interactions, and turn structure.
 *
 * Since: 1.0
 */
struct _LrgCombatRulesInterface
{
    GTypeInterface parent_iface;

    /* Damage/Block calculations */
    gint    (* calculate_damage)     (LrgCombatRules *self,
                                      gint            base_damage,
                                      LrgCombatant   *attacker,
                                      LrgCombatant   *defender);
    gint    (* calculate_block)      (LrgCombatRules *self,
                                      gint            base_block,
                                      LrgCombatant   *blocker);
    gint    (* calculate_healing)    (LrgCombatRules *self,
                                      gint            base_healing,
                                      LrgCombatant   *target);

    /* Turn structure */
    gint    (* get_energy_per_turn)  (LrgCombatRules *self,
                                      LrgCombatant   *player);
    gint    (* get_cards_per_turn)   (LrgCombatRules *self,
                                      LrgCombatant   *player);
    gint    (* get_hand_size_limit)  (LrgCombatRules *self,
                                      LrgCombatant   *player);

    /* Combat options */
    gboolean (* can_escape)          (LrgCombatRules *self);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_combat_rules_calculate_damage:
 * @self: an #LrgCombatRules
 * @base_damage: the base damage amount
 * @attacker: (nullable): the attacking combatant
 * @defender: the defending combatant
 *
 * Calculates the final damage after applying all modifiers
 * (strength, weak, vulnerable, etc.) but before block.
 *
 * The default formula (Slay the Spire style):
 * 1. Add attacker's strength
 * 2. Apply weak (×0.75)
 * 3. Apply vulnerable (×1.5)
 * 4. Apply intangible (reduce to 1)
 *
 * Returns: the calculated damage amount
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_combat_rules_calculate_damage (LrgCombatRules *self,
                                   gint            base_damage,
                                   LrgCombatant   *attacker,
                                   LrgCombatant   *defender);

/**
 * lrg_combat_rules_calculate_block:
 * @self: an #LrgCombatRules
 * @base_block: the base block amount
 * @blocker: the combatant gaining block
 *
 * Calculates the final block after applying modifiers
 * (dexterity, frail, etc.).
 *
 * The default formula:
 * 1. Add dexterity
 * 2. Apply frail (×0.75)
 *
 * Returns: the calculated block amount
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_combat_rules_calculate_block (LrgCombatRules *self,
                                  gint            base_block,
                                  LrgCombatant   *blocker);

/**
 * lrg_combat_rules_calculate_healing:
 * @self: an #LrgCombatRules
 * @base_healing: the base healing amount
 * @target: the combatant being healed
 *
 * Calculates the final healing amount after modifiers.
 *
 * Returns: the calculated healing amount
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_combat_rules_calculate_healing (LrgCombatRules *self,
                                    gint            base_healing,
                                    LrgCombatant   *target);

/**
 * lrg_combat_rules_get_energy_per_turn:
 * @self: an #LrgCombatRules
 * @player: the player combatant
 *
 * Gets the amount of energy granted at the start of each turn.
 *
 * Returns: energy per turn (default: 3)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_combat_rules_get_energy_per_turn (LrgCombatRules *self,
                                      LrgCombatant   *player);

/**
 * lrg_combat_rules_get_cards_per_turn:
 * @self: an #LrgCombatRules
 * @player: the player combatant
 *
 * Gets the number of cards drawn at the start of each turn.
 *
 * Returns: cards per turn (default: 5)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_combat_rules_get_cards_per_turn (LrgCombatRules *self,
                                     LrgCombatant   *player);

/**
 * lrg_combat_rules_get_hand_size_limit:
 * @self: an #LrgCombatRules
 * @player: the player combatant
 *
 * Gets the maximum number of cards that can be in hand.
 *
 * Returns: hand size limit (default: 10)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_combat_rules_get_hand_size_limit (LrgCombatRules *self,
                                      LrgCombatant   *player);

/**
 * lrg_combat_rules_can_escape:
 * @self: an #LrgCombatRules
 *
 * Checks if the player can escape from combat.
 *
 * Returns: %TRUE if escape is allowed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_combat_rules_can_escape (LrgCombatRules *self);

G_END_DECLS

#endif /* LRG_COMBAT_RULES_H */
