/* lrg-scoring-manager.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-scoring-manager.h"
#include "lrg-joker-def.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DECKBUILDER

/*
 * Default Balatro-style base values per hand type.
 * These are used when no custom scoring rules are set.
 */
static gint64
get_default_base_chips (LrgHandType hand_type)
{
    switch (hand_type)
    {
        case LRG_HAND_TYPE_HIGH_CARD:       return 5;
        case LRG_HAND_TYPE_PAIR:            return 10;
        case LRG_HAND_TYPE_TWO_PAIR:        return 20;
        case LRG_HAND_TYPE_THREE_OF_A_KIND: return 30;
        case LRG_HAND_TYPE_STRAIGHT:        return 30;
        case LRG_HAND_TYPE_FLUSH:           return 35;
        case LRG_HAND_TYPE_FULL_HOUSE:      return 40;
        case LRG_HAND_TYPE_FOUR_OF_A_KIND:  return 60;
        case LRG_HAND_TYPE_STRAIGHT_FLUSH:  return 100;
        case LRG_HAND_TYPE_ROYAL_FLUSH:     return 100;
        case LRG_HAND_TYPE_FIVE_OF_A_KIND:  return 120;
        case LRG_HAND_TYPE_FLUSH_HOUSE:     return 140;
        case LRG_HAND_TYPE_FLUSH_FIVE:      return 160;
        case LRG_HAND_TYPE_NONE:
        default:
            return 0;
    }
}

static gint64
get_default_base_mult (LrgHandType hand_type)
{
    switch (hand_type)
    {
        case LRG_HAND_TYPE_HIGH_CARD:       return 1;
        case LRG_HAND_TYPE_PAIR:            return 2;
        case LRG_HAND_TYPE_TWO_PAIR:        return 2;
        case LRG_HAND_TYPE_THREE_OF_A_KIND: return 3;
        case LRG_HAND_TYPE_STRAIGHT:        return 4;
        case LRG_HAND_TYPE_FLUSH:           return 4;
        case LRG_HAND_TYPE_FULL_HOUSE:      return 4;
        case LRG_HAND_TYPE_FOUR_OF_A_KIND:  return 7;
        case LRG_HAND_TYPE_STRAIGHT_FLUSH:  return 8;
        case LRG_HAND_TYPE_ROYAL_FLUSH:     return 8;
        case LRG_HAND_TYPE_FIVE_OF_A_KIND:  return 12;
        case LRG_HAND_TYPE_FLUSH_HOUSE:     return 14;
        case LRG_HAND_TYPE_FLUSH_FIVE:      return 16;
        case LRG_HAND_TYPE_NONE:
        default:
            return 1;
    }
}

/**
 * LrgScoringManager:
 *
 * Manages the Balatro-style scoring game flow.
 *
 * The scoring manager handles:
 * - Round lifecycle (start, play hands, end)
 * - Hand evaluation and scoring
 * - Joker effect application
 * - Score tracking
 * - Phase management
 *
 * A typical round flow:
 * 1. Start round with target score
 * 2. Player selects cards
 * 3. Play hand or discard
 * 4. Score is calculated with jokers
 * 5. Repeat until score reached or out of hands
 *
 * Since: 1.0
 */

typedef struct
{
    /* Configuration */
    LrgScoringRules *rules;
    LrgScoringHand  *evaluator;
    gint             max_jokers;

    /* Round state */
    gboolean         round_active;
    gint64           current_score;
    gint64           target_score;
    gint             hands_remaining;
    gint             discards_remaining;
    LrgScoringPhase  phase;

    /* Jokers */
    GPtrArray       *jokers;

    /* Last score context */
    LrgScoringContext *last_context;
} LrgScoringManagerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgScoringManager, lrg_scoring_manager, G_TYPE_OBJECT)

