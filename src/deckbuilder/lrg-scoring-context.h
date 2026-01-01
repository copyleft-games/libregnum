/* lrg-scoring-context.h
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
#include "lrg-scoring-hand.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCORING_CONTEXT (lrg_scoring_context_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScoringContext, lrg_scoring_context, LRG, SCORING_CONTEXT, GObject)

/**
 * LrgScoringContext:
 *
 * Holds the state during a scoring round.
 *
 * The scoring context tracks all values that contribute to the final
 * score calculation in a Balatro-style scoring system:
 *
 * - Base chips from the hand type
 * - Base mult from the hand type
 * - Bonus chips from cards and jokers
 * - Bonus mult from cards and jokers
 * - X-mult (multiplicative multipliers) from jokers
 * - Active jokers and their effects
 *
 * Since: 1.0
 */

/* Construction */

/**
 * lrg_scoring_context_new:
 *
 * Creates a new scoring context.
 *
 * Returns: (transfer full): A new #LrgScoringContext
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgScoringContext *
lrg_scoring_context_new (void);

/* Hand Information */

/**
 * lrg_scoring_context_set_hand_type:
 * @self: a #LrgScoringContext
 * @hand_type: the #LrgHandType
 *
 * Sets the current hand type being scored.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scoring_context_set_hand_type (LrgScoringContext *self,
                                   LrgHandType        hand_type);

/**
 * lrg_scoring_context_get_hand_type:
 * @self: a #LrgScoringContext
 *
 * Gets the current hand type.
 *
 * Returns: the #LrgHandType
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgHandType
lrg_scoring_context_get_hand_type (LrgScoringContext *self);

/**
 * lrg_scoring_context_set_scoring_cards:
 * @self: a #LrgScoringContext
 * @cards: (element-type LrgCardInstance) (transfer none): the scoring cards
 *
 * Sets the cards that contribute to the hand score.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scoring_context_set_scoring_cards (LrgScoringContext *self,
                                       GPtrArray         *cards);

/**
 * lrg_scoring_context_get_scoring_cards:
 * @self: a #LrgScoringContext
 *
 * Gets the scoring cards.
 *
 * Returns: (transfer none) (element-type LrgCardInstance): the scoring cards
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_scoring_context_get_scoring_cards (LrgScoringContext *self);

/* Chip Values */

/**
 * lrg_scoring_context_set_base_chips:
 * @self: a #LrgScoringContext
 * @chips: the base chip value
 *
 * Sets the base chips from the hand type.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scoring_context_set_base_chips (LrgScoringContext *self,
                                    gint64             chips);

/**
 * lrg_scoring_context_get_base_chips:
 * @self: a #LrgScoringContext
 *
 * Gets the base chips from the hand type.
 *
 * Returns: the base chips
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_scoring_context_get_base_chips (LrgScoringContext *self);

/**
 * lrg_scoring_context_add_chips:
 * @self: a #LrgScoringContext
 * @chips: chips to add
 *
 * Adds bonus chips (from cards, jokers, etc.).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scoring_context_add_chips (LrgScoringContext *self,
                               gint64             chips);

/**
 * lrg_scoring_context_get_total_chips:
 * @self: a #LrgScoringContext
 *
 * Gets the total chips (base + bonus).
 *
 * Returns: total chips
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_scoring_context_get_total_chips (LrgScoringContext *self);

/* Multiplier Values */

/**
 * lrg_scoring_context_set_base_mult:
 * @self: a #LrgScoringContext
 * @mult: the base multiplier
 *
 * Sets the base mult from the hand type.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scoring_context_set_base_mult (LrgScoringContext *self,
                                   gint64             mult);

/**
 * lrg_scoring_context_get_base_mult:
 * @self: a #LrgScoringContext
 *
 * Gets the base mult from the hand type.
 *
 * Returns: the base mult
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_scoring_context_get_base_mult (LrgScoringContext *self);

/**
 * lrg_scoring_context_add_mult:
 * @self: a #LrgScoringContext
 * @mult: mult to add
 *
 * Adds bonus mult (from cards, jokers, etc.).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scoring_context_add_mult (LrgScoringContext *self,
                              gint64             mult);

/**
 * lrg_scoring_context_get_total_mult:
 * @self: a #LrgScoringContext
 *
 * Gets the total mult (base + bonus) before X-mult.
 *
 * Returns: total mult
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_scoring_context_get_total_mult (LrgScoringContext *self);

/* X-Mult (multiplicative multipliers) */

/**
 * lrg_scoring_context_apply_x_mult:
 * @self: a #LrgScoringContext
 * @x_mult: the X-mult to apply
 *
 * Applies a multiplicative multiplier (e.g., x1.5, x2).
 * Multiple X-mults are multiplied together.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scoring_context_apply_x_mult (LrgScoringContext *self,
                                  gdouble            x_mult);

/**
 * lrg_scoring_context_get_x_mult:
 * @self: a #LrgScoringContext
 *
 * Gets the combined X-mult value.
 *
 * Returns: the X-mult
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_scoring_context_get_x_mult (LrgScoringContext *self);

/* Final Score */

/**
 * lrg_scoring_context_calculate_score:
 * @self: a #LrgScoringContext
 *
 * Calculates the final score: (chips) * (mult * x_mult)
 *
 * Returns: the final score
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_scoring_context_calculate_score (LrgScoringContext *self);

/* State Management */

/**
 * lrg_scoring_context_reset:
 * @self: a #LrgScoringContext
 *
 * Resets all values to their defaults for a new hand.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scoring_context_reset (LrgScoringContext *self);

/* Joker Tracking */

/**
 * lrg_scoring_context_set_jokers:
 * @self: a #LrgScoringContext
 * @jokers: (element-type LrgJokerInstance) (transfer none) (nullable): active jokers
 *
 * Sets the active jokers for this scoring round.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scoring_context_set_jokers (LrgScoringContext *self,
                                GPtrArray         *jokers);

/**
 * lrg_scoring_context_get_jokers:
 * @self: a #LrgScoringContext
 *
 * Gets the active jokers.
 *
 * Returns: (transfer none) (element-type LrgJokerInstance) (nullable): the jokers
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_scoring_context_get_jokers (LrgScoringContext *self);

/* Tracking triggered effects */

/**
 * lrg_scoring_context_add_triggered_card:
 * @self: a #LrgScoringContext
 * @card: the card that triggered
 *
 * Records that a card's scoring ability triggered.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scoring_context_add_triggered_card (LrgScoringContext *self,
                                        LrgCardInstance   *card);

/**
 * lrg_scoring_context_get_triggered_cards:
 * @self: a #LrgScoringContext
 *
 * Gets cards that triggered during scoring.
 *
 * Returns: (transfer none) (element-type LrgCardInstance): triggered cards
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_scoring_context_get_triggered_cards (LrgScoringContext *self);

G_END_DECLS
