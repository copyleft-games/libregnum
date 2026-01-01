/* lrg-scoring-hand.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-scoring-hand.h"
#include "lrg-card-def.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DECKBUILDER

/**
 * LrgScoringHand:
 *
 * Evaluates poker hands for scoring deckbuilders.
 *
 * The scoring hand takes a set of cards and determines the best
 * poker hand that can be formed. It tracks which cards contribute
 * to the hand (scoring cards) for chip calculation.
 *
 * Supported hand types (in order of strength):
 * - High Card
 * - Pair
 * - Two Pair
 * - Three of a Kind
 * - Straight
 * - Flush
 * - Full House
 * - Four of a Kind
 * - Straight Flush
 * - Royal Flush
 * - Five of a Kind (requires wild cards)
 * - Flush House (Balatro special)
 * - Flush Five (Balatro special)
 *
 * Since: 1.0
 */

typedef struct
{
    GPtrArray   *cards;          /* Current cards to evaluate */
    GPtrArray   *scoring_cards;  /* Cards that contribute to hand */
    LrgHandType  hand_type;      /* Result of last evaluation */
    gint         rank_counts[14]; /* Count of each rank (1-13, 0 unused) */
    gint         suit_counts[5];  /* Count of each suit */
    gboolean     has_wild;        /* Whether any card is wild */
} LrgScoringHandPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgScoringHand, lrg_scoring_hand, G_TYPE_OBJECT)

/* Chip values for each rank (Balatro-style) */
static const gint chip_values[] = {
    0,   /* NONE */
    11,  /* ACE */
    2,   /* TWO */
    3,   /* THREE */
    4,   /* FOUR */
    5,   /* FIVE */
    6,   /* SIX */
    7,   /* SEVEN */
    8,   /* EIGHT */
    9,   /* NINE */
    10,  /* TEN */
    10,  /* JACK */
    10,  /* QUEEN */
    10   /* KING */
};

/* Forward declarations for helper functions */
static void count_ranks_and_suits (LrgScoringHand *self);
static gboolean check_flush (LrgScoringHand *self, LrgCardSuit *flush_suit);
static gboolean check_straight (LrgScoringHand *self, gint *high_rank);
static void find_scoring_cards_for_hand (LrgScoringHand *self, LrgHandType type);

static void
lrg_scoring_hand_finalize (GObject *object)
{
    LrgScoringHand *self = LRG_SCORING_HAND (object);
    LrgScoringHandPrivate *priv = lrg_scoring_hand_get_instance_private (self);

    g_clear_pointer (&priv->cards, g_ptr_array_unref);
    g_clear_pointer (&priv->scoring_cards, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_scoring_hand_parent_class)->finalize (object);
}

