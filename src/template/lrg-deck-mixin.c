/* lrg-deck-mixin.c - Interface for deckbuilder game mechanics
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"

#include "lrg-deck-mixin.h"
#include "../deckbuilder/lrg-deck-instance.h"
#include "../deckbuilder/lrg-card-pile.h"
#include "../deckbuilder/lrg-hand.h"
#include "../deckbuilder/lrg-card-instance.h"

/**
 * SECTION:lrg-deck-mixin
 * @title: LrgDeckMixin
 * @short_description: Interface for deckbuilder game mechanics
 * @see_also: #LrgDeckInstance, #LrgCardPile, #LrgHand
 *
 * #LrgDeckMixin is a composable interface that provides deckbuilder
 * game mechanics. It integrates with the existing deck management
 * systems to provide draw, play, discard, and shuffle operations.
 *
 * ## Implementing the Interface
 *
 * To add deck mechanics to your game, implement this interface:
 *
 * |[<!-- language="C" -->
 * static LrgDeckInstance *
 * my_game_get_deck_instance (LrgDeckMixin *mixin)
 * {
 *     MyGame *self = MY_GAME (mixin);
 *     return self->deck;
 * }
 *
 * static void
 * my_game_deck_mixin_init (LrgDeckMixinInterface *iface)
 * {
 *     iface->get_deck_instance = my_game_get_deck_instance;
 *     // ... implement other methods
 * }
 *
 * G_DEFINE_TYPE_WITH_CODE (MyGame, my_game, LRG_TYPE_GAME_TEMPLATE,
 *     G_IMPLEMENT_INTERFACE (LRG_TYPE_DECK_MIXIN, my_game_deck_mixin_init))
 * ]|
 *
 * Since: 1.0
 */

G_DEFINE_INTERFACE (LrgDeckMixin, lrg_deck_mixin, G_TYPE_OBJECT)

/* Default implementations */

static LrgDeckInstance *
lrg_deck_mixin_real_get_deck_instance (LrgDeckMixin *self)
{
    (void)self;
    return NULL;
}

static guint
lrg_deck_mixin_real_get_hand_size (LrgDeckMixin *self)
{
    (void)self;
    return LRG_HAND_DEFAULT_MAX_SIZE;
}

static void
lrg_deck_mixin_real_on_card_drawn (LrgDeckMixin    *self,
                                    LrgCardInstance *card)
{
    (void)self;
    (void)card;
}

static void
lrg_deck_mixin_real_on_card_played (LrgDeckMixin    *self,
                                     LrgCardInstance *card,
                                     gpointer         target)
{
    (void)self;
    (void)card;
    (void)target;
}

static void
lrg_deck_mixin_real_on_card_discarded (LrgDeckMixin    *self,
                                        LrgCardInstance *card)
{
    (void)self;
    (void)card;
}

static void
lrg_deck_mixin_real_on_deck_shuffled (LrgDeckMixin *self)
{
    (void)self;
}

static void
lrg_deck_mixin_real_on_card_exhausted (LrgDeckMixin    *self,
                                        LrgCardInstance *card)
{
    (void)self;
    (void)card;
}

static void
lrg_deck_mixin_real_on_turn_started (LrgDeckMixin *self,
                                      guint         turn_number)
{
    (void)self;
    (void)turn_number;
}

static void
lrg_deck_mixin_real_on_turn_ended (LrgDeckMixin *self,
                                    guint         turn_number)
{
    (void)self;
    (void)turn_number;
}

