/* test-achievement.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the Achievement module.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Custom Achievement Subclass for Testing
 *
 * Overrides check_unlock to provide custom logic.
 * ========================================================================== */

#define TEST_TYPE_CUSTOM_ACHIEVEMENT (test_custom_achievement_get_type ())
G_DECLARE_FINAL_TYPE (TestCustomAchievement, test_custom_achievement, TEST, CUSTOM_ACHIEVEMENT, LrgAchievement)

struct _TestCustomAchievement
{
    LrgAchievement parent_instance;

    gint64  required_kills;
    gint64  current_kills;
    gboolean on_unlocked_called;
};

G_DEFINE_TYPE (TestCustomAchievement, test_custom_achievement, LRG_TYPE_ACHIEVEMENT)

static gboolean
test_custom_achievement_check_unlock (LrgAchievement *achievement)
{
    TestCustomAchievement *self = TEST_CUSTOM_ACHIEVEMENT (achievement);

    return self->current_kills >= self->required_kills;
}

static void
test_custom_achievement_on_unlocked (LrgAchievement *achievement)
{
    TestCustomAchievement *self = TEST_CUSTOM_ACHIEVEMENT (achievement);

    self->on_unlocked_called = TRUE;

    /* Chain up */
    LRG_ACHIEVEMENT_CLASS (test_custom_achievement_parent_class)->on_unlocked (achievement);
}

static void
test_custom_achievement_class_init (TestCustomAchievementClass *klass)
{
    LrgAchievementClass *achievement_class = LRG_ACHIEVEMENT_CLASS (klass);

    achievement_class->check_unlock = test_custom_achievement_check_unlock;
    achievement_class->on_unlocked = test_custom_achievement_on_unlocked;
}

static void
test_custom_achievement_init (TestCustomAchievement *self)
{
    self->required_kills = 10;
    self->current_kills = 0;
    self->on_unlocked_called = FALSE;
}

static TestCustomAchievement *
test_custom_achievement_new (const gchar *id,
                             const gchar *name)
{
    return g_object_new (TEST_TYPE_CUSTOM_ACHIEVEMENT,
                         "id", id,
                         "name", name,
                         NULL);
}

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgAchievementManager *manager;
} AchievementFixture;

static void
achievement_fixture_set_up (AchievementFixture *fixture,
                            gconstpointer       user_data)
{
    /* Create a fresh manager instance */
    fixture->manager = g_object_new (LRG_TYPE_ACHIEVEMENT_MANAGER, NULL);
    g_assert_nonnull (fixture->manager);
}

static void
achievement_fixture_tear_down (AchievementFixture *fixture,
                               gconstpointer       user_data)
{
    g_clear_object (&fixture->manager);
}

/* ==========================================================================
 * Test Cases - LrgAchievementProgress
 * ========================================================================== */

static void
test_achievement_progress_new (void)
{
    LrgAchievementProgress *progress = NULL;

    progress = lrg_achievement_progress_new (5, 10);
    g_assert_nonnull (progress);
    g_assert_cmpint (lrg_achievement_progress_get_current (progress), ==, 5);
    g_assert_cmpint (lrg_achievement_progress_get_target (progress), ==, 10);

    lrg_achievement_progress_free (progress);
}

static void
test_achievement_progress_percentage (void)
{
    LrgAchievementProgress *progress = NULL;
    gfloat percentage;

    progress = lrg_achievement_progress_new (25, 100);
    percentage = lrg_achievement_progress_get_percentage (progress);
    g_assert_cmpfloat_with_epsilon (percentage, 0.25f, 0.001f);

    lrg_achievement_progress_free (progress);

    /* Test 100% */
    progress = lrg_achievement_progress_new (100, 100);
    percentage = lrg_achievement_progress_get_percentage (progress);
    g_assert_cmpfloat_with_epsilon (percentage, 1.0f, 0.001f);

    lrg_achievement_progress_free (progress);

    /* Test 0% */
    progress = lrg_achievement_progress_new (0, 100);
    percentage = lrg_achievement_progress_get_percentage (progress);
    g_assert_cmpfloat_with_epsilon (percentage, 0.0f, 0.001f);

    lrg_achievement_progress_free (progress);
}

static void
test_achievement_progress_complete (void)
{
    LrgAchievementProgress *progress = NULL;

    progress = lrg_achievement_progress_new (5, 10);
    g_assert_false (lrg_achievement_progress_is_complete (progress));

    lrg_achievement_progress_free (progress);

    progress = lrg_achievement_progress_new (10, 10);
    g_assert_true (lrg_achievement_progress_is_complete (progress));

    lrg_achievement_progress_free (progress);

    /* Over 100% should still be complete */
    progress = lrg_achievement_progress_new (15, 10);
    g_assert_true (lrg_achievement_progress_is_complete (progress));

    lrg_achievement_progress_free (progress);
}

static void
test_achievement_progress_copy (void)
{
    LrgAchievementProgress *original = NULL;
    LrgAchievementProgress *copy = NULL;

    original = lrg_achievement_progress_new (7, 20);
    copy = lrg_achievement_progress_copy (original);

    g_assert_nonnull (copy);
    g_assert_true (copy != original);
    g_assert_cmpint (lrg_achievement_progress_get_current (copy), ==, 7);
    g_assert_cmpint (lrg_achievement_progress_get_target (copy), ==, 20);

    lrg_achievement_progress_free (original);
    lrg_achievement_progress_free (copy);
}