/* Signals */
enum
{
    SIGNAL_HAND_SCORED,
    SIGNAL_ROUND_STARTED,
    SIGNAL_ROUND_ENDED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static LrgScoringManager *default_manager = NULL;

static void
lrg_scoring_manager_finalize (GObject *object)
{
    LrgScoringManager *self = LRG_SCORING_MANAGER (object);
    LrgScoringManagerPrivate *priv = lrg_scoring_manager_get_instance_private (self);

    g_clear_object (&priv->rules);
    g_clear_object (&priv->evaluator);
    g_clear_pointer (&priv->jokers, g_ptr_array_unref);
    g_clear_object (&priv->last_context);

    G_OBJECT_CLASS (lrg_scoring_manager_parent_class)->finalize (object);
}

static void
lrg_scoring_manager_real_on_hand_scored (LrgScoringManager *self,
                                         LrgScoringContext *ctx)
{
    /* Default implementation does nothing */
}

static void
lrg_scoring_manager_real_on_round_complete (LrgScoringManager *self,
                                            gboolean           victory)
{
    /* Default implementation does nothing */
}

static void
lrg_scoring_manager_class_init (LrgScoringManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_scoring_manager_finalize;

    klass->on_hand_scored = lrg_scoring_manager_real_on_hand_scored;
    klass->on_round_complete = lrg_scoring_manager_real_on_round_complete;

    /**
     * LrgScoringManager::hand-scored:
     * @self: the manager
     * @ctx: the scoring context
     * @score: the score for this hand
     *
     * Emitted when a hand is scored.
     *
     * Since: 1.0
     */
    signals[SIGNAL_HAND_SCORED] =
        g_signal_new ("hand-scored",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 2,
                      LRG_TYPE_SCORING_CONTEXT, G_TYPE_INT64);

    /**
     * LrgScoringManager::round-started:
     * @self: the manager
     * @target_score: the score needed to win
     *
     * Emitted when a round starts.
     *
     * Since: 1.0
     */
    signals[SIGNAL_ROUND_STARTED] =
        g_signal_new ("round-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_INT64);

    /**
     * LrgScoringManager::round-ended:
     * @self: the manager
     * @victory: whether the round was won
     * @final_score: the final score
     *
     * Emitted when a round ends.
     *
     * Since: 1.0
     */
    signals[SIGNAL_ROUND_ENDED] =
        g_signal_new ("round-ended",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 2,
                      G_TYPE_BOOLEAN, G_TYPE_INT64);
}

static void
lrg_scoring_manager_init (LrgScoringManager *self)
{
    LrgScoringManagerPrivate *priv = lrg_scoring_manager_get_instance_private (self);

    priv->rules = NULL;
    priv->evaluator = lrg_scoring_hand_new ();
    priv->max_jokers = 5;
    priv->round_active = FALSE;
    priv->current_score = 0;
    priv->target_score = 0;
    priv->hands_remaining = 0;
    priv->discards_remaining = 0;
    priv->phase = LRG_SCORING_PHASE_SETUP;
    priv->jokers = g_ptr_array_new_with_free_func (g_object_unref);
    priv->last_context = NULL;
}

/**
 * lrg_scoring_manager_new:
 *
 * Creates a new scoring manager.
 *
 * Returns: (transfer full): A new #LrgScoringManager
 *
 * Since: 1.0
 */
LrgScoringManager *
lrg_scoring_manager_new (void)
{
    return g_object_new (LRG_TYPE_SCORING_MANAGER, NULL);
}

/**
 * lrg_scoring_manager_get_default:
 *
 * Gets the default scoring manager singleton.
 *
 * Returns: (transfer none): the default #LrgScoringManager
 *
 * Since: 1.0
 */
LrgScoringManager *
lrg_scoring_manager_get_default (void)
{
    if (default_manager == NULL)
    {
        default_manager = lrg_scoring_manager_new ();
    }

    return default_manager;
}

/**
 * lrg_scoring_manager_set_rules:
 * @self: a #LrgScoringManager
 * @rules: (transfer none): the scoring rules to use
 *
 * Sets the scoring rules implementation.
 *
 * Since: 1.0
 */
void
lrg_scoring_manager_set_rules (LrgScoringManager *self,
                               LrgScoringRules   *rules)
{
    LrgScoringManagerPrivate *priv;

    g_return_if_fail (LRG_IS_SCORING_MANAGER (self));

    priv = lrg_scoring_manager_get_instance_private (self);

    g_set_object (&priv->rules, rules);
}

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
LrgScoringRules *
lrg_scoring_manager_get_rules (LrgScoringManager *self)
{
    LrgScoringManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCORING_MANAGER (self), NULL);

    priv = lrg_scoring_manager_get_instance_private (self);
    return priv->rules;
}

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
void
lrg_scoring_manager_start_round (LrgScoringManager *self,
                                 gint64             target_score,
                                 gint               hands,
                                 gint               discards)
{
    LrgScoringManagerPrivate *priv;

    g_return_if_fail (LRG_IS_SCORING_MANAGER (self));
    g_return_if_fail (target_score > 0);
    g_return_if_fail (hands > 0);

    priv = lrg_scoring_manager_get_instance_private (self);

    priv->round_active = TRUE;
    priv->current_score = 0;
    priv->target_score = target_score;
    priv->hands_remaining = hands;
    priv->discards_remaining = discards;
    priv->phase = LRG_SCORING_PHASE_SELECT;

    g_clear_object (&priv->last_context);

    g_signal_emit (self, signals[SIGNAL_ROUND_STARTED], 0, target_score);

    lrg_debug (LRG_LOG_DOMAIN,
               "Round started: target=%ld, hands=%d, discards=%d",
               target_score, hands, discards);
}

/**
 * lrg_scoring_manager_end_round:
 * @self: a #LrgScoringManager
 *
 * Ends the current round.
 *
 * Since: 1.0
 */
void
lrg_scoring_manager_end_round (LrgScoringManager *self)
{
    LrgScoringManagerPrivate *priv;
    LrgScoringManagerClass *klass;
    gboolean victory;

    g_return_if_fail (LRG_IS_SCORING_MANAGER (self));

    priv = lrg_scoring_manager_get_instance_private (self);

    if (!priv->round_active)
        return;

    victory = priv->current_score >= priv->target_score;
    priv->round_active = FALSE;
    priv->phase = LRG_SCORING_PHASE_FINISHED;

    klass = LRG_SCORING_MANAGER_GET_CLASS (self);
    if (klass->on_round_complete != NULL)
        klass->on_round_complete (self, victory);

    g_signal_emit (self, signals[SIGNAL_ROUND_ENDED], 0,
                   victory, priv->current_score);

    lrg_debug (LRG_LOG_DOMAIN,
               "Round ended: %s (score: %ld / %ld)",
               victory ? "VICTORY" : "DEFEAT",
               priv->current_score, priv->target_score);
}

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
gboolean
lrg_scoring_manager_is_round_active (LrgScoringManager *self)
{
    LrgScoringManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCORING_MANAGER (self), FALSE);

