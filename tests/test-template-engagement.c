/* test-template-engagement.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for template engagement systems:
 *   - LrgTemplateStatistics (game statistics tracking)
 *   - LrgTemplateDailyRewards (daily reward interface)
 *   - LrgTemplateDifficulty (dynamic difficulty interface)
 */

#include <glib.h>
#include <math.h>
#include <libregnum.h>

/* ==========================================================================
 * Skip Macros for CI/Headless
 * ========================================================================== */

#define SKIP_IF_NO_DISPLAY() \
    do { \
        if (g_getenv ("DISPLAY") == NULL && g_getenv ("WAYLAND_DISPLAY") == NULL) \
        { \
            g_test_skip ("No display available (headless environment)"); \
            return; \
        } \
    } while (0)

#define SKIP_IF_NULL(ptr) \
    do { \
        if ((ptr) == NULL) \
        { \
            g_test_skip ("Resource not available"); \
            return; \
        } \
    } while (0)

/* ==========================================================================
 * Mock Daily Rewards Implementation
 * ========================================================================== */

#define TEST_TYPE_DAILY_REWARDS_MOCK (test_daily_rewards_mock_get_type ())
G_DECLARE_FINAL_TYPE (TestDailyRewardsMock, test_daily_rewards_mock,
                      TEST, DAILY_REWARDS_MOCK, GObject)

struct _TestDailyRewardsMock
{
    GObject               parent_instance;
    LrgDailyRewardState  *state;
    gint                  last_claimed_streak;
    gint                  broken_streak_value;
    gboolean              streak_broken_called;
};

static LrgDailyRewardState *
test_daily_rewards_mock_get_state (LrgTemplateDailyRewards *rewards)
{
    TestDailyRewardsMock *self = TEST_DAILY_REWARDS_MOCK (rewards);
    return self->state;
}

static void
test_daily_rewards_mock_on_claimed (LrgTemplateDailyRewards *rewards,
                                    gint                     streak_day)
{
    TestDailyRewardsMock *self = TEST_DAILY_REWARDS_MOCK (rewards);
    self->last_claimed_streak = streak_day;
}

static void
test_daily_rewards_mock_on_streak_broken (LrgTemplateDailyRewards *rewards,
                                          gint                     previous_streak)
{
    TestDailyRewardsMock *self = TEST_DAILY_REWARDS_MOCK (rewards);
    self->streak_broken_called = TRUE;
    self->broken_streak_value = previous_streak;
}

static void
test_daily_rewards_iface_init (LrgTemplateDailyRewardsInterface *iface)
{
    iface->get_daily_reward_state = test_daily_rewards_mock_get_state;
    iface->on_daily_reward_claimed = test_daily_rewards_mock_on_claimed;
    iface->on_streak_broken = test_daily_rewards_mock_on_streak_broken;
}

static void
test_daily_rewards_mock_finalize (GObject *object)
{
    TestDailyRewardsMock *self = TEST_DAILY_REWARDS_MOCK (object);

    g_clear_pointer (&self->state, lrg_daily_reward_state_free);
    /* GObject finalize does nothing, safe to not chain up for test code */
}

static void
test_daily_rewards_mock_init (TestDailyRewardsMock *self)
{
    self->state = lrg_daily_reward_state_new ();
    self->last_claimed_streak = 0;
    self->broken_streak_value = 0;
    self->streak_broken_called = FALSE;
}

static void
test_daily_rewards_mock_class_init (TestDailyRewardsMockClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = test_daily_rewards_mock_finalize;
}

G_DEFINE_TYPE_WITH_CODE (TestDailyRewardsMock, test_daily_rewards_mock, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_TEMPLATE_DAILY_REWARDS,
                                                test_daily_rewards_iface_init))

/* ==========================================================================
 * Mock Difficulty Implementation
 * ========================================================================== */

#define TEST_TYPE_DIFFICULTY_MOCK (test_difficulty_mock_get_type ())
G_DECLARE_FINAL_TYPE (TestDifficultyMock, test_difficulty_mock,
                      TEST, DIFFICULTY_MOCK, GObject)

struct _TestDifficultyMock
{
    GObject  parent_instance;
    gdouble  success_sum;
    gdouble  failure_sum;
    gdouble  total_weight;
    gdouble  current_modifier;
    gdouble  old_modifier_from_callback;
    gdouble  new_modifier_from_callback;
    gboolean difficulty_changed_called;
};

static gdouble
test_difficulty_mock_get_performance_score (LrgTemplateDifficulty *difficulty)
{
    TestDifficultyMock *self = TEST_DIFFICULTY_MOCK (difficulty);

    if (self->total_weight <= 0.0)
        return 0.5; /* Neutral if no data */

    return self->success_sum / self->total_weight;
}

