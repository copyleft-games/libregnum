/* lrg-deckbuilder-template.h - Game template for deckbuilder games
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * LrgDeckbuilderTemplate is a derivable game template specialized for
 * deckbuilder games. It provides automatic integration with the deck
 * management systems, turn structure, and card evaluation hooks.
 *
 * ## Features
 *
 * - **Deck Management**: Draw pile, discard pile, exhaust pile, hand
 * - **Turn Structure**: Start/end turn hooks with energy management
 * - **Card Play**: Play cards from hand with target selection
 * - **Run Management**: Integrate with LrgDeckbuilderManager for runs
 *
 * ## Variants
 *
 * - **LrgDeckbuilderCombatTemplate**: Slay the Spire-style combat
 * - **LrgDeckbuilderPokerTemplate**: Balatro-style poker mechanics
 *
 * Since: 1.0
 */

#ifndef LRG_DECKBUILDER_TEMPLATE_H
#define LRG_DECKBUILDER_TEMPLATE_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-game-template.h"
#include "lrg-deck-mixin.h"

G_BEGIN_DECLS

/* Forward declarations */
typedef struct _LrgDeckInstance  LrgDeckInstance;
typedef struct _LrgDeckDef       LrgDeckDef;
typedef struct _LrgCardInstance  LrgCardInstance;
typedef struct _LrgCardDef       LrgCardDef;

#define LRG_TYPE_DECKBUILDER_TEMPLATE (lrg_deckbuilder_template_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgDeckbuilderTemplate, lrg_deckbuilder_template,
                          LRG, DECKBUILDER_TEMPLATE, LrgGameTemplate)

/**
 * LrgDeckbuilderTemplateClass:
 * @parent_class: the parent class
 * @create_deck_def: Creates the default deck definition
 * @create_deck_instance: Creates the deck instance from a definition
 * @on_card_played: Called when a card is played
 * @evaluate_card_cost: Calculate the cost to play a card
 * @can_play_card: Check if a card can be played
 * @start_turn: Called at the start of each turn
 * @end_turn: Called at the end of each turn
 * @get_starting_energy: Get energy at turn start
 * @get_cards_to_draw: Get number of cards to draw at turn start
 *
 * The virtual function table for #LrgDeckbuilderTemplate.
 *
 * Since: 1.0
 */
struct _LrgDeckbuilderTemplateClass
{
    LrgGameTemplateClass parent_class;

    /*< public >*/

    /**
     * LrgDeckbuilderTemplateClass::create_deck_def:
     * @self: an #LrgDeckbuilderTemplate
     *
     * Creates the default deck definition for new games.
     * Override to provide a custom starting deck.
     *
     * Returns: (transfer full): a new #LrgDeckDef
     */
    LrgDeckDef * (*create_deck_def) (LrgDeckbuilderTemplate *self);

    /**
     * LrgDeckbuilderTemplateClass::create_deck_instance:
     * @self: an #LrgDeckbuilderTemplate
     * @def: (transfer none): the deck definition
     *
     * Creates a deck instance from a definition.
     *
     * Returns: (transfer full): a new #LrgDeckInstance
     */
    LrgDeckInstance * (*create_deck_instance) (LrgDeckbuilderTemplate *self,
                                                LrgDeckDef             *def);

    /**
     * LrgDeckbuilderTemplateClass::on_card_played:
     * @self: an #LrgDeckbuilderTemplate
     * @card: (transfer none): the card being played
     * @target: (nullable): optional target for the card
     *
     * Called when a card is played. Override to implement card effects.
     *
     * Returns: %TRUE if the card was successfully played
     */
    gboolean (*on_card_played) (LrgDeckbuilderTemplate *self,
                                 LrgCardInstance        *card,
                                 gpointer                target);

    /**
     * LrgDeckbuilderTemplateClass::evaluate_card_cost:
     * @self: an #LrgDeckbuilderTemplate
     * @card: (transfer none): the card to evaluate
     *
     * Calculates the effective cost to play a card.
     * Override to implement cost modifications.
     *
     * Returns: the effective energy cost
     */
    gint (*evaluate_card_cost) (LrgDeckbuilderTemplate *self,
                                LrgCardInstance        *card);

    /**
     * LrgDeckbuilderTemplateClass::can_play_card:
     * @self: an #LrgDeckbuilderTemplate
     * @card: (transfer none): the card to check
     * @target: (nullable): optional target
     *
     * Checks if a card can be played.
     *
     * Returns: %TRUE if the card can be played
     */
    gboolean (*can_play_card) (LrgDeckbuilderTemplate *self,
                               LrgCardInstance        *card,
                               gpointer                target);

