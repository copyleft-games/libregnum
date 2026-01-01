/* lrg-scoring-context.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-scoring-context.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DECKBUILDER

/**
 * LrgScoringContext:
 *
 * Holds the state during a scoring round.
 *
 * The scoring context tracks all values that contribute to the final
 * score calculation in a Balatro-style scoring system. The formula is:
 *
 * score = total_chips * (total_mult * x_mult)
 *
 * Where:
 * - total_chips = base_chips + bonus_chips
 * - total_mult = base_mult + bonus_mult
 * - x_mult = product of all X-mult modifiers
 *
 * Since: 1.0
 */
struct _LrgScoringContext
{
    GObject parent_instance;

    /* Hand information */
    LrgHandType  hand_type;
    GPtrArray   *scoring_cards;

    /* Chip values */
    gint64       base_chips;
    gint64       bonus_chips;

    /* Mult values */
    gint64       base_mult;
    gint64       bonus_mult;

    /* X-mult (multiplicative) */
    gdouble      x_mult;

    /* Active jokers */
    GPtrArray   *jokers;

    /* Triggered effects tracking */
    GPtrArray   *triggered_cards;
};

G_DEFINE_FINAL_TYPE (LrgScoringContext, lrg_scoring_context, G_TYPE_OBJECT)

static void
lrg_scoring_context_finalize (GObject *object)
{
    LrgScoringContext *self = LRG_SCORING_CONTEXT (object);

    g_clear_pointer (&self->scoring_cards, g_ptr_array_unref);
    g_clear_pointer (&self->jokers, g_ptr_array_unref);
    g_clear_pointer (&self->triggered_cards, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_scoring_context_parent_class)->finalize (object);
}

static void
lrg_scoring_context_class_init (LrgScoringContextClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_scoring_context_finalize;
}

static void
lrg_scoring_context_init (LrgScoringContext *self)
{
    self->hand_type = LRG_HAND_TYPE_NONE;
    self->scoring_cards = g_ptr_array_new ();
    self->base_chips = 0;
    self->bonus_chips = 0;
    self->base_mult = 0;
    self->bonus_mult = 0;
    self->x_mult = 1.0;
    self->jokers = NULL;
    self->triggered_cards = g_ptr_array_new ();
}

/**
 * lrg_scoring_context_new:
 *
 * Creates a new scoring context.
 *
 * Returns: (transfer full): A new #LrgScoringContext
 *
 * Since: 1.0
 */
LrgScoringContext *
lrg_scoring_context_new (void)
{
    return g_object_new (LRG_TYPE_SCORING_CONTEXT, NULL);
}

/**
 * lrg_scoring_context_set_hand_type:
 * @self: a #LrgScoringContext
 * @hand_type: the #LrgHandType
 *
 * Sets the current hand type being scored.
 *
 * Since: 1.0
 */
void
lrg_scoring_context_set_hand_type (LrgScoringContext *self,
                                   LrgHandType        hand_type)
{
    g_return_if_fail (LRG_IS_SCORING_CONTEXT (self));

    self->hand_type = hand_type;
}

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
LrgHandType
lrg_scoring_context_get_hand_type (LrgScoringContext *self)
{
    g_return_val_if_fail (LRG_IS_SCORING_CONTEXT (self), LRG_HAND_TYPE_NONE);

    return self->hand_type;
}

/**
 * lrg_scoring_context_set_scoring_cards:
 * @self: a #LrgScoringContext
 * @cards: (element-type LrgCardInstance) (transfer none): the scoring cards
 *
 * Sets the cards that contribute to the hand score.
 *
 * Since: 1.0
 */
void
lrg_scoring_context_set_scoring_cards (LrgScoringContext *self,
                                       GPtrArray         *cards)
{
    guint i;

    g_return_if_fail (LRG_IS_SCORING_CONTEXT (self));

    g_ptr_array_set_size (self->scoring_cards, 0);

    if (cards != NULL)
    {
        for (i = 0; i < cards->len; i++)
        {
            g_ptr_array_add (self->scoring_cards,
                             g_ptr_array_index (cards, i));
        }
    }
}

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
GPtrArray *
lrg_scoring_context_get_scoring_cards (LrgScoringContext *self)
{
    g_return_val_if_fail (LRG_IS_SCORING_CONTEXT (self), NULL);

    return self->scoring_cards;
}

/**
 * lrg_scoring_context_set_base_chips:
 * @self: a #LrgScoringContext
 * @chips: the base chip value
 *
 * Sets the base chips from the hand type.
 *
 * Since: 1.0
 */
void
lrg_scoring_context_set_base_chips (LrgScoringContext *self,
                                    gint64             chips)
{
    g_return_if_fail (LRG_IS_SCORING_CONTEXT (self));

    self->base_chips = chips;
}

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
gint64
lrg_scoring_context_get_base_chips (LrgScoringContext *self)
{
    g_return_val_if_fail (LRG_IS_SCORING_CONTEXT (self), 0);

    return self->base_chips;
}

/**
 * lrg_scoring_context_add_chips:
 * @self: a #LrgScoringContext
 * @chips: chips to add
 *
 * Adds bonus chips (from cards, jokers, etc.).
 *
 * Since: 1.0
 */