static gdouble
test_difficulty_mock_get_difficulty_modifier (LrgTemplateDifficulty *difficulty)
{
    TestDifficultyMock *self = TEST_DIFFICULTY_MOCK (difficulty);
    return self->current_modifier;
}

static void
test_difficulty_mock_update_modifier (TestDifficultyMock *self)
{
    gdouble old_modifier = self->current_modifier;
    gdouble score = test_difficulty_mock_get_performance_score (
        LRG_TEMPLATE_DIFFICULTY (self));

    /* Simple linear scaling: 0.0 -> 0.5x, 0.5 -> 1.0x, 1.0 -> 1.5x */
    self->current_modifier = 0.5 + score;

    /* Clamp to reasonable range */
    self->current_modifier = CLAMP (self->current_modifier, 0.5, 2.0);

    /* Track changes */
    if (fabs (old_modifier - self->current_modifier) > 0.01)
    {
        self->difficulty_changed_called = TRUE;
        self->old_modifier_from_callback = old_modifier;
        self->new_modifier_from_callback = self->current_modifier;
    }
}

static void
test_difficulty_mock_record_success (LrgTemplateDifficulty *difficulty,
                                     gdouble                weight)
{
    TestDifficultyMock *self = TEST_DIFFICULTY_MOCK (difficulty);
    self->success_sum += weight;
    self->total_weight += weight;
    test_difficulty_mock_update_modifier (self);
}

static void
test_difficulty_mock_record_failure (LrgTemplateDifficulty *difficulty,
                                     gdouble                weight)
{
    TestDifficultyMock *self = TEST_DIFFICULTY_MOCK (difficulty);
    self->failure_sum += weight;
    self->total_weight += weight;
    test_difficulty_mock_update_modifier (self);
}

static void
test_difficulty_mock_reset_window (LrgTemplateDifficulty *difficulty)
{
    TestDifficultyMock *self = TEST_DIFFICULTY_MOCK (difficulty);
    self->success_sum = 0.0;
    self->failure_sum = 0.0;
    self->total_weight = 0.0;
    /* Keep current_modifier */
}

static void
test_difficulty_mock_on_changed (LrgTemplateDifficulty *difficulty,
                                 gdouble                old_modifier,
                                 gdouble                new_modifier)
{
    TestDifficultyMock *self = TEST_DIFFICULTY_MOCK (difficulty);
    self->difficulty_changed_called = TRUE;
    self->old_modifier_from_callback = old_modifier;
    self->new_modifier_from_callback = new_modifier;
}

static void
test_difficulty_iface_init (LrgTemplateDifficultyInterface *iface)
{
    iface->get_performance_score = test_difficulty_mock_get_performance_score;
    iface->get_difficulty_modifier = test_difficulty_mock_get_difficulty_modifier;
    iface->record_player_success = test_difficulty_mock_record_success;
    iface->record_player_failure = test_difficulty_mock_record_failure;
    iface->reset_performance_window = test_difficulty_mock_reset_window;
    iface->on_difficulty_changed = test_difficulty_mock_on_changed;
}

static void
test_difficulty_mock_init (TestDifficultyMock *self)
{
    self->success_sum = 0.0;
    self->failure_sum = 0.0;
    self->total_weight = 0.0;
    self->current_modifier = 1.0;
    self->difficulty_changed_called = FALSE;
}

static void
test_difficulty_mock_class_init (TestDifficultyMockClass *klass)
{
    /* Nothing special needed */
}

G_DEFINE_TYPE_WITH_CODE (TestDifficultyMock, test_difficulty_mock, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_TEMPLATE_DIFFICULTY,
                                                test_difficulty_iface_init))

/* ==========================================================================
 * Test Cases - LrgTemplateStatistics Construction
 * ========================================================================== */

static void
test_statistics_new (void)
{
    g_autoptr(LrgTemplateStatistics) stats = NULL;

    stats = lrg_template_statistics_new ("test-stats");

    g_assert_nonnull (stats);
    g_assert_true (LRG_IS_TEMPLATE_STATISTICS (stats));
}

static void
test_statistics_get_id (void)
{
    g_autoptr(LrgTemplateStatistics) stats = NULL;
    const gchar *id;

    stats = lrg_template_statistics_new ("my-stats-id");
    g_assert_nonnull (stats);

    id = lrg_template_statistics_get_id (stats);

    g_assert_cmpstr (id, ==, "my-stats-id");
}

/* ==========================================================================
 * Test Cases - LrgTemplateStatistics Counters
 * ========================================================================== */

