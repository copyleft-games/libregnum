/* lrg-template-difficulty.h - Dynamic difficulty adjustment interface
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_TEMPLATE_DIFFICULTY_H
#define LRG_TEMPLATE_DIFFICULTY_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_TEMPLATE_DIFFICULTY (lrg_template_difficulty_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgTemplateDifficulty, lrg_template_difficulty,
                     LRG, TEMPLATE_DIFFICULTY, GObject)

/**
 * SECTION:lrg-template-difficulty
 * @title: LrgTemplateDifficulty
 * @short_description: Interface for dynamic difficulty adjustment
 *
 * #LrgTemplateDifficulty is an interface for implementing dynamic difficulty
 * adjustment (DDA) in games. It tracks player performance and adjusts
 * a difficulty modifier to keep the game challenging but fair.
 *
 * ## How It Works
 *
 * The system uses a performance score (0.0 to 1.0) that represents
 * how well the player is doing:
 * - 0.0 = Struggling (dying frequently, failing objectives)
 * - 0.5 = Balanced (appropriate challenge level)
 * - 1.0 = Dominating (never dying, completing objectives easily)
 *
 * Based on this score, a difficulty modifier is calculated:
 * - Below 0.5: Game gets easier (modifier < 1.0)
 * - At 0.5: No change (modifier = 1.0)
 * - Above 0.5: Game gets harder (modifier > 1.0)
 *
 * ## Implementing the Interface
 *
 * |[<!-- language="C" -->
 * struct _MyGameState
 * {
 *     LrgGameState parent_instance;
 *
 *     gdouble adaptation_speed;
 *     gdouble difficulty_floor;
 *     gdouble difficulty_ceiling;
 *     gdouble current_modifier;
 *
 *     // Performance tracking
 *     gdouble success_sum;
 *     gdouble failure_sum;
 *     gdouble total_weight;
 * };
 *
 * static gdouble
 * my_game_state_get_performance_score (LrgTemplateDifficulty *difficulty)
 * {
 *     MyGameState *self = MY_GAME_STATE (difficulty);
 *     if (self->total_weight <= 0.0)
 *         return 0.5;  // Neutral if no data
 *     return self->success_sum / self->total_weight;
 * }
 *
 * static gdouble
 * my_game_state_get_difficulty_modifier (LrgTemplateDifficulty *difficulty)
 * {
 *     MyGameState *self = MY_GAME_STATE (difficulty);
 *     return self->current_modifier;
 * }
 *
 * static void
 * my_game_state_record_player_success (LrgTemplateDifficulty *difficulty,
 *                                       gdouble                weight)
 * {
 *     MyGameState *self = MY_GAME_STATE (difficulty);
 *     self->success_sum += weight;
 *     self->total_weight += weight;
 *     update_modifier (self);
 * }
 *
 * static void
 * my_game_state_record_player_failure (LrgTemplateDifficulty *difficulty,
 *                                       gdouble                weight)
 * {
 *     MyGameState *self = MY_GAME_STATE (difficulty);
 *     self->failure_sum += weight;
 *     self->total_weight += weight;
 *     update_modifier (self);
 * }
 *
 * static void
 * difficulty_iface_init (LrgTemplateDifficultyInterface *iface)
 * {
 *     iface->get_performance_score = my_game_state_get_performance_score;
 *     iface->get_difficulty_modifier = my_game_state_get_difficulty_modifier;
 *     iface->record_player_success = my_game_state_record_player_success;
 *     iface->record_player_failure = my_game_state_record_player_failure;
 * }
 * ]|
 *
 * ## Using the Modifier
 *
 * Apply the difficulty modifier to game parameters:
 *
 * |[<!-- language="C" -->
 * // Enemy stats
 * gdouble modifier = lrg_template_difficulty_get_difficulty_modifier (
 *     LRG_TEMPLATE_DIFFICULTY (state));
 * enemy->health *= modifier;
 * enemy->damage *= modifier;
 * enemy->speed *= (modifier * 0.5 + 0.5);  // Less aggressive speed scaling
 *
 * // Player benefits (inverse scaling)
 * player->regen_rate *= (2.0 - modifier);
 * ]|
 *
 * ## Recording Events
 *
 * Record successes and failures with appropriate weights:
 *
 * |[<!-- language="C" -->
 * // Player killed an enemy
 * lrg_template_difficulty_record_player_success (
 *     LRG_TEMPLATE_DIFFICULTY (state), 1.0);
 *
 * // Player killed a boss (bigger impact)
 * lrg_template_difficulty_record_player_success (
 *     LRG_TEMPLATE_DIFFICULTY (state), 5.0);
 *
 * // Player died
 * lrg_template_difficulty_record_player_failure (
 *     LRG_TEMPLATE_DIFFICULTY (state), 3.0);
 *
 * // Player took damage
 * lrg_template_difficulty_record_player_failure (
 *     LRG_TEMPLATE_DIFFICULTY (state), 0.5);
 * ]|
 */

