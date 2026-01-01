/* lrg-combat-context.h
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef LRG_COMBAT_CONTEXT_H
#define LRG_COMBAT_CONTEXT_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-combatant.h"
#include "lrg-combat-rules.h"
#include "lrg-player-combatant.h"
#include "lrg-enemy-instance.h"
#include "lrg-card-pile.h"
#include "lrg-hand.h"

G_BEGIN_DECLS

#define LRG_TYPE_COMBAT_CONTEXT (lrg_combat_context_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCombatContext, lrg_combat_context, LRG, COMBAT_CONTEXT, GObject)

/**
 * lrg_combat_context_new:
 * @player: the player combatant
 * @rules: (nullable): combat rules (uses defaults if NULL)
 *
 * Creates a new combat context.
 *
 * Returns: (transfer full): a new #LrgCombatContext
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCombatContext *
lrg_combat_context_new (LrgPlayerCombatant *player,
                        LrgCombatRules     *rules);

/* Combat state */

LRG_AVAILABLE_IN_ALL
LrgCombatPhase
lrg_combat_context_get_phase (LrgCombatContext *self);

LRG_AVAILABLE_IN_ALL
void
lrg_combat_context_set_phase (LrgCombatContext *self,
                              LrgCombatPhase    phase);

LRG_AVAILABLE_IN_ALL
gint
lrg_combat_context_get_turn (LrgCombatContext *self);

LRG_AVAILABLE_IN_ALL
void
lrg_combat_context_increment_turn (LrgCombatContext *self);

/* Participants */

/**
 * lrg_combat_context_get_player:
 * @self: a #LrgCombatContext
 *
 * Gets the player combatant.
 *
 * Returns: (transfer none): the player
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPlayerCombatant *
lrg_combat_context_get_player (LrgCombatContext *self);

/**
 * lrg_combat_context_add_enemy:
 * @self: a #LrgCombatContext
 * @enemy: (transfer full): the enemy to add
 *
 * Adds an enemy to the combat.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_combat_context_add_enemy (LrgCombatContext *self,
                              LrgEnemyInstance *enemy);

/**
 * lrg_combat_context_remove_enemy:
 * @self: a #LrgCombatContext
 * @enemy: the enemy to remove
 *
 * Removes an enemy from combat.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_combat_context_remove_enemy (LrgCombatContext *self,
                                 LrgEnemyInstance *enemy);

/**
 * lrg_combat_context_get_enemies:
 * @self: a #LrgCombatContext
 *
 * Gets all enemies in combat.
 *
 * Returns: (transfer none) (element-type LrgEnemyInstance): enemy list
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_combat_context_get_enemies (LrgCombatContext *self);

LRG_AVAILABLE_IN_ALL
guint
lrg_combat_context_get_enemy_count (LrgCombatContext *self);

/**
 * lrg_combat_context_get_enemy_at:
 * @self: a #LrgCombatContext
 * @index: enemy index
 *
 * Gets an enemy by index.
 *
 * Returns: (transfer none) (nullable): the enemy or NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEnemyInstance *
lrg_combat_context_get_enemy_at (LrgCombatContext *self,
                                 guint             index);

/* Card piles */

/**
 * lrg_combat_context_get_draw_pile:
 * @self: a #LrgCombatContext
 *
 * Gets the draw pile.
 *
 * Returns: (transfer none): the draw pile
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardPile *
lrg_combat_context_get_draw_pile (LrgCombatContext *self);

/**
 * lrg_combat_context_get_discard_pile:
 * @self: a #LrgCombatContext
 *
 * Gets the discard pile.
 *
 * Returns: (transfer none): the discard pile
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardPile *
lrg_combat_context_get_discard_pile (LrgCombatContext *self);

/**
 * lrg_combat_context_get_exhaust_pile:
 * @self: a #LrgCombatContext
 *
 * Gets the exhaust pile.
 *
 * Returns: (transfer none): the exhaust pile
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardPile *
lrg_combat_context_get_exhaust_pile (LrgCombatContext *self);

/**
 * lrg_combat_context_get_hand:
 * @self: a #LrgCombatContext
 *
 * Gets the player's hand.
 *
 * Returns: (transfer none): the hand
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgHand *
lrg_combat_context_get_hand (LrgCombatContext *self);

/* Energy */

LRG_AVAILABLE_IN_ALL
gint
lrg_combat_context_get_energy (LrgCombatContext *self);

LRG_AVAILABLE_IN_ALL
void
lrg_combat_context_set_energy (LrgCombatContext *self,
                               gint              energy);

LRG_AVAILABLE_IN_ALL
gboolean
lrg_combat_context_spend_energy (LrgCombatContext *self,
                                 gint              amount);

LRG_AVAILABLE_IN_ALL
void
lrg_combat_context_add_energy (LrgCombatContext *self,
                               gint              amount);

/* Turn tracking */

LRG_AVAILABLE_IN_ALL
gint
lrg_combat_context_get_cards_played_this_turn (LrgCombatContext *self);

LRG_AVAILABLE_IN_ALL
void
lrg_combat_context_increment_cards_played (LrgCombatContext *self);

LRG_AVAILABLE_IN_ALL
void
lrg_combat_context_reset_turn_counters (LrgCombatContext *self);

/* Combat rules */

/**
 * lrg_combat_context_get_rules:
 * @self: a #LrgCombatContext
 *
 * Gets the combat rules.
 *
 * Returns: (transfer none) (nullable): the rules
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCombatRules *
lrg_combat_context_get_rules (LrgCombatContext *self);

/* Variables (for X-cost, etc.) */

LRG_AVAILABLE_IN_ALL
void
lrg_combat_context_set_variable (LrgCombatContext *self,
                                 const gchar      *name,
                                 gint              value);

LRG_AVAILABLE_IN_ALL
gint
lrg_combat_context_get_variable (LrgCombatContext *self,
                                 const gchar      *name);

/* RNG */

/**
 * lrg_combat_context_get_rng:
 * @self: a #LrgCombatContext
 *
 * Gets the seeded random number generator.
 *
 * Returns: (transfer none): the RNG
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GRand *
lrg_combat_context_get_rng (LrgCombatContext *self);

LRG_AVAILABLE_IN_ALL
void
lrg_combat_context_set_seed (LrgCombatContext *self,
                             guint32           seed);

G_END_DECLS

#endif /* LRG_COMBAT_CONTEXT_H */
