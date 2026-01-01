/* lrg-run.h
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef LRG_RUN_H
#define LRG_RUN_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-run-map.h"
#include "lrg-map-node.h"
#include "lrg-deck-instance.h"
#include "lrg-player-combatant.h"
#include "lrg-relic-instance.h"
#include "lrg-potion-instance.h"

G_BEGIN_DECLS

#define LRG_TYPE_RUN (lrg_run_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRun, lrg_run, LRG, RUN, GObject)

/**
 * lrg_run_new:
 * @character_id: identifier for the character class
 * @seed: random seed for the run
 *
 * Creates a new deckbuilder run.
 *
 * Returns: (transfer full): a new #LrgRun
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRun *
lrg_run_new (const gchar *character_id,
             guint64      seed);

/**
 * lrg_run_get_seed:
 * @self: a #LrgRun
 *
 * Gets the random seed for this run.
 *
 * Returns: the seed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_run_get_seed (LrgRun *self);

/**
 * lrg_run_get_character_id:
 * @self: a #LrgRun
 *
 * Gets the character class ID.
 *
 * Returns: (transfer none): the character ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_run_get_character_id (LrgRun *self);

/**
 * lrg_run_get_state:
 * @self: a #LrgRun
 *
 * Gets the current run state.
 *
 * Returns: the run state
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRunState
lrg_run_get_state (LrgRun *self);

/**
 * lrg_run_set_state:
 * @self: a #LrgRun
 * @state: new run state
 *
 * Sets the run state.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_set_state (LrgRun     *self,
                   LrgRunState state);

/**
 * lrg_run_get_player:
 * @self: a #LrgRun
 *
 * Gets the player combatant for this run.
 *
 * Returns: (transfer none): the player
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPlayerCombatant *
lrg_run_get_player (LrgRun *self);

/**
 * lrg_run_set_player:
 * @self: a #LrgRun
 * @player: (transfer full): the player combatant
 *
 * Sets the player combatant.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_set_player (LrgRun             *self,
                    LrgPlayerCombatant *player);

/**
 * lrg_run_get_deck:
 * @self: a #LrgRun
 *
 * Gets the player's master deck.
 *
 * Returns: (transfer none): the deck
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgDeckInstance *
lrg_run_get_deck (LrgRun *self);

/**
 * lrg_run_set_deck:
 * @self: a #LrgRun
 * @deck: (transfer full): the deck
 *
 * Sets the master deck.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_set_deck (LrgRun          *self,
                  LrgDeckInstance *deck);

/* Act and Floor management */

/**
 * lrg_run_get_current_act:
 * @self: a #LrgRun
 *
 * Gets the current act number (1-based).
 *
 * Returns: act number
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_run_get_current_act (LrgRun *self);

/**
 * lrg_run_get_current_floor:
 * @self: a #LrgRun
 *
 * Gets the current floor number within the act.
 *
 * Returns: floor number
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_run_get_current_floor (LrgRun *self);

/**
 * lrg_run_get_map:
 * @self: a #LrgRun
 *
 * Gets the current act's map.
 *
 * Returns: (transfer none) (nullable): the map
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRunMap *
lrg_run_get_map (LrgRun *self);

/**
 * lrg_run_set_map:
 * @self: a #LrgRun
 * @map: (transfer full): the map
 *
 * Sets the current map.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_set_map (LrgRun    *self,
                 LrgRunMap *map);

/**
 * lrg_run_get_current_node:
 * @self: a #LrgRun
 *
 * Gets the current map node.
 *
 * Returns: (transfer none) (nullable): current node
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgMapNode *
lrg_run_get_current_node (LrgRun *self);

/**
 * lrg_run_set_current_node:
 * @self: a #LrgRun
 * @node: (nullable): the node
 *
 * Sets the current map node.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_set_current_node (LrgRun     *self,
                          LrgMapNode *node);

/**
 * lrg_run_advance_act:
 * @self: a #LrgRun
 *
 * Advances to the next act.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_advance_act (LrgRun *self);

/* Relic management */

