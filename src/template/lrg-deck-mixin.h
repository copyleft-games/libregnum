/* lrg-deck-mixin.h - Interface for deckbuilder game mechanics
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * LrgDeckMixin is a composable interface that provides deckbuilder game
 * mechanics. It integrates with the existing LrgDeckInstance system to
 * provide deck, hand, and discard pile management with event hooks.
 *
 * ## Features
 *
 * - **Deck Management**: Draw pile, discard pile, exhaust pile, hand
 * - **Card Operations**: Draw, play, discard, shuffle
 * - **Event Hooks**: Callbacks for card drawn, played, discarded, etc.
 * - **Turn Structure**: Hook points for turn-based gameplay
 *
 * ## Usage
 *
 * Implement this interface on your game state or template class:
 *
 * ```c
 * G_DEFINE_TYPE_WITH_CODE (MyCardGame, my_card_game, LRG_TYPE_GAME_TEMPLATE,
 *     G_IMPLEMENT_INTERFACE (LRG_TYPE_DECK_MIXIN, my_card_game_deck_mixin_init))
 * ```
 *
 * Since: 1.0
 */

#ifndef LRG_DECK_MIXIN_H
#define LRG_DECK_MIXIN_H

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/* Forward declarations */
typedef struct _LrgDeckInstance LrgDeckInstance;
typedef struct _LrgCardInstance LrgCardInstance;
typedef struct _LrgCardPile     LrgCardPile;
typedef struct _LrgHand         LrgHand;

#define LRG_TYPE_DECK_MIXIN (lrg_deck_mixin_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgDeckMixin, lrg_deck_mixin, LRG, DECK_MIXIN, GObject)

/**
 * LrgDeckMixinInterface:
 * @parent_iface: the parent interface
 * @get_deck_instance: Returns the deck instance
 * @get_hand_size: Returns the maximum hand size
 * @on_card_drawn: Hook called when a card is drawn
 * @on_card_played: Hook called when a card is played
 * @on_card_discarded: Hook called when a card is discarded
 * @on_deck_shuffled: Hook called when the deck is shuffled
 *
 * Interface for deckbuilder game mechanics. Implement this interface
 * to add deck management to your game template or state.
 *
 * Since: 1.0
 */
struct _LrgDeckMixinInterface
{
    GTypeInterface parent_iface;

    /*< public >*/

    /**
     * LrgDeckMixinInterface::get_deck_instance:
     * @self: an #LrgDeckMixin
     *
     * Gets the deck instance that manages cards.
     *
     * Returns: (transfer none): the #LrgDeckInstance
     */
    LrgDeckInstance * (*get_deck_instance) (LrgDeckMixin *self);

    /**
     * LrgDeckMixinInterface::get_hand_size:
     * @self: an #LrgDeckMixin
     *
     * Gets the maximum number of cards in hand.
     *
     * Returns: maximum hand size
     */
    guint (*get_hand_size) (LrgDeckMixin *self);

    /**
     * LrgDeckMixinInterface::on_card_drawn:
     * @self: an #LrgDeckMixin
     * @card: (transfer none): the card that was drawn
     *
     * Hook called when a card is drawn from the draw pile.
     */
    void (*on_card_drawn) (LrgDeckMixin    *self,
                           LrgCardInstance *card);

    /**
     * LrgDeckMixinInterface::on_card_played:
     * @self: an #LrgDeckMixin
     * @card: (transfer none): the card that was played
     * @target: (transfer none) (nullable): optional target
     *
     * Hook called when a card is played from hand.
     */
    void (*on_card_played) (LrgDeckMixin    *self,
                            LrgCardInstance *card,
                            gpointer         target);

    /**
     * LrgDeckMixinInterface::on_card_discarded:
     * @self: an #LrgDeckMixin
     * @card: (transfer none): the card that was discarded
     *
     * Hook called when a card is discarded.
     */
    void (*on_card_discarded) (LrgDeckMixin    *self,
                               LrgCardInstance *card);