    /**
     * LrgDeckbuilderTemplateClass::start_turn:
     * @self: an #LrgDeckbuilderTemplate
     * @turn_number: the turn number (1-indexed)
     *
     * Called at the start of each turn. Default implementation
     * draws cards and resets energy.
     */
    void (*start_turn) (LrgDeckbuilderTemplate *self,
                        guint                   turn_number);

    /**
     * LrgDeckbuilderTemplateClass::end_turn:
     * @self: an #LrgDeckbuilderTemplate
     * @turn_number: the turn that just ended
     *
     * Called at the end of each turn. Default implementation
     * discards hand.
     */
    void (*end_turn) (LrgDeckbuilderTemplate *self,
                      guint                   turn_number);

    /**
     * LrgDeckbuilderTemplateClass::get_starting_energy:
     * @self: an #LrgDeckbuilderTemplate
     *
     * Gets the energy to restore at turn start.
     *
     * Returns: starting energy
     */
    gint (*get_starting_energy) (LrgDeckbuilderTemplate *self);

    /**
     * LrgDeckbuilderTemplateClass::get_cards_to_draw:
     * @self: an #LrgDeckbuilderTemplate
     *
     * Gets the number of cards to draw at turn start.
     *
     * Returns: cards to draw
     */
    guint (*get_cards_to_draw) (LrgDeckbuilderTemplate *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_deckbuilder_template_new:
 *
 * Creates a new deckbuilder template with default settings.
 *
 * Returns: (transfer full): a new #LrgDeckbuilderTemplate
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgDeckbuilderTemplate *
lrg_deckbuilder_template_new (void);

/* ==========================================================================
 * Deck Access
 * ========================================================================== */

/**
 * lrg_deckbuilder_template_get_deck_instance:
 * @self: an #LrgDeckbuilderTemplate
 *
 * Gets the current deck instance.
 *
 * Returns: (transfer none): the #LrgDeckInstance
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgDeckInstance *
lrg_deckbuilder_template_get_deck_instance (LrgDeckbuilderTemplate *self);

/**
 * lrg_deckbuilder_template_set_deck_instance:
 * @self: an #LrgDeckbuilderTemplate
 * @deck: (transfer none): the deck to use
 *
 * Sets the current deck instance.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_template_set_deck_instance (LrgDeckbuilderTemplate *self,
                                            LrgDeckInstance        *deck);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_deckbuilder_template_get_current_energy:
 * @self: an #LrgDeckbuilderTemplate
 *
 * Gets the current energy available.
 *
 * Returns: current energy
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_deckbuilder_template_get_current_energy (LrgDeckbuilderTemplate *self);

/**
 * lrg_deckbuilder_template_set_current_energy:
 * @self: an #LrgDeckbuilderTemplate
 * @energy: energy value
 *
 * Sets the current energy.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_template_set_current_energy (LrgDeckbuilderTemplate *self,
                                             gint                    energy);

/**
 * lrg_deckbuilder_template_get_max_energy:
 * @self: an #LrgDeckbuilderTemplate
 *
 * Gets the maximum energy.
 *
 * Returns: maximum energy
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_deckbuilder_template_get_max_energy (LrgDeckbuilderTemplate *self);

/**
 * lrg_deckbuilder_template_set_max_energy:
 * @self: an #LrgDeckbuilderTemplate
 * @energy: max energy value
 *
 * Sets the maximum energy.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_template_set_max_energy (LrgDeckbuilderTemplate *self,
                                         gint                    energy);

/**
 * lrg_deckbuilder_template_get_current_turn:
 * @self: an #LrgDeckbuilderTemplate
 *
 * Gets the current turn number.
 *
 * Returns: current turn (1-indexed)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_deckbuilder_template_get_current_turn (LrgDeckbuilderTemplate *self);

/**
 * lrg_deckbuilder_template_get_base_hand_size:
 * @self: an #LrgDeckbuilderTemplate
 *
 * Gets the base hand size (cards drawn per turn).
 *
 * Returns: base hand size
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_deckbuilder_template_get_base_hand_size (LrgDeckbuilderTemplate *self);

/**
 * lrg_deckbuilder_template_set_base_hand_size:
 * @self: an #LrgDeckbuilderTemplate
 * @size: new base hand size
 *
 * Sets the base hand size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_template_set_base_hand_size (LrgDeckbuilderTemplate *self,
                                             guint                   size);

/* ==========================================================================
 * Turn Management
 * ========================================================================== */

/**
 * lrg_deckbuilder_template_start_turn:
 * @self: an #LrgDeckbuilderTemplate
 *
 * Starts a new turn. Increments turn counter, draws cards, resets energy.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_template_start_turn (LrgDeckbuilderTemplate *self);

/**
 * lrg_deckbuilder_template_end_turn:
 * @self: an #LrgDeckbuilderTemplate
 *
 * Ends the current turn. Discards hand, triggers end-turn effects.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_template_end_turn (LrgDeckbuilderTemplate *self);

/**
 * lrg_deckbuilder_template_is_player_turn:
 * @self: an #LrgDeckbuilderTemplate
 *
 * Checks if it's the player's turn.
 *
 * Returns: %TRUE if player can act
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_deckbuilder_template_is_player_turn (LrgDeckbuilderTemplate *self);

/* ==========================================================================
 * Card Operations
 * ========================================================================== */

/**
 * lrg_deckbuilder_template_play_card:
 * @self: an #LrgDeckbuilderTemplate
 * @card: (transfer none): the card to play
 * @target: (nullable): optional target
 *
 * Plays a card from hand. Checks cost, executes effect, moves to discard.
 *
 * Returns: %TRUE if the card was played
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_deckbuilder_template_play_card (LrgDeckbuilderTemplate *self,
                                    LrgCardInstance        *card,
                                    gpointer                target);

/**
 * lrg_deckbuilder_template_play_card_at:
 * @self: an #LrgDeckbuilderTemplate
 * @hand_index: index in hand
 * @target: (nullable): optional target
 *
 * Plays the card at a specific hand index.
 *
 * Returns: %TRUE if the card was played
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_deckbuilder_template_play_card_at (LrgDeckbuilderTemplate *self,
                                       guint                   hand_index,
                                       gpointer                target);

/**
 * lrg_deckbuilder_template_can_play_card:
 * @self: an #LrgDeckbuilderTemplate
 * @card: (transfer none): the card to check
 *
 * Checks if a card can be played (enough energy, valid target, etc.).
 *
 * Returns: %TRUE if the card can be played
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_deckbuilder_template_can_play_card (LrgDeckbuilderTemplate *self,
                                        LrgCardInstance        *card);

/**
 * lrg_deckbuilder_template_get_card_cost:
 * @self: an #LrgDeckbuilderTemplate
 * @card: (transfer none): the card to evaluate
 *
 * Gets the effective cost to play a card.
 *
 * Returns: energy cost
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_deckbuilder_template_get_card_cost (LrgDeckbuilderTemplate *self,
                                        LrgCardInstance        *card);

/**
 * lrg_deckbuilder_template_draw_cards:
 * @self: an #LrgDeckbuilderTemplate
 * @count: number of cards to draw
 *
 * Draws cards from the deck to hand.
 *
 * Returns: actual number of cards drawn
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_deckbuilder_template_draw_cards (LrgDeckbuilderTemplate *self,
                                     guint                   count);

/**
 * lrg_deckbuilder_template_add_card_to_deck:
 * @self: an #LrgDeckbuilderTemplate
 * @card_def: (transfer none): card definition to add
 *
 * Adds a new card to the deck (master deck).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_template_add_card_to_deck (LrgDeckbuilderTemplate *self,
                                           LrgCardDef             *card_def);

/**
 * lrg_deckbuilder_template_remove_card_from_deck:
 * @self: an #LrgDeckbuilderTemplate
 * @card: (transfer none): card instance to remove
 *
 * Removes a card from the deck permanently.
 *
 * Returns: %TRUE if the card was removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_deckbuilder_template_remove_card_from_deck (LrgDeckbuilderTemplate *self,
                                                LrgCardInstance        *card);

/* ==========================================================================
 * Energy Operations
 * ========================================================================== */

/**
 * lrg_deckbuilder_template_spend_energy:
 * @self: an #LrgDeckbuilderTemplate
 * @amount: energy to spend
 *
 * Spends energy.
 *
 * Returns: %TRUE if enough energy was available
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_deckbuilder_template_spend_energy (LrgDeckbuilderTemplate *self,
                                       gint                    amount);

/**
 * lrg_deckbuilder_template_gain_energy:
 * @self: an #LrgDeckbuilderTemplate
 * @amount: energy to gain
 *
 * Gains energy.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_template_gain_energy (LrgDeckbuilderTemplate *self,
                                      gint                    amount);

/**
 * lrg_deckbuilder_template_reset_energy:
 * @self: an #LrgDeckbuilderTemplate
 *
 * Resets energy to max (or starting) value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_template_reset_energy (LrgDeckbuilderTemplate *self);

G_END_DECLS

#endif /* LRG_DECKBUILDER_TEMPLATE_H */