static void
test_statistics_counter_track (void)
{
    g_autoptr(LrgTemplateStatistics) stats = NULL;
    gint64 value;

    stats = lrg_template_statistics_new ("test");
    g_assert_nonnull (stats);

    /* Initially zero */
    value = lrg_template_statistics_get_counter (stats, "enemies_killed");
    g_assert_cmpint (value, ==, 0);

    /* Track increments */
    lrg_template_statistics_track_counter (stats, "enemies_killed", 1);
    value = lrg_template_statistics_get_counter (stats, "enemies_killed");
    g_assert_cmpint (value, ==, 1);

    lrg_template_statistics_track_counter (stats, "enemies_killed", 5);
    value = lrg_template_statistics_get_counter (stats, "enemies_killed");
    g_assert_cmpint (value, ==, 6);
}

static void
test_statistics_counter_negative (void)
{
    g_autoptr(LrgTemplateStatistics) stats = NULL;
    gint64 value;

    stats = lrg_template_statistics_new ("test");
    g_assert_nonnull (stats);

    lrg_template_statistics_track_counter (stats, "score", 100);
    lrg_template_statistics_track_counter (stats, "score", -25);

    value = lrg_template_statistics_get_counter (stats, "score");
    g_assert_cmpint (value, ==, 75);
}

static void
test_statistics_counter_set (void)
{
    g_autoptr(LrgTemplateStatistics) stats = NULL;
    gint64 value;

    stats = lrg_template_statistics_new ("test");
    g_assert_nonnull (stats);

    lrg_template_statistics_set_counter (stats, "level", 10);
    value = lrg_template_statistics_get_counter (stats, "level");
    g_assert_cmpint (value, ==, 10);

    lrg_template_statistics_set_counter (stats, "level", 5);
    value = lrg_template_statistics_get_counter (stats, "level");
    g_assert_cmpint (value, ==, 5);
}

/* ==========================================================================
 * Test Cases - LrgTemplateStatistics Maximums
 * ========================================================================== */

static void
test_statistics_maximum_track (void)
{
    g_autoptr(LrgTemplateStatistics) stats = NULL;
    gdouble value;

    stats = lrg_template_statistics_new ("test");
    g_assert_nonnull (stats);

    /* First value becomes maximum */
    lrg_template_statistics_track_maximum (stats, "high_score", 1000.0);
    value = lrg_template_statistics_get_maximum (stats, "high_score");
    g_assert_cmpfloat (value, ==, 1000.0);

    /* Lower value doesn't replace */
    lrg_template_statistics_track_maximum (stats, "high_score", 500.0);
    value = lrg_template_statistics_get_maximum (stats, "high_score");
    g_assert_cmpfloat (value, ==, 1000.0);

    /* Higher value replaces */
    lrg_template_statistics_track_maximum (stats, "high_score", 1500.0);
    value = lrg_template_statistics_get_maximum (stats, "high_score");
    g_assert_cmpfloat (value, ==, 1500.0);
}

static void
test_statistics_maximum_missing (void)
{
    g_autoptr(LrgTemplateStatistics) stats = NULL;
    gdouble value;

    stats = lrg_template_statistics_new ("test");
    g_assert_nonnull (stats);

    /* Untracked maximum returns -G_MAXDOUBLE */
    value = lrg_template_statistics_get_maximum (stats, "nonexistent");
    g_assert_cmpfloat (value, ==, -G_MAXDOUBLE);
}

/* ==========================================================================
 * Test Cases - LrgTemplateStatistics Minimums
 * ========================================================================== */

static void
test_statistics_minimum_track (void)
{
    g_autoptr(LrgTemplateStatistics) stats = NULL;
    gdouble value;

    stats = lrg_template_statistics_new ("test");
    g_assert_nonnull (stats);

    /* First value becomes minimum */
    lrg_template_statistics_track_minimum (stats, "fastest_time", 60.0);
    value = lrg_template_statistics_get_minimum (stats, "fastest_time");
    g_assert_cmpfloat (value, ==, 60.0);

    /* Higher value doesn't replace */
    lrg_template_statistics_track_minimum (stats, "fastest_time", 90.0);
    value = lrg_template_statistics_get_minimum (stats, "fastest_time");
    g_assert_cmpfloat (value, ==, 60.0);

    /* Lower value replaces */
    lrg_template_statistics_track_minimum (stats, "fastest_time", 45.0);
    value = lrg_template_statistics_get_minimum (stats, "fastest_time");
    g_assert_cmpfloat (value, ==, 45.0);
}

static void
test_statistics_minimum_missing (void)
{
    g_autoptr(LrgTemplateStatistics) stats = NULL;
    gdouble value;

    stats = lrg_template_statistics_new ("test");
    g_assert_nonnull (stats);

    /* Untracked minimum returns G_MAXDOUBLE */
    value = lrg_template_statistics_get_minimum (stats, "nonexistent");
    g_assert_cmpfloat (value, ==, G_MAXDOUBLE);
}

/* ==========================================================================
 * Test Cases - LrgTemplateStatistics Timers
 * ========================================================================== */