static void
test_achievement_progress_set (void)
{
    LrgAchievementProgress *progress = NULL;

    progress = lrg_achievement_progress_new (0, 10);
    g_assert_cmpint (lrg_achievement_progress_get_current (progress), ==, 0);

    lrg_achievement_progress_set_current (progress, 5);
    g_assert_cmpint (lrg_achievement_progress_get_current (progress), ==, 5);

    lrg_achievement_progress_set_target (progress, 20);
    g_assert_cmpint (lrg_achievement_progress_get_target (progress), ==, 20);

    lrg_achievement_progress_free (progress);
}

/* ==========================================================================
 * Test Cases - LrgAchievement
 * ========================================================================== */

static void
test_achievement_new (void)
{
    g_autoptr(LrgAchievement) achievement = NULL;

    achievement = lrg_achievement_new ("test_ach", "Test Achievement", "");
    g_assert_nonnull (achievement);
    g_assert_cmpstr (lrg_achievement_get_id (achievement), ==, "test_ach");
    g_assert_cmpstr (lrg_achievement_get_name (achievement), ==, "Test Achievement");
    g_assert_false (lrg_achievement_is_unlocked (achievement));
}

static void
test_achievement_properties (void)
{
    g_autoptr(LrgAchievement) achievement = NULL;

    /* Create with description set in constructor */
    achievement = lrg_achievement_new ("ach_001", "First Achievement", "Complete the first task");
    g_assert_cmpstr (lrg_achievement_get_description (achievement), ==, "Complete the first task");

    lrg_achievement_set_hidden (achievement, TRUE);
    g_assert_true (lrg_achievement_is_hidden (achievement));

    lrg_achievement_set_points (achievement, 50);
    g_assert_cmpint (lrg_achievement_get_points (achievement), ==, 50);
}

static void
test_achievement_unlock (void)
{
    g_autoptr(LrgAchievement) achievement = NULL;

    achievement = lrg_achievement_new ("ach_unlock", "Unlock Test", "");

    g_assert_false (lrg_achievement_is_unlocked (achievement));
    g_assert_null (lrg_achievement_get_unlock_time (achievement));

    lrg_achievement_unlock (achievement);

    g_assert_true (lrg_achievement_is_unlocked (achievement));
    g_assert_nonnull (lrg_achievement_get_unlock_time (achievement));
}

static void
test_achievement_custom_check_unlock (void)
{
    g_autoptr(TestCustomAchievement) achievement = NULL;

    achievement = test_custom_achievement_new ("kills_10", "10 Kills");

    /* Set required kills to 10 */
    achievement->required_kills = 10;
    achievement->current_kills = 0;

    /* Should not unlock yet */
    g_assert_false (lrg_achievement_check_unlock (LRG_ACHIEVEMENT (achievement)));

    /* Partial progress */
    achievement->current_kills = 5;
    g_assert_false (lrg_achievement_check_unlock (LRG_ACHIEVEMENT (achievement)));

    /* Meets requirement */
    achievement->current_kills = 10;
    g_assert_true (lrg_achievement_check_unlock (LRG_ACHIEVEMENT (achievement)));

    /* Exceeds requirement */
    achievement->current_kills = 15;
    g_assert_true (lrg_achievement_check_unlock (LRG_ACHIEVEMENT (achievement)));
}

static void
test_achievement_on_unlocked_virtual (void)
{
    g_autoptr(TestCustomAchievement) achievement = NULL;

    achievement = test_custom_achievement_new ("test_unlock", "Test Unlock");
    achievement->required_kills = 1;
    achievement->current_kills = 1;

    g_assert_false (achievement->on_unlocked_called);

    lrg_achievement_unlock (LRG_ACHIEVEMENT (achievement));

    g_assert_true (achievement->on_unlocked_called);
}

static void
test_achievement_progress (void)
{
    g_autoptr(LrgAchievement) achievement = NULL;
    LrgAchievementProgress *progress;

    /* Create achievement with progress tracking (target = 10) */
    achievement = lrg_achievement_new_with_progress ("ach_prog", "Progress Test", "", 10);

    /* Set current progress to 5 */
    lrg_achievement_set_progress_value (achievement, 5);
    progress = lrg_achievement_get_progress (achievement);

    g_assert_nonnull (progress);
    g_assert_cmpint (lrg_achievement_progress_get_current (progress), ==, 5);
    g_assert_cmpint (lrg_achievement_progress_get_target (progress), ==, 10);
    g_assert_cmpfloat_with_epsilon (lrg_achievement_progress_get_percentage (progress),
                                    0.5f, 0.001f);
}

/* ==========================================================================
 * Test Cases - LrgAchievementManager
 * ========================================================================== */

static void
test_achievement_manager_singleton (void)
{
    LrgAchievementManager *a1;
    LrgAchievementManager *a2;

    a1 = lrg_achievement_manager_get_default ();
    a2 = lrg_achievement_manager_get_default ();

    g_assert_nonnull (a1);
    g_assert_true (a1 == a2);
}

static void
test_achievement_manager_register (AchievementFixture *fixture,
                                   gconstpointer       user_data)
{
    LrgAchievement *achievement = NULL;  /* Manager takes ownership */
    LrgAchievement *retrieved;

    achievement = lrg_achievement_new ("ach_register", "Register Test", "");

    /* Not registered yet - get returns NULL */
    retrieved = lrg_achievement_manager_get (fixture->manager, "ach_register");
    g_assert_null (retrieved);

    lrg_achievement_manager_register (fixture->manager, achievement);

    /* Now registered - get returns the achievement */
    retrieved = lrg_achievement_manager_get (fixture->manager, "ach_register");
    g_assert_nonnull (retrieved);
}