static void
lrg_deck_mixin_default_init (LrgDeckMixinInterface *iface)
{
    iface->get_deck_instance = lrg_deck_mixin_real_get_deck_instance;
    iface->get_hand_size = lrg_deck_mixin_real_get_hand_size;
    iface->on_card_drawn = lrg_deck_mixin_real_on_card_drawn;
    iface->on_card_played = lrg_deck_mixin_real_on_card_played;
    iface->on_card_discarded = lrg_deck_mixin_real_on_card_discarded;
    iface->on_deck_shuffled = lrg_deck_mixin_real_on_deck_shuffled;
    iface->on_card_exhausted = lrg_deck_mixin_real_on_card_exhausted;
    iface->on_turn_started = lrg_deck_mixin_real_on_turn_started;
    iface->on_turn_ended = lrg_deck_mixin_real_on_turn_ended;
}

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
LrgDeckInstance *
lrg_deck_mixin_get_deck_instance (LrgDeckMixin *self)
{
    LrgDeckMixinInterface *iface;

    g_return_val_if_fail (LRG_IS_DECK_MIXIN (self), NULL);

    iface = LRG_DECK_MIXIN_GET_IFACE (self);
    if (iface->get_deck_instance != NULL)
        return iface->get_deck_instance (self);

    return NULL;
}

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
guint
lrg_deck_mixin_get_hand_size (LrgDeckMixin *self)
{
    LrgDeckMixinInterface *iface;

    g_return_val_if_fail (LRG_IS_DECK_MIXIN (self), LRG_HAND_DEFAULT_MAX_SIZE);

    iface = LRG_DECK_MIXIN_GET_IFACE (self);
    if (iface->get_hand_size != NULL)
        return iface->get_hand_size (self);

    return LRG_HAND_DEFAULT_MAX_SIZE;
}

/**
 * lrg_deck_mixin_on_card_drawn:
 * @self: an #LrgDeckMixin
 * @card: (transfer none): the card that was drawn
 *
 * Called when a card is drawn from the draw pile to hand.
 *
 * Since: 1.0
 */
void
lrg_deck_mixin_on_card_drawn (LrgDeckMixin    *self,
                              LrgCardInstance *card)
{
    LrgDeckMixinInterface *iface;

    g_return_if_fail (LRG_IS_DECK_MIXIN (self));
    g_return_if_fail (card != NULL);

    iface = LRG_DECK_MIXIN_GET_IFACE (self);
    if (iface->on_card_drawn != NULL)
        iface->on_card_drawn (self, card);
}

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
void
lrg_deck_mixin_on_card_played (LrgDeckMixin    *self,
                               LrgCardInstance *card,
                               gpointer         target)
{
    LrgDeckMixinInterface *iface;

    g_return_if_fail (LRG_IS_DECK_MIXIN (self));
    g_return_if_fail (card != NULL);

    iface = LRG_DECK_MIXIN_GET_IFACE (self);
    if (iface->on_card_played != NULL)
        iface->on_card_played (self, card, target);
}

/**
 * lrg_deck_mixin_on_card_discarded:
 * @self: an #LrgDeckMixin
 * @card: (transfer none): the card that was discarded
 *
 * Called when a card is discarded from hand.
 *
 * Since: 1.0
 */
void
lrg_deck_mixin_on_card_discarded (LrgDeckMixin    *self,
                                  LrgCardInstance *card)
{
    LrgDeckMixinInterface *iface;

    g_return_if_fail (LRG_IS_DECK_MIXIN (self));
    g_return_if_fail (card != NULL);

    iface = LRG_DECK_MIXIN_GET_IFACE (self);
    if (iface->on_card_discarded != NULL)
        iface->on_card_discarded (self, card);
}

/**
 * lrg_deck_mixin_on_deck_shuffled:
 * @self: an #LrgDeckMixin
 *
 * Called when the deck is shuffled.
 *
 * Since: 1.0
 */
void
lrg_deck_mixin_on_deck_shuffled (LrgDeckMixin *self)
{
    LrgDeckMixinInterface *iface;

    g_return_if_fail (LRG_IS_DECK_MIXIN (self));

    iface = LRG_DECK_MIXIN_GET_IFACE (self);
    if (iface->on_deck_shuffled != NULL)
        iface->on_deck_shuffled (self);
}

/**
 * lrg_deck_mixin_on_card_exhausted:
 * @self: an #LrgDeckMixin
 * @card: (transfer none): the card that was exhausted
 *
 * Called when a card is exhausted.
 *
 * Since: 1.0
 */
void
lrg_deck_mixin_on_card_exhausted (LrgDeckMixin    *self,
                                  LrgCardInstance *card)
{
    LrgDeckMixinInterface *iface;

    g_return_if_fail (LRG_IS_DECK_MIXIN (self));
    g_return_if_fail (card != NULL);

    iface = LRG_DECK_MIXIN_GET_IFACE (self);
    if (iface->on_card_exhausted != NULL)
        iface->on_card_exhausted (self, card);
}

/**
 * lrg_deck_mixin_on_turn_started:
 * @self: an #LrgDeckMixin
 * @turn_number: the current turn number
 *
 * Called at the start of a new turn.
 *
 * Since: 1.0
 */