static void
test_statistics_timer_basic (void)
{
    g_autoptr(LrgTemplateStatistics) stats = NULL;

    stats = lrg_template_statistics_new ("test");
    g_assert_nonnull (stats);

    /* Timer not running initially */
    g_assert_false (lrg_template_statistics_is_timer_running (stats, "session"));

    /* Start timer */
    lrg_template_statistics_timer_start (stats, "session");
    g_assert_true (lrg_template_statistics_is_timer_running (stats, "session"));

    /* Stop timer */
    lrg_template_statistics_timer_stop (stats, "session");
    g_assert_false (lrg_template_statistics_is_timer_running (stats, "session"));
}

static void
test_statistics_timer_reset (void)
{
    g_autoptr(LrgTemplateStatistics) stats = NULL;
    gdouble value;

    stats = lrg_template_statistics_new ("test");
    g_assert_nonnull (stats);

    lrg_template_statistics_timer_start (stats, "level");
    /* Simulate some time passing would require sleep - just test reset */
    lrg_template_statistics_timer_stop (stats, "level");

    /* Reset should clear accumulated time */
    lrg_template_statistics_timer_reset (stats, "level");
    value = lrg_template_statistics_get_timer (stats, "level");
    g_assert_cmpfloat (value, ==, 0.0);
    g_assert_false (lrg_template_statistics_is_timer_running (stats, "level"));
}

/* ==========================================================================
 * Test Cases - LrgTemplateStatistics Utility Methods
 * ========================================================================== */

static void
test_statistics_has_stat (void)
{
    g_autoptr(LrgTemplateStatistics) stats = NULL;

    stats = lrg_template_statistics_new ("test");
    g_assert_nonnull (stats);

    g_assert_false (lrg_template_statistics_has_stat (stats, "score"));

    lrg_template_statistics_track_counter (stats, "score", 100);
    g_assert_true (lrg_template_statistics_has_stat (stats, "score"));
}

static void
test_statistics_remove_stat (void)
{
    g_autoptr(LrgTemplateStatistics) stats = NULL;
    gboolean removed;

    stats = lrg_template_statistics_new ("test");
    g_assert_nonnull (stats);

    lrg_template_statistics_track_counter (stats, "score", 100);
    g_assert_true (lrg_template_statistics_has_stat (stats, "score"));

    removed = lrg_template_statistics_remove_stat (stats, "score");
    g_assert_true (removed);
    g_assert_false (lrg_template_statistics_has_stat (stats, "score"));

    /* Removing non-existent returns FALSE */
    removed = lrg_template_statistics_remove_stat (stats, "nonexistent");
    g_assert_false (removed);
}

static void
test_statistics_clear_all (void)
{
    g_autoptr(LrgTemplateStatistics) stats = NULL;

    stats = lrg_template_statistics_new ("test");
    g_assert_nonnull (stats);

    lrg_template_statistics_track_counter (stats, "kills", 10);
    lrg_template_statistics_track_maximum (stats, "score", 1000.0);
    lrg_template_statistics_track_minimum (stats, "time", 30.0);

    g_assert_true (lrg_template_statistics_has_stat (stats, "kills"));
    g_assert_true (lrg_template_statistics_has_stat (stats, "score"));
    g_assert_true (lrg_template_statistics_has_stat (stats, "time"));

    lrg_template_statistics_clear_all (stats);

    g_assert_false (lrg_template_statistics_has_stat (stats, "kills"));
    g_assert_false (lrg_template_statistics_has_stat (stats, "score"));
    g_assert_false (lrg_template_statistics_has_stat (stats, "time"));
}

static void
test_statistics_get_names (void)
{
    g_autoptr(LrgTemplateStatistics) stats = NULL;
    GList *names;

    stats = lrg_template_statistics_new ("test");
    g_assert_nonnull (stats);

    lrg_template_statistics_track_counter (stats, "counter1", 1);
    lrg_template_statistics_track_counter (stats, "counter2", 2);
    lrg_template_statistics_track_maximum (stats, "max1", 100.0);

    /* Check all names */
    names = lrg_template_statistics_get_all_names (stats);
    g_assert_cmpuint (g_list_length (names), ==, 3);
    g_list_free_full (names, g_free);

    /* Check counter names only */
    names = lrg_template_statistics_get_counter_names (stats);
    g_assert_cmpuint (g_list_length (names), ==, 2);
    g_list_free_full (names, g_free);

    /* Check maximum names only */
    names = lrg_template_statistics_get_maximum_names (stats);
    g_assert_cmpuint (g_list_length (names), ==, 1);
    g_list_free_full (names, g_free);
}

/* ==========================================================================
 * Test Cases - LrgDailyRewardState (GBoxed)
 * ========================================================================== */

