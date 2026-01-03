/* lrg-template-daily-rewards.c - Daily/weekly reward system interface
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-template-daily-rewards.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TEMPLATE
#include "../lrg-log.h"

/* ==========================================================================
 * Constants
 * ========================================================================== */

/* Time constants in seconds */
#define SECONDS_PER_DAY     (24 * 60 * 60)
#define STREAK_EXPIRE_HOURS 48
#define STREAK_EXPIRE_SECS  (STREAK_EXPIRE_HOURS * 60 * 60)

/* Streak bonus constants */
#define STREAK_BONUS_PER_DAY 0.1
#define STREAK_BONUS_CAP     3.0

/* HMAC secret for streak validation (in production, use a better secret) */
static const guint32 STREAK_HMAC_SECRET = 0xDEADBEEF;

/* ==========================================================================
 * LrgDailyRewardState (GBoxed)
 * ========================================================================== */

G_DEFINE_BOXED_TYPE (LrgDailyRewardState, lrg_daily_reward_state,
                     lrg_daily_reward_state_copy, lrg_daily_reward_state_free)

/**
 * lrg_daily_reward_state_new:
 *
 * Creates a new #LrgDailyRewardState with default values.
 *
 * Returns: (transfer full): A new #LrgDailyRewardState
 */
LrgDailyRewardState *
lrg_daily_reward_state_new (void)
{
    LrgDailyRewardState *state;

    state = g_new0 (LrgDailyRewardState, 1);
    state->last_claim_timestamp = 0;
    state->last_session_timestamp = 0;
    state->current_streak = 0;
    state->max_streak = 0;
    state->streak_hash = 0;

    return state;
}

/**
 * lrg_daily_reward_state_copy:
 * @state: an #LrgDailyRewardState
 *
 * Creates a copy of @state.
 *
 * Returns: (transfer full): A copy of @state
 */
LrgDailyRewardState *
lrg_daily_reward_state_copy (const LrgDailyRewardState *state)
{
    LrgDailyRewardState *copy;

    g_return_val_if_fail (state != NULL, NULL);

    copy = g_new (LrgDailyRewardState, 1);
    copy->last_claim_timestamp = state->last_claim_timestamp;
    copy->last_session_timestamp = state->last_session_timestamp;
    copy->current_streak = state->current_streak;
    copy->max_streak = state->max_streak;
    copy->streak_hash = state->streak_hash;

    return copy;
}

/**
 * lrg_daily_reward_state_free:
 * @state: an #LrgDailyRewardState
 *
 * Frees @state.
 */
void
lrg_daily_reward_state_free (LrgDailyRewardState *state)
{
    g_free (state);
}

/* ==========================================================================
 * HMAC Validation Helpers
 * ========================================================================== */

static guint32
compute_streak_hash (const LrgDailyRewardState *state)
{
    /*
     * Simple HMAC-like hash for streak validation.
     * This is not cryptographically secure but catches casual tampering.
     * For production games with real-money rewards, use a proper HMAC.
     */
    guint32 hash;

    hash = STREAK_HMAC_SECRET;
    hash ^= (guint32) (state->last_claim_timestamp & 0xFFFFFFFF);
    hash ^= (guint32) ((state->last_claim_timestamp >> 32) & 0xFFFFFFFF);
    hash ^= (guint32) state->current_streak * 0x1337;
    hash ^= (guint32) state->max_streak * 0x7331;
    hash = (hash << 13) | (hash >> 19);  /* Rotate */
    hash ^= STREAK_HMAC_SECRET;

    return hash;
}

static gboolean
validate_streak_hash (const LrgDailyRewardState *state)
{
    /* Fresh state (never claimed) is always valid */
    if (state->current_streak == 0 && state->last_claim_timestamp == 0)
        return TRUE;

    return state->streak_hash == compute_streak_hash (state);
}

static void
update_streak_hash (LrgDailyRewardState *state)
{
    state->streak_hash = compute_streak_hash (state);
}

/* ==========================================================================
 * Interface Implementation
 * ========================================================================== */

G_DEFINE_INTERFACE (LrgTemplateDailyRewards, lrg_template_daily_rewards, G_TYPE_OBJECT)

