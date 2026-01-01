/* lrg-scoring-rules.h
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

G_BEGIN_DECLS

#define LRG_TYPE_SCORING_RULES (lrg_scoring_rules_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgScoringRules, lrg_scoring_rules, LRG, SCORING_RULES, GObject)

/**
 * LrgScoringRulesInterface:
 * @get_base_chips: Get base chips for a hand type
 * @get_base_mult: Get base multiplier for a hand type
 * @get_hand_level: Get the level of a hand type (for upgrades)
 * @evaluate_hand: Evaluate cards to determine hand type
 * @calculate_score: Calculate final score from chips and mult
 * @get_hand_name: Get display name for a hand type
 *
 * Interface for scoring rule systems.
 *
 * Implementations can define custom hand evaluations,
 * base values, and scoring calculations.
 *
 * Since: 1.0
 */
struct _LrgScoringRulesInterface
{
    GTypeInterface parent_iface;

    /**
     * LrgScoringRulesInterface::get_base_chips:
     * @self: a #LrgScoringRules
     * @hand_type: the poker hand type
     *
     * Gets the base chip value for a hand type.
     *
     * Returns: base chips for the hand type
     *
     * Since: 1.0
     */
    gint64 (*get_base_chips) (LrgScoringRules *self,
                              LrgHandType      hand_type);

    /**
     * LrgScoringRulesInterface::get_base_mult:
     * @self: a #LrgScoringRules
     * @hand_type: the poker hand type
     *
     * Gets the base multiplier for a hand type.
     *
     * Returns: base multiplier for the hand type
     *
     * Since: 1.0
     */
    gint64 (*get_base_mult) (LrgScoringRules *self,
                             LrgHandType      hand_type);

    /**
     * LrgScoringRulesInterface::get_hand_level:
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
    gint (*get_hand_level) (LrgScoringRules *self,
                            LrgHandType      hand_type);

    /**
     * LrgScoringRulesInterface::evaluate_hand:
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
    LrgHandType (*evaluate_hand) (LrgScoringRules *self,
                                  GPtrArray       *cards,
                                  GPtrArray      **scoring_cards);

    /**
     * LrgScoringRulesInterface::calculate_score:
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
    gint64 (*calculate_score) (LrgScoringRules *self,
                               gint64           chips,
                               gint64           mult);

    /**
     * LrgScoringRulesInterface::get_hand_name:
     * @self: a #LrgScoringRules
     * @hand_type: the poker hand type
     *
     * Gets the display name for a hand type.
     *
     * Returns: (transfer none): display name for the hand type
     *
     * Since: 1.0
     */
    const gchar * (*get_hand_name) (LrgScoringRules *self,
                                    LrgHandType      hand_type);

    /*< private >*/
    gpointer _reserved[8];
};

LRG_AVAILABLE_IN_ALL
gint64
lrg_scoring_rules_get_base_chips (LrgScoringRules *self,
                                  LrgHandType      hand_type);

LRG_AVAILABLE_IN_ALL
gint64
lrg_scoring_rules_get_base_mult (LrgScoringRules *self,
                                 LrgHandType      hand_type);

LRG_AVAILABLE_IN_ALL
gint
lrg_scoring_rules_get_hand_level (LrgScoringRules *self,
                                  LrgHandType      hand_type);

LRG_AVAILABLE_IN_ALL
LrgHandType
lrg_scoring_rules_evaluate_hand (LrgScoringRules *self,
                                 GPtrArray       *cards,
                                 GPtrArray      **scoring_cards);

LRG_AVAILABLE_IN_ALL
gint64
lrg_scoring_rules_calculate_score (LrgScoringRules *self,
                                   gint64           chips,
                                   gint64           mult);

LRG_AVAILABLE_IN_ALL
const gchar *
lrg_scoring_rules_get_hand_name (LrgScoringRules *self,
                                 LrgHandType      hand_type);

G_END_DECLS
