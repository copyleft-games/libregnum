/* lrg-card-pile.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCardPile - A pile of cards (draw, discard, exhaust).
 *
 * Card piles are ordered collections of card instances. Cards can be
 * added to the top, bottom, or random position. Cards are drawn from
 * the top (end of internal array for O(1) removal).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-card-instance.h"

G_BEGIN_DECLS

#define LRG_TYPE_CARD_PILE (lrg_card_pile_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCardPile, lrg_card_pile, LRG, CARD_PILE, GObject)

/* Construction */

/**
 * lrg_card_pile_new:
 *
 * Creates a new empty card pile.
 *
 * Returns: (transfer full): A new #LrgCardPile
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardPile *
lrg_card_pile_new (void);

/**
 * lrg_card_pile_new_with_zone:
 * @zone: the zone for cards in this pile
 *
 * Creates a new empty card pile that automatically sets
 * the zone on cards added to it.
 *
 * Returns: (transfer full): A new #LrgCardPile
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardPile *
lrg_card_pile_new_with_zone (LrgCardZone zone);

/* Card Operations */

/**
 * lrg_card_pile_add:
 * @self: a #LrgCardPile
 * @card: (transfer full): the card to add
 * @position: where to add the card
 *
 * Adds a card to the pile at the specified position.
 * Takes ownership of the card reference.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_pile_add (LrgCardPile     *self,
                   LrgCardInstance *card,
                   LrgPilePosition  position);

/**
 * lrg_card_pile_add_top:
 * @self: a #LrgCardPile
 * @card: (transfer full): the card to add
 *
 * Convenience function to add a card to the top of the pile.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_pile_add_top (LrgCardPile     *self,
                       LrgCardInstance *card);

/**
 * lrg_card_pile_add_bottom:
 * @self: a #LrgCardPile
 * @card: (transfer full): the card to add
 *
 * Convenience function to add a card to the bottom of the pile.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_pile_add_bottom (LrgCardPile     *self,
                          LrgCardInstance *card);

/**
 * lrg_card_pile_draw:
 * @self: a #LrgCardPile
 *
 * Draws a card from the top of the pile.
 *
 * Returns: (transfer full) (nullable): the drawn card, or %NULL if empty
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardInstance *
lrg_card_pile_draw (LrgCardPile *self);

/**
 * lrg_card_pile_draw_bottom:
 * @self: a #LrgCardPile
 *
 * Draws a card from the bottom of the pile.
 *
 * Returns: (transfer full) (nullable): the drawn card, or %NULL if empty
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardInstance *
lrg_card_pile_draw_bottom (LrgCardPile *self);

/**
 * lrg_card_pile_draw_random:
 * @self: a #LrgCardPile
 * @rng: (nullable): random number generator, or %NULL for default
 *
 * Draws a random card from the pile.
 *
 * Returns: (transfer full) (nullable): the drawn card, or %NULL if empty
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardInstance *
lrg_card_pile_draw_random (LrgCardPile *self,
                           GRand       *rng);

/**
 * lrg_card_pile_remove:
 * @self: a #LrgCardPile
 * @card: the card to remove
 *
 * Removes a specific card from the pile.
 *
 * Returns: %TRUE if the card was found and removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_card_pile_remove (LrgCardPile     *self,
                      LrgCardInstance *card);

/**
 * lrg_card_pile_peek:
 * @self: a #LrgCardPile
 *
 * Peeks at the top card without removing it.
 *
 * Returns: (transfer none) (nullable): the top card, or %NULL if empty
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardInstance *
lrg_card_pile_peek (LrgCardPile *self);

/**
 * lrg_card_pile_peek_n:
 * @self: a #LrgCardPile
 * @n: number of cards to peek
 *
 * Peeks at the top N cards without removing them.
 *
 * Returns: (transfer container) (element-type LrgCardInstance): array of cards
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_card_pile_peek_n (LrgCardPile *self,
                      guint        n);

/* Shuffle */

/**
 * lrg_card_pile_shuffle:
 * @self: a #LrgCardPile
 * @rng: (nullable): random number generator, or %NULL for default
 *
 * Shuffles the pile using Fisher-Yates algorithm.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_pile_shuffle (LrgCardPile *self,
                       GRand       *rng);

/* Query */

