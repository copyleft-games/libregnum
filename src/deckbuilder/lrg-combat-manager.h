/* lrg-combat-manager.h
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef LRG_COMBAT_MANAGER_H
#define LRG_COMBAT_MANAGER_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-combat-context.h"
#include "lrg-card-instance.h"

G_BEGIN_DECLS

#define LRG_TYPE_COMBAT_MANAGER (lrg_combat_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgCombatManager, lrg_combat_manager, LRG, COMBAT_MANAGER, GObject)

/**
 * LrgCombatManagerClass:
 * @parent_class: parent class
 * @on_combat_start: called when combat starts
 * @on_turn_start: called at start of player turn
 * @on_turn_end: called at end of player turn
 * @on_enemy_turn: called during enemy turn
 * @on_combat_end: called when combat ends
 *
 * Class structure for combat managers.
 * Subclass to customize combat flow.
 *
 * Since: 1.0
 */
struct _LrgCombatManagerClass
{
    GObjectClass parent_class;

    /* Virtual methods */
    void (* on_combat_start) (LrgCombatManager *self);
    void (* on_turn_start)   (LrgCombatManager *self);
    void (* on_turn_end)     (LrgCombatManager *self);
    void (* on_enemy_turn)   (LrgCombatManager *self,
                              LrgEnemyInstance *enemy);
    void (* on_combat_end)   (LrgCombatManager *self,
                              LrgCombatResult   result);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_combat_manager_new:
 *
 * Creates a new combat manager.
 *
 * Returns: (transfer full): a new #LrgCombatManager
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCombatManager *
lrg_combat_manager_new (void);

/* Combat lifecycle */

/**
 * lrg_combat_manager_start_combat:
 * @self: a #LrgCombatManager
 * @context: the combat context
 *
 * Starts a new combat with the given context.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_combat_manager_start_combat (LrgCombatManager *self,
                                 LrgCombatContext *context);

/**
 * lrg_combat_manager_end_combat:
 * @self: a #LrgCombatManager
 * @result: the combat result
 *
 * Ends the current combat.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_combat_manager_end_combat (LrgCombatManager *self,
                               LrgCombatResult   result);

/**
 * lrg_combat_manager_get_context:
 * @self: a #LrgCombatManager
 *
 * Gets the current combat context.
 *
 * Returns: (transfer none) (nullable): current context
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCombatContext *
lrg_combat_manager_get_context (LrgCombatManager *self);

/**
 * lrg_combat_manager_is_active:
 * @self: a #LrgCombatManager
 *
 * Checks if combat is currently active.
 *
 * Returns: %TRUE if combat is active
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_combat_manager_is_active (LrgCombatManager *self);

/* Turn flow */

/**
 * lrg_combat_manager_start_player_turn:
 * @self: a #LrgCombatManager
 *
 * Starts the player's turn.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_combat_manager_start_player_turn (LrgCombatManager *self);

/**
 * lrg_combat_manager_end_player_turn:
 * @self: a #LrgCombatManager
 *
 * Ends the player's turn and starts enemy turns.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_combat_manager_end_player_turn (LrgCombatManager *self);

/* Card playing */

/**
 * lrg_combat_manager_play_card:
 * @self: a #LrgCombatManager
 * @card: the card to play
 * @target: (nullable): target combatant
 * @error: (nullable): return location for error
 *
 * Plays a card from hand.
 *
 * Returns: %TRUE if card was played successfully
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_combat_manager_play_card (LrgCombatManager  *self,
                              LrgCardInstance   *card,
                              LrgCombatant      *target,
                              GError           **error);

/**
 * lrg_combat_manager_draw_cards:
 * @self: a #LrgCombatManager
 * @count: number of cards to draw
 *
 * Draws cards from the draw pile to hand.
 *
 * Returns: number of cards actually drawn
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_combat_manager_draw_cards (LrgCombatManager *self,
                               gint              count);

/* Victory/defeat checks */

/**
 * lrg_combat_manager_check_victory:
 * @self: a #LrgCombatManager
 *
 * Checks if all enemies are dead.
 *
 * Returns: %TRUE if player has won
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_combat_manager_check_victory (LrgCombatManager *self);

/**
 * lrg_combat_manager_check_defeat:
 * @self: a #LrgCombatManager
 *
 * Checks if player is dead.
 *
 * Returns: %TRUE if player has lost
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_combat_manager_check_defeat (LrgCombatManager *self);

G_END_DECLS

#endif /* LRG_COMBAT_MANAGER_H */