    priv = lrg_scoring_manager_get_instance_private (self);
    return priv->round_active;
}

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
gint64
lrg_scoring_manager_get_current_score (LrgScoringManager *self)
{
    LrgScoringManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCORING_MANAGER (self), 0);

    priv = lrg_scoring_manager_get_instance_private (self);
    return priv->current_score;
}

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
gint64
lrg_scoring_manager_get_target_score (LrgScoringManager *self)
{
    LrgScoringManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCORING_MANAGER (self), 0);

    priv = lrg_scoring_manager_get_instance_private (self);
    return priv->target_score;
}

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
gint
lrg_scoring_manager_get_hands_remaining (LrgScoringManager *self)
{
    LrgScoringManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCORING_MANAGER (self), 0);

    priv = lrg_scoring_manager_get_instance_private (self);
    return priv->hands_remaining;
}

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
gint
lrg_scoring_manager_get_discards_remaining (LrgScoringManager *self)
{
    LrgScoringManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCORING_MANAGER (self), 0);

    priv = lrg_scoring_manager_get_instance_private (self);
    return priv->discards_remaining;
}

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
LrgScoringPhase
lrg_scoring_manager_get_phase (LrgScoringManager *self)
{
    LrgScoringManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCORING_MANAGER (self), LRG_SCORING_PHASE_SETUP);

    priv = lrg_scoring_manager_get_instance_private (self);
    return priv->phase;
}

/**
 * lrg_scoring_manager_add_joker:
 * @self: a #LrgScoringManager
 * @joker: (transfer full): the joker to add
 *
 * Adds a joker to the active jokers.
 *
 * Since: 1.0
 */
void
lrg_scoring_manager_add_joker (LrgScoringManager *self,
                               LrgJokerInstance  *joker)
{
    LrgScoringManagerPrivate *priv;

    g_return_if_fail (LRG_IS_SCORING_MANAGER (self));
    g_return_if_fail (LRG_IS_JOKER_INSTANCE (joker));

    priv = lrg_scoring_manager_get_instance_private (self);

    if ((gint)priv->jokers->len >= priv->max_jokers)
    {
        lrg_debug (LRG_LOG_DOMAIN,
                   "Cannot add joker: at max capacity (%d)",
                   priv->max_jokers);
        g_object_unref (joker);
        return;
    }

    g_ptr_array_add (priv->jokers, joker);

    lrg_debug (LRG_LOG_DOMAIN,
               "Added joker '%s' (%d/%d slots)",
               lrg_joker_instance_get_name (joker),
               priv->jokers->len, priv->max_jokers);
}

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
gboolean
lrg_scoring_manager_remove_joker (LrgScoringManager *self,
                                  LrgJokerInstance  *joker)
{
    LrgScoringManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCORING_MANAGER (self), FALSE);
    g_return_val_if_fail (LRG_IS_JOKER_INSTANCE (joker), FALSE);

    priv = lrg_scoring_manager_get_instance_private (self);

    return g_ptr_array_remove (priv->jokers, joker);
}

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
GPtrArray *
lrg_scoring_manager_get_jokers (LrgScoringManager *self)
{
    LrgScoringManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCORING_MANAGER (self), NULL);

    priv = lrg_scoring_manager_get_instance_private (self);
    return priv->jokers;
}

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
gint
lrg_scoring_manager_get_max_jokers (LrgScoringManager *self)
{
    LrgScoringManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCORING_MANAGER (self), 0);

    priv = lrg_scoring_manager_get_instance_private (self);
    return priv->max_jokers;
}

