/* lrg-hand.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgHand - The player's hand of cards.
 *
 * The hand manages the cards currently held by the player. It has a
 * maximum size (typically 10) and handles adding/removing cards with
 * special keyword support (Retain prevents discarding at end of turn).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-card-instance.h"
#include "lrg-card-pile.h"

G_BEGIN_DECLS

#define LRG_TYPE_HAND (lrg_hand_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgHand, lrg_hand, LRG, HAND, GObject)

/* Default maximum hand size */
#define LRG_HAND_DEFAULT_MAX_SIZE 10

/* Construction */

/**
 * lrg_hand_new:
 *
 * Creates a new hand with default maximum size.
 *
 * Returns: (transfer full): A new #LrgHand
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgHand *
lrg_hand_new (void);

/**
 * lrg_hand_new_with_size:
 * @max_size: the maximum number of cards the hand can hold
 *
 * Creates a new hand with the specified maximum size.
 *
 * Returns: (transfer full): A new #LrgHand
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgHand *
lrg_hand_new_with_size (guint max_size);

/* Card Operations */

/**
 * lrg_hand_add:
 * @self: a #LrgHand
 * @card: (transfer full): the card to add
 *
 * Adds a card to the hand. Takes ownership of the card reference.
 *
 * Returns: %TRUE if the card was added, %FALSE if the hand is full
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hand_add (LrgHand         *self,
              LrgCardInstance *card);

/**
 * lrg_hand_remove:
 * @self: a #LrgHand
 * @card: the card to remove
 *
 * Removes a specific card from the hand.
 *
 * Returns: (transfer full) (nullable): the removed card, or %NULL if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardInstance *
lrg_hand_remove (LrgHand         *self,
                 LrgCardInstance *card);

/**
 * lrg_hand_remove_at:
 * @self: a #LrgHand
 * @index: the index to remove at
 *
 * Removes the card at the specified index.
 *
 * Returns: (transfer full) (nullable): the removed card, or %NULL if out of range
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardInstance *
lrg_hand_remove_at (LrgHand *self,
                    guint    index);

/**
 * lrg_hand_discard:
 * @self: a #LrgHand
 * @card: the card to discard
 * @discard_pile: the pile to discard to
 *
 * Discards a specific card from the hand to the discard pile.
 * Respects the Retain keyword - retained cards are not discarded.
 *
 * Returns: %TRUE if the card was discarded, %FALSE if not found or retained
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hand_discard (LrgHand         *self,
                  LrgCardInstance *card,
                  LrgCardPile     *discard_pile);

/**
 * lrg_hand_discard_all:
 * @self: a #LrgHand
 * @discard_pile: the pile to discard to
 *
 * Discards all cards from the hand to the discard pile.
 * Respects the Retain keyword - retained cards stay in hand.
 *
 * Returns: the number of cards discarded
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_hand_discard_all (LrgHand     *self,
                      LrgCardPile *discard_pile);

/**
 * lrg_hand_discard_random:
 * @self: a #LrgHand
 * @discard_pile: the pile to discard to
 * @rng: (nullable): random number generator, or %NULL for default
 *
 * Discards a random card from the hand.
 * Respects the Retain keyword - if selected card is retained, picks another.
 *
 * Returns: (transfer none) (nullable): the discarded card, or %NULL if hand empty or all retained
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardInstance *
lrg_hand_discard_random (LrgHand     *self,
                         LrgCardPile *discard_pile,
                         GRand       *rng);

/* Query */

/**
 * lrg_hand_get_count:
 * @self: a #LrgHand
 *
 * Gets the number of cards in the hand.
 *
 * Returns: the card count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_hand_get_count (LrgHand *self);

/**
 * lrg_hand_get_max_size:
 * @self: a #LrgHand
 *
 * Gets the maximum hand size.
 *
 * Returns: the maximum number of cards
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_hand_get_max_size (LrgHand *self);

/**
 * lrg_hand_set_max_size:
 * @self: a #LrgHand
 * @max_size: the new maximum size
 *
 * Sets the maximum hand size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hand_set_max_size (LrgHand *self,
                       guint    max_size);

/**
 * lrg_hand_is_full:
 * @self: a #LrgHand
 *
 * Checks if the hand is at maximum capacity.
 *
 * Returns: %TRUE if the hand is full
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hand_is_full (LrgHand *self);

/**
 * lrg_hand_is_empty:
 * @self: a #LrgHand
 *
 * Checks if the hand is empty.
 *
 * Returns: %TRUE if the hand has no cards
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hand_is_empty (LrgHand *self);

/**
 * lrg_hand_contains:
 * @self: a #LrgHand
 * @card: the card to check
 *
 * Checks if the hand contains a specific card.
 *
 * Returns: %TRUE if the card is in the hand
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hand_contains (LrgHand         *self,
                   LrgCardInstance *card);

/**
 * lrg_hand_get_card_at:
 * @self: a #LrgHand
 * @index: the index (0 = leftmost)
 *
 * Gets the card at a specific index in the hand.
 *
 * Returns: (transfer none) (nullable): the card, or %NULL if out of range
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardInstance *
lrg_hand_get_card_at (LrgHand *self,
                      guint    index);

/**
 * lrg_hand_get_cards:
 * @self: a #LrgHand
 *
 * Gets all cards in the hand.
 *
 * Returns: (transfer none) (element-type LrgCardInstance): the cards array
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_hand_get_cards (LrgHand *self);

/* Search */

