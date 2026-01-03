/* lrg-deckbuilder-poker-template.h - Poker-focused deckbuilder template
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * LrgDeckbuilderPokerTemplate is a final template specialized for
 * Balatro-style poker deckbuilder games. It extends LrgDeckbuilderTemplate
 * with poker hand evaluation, scoring context, and joker management.
 *
 * ## Features
 *
 * - **Hand Evaluation**: Standard poker hand detection (pair to royal flush)
 * - **Scoring System**: Chips x Mult scoring with X-mult support
 * - **Joker Management**: Active jokers that modify scoring
 * - **Ante System**: Progressive difficulty with blinds
 * - **Discards**: Limited discards per hand
 * - **Hand Plays**: Limited hands per round
 *
 * ## Usage
 *
 * ```c
 * LrgDeckbuilderPokerTemplate *poker = lrg_deckbuilder_poker_template_new ();
 *
 * // Start a round
 * lrg_deckbuilder_poker_template_start_round (poker);
 *
 * // Select cards and play hand
 * lrg_deckbuilder_poker_template_play_hand (poker, selected_cards);
 *
 * // Or discard and draw new cards
 * lrg_deckbuilder_poker_template_discard_cards (poker, cards_to_discard);
 * ```
 *
 * Since: 1.0
 */

#ifndef LRG_DECKBUILDER_POKER_TEMPLATE_H
#define LRG_DECKBUILDER_POKER_TEMPLATE_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-deckbuilder-template.h"

G_BEGIN_DECLS

/* Forward declarations */
typedef struct _LrgScoringContext LrgScoringContext;
typedef struct _LrgScoringHand    LrgScoringHand;
typedef struct _LrgScoringRules   LrgScoringRules;
typedef struct _LrgJokerInstance  LrgJokerInstance;
typedef struct _LrgJokerDef       LrgJokerDef;

#define LRG_TYPE_DECKBUILDER_POKER_TEMPLATE (lrg_deckbuilder_poker_template_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDeckbuilderPokerTemplate, lrg_deckbuilder_poker_template,
                      LRG, DECKBUILDER_POKER_TEMPLATE, LrgDeckbuilderTemplate)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_deckbuilder_poker_template_new:
 *
 * Creates a new poker template with default settings.
 *
 * Returns: (transfer full): a new #LrgDeckbuilderPokerTemplate
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgDeckbuilderPokerTemplate *
lrg_deckbuilder_poker_template_new (void);

/* ==========================================================================
 * Scoring Context
 * ========================================================================== */

/**
 * lrg_deckbuilder_poker_template_get_scoring_context:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Gets the current scoring context.
 *
 * Returns: (transfer none): the #LrgScoringContext
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgScoringContext *
lrg_deckbuilder_poker_template_get_scoring_context (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_get_scoring_hand:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Gets the hand evaluator.
 *
 * Returns: (transfer none): the #LrgScoringHand
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgScoringHand *
lrg_deckbuilder_poker_template_get_scoring_hand (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_get_scoring_rules:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Gets the scoring rules.
 *
 * Returns: (transfer none) (nullable): the #LrgScoringRules
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgScoringRules *
lrg_deckbuilder_poker_template_get_scoring_rules (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_set_scoring_rules:
 * @self: an #LrgDeckbuilderPokerTemplate
 * @rules: (transfer none): the scoring rules
 *
 * Sets the scoring rules.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_poker_template_set_scoring_rules (LrgDeckbuilderPokerTemplate *self,
                                                   LrgScoringRules             *rules);

/* ==========================================================================
 * Score & Progress
 * ========================================================================== */

/**
 * lrg_deckbuilder_poker_template_get_score:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Gets the current score this round.
 *
 * Returns: current score
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_deckbuilder_poker_template_get_score (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_get_blind_score:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Gets the score needed to beat the current blind.
 *
 * Returns: required score
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_deckbuilder_poker_template_get_blind_score (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_set_blind_score:
 * @self: an #LrgDeckbuilderPokerTemplate
 * @score: required score
 *
 * Sets the score needed to beat the blind.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_poker_template_set_blind_score (LrgDeckbuilderPokerTemplate *self,
                                                 gint64                       score);

/**
 * lrg_deckbuilder_poker_template_get_ante:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Gets the current ante level.
 *
 * Returns: current ante
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_deckbuilder_poker_template_get_ante (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_set_ante:
 * @self: an #LrgDeckbuilderPokerTemplate
 * @ante: new ante level
 *
 * Sets the ante level.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_poker_template_set_ante (LrgDeckbuilderPokerTemplate *self,
                                          guint                        ante);

/**
 * lrg_deckbuilder_poker_template_get_money:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Gets the current money.
 *
 * Returns: current money
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_deckbuilder_poker_template_get_money (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_set_money:
 * @self: an #LrgDeckbuilderPokerTemplate
 * @money: new money amount
 *
 * Sets the current money.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_poker_template_set_money (LrgDeckbuilderPokerTemplate *self,
                                           gint64                       money);

/**
 * lrg_deckbuilder_poker_template_add_money:
 * @self: an #LrgDeckbuilderPokerTemplate
 * @amount: money to add
 *
 * Adds money.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_poker_template_add_money (LrgDeckbuilderPokerTemplate *self,
                                           gint64                       amount);

/* ==========================================================================
 * Hands & Discards
 * ========================================================================== */