struct _LrgTemplateDifficultyInterface
{
    GTypeInterface parent_iface;

    /* Required: Get current performance score (0.0 = struggling, 1.0 = dominating) */
    gdouble (*get_performance_score) (LrgTemplateDifficulty *self);

    /* Required: Get current difficulty modifier */
    gdouble (*get_difficulty_modifier) (LrgTemplateDifficulty *self);

    /* Required: Record a success event with weight */
    void (*record_player_success) (LrgTemplateDifficulty *self,
                                   gdouble                weight);

    /* Required: Record a failure event with weight */
    void (*record_player_failure) (LrgTemplateDifficulty *self,
                                   gdouble                weight);

    /* Optional: Reset performance window (e.g., on level change) */
    void (*reset_performance_window) (LrgTemplateDifficulty *self);

    /* Optional: Called when difficulty modifier changes significantly */
    void (*on_difficulty_changed) (LrgTemplateDifficulty *self,
                                   gdouble                old_modifier,
                                   gdouble                new_modifier);

    /* Reserved for future expansion */
    gpointer _reserved[6];
};

/* ==========================================================================
 * Core Methods
 * ========================================================================== */

/**
 * lrg_template_difficulty_get_performance_score:
 * @self: an #LrgTemplateDifficulty
 *
 * Gets the current performance score.
 *
 * The score ranges from 0.0 (struggling) to 1.0 (dominating),
 * with 0.5 representing balanced performance.
 *
 * Returns: Performance score between 0.0 and 1.0
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_template_difficulty_get_performance_score (LrgTemplateDifficulty *self);

/**
 * lrg_template_difficulty_get_difficulty_modifier:
 * @self: an #LrgTemplateDifficulty
 *
 * Gets the current difficulty modifier.
 *
 * This value can be applied to game parameters:
 * - Values < 1.0: Game is easier
 * - Value = 1.0: No modification
 * - Values > 1.0: Game is harder
 *
 * The modifier is clamped between the floor and ceiling
 * properties of the implementation.
 *
 * Returns: Difficulty modifier (typically 0.5 to 2.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_template_difficulty_get_difficulty_modifier (LrgTemplateDifficulty *self);

/**
 * lrg_template_difficulty_record_player_success:
 * @self: an #LrgTemplateDifficulty
 * @weight: importance of the success (1.0 = normal, higher = more impact)
 *
 * Records a player success event.
 *
 * Call this when the player accomplishes something:
 * - Killing an enemy (weight: 1.0)
 * - Killing a boss (weight: 3.0-5.0)
 * - Completing an objective (weight: 2.0)
 * - Completing level without damage (weight: 5.0)
 *
 * Higher weights have more impact on the performance score.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_difficulty_record_player_success (LrgTemplateDifficulty *self,
                                                gdouble                weight);

/**
 * lrg_template_difficulty_record_player_failure:
 * @self: an #LrgTemplateDifficulty
 * @weight: importance of the failure (1.0 = normal, higher = more impact)
 *
 * Records a player failure event.
 *
 * Call this when the player fails or takes a setback:
 * - Taking damage (weight: 0.5)
 * - Dying (weight: 3.0-5.0)
 * - Failing an objective (weight: 2.0)
 * - Running out of resources (weight: 1.0)
 *
 * Higher weights have more impact on the performance score.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_difficulty_record_player_failure (LrgTemplateDifficulty *self,
                                                gdouble                weight);

/**
 * lrg_template_difficulty_reset_performance_window:
 * @self: an #LrgTemplateDifficulty
 *
 * Resets the performance tracking window.
 *
 * Call this when transitioning to a new level or game phase.
 * The difficulty modifier is preserved, but the performance
 * score starts fresh.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_difficulty_reset_performance_window (LrgTemplateDifficulty *self);

/* ==========================================================================
 * Utility Methods
 * ========================================================================== */

/**
 * lrg_template_difficulty_is_player_struggling:
 * @self: an #LrgTemplateDifficulty
 *
 * Checks if the player appears to be struggling.
 *
 * Returns %TRUE if performance score is below 0.35.
 *
 * Returns: %TRUE if player is struggling
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_difficulty_is_player_struggling (LrgTemplateDifficulty *self);

/**
 * lrg_template_difficulty_is_player_dominating:
 * @self: an #LrgTemplateDifficulty
 *
 * Checks if the player appears to be dominating.
 *
 * Returns %TRUE if performance score is above 0.65.
 *
 * Returns: %TRUE if player is dominating
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_difficulty_is_player_dominating (LrgTemplateDifficulty *self);

/**
 * lrg_template_difficulty_get_performance_label:
 * @self: an #LrgTemplateDifficulty
 *
 * Gets a human-readable label for current performance.
 *
 * Returns one of: "Struggling", "Below Average", "Balanced",
 * "Above Average", or "Dominating".
 *
 * Returns: (transfer none): Performance label string
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_template_difficulty_get_performance_label (LrgTemplateDifficulty *self);

G_END_DECLS

#endif /* LRG_TEMPLATE_DIFFICULTY_H */