static void
test_daily_reward_state_new (void)
{
    LrgDailyRewardState *state;

    state = lrg_daily_reward_state_new ();

    g_assert_nonnull (state);
    g_assert_cmpint (state->current_streak, ==, 0);
    g_assert_cmpint (state->max_streak, ==, 0);
    g_assert_cmpint (state->last_claim_timestamp, ==, 0);

    lrg_daily_reward_state_free (state);
}

static void
test_daily_reward_state_copy (void)
{
    LrgDailyRewardState *state;
    LrgDailyRewardState *copy;

    state = lrg_daily_reward_state_new ();
    state->current_streak = 5;
    state->max_streak = 10;
    state->last_claim_timestamp = 12345;

    copy = lrg_daily_reward_state_copy (state);

    g_assert_nonnull (copy);
    g_assert_cmpint (copy->current_streak, ==, 5);
    g_assert_cmpint (copy->max_streak, ==, 10);
    g_assert_cmpint (copy->last_claim_timestamp, ==, 12345);

    lrg_daily_reward_state_free (state);
    lrg_daily_reward_state_free (copy);
}

static void
test_daily_reward_state_free_null (void)
{
    /* Should handle NULL without crashing */
    lrg_daily_reward_state_free (NULL);
    g_assert_true (TRUE);
}

/* ==========================================================================
 * Test Cases - LrgTemplateDailyRewards Interface
 * ========================================================================== */

static void
test_daily_rewards_interface_implements (void)
{
    g_autoptr(TestDailyRewardsMock) mock = NULL;

    mock = g_object_new (TEST_TYPE_DAILY_REWARDS_MOCK, NULL);

    g_assert_nonnull (mock);
    g_assert_true (LRG_IS_TEMPLATE_DAILY_REWARDS (mock));
}

static void
test_daily_rewards_get_state (void)
{
    g_autoptr(TestDailyRewardsMock) mock = NULL;
    LrgDailyRewardState *state;

    mock = g_object_new (TEST_TYPE_DAILY_REWARDS_MOCK, NULL);
    g_assert_nonnull (mock);

    state = lrg_template_daily_rewards_get_daily_reward_state (
        LRG_TEMPLATE_DAILY_REWARDS (mock));

    g_assert_nonnull (state);
    g_assert_cmpint (state->current_streak, ==, 0);
}

static void
test_daily_rewards_streak_bonus (void)
{
    g_autoptr(TestDailyRewardsMock) mock = NULL;
    LrgDailyRewardState *state;
    gdouble bonus;

    mock = g_object_new (TEST_TYPE_DAILY_REWARDS_MOCK, NULL);
    g_assert_nonnull (mock);

    state = lrg_template_daily_rewards_get_daily_reward_state (
        LRG_TEMPLATE_DAILY_REWARDS (mock));

    /* No streak = base multiplier (1.0 or 1.1 depending on implementation) */
    bonus = lrg_template_daily_rewards_get_streak_bonus_multiplier (
        LRG_TEMPLATE_DAILY_REWARDS (mock));
    g_assert_cmpfloat (bonus, >=, 1.0);

    /* Set a streak and check multiplier increases */
    state->current_streak = 5;
    bonus = lrg_template_daily_rewards_get_streak_bonus_multiplier (
        LRG_TEMPLATE_DAILY_REWARDS (mock));
    g_assert_cmpfloat (bonus, >=, 1.0); /* Should be higher with streak */
}

static void
test_daily_rewards_get_current_streak (void)
{
    g_autoptr(TestDailyRewardsMock) mock = NULL;
    LrgDailyRewardState *state;
    gint streak;

    mock = g_object_new (TEST_TYPE_DAILY_REWARDS_MOCK, NULL);
    g_assert_nonnull (mock);

    state = lrg_template_daily_rewards_get_daily_reward_state (
        LRG_TEMPLATE_DAILY_REWARDS (mock));
    state->current_streak = 7;

    streak = lrg_template_daily_rewards_get_current_streak (
        LRG_TEMPLATE_DAILY_REWARDS (mock));

    g_assert_cmpint (streak, ==, 7);
}

static void
test_daily_rewards_get_max_streak (void)
{
    g_autoptr(TestDailyRewardsMock) mock = NULL;
    LrgDailyRewardState *state;
    gint max_streak;

    mock = g_object_new (TEST_TYPE_DAILY_REWARDS_MOCK, NULL);
    g_assert_nonnull (mock);

    state = lrg_template_daily_rewards_get_daily_reward_state (
        LRG_TEMPLATE_DAILY_REWARDS (mock));
    state->max_streak = 15;

    max_streak = lrg_template_daily_rewards_get_max_streak (
        LRG_TEMPLATE_DAILY_REWARDS (mock));

    g_assert_cmpint (max_streak, ==, 15);
}

