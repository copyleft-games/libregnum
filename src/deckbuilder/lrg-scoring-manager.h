/* lrg-scoring-manager.h
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
#include "lrg-scoring-hand.h"
#include "lrg-scoring-context.h"
#include "lrg-scoring-rules.h"
#include "lrg-joker-instance.h"
#include "lrg-card-instance.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCORING_MANAGER (lrg_scoring_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgScoringManager, lrg_scoring_manager, LRG, SCORING_MANAGER, GObject)

/**
 * LrgScoringManagerClass:
 * @on_hand_scored: Virtual method called when a hand is scored
 * @on_round_complete: Virtual method called when a round completes
 *
 * Class structure for #LrgScoringManager.
 *
 * Subclasses can override methods to customize scoring behavior.
 *
 * Since: 1.0
 */
struct _LrgScoringManagerClass
{
    GObjectClass parent_class;

    /**
     * LrgScoringManagerClass::on_hand_scored:
     * @self: a #LrgScoringManager
     * @ctx: the scoring context with results
     *
     * Called after a hand is scored. Override to add custom effects.
     *
     * Since: 1.0
     */
    void (*on_hand_scored) (LrgScoringManager *self,
                            LrgScoringContext *ctx);

    /**
     * LrgScoringManagerClass::on_round_complete:
     * @self: a #LrgScoringManager
     * @victory: whether the round was won
     *
     * Called when a round ends. Override for custom round completion logic.
     *
     * Since: 1.0
     */
    void (*on_round_complete) (LrgScoringManager *self,
                               gboolean           victory);

    /*< private >*/
    gpointer _reserved[8];
};

/* Signals */
/* hand-scored: emitted when a hand is scored */
/* round-started: emitted when a round starts */
/* round-ended: emitted when a round ends */

/* Construction */

/**
 * lrg_scoring_manager_new:
 *
 * Creates a new scoring manager.
 *
 * Returns: (transfer full): A new #LrgScoringManager
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgScoringManager *
lrg_scoring_manager_new (void);

/**
 * lrg_scoring_manager_get_default:
 *
 * Gets the default scoring manager singleton.
 *
 * Returns: (transfer none): the default #LrgScoringManager
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgScoringManager *
lrg_scoring_manager_get_default (void);

/* Rules Configuration */

/**
 * lrg_scoring_manager_set_rules:
 * @self: a #LrgScoringManager
 * @rules: (transfer none): the scoring rules to use
 *
 * Sets the scoring rules implementation.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scoring_manager_set_rules (LrgScoringManager *self,
                               LrgScoringRules   *rules);

/**
 * lrg_scoring_manager_get_rules:
 * @self: a #LrgScoringManager
 *
 * Gets the current scoring rules.
 *
 * Returns: (transfer none) (nullable): the #LrgScoringRules
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgScoringRules *
lrg_scoring_manager_get_rules (LrgScoringManager *self);

/* Round Management */

/**
 * lrg_scoring_manager_start_round:
 * @self: a #LrgScoringManager
 * @target_score: the score needed to win
 * @hands: number of hands allowed
 * @discards: number of discards allowed
 *
 * Starts a new scoring round.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scoring_manager_start_round (LrgScoringManager *self,
                                 gint64             target_score,
                                 gint               hands,
                                 gint               discards);

/**
 * lrg_scoring_manager_end_round:
 * @self: a #LrgScoringManager
 *
 * Ends the current round.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scoring_manager_end_round (LrgScoringManager *self);

/**
 * lrg_scoring_manager_is_round_active:
 * @self: a #LrgScoringManager
 *
 * Checks if a round is currently active.
 *
 * Returns: %TRUE if a round is active
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_scoring_manager_is_round_active (LrgScoringManager *self);

/* Round State */

