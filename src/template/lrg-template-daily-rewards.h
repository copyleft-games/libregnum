/* lrg-template-daily-rewards.h - Daily/weekly reward system interface
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_TEMPLATE_DAILY_REWARDS_H
#define LRG_TEMPLATE_DAILY_REWARDS_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Daily Reward State (GBoxed)
 * ========================================================================== */

#define LRG_TYPE_DAILY_REWARD_STATE (lrg_daily_reward_state_get_type ())

/**
 * LrgDailyRewardState:
 * @last_claim_timestamp: Unix timestamp of last reward claim
 * @last_session_timestamp: Unix timestamp of last session start (for rollback detection)
 * @current_streak: Current consecutive day streak
 * @max_streak: Highest streak achieved
 * @streak_hash: HMAC for validation (anti-tampering)
 *
 * State data for tracking daily reward progress and streak.
 *
 * This is a GBoxed type used to persist daily reward state.
 * Implementers of #LrgTemplateDailyRewards should store this
 * in a saveable location.
 *
 * Since: 1.0
 */
typedef struct _LrgDailyRewardState LrgDailyRewardState;

struct _LrgDailyRewardState
{
    gint64  last_claim_timestamp;
    gint64  last_session_timestamp;
    gint    current_streak;
    gint    max_streak;
    guint32 streak_hash;
};

LRG_AVAILABLE_IN_ALL
GType lrg_daily_reward_state_get_type (void) G_GNUC_CONST;

LRG_AVAILABLE_IN_ALL
LrgDailyRewardState *
lrg_daily_reward_state_new (void);

LRG_AVAILABLE_IN_ALL
LrgDailyRewardState *
lrg_daily_reward_state_copy (const LrgDailyRewardState *state);

LRG_AVAILABLE_IN_ALL
void
lrg_daily_reward_state_free (LrgDailyRewardState *state);

/* ==========================================================================
 * Daily Rewards Interface
 * ========================================================================== */

#define LRG_TYPE_TEMPLATE_DAILY_REWARDS (lrg_template_daily_rewards_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgTemplateDailyRewards, lrg_template_daily_rewards,
                     LRG, TEMPLATE_DAILY_REWARDS, GObject)

/**
 * SECTION:lrg-template-daily-rewards
 * @title: LrgTemplateDailyRewards
 * @short_description: Interface for daily/weekly reward systems
 *
 * #LrgTemplateDailyRewards is an interface for implementing daily login
 * rewards with streak bonuses. It provides:
 *
 * - **Time-based claiming**: 24-hour cooldown between claims
 * - **Streak tracking**: Consecutive day login bonuses
 * - **Anti-cheat measures**: Basic clock manipulation detection
 * - **Extensible rewards**: Custom reward types via subclass hooks
 *
 * ## Implementing the Interface
 *
 * To implement daily rewards in your game state:
 *
 * |[<!-- language="C" -->
 * struct _MyGameState
 * {
 *     LrgGameState parent_instance;
 *     LrgDailyRewardState *daily_state;
 * };
 *
 * static LrgDailyRewardState *
 * my_game_state_get_daily_reward_state (LrgTemplateDailyRewards *rewards)
 * {
 *     MyGameState *self = MY_GAME_STATE (rewards);
 *     return self->daily_state;
 * }
 *
 * static void
 * my_game_state_on_daily_reward_claimed (LrgTemplateDailyRewards *rewards)
 * {
 *     MyGameState *self = MY_GAME_STATE (rewards);
 *
 *     // Grant the reward based on streak
 *     gint streak = self->daily_state->current_streak;
 *     gint coins = 100 * streak;  // More coins for longer streaks
 *     add_player_coins (self->player, coins);
 * }
 *
 * static void
 * daily_rewards_iface_init (LrgTemplateDailyRewardsInterface *iface)
 * {
 *     iface->get_daily_reward_state = my_game_state_get_daily_reward_state;
 *     iface->on_daily_reward_claimed = my_game_state_on_daily_reward_claimed;
 * }
 *
 * G_DEFINE_TYPE_WITH_CODE (MyGameState, my_game_state, LRG_TYPE_GAME_STATE,
 *                          G_IMPLEMENT_INTERFACE (LRG_TYPE_TEMPLATE_DAILY_REWARDS,
 *                                                  daily_rewards_iface_init))
 * ]|
 *
 * ## Usage
 *
 * |[<!-- language="C" -->
 * // Check if player can claim daily reward
 * if (lrg_template_daily_rewards_can_claim (LRG_TEMPLATE_DAILY_REWARDS (state)))
 * {
 *     // Show claim button
 *     show_daily_reward_popup ();
 * }
 *
 * // When player clicks claim
 * lrg_template_daily_rewards_claim (LRG_TEMPLATE_DAILY_REWARDS (state));
 *
 * // Get streak bonus for other rewards
 * gdouble bonus = lrg_template_daily_rewards_get_streak_bonus_multiplier (
 *     LRG_TEMPLATE_DAILY_REWARDS (state));
 * final_reward = base_reward * bonus;
 * ]|
 *
 * ## Anti-Cheat Notes
 *
 * The default implementation includes basic anti-cheat measures:
 *
 * - Clock rollback detection (denies claim if time went backwards)
 * - HMAC validation of streak data
 * - 48-hour streak break threshold
 *
 * However, **perfect anti-cheat is impossible for offline games**.
 * For high-stakes rewards, consider server-side validation.
 */