static LrgHandType
lrg_scoring_hand_real_evaluate (LrgScoringHand *self)
{
    LrgScoringHandPrivate *priv = lrg_scoring_hand_get_instance_private (self);
    gint pairs = 0;
    gint three_of_kind = 0;
    gint four_of_kind = 0;
    gint five_of_kind = 0;
    gboolean is_flush = FALSE;
    gboolean is_straight = FALSE;
    LrgCardSuit flush_suit = LRG_CARD_SUIT_NONE;
    gint straight_high = 0;
    gint i;

    if (priv->cards == NULL || priv->cards->len == 0)
    {
        priv->hand_type = LRG_HAND_TYPE_NONE;
        return priv->hand_type;
    }

    /* Count ranks and suits */
    count_ranks_and_suits (self);

    /* Count pairs, three of a kind, etc. */
    for (i = 1; i <= 13; i++)
    {
        switch (priv->rank_counts[i])
        {
        case 2:
            pairs++;
            break;
        case 3:
            three_of_kind++;
            break;
        case 4:
            four_of_kind++;
            break;
        case 5:
            five_of_kind++;
            break;
        }
    }

    /* Check for flush and straight */
    is_flush = check_flush (self, &flush_suit);
    is_straight = check_straight (self, &straight_high);

    /* Determine hand type (check from highest to lowest) */

    /* Flush Five: 5 of a kind, all same suit */
    if (five_of_kind > 0 && is_flush)
    {
        priv->hand_type = LRG_HAND_TYPE_FLUSH_FIVE;
        find_scoring_cards_for_hand (self, priv->hand_type);
        return priv->hand_type;
    }

    /* Flush House: full house, all same suit */
    if (three_of_kind > 0 && pairs > 0 && is_flush)
    {
        priv->hand_type = LRG_HAND_TYPE_FLUSH_HOUSE;
        find_scoring_cards_for_hand (self, priv->hand_type);
        return priv->hand_type;
    }

    /* Five of a Kind */
    if (five_of_kind > 0)
    {
        priv->hand_type = LRG_HAND_TYPE_FIVE_OF_A_KIND;
        find_scoring_cards_for_hand (self, priv->hand_type);
        return priv->hand_type;
    }

    /* Royal Flush: A-K-Q-J-10 of same suit */
    if (is_flush && is_straight && straight_high == 14)
    {
        priv->hand_type = LRG_HAND_TYPE_ROYAL_FLUSH;
        find_scoring_cards_for_hand (self, priv->hand_type);
        return priv->hand_type;
    }

    /* Straight Flush */
    if (is_flush && is_straight)
    {
        priv->hand_type = LRG_HAND_TYPE_STRAIGHT_FLUSH;
        find_scoring_cards_for_hand (self, priv->hand_type);
        return priv->hand_type;
    }

    /* Four of a Kind */
    if (four_of_kind > 0)
    {
        priv->hand_type = LRG_HAND_TYPE_FOUR_OF_A_KIND;
        find_scoring_cards_for_hand (self, priv->hand_type);
        return priv->hand_type;
    }

    /* Full House */
    if (three_of_kind > 0 && pairs > 0)
    {
        priv->hand_type = LRG_HAND_TYPE_FULL_HOUSE;
        find_scoring_cards_for_hand (self, priv->hand_type);
        return priv->hand_type;
    }

    /* Flush */
    if (is_flush)
    {
        priv->hand_type = LRG_HAND_TYPE_FLUSH;
        find_scoring_cards_for_hand (self, priv->hand_type);
        return priv->hand_type;
    }

    /* Straight */
    if (is_straight)
    {
        priv->hand_type = LRG_HAND_TYPE_STRAIGHT;
        find_scoring_cards_for_hand (self, priv->hand_type);
        return priv->hand_type;
    }

    /* Three of a Kind */
    if (three_of_kind > 0)
    {
        priv->hand_type = LRG_HAND_TYPE_THREE_OF_A_KIND;
        find_scoring_cards_for_hand (self, priv->hand_type);
        return priv->hand_type;
    }

    /* Two Pair */
    if (pairs >= 2)
    {
        priv->hand_type = LRG_HAND_TYPE_TWO_PAIR;
        find_scoring_cards_for_hand (self, priv->hand_type);
        return priv->hand_type;
    }

    /* Pair */
    if (pairs == 1)
    {
        priv->hand_type = LRG_HAND_TYPE_PAIR;
        find_scoring_cards_for_hand (self, priv->hand_type);
        return priv->hand_type;
    }

    /* High Card */
    priv->hand_type = LRG_HAND_TYPE_HIGH_CARD;
    find_scoring_cards_for_hand (self, priv->hand_type);
    return priv->hand_type;
}

static void
lrg_scoring_hand_class_init (LrgScoringHandClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_scoring_hand_finalize;

    klass->evaluate = lrg_scoring_hand_real_evaluate;
}

static void
lrg_scoring_hand_init (LrgScoringHand *self)
{
    LrgScoringHandPrivate *priv = lrg_scoring_hand_get_instance_private (self);

    priv->cards = NULL;
    priv->scoring_cards = g_ptr_array_new ();
    priv->hand_type = LRG_HAND_TYPE_NONE;
    priv->has_wild = FALSE;
}

/*
 * Helper: Count the occurrence of each rank and suit
 */