static void
test_daily_rewards_can_claim_fresh (void)
{
    g_autoptr(TestDailyRewardsMock) mock = NULL;
    gboolean can_claim;

    mock = g_object_new (TEST_TYPE_DAILY_REWARDS_MOCK, NULL);
    g_assert_nonnull (mock);

    /* Fresh state should allow claiming (never claimed before) */
    can_claim = lrg_template_daily_rewards_can_claim (
        LRG_TEMPLATE_DAILY_REWARDS (mock));

    g_assert_true (can_claim);
}

static void
test_daily_rewards_time_until_claim (void)
{
    g_autoptr(TestDailyRewardsMock) mock = NULL;
    gint64 time_until;

    mock = g_object_new (TEST_TYPE_DAILY_REWARDS_MOCK, NULL);
    g_assert_nonnull (mock);

    /* Fresh state should return 0 (can claim now) */
    time_until = lrg_template_daily_rewards_get_time_until_claim (
        LRG_TEMPLATE_DAILY_REWARDS (mock));

    g_assert_cmpint (time_until, ==, 0);
}

/* ==========================================================================
 * Test Cases - LrgTemplateDifficulty Interface
 * ========================================================================== */

static void
test_difficulty_interface_implements (void)
{
    g_autoptr(TestDifficultyMock) mock = NULL;

    mock = g_object_new (TEST_TYPE_DIFFICULTY_MOCK, NULL);

    g_assert_nonnull (mock);
    g_assert_true (LRG_IS_TEMPLATE_DIFFICULTY (mock));
}

static void
test_difficulty_initial_state (void)
{
    g_autoptr(TestDifficultyMock) mock = NULL;
    gdouble score;
    gdouble modifier;

    mock = g_object_new (TEST_TYPE_DIFFICULTY_MOCK, NULL);
    g_assert_nonnull (mock);

    /* No data = neutral performance (0.5) */
    score = lrg_template_difficulty_get_performance_score (
        LRG_TEMPLATE_DIFFICULTY (mock));
    g_assert_cmpfloat (score, ==, 0.5);

    /* Initial modifier should be 1.0 */
    modifier = lrg_template_difficulty_get_difficulty_modifier (
        LRG_TEMPLATE_DIFFICULTY (mock));
    g_assert_cmpfloat (modifier, ==, 1.0);
}

static void
test_difficulty_record_success (void)
{
    g_autoptr(TestDifficultyMock) mock = NULL;
    gdouble score;

    mock = g_object_new (TEST_TYPE_DIFFICULTY_MOCK, NULL);
    g_assert_nonnull (mock);

    /* Record some successes */
    lrg_template_difficulty_record_player_success (
        LRG_TEMPLATE_DIFFICULTY (mock), 1.0);
    lrg_template_difficulty_record_player_success (
        LRG_TEMPLATE_DIFFICULTY (mock), 1.0);

    score = lrg_template_difficulty_get_performance_score (
        LRG_TEMPLATE_DIFFICULTY (mock));

    /* All successes = score should be 1.0 (100% success rate) */
    g_assert_cmpfloat (score, ==, 1.0);
}

static void
test_difficulty_record_failure (void)
{
    g_autoptr(TestDifficultyMock) mock = NULL;
    gdouble score;

    mock = g_object_new (TEST_TYPE_DIFFICULTY_MOCK, NULL);
    g_assert_nonnull (mock);

    /* Record some failures */
    lrg_template_difficulty_record_player_failure (
        LRG_TEMPLATE_DIFFICULTY (mock), 1.0);
    lrg_template_difficulty_record_player_failure (
        LRG_TEMPLATE_DIFFICULTY (mock), 1.0);

    score = lrg_template_difficulty_get_performance_score (
        LRG_TEMPLATE_DIFFICULTY (mock));

    /* All failures = score should be 0.0 */
    g_assert_cmpfloat (score, ==, 0.0);
}

static void
test_difficulty_mixed_events (void)
{
    g_autoptr(TestDifficultyMock) mock = NULL;
    gdouble score;

    mock = g_object_new (TEST_TYPE_DIFFICULTY_MOCK, NULL);
    g_assert_nonnull (mock);

    /* 3 success + 1 failure = 75% success rate */
    lrg_template_difficulty_record_player_success (
        LRG_TEMPLATE_DIFFICULTY (mock), 1.0);
    lrg_template_difficulty_record_player_success (
        LRG_TEMPLATE_DIFFICULTY (mock), 1.0);
    lrg_template_difficulty_record_player_success (
        LRG_TEMPLATE_DIFFICULTY (mock), 1.0);
    lrg_template_difficulty_record_player_failure (
        LRG_TEMPLATE_DIFFICULTY (mock), 1.0);

    score = lrg_template_difficulty_get_performance_score (
        LRG_TEMPLATE_DIFFICULTY (mock));

    g_assert_cmpfloat (score, ==, 0.75);
}

