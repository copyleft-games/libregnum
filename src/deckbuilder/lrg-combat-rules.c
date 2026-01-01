/* lrg-combat-rules.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-combat-rules.h"
#include "../lrg-log.h"
#include <math.h>

/**
 * SECTION:lrg-combat-rules
 * @title: LrgCombatRules
 * @short_description: Interface for combat mechanics
 *
 * #LrgCombatRules defines how combat calculations work.
 * Games can implement this interface to customize damage
 * formulas, status effect interactions, and turn structure.
 *
 * The default implementations follow Slay the Spire conventions:
 * - Damage: base + strength, ×0.75 if weak, ×1.5 if vulnerable
 * - Block: base + dexterity, ×0.75 if frail
 * - 3 energy per turn
 * - 5 cards drawn per turn
 * - 10 card hand limit
 *
 * Since: 1.0
 */

G_DEFINE_INTERFACE (LrgCombatRules, lrg_combat_rules, G_TYPE_OBJECT)

/* Default implementations */

static gint
lrg_combat_rules_default_calculate_damage (LrgCombatRules *self,
                                           gint            base_damage,
                                           LrgCombatant   *attacker,
                                           LrgCombatant   *defender)
{
    gdouble damage;
    gint strength;

    damage = (gdouble)base_damage;

    /* Step 1: Add attacker's strength */
    if (attacker != NULL)
    {
        strength = lrg_combatant_get_status_stacks (attacker, "strength");
        damage += strength;
    }

    /* Step 2: Apply weak (25% less damage) */
    if (attacker != NULL && lrg_combatant_has_status (attacker, "weak"))
        damage *= 0.75;

    /* Step 3: Apply vulnerable (50% more damage) */
    if (defender != NULL && lrg_combatant_has_status (defender, "vulnerable"))
        damage *= 1.5;

    /* Round damage */
    damage = floor (damage);

    /* Step 4: Apply intangible (reduce to 1) */
    if (defender != NULL && lrg_combatant_has_status (defender, "intangible"))
    {
        if (damage > 1.0)
            damage = 1.0;
    }

    return MAX (0, (gint)damage);
}

static gint
lrg_combat_rules_default_calculate_block (LrgCombatRules *self,
                                          gint            base_block,
                                          LrgCombatant   *blocker)
{
    gdouble block;
    gint dexterity;

    block = (gdouble)base_block;

    /* Step 1: Add dexterity */
    if (blocker != NULL)
    {
        dexterity = lrg_combatant_get_status_stacks (blocker, "dexterity");
        block += dexterity;
    }

    /* Step 2: Apply frail (25% less block) */
    if (blocker != NULL && lrg_combatant_has_status (blocker, "frail"))
        block *= 0.75;

    return MAX (0, (gint)floor (block));
}

static gint
lrg_combat_rules_default_calculate_healing (LrgCombatRules *self,
                                            gint            base_healing,
                                            LrgCombatant   *target)
{
    /* Default: no healing modifiers */
    return MAX (0, base_healing);
}

static gint
lrg_combat_rules_default_get_energy_per_turn (LrgCombatRules *self,
                                              LrgCombatant   *player)
{
    /* Default: 3 energy per turn */
    return 3;
}

static gint
lrg_combat_rules_default_get_cards_per_turn (LrgCombatRules *self,
                                             LrgCombatant   *player)
{
    /* Default: 5 cards per turn */
    return 5;
}

static gint
lrg_combat_rules_default_get_hand_size_limit (LrgCombatRules *self,
                                              LrgCombatant   *player)
{
    /* Default: 10 card hand limit */
    return 10;
}

static gboolean
lrg_combat_rules_default_can_escape (LrgCombatRules *self)
{
    /* Default: no escape */
    return FALSE;
}

