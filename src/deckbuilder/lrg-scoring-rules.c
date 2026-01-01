/* lrg-scoring-rules.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-scoring-rules.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DECKBUILDER

/**
 * LrgScoringRules:
 *
 * Interface for scoring rule systems.
 *
 * The scoring rules interface defines how poker hands are evaluated
 * and scored. Implementations can customize:
 *
 * - Base chip values for each hand type
 * - Base multiplier values for each hand type
 * - Hand levels (for upgrade systems)
 * - Hand evaluation logic
 * - Final score calculation
 *
 * This is inspired by Balatro's scoring system where hands have
 * base chips and mult that can be modified by jokers, enhancements,
 * and other game effects.
 *
 * Since: 1.0
 */

G_DEFINE_INTERFACE (LrgScoringRules, lrg_scoring_rules, G_TYPE_OBJECT)

/*
 * Default arrays indexed by LrgHandType enum value.
 * NONE = 0, HIGH_CARD = 1, etc.
 */

/* Default hand names */
static const gchar *default_hand_names[] = {
    "None",           /* NONE */
    "High Card",      /* HIGH_CARD */
    "Pair",           /* PAIR */
    "Two Pair",       /* TWO_PAIR */
    "Three of a Kind",/* THREE_OF_A_KIND */
    "Straight",       /* STRAIGHT */
    "Flush",          /* FLUSH */
    "Full House",     /* FULL_HOUSE */
    "Four of a Kind", /* FOUR_OF_A_KIND */
    "Straight Flush", /* STRAIGHT_FLUSH */
    "Royal Flush",    /* ROYAL_FLUSH */
    "Five of a Kind", /* FIVE_OF_A_KIND */
    "Flush House",    /* FLUSH_HOUSE */
    "Flush Five"      /* FLUSH_FIVE */
};

/* Default base chips (Balatro-inspired) */
static const gint64 default_base_chips[] = {
    0,    /* None */
    5,    /* High Card */
    10,   /* Pair */
    20,   /* Two Pair */
    30,   /* Three of a Kind */
    30,   /* Straight */
    35,   /* Flush */
    40,   /* Full House */
    60,   /* Four of a Kind */
    100,  /* Straight Flush */
    100,  /* Royal Flush */
    120,  /* Five of a Kind */
    140,  /* Flush House */
    160   /* Flush Five */
};

/* Default base mult (Balatro-inspired) */
static const gint64 default_base_mult[] = {
    0,    /* None (invalid) */
    1,    /* High Card */
    2,    /* Pair */
    2,    /* Two Pair */
    3,    /* Three of a Kind */
    4,    /* Straight */
    4,    /* Flush */
    4,    /* Full House */
    7,    /* Four of a Kind */
    8,    /* Straight Flush */
    8,    /* Royal Flush */
    12,   /* Five of a Kind */
    14,   /* Flush House */
    16    /* Flush Five */
};

static gint64
lrg_scoring_rules_default_get_base_chips (LrgScoringRules *self,
                                          LrgHandType      hand_type)
{
    if (hand_type < 0 || hand_type > LRG_HAND_TYPE_FLUSH_FIVE)
        return 0;

    return default_base_chips[hand_type];
}

static gint64
lrg_scoring_rules_default_get_base_mult (LrgScoringRules *self,
                                         LrgHandType      hand_type)
{
    if (hand_type < 0 || hand_type > LRG_HAND_TYPE_FLUSH_FIVE)
        return 1;

    return default_base_mult[hand_type];
}

static gint
lrg_scoring_rules_default_get_hand_level (LrgScoringRules *self,
                                          LrgHandType      hand_type)
{
    /* Default: all hands at level 1 */
    return 1;
}

static LrgHandType
lrg_scoring_rules_default_evaluate_hand (LrgScoringRules *self,
                                         GPtrArray       *cards,
                                         GPtrArray      **scoring_cards)
{
    /*
     * Default implementation returns HIGH_CARD.
     * Actual hand evaluation is done in LrgScoringHand.
     */
    if (scoring_cards != NULL)
        *scoring_cards = NULL;

    return LRG_HAND_TYPE_HIGH_CARD;
}

static gint64
lrg_scoring_rules_default_calculate_score (LrgScoringRules *self,
                                           gint64           chips,
                                           gint64           mult)
{
    /* Default: chips * mult */
    return chips * mult;
}

static const gchar *
lrg_scoring_rules_default_get_hand_name (LrgScoringRules *self,
                                         LrgHandType      hand_type)
{
    if (hand_type < 0 || hand_type > LRG_HAND_TYPE_FLUSH_FIVE)
        return "Unknown";

    return default_hand_names[hand_type];
}

static void
lrg_scoring_rules_default_init (LrgScoringRulesInterface *iface)
{
    iface->get_base_chips = lrg_scoring_rules_default_get_base_chips;
    iface->get_base_mult = lrg_scoring_rules_default_get_base_mult;
    iface->get_hand_level = lrg_scoring_rules_default_get_hand_level;
    iface->evaluate_hand = lrg_scoring_rules_default_evaluate_hand;
    iface->calculate_score = lrg_scoring_rules_default_calculate_score;
    iface->get_hand_name = lrg_scoring_rules_default_get_hand_name;
}