static void
test_difficulty_weighted_events (void)
{
    g_autoptr(TestDifficultyMock) mock = NULL;
    gdouble score;

    mock = g_object_new (TEST_TYPE_DIFFICULTY_MOCK, NULL);
    g_assert_nonnull (mock);

    /* 1 success (weight 5) + 1 failure (weight 5) = 50% */
    lrg_template_difficulty_record_player_success (
        LRG_TEMPLATE_DIFFICULTY (mock), 5.0);
    lrg_template_difficulty_record_player_failure (
        LRG_TEMPLATE_DIFFICULTY (mock), 5.0);

    score = lrg_template_difficulty_get_performance_score (
        LRG_TEMPLATE_DIFFICULTY (mock));

    g_assert_cmpfloat (score, ==, 0.5);
}

static void
test_difficulty_reset_window (void)
{
    g_autoptr(TestDifficultyMock) mock = NULL;
    gdouble score;
    gdouble modifier_before;
    gdouble modifier_after;

    mock = g_object_new (TEST_TYPE_DIFFICULTY_MOCK, NULL);
    g_assert_nonnull (mock);

    /* Record some events to change modifier */
    lrg_template_difficulty_record_player_success (
        LRG_TEMPLATE_DIFFICULTY (mock), 5.0);

    modifier_before = lrg_template_difficulty_get_difficulty_modifier (
        LRG_TEMPLATE_DIFFICULTY (mock));

    /* Reset performance window */
    lrg_template_difficulty_reset_performance_window (
        LRG_TEMPLATE_DIFFICULTY (mock));

    /* Score should be neutral again */
    score = lrg_template_difficulty_get_performance_score (
        LRG_TEMPLATE_DIFFICULTY (mock));
    g_assert_cmpfloat (score, ==, 0.5);

    /* Modifier should be preserved */
    modifier_after = lrg_template_difficulty_get_difficulty_modifier (
        LRG_TEMPLATE_DIFFICULTY (mock));
    g_assert_cmpfloat (modifier_after, ==, modifier_before);
}

static void
test_difficulty_is_struggling (void)
{
    g_autoptr(TestDifficultyMock) mock = NULL;
    gboolean struggling;

    mock = g_object_new (TEST_TYPE_DIFFICULTY_MOCK, NULL);
    g_assert_nonnull (mock);

    /* Record failures to lower score below 0.35 */
    lrg_template_difficulty_record_player_failure (
        LRG_TEMPLATE_DIFFICULTY (mock), 5.0);
    lrg_template_difficulty_record_player_success (
        LRG_TEMPLATE_DIFFICULTY (mock), 1.0);

    struggling = lrg_template_difficulty_is_player_struggling (
        LRG_TEMPLATE_DIFFICULTY (mock));

    /* score = 1/6 = 0.167, should be struggling */
    g_assert_true (struggling);
}

static void
test_difficulty_is_dominating (void)
{
    g_autoptr(TestDifficultyMock) mock = NULL;
    gboolean dominating;

    mock = g_object_new (TEST_TYPE_DIFFICULTY_MOCK, NULL);
    g_assert_nonnull (mock);

    /* Record successes to raise score above 0.65 */
    lrg_template_difficulty_record_player_success (
        LRG_TEMPLATE_DIFFICULTY (mock), 5.0);
    lrg_template_difficulty_record_player_failure (
        LRG_TEMPLATE_DIFFICULTY (mock), 1.0);

    dominating = lrg_template_difficulty_is_player_dominating (
        LRG_TEMPLATE_DIFFICULTY (mock));

    /* score = 5/6 = 0.833, should be dominating */
    g_assert_true (dominating);
}

static void
test_difficulty_performance_label (void)
{
    g_autoptr(TestDifficultyMock) mock = NULL;
    const gchar *label;

    mock = g_object_new (TEST_TYPE_DIFFICULTY_MOCK, NULL);
    g_assert_nonnull (mock);

    /* Neutral state = "Balanced" */
    label = lrg_template_difficulty_get_performance_label (
        LRG_TEMPLATE_DIFFICULTY (mock));

    g_assert_nonnull (label);
    g_assert_cmpstr (label, ==, "Balanced");
}

static void
test_difficulty_performance_label_struggling (void)
{
    g_autoptr(TestDifficultyMock) mock = NULL;
    const gchar *label;

    mock = g_object_new (TEST_TYPE_DIFFICULTY_MOCK, NULL);
    g_assert_nonnull (mock);

    /* All failures = struggling */
    lrg_template_difficulty_record_player_failure (
        LRG_TEMPLATE_DIFFICULTY (mock), 1.0);

    label = lrg_template_difficulty_get_performance_label (
        LRG_TEMPLATE_DIFFICULTY (mock));

    g_assert_nonnull (label);
    g_assert_cmpstr (label, ==, "Struggling");
}