void
lrg_deck_mixin_on_turn_started (LrgDeckMixin *self,
                                guint         turn_number)
{
    LrgDeckMixinInterface *iface;

    g_return_if_fail (LRG_IS_DECK_MIXIN (self));

    iface = LRG_DECK_MIXIN_GET_IFACE (self);
    if (iface->on_turn_started != NULL)
        iface->on_turn_started (self, turn_number);
}

/**
 * lrg_deck_mixin_on_turn_ended:
 * @self: an #LrgDeckMixin
 * @turn_number: the turn that just ended
 *
 * Called at the end of a turn.
 *
 * Since: 1.0
 */
void
lrg_deck_mixin_on_turn_ended (LrgDeckMixin *self,
                              guint         turn_number)
{
    LrgDeckMixinInterface *iface;

    g_return_if_fail (LRG_IS_DECK_MIXIN (self));

    iface = LRG_DECK_MIXIN_GET_IFACE (self);
    if (iface->on_turn_ended != NULL)
        iface->on_turn_ended (self, turn_number);
}

/* ==========================================================================
 * Helper Methods
 * ========================================================================== */

/**
 * lrg_deck_mixin_draw_card:
 * @self: an #LrgDeckMixin
 *
 * Draws a single card from the draw pile to hand.
 *
 * Returns: (transfer none) (nullable): the drawn card, or %NULL
 *
 * Since: 1.0
 */
LrgCardInstance *
lrg_deck_mixin_draw_card (LrgDeckMixin *self)
{
    LrgDeckInstance *deck;
    LrgCardInstance *card;

    g_return_val_if_fail (LRG_IS_DECK_MIXIN (self), NULL);

    deck = lrg_deck_mixin_get_deck_instance (self);
    if (deck == NULL)
        return NULL;

    card = lrg_deck_instance_draw_card (deck);
    if (card != NULL)
        lrg_deck_mixin_on_card_drawn (self, card);

    return card;
}

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
guint
lrg_deck_mixin_draw_cards (LrgDeckMixin *self,
                           guint         count)
{
    LrgDeckInstance *deck;
    guint i;
    guint drawn = 0;

    g_return_val_if_fail (LRG_IS_DECK_MIXIN (self), 0);

    deck = lrg_deck_mixin_get_deck_instance (self);
    if (deck == NULL)
        return 0;

    for (i = 0; i < count; i++)
    {
        LrgCardInstance *card = lrg_deck_instance_draw_card (deck);
        if (card == NULL)
            break;

        lrg_deck_mixin_on_card_drawn (self, card);
        drawn++;
    }

    return drawn;
}

/**
 * lrg_deck_mixin_discard_hand:
 * @self: an #LrgDeckMixin
 *
 * Discards all cards in hand to the discard pile.
 *
 * Returns: number of cards discarded
 *
 * Since: 1.0
 */
guint
lrg_deck_mixin_discard_hand (LrgDeckMixin *self)
{
    LrgDeckInstance *deck;
    LrgHand *hand;
    LrgCardPile *discard_pile;
    GPtrArray *cards;
    guint i;
    guint discarded = 0;

    g_return_val_if_fail (LRG_IS_DECK_MIXIN (self), 0);

    deck = lrg_deck_mixin_get_deck_instance (self);
    if (deck == NULL)
        return 0;

    hand = lrg_deck_instance_get_hand (deck);
    if (hand == NULL)
        return 0;

    discard_pile = lrg_deck_instance_get_discard_pile (deck);
    if (discard_pile == NULL)
        return 0;

    /* Get a copy of the cards array since discard modifies it */
    cards = g_ptr_array_new ();
    {
        GPtrArray *hand_cards = lrg_hand_get_cards (hand);
        for (i = 0; i < hand_cards->len; i++)
            g_ptr_array_add (cards, g_ptr_array_index (hand_cards, i));
    }

    /* Discard each card */
    for (i = 0; i < cards->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (cards, i);
        if (lrg_hand_discard (hand, card, discard_pile))
        {
            lrg_deck_mixin_on_card_discarded (self, card);
            discarded++;
        }
    }

    g_ptr_array_unref (cards);

    return discarded;
}

/**
 * lrg_deck_mixin_shuffle_discard_into_deck:
 * @self: an #LrgDeckMixin
 *
 * Shuffles the discard pile into the draw pile.
 *
 * Since: 1.0
 */