static void
count_ranks_and_suits (LrgScoringHand *self)
{
    LrgScoringHandPrivate *priv = lrg_scoring_hand_get_instance_private (self);
    guint i;

    /* Reset counts */
    memset (priv->rank_counts, 0, sizeof (priv->rank_counts));
    memset (priv->suit_counts, 0, sizeof (priv->suit_counts));
    priv->has_wild = FALSE;

    for (i = 0; i < priv->cards->len; i++)
    {
        LrgCardInstance *card = g_ptr_array_index (priv->cards, i);
        LrgCardDef *def = lrg_card_instance_get_def (card);
        LrgCardRank rank = lrg_card_def_get_rank (def);
        LrgCardSuit suit = lrg_card_def_get_suit (def);

        if (rank > 0 && rank <= 13)
            priv->rank_counts[rank]++;

        if (suit > 0 && suit <= 4)
            priv->suit_counts[suit]++;

        /* TODO: Check for wild card enhancement */
    }
}

/*
 * Helper: Check if cards form a flush (5+ same suit)
 */
static gboolean
check_flush (LrgScoringHand *self, LrgCardSuit *flush_suit)
{
    LrgScoringHandPrivate *priv = lrg_scoring_hand_get_instance_private (self);
    gint i;

    for (i = 1; i <= 4; i++)
    {
        if (priv->suit_counts[i] >= 5)
        {
            if (flush_suit != NULL)
                *flush_suit = (LrgCardSuit)i;
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * Helper: Check if cards form a straight (5 consecutive ranks)
 */
static gboolean
check_straight (LrgScoringHand *self, gint *high_rank)
{
    LrgScoringHandPrivate *priv = lrg_scoring_hand_get_instance_private (self);
    gint consecutive = 0;
    gint i;

    /*
     * Check for regular straight (2-A).
     * Ace can be high (10-J-Q-K-A) or low (A-2-3-4-5).
     */

    /* Check high straight ending at Ace (10-A) */
    for (i = 10; i <= 13; i++)
    {
        if (priv->rank_counts[i] > 0)
            consecutive++;
        else
            break;
    }
    if (consecutive == 4 && priv->rank_counts[1] > 0)
    {
        /* Ace-high straight (Royal) */
        if (high_rank != NULL)
            *high_rank = 14;
        return TRUE;
    }

    /* Check for regular straights */
    consecutive = 0;
    for (i = 1; i <= 13; i++)
    {
        if (priv->rank_counts[i] > 0)
        {
            consecutive++;
            if (consecutive >= 5)
            {
                if (high_rank != NULL)
                    *high_rank = i;
                return TRUE;
            }
        }
        else
        {
            consecutive = 0;
        }
    }

    /* Check for low straight (A-2-3-4-5) */
    if (priv->rank_counts[1] > 0 &&  /* Ace */
        priv->rank_counts[2] > 0 &&
        priv->rank_counts[3] > 0 &&
        priv->rank_counts[4] > 0 &&
        priv->rank_counts[5] > 0)
    {
        if (high_rank != NULL)
            *high_rank = 5;
        return TRUE;
    }

    return FALSE;
}

/*
 * Helper: Find which cards contribute to the hand type.
 * This populates the scoring_cards array.
 */
static void
find_scoring_cards_for_hand (LrgScoringHand *self, LrgHandType type)
{
    LrgScoringHandPrivate *priv = lrg_scoring_hand_get_instance_private (self);
    guint i;

    g_ptr_array_set_size (priv->scoring_cards, 0);

    /*
     * For simplicity, we include all cards in scoring_cards.
     * A more sophisticated implementation would only include
     * the cards that actually contribute to the hand.
     */
    for (i = 0; i < priv->cards->len; i++)
    {
        g_ptr_array_add (priv->scoring_cards,
                         g_ptr_array_index (priv->cards, i));
    }
}

/**
 * lrg_scoring_hand_new:
 *
 * Creates a new scoring hand evaluator.
 *
 * Returns: (transfer full): A new #LrgScoringHand
 *
 * Since: 1.0
 */
LrgScoringHand *
lrg_scoring_hand_new (void)
{
    return g_object_new (LRG_TYPE_SCORING_HAND, NULL);
}

/**
 * lrg_scoring_hand_set_cards:
 * @self: a #LrgScoringHand
 * @cards: (element-type LrgCardInstance) (transfer none): the cards to evaluate
 *
 * Sets the cards to evaluate for this hand.
 *
 * Since: 1.0
 */
void
lrg_scoring_hand_set_cards (LrgScoringHand *self,
                            GPtrArray      *cards)
{
    LrgScoringHandPrivate *priv;

    g_return_if_fail (LRG_IS_SCORING_HAND (self));

    priv = lrg_scoring_hand_get_instance_private (self);

    if (priv->cards != NULL)
        g_ptr_array_unref (priv->cards);

    priv->cards = cards != NULL ? g_ptr_array_ref (cards) : NULL;
    priv->hand_type = LRG_HAND_TYPE_NONE;
    g_ptr_array_set_size (priv->scoring_cards, 0);
}

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
GPtrArray *
lrg_scoring_hand_get_cards (LrgScoringHand *self)
{
    LrgScoringHandPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCORING_HAND (self), NULL);

    priv = lrg_scoring_hand_get_instance_private (self);
    return priv->cards;
}

/**
 * lrg_scoring_hand_clear_cards:
 * @self: a #LrgScoringHand
 *
 * Clears all cards from the hand.
 *
 * Since: 1.0
 */
void
lrg_scoring_hand_clear_cards (LrgScoringHand *self)
{
    LrgScoringHandPrivate *priv;

    g_return_if_fail (LRG_IS_SCORING_HAND (self));

    priv = lrg_scoring_hand_get_instance_private (self);

    g_clear_pointer (&priv->cards, g_ptr_array_unref);
    g_ptr_array_set_size (priv->scoring_cards, 0);
    priv->hand_type = LRG_HAND_TYPE_NONE;
}

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
LrgHandType
lrg_scoring_hand_evaluate (LrgScoringHand *self)
{
    LrgScoringHandClass *klass;

    g_return_val_if_fail (LRG_IS_SCORING_HAND (self), LRG_HAND_TYPE_NONE);

    klass = LRG_SCORING_HAND_GET_CLASS (self);
    g_return_val_if_fail (klass->evaluate != NULL, LRG_HAND_TYPE_NONE);

    return klass->evaluate (self);
}

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
LrgHandType
lrg_scoring_hand_get_hand_type (LrgScoringHand *self)
{
    LrgScoringHandPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCORING_HAND (self), LRG_HAND_TYPE_NONE);

    priv = lrg_scoring_hand_get_instance_private (self);
    return priv->hand_type;
}

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
GPtrArray *
lrg_scoring_hand_get_scoring_cards (LrgScoringHand *self)
{
    LrgScoringHandPrivate *priv;

    g_return_val_if_fail (LRG_IS_SCORING_HAND (self), NULL);

    priv = lrg_scoring_hand_get_instance_private (self);
    return priv->scoring_cards;
}

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
gint
lrg_scoring_hand_get_rank_value (LrgCardRank rank)
{
    switch (rank)
    {
    case LRG_CARD_RANK_ACE:
        return 14;  /* Ace high */
    case LRG_CARD_RANK_TWO:
        return 2;
    case LRG_CARD_RANK_THREE:
        return 3;
    case LRG_CARD_RANK_FOUR:
        return 4;
    case LRG_CARD_RANK_FIVE:
        return 5;
    case LRG_CARD_RANK_SIX:
        return 6;
    case LRG_CARD_RANK_SEVEN:
        return 7;
    case LRG_CARD_RANK_EIGHT:
        return 8;
    case LRG_CARD_RANK_NINE:
        return 9;
    case LRG_CARD_RANK_TEN:
        return 10;
    case LRG_CARD_RANK_JACK:
        return 11;
    case LRG_CARD_RANK_QUEEN:
        return 12;
    case LRG_CARD_RANK_KING:
        return 13;
    case LRG_CARD_RANK_NONE:
    default:
        return 0;
    }
}

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
gint
lrg_scoring_hand_get_chip_value (LrgCardRank rank)
{
    if (rank < 0 || rank > LRG_CARD_RANK_KING)
        return 0;

    return chip_values[rank];
}
