/* lrg-scoring-hand.h
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "../lrg-types.h"
#include "lrg-card-instance.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCORING_HAND (lrg_scoring_hand_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgScoringHand, lrg_scoring_hand, LRG, SCORING_HAND, GObject)

/**
 * LrgScoringHandClass:
 * @evaluate: Virtual method to evaluate cards and determine hand type
 *
 * Class structure for #LrgScoringHand.
 *
 * Subclasses can override the evaluate method to implement
 * custom hand evaluation logic (e.g., for wild cards).
 *
 * Since: 1.0
 */
struct _LrgScoringHandClass
{
    GObjectClass parent_class;

    /**
     * LrgScoringHandClass::evaluate:
     * @self: a #LrgScoringHand
     *
     * Evaluates the current cards to determine the best hand.
     *
     * Returns: the best #LrgHandType from the cards
     *
     * Since: 1.0
     */
    LrgHandType (*evaluate) (LrgScoringHand *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* Construction */

/**
 * lrg_scoring_hand_new:
 *
 * Creates a new scoring hand evaluator.
 *
 * Returns: (transfer full): A new #LrgScoringHand
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgScoringHand *
lrg_scoring_hand_new (void);

/* Card Management */

/**
 * lrg_scoring_hand_set_cards:
 * @self: a #LrgScoringHand
 * @cards: (element-type LrgCardInstance) (transfer none): the cards to evaluate
 *
 * Sets the cards to evaluate for this hand.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scoring_hand_set_cards (LrgScoringHand *self,
                            GPtrArray      *cards);

/**
 * lrg_scoring_hand_get_cards:
 * @self: a #LrgScoringHand
 *
 * Gets the cards currently being evaluated.
 *
 * Returns: (transfer none) (element-type LrgCardInstance): the cards
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_scoring_hand_get_cards (LrgScoringHand *self);

/**
 * lrg_scoring_hand_clear_cards:
 * @self: a #LrgScoringHand
 *
 * Clears all cards from the hand.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scoring_hand_clear_cards (LrgScoringHand *self);

/* Evaluation */

/**
 * lrg_scoring_hand_evaluate:
 * @self: a #LrgScoringHand
 *
 * Evaluates the current cards to determine the best hand type.
 * This also updates the scoring cards array.
 *
 * Returns: the #LrgHandType formed by the cards
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgHandType
lrg_scoring_hand_evaluate (LrgScoringHand *self);

/**
 * lrg_scoring_hand_get_hand_type:
 * @self: a #LrgScoringHand
 *
 * Gets the last evaluated hand type.
 *
 * Returns: the #LrgHandType from the last evaluation
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgHandType
lrg_scoring_hand_get_hand_type (LrgScoringHand *self);

/**
 * lrg_scoring_hand_get_scoring_cards:
 * @self: a #LrgScoringHand
 *
 * Gets the cards that contribute to the current hand.
 * This is populated after calling lrg_scoring_hand_evaluate().
 *
 * Returns: (transfer none) (element-type LrgCardInstance): scoring cards
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_scoring_hand_get_scoring_cards (LrgScoringHand *self);

/* Utility Functions */

/**
 * lrg_scoring_hand_get_rank_value:
 * @rank: a #LrgCardRank
 *
 * Gets the numeric value of a card rank.
 * Ace is high (14), numbered cards are face value,
 * Jack=11, Queen=12, King=13.
 *
 * Returns: the numeric value of the rank
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_scoring_hand_get_rank_value (LrgCardRank rank);

/**
 * lrg_scoring_hand_get_chip_value:
 * @rank: a #LrgCardRank
 *
 * Gets the base chip value of a card rank (Balatro-style).
 * Numbered cards are worth their face value in chips.
 * Face cards are worth 10 chips. Aces are worth 11 chips.
 *
 * Returns: the chip value of the rank
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_scoring_hand_get_chip_value (LrgCardRank rank);

G_END_DECLS