/**
 * lrg_card_pile_get_count:
 * @self: a #LrgCardPile
 *
 * Gets the number of cards in the pile.
 *
 * Returns: the card count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_card_pile_get_count (LrgCardPile *self);

/**
 * lrg_card_pile_is_empty:
 * @self: a #LrgCardPile
 *
 * Checks if the pile is empty.
 *
 * Returns: %TRUE if empty
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_card_pile_is_empty (LrgCardPile *self);

/**
 * lrg_card_pile_contains:
 * @self: a #LrgCardPile
 * @card: the card to check
 *
 * Checks if the pile contains a specific card.
 *
 * Returns: %TRUE if the card is in the pile
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_card_pile_contains (LrgCardPile     *self,
                        LrgCardInstance *card);

/**
 * lrg_card_pile_get_card_at:
 * @self: a #LrgCardPile
 * @index: the index (0 = bottom, count-1 = top)
 *
 * Gets the card at a specific index.
 *
 * Returns: (transfer none) (nullable): the card, or %NULL if out of range
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardInstance *
lrg_card_pile_get_card_at (LrgCardPile *self,
                           guint        index);

/**
 * lrg_card_pile_get_cards:
 * @self: a #LrgCardPile
 *
 * Gets all cards in the pile.
 *
 * Returns: (transfer none) (element-type LrgCardInstance): the cards array
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_card_pile_get_cards (LrgCardPile *self);

/* Transfer */

/**
 * lrg_card_pile_transfer_all:
 * @self: the source pile
 * @dest: the destination pile
 *
 * Transfers all cards from this pile to another.
 * Does not shuffle - maintains order.
 *
 * Returns: the number of cards transferred
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_card_pile_transfer_all (LrgCardPile *self,
                            LrgCardPile *dest);

/**
 * lrg_card_pile_clear:
 * @self: a #LrgCardPile
 *
 * Removes all cards from the pile.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_pile_clear (LrgCardPile *self);

/* Zone */

/**
 * lrg_card_pile_get_zone:
 * @self: a #LrgCardPile
 *
 * Gets the zone associated with this pile.
 *
 * Returns: the #LrgCardZone
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardZone
lrg_card_pile_get_zone (LrgCardPile *self);

/**
 * lrg_card_pile_set_zone:
 * @self: a #LrgCardPile
 * @zone: the zone
 *
 * Sets the zone for this pile. Cards added will have their zone set.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_pile_set_zone (LrgCardPile *self,
                        LrgCardZone  zone);

/* Search */

/**
 * lrg_card_pile_find_by_id:
 * @self: a #LrgCardPile
 * @card_id: the card definition ID to find
 *
 * Finds the first card with the given definition ID.
 *
 * Returns: (transfer none) (nullable): the card, or %NULL if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardInstance *
lrg_card_pile_find_by_id (LrgCardPile *self,
                          const gchar *card_id);

/**
 * lrg_card_pile_find_all_by_id:
 * @self: a #LrgCardPile
 * @card_id: the card definition ID to find
 *
 * Finds all cards with the given definition ID.
 *
 * Returns: (transfer container) (element-type LrgCardInstance): array of matching cards
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_card_pile_find_all_by_id (LrgCardPile *self,
                              const gchar *card_id);

/**
 * lrg_card_pile_find_by_type:
 * @self: a #LrgCardPile
 * @card_type: the card type to find
 *
 * Finds all cards of a specific type.
 *
 * Returns: (transfer container) (element-type LrgCardInstance): array of matching cards
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_card_pile_find_by_type (LrgCardPile *self,
                            LrgCardType  card_type);

/**
 * lrg_card_pile_find_by_keyword:
 * @self: a #LrgCardPile
 * @keyword: the keyword to find
 *
 * Finds all cards with a specific keyword.
 *
 * Returns: (transfer container) (element-type LrgCardInstance): array of matching cards
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_card_pile_find_by_keyword (LrgCardPile    *self,
                               LrgCardKeyword  keyword);

/* Foreach */

/**
 * lrg_card_pile_foreach:
 * @self: a #LrgCardPile
 * @func: (scope call): callback for each card
 * @user_data: (closure): data for callback
 *
 * Calls a function for each card in the pile.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_pile_foreach (LrgCardPile *self,
                       GFunc        func,
                       gpointer     user_data);

G_END_DECLS