static void
test_achievement_manager_get (AchievementFixture *fixture,
                              gconstpointer       user_data)
{
    LrgAchievement *achievement = NULL;  /* Manager takes ownership */
    LrgAchievement *retrieved;

    achievement = lrg_achievement_new ("ach_get", "Get Test", "");
    lrg_achievement_manager_register (fixture->manager, achievement);

    retrieved = lrg_achievement_manager_get (fixture->manager, "ach_get");
    g_assert_nonnull (retrieved);
    g_assert_true (retrieved == achievement);

    /* Non-existent achievement */
    retrieved = lrg_achievement_manager_get (fixture->manager, "non_existent");
    g_assert_null (retrieved);
}

static void
test_achievement_manager_unlock (AchievementFixture *fixture,
                                 gconstpointer       user_data)
{
    LrgAchievement *achievement = NULL;  /* Manager takes ownership */
    LrgAchievement *retrieved;

    achievement = lrg_achievement_new ("ach_mgr_unlock", "Manager Unlock Test", "");
    lrg_achievement_manager_register (fixture->manager, achievement);

    g_assert_false (lrg_achievement_is_unlocked (achievement));

    lrg_achievement_manager_unlock (fixture->manager, "ach_mgr_unlock");

    retrieved = lrg_achievement_manager_get (fixture->manager, "ach_mgr_unlock");
    g_assert_true (lrg_achievement_is_unlocked (retrieved));
}

static void
test_achievement_manager_count (AchievementFixture *fixture,
                                gconstpointer       user_data)
{
    LrgAchievement *ach1 = NULL;  /* Manager takes ownership */
    LrgAchievement *ach2 = NULL;
    LrgAchievement *ach3 = NULL;

    g_assert_cmpuint (lrg_achievement_manager_get_count (fixture->manager), ==, 0);

    ach1 = lrg_achievement_new ("ach_count_1", "Count 1", "");
    ach2 = lrg_achievement_new ("ach_count_2", "Count 2", "");
    ach3 = lrg_achievement_new ("ach_count_3", "Count 3", "");

    lrg_achievement_manager_register (fixture->manager, ach1);
    lrg_achievement_manager_register (fixture->manager, ach2);
    lrg_achievement_manager_register (fixture->manager, ach3);

    g_assert_cmpuint (lrg_achievement_manager_get_count (fixture->manager), ==, 3);
    g_assert_cmpuint (lrg_achievement_manager_get_unlocked_count (fixture->manager), ==, 0);

    lrg_achievement_unlock (ach1);
    lrg_achievement_unlock (ach2);

    g_assert_cmpuint (lrg_achievement_manager_get_unlocked_count (fixture->manager), ==, 2);
}

static void
test_achievement_manager_get_all (AchievementFixture *fixture,
                                  gconstpointer       user_data)
{
    LrgAchievement *ach1 = NULL;  /* Manager takes ownership */
    LrgAchievement *ach2 = NULL;
    GList *all;

    ach1 = lrg_achievement_new ("ach_all_1", "All 1", "");
    ach2 = lrg_achievement_new ("ach_all_2", "All 2", "");

    lrg_achievement_manager_register (fixture->manager, ach1);
    lrg_achievement_manager_register (fixture->manager, ach2);

    all = lrg_achievement_manager_get_all (fixture->manager);
    g_assert_nonnull (all);
    g_assert_cmpuint (g_list_length (all), ==, 2);

    g_list_free (all);
}

static void
test_achievement_manager_get_unlocked (AchievementFixture *fixture,
                                       gconstpointer       user_data)
{
    LrgAchievement *ach1 = NULL;  /* Manager takes ownership */
    LrgAchievement *ach2 = NULL;
    LrgAchievement *ach3 = NULL;

    ach1 = lrg_achievement_new ("ach_unlocked_1", "Unlocked 1", "");
    ach2 = lrg_achievement_new ("ach_unlocked_2", "Unlocked 2", "");
    ach3 = lrg_achievement_new ("ach_unlocked_3", "Unlocked 3", "");

    lrg_achievement_manager_register (fixture->manager, ach1);
    lrg_achievement_manager_register (fixture->manager, ach2);
    lrg_achievement_manager_register (fixture->manager, ach3);

    lrg_achievement_unlock (ach1);
    lrg_achievement_unlock (ach3);

    /* Check unlocked count instead of getting list */
    g_assert_cmpuint (lrg_achievement_manager_get_unlocked_count (fixture->manager), ==, 2);
}

static void
test_achievement_manager_stats (AchievementFixture *fixture,
                                gconstpointer       user_data)
{
    /* Integer stats */
    g_assert_cmpint (lrg_achievement_manager_get_stat_int (fixture->manager, "kills"), ==, 0);

    lrg_achievement_manager_set_stat_int (fixture->manager, "kills", 10);
    g_assert_cmpint (lrg_achievement_manager_get_stat_int (fixture->manager, "kills"), ==, 10);

    lrg_achievement_manager_increment_stat (fixture->manager, "kills", 5);
    g_assert_cmpint (lrg_achievement_manager_get_stat_int (fixture->manager, "kills"), ==, 15);

    /* Float stats */
    g_assert_cmpfloat_with_epsilon (lrg_achievement_manager_get_stat_float (fixture->manager, "distance"),
                                    0.0f, 0.001f);

    lrg_achievement_manager_set_stat_float (fixture->manager, "distance", 100.5f);
    g_assert_cmpfloat_with_epsilon (lrg_achievement_manager_get_stat_float (fixture->manager, "distance"),
                                    100.5f, 0.001f);
}