void
lrg_deck_mixin_shuffle_discard_into_deck (LrgDeckMixin *self)
{
    LrgDeckInstance *deck;

    g_return_if_fail (LRG_IS_DECK_MIXIN (self));

    deck = lrg_deck_mixin_get_deck_instance (self);
    if (deck == NULL)
        return;

    lrg_deck_instance_shuffle_discard_into_draw (deck);
    lrg_deck_mixin_on_deck_shuffled (self);
}

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
LrgCardPile *
lrg_deck_mixin_get_draw_pile (LrgDeckMixin *self)
{
    LrgDeckInstance *deck;

    g_return_val_if_fail (LRG_IS_DECK_MIXIN (self), NULL);

    deck = lrg_deck_mixin_get_deck_instance (self);
    if (deck == NULL)
        return NULL;

    return lrg_deck_instance_get_draw_pile (deck);
}

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
LrgCardPile *
lrg_deck_mixin_get_discard_pile (LrgDeckMixin *self)
{
    LrgDeckInstance *deck;

    g_return_val_if_fail (LRG_IS_DECK_MIXIN (self), NULL);

    deck = lrg_deck_mixin_get_deck_instance (self);
    if (deck == NULL)
        return NULL;

    return lrg_deck_instance_get_discard_pile (deck);
}

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
LrgCardPile *
lrg_deck_mixin_get_exhaust_pile (LrgDeckMixin *self)
{
    LrgDeckInstance *deck;

    g_return_val_if_fail (LRG_IS_DECK_MIXIN (self), NULL);

    deck = lrg_deck_mixin_get_deck_instance (self);
    if (deck == NULL)
        return NULL;

    return lrg_deck_instance_get_exhaust_pile (deck);
}

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
LrgHand *
lrg_deck_mixin_get_hand (LrgDeckMixin *self)
{
    LrgDeckInstance *deck;

    g_return_val_if_fail (LRG_IS_DECK_MIXIN (self), NULL);

    deck = lrg_deck_mixin_get_deck_instance (self);
    if (deck == NULL)
        return NULL;

    return lrg_deck_instance_get_hand (deck);
}

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
guint
lrg_deck_mixin_get_draw_pile_count (LrgDeckMixin *self)
{
    LrgCardPile *pile;

    g_return_val_if_fail (LRG_IS_DECK_MIXIN (self), 0);

    pile = lrg_deck_mixin_get_draw_pile (self);
    if (pile == NULL)
        return 0;

    return lrg_card_pile_get_count (pile);
}

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
guint
lrg_deck_mixin_get_discard_pile_count (LrgDeckMixin *self)
{
    LrgCardPile *pile;

    g_return_val_if_fail (LRG_IS_DECK_MIXIN (self), 0);

    pile = lrg_deck_mixin_get_discard_pile (self);
    if (pile == NULL)
        return 0;

    return lrg_card_pile_get_count (pile);
}

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
guint
lrg_deck_mixin_get_hand_count (LrgDeckMixin *self)
{
    LrgHand *hand;

    g_return_val_if_fail (LRG_IS_DECK_MIXIN (self), 0);

    hand = lrg_deck_mixin_get_hand (self);
    if (hand == NULL)
        return 0;

    return lrg_hand_get_count (hand);
}

/**
 * lrg_deck_mixin_setup_deck:
 * @self: an #LrgDeckMixin
 *
 * Sets up the deck at the start of combat/round.
 *
 * Since: 1.0
 */
void
lrg_deck_mixin_setup_deck (LrgDeckMixin *self)
{
    LrgDeckInstance *deck;

    g_return_if_fail (LRG_IS_DECK_MIXIN (self));

    deck = lrg_deck_mixin_get_deck_instance (self);
    if (deck == NULL)
        return;

    lrg_deck_instance_setup (deck);
    lrg_deck_mixin_on_deck_shuffled (self);
}

/**
 * lrg_deck_mixin_end_combat:
 * @self: an #LrgDeckMixin
 *
 * Cleans up at the end of combat/round.
 *
 * Since: 1.0
 */
void
lrg_deck_mixin_end_combat (LrgDeckMixin *self)
{
    LrgDeckInstance *deck;

    g_return_if_fail (LRG_IS_DECK_MIXIN (self));

    deck = lrg_deck_mixin_get_deck_instance (self);
    if (deck == NULL)
        return;

    lrg_deck_instance_end_combat (deck);
}
