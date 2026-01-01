/* lrg-player-combatant.h
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef LRG_PLAYER_COMBATANT_H
#define LRG_PLAYER_COMBATANT_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-combatant.h"

G_BEGIN_DECLS

#define LRG_TYPE_PLAYER_COMBATANT (lrg_player_combatant_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPlayerCombatant, lrg_player_combatant, LRG, PLAYER_COMBATANT, GObject)

/**
 * lrg_player_combatant_new:
 * @id: player identifier
 * @name: player display name
 * @max_health: maximum health
 *
 * Creates a new player combatant.
 *
 * Returns: (transfer full): a new #LrgPlayerCombatant
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPlayerCombatant *
lrg_player_combatant_new (const gchar *id,
                          const gchar *name,
                          gint         max_health);

/**
 * lrg_player_combatant_set_max_health:
 * @self: a #LrgPlayerCombatant
 * @max_health: new maximum health
 *
 * Sets the player's maximum health.
 * Current health is clamped if it exceeds the new max.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_player_combatant_set_max_health (LrgPlayerCombatant *self,
                                     gint                max_health);

/**
 * lrg_player_combatant_get_gold:
 * @self: a #LrgPlayerCombatant
 *
 * Gets the player's current gold amount.
 *
 * Returns: gold amount
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_player_combatant_get_gold (LrgPlayerCombatant *self);

/**
 * lrg_player_combatant_set_gold:
 * @self: a #LrgPlayerCombatant
 * @gold: new gold amount
 *
 * Sets the player's gold amount.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_player_combatant_set_gold (LrgPlayerCombatant *self,
                               gint                gold);

/**
 * lrg_player_combatant_add_gold:
 * @self: a #LrgPlayerCombatant
 * @amount: gold to add
 *
 * Adds gold to the player's total.
 *
 * Returns: new gold total
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_player_combatant_add_gold (LrgPlayerCombatant *self,
                               gint                amount);

/**
 * lrg_player_combatant_remove_gold:
 * @self: a #LrgPlayerCombatant
 * @amount: gold to remove
 *
 * Removes gold from the player. Cannot go below 0.
 *
 * Returns: %TRUE if player had enough gold
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_player_combatant_remove_gold (LrgPlayerCombatant *self,
                                  gint                amount);

G_END_DECLS

#endif /* LRG_PLAYER_COMBATANT_H */