/**
 * lrg_run_get_relics:
 * @self: a #LrgRun
 *
 * Gets all relics the player has.
 *
 * Returns: (transfer none) (element-type LrgRelicInstance): relics
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_run_get_relics (LrgRun *self);

/**
 * lrg_run_add_relic:
 * @self: a #LrgRun
 * @relic: (transfer full): relic to add
 *
 * Adds a relic to the player's collection.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_add_relic (LrgRun           *self,
                   LrgRelicInstance *relic);

/**
 * lrg_run_has_relic:
 * @self: a #LrgRun
 * @relic_id: the relic definition ID
 *
 * Checks if the player has a specific relic.
 *
 * Returns: %TRUE if the player has the relic
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_run_has_relic (LrgRun      *self,
                   const gchar *relic_id);

/**
 * lrg_run_get_relic:
 * @self: a #LrgRun
 * @relic_id: the relic definition ID
 *
 * Gets a specific relic by its definition ID.
 *
 * Returns: (transfer none) (nullable): the relic, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRelicInstance *
lrg_run_get_relic (LrgRun      *self,
                   const gchar *relic_id);

/* Potion management */

/**
 * lrg_run_get_potions:
 * @self: a #LrgRun
 *
 * Gets all potions the player has.
 *
 * Returns: (transfer none) (element-type LrgPotionInstance): potions
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_run_get_potions (LrgRun *self);

/**
 * lrg_run_get_max_potions:
 * @self: a #LrgRun
 *
 * Gets the maximum number of potions the player can hold.
 *
 * Returns: max potion slots
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_run_get_max_potions (LrgRun *self);

/**
 * lrg_run_set_max_potions:
 * @self: a #LrgRun
 * @max: new maximum
 *
 * Sets the maximum potion slots.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_set_max_potions (LrgRun *self,
                         gint    max);

/**
 * lrg_run_add_potion:
 * @self: a #LrgRun
 * @potion: (transfer full): potion to add
 *
 * Adds a potion if there's room.
 *
 * Returns: %TRUE if added, %FALSE if full
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_run_add_potion (LrgRun            *self,
                    LrgPotionInstance *potion);

/**
 * lrg_run_remove_potion:
 * @self: a #LrgRun
 * @index: potion slot index
 *
 * Removes a potion from a slot (e.g., after use or discard).
 *
 * Returns: %TRUE if removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_run_remove_potion (LrgRun *self,
                       guint   index);

/* Gold management */

/**
 * lrg_run_get_gold:
 * @self: a #LrgRun
 *
 * Gets the player's current gold.
 *
 * Returns: gold amount
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_run_get_gold (LrgRun *self);

/**
 * lrg_run_set_gold:
 * @self: a #LrgRun
 * @gold: new gold amount
 *
 * Sets the player's gold.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_set_gold (LrgRun *self,
                  gint    gold);

/**
 * lrg_run_add_gold:
 * @self: a #LrgRun
 * @amount: gold to add
 *
 * Adds gold to the player's total.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_add_gold (LrgRun *self,
                  gint    amount);

/**
 * lrg_run_spend_gold:
 * @self: a #LrgRun
 * @amount: gold to spend
 *
 * Spends gold if the player has enough.
 *
 * Returns: %TRUE if the purchase was successful
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_run_spend_gold (LrgRun *self,
                    gint    amount);

/* Statistics */

/**
 * lrg_run_get_total_floors_cleared:
 * @self: a #LrgRun
 *
 * Gets the total number of floors cleared across all acts.
 *
 * Returns: floors cleared
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_run_get_total_floors_cleared (LrgRun *self);

/**
 * lrg_run_get_enemies_killed:
 * @self: a #LrgRun
 *
 * Gets the total number of enemies killed.
 *
 * Returns: enemies killed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_run_get_enemies_killed (LrgRun *self);

/**
 * lrg_run_add_enemy_killed:
 * @self: a #LrgRun
 *
 * Increments the enemy kill counter.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_add_enemy_killed (LrgRun *self);

/**
 * lrg_run_get_elapsed_time:
 * @self: a #LrgRun
 *
 * Gets the elapsed run time in seconds.
 *
 * Returns: elapsed time
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_run_get_elapsed_time (LrgRun *self);

/**
 * lrg_run_add_elapsed_time:
 * @self: a #LrgRun
 * @seconds: time to add
 *
 * Adds time to the run timer.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_add_elapsed_time (LrgRun  *self,
                          gdouble  seconds);

G_END_DECLS

#endif /* LRG_RUN_H */