static void
lrg_template_daily_rewards_default_init (LrgTemplateDailyRewardsInterface *iface)
{
    /* No default implementations for required methods */
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_template_daily_rewards_get_daily_reward_state:
 * @self: an #LrgTemplateDailyRewards
 *
 * Gets the current daily reward state.
 *
 * Returns: (transfer none): The #LrgDailyRewardState
 */
LrgDailyRewardState *
lrg_template_daily_rewards_get_daily_reward_state (LrgTemplateDailyRewards *self)
{
    LrgTemplateDailyRewardsInterface *iface;

    g_return_val_if_fail (LRG_IS_TEMPLATE_DAILY_REWARDS (self), NULL);

    iface = LRG_TEMPLATE_DAILY_REWARDS_GET_IFACE (self);

    g_return_val_if_fail (iface->get_daily_reward_state != NULL, NULL);

    return iface->get_daily_reward_state (self);
}

/**
 * lrg_template_daily_rewards_can_claim:
 * @self: an #LrgTemplateDailyRewards
 *
 * Checks if a daily reward can be claimed.
 *
 * Returns: %TRUE if reward can be claimed
 */
gboolean
lrg_template_daily_rewards_can_claim (LrgTemplateDailyRewards *self)
{
    LrgDailyRewardState *state;
    gint64 now;
    gint64 time_since_claim;

    g_return_val_if_fail (LRG_IS_TEMPLATE_DAILY_REWARDS (self), FALSE);

    state = lrg_template_daily_rewards_get_daily_reward_state (self);
    if (state == NULL)
        return FALSE;

    now = g_get_real_time () / G_USEC_PER_SEC;

    /* Layer 1: Basic 24-hour check */
    time_since_claim = now - state->last_claim_timestamp;
    if (time_since_claim < SECONDS_PER_DAY)
        return FALSE;  /* Less than 24 hours */

    /* Layer 2: Clock rollback detection */
    if (now < state->last_session_timestamp)
    {
        lrg_warning (LRG_LOG_DOMAIN, "Clock rollback detected, daily reward denied");
        return FALSE;
    }

    /* Layer 3: Validate streak with HMAC */
    if (!validate_streak_hash (state))
    {
        lrg_debug (LRG_LOG_DOMAIN, "Streak validation failed, will reset on claim");
        /* Don't deny claim, but streak will be reset */
    }

    return TRUE;
}

/**
 * lrg_template_daily_rewards_claim:
 * @self: an #LrgTemplateDailyRewards
 *
 * Claims the daily reward.
 *
 * Returns: The streak day (1 for first day, 2 for second, etc.)
 */
gint
lrg_template_daily_rewards_claim (LrgTemplateDailyRewards *self)
{
    LrgTemplateDailyRewardsInterface *iface;
    LrgDailyRewardState *state;
    gint64 now;
    gint64 time_since_claim;
    gint previous_streak;
    gboolean streak_broken;

    g_return_val_if_fail (LRG_IS_TEMPLATE_DAILY_REWARDS (self), 0);

    if (!lrg_template_daily_rewards_can_claim (self))
        return 0;

    state = lrg_template_daily_rewards_get_daily_reward_state (self);
    if (state == NULL)
        return 0;

    iface = LRG_TEMPLATE_DAILY_REWARDS_GET_IFACE (self);

    now = g_get_real_time () / G_USEC_PER_SEC;
    time_since_claim = now - state->last_claim_timestamp;
    previous_streak = state->current_streak;

    /* Check if streak was broken (more than 48 hours) or hash invalid */
    streak_broken = (time_since_claim > STREAK_EXPIRE_SECS) ||
                    !validate_streak_hash (state);

    if (streak_broken && previous_streak > 0)
    {
        /* Notify about streak break */
        if (iface->on_streak_broken != NULL)
            iface->on_streak_broken (self, previous_streak);

        state->current_streak = 0;
    }

    /* Update state */
    state->current_streak++;
    state->last_claim_timestamp = now;

    if (state->current_streak > state->max_streak)
        state->max_streak = state->current_streak;

    /* Update HMAC */
    update_streak_hash (state);

    /* Notify about claim */
    if (iface->on_daily_reward_claimed != NULL)
        iface->on_daily_reward_claimed (self, state->current_streak);

    lrg_debug (LRG_LOG_DOMAIN, "Daily reward claimed, streak: %d (max: %d)",
               state->current_streak, state->max_streak);

    return state->current_streak;
}

/**
 * lrg_template_daily_rewards_get_streak_bonus_multiplier:
 * @self: an #LrgTemplateDailyRewards
 *
 * Gets a bonus multiplier based on current streak.
 *
 * Returns: Multiplier value (1.0 or higher)
 */
gdouble
lrg_template_daily_rewards_get_streak_bonus_multiplier (LrgTemplateDailyRewards *self)
{
    LrgDailyRewardState *state;
    gdouble multiplier;

    g_return_val_if_fail (LRG_IS_TEMPLATE_DAILY_REWARDS (self), 1.0);

    state = lrg_template_daily_rewards_get_daily_reward_state (self);
    if (state == NULL)
        return 1.0;

    /* Formula: 1.0 + (streak * 0.1), capped at 3.0 */
    multiplier = 1.0 + (state->current_streak * STREAK_BONUS_PER_DAY);
    if (multiplier > STREAK_BONUS_CAP)
        multiplier = STREAK_BONUS_CAP;

    return multiplier;
}

/**
 * lrg_template_daily_rewards_get_current_streak:
 * @self: an #LrgTemplateDailyRewards
 *
 * Gets the current streak day count.
 *
 * Returns: Number of consecutive days claimed
 */
gint
lrg_template_daily_rewards_get_current_streak (LrgTemplateDailyRewards *self)
{
    LrgDailyRewardState *state;

    g_return_val_if_fail (LRG_IS_TEMPLATE_DAILY_REWARDS (self), 0);

    state = lrg_template_daily_rewards_get_daily_reward_state (self);
    if (state == NULL)
        return 0;

    return state->current_streak;
}

/**
 * lrg_template_daily_rewards_get_max_streak:
 * @self: an #LrgTemplateDailyRewards
 *
 * Gets the highest streak achieved.
 *
 * Returns: Maximum consecutive days ever claimed
 */
gint
lrg_template_daily_rewards_get_max_streak (LrgTemplateDailyRewards *self)
{
    LrgDailyRewardState *state;

    g_return_val_if_fail (LRG_IS_TEMPLATE_DAILY_REWARDS (self), 0);

    state = lrg_template_daily_rewards_get_daily_reward_state (self);
    if (state == NULL)
        return 0;

    return state->max_streak;
}

/**
 * lrg_template_daily_rewards_get_time_until_claim:
 * @self: an #LrgTemplateDailyRewards
 *
 * Gets seconds remaining until next claim is available.
 *
 * Returns: Seconds until claimable, or 0 if already claimable
 */
gint64
lrg_template_daily_rewards_get_time_until_claim (LrgTemplateDailyRewards *self)
{
    LrgDailyRewardState *state;
    gint64 now;
    gint64 next_claim_time;
    gint64 remaining;

    g_return_val_if_fail (LRG_IS_TEMPLATE_DAILY_REWARDS (self), 0);

    state = lrg_template_daily_rewards_get_daily_reward_state (self);
    if (state == NULL)
        return 0;

    /* Never claimed = can claim now */
    if (state->last_claim_timestamp == 0)
        return 0;

    now = g_get_real_time () / G_USEC_PER_SEC;
    next_claim_time = state->last_claim_timestamp + SECONDS_PER_DAY;
    remaining = next_claim_time - now;

    return (remaining > 0) ? remaining : 0;
}

/**
 * lrg_template_daily_rewards_get_time_until_streak_expires:
 * @self: an #LrgTemplateDailyRewards
 *
 * Gets seconds remaining until streak expires (48h window).
 *
 * Returns: Seconds until streak expires, or 0 if already expired
 */
gint64
lrg_template_daily_rewards_get_time_until_streak_expires (LrgTemplateDailyRewards *self)
{
    LrgDailyRewardState *state;
    gint64 now;
    gint64 expire_time;
    gint64 remaining;

    g_return_val_if_fail (LRG_IS_TEMPLATE_DAILY_REWARDS (self), 0);

    state = lrg_template_daily_rewards_get_daily_reward_state (self);
    if (state == NULL)
        return 0;

    /* No streak = nothing to expire */
    if (state->current_streak == 0 || state->last_claim_timestamp == 0)
        return 0;

    now = g_get_real_time () / G_USEC_PER_SEC;
    expire_time = state->last_claim_timestamp + STREAK_EXPIRE_SECS;
    remaining = expire_time - now;

    return (remaining > 0) ? remaining : 0;
}

/**
 * lrg_template_daily_rewards_session_start:
 * @self: an #LrgTemplateDailyRewards
 *
 * Records a session start timestamp.
 */
void
lrg_template_daily_rewards_session_start (LrgTemplateDailyRewards *self)
{
    LrgDailyRewardState *state;

    g_return_if_fail (LRG_IS_TEMPLATE_DAILY_REWARDS (self));

    state = lrg_template_daily_rewards_get_daily_reward_state (self);
    if (state == NULL)
        return;

    state->last_session_timestamp = g_get_real_time () / G_USEC_PER_SEC;

    lrg_debug (LRG_LOG_DOMAIN, "Daily rewards session started, timestamp: %" G_GINT64_FORMAT,
               state->last_session_timestamp);
}
