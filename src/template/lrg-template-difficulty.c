/* lrg-template-difficulty.c - Dynamic difficulty adjustment interface
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-template-difficulty.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TEMPLATE
#include "../lrg-log.h"

/* ==========================================================================
 * Constants
 * ========================================================================== */

/* Performance thresholds */
#define STRUGGLING_THRESHOLD  0.35
#define DOMINATING_THRESHOLD  0.65

/* ==========================================================================
 * Interface Implementation
 * ========================================================================== */

G_DEFINE_INTERFACE (LrgTemplateDifficulty, lrg_template_difficulty, G_TYPE_OBJECT)

static void
lrg_template_difficulty_default_init (LrgTemplateDifficultyInterface *iface)
{
    /* No default implementations - all methods are abstract */
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_template_difficulty_get_performance_score:
 * @self: an #LrgTemplateDifficulty
 *
 * Gets the current performance score.
 *
 * Returns: Performance score between 0.0 and 1.0
 */
gdouble
lrg_template_difficulty_get_performance_score (LrgTemplateDifficulty *self)
{
    LrgTemplateDifficultyInterface *iface;

    g_return_val_if_fail (LRG_IS_TEMPLATE_DIFFICULTY (self), 0.5);

    iface = LRG_TEMPLATE_DIFFICULTY_GET_IFACE (self);

    g_return_val_if_fail (iface->get_performance_score != NULL, 0.5);

    return iface->get_performance_score (self);
}

/**
 * lrg_template_difficulty_get_difficulty_modifier:
 * @self: an #LrgTemplateDifficulty
 *
 * Gets the current difficulty modifier.
 *
 * Returns: Difficulty modifier (typically 0.5 to 2.0)
 */
gdouble
lrg_template_difficulty_get_difficulty_modifier (LrgTemplateDifficulty *self)
{
    LrgTemplateDifficultyInterface *iface;

    g_return_val_if_fail (LRG_IS_TEMPLATE_DIFFICULTY (self), 1.0);

    iface = LRG_TEMPLATE_DIFFICULTY_GET_IFACE (self);

    g_return_val_if_fail (iface->get_difficulty_modifier != NULL, 1.0);

    return iface->get_difficulty_modifier (self);
}

/**
 * lrg_template_difficulty_record_player_success:
 * @self: an #LrgTemplateDifficulty
 * @weight: importance of the success
 *
 * Records a player success event.
 */
void
lrg_template_difficulty_record_player_success (LrgTemplateDifficulty *self,
                                                gdouble                weight)
{
    LrgTemplateDifficultyInterface *iface;

    g_return_if_fail (LRG_IS_TEMPLATE_DIFFICULTY (self));
    g_return_if_fail (weight > 0.0);

    iface = LRG_TEMPLATE_DIFFICULTY_GET_IFACE (self);

    if (iface->record_player_success != NULL)
    {
        iface->record_player_success (self, weight);

        lrg_debug (LRG_LOG_DOMAIN, "Recorded player success (weight: %.2f), "
                   "performance: %.2f, modifier: %.2f",
                   weight,
                   lrg_template_difficulty_get_performance_score (self),
                   lrg_template_difficulty_get_difficulty_modifier (self));
    }
}

/**
 * lrg_template_difficulty_record_player_failure:
 * @self: an #LrgTemplateDifficulty
 * @weight: importance of the failure
 *
 * Records a player failure event.
 */
void
lrg_template_difficulty_record_player_failure (LrgTemplateDifficulty *self,
                                                gdouble                weight)
{
    LrgTemplateDifficultyInterface *iface;

    g_return_if_fail (LRG_IS_TEMPLATE_DIFFICULTY (self));
    g_return_if_fail (weight > 0.0);

    iface = LRG_TEMPLATE_DIFFICULTY_GET_IFACE (self);

    if (iface->record_player_failure != NULL)
    {
        iface->record_player_failure (self, weight);

        lrg_debug (LRG_LOG_DOMAIN, "Recorded player failure (weight: %.2f), "
                   "performance: %.2f, modifier: %.2f",
                   weight,
                   lrg_template_difficulty_get_performance_score (self),
                   lrg_template_difficulty_get_difficulty_modifier (self));
    }
}

/**
 * lrg_template_difficulty_reset_performance_window:
 * @self: an #LrgTemplateDifficulty
 *
 * Resets the performance tracking window.
 */
void
lrg_template_difficulty_reset_performance_window (LrgTemplateDifficulty *self)
{
    LrgTemplateDifficultyInterface *iface;

    g_return_if_fail (LRG_IS_TEMPLATE_DIFFICULTY (self));

    iface = LRG_TEMPLATE_DIFFICULTY_GET_IFACE (self);

    if (iface->reset_performance_window != NULL)
    {
        iface->reset_performance_window (self);

        lrg_debug (LRG_LOG_DOMAIN, "Performance window reset, modifier: %.2f",
                   lrg_template_difficulty_get_difficulty_modifier (self));
    }
}

/**
 * lrg_template_difficulty_is_player_struggling:
 * @self: an #LrgTemplateDifficulty
 *
 * Checks if the player appears to be struggling.
 *
 * Returns: %TRUE if player is struggling
 */
gboolean
lrg_template_difficulty_is_player_struggling (LrgTemplateDifficulty *self)
{
    gdouble score;

    g_return_val_if_fail (LRG_IS_TEMPLATE_DIFFICULTY (self), FALSE);

    score = lrg_template_difficulty_get_performance_score (self);

    return score < STRUGGLING_THRESHOLD;
}

/**
 * lrg_template_difficulty_is_player_dominating:
 * @self: an #LrgTemplateDifficulty
 *
 * Checks if the player appears to be dominating.
 *
 * Returns: %TRUE if player is dominating
 */
gboolean
lrg_template_difficulty_is_player_dominating (LrgTemplateDifficulty *self)
{
    gdouble score;

    g_return_val_if_fail (LRG_IS_TEMPLATE_DIFFICULTY (self), FALSE);

    score = lrg_template_difficulty_get_performance_score (self);

    return score > DOMINATING_THRESHOLD;
}

/**
 * lrg_template_difficulty_get_performance_label:
 * @self: an #LrgTemplateDifficulty
 *
 * Gets a human-readable label for current performance.
 *
 * Returns: (transfer none): Performance label string
 */
const gchar *
lrg_template_difficulty_get_performance_label (LrgTemplateDifficulty *self)
{
    gdouble score;

    g_return_val_if_fail (LRG_IS_TEMPLATE_DIFFICULTY (self), "Unknown");

    score = lrg_template_difficulty_get_performance_score (self);

    if (score < 0.2)
        return "Struggling";
    else if (score < 0.4)
        return "Below Average";
    else if (score < 0.6)
        return "Balanced";
    else if (score < 0.8)
        return "Above Average";
    else
        return "Dominating";
}