static void
lrg_combat_rules_default_init (LrgCombatRulesInterface *iface)
{
    iface->calculate_damage = lrg_combat_rules_default_calculate_damage;
    iface->calculate_block = lrg_combat_rules_default_calculate_block;
    iface->calculate_healing = lrg_combat_rules_default_calculate_healing;
    iface->get_energy_per_turn = lrg_combat_rules_default_get_energy_per_turn;
    iface->get_cards_per_turn = lrg_combat_rules_default_get_cards_per_turn;
    iface->get_hand_size_limit = lrg_combat_rules_default_get_hand_size_limit;
    iface->can_escape = lrg_combat_rules_default_can_escape;
}

gint
lrg_combat_rules_calculate_damage (LrgCombatRules *self,
                                   gint            base_damage,
                                   LrgCombatant   *attacker,
                                   LrgCombatant   *defender)
{
    LrgCombatRulesInterface *iface;

    g_return_val_if_fail (LRG_IS_COMBAT_RULES (self), base_damage);

    iface = LRG_COMBAT_RULES_GET_IFACE (self);
    g_return_val_if_fail (iface->calculate_damage != NULL, base_damage);

    return iface->calculate_damage (self, base_damage, attacker, defender);
}

gint
lrg_combat_rules_calculate_block (LrgCombatRules *self,
                                  gint            base_block,
                                  LrgCombatant   *blocker)
{
    LrgCombatRulesInterface *iface;

    g_return_val_if_fail (LRG_IS_COMBAT_RULES (self), base_block);

    iface = LRG_COMBAT_RULES_GET_IFACE (self);
    g_return_val_if_fail (iface->calculate_block != NULL, base_block);

    return iface->calculate_block (self, base_block, blocker);
}

gint
lrg_combat_rules_calculate_healing (LrgCombatRules *self,
                                    gint            base_healing,
                                    LrgCombatant   *target)
{
    LrgCombatRulesInterface *iface;

    g_return_val_if_fail (LRG_IS_COMBAT_RULES (self), base_healing);

    iface = LRG_COMBAT_RULES_GET_IFACE (self);
    g_return_val_if_fail (iface->calculate_healing != NULL, base_healing);

    return iface->calculate_healing (self, base_healing, target);
}

gint
lrg_combat_rules_get_energy_per_turn (LrgCombatRules *self,
                                      LrgCombatant   *player)
{
    LrgCombatRulesInterface *iface;

    g_return_val_if_fail (LRG_IS_COMBAT_RULES (self), 3);

    iface = LRG_COMBAT_RULES_GET_IFACE (self);
    g_return_val_if_fail (iface->get_energy_per_turn != NULL, 3);

    return iface->get_energy_per_turn (self, player);
}

gint
lrg_combat_rules_get_cards_per_turn (LrgCombatRules *self,
                                     LrgCombatant   *player)
{
    LrgCombatRulesInterface *iface;

    g_return_val_if_fail (LRG_IS_COMBAT_RULES (self), 5);

    iface = LRG_COMBAT_RULES_GET_IFACE (self);
    g_return_val_if_fail (iface->get_cards_per_turn != NULL, 5);

    return iface->get_cards_per_turn (self, player);
}

gint
lrg_combat_rules_get_hand_size_limit (LrgCombatRules *self,
                                      LrgCombatant   *player)
{
    LrgCombatRulesInterface *iface;

    g_return_val_if_fail (LRG_IS_COMBAT_RULES (self), 10);

    iface = LRG_COMBAT_RULES_GET_IFACE (self);
    g_return_val_if_fail (iface->get_hand_size_limit != NULL, 10);

    return iface->get_hand_size_limit (self, player);
}

gboolean
lrg_combat_rules_can_escape (LrgCombatRules *self)
{
    LrgCombatRulesInterface *iface;

    g_return_val_if_fail (LRG_IS_COMBAT_RULES (self), FALSE);

    iface = LRG_COMBAT_RULES_GET_IFACE (self);
    g_return_val_if_fail (iface->can_escape != NULL, FALSE);

    return iface->can_escape (self);
}