static void
test_achievement_manager_reset (AchievementFixture *fixture,
                                gconstpointer       user_data)
{
    LrgAchievement *ach1 = NULL;  /* Manager takes ownership */
    LrgAchievement *ach2 = NULL;

    ach1 = lrg_achievement_new ("ach_reset_1", "Reset 1", "");
    ach2 = lrg_achievement_new ("ach_reset_2", "Reset 2", "");

    lrg_achievement_manager_register (fixture->manager, ach1);
    lrg_achievement_manager_register (fixture->manager, ach2);

    lrg_achievement_unlock (ach1);
    lrg_achievement_unlock (ach2);
    lrg_achievement_manager_set_stat_int (fixture->manager, "kills", 50);

    g_assert_cmpuint (lrg_achievement_manager_get_unlocked_count (fixture->manager), ==, 2);
    g_assert_cmpint (lrg_achievement_manager_get_stat_int (fixture->manager, "kills"), ==, 50);

    lrg_achievement_manager_reset_all (fixture->manager);

    /* Achievements should still be registered but not unlocked */
    g_assert_cmpuint (lrg_achievement_manager_get_count (fixture->manager), ==, 2);
    g_assert_cmpuint (lrg_achievement_manager_get_unlocked_count (fixture->manager), ==, 0);
}

/* ==========================================================================
 * Test Cases - LrgAchievementNotification
 * ========================================================================== */

static void
test_achievement_notification_new (void)
{
    g_autoptr(LrgAchievementNotification) notification = NULL;

    notification = lrg_achievement_notification_new ();
    g_assert_nonnull (notification);
    g_assert_false (lrg_achievement_notification_is_visible (notification));
}

static void
test_achievement_notification_duration (void)
{
    g_autoptr(LrgAchievementNotification) notification = NULL;

    notification = lrg_achievement_notification_new ();

    /* Default duration should be 5.0 seconds */
    g_assert_cmpfloat_with_epsilon (lrg_achievement_notification_get_duration (notification),
                                    5.0f, 0.001f);

    lrg_achievement_notification_set_duration (notification, 10.0f);
    g_assert_cmpfloat_with_epsilon (lrg_achievement_notification_get_duration (notification),
                                    10.0f, 0.001f);
}

static void
test_achievement_notification_fade_duration (void)
{
    g_autoptr(LrgAchievementNotification) notification = NULL;

    notification = lrg_achievement_notification_new ();

    /* Default fade duration should be 0.5 seconds */
    g_assert_cmpfloat_with_epsilon (lrg_achievement_notification_get_fade_duration (notification),
                                    0.5f, 0.001f);

    lrg_achievement_notification_set_fade_duration (notification, 1.0f);
    g_assert_cmpfloat_with_epsilon (lrg_achievement_notification_get_fade_duration (notification),
                                    1.0f, 0.001f);
}

static void
test_achievement_notification_position (void)
{
    g_autoptr(LrgAchievementNotification) notification = NULL;

    notification = lrg_achievement_notification_new ();

    /* Default position should be top right */
    g_assert_cmpint (lrg_achievement_notification_get_position (notification),
                     ==, LRG_NOTIFICATION_POSITION_TOP_RIGHT);

    lrg_achievement_notification_set_position (notification, LRG_NOTIFICATION_POSITION_BOTTOM_LEFT);
    g_assert_cmpint (lrg_achievement_notification_get_position (notification),
                     ==, LRG_NOTIFICATION_POSITION_BOTTOM_LEFT);
}

static void
test_achievement_notification_show_hide (void)
{
    g_autoptr(LrgAchievementNotification) notification = NULL;
    g_autoptr(LrgAchievement) achievement = NULL;

    notification = lrg_achievement_notification_new ();
    achievement = lrg_achievement_new ("ach_notify", "Notification Test", "Test description");

    g_assert_false (lrg_achievement_notification_is_visible (notification));

    lrg_achievement_notification_show (notification, achievement);
    g_assert_true (lrg_achievement_notification_is_visible (notification));

    lrg_achievement_notification_hide (notification);
    g_assert_false (lrg_achievement_notification_is_visible (notification));
}

static void
test_achievement_notification_update (void)
{
    g_autoptr(LrgAchievementNotification) notification = NULL;
    g_autoptr(LrgAchievement) achievement = NULL;

    notification = lrg_achievement_notification_new ();
    achievement = lrg_achievement_new ("ach_update", "Update Test", "");

    /* Set short durations for testing */
    lrg_achievement_notification_set_duration (notification, 0.5f);
    lrg_achievement_notification_set_fade_duration (notification, 0.1f);

    lrg_achievement_notification_show (notification, achievement);
    g_assert_true (lrg_achievement_notification_is_visible (notification));

    /* Update through fade in */
    lrg_achievement_notification_update (notification, 0.1f);
    g_assert_true (lrg_achievement_notification_is_visible (notification));

    /* Update through visible state */
    lrg_achievement_notification_update (notification, 0.5f);
    g_assert_true (lrg_achievement_notification_is_visible (notification));

    /* Update through fade out */
    lrg_achievement_notification_update (notification, 0.2f);
    g_assert_false (lrg_achievement_notification_is_visible (notification));
}