/**
 * lrg_scoring_manager_get_current_score:
 * @self: a #LrgScoringManager
 *
 * Gets the current accumulated score this round.
 *
 * Returns: the current score
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_scoring_manager_get_current_score (LrgScoringManager *self);

/**
 * lrg_scoring_manager_get_target_score:
 * @self: a #LrgScoringManager
 *
 * Gets the target score for this round.
 *
 * Returns: the target score
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_scoring_manager_get_target_score (LrgScoringManager *self);

/**
 * lrg_scoring_manager_get_hands_remaining:
 * @self: a #LrgScoringManager
 *
 * Gets the number of hands remaining this round.
 *
 * Returns: hands remaining
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_scoring_manager_get_hands_remaining (LrgScoringManager *self);

/**
 * lrg_scoring_manager_get_discards_remaining:
 * @self: a #LrgScoringManager
 *
 * Gets the number of discards remaining this round.
 *
 * Returns: discards remaining
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_scoring_manager_get_discards_remaining (LrgScoringManager *self);

/**
 * lrg_scoring_manager_get_phase:
 * @self: a #LrgScoringManager
 *
 * Gets the current scoring phase.
 *
 * Returns: the #LrgScoringPhase
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgScoringPhase
lrg_scoring_manager_get_phase (LrgScoringManager *self);

/* Joker Management */

/**
 * lrg_scoring_manager_add_joker:
 * @self: a #LrgScoringManager
 * @joker: (transfer full): the joker to add
 *
 * Adds a joker to the active jokers.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scoring_manager_add_joker (LrgScoringManager *self,
                               LrgJokerInstance  *joker);

/**
 * lrg_scoring_manager_remove_joker:
 * @self: a #LrgScoringManager
 * @joker: the joker to remove
 *
 * Removes a joker from the active jokers.
 *
 * Returns: %TRUE if the joker was removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_scoring_manager_remove_joker (LrgScoringManager *self,
                                  LrgJokerInstance  *joker);

/**
 * lrg_scoring_manager_get_jokers:
 * @self: a #LrgScoringManager
 *
 * Gets the active jokers.
 *
 * Returns: (transfer none) (element-type LrgJokerInstance): the jokers
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_scoring_manager_get_jokers (LrgScoringManager *self);

/**
 * lrg_scoring_manager_get_max_jokers:
 * @self: a #LrgScoringManager
 *
 * Gets the maximum number of jokers allowed.
 *
 * Returns: max joker slots
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_scoring_manager_get_max_jokers (LrgScoringManager *self);

/**
 * lrg_scoring_manager_set_max_jokers:
 * @self: a #LrgScoringManager
 * @max: the maximum joker slots
 *
 * Sets the maximum number of jokers allowed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scoring_manager_set_max_jokers (LrgScoringManager *self,
                                    gint               max);

/* Scoring Actions */

/**
 * lrg_scoring_manager_play_hand:
 * @self: a #LrgScoringManager
 * @cards: (element-type LrgCardInstance): the cards to play
 *
 * Plays a hand of cards and scores it.
 *
 * Returns: the score for this hand
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_scoring_manager_play_hand (LrgScoringManager *self,
                               GPtrArray         *cards);

/**
 * lrg_scoring_manager_discard:
 * @self: a #LrgScoringManager
 * @cards: (element-type LrgCardInstance): the cards to discard
 *
 * Discards cards (uses a discard).
 *
 * Returns: %TRUE if the discard was successful
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_scoring_manager_discard (LrgScoringManager *self,
                             GPtrArray         *cards);

/* Hand Evaluation (without playing) */

/**
 * lrg_scoring_manager_evaluate_hand:
 * @self: a #LrgScoringManager
 * @cards: (element-type LrgCardInstance): the cards to evaluate
 *
 * Evaluates a hand without playing it.
 *
 * Returns: the #LrgHandType of the hand
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgHandType
lrg_scoring_manager_evaluate_hand (LrgScoringManager *self,
                                   GPtrArray         *cards);

/**
 * lrg_scoring_manager_preview_score:
 * @self: a #LrgScoringManager
 * @cards: (element-type LrgCardInstance): the cards to preview
 *
 * Previews the score for a hand without playing it.
 *
 * Returns: the estimated score
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_scoring_manager_preview_score (LrgScoringManager *self,
                                   GPtrArray         *cards);

/* Last Score Context */

/**
 * lrg_scoring_manager_get_last_context:
 * @self: a #LrgScoringManager
 *
 * Gets the scoring context from the last played hand.
 *
 * Returns: (transfer none) (nullable): the last #LrgScoringContext
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgScoringContext *
lrg_scoring_manager_get_last_context (LrgScoringManager *self);

G_END_DECLS