void
lrg_scoring_context_add_chips (LrgScoringContext *self,
                               gint64             chips)
{
    g_return_if_fail (LRG_IS_SCORING_CONTEXT (self));

    self->bonus_chips += chips;
}

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
gint64
lrg_scoring_context_get_total_chips (LrgScoringContext *self)
{
    g_return_val_if_fail (LRG_IS_SCORING_CONTEXT (self), 0);

    return self->base_chips + self->bonus_chips;
}

/**
 * lrg_scoring_context_set_base_mult:
 * @self: a #LrgScoringContext
 * @mult: the base multiplier
 *
 * Sets the base mult from the hand type.
 *
 * Since: 1.0
 */
void
lrg_scoring_context_set_base_mult (LrgScoringContext *self,
                                   gint64             mult)
{
    g_return_if_fail (LRG_IS_SCORING_CONTEXT (self));

    self->base_mult = mult;
}

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
gint64
lrg_scoring_context_get_base_mult (LrgScoringContext *self)
{
    g_return_val_if_fail (LRG_IS_SCORING_CONTEXT (self), 0);

    return self->base_mult;
}

/**
 * lrg_scoring_context_add_mult:
 * @self: a #LrgScoringContext
 * @mult: mult to add
 *
 * Adds bonus mult (from cards, jokers, etc.).
 *
 * Since: 1.0
 */
void
lrg_scoring_context_add_mult (LrgScoringContext *self,
                              gint64             mult)
{
    g_return_if_fail (LRG_IS_SCORING_CONTEXT (self));

    self->bonus_mult += mult;
}

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
gint64
lrg_scoring_context_get_total_mult (LrgScoringContext *self)
{
    g_return_val_if_fail (LRG_IS_SCORING_CONTEXT (self), 0);

    return self->base_mult + self->bonus_mult;
}

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
void
lrg_scoring_context_apply_x_mult (LrgScoringContext *self,
                                  gdouble            x_mult)
{
    g_return_if_fail (LRG_IS_SCORING_CONTEXT (self));
    g_return_if_fail (x_mult > 0.0);

    self->x_mult *= x_mult;
}

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
gdouble
lrg_scoring_context_get_x_mult (LrgScoringContext *self)
{
    g_return_val_if_fail (LRG_IS_SCORING_CONTEXT (self), 1.0);

    return self->x_mult;
}

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
gint64
lrg_scoring_context_calculate_score (LrgScoringContext *self)
{
    gint64 total_chips;
    gint64 total_mult;
    gdouble final_mult;
    gint64 score;

    g_return_val_if_fail (LRG_IS_SCORING_CONTEXT (self), 0);

    total_chips = lrg_scoring_context_get_total_chips (self);
    total_mult = lrg_scoring_context_get_total_mult (self);
    final_mult = (gdouble)total_mult * self->x_mult;

    score = (gint64)((gdouble)total_chips * final_mult);

    lrg_debug (LRG_LOG_DOMAIN,
               "Score: %ld chips x %ld mult x %.2f x_mult = %ld",
               total_chips, total_mult, self->x_mult, score);

    return score;
}

/**
 * lrg_scoring_context_reset:
 * @self: a #LrgScoringContext
 *
 * Resets all values to their defaults for a new hand.
 *
 * Since: 1.0
 */
void
lrg_scoring_context_reset (LrgScoringContext *self)
{
    g_return_if_fail (LRG_IS_SCORING_CONTEXT (self));

    self->hand_type = LRG_HAND_TYPE_NONE;
    g_ptr_array_set_size (self->scoring_cards, 0);
    self->base_chips = 0;
    self->bonus_chips = 0;
    self->base_mult = 0;
    self->bonus_mult = 0;
    self->x_mult = 1.0;
    g_ptr_array_set_size (self->triggered_cards, 0);
}

/**
 * lrg_scoring_context_set_jokers:
 * @self: a #LrgScoringContext
 * @jokers: (element-type LrgJokerInstance) (transfer none) (nullable): active jokers
 *
 * Sets the active jokers for this scoring round.
 *
 * Since: 1.0
 */
void
lrg_scoring_context_set_jokers (LrgScoringContext *self,
                                GPtrArray         *jokers)
{
    g_return_if_fail (LRG_IS_SCORING_CONTEXT (self));

    if (self->jokers != NULL)
        g_ptr_array_unref (self->jokers);

    self->jokers = jokers != NULL ? g_ptr_array_ref (jokers) : NULL;
}

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
GPtrArray *
lrg_scoring_context_get_jokers (LrgScoringContext *self)
{
    g_return_val_if_fail (LRG_IS_SCORING_CONTEXT (self), NULL);

    return self->jokers;
}

/**
 * lrg_scoring_context_add_triggered_card:
 * @self: a #LrgScoringContext
 * @card: the card that triggered
 *
 * Records that a card's scoring ability triggered.
 *
 * Since: 1.0
 */
void
lrg_scoring_context_add_triggered_card (LrgScoringContext *self,
                                        LrgCardInstance   *card)
{
    g_return_if_fail (LRG_IS_SCORING_CONTEXT (self));
    g_return_if_fail (LRG_IS_CARD_INSTANCE (card));

    g_ptr_array_add (self->triggered_cards, card);
}

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
GPtrArray *
lrg_scoring_context_get_triggered_cards (LrgScoringContext *self)
{
    g_return_val_if_fail (LRG_IS_SCORING_CONTEXT (self), NULL);

    return self->triggered_cards;
}