/**
 * lrg_scoring_rules_get_base_chips:
 * @self: a #LrgScoringRules
 * @hand_type: the poker hand type
 *
 * Gets the base chip value for a hand type.
 *
 * Returns: base chips for the hand type
 *
 * Since: 1.0
 */
gint64
lrg_scoring_rules_get_base_chips (LrgScoringRules *self,
                                  LrgHandType      hand_type)
{
    LrgScoringRulesInterface *iface;

    g_return_val_if_fail (LRG_IS_SCORING_RULES (self), 0);

    iface = LRG_SCORING_RULES_GET_IFACE (self);
    g_return_val_if_fail (iface->get_base_chips != NULL, 0);

    return iface->get_base_chips (self, hand_type);
}

/**
 * lrg_scoring_rules_get_base_mult:
 * @self: a #LrgScoringRules
 * @hand_type: the poker hand type
 *
 * Gets the base multiplier for a hand type.
 *
 * Returns: base multiplier for the hand type
 *
 * Since: 1.0
 */
gint64
lrg_scoring_rules_get_base_mult (LrgScoringRules *self,
                                 LrgHandType      hand_type)
{
    LrgScoringRulesInterface *iface;

    g_return_val_if_fail (LRG_IS_SCORING_RULES (self), 1);

    iface = LRG_SCORING_RULES_GET_IFACE (self);
    g_return_val_if_fail (iface->get_base_mult != NULL, 1);

    return iface->get_base_mult (self, hand_type);
}

/**
 * lrg_scoring_rules_get_hand_level:
 * @self: a #LrgScoringRules
 * @hand_type: the poker hand type
 *
 * Gets the current level of a hand type.
 * Higher levels grant bonus chips/mult.
 *
 * Returns: level of the hand type (1 = base)
 *
 * Since: 1.0
 */
gint
lrg_scoring_rules_get_hand_level (LrgScoringRules *self,
                                  LrgHandType      hand_type)
{
    LrgScoringRulesInterface *iface;

    g_return_val_if_fail (LRG_IS_SCORING_RULES (self), 1);

    iface = LRG_SCORING_RULES_GET_IFACE (self);
    g_return_val_if_fail (iface->get_hand_level != NULL, 1);

    return iface->get_hand_level (self, hand_type);
}

/**
 * lrg_scoring_rules_evaluate_hand:
 * @self: a #LrgScoringRules
 * @cards: (element-type LrgCardInstance): array of cards to evaluate
 * @scoring_cards: (out) (optional) (element-type LrgCardInstance): cards that score
 *
 * Evaluates cards to determine the best poker hand.
 * Optionally returns which cards contribute to the hand.
 *
 * Returns: the hand type formed by the cards
 *
 * Since: 1.0
 */
LrgHandType
lrg_scoring_rules_evaluate_hand (LrgScoringRules *self,
                                 GPtrArray       *cards,
                                 GPtrArray      **scoring_cards)
{
    LrgScoringRulesInterface *iface;

    g_return_val_if_fail (LRG_IS_SCORING_RULES (self), LRG_HAND_TYPE_HIGH_CARD);
    g_return_val_if_fail (cards != NULL, LRG_HAND_TYPE_HIGH_CARD);

    iface = LRG_SCORING_RULES_GET_IFACE (self);
    g_return_val_if_fail (iface->evaluate_hand != NULL, LRG_HAND_TYPE_HIGH_CARD);

    return iface->evaluate_hand (self, cards, scoring_cards);
}

/**
 * lrg_scoring_rules_calculate_score:
 * @self: a #LrgScoringRules
 * @chips: total chip value
 * @mult: total multiplier value
 *
 * Calculates final score from chips and mult.
 * Default is chips * mult.
 *
 * Returns: final score
 *
 * Since: 1.0
 */
gint64
lrg_scoring_rules_calculate_score (LrgScoringRules *self,
                                   gint64           chips,
                                   gint64           mult)
{
    LrgScoringRulesInterface *iface;

    g_return_val_if_fail (LRG_IS_SCORING_RULES (self), 0);

    iface = LRG_SCORING_RULES_GET_IFACE (self);
    g_return_val_if_fail (iface->calculate_score != NULL, 0);

    return iface->calculate_score (self, chips, mult);
}

/**
 * lrg_scoring_rules_get_hand_name:
 * @self: a #LrgScoringRules
 * @hand_type: the poker hand type
 *
 * Gets the display name for a hand type.
 *
 * Returns: (transfer none): display name for the hand type
 *
 * Since: 1.0
 */
const gchar *
lrg_scoring_rules_get_hand_name (LrgScoringRules *self,
                                 LrgHandType      hand_type)
{
    LrgScoringRulesInterface *iface;

    g_return_val_if_fail (LRG_IS_SCORING_RULES (self), "Unknown");

    iface = LRG_SCORING_RULES_GET_IFACE (self);
    g_return_val_if_fail (iface->get_hand_name != NULL, "Unknown");

    return iface->get_hand_name (self, hand_type);
}