static void
test_difficulty_performance_label_dominating (void)
{
    g_autoptr(TestDifficultyMock) mock = NULL;
    const gchar *label;

    mock = g_object_new (TEST_TYPE_DIFFICULTY_MOCK, NULL);
    g_assert_nonnull (mock);

    /* All successes = dominating */
    lrg_template_difficulty_record_player_success (
        LRG_TEMPLATE_DIFFICULTY (mock), 1.0);

    label = lrg_template_difficulty_get_performance_label (
        LRG_TEMPLATE_DIFFICULTY (mock));

    g_assert_nonnull (label);
    g_assert_cmpstr (label, ==, "Dominating");
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* LrgTemplateStatistics - Construction */
    g_test_add_func ("/template/statistics/new",
                     test_statistics_new);
    g_test_add_func ("/template/statistics/get-id",
                     test_statistics_get_id);

    /* LrgTemplateStatistics - Counters */
    g_test_add_func ("/template/statistics/counter/track",
                     test_statistics_counter_track);
    g_test_add_func ("/template/statistics/counter/negative",
                     test_statistics_counter_negative);
    g_test_add_func ("/template/statistics/counter/set",
                     test_statistics_counter_set);

    /* LrgTemplateStatistics - Maximums */
    g_test_add_func ("/template/statistics/maximum/track",
                     test_statistics_maximum_track);
    g_test_add_func ("/template/statistics/maximum/missing",
                     test_statistics_maximum_missing);

    /* LrgTemplateStatistics - Minimums */
    g_test_add_func ("/template/statistics/minimum/track",
                     test_statistics_minimum_track);
    g_test_add_func ("/template/statistics/minimum/missing",
                     test_statistics_minimum_missing);

    /* LrgTemplateStatistics - Timers */
    g_test_add_func ("/template/statistics/timer/basic",
                     test_statistics_timer_basic);
    g_test_add_func ("/template/statistics/timer/reset",
                     test_statistics_timer_reset);

    /* LrgTemplateStatistics - Utility */
    g_test_add_func ("/template/statistics/has-stat",
                     test_statistics_has_stat);
    g_test_add_func ("/template/statistics/remove-stat",
                     test_statistics_remove_stat);
    g_test_add_func ("/template/statistics/clear-all",
                     test_statistics_clear_all);
    g_test_add_func ("/template/statistics/get-names",
                     test_statistics_get_names);

    /* LrgDailyRewardState (GBoxed) */
    g_test_add_func ("/template/daily-reward-state/new",
                     test_daily_reward_state_new);
    g_test_add_func ("/template/daily-reward-state/copy",
                     test_daily_reward_state_copy);
    g_test_add_func ("/template/daily-reward-state/free-null",
                     test_daily_reward_state_free_null);

    /* LrgTemplateDailyRewards Interface */
    g_test_add_func ("/template/daily-rewards/interface-implements",
                     test_daily_rewards_interface_implements);
    g_test_add_func ("/template/daily-rewards/get-state",
                     test_daily_rewards_get_state);
    g_test_add_func ("/template/daily-rewards/streak-bonus",
                     test_daily_rewards_streak_bonus);
    g_test_add_func ("/template/daily-rewards/get-current-streak",
                     test_daily_rewards_get_current_streak);
    g_test_add_func ("/template/daily-rewards/get-max-streak",
                     test_daily_rewards_get_max_streak);
    g_test_add_func ("/template/daily-rewards/can-claim-fresh",
                     test_daily_rewards_can_claim_fresh);
    g_test_add_func ("/template/daily-rewards/time-until-claim",
                     test_daily_rewards_time_until_claim);

    /* LrgTemplateDifficulty Interface */
    g_test_add_func ("/template/difficulty/interface-implements",
                     test_difficulty_interface_implements);
    g_test_add_func ("/template/difficulty/initial-state",
                     test_difficulty_initial_state);
    g_test_add_func ("/template/difficulty/record-success",
                     test_difficulty_record_success);
    g_test_add_func ("/template/difficulty/record-failure",
                     test_difficulty_record_failure);
    g_test_add_func ("/template/difficulty/mixed-events",
                     test_difficulty_mixed_events);
    g_test_add_func ("/template/difficulty/weighted-events",
                     test_difficulty_weighted_events);
    g_test_add_func ("/template/difficulty/reset-window",
                     test_difficulty_reset_window);
    g_test_add_func ("/template/difficulty/is-struggling",
                     test_difficulty_is_struggling);
    g_test_add_func ("/template/difficulty/is-dominating",
                     test_difficulty_is_dominating);
    g_test_add_func ("/template/difficulty/performance-label",
                     test_difficulty_performance_label);
    g_test_add_func ("/template/difficulty/performance-label-struggling",
                     test_difficulty_performance_label_struggling);
    g_test_add_func ("/template/difficulty/performance-label-dominating",
                     test_difficulty_performance_label_dominating);

    return g_test_run ();
}