/* ==========================================================================
 * Test Cases - Manager Signals
 * ========================================================================== */

static gboolean unlocked_signal_received = FALSE;
static gchar *unlocked_signal_id = NULL;

static void
on_achievement_unlocked (LrgAchievementManager *manager,
                         LrgAchievement        *achievement,
                         gpointer               user_data)
{
    unlocked_signal_received = TRUE;
    g_free (unlocked_signal_id);
    unlocked_signal_id = g_strdup (lrg_achievement_get_id (achievement));
}

static void
test_achievement_manager_signal_unlocked (AchievementFixture *fixture,
                                          gconstpointer       user_data)
{
    LrgAchievement *achievement = NULL;  /* Manager takes ownership */
    gulong handler_id;

    /* Reset signal state */
    unlocked_signal_received = FALSE;
    g_clear_pointer (&unlocked_signal_id, g_free);

    achievement = lrg_achievement_new ("ach_signal", "Signal Test", "");
    lrg_achievement_manager_register (fixture->manager, achievement);

    handler_id = g_signal_connect (fixture->manager, "achievement-unlocked",
                                   G_CALLBACK (on_achievement_unlocked), NULL);

    lrg_achievement_manager_unlock (fixture->manager, "ach_signal");

    g_assert_true (unlocked_signal_received);
    g_assert_cmpstr (unlocked_signal_id, ==, "ach_signal");

    g_signal_handler_disconnect (fixture->manager, handler_id);
    g_clear_pointer (&unlocked_signal_id, g_free);
}

static gboolean progress_signal_received = FALSE;

static void
on_achievement_progress (LrgAchievementManager  *manager,
                         LrgAchievement         *achievement,
                         LrgAchievementProgress *progress,
                         gpointer                user_data)
{
    progress_signal_received = TRUE;
}