/**
 * lrg_deckbuilder_poker_template_get_hands_remaining:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Gets the number of hands remaining this round.
 *
 * Returns: hands remaining
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_deckbuilder_poker_template_get_hands_remaining (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_set_hands_remaining:
 * @self: an #LrgDeckbuilderPokerTemplate
 * @hands: hands remaining
 *
 * Sets the hands remaining.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_poker_template_set_hands_remaining (LrgDeckbuilderPokerTemplate *self,
                                                     guint                        hands);

/**
 * lrg_deckbuilder_poker_template_get_discards_remaining:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Gets the number of discards remaining this round.
 *
 * Returns: discards remaining
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_deckbuilder_poker_template_get_discards_remaining (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_set_discards_remaining:
 * @self: an #LrgDeckbuilderPokerTemplate
 * @discards: discards remaining
 *
 * Sets the discards remaining.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_poker_template_set_discards_remaining (LrgDeckbuilderPokerTemplate *self,
                                                        guint                        discards);

/**
 * lrg_deckbuilder_poker_template_get_max_hands:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Gets the maximum hands per round.
 *
 * Returns: max hands
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_deckbuilder_poker_template_get_max_hands (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_set_max_hands:
 * @self: an #LrgDeckbuilderPokerTemplate
 * @max_hands: max hands per round
 *
 * Sets the maximum hands per round.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_poker_template_set_max_hands (LrgDeckbuilderPokerTemplate *self,
                                               guint                        max_hands);

/**
 * lrg_deckbuilder_poker_template_get_max_discards:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Gets the maximum discards per round.
 *
 * Returns: max discards
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_deckbuilder_poker_template_get_max_discards (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_set_max_discards:
 * @self: an #LrgDeckbuilderPokerTemplate
 * @max_discards: max discards per round
 *
 * Sets the maximum discards per round.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_poker_template_set_max_discards (LrgDeckbuilderPokerTemplate *self,
                                                  guint                        max_discards);

/* ==========================================================================
 * Joker Management
 * ========================================================================== */

/**
 * lrg_deckbuilder_poker_template_add_joker:
 * @self: an #LrgDeckbuilderPokerTemplate
 * @joker: (transfer full): joker to add
 *
 * Adds a joker to the active jokers.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_poker_template_add_joker (LrgDeckbuilderPokerTemplate *self,
                                           LrgJokerInstance            *joker);

/**
 * lrg_deckbuilder_poker_template_add_joker_from_def:
 * @self: an #LrgDeckbuilderPokerTemplate
 * @def: (transfer none): joker definition
 *
 * Creates and adds a joker from a definition.
 *
 * Returns: (transfer none): the created joker
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgJokerInstance *
lrg_deckbuilder_poker_template_add_joker_from_def (LrgDeckbuilderPokerTemplate *self,
                                                    LrgJokerDef                 *def);

/**
 * lrg_deckbuilder_poker_template_remove_joker:
 * @self: an #LrgDeckbuilderPokerTemplate
 * @joker: joker to remove
 *
 * Removes a joker.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_poker_template_remove_joker (LrgDeckbuilderPokerTemplate *self,
                                              LrgJokerInstance            *joker);

/**
 * lrg_deckbuilder_poker_template_get_jokers:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Gets all active jokers.
 *
 * Returns: (transfer none) (element-type LrgJokerInstance): joker list
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_deckbuilder_poker_template_get_jokers (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_get_joker_count:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Gets the number of active jokers.
 *
 * Returns: joker count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_deckbuilder_poker_template_get_joker_count (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_get_max_jokers:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Gets the maximum number of jokers allowed.
 *
 * Returns: max jokers
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_deckbuilder_poker_template_get_max_jokers (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_set_max_jokers:
 * @self: an #LrgDeckbuilderPokerTemplate
 * @max_jokers: max jokers allowed
 *
 * Sets the maximum jokers allowed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_poker_template_set_max_jokers (LrgDeckbuilderPokerTemplate *self,
                                                guint                        max_jokers);

/* ==========================================================================
 * Round Management
 * ========================================================================== */