    /**
     * LrgDeckMixinInterface::on_deck_shuffled:
     * @self: an #LrgDeckMixin
     *
     * Hook called when the deck is shuffled.
     */
    void (*on_deck_shuffled) (LrgDeckMixin *self);

    /**
     * LrgDeckMixinInterface::on_card_exhausted:
     * @self: an #LrgDeckMixin
     * @card: (transfer none): the card that was exhausted
     *
     * Hook called when a card is exhausted (removed from play).
     */
    void (*on_card_exhausted) (LrgDeckMixin    *self,
                               LrgCardInstance *card);

    /**
     * LrgDeckMixinInterface::on_turn_started:
     * @self: an #LrgDeckMixin
     * @turn_number: the current turn number
     *
     * Hook called at the start of a new turn.
     */
    void (*on_turn_started) (LrgDeckMixin *self,
                             guint         turn_number);

    /**
     * LrgDeckMixinInterface::on_turn_ended:
     * @self: an #LrgDeckMixin
     * @turn_number: the turn that just ended
     *
     * Hook called at the end of a turn.
     */
    void (*on_turn_ended) (LrgDeckMixin *self,
                           guint         turn_number);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Interface Methods
 * ========================================================================== */

/**
 * lrg_deck_mixin_get_deck_instance:
 * @self: an #LrgDeckMixin
 *
 * Gets the deck instance that manages draw pile, discard, hand, etc.
 *
 * Returns: (transfer none): the #LrgDeckInstance
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgDeckInstance *
lrg_deck_mixin_get_deck_instance (LrgDeckMixin *self);

/**
 * lrg_deck_mixin_get_hand_size:
 * @self: an #LrgDeckMixin
 *
 * Gets the maximum number of cards that can be held in hand.
 *
 * Returns: maximum hand size
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_deck_mixin_get_hand_size (LrgDeckMixin *self);

/**
 * lrg_deck_mixin_on_card_drawn:
 * @self: an #LrgDeckMixin
 * @card: (transfer none): the card that was drawn
 *
 * Called when a card is drawn from the draw pile to hand.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deck_mixin_on_card_drawn (LrgDeckMixin    *self,
                              LrgCardInstance *card);

/**
 * lrg_deck_mixin_on_card_played:
 * @self: an #LrgDeckMixin
 * @card: (transfer none): the card that was played
 * @target: (transfer none) (nullable): optional target
 *
 * Called when a card is played from hand.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deck_mixin_on_card_played (LrgDeckMixin    *self,
                               LrgCardInstance *card,
                               gpointer         target);

/**
 * lrg_deck_mixin_on_card_discarded:
 * @self: an #LrgDeckMixin
 * @card: (transfer none): the card that was discarded
 *
 * Called when a card is discarded from hand.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deck_mixin_on_card_discarded (LrgDeckMixin    *self,
                                  LrgCardInstance *card);

/**
 * lrg_deck_mixin_on_deck_shuffled:
 * @self: an #LrgDeckMixin
 *
 * Called when the deck is shuffled.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deck_mixin_on_deck_shuffled (LrgDeckMixin *self);

/**
 * lrg_deck_mixin_on_card_exhausted:
 * @self: an #LrgDeckMixin
 * @card: (transfer none): the card that was exhausted
 *
 * Called when a card is exhausted.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deck_mixin_on_card_exhausted (LrgDeckMixin    *self,
                                  LrgCardInstance *card);

/**
 * lrg_deck_mixin_on_turn_started:
 * @self: an #LrgDeckMixin
 * @turn_number: the current turn number
 *
 * Called at the start of a new turn.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deck_mixin_on_turn_started (LrgDeckMixin *self,
                                guint         turn_number);

/**
 * lrg_deck_mixin_on_turn_ended:
 * @self: an #LrgDeckMixin
 * @turn_number: the turn that just ended
 *
 * Called at the end of a turn.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deck_mixin_on_turn_ended (LrgDeckMixin *self,
                              guint         turn_number);

/* ==========================================================================
 * Helper Methods
 * ========================================================================== */

/**
 * lrg_deck_mixin_draw_card:
 * @self: an #LrgDeckMixin
 *
 * Draws a single card from the draw pile to hand.
 * If the draw pile is empty, shuffles the discard pile first.
 *
 * Returns: (transfer none) (nullable): the drawn card, or %NULL if no cards
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardInstance *
lrg_deck_mixin_draw_card (LrgDeckMixin *self);

/**
 * lrg_deck_mixin_draw_cards:
 * @self: an #LrgDeckMixin
 * @count: number of cards to draw
 *
 * Draws multiple cards from the draw pile to hand.
 *
 * Returns: actual number of cards drawn
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_deck_mixin_draw_cards (LrgDeckMixin *self,
                           guint         count);

/**
 * lrg_deck_mixin_discard_hand:
 * @self: an #LrgDeckMixin
 *
 * Discards all cards in hand to the discard pile.
 * Calls on_card_discarded for each card.
 *
 * Returns: number of cards discarded
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_deck_mixin_discard_hand (LrgDeckMixin *self);

/**
 * lrg_deck_mixin_shuffle_discard_into_deck:
 * @self: an #LrgDeckMixin
 *
 * Shuffles the discard pile into the draw pile.
 * Calls on_deck_shuffled after shuffling.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deck_mixin_shuffle_discard_into_deck (LrgDeckMixin *self);

/**
 * lrg_deck_mixin_get_draw_pile:
 * @self: an #LrgDeckMixin
 *
 * Gets the draw pile.
 *
 * Returns: (transfer none): the draw pile
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardPile *
lrg_deck_mixin_get_draw_pile (LrgDeckMixin *self);

/**
 * lrg_deck_mixin_get_discard_pile:
 * @self: an #LrgDeckMixin
 *
 * Gets the discard pile.
 *
 * Returns: (transfer none): the discard pile
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardPile *
lrg_deck_mixin_get_discard_pile (LrgDeckMixin *self);

/**
 * lrg_deck_mixin_get_exhaust_pile:
 * @self: an #LrgDeckMixin
 *
 * Gets the exhaust pile.
 *
 * Returns: (transfer none): the exhaust pile
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardPile *
lrg_deck_mixin_get_exhaust_pile (LrgDeckMixin *self);

/**
 * lrg_deck_mixin_get_hand:
 * @self: an #LrgDeckMixin
 *
 * Gets the current hand.
 *
 * Returns: (transfer none): the hand
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgHand *
lrg_deck_mixin_get_hand (LrgDeckMixin *self);

/**
 * lrg_deck_mixin_get_draw_pile_count:
 * @self: an #LrgDeckMixin
 *
 * Gets the number of cards in the draw pile.
 *
 * Returns: number of cards in draw pile
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_deck_mixin_get_draw_pile_count (LrgDeckMixin *self);

/**
 * lrg_deck_mixin_get_discard_pile_count:
 * @self: an #LrgDeckMixin
 *
 * Gets the number of cards in the discard pile.
 *
 * Returns: number of cards in discard pile
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_deck_mixin_get_discard_pile_count (LrgDeckMixin *self);

/**
 * lrg_deck_mixin_get_hand_count:
 * @self: an #LrgDeckMixin
 *
 * Gets the number of cards in hand.
 *
 * Returns: number of cards in hand
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_deck_mixin_get_hand_count (LrgDeckMixin *self);

/**
 * lrg_deck_mixin_setup_deck:
 * @self: an #LrgDeckMixin
 *
 * Sets up the deck at the start of combat/round.
 * Copies master deck to draw pile and shuffles.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deck_mixin_setup_deck (LrgDeckMixin *self);

/**
 * lrg_deck_mixin_end_combat:
 * @self: an #LrgDeckMixin
 *
 * Cleans up at the end of combat/round.
 * Moves all cards back to master deck.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deck_mixin_end_combat (LrgDeckMixin *self);

G_END_DECLS

#endif /* LRG_DECK_MIXIN_H */