static void
test_achievement_manager_signal_progress (AchievementFixture *fixture,
                                          gconstpointer       user_data)
{
    LrgAchievement *achievement = NULL;  /* Manager takes ownership */
    gulong handler_id;

    /* Reset signal state */
    progress_signal_received = FALSE;

    /* Use achievement with progress tracking enabled */
    achievement = lrg_achievement_new_with_progress ("ach_prog_signal", "Progress Signal Test", "", 10);
    lrg_achievement_manager_register (fixture->manager, achievement);

    handler_id = g_signal_connect (fixture->manager, "achievement-progress",
                                   G_CALLBACK (on_achievement_progress), NULL);

    lrg_achievement_manager_set_progress (fixture->manager, "ach_prog_signal", 5);

    g_assert_true (progress_signal_received);

    g_signal_handler_disconnect (fixture->manager, handler_id);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

/* ==========================================================================
 * Criteria, categories, scopes, state and toasts
 * ========================================================================== */

static void
test_achievement_criterion (void)
{
    g_autoptr(LrgAchievementCriterion) c = NULL;
    g_auto(GStrv) keys = NULL;

    c = lrg_achievement_criterion_new ("towns", "Visit towns", 3);
    g_assert_cmpstr (lrg_achievement_criterion_get_id (c), ==, "towns");
    g_assert_cmpint (lrg_achievement_criterion_get_target (c), ==, 3);
    g_assert_true (lrg_achievement_criterion_add_key (c, "Daggerfall"));
    g_assert_false (lrg_achievement_criterion_add_key (c, "Daggerfall"));
    g_assert_true (lrg_achievement_criterion_add_key (c, "Wayrest"));
    g_assert_cmpint (lrg_achievement_criterion_get_current (c), ==, 2);
    g_assert_false (lrg_achievement_criterion_is_complete (c));
    g_assert_true (lrg_achievement_criterion_add_key (c, "Sentinel"));
    g_assert_true (lrg_achievement_criterion_add_key (c, "Orsinium"));
    /* past the target: remembered, not counted */
    g_assert_cmpint (lrg_achievement_criterion_get_current (c), ==, 3);
    g_assert_true (lrg_achievement_criterion_is_complete (c));
    keys = lrg_achievement_criterion_get_keys (c);
    g_assert_cmpuint (g_strv_length (keys), ==, 4);
    g_assert_cmpstr (keys[0], ==, "Daggerfall");
    g_assert_cmpfloat_with_epsilon (lrg_achievement_criterion_get_fraction (c), 1.0, 1e-9);
    g_assert_false (lrg_achievement_criterion_set_current (c, 99));
    g_assert_true (lrg_achievement_criterion_set_current (c, -5));
    g_assert_cmpint (lrg_achievement_criterion_get_current (c), ==, 0);
    lrg_achievement_criterion_reset (c);
    g_assert_false (lrg_achievement_criterion_has_key (c, "Wayrest"));
}

static void
on_criterion_counted (LrgAchievementManager *manager,
                      LrgAchievement        *achievement,
                      const gchar           *criterion,
                      gint64                 current,
                      gint64                 target,
                      gpointer               user_data)
{
    (void)manager;
    (void)achievement;
    (void)criterion;
    (void)current;
    (void)target;
    (*(guint *)user_data)++;
}

static void
on_unlock_counted (LrgAchievementManager *manager,
                   LrgAchievement        *achievement,
                   gpointer               user_data)
{
    (void)manager;
    (void)achievement;
    (*(guint *)user_data)++;
}

/* an achievement with two criteria unlocks once both complete */
static void
test_achievement_criteria_unlock (void)
{
    g_autoptr(LrgAchievementManager) manager = NULL;
    g_autoptr(LrgAchievementCriterion) kills = NULL;
    g_autoptr(LrgAchievementCriterion) level = NULL;
    g_autoptr(GPtrArray) list = NULL;
    g_auto(GStrv) categories = NULL;
    LrgAchievement *a;
    LrgAchievement *b;
    guint moved = 0;
    guint unlocks = 0;
    guint earned = 0;
    guint total = 0;

    manager = lrg_achievement_manager_new ();
    g_assert_true (manager != lrg_achievement_manager_get_default ());
    a = lrg_achievement_new ("veteran", "Veteran", "Win fights at a level");
    lrg_achievement_set_category (a, "combat");
    lrg_achievement_set_points (a, 10);
    lrg_achievement_set_scope (a, LRG_ACHIEVEMENT_SCOPE_CHARACTER);
    kills = lrg_achievement_criterion_new ("kills", "Win fights", 2);
    level = lrg_achievement_criterion_new ("level", "Reach level 5", 5);
    g_assert_true (lrg_achievement_add_criterion (a, kills));
    g_assert_true (lrg_achievement_add_criterion (a, level));
    g_assert_false (lrg_achievement_add_criterion (a, kills));
    lrg_achievement_manager_register (manager, a);
    b = lrg_achievement_new ("explorer", "Explorer", NULL);
    lrg_achievement_set_category (b, "exploration");
    lrg_achievement_set_points (b, 5);
    lrg_achievement_manager_register (manager, b);
    g_signal_connect (manager, "criterion-changed", G_CALLBACK (on_criterion_counted), &moved);
    g_signal_connect (manager, "achievement-unlocked", G_CALLBACK (on_unlock_counted), &unlocks);

    g_assert_true (lrg_achievement_increment_criterion (a, "kills", 1));
    g_assert_true (lrg_achievement_increment_criterion (a, "kills", 5));
    g_assert_false (lrg_achievement_increment_criterion (a, "kills", 1));
    g_assert_false (lrg_achievement_increment_criterion (a, "nope", 1));
    g_assert_false (lrg_achievement_is_unlocked (a));
    g_assert_cmpfloat_with_epsilon (lrg_achievement_get_fraction (a), 0.5, 1e-9);
    g_assert_true (lrg_achievement_set_criterion_progress (a, "level", 5));
    g_assert_true (lrg_achievement_is_unlocked (a));
    g_assert_cmpuint (moved, ==, 3);
    g_assert_cmpuint (unlocks, ==, 1);
    /* nothing moves once unlocked */
    g_assert_false (lrg_achievement_set_criterion_progress (a, "level", 1));

    list = lrg_achievement_manager_list (manager, "combat");
    g_assert_cmpuint (list->len, ==, 1);
    g_assert_true (g_ptr_array_index (list, 0) == (gpointer)a);
    categories = lrg_achievement_manager_get_categories (manager);
    g_assert_cmpuint (g_strv_length (categories), ==, 2);
    g_assert_cmpstr (categories[0], ==, "combat");
    g_assert_cmpstr (categories[1], ==, "exploration");
    g_assert_cmpuint (lrg_achievement_manager_count_points (manager, NULL, &earned, &total), ==, 1);
    g_assert_cmpuint (earned, ==, 10);
    g_assert_cmpuint (total, ==, 15);
    g_assert_true (lrg_achievement_manager_unregister (manager, "explorer"));
    g_assert_cmpuint (lrg_achievement_manager_count_points (manager, NULL, NULL, &total), ==, 1);
    g_assert_cmpuint (total, ==, 10);
}

/* each scope saves its own state; unknown entries wait for their achievement */
static void
test_achievement_state_scopes (void)
{
    g_autoptr(LrgAchievementManager) manager = NULL;
    g_autoptr(LrgAchievementManager) other = NULL;
    g_autoptr(LrgAchievementCriterion) towns = NULL;
    g_autoptr(GVariant) character = NULL;
    g_autoptr(GVariant) account = NULL;
    g_autoptr(GVariant) again = NULL;
    g_autoptr(GVariant) bad = NULL;
    g_autoptr(GError) error = NULL;
    LrgAchievement *hero;
    LrgAchievement *seen;
    LrgAchievement *copy;
    guint unlocks = 0;

    manager = lrg_achievement_manager_new ();
    hero = lrg_achievement_new ("hero", "Hero", NULL);
    lrg_achievement_set_scope (hero, LRG_ACHIEVEMENT_SCOPE_CHARACTER);
    towns = lrg_achievement_criterion_new ("towns", NULL, 5);
    lrg_achievement_add_criterion (hero, towns);
    lrg_achievement_manager_register (manager, hero);
    seen = lrg_achievement_new ("seen", "Seen it all", NULL);
    lrg_achievement_manager_register (manager, seen);
    lrg_achievement_add_criterion_key (hero, "towns", "Daggerfall");
    lrg_achievement_add_criterion_key (hero, "towns", "Wayrest");
    lrg_achievement_unlock (seen);

    character = lrg_achievement_manager_save_state (manager, LRG_ACHIEVEMENT_SCOPE_CHARACTER);
    account = lrg_achievement_manager_save_state (manager, LRG_ACHIEVEMENT_SCOPE_ACCOUNT);
    g_assert_true (g_variant_is_of_type (character, G_VARIANT_TYPE (LRG_ACHIEVEMENT_STATE_TYPE)));
    g_assert_cmpuint (g_variant_n_children (character), ==, 1);
    g_assert_cmpuint (g_variant_n_children (account), ==, 1);

    /* a refused variant changes nothing */
    bad = g_variant_ref_sink (g_variant_new_string ("nope"));
    g_assert_false (lrg_achievement_manager_load_state (manager, LRG_ACHIEVEMENT_SCOPE_CHARACTER, bad, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);
    g_assert_cmpint (lrg_achievement_criterion_get_current (towns), ==, 2);

    /* a new character: the character scope resets, the account keeps */
    lrg_achievement_manager_reset_scope (manager, LRG_ACHIEVEMENT_SCOPE_CHARACTER);
    g_assert_cmpint (lrg_achievement_criterion_get_current (towns), ==, 0);
    g_assert_true (lrg_achievement_is_unlocked (seen));

    /* loading restores silently */
    g_signal_connect (manager, "achievement-unlocked", G_CALLBACK (on_unlock_counted), &unlocks);
    g_assert_true (lrg_achievement_manager_load_state (manager, LRG_ACHIEVEMENT_SCOPE_CHARACTER, character, &error));
    g_assert_no_error (error);
    g_assert_cmpint (lrg_achievement_criterion_get_current (towns), ==, 2);
    g_assert_true (lrg_achievement_criterion_has_key (towns, "Wayrest"));
    g_assert_false (lrg_achievement_add_criterion_key (hero, "towns", "Wayrest"));

    /* a manager without the achievement keeps the entry until it registers */
    other = lrg_achievement_manager_new ();
    g_assert_true (lrg_achievement_manager_load_state (other, LRG_ACHIEVEMENT_SCOPE_ACCOUNT, account, &error));
    again = lrg_achievement_manager_save_state (other, LRG_ACHIEVEMENT_SCOPE_ACCOUNT);
    g_assert_cmpuint (g_variant_n_children (again), ==, 1);
    copy = lrg_achievement_new ("seen", "Seen it all", NULL);
    lrg_achievement_manager_register (other, copy);
    g_assert_true (lrg_achievement_is_unlocked (copy));
    g_assert_nonnull (lrg_achievement_get_unlock_time (copy));
    g_assert_cmpuint (unlocks, ==, 0);

    /* unregistering keeps the progress for the next registration */
    g_assert_true (lrg_achievement_manager_unregister (manager, "hero"));
    {
        g_autoptr(GVariant) kept = lrg_achievement_manager_save_state (manager, LRG_ACHIEVEMENT_SCOPE_CHARACTER);
        g_autoptr(LrgAchievementCriterion) towns2 = lrg_achievement_criterion_new ("towns", NULL, 5);
        LrgAchievement *hero2 = lrg_achievement_new ("hero", "Hero", NULL);

        g_assert_cmpuint (g_variant_n_children (kept), ==, 1);
        lrg_achievement_set_scope (hero2, LRG_ACHIEVEMENT_SCOPE_CHARACTER);
        lrg_achievement_add_criterion (hero2, towns2);
        lrg_achievement_manager_register (manager, hero2);
        g_assert_cmpint (lrg_achievement_criterion_get_current (towns2), ==, 2);
    }
}

static void
test_achievement_toasts (void)
{
    g_autoptr(LrgAchievementManager) manager = NULL;
    g_autoptr(LrgAchievementToasts) toasts = NULL;
    LrgAchievement *a;
    LrgAchievement *b;

    manager = lrg_achievement_manager_new ();
    a = lrg_achievement_new ("a", "First", "The first");
    lrg_achievement_set_points (a, 10);
    b = lrg_achievement_new ("b", "Second", NULL);
    lrg_achievement_manager_register (manager, a);
    lrg_achievement_manager_register (manager, b);
    toasts = lrg_achievement_toasts_new ();
    g_object_set (toasts, "duration", 4.0, "fade", 1.0, NULL);
    lrg_achievement_toasts_follow (toasts, manager);

    lrg_achievement_manager_unlock (manager, "a");
    lrg_achievement_manager_unlock (manager, "b");
    g_assert_cmpuint (lrg_achievement_toasts_get_count (toasts), ==, 1);
    g_assert_cmpuint (lrg_achievement_toasts_get_waiting (toasts), ==, 1);
    g_assert_cmpstr (lrg_achievement_toast_get_name (lrg_achievement_toasts_get_toast (toasts, 0)), ==, "First");
    g_assert_cmpuint (lrg_achievement_toast_get_points (lrg_achievement_toasts_get_toast (toasts, 0)), ==, 10);
    g_assert_cmpfloat (lrg_achievement_toasts_get_alpha (toasts, 0), ==, 0.0);
    lrg_achievement_toasts_update (toasts, 0.5);
    g_assert_cmpfloat_with_epsilon (lrg_achievement_toasts_get_alpha (toasts, 0), 0.5, 1e-9);
    g_assert_cmpfloat_with_epsilon (lrg_achievement_toasts_get_slide (toasts, 0), 0.75, 1e-9);
    lrg_achievement_toasts_update (toasts, 1.5);
    g_assert_cmpfloat_with_epsilon (lrg_achievement_toasts_get_alpha (toasts, 0), 1.0, 1e-9);
    lrg_achievement_toasts_update (toasts, 1.5);
    g_assert_cmpfloat_with_epsilon (lrg_achievement_toasts_get_alpha (toasts, 0), 0.5, 1e-9);
    lrg_achievement_toasts_update (toasts, 0.6);
    /* the first ended: the second shows */
    g_assert_cmpuint (lrg_achievement_toasts_get_count (toasts), ==, 1);
    g_assert_cmpuint (lrg_achievement_toasts_get_waiting (toasts), ==, 0);
    g_assert_cmpstr (lrg_achievement_toast_get_id (lrg_achievement_toasts_get_toast (toasts, 0)), ==, "b");
    g_object_set (toasts, "max-visible", 3, NULL);
    lrg_achievement_toasts_push (toasts, a);
    g_assert_cmpuint (lrg_achievement_toasts_get_count (toasts), ==, 2);
    lrg_achievement_toasts_clear (toasts);
    g_assert_cmpuint (lrg_achievement_toasts_get_count (toasts), ==, 0);
    g_assert_null (lrg_achievement_toasts_get_toast (toasts, 0));
    lrg_achievement_toasts_follow (toasts, NULL);
    lrg_achievement_manager_reset_all (manager);
    lrg_achievement_manager_unlock (manager, "a");
    g_assert_cmpuint (lrg_achievement_toasts_get_count (toasts), ==, 0);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* LrgAchievementProgress tests */
    g_test_add_func ("/achievement/progress/new", test_achievement_progress_new);
    g_test_add_func ("/achievement/progress/percentage", test_achievement_progress_percentage);
    g_test_add_func ("/achievement/progress/complete", test_achievement_progress_complete);
    g_test_add_func ("/achievement/progress/copy", test_achievement_progress_copy);
    g_test_add_func ("/achievement/progress/set", test_achievement_progress_set);

    /* LrgAchievement tests */
    g_test_add_func ("/achievement/achievement/new", test_achievement_new);
    g_test_add_func ("/achievement/achievement/properties", test_achievement_properties);
    g_test_add_func ("/achievement/achievement/unlock", test_achievement_unlock);
    g_test_add_func ("/achievement/achievement/custom_check_unlock", test_achievement_custom_check_unlock);
    g_test_add_func ("/achievement/achievement/on_unlocked_virtual", test_achievement_on_unlocked_virtual);
    g_test_add_func ("/achievement/achievement/progress", test_achievement_progress);

    /* LrgAchievementManager tests */
    g_test_add_func ("/achievement/manager/singleton", test_achievement_manager_singleton);

    g_test_add ("/achievement/manager/register", AchievementFixture, NULL,
                achievement_fixture_set_up, test_achievement_manager_register, achievement_fixture_tear_down);
    g_test_add ("/achievement/manager/get", AchievementFixture, NULL,
                achievement_fixture_set_up, test_achievement_manager_get, achievement_fixture_tear_down);
    g_test_add ("/achievement/manager/unlock", AchievementFixture, NULL,
                achievement_fixture_set_up, test_achievement_manager_unlock, achievement_fixture_tear_down);
    g_test_add ("/achievement/manager/count", AchievementFixture, NULL,
                achievement_fixture_set_up, test_achievement_manager_count, achievement_fixture_tear_down);
    g_test_add ("/achievement/manager/get_all", AchievementFixture, NULL,
                achievement_fixture_set_up, test_achievement_manager_get_all, achievement_fixture_tear_down);
    g_test_add ("/achievement/manager/get_unlocked", AchievementFixture, NULL,
                achievement_fixture_set_up, test_achievement_manager_get_unlocked, achievement_fixture_tear_down);
    g_test_add ("/achievement/manager/stats", AchievementFixture, NULL,
                achievement_fixture_set_up, test_achievement_manager_stats, achievement_fixture_tear_down);
    g_test_add ("/achievement/manager/reset", AchievementFixture, NULL,
                achievement_fixture_set_up, test_achievement_manager_reset, achievement_fixture_tear_down);
    g_test_add ("/achievement/manager/signal_unlocked", AchievementFixture, NULL,
                achievement_fixture_set_up, test_achievement_manager_signal_unlocked, achievement_fixture_tear_down);
    g_test_add ("/achievement/manager/signal_progress", AchievementFixture, NULL,
                achievement_fixture_set_up, test_achievement_manager_signal_progress, achievement_fixture_tear_down);

    /* LrgAchievementNotification tests */
    g_test_add_func ("/achievement/notification/new", test_achievement_notification_new);
    g_test_add_func ("/achievement/notification/duration", test_achievement_notification_duration);
    g_test_add_func ("/achievement/notification/fade_duration", test_achievement_notification_fade_duration);
    g_test_add_func ("/achievement/notification/position", test_achievement_notification_position);
    g_test_add_func ("/achievement/notification/show_hide", test_achievement_notification_show_hide);
    g_test_add_func ("/achievement/notification/update", test_achievement_notification_update);

    g_test_add_func ("/achievement/criterion/progress", test_achievement_criterion);
    g_test_add_func ("/achievement/criterion/unlock", test_achievement_criteria_unlock);
    g_test_add_func ("/achievement/manager/state-scopes", test_achievement_state_scopes);
    g_test_add_func ("/achievement/toasts", test_achievement_toasts);

    return g_test_run ();
}