/**
 * lrg_deckbuilder_poker_template_start_round:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Starts a new round. Sets up deck, resets hands/discards.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_deckbuilder_poker_template_start_round (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_end_round:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Ends the current round.
 *
 * Returns: %TRUE if blind was beaten
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_deckbuilder_poker_template_end_round (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_is_in_round:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Checks if currently in a round.
 *
 * Returns: %TRUE if in round
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_deckbuilder_poker_template_is_in_round (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_is_round_won:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Checks if the current score beats the blind.
 *
 * Returns: %TRUE if score >= blind_score
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_deckbuilder_poker_template_is_round_won (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_is_round_lost:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Checks if the round is lost (no hands remaining and score < blind).
 *
 * Returns: %TRUE if round is lost
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_deckbuilder_poker_template_is_round_lost (LrgDeckbuilderPokerTemplate *self);

/* ==========================================================================
 * Hand Operations
 * ========================================================================== */

/**
 * lrg_deckbuilder_poker_template_play_hand:
 * @self: an #LrgDeckbuilderPokerTemplate
 * @cards: (element-type LrgCardInstance) (transfer none): cards to play
 *
 * Plays the selected cards as a poker hand.
 * Evaluates the hand, applies scoring, and discards played cards.
 *
 * Returns: the score from this hand
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_deckbuilder_poker_template_play_hand (LrgDeckbuilderPokerTemplate *self,
                                           GPtrArray                   *cards);

/**
 * lrg_deckbuilder_poker_template_play_selected:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Plays the currently selected cards from hand.
 *
 * Returns: the score from this hand
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_deckbuilder_poker_template_play_selected (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_discard_cards:
 * @self: an #LrgDeckbuilderPokerTemplate
 * @cards: (element-type LrgCardInstance) (transfer none): cards to discard
 *
 * Discards the selected cards and draws replacements.
 *
 * Returns: %TRUE if discard was successful
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_deckbuilder_poker_template_discard_cards (LrgDeckbuilderPokerTemplate *self,
                                               GPtrArray                   *cards);

/**
 * lrg_deckbuilder_poker_template_discard_selected:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Discards the currently selected cards and draws replacements.
 *
 * Returns: %TRUE if discard was successful
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_deckbuilder_poker_template_discard_selected (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_can_play_hand:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Checks if a hand can be played (hands remaining > 0, cards selected).
 *
 * Returns: %TRUE if a hand can be played
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_deckbuilder_poker_template_can_play_hand (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_can_discard:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Checks if a discard is allowed (discards remaining > 0, cards selected).
 *
 * Returns: %TRUE if discard is allowed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_deckbuilder_poker_template_can_discard (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_evaluate_hand:
 * @self: an #LrgDeckbuilderPokerTemplate
 * @cards: (element-type LrgCardInstance) (transfer none): cards to evaluate
 *
 * Evaluates cards without playing them (for preview).
 *
 * Returns: the #LrgHandType
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgHandType
lrg_deckbuilder_poker_template_evaluate_hand (LrgDeckbuilderPokerTemplate *self,
                                               GPtrArray                   *cards);

/**
 * lrg_deckbuilder_poker_template_preview_score:
 * @self: an #LrgDeckbuilderPokerTemplate
 * @cards: (element-type LrgCardInstance) (transfer none): cards to evaluate
 *
 * Previews the score without playing (for UI display).
 *
 * Returns: the estimated score
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_deckbuilder_poker_template_preview_score (LrgDeckbuilderPokerTemplate *self,
                                               GPtrArray                   *cards);

/**
 * lrg_deckbuilder_poker_template_get_last_hand_type:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Gets the hand type from the last played hand.
 *
 * Returns: the #LrgHandType
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgHandType
lrg_deckbuilder_poker_template_get_last_hand_type (LrgDeckbuilderPokerTemplate *self);

/**
 * lrg_deckbuilder_poker_template_get_last_hand_score:
 * @self: an #LrgDeckbuilderPokerTemplate
 *
 * Gets the score from the last played hand.
 *
 * Returns: the last hand score
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_deckbuilder_poker_template_get_last_hand_score (LrgDeckbuilderPokerTemplate *self);

G_END_DECLS

#endif /* LRG_DECKBUILDER_POKER_TEMPLATE_H */