struct _LrgTemplateDailyRewardsInterface
{
    GTypeInterface parent_iface;

    /* Required: Get the reward state (must be stored by implementer) */
    LrgDailyRewardState * (*get_daily_reward_state) (LrgTemplateDailyRewards *self);

    /* Optional: Called when a reward is successfully claimed */
    void (*on_daily_reward_claimed) (LrgTemplateDailyRewards *self,
                                     gint                     streak_day);

    /* Optional: Called when streak is broken (more than 48h since last claim) */
    void (*on_streak_broken) (LrgTemplateDailyRewards *self,
                              gint                     previous_streak);

    /* Reserved for future expansion */
    gpointer _reserved[8];
};

/* ==========================================================================
 * Core Methods
 * ========================================================================== */

/**
 * lrg_template_daily_rewards_get_daily_reward_state:
 * @self: an #LrgTemplateDailyRewards
 *
 * Gets the current daily reward state.
 *
 * Returns: (transfer none): The #LrgDailyRewardState
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgDailyRewardState *
lrg_template_daily_rewards_get_daily_reward_state (LrgTemplateDailyRewards *self);

/**
 * lrg_template_daily_rewards_can_claim:
 * @self: an #LrgTemplateDailyRewards
 *
 * Checks if a daily reward can be claimed.
 *
 * Returns %TRUE if at least 24 hours have passed since the last claim
 * and no clock manipulation is detected.
 *
 * Returns: %TRUE if reward can be claimed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_daily_rewards_can_claim (LrgTemplateDailyRewards *self);

/**
 * lrg_template_daily_rewards_claim:
 * @self: an #LrgTemplateDailyRewards
 *
 * Claims the daily reward.
 *
 * Updates the state, increments streak, and calls the
 * #LrgTemplateDailyRewardsInterface.on_daily_reward_claimed() hook.
 *
 * If the streak was broken (more than 48 hours since last claim),
 * the #LrgTemplateDailyRewardsInterface.on_streak_broken() hook is
 * called first with the previous streak value.
 *
 * Returns: The streak day (1 for first day, 2 for second, etc.)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_template_daily_rewards_claim (LrgTemplateDailyRewards *self);

/**
 * lrg_template_daily_rewards_get_streak_bonus_multiplier:
 * @self: an #LrgTemplateDailyRewards
 *
 * Gets a bonus multiplier based on current streak.
 *
 * The default formula is: `1.0 + (streak * 0.1)`, capped at 3.0.
 * For example:
 * - Day 1: 1.1x
 * - Day 5: 1.5x
 * - Day 10: 2.0x
 * - Day 20+: 3.0x (capped)
 *
 * Returns: Multiplier value (1.0 or higher)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_template_daily_rewards_get_streak_bonus_multiplier (LrgTemplateDailyRewards *self);

/**
 * lrg_template_daily_rewards_get_current_streak:
 * @self: an #LrgTemplateDailyRewards
 *
 * Gets the current streak day count.
 *
 * Returns: Number of consecutive days claimed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_template_daily_rewards_get_current_streak (LrgTemplateDailyRewards *self);

/**
 * lrg_template_daily_rewards_get_max_streak:
 * @self: an #LrgTemplateDailyRewards
 *
 * Gets the highest streak achieved.
 *
 * Returns: Maximum consecutive days ever claimed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_template_daily_rewards_get_max_streak (LrgTemplateDailyRewards *self);

/**
 * lrg_template_daily_rewards_get_time_until_claim:
 * @self: an #LrgTemplateDailyRewards
 *
 * Gets seconds remaining until next claim is available.
 *
 * Returns: Seconds until claimable, or 0 if already claimable
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_template_daily_rewards_get_time_until_claim (LrgTemplateDailyRewards *self);

/**
 * lrg_template_daily_rewards_get_time_until_streak_expires:
 * @self: an #LrgTemplateDailyRewards
 *
 * Gets seconds remaining until streak expires (48h window).
 *
 * Returns: Seconds until streak expires, or 0 if already expired
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_template_daily_rewards_get_time_until_streak_expires (LrgTemplateDailyRewards *self);

/* ==========================================================================
 * Session Management
 * ========================================================================== */

/**
 * lrg_template_daily_rewards_session_start:
 * @self: an #LrgTemplateDailyRewards
 *
 * Records a session start timestamp.
 *
 * Call this when the game starts or resumes. Used for clock
 * rollback detection.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_daily_rewards_session_start (LrgTemplateDailyRewards *self);

G_END_DECLS

#endif /* LRG_TEMPLATE_DAILY_REWARDS_H */