/**
 * lrg_scoring_manager_set_max_jokers:
 * @self: a #LrgScoringManager
 * @max: the maximum joker slots
 *
 * Sets the maximum number of jokers allowed.
 *
 * Since: 1.0
 */
void
lrg_scoring_manager_set_max_jokers (LrgScoringManager *self,
                                    gint               max)
{
    LrgScoringManagerPrivate *priv;

    g_return_if_fail (LRG_IS_SCORING_MANAGER (self));
    g_return_if_fail (max > 0);

    priv = lrg_scoring_manager_get_instance_private (self);
    priv->max_jokers = max;
}

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
gint64
lrg_scoring_manager_play_hand (LrgScoringManager *self,
                               GPtrArray         *cards)
{
    LrgScoringManagerPrivate *priv;
    LrgScoringManagerClass *klass;
    LrgScoringContext *ctx;
    LrgHandType hand_type;
    gint64 base_chips;
    gint64 base_mult;
    gint64 score;
    guint i;

    g_return_val_if_fail (LRG_IS_SCORING_MANAGER (self), 0);
    g_return_val_if_fail (cards != NULL, 0);

    priv = lrg_scoring_manager_get_instance_private (self);

    if (!priv->round_active)
    {
        lrg_debug (LRG_LOG_DOMAIN, "Cannot play hand: no active round");
        return 0;
    }

    if (priv->hands_remaining <= 0)
    {
        lrg_debug (LRG_LOG_DOMAIN, "Cannot play hand: no hands remaining");
        return 0;
    }

    /* Create scoring context */
    ctx = lrg_scoring_context_new ();

    /* Evaluate hand */
    lrg_scoring_hand_set_cards (priv->evaluator, cards);
    hand_type = lrg_scoring_hand_evaluate (priv->evaluator);

    lrg_scoring_context_set_hand_type (ctx, hand_type);
    lrg_scoring_context_set_scoring_cards (ctx,
        lrg_scoring_hand_get_scoring_cards (priv->evaluator));

    /* Get base values from rules */
    if (priv->rules != NULL)
    {
        base_chips = lrg_scoring_rules_get_base_chips (priv->rules, hand_type);
        base_mult = lrg_scoring_rules_get_base_mult (priv->rules, hand_type);
    }
    else
    {
        /* Use default Balatro-style base values */
        base_chips = get_default_base_chips (hand_type);
        base_mult = get_default_base_mult (hand_type);
    }

    lrg_scoring_context_set_base_chips (ctx, base_chips);
    lrg_scoring_context_set_base_mult (ctx, base_mult);

    /* Add chips from scoring cards */
    for (i = 0; i < cards->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (cards, i);
        gint card_chips = lrg_card_instance_get_total_chip_value (card);
        lrg_scoring_context_add_chips (ctx, card_chips);
    }

    /* Apply joker effects */
    lrg_scoring_context_set_jokers (ctx, priv->jokers);
    for (i = 0; i < priv->jokers->len; i++)
    {
        LrgJokerInstance *joker = g_ptr_array_index (priv->jokers, i);
        LrgJokerDef *def = lrg_joker_instance_get_def (joker);

        if (lrg_joker_def_can_trigger (def, ctx, joker))
        {
            lrg_joker_def_apply_effect (def, ctx, joker);
            lrg_joker_instance_increment_trigger_count (joker);

            /* Apply edition bonuses */
            lrg_scoring_context_add_chips (ctx,
                lrg_joker_instance_get_edition_chips (joker));
            lrg_scoring_context_add_mult (ctx,
                lrg_joker_instance_get_edition_mult (joker));

            if (lrg_joker_instance_get_edition_x_mult (joker) > 1.0)
            {
                lrg_scoring_context_apply_x_mult (ctx,
                    lrg_joker_instance_get_edition_x_mult (joker));
            }
        }
    }

    /* Calculate final score */
    score = lrg_scoring_context_calculate_score (ctx);

    /* Update round state */
    priv->current_score += score;
    priv->hands_remaining--;

    /* Store context for inspection */
    g_clear_object (&priv->last_context);
    priv->last_context = ctx;

    /* Emit signal and call virtual method */
    klass = LRG_SCORING_MANAGER_GET_CLASS (self);
    if (klass->on_hand_scored != NULL)
        klass->on_hand_scored (self, ctx);

    g_signal_emit (self, signals[SIGNAL_HAND_SCORED], 0, ctx, score);

    lrg_debug (LRG_LOG_DOMAIN,
               "Played hand: %s = %ld (total: %ld/%ld)",
               priv->rules != NULL ?
                   lrg_scoring_rules_get_hand_name (priv->rules, hand_type) :
                   "Unknown",
               score, priv->current_score, priv->target_score);

    /* Check for round end */
    if (priv->current_score >= priv->target_score)
    {
        lrg_scoring_manager_end_round (self);
    }
    else if (priv->hands_remaining <= 0)
    {
        lrg_scoring_manager_end_round (self);
    }

    return score;
}

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
gboolean
lrg_scoring_manager_discard (LrgScoringManager *self,
                             GPtrArray         *cards)
{
    LrgScoringManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCORING_MANAGER (self), FALSE);
    g_return_val_if_fail (cards != NULL, FALSE);

    priv = lrg_scoring_manager_get_instance_private (self);

    if (!priv->round_active)
    {
        lrg_debug (LRG_LOG_DOMAIN, "Cannot discard: no active round");
        return FALSE;
    }

    if (priv->discards_remaining <= 0)
    {
        lrg_debug (LRG_LOG_DOMAIN, "Cannot discard: no discards remaining");
        return FALSE;
    }

    priv->discards_remaining--;

    lrg_debug (LRG_LOG_DOMAIN,
               "Discarded %u cards (%d discards remaining)",
               cards->len, priv->discards_remaining);

    return TRUE;
}

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
LrgHandType
lrg_scoring_manager_evaluate_hand (LrgScoringManager *self,
                                   GPtrArray         *cards)
{
    LrgScoringManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCORING_MANAGER (self), LRG_HAND_TYPE_NONE);
    g_return_val_if_fail (cards != NULL, LRG_HAND_TYPE_NONE);

    priv = lrg_scoring_manager_get_instance_private (self);

    lrg_scoring_hand_set_cards (priv->evaluator, cards);
    return lrg_scoring_hand_evaluate (priv->evaluator);
}

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
gint64
lrg_scoring_manager_preview_score (LrgScoringManager *self,
                                   GPtrArray         *cards)
{
    LrgScoringManagerPrivate *priv;
    g_autoptr(LrgScoringContext) ctx = NULL;
    LrgHandType hand_type;
    gint64 base_chips;
    gint64 base_mult;
    guint i;

    g_return_val_if_fail (LRG_IS_SCORING_MANAGER (self), 0);
    g_return_val_if_fail (cards != NULL, 0);

    priv = lrg_scoring_manager_get_instance_private (self);

    /* Evaluate hand */
    lrg_scoring_hand_set_cards (priv->evaluator, cards);
    hand_type = lrg_scoring_hand_evaluate (priv->evaluator);

    /* Create preview context */
    ctx = lrg_scoring_context_new ();
    lrg_scoring_context_set_hand_type (ctx, hand_type);

    /* Get base values */
    if (priv->rules != NULL)
    {
        base_chips = lrg_scoring_rules_get_base_chips (priv->rules, hand_type);
        base_mult = lrg_scoring_rules_get_base_mult (priv->rules, hand_type);
    }
    else
    {
        /* Use default Balatro-style base values */
        base_chips = get_default_base_chips (hand_type);
        base_mult = get_default_base_mult (hand_type);
    }

    lrg_scoring_context_set_base_chips (ctx, base_chips);
    lrg_scoring_context_set_base_mult (ctx, base_mult);

    /* Add card chips */
    for (i = 0; i < cards->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (cards, i);
        gint card_chips = lrg_card_instance_get_total_chip_value (card);
        lrg_scoring_context_add_chips (ctx, card_chips);
    }

    /* Note: We don't apply joker effects in preview for simplicity */

    return lrg_scoring_context_calculate_score (ctx);
}

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
LrgScoringContext *
lrg_scoring_manager_get_last_context (LrgScoringManager *self)
{
    LrgScoringManagerPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCORING_MANAGER (self), NULL);

    priv = lrg_scoring_manager_get_instance_private (self);
    return priv->last_context;
}