/**
 * lrg_hand_find_by_id:
 * @self: a #LrgHand
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
lrg_hand_find_by_id (LrgHand     *self,
                     const gchar *card_id);

/**
 * lrg_hand_find_all_by_id:
 * @self: a #LrgHand
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
lrg_hand_find_all_by_id (LrgHand     *self,
                         const gchar *card_id);

/**
 * lrg_hand_find_by_type:
 * @self: a #LrgHand
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
lrg_hand_find_by_type (LrgHand     *self,
                       LrgCardType  card_type);

/**
 * lrg_hand_find_by_keyword:
 * @self: a #LrgHand
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
lrg_hand_find_by_keyword (LrgHand        *self,
                          LrgCardKeyword  keyword);

/**
 * lrg_hand_find_playable:
 * @self: a #LrgHand
 * @available_energy: the amount of energy available
 *
 * Finds all cards that can be played with the available energy.
 * Excludes cards with the Unplayable keyword.
 *
 * Returns: (transfer container) (element-type LrgCardInstance): array of playable cards
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_hand_find_playable (LrgHand *self,
                        gint     available_energy);

/* Selection Support (for UI) */

/**
 * lrg_hand_get_selected:
 * @self: a #LrgHand
 *
 * Gets the currently selected cards (for multi-select UI).
 *
 * Returns: (transfer none) (element-type LrgCardInstance): array of selected cards
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_hand_get_selected (LrgHand *self);

/**
 * lrg_hand_select:
 * @self: a #LrgHand
 * @card: the card to select
 *
 * Selects a card in the hand.
 *
 * Returns: %TRUE if the card was selected
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hand_select (LrgHand         *self,
                 LrgCardInstance *card);

/**
 * lrg_hand_deselect:
 * @self: a #LrgHand
 * @card: the card to deselect
 *
 * Deselects a card in the hand.
 *
 * Returns: %TRUE if the card was deselected
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hand_deselect (LrgHand         *self,
                   LrgCardInstance *card);

/**
 * lrg_hand_clear_selection:
 * @self: a #LrgHand
 *
 * Clears all card selections.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hand_clear_selection (LrgHand *self);

/**
 * lrg_hand_is_selected:
 * @self: a #LrgHand
 * @card: the card to check
 *
 * Checks if a card is currently selected.
 *
 * Returns: %TRUE if the card is selected
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hand_is_selected (LrgHand         *self,
                      LrgCardInstance *card);

/* Utility */

/**
 * lrg_hand_clear:
 * @self: a #LrgHand
 *
 * Removes all cards from the hand without discarding.
 * Cards are released (unref'd).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hand_clear (LrgHand *self);

/**
 * lrg_hand_foreach:
 * @self: a #LrgHand
 * @func: (scope call): callback for each card
 * @user_data: (closure): data for callback
 *
 * Calls a function for each card in the hand.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hand_foreach (LrgHand  *self,
                  GFunc     func,
                  gpointer  user_data);

/**
 * lrg_hand_get_index_of:
 * @self: a #LrgHand
 * @card: the card to find
 *
 * Gets the index of a card in the hand.
 *
 * Returns: the index, or -1 if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_hand_get_index_of (LrgHand         *self,
                       LrgCardInstance *card);

/**
 * lrg_hand_sort_by_cost:
 * @self: a #LrgHand
 * @ascending: %TRUE for lowest cost first
 *
 * Sorts the hand by card cost.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hand_sort_by_cost (LrgHand  *self,
                       gboolean  ascending);

/**
 * lrg_hand_sort_by_type:
 * @self: a #LrgHand
 *
 * Sorts the hand by card type (Attack, Skill, Power, etc.).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hand_sort_by_type (LrgHand *self);

G_END_DECLS
