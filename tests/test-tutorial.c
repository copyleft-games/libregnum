/* test-tutorial.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the tutorial module.
 */

#include "../src/tutorial/lrg-tutorial-step.h"
#include "../src/tutorial/lrg-tutorial.h"
#include "../src/tutorial/lrg-tutorial-manager.h"
#include "../src/tutorial/lrg-highlight.h"
#include "../src/tutorial/lrg-input-prompt.h"
#include "../src/tutorial/lrg-tooltip-arrow.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <unistd.h>

/* ========================================================================== */
/*                          Tutorial Step Tests                               */
/* ========================================================================== */

static void
test_tutorial_step_new (void)
{
    LrgTutorialStep *step;

    step = lrg_tutorial_step_new (LRG_TUTORIAL_STEP_TEXT);

    g_assert_nonnull (step);
    g_assert_cmpint (lrg_tutorial_step_get_step_type (step), ==, LRG_TUTORIAL_STEP_TEXT);

    lrg_tutorial_step_free (step);
}

static void
test_tutorial_step_new_text (void)
{
    LrgTutorialStep *step;

    step = lrg_tutorial_step_new_text ("Press W to move forward", "Guide");

    g_assert_nonnull (step);
    g_assert_cmpint (lrg_tutorial_step_get_step_type (step), ==, LRG_TUTORIAL_STEP_TEXT);
    g_assert_cmpstr (lrg_tutorial_step_get_text (step), ==, "Press W to move forward");
    g_assert_cmpstr (lrg_tutorial_step_get_speaker (step), ==, "Guide");

    lrg_tutorial_step_free (step);
}

static void
test_tutorial_step_new_highlight (void)
{
    LrgTutorialStep *step;

    step = lrg_tutorial_step_new_highlight ("inventory_button", LRG_HIGHLIGHT_STYLE_GLOW);

    g_assert_nonnull (step);
    g_assert_cmpint (lrg_tutorial_step_get_step_type (step), ==, LRG_TUTORIAL_STEP_HIGHLIGHT);
    g_assert_cmpstr (lrg_tutorial_step_get_target_id (step), ==, "inventory_button");
    g_assert_cmpint (lrg_tutorial_step_get_highlight_style (step), ==, LRG_HIGHLIGHT_STYLE_GLOW);

    lrg_tutorial_step_free (step);
}

static void
test_tutorial_step_new_input (void)
{
    LrgTutorialStep *step;

    step = lrg_tutorial_step_new_input ("jump", TRUE);

    g_assert_nonnull (step);
    g_assert_cmpint (lrg_tutorial_step_get_step_type (step), ==, LRG_TUTORIAL_STEP_INPUT);
    g_assert_cmpstr (lrg_tutorial_step_get_action_name (step), ==, "jump");
    g_assert_true (lrg_tutorial_step_get_show_prompt (step));

    lrg_tutorial_step_free (step);
}

static void
test_tutorial_step_new_condition (void)
{
    LrgTutorialStep *step;

    step = lrg_tutorial_step_new_condition ("has_sword");

    g_assert_nonnull (step);
    g_assert_cmpint (lrg_tutorial_step_get_step_type (step), ==, LRG_TUTORIAL_STEP_CONDITION);
    g_assert_cmpstr (lrg_tutorial_step_get_condition_id (step), ==, "has_sword");

    lrg_tutorial_step_free (step);
}

static void
test_tutorial_step_new_delay (void)
{
    LrgTutorialStep *step;

    step = lrg_tutorial_step_new_delay (2.5f);

    g_assert_nonnull (step);
    g_assert_cmpint (lrg_tutorial_step_get_step_type (step), ==, LRG_TUTORIAL_STEP_DELAY);
    g_assert_cmpfloat_with_epsilon (lrg_tutorial_step_get_duration (step), 2.5f, 0.001f);

    lrg_tutorial_step_free (step);
}

static void
test_tutorial_step_copy (void)
{
    LrgTutorialStep *original;
    LrgTutorialStep *copy;

    original = lrg_tutorial_step_new_text ("Original text", "Speaker");
    lrg_tutorial_step_set_id (original, "step_01");
    lrg_tutorial_step_set_can_skip (original, TRUE);
    lrg_tutorial_step_set_blocks_input (original, TRUE);

    copy = lrg_tutorial_step_copy (original);

    g_assert_nonnull (copy);
    g_assert_cmpstr (lrg_tutorial_step_get_id (copy), ==, "step_01");
    g_assert_cmpstr (lrg_tutorial_step_get_text (copy), ==, "Original text");
    g_assert_cmpstr (lrg_tutorial_step_get_speaker (copy), ==, "Speaker");
    g_assert_true (lrg_tutorial_step_get_can_skip (copy));
    g_assert_true (lrg_tutorial_step_get_blocks_input (copy));

    lrg_tutorial_step_free (original);
    lrg_tutorial_step_free (copy);
}

static void
test_tutorial_step_id (void)
{
    LrgTutorialStep *step;

    step = lrg_tutorial_step_new (LRG_TUTORIAL_STEP_TEXT);

    g_assert_null (lrg_tutorial_step_get_id (step));

    lrg_tutorial_step_set_id (step, "my_step");
    g_assert_cmpstr (lrg_tutorial_step_get_id (step), ==, "my_step");

    lrg_tutorial_step_free (step);
}

static void
test_tutorial_step_properties (void)
{
    LrgTutorialStep *step;
    gfloat x, y;

    step = lrg_tutorial_step_new (LRG_TUTORIAL_STEP_TEXT);

    /* Test skip/block/auto-advance flags (can_skip defaults to TRUE) */
    g_assert_true (lrg_tutorial_step_get_can_skip (step));
    g_assert_false (lrg_tutorial_step_get_blocks_input (step));
    g_assert_false (lrg_tutorial_step_get_auto_advance (step));

    lrg_tutorial_step_set_can_skip (step, FALSE);
    lrg_tutorial_step_set_blocks_input (step, TRUE);
    lrg_tutorial_step_set_auto_advance (step, TRUE);

    g_assert_false (lrg_tutorial_step_get_can_skip (step));
    g_assert_true (lrg_tutorial_step_get_blocks_input (step));
    g_assert_true (lrg_tutorial_step_get_auto_advance (step));

    /* Test position */
    lrg_tutorial_step_set_position (step, 100.0f, 200.0f);
    lrg_tutorial_step_get_position (step, &x, &y);
    g_assert_cmpfloat_with_epsilon (x, 100.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (y, 200.0f, 0.001f);

    /* Test arrow direction */
    lrg_tutorial_step_set_arrow_direction (step, LRG_ARROW_DIRECTION_LEFT);
    g_assert_cmpint (lrg_tutorial_step_get_arrow_direction (step), ==, LRG_ARROW_DIRECTION_LEFT);

    lrg_tutorial_step_free (step);
}

/* ========================================================================== */
/*                            Tutorial Tests                                  */
/* ========================================================================== */

typedef struct
{
    LrgTutorial *tutorial;
} TutorialFixture;

static void
tutorial_fixture_set_up (TutorialFixture *fixture,
                         gconstpointer    user_data)
{
    fixture->tutorial = lrg_tutorial_new ("basic_movement", "Basic Movement");
}

static void
tutorial_fixture_tear_down (TutorialFixture *fixture,
                            gconstpointer    user_data)
{
    g_object_unref (fixture->tutorial);
}

static void
test_tutorial_new (TutorialFixture *fixture,
                   gconstpointer    user_data)
{
    g_assert_nonnull (fixture->tutorial);
    g_assert_cmpstr (lrg_tutorial_get_id (fixture->tutorial), ==, "basic_movement");
    g_assert_cmpstr (lrg_tutorial_get_name (fixture->tutorial), ==, "Basic Movement");
    g_assert_cmpint (lrg_tutorial_get_state (fixture->tutorial), ==, LRG_TUTORIAL_STATE_INACTIVE);
    g_assert_cmpuint (lrg_tutorial_get_step_count (fixture->tutorial), ==, 0);
}

static void
test_tutorial_description (TutorialFixture *fixture,
                           gconstpointer    user_data)
{
    g_assert_null (lrg_tutorial_get_description (fixture->tutorial));

    lrg_tutorial_set_description (fixture->tutorial, "Learn basic movement controls");
    g_assert_cmpstr (lrg_tutorial_get_description (fixture->tutorial), ==, "Learn basic movement controls");
}

static void
test_tutorial_repeatable (TutorialFixture *fixture,
                          gconstpointer    user_data)
{
    g_assert_false (lrg_tutorial_is_repeatable (fixture->tutorial));

    lrg_tutorial_set_repeatable (fixture->tutorial, TRUE);
    g_assert_true (lrg_tutorial_is_repeatable (fixture->tutorial));
}

static void
test_tutorial_skippable (TutorialFixture *fixture,
                         gconstpointer    user_data)
{
    /* Default should be TRUE */
    g_assert_true (lrg_tutorial_is_skippable (fixture->tutorial));

    lrg_tutorial_set_skippable (fixture->tutorial, FALSE);
    g_assert_false (lrg_tutorial_is_skippable (fixture->tutorial));
}

static void
test_tutorial_add_step (TutorialFixture *fixture,
                        gconstpointer    user_data)
{
    LrgTutorialStep *step1;
    LrgTutorialStep *step2;
    guint index;

    step1 = lrg_tutorial_step_new_text ("Step 1 text", NULL);
    step2 = lrg_tutorial_step_new_text ("Step 2 text", NULL);

    index = lrg_tutorial_add_step (fixture->tutorial, step1);
    g_assert_cmpuint (index, ==, 0);
    g_assert_cmpuint (lrg_tutorial_get_step_count (fixture->tutorial), ==, 1);

    index = lrg_tutorial_add_step (fixture->tutorial, step2);
    g_assert_cmpuint (index, ==, 1);
    g_assert_cmpuint (lrg_tutorial_get_step_count (fixture->tutorial), ==, 2);

    lrg_tutorial_step_free (step1);
    lrg_tutorial_step_free (step2);
}

static void
test_tutorial_get_step (TutorialFixture *fixture,
                        gconstpointer    user_data)
{
    LrgTutorialStep *step;
    LrgTutorialStep *retrieved;

    step = lrg_tutorial_step_new_text ("Test text", NULL);
    lrg_tutorial_step_set_id (step, "test_step");
    lrg_tutorial_add_step (fixture->tutorial, step);

    retrieved = lrg_tutorial_get_step (fixture->tutorial, 0);
    g_assert_nonnull (retrieved);
    g_assert_cmpstr (lrg_tutorial_step_get_text (retrieved), ==, "Test text");

    retrieved = lrg_tutorial_get_step_by_id (fixture->tutorial, "test_step");
    g_assert_nonnull (retrieved);

    retrieved = lrg_tutorial_get_step (fixture->tutorial, 100);
    g_assert_null (retrieved);

    lrg_tutorial_step_free (step);
}

static void
test_tutorial_remove_step (TutorialFixture *fixture,
                           gconstpointer    user_data)
{
    LrgTutorialStep *step1;
    LrgTutorialStep *step2;
    gboolean removed;

    step1 = lrg_tutorial_step_new_text ("Step 1", NULL);
    step2 = lrg_tutorial_step_new_text ("Step 2", NULL);
    lrg_tutorial_add_step (fixture->tutorial, step1);
    lrg_tutorial_add_step (fixture->tutorial, step2);

    g_assert_cmpuint (lrg_tutorial_get_step_count (fixture->tutorial), ==, 2);

    removed = lrg_tutorial_remove_step (fixture->tutorial, 0);
    g_assert_true (removed);
    g_assert_cmpuint (lrg_tutorial_get_step_count (fixture->tutorial), ==, 1);

    /* Step 2 is now at index 0 */
    g_assert_cmpstr (lrg_tutorial_step_get_text (lrg_tutorial_get_step (fixture->tutorial, 0)), ==, "Step 2");

    lrg_tutorial_step_free (step1);
    lrg_tutorial_step_free (step2);
}

static void
test_tutorial_clear_steps (TutorialFixture *fixture,
                           gconstpointer    user_data)
{
    LrgTutorialStep *step;

    step = lrg_tutorial_step_new_text ("Text", NULL);
    lrg_tutorial_add_step (fixture->tutorial, step);
    lrg_tutorial_add_step (fixture->tutorial, step);
    lrg_tutorial_add_step (fixture->tutorial, step);

    g_assert_cmpuint (lrg_tutorial_get_step_count (fixture->tutorial), ==, 3);

    lrg_tutorial_clear_steps (fixture->tutorial);
    g_assert_cmpuint (lrg_tutorial_get_step_count (fixture->tutorial), ==, 0);

    lrg_tutorial_step_free (step);
}

static void
test_tutorial_start (TutorialFixture *fixture,
                     gconstpointer    user_data)
{
    LrgTutorialStep *step;
    gboolean started;

    /* Cannot start with no steps */
    started = lrg_tutorial_start (fixture->tutorial);
    g_assert_false (started);

    /* Add steps and start */
    step = lrg_tutorial_step_new_text ("Step text", NULL);
    lrg_tutorial_add_step (fixture->tutorial, step);

    started = lrg_tutorial_start (fixture->tutorial);
    g_assert_true (started);
    g_assert_cmpint (lrg_tutorial_get_state (fixture->tutorial), ==, LRG_TUTORIAL_STATE_ACTIVE);
    g_assert_cmpuint (lrg_tutorial_get_current_step_index (fixture->tutorial), ==, 0);

    lrg_tutorial_step_free (step);
}

static void
test_tutorial_advance (TutorialFixture *fixture,
                       gconstpointer    user_data)
{
    LrgTutorialStep *step1;
    LrgTutorialStep *step2;
    gboolean advanced;

    step1 = lrg_tutorial_step_new_text ("Step 1", NULL);
    step2 = lrg_tutorial_step_new_text ("Step 2", NULL);
    lrg_tutorial_add_step (fixture->tutorial, step1);
    lrg_tutorial_add_step (fixture->tutorial, step2);

    lrg_tutorial_start (fixture->tutorial);
    g_assert_cmpuint (lrg_tutorial_get_current_step_index (fixture->tutorial), ==, 0);

    advanced = lrg_tutorial_advance (fixture->tutorial);
    g_assert_true (advanced);
    g_assert_cmpuint (lrg_tutorial_get_current_step_index (fixture->tutorial), ==, 1);

    /* Advance past last step should complete tutorial */
    advanced = lrg_tutorial_advance (fixture->tutorial);
    g_assert_false (advanced);
    g_assert_cmpint (lrg_tutorial_get_state (fixture->tutorial), ==, LRG_TUTORIAL_STATE_COMPLETED);

    lrg_tutorial_step_free (step1);
    lrg_tutorial_step_free (step2);
}

static void
test_tutorial_pause_resume (TutorialFixture *fixture,
                            gconstpointer    user_data)
{
    LrgTutorialStep *step;

    step = lrg_tutorial_step_new_text ("Text", NULL);
    lrg_tutorial_add_step (fixture->tutorial, step);

    lrg_tutorial_start (fixture->tutorial);
    g_assert_cmpint (lrg_tutorial_get_state (fixture->tutorial), ==, LRG_TUTORIAL_STATE_ACTIVE);

    lrg_tutorial_pause (fixture->tutorial);
    g_assert_cmpint (lrg_tutorial_get_state (fixture->tutorial), ==, LRG_TUTORIAL_STATE_PAUSED);

    lrg_tutorial_resume (fixture->tutorial);
    g_assert_cmpint (lrg_tutorial_get_state (fixture->tutorial), ==, LRG_TUTORIAL_STATE_ACTIVE);

    lrg_tutorial_step_free (step);
}

static void
test_tutorial_skip (TutorialFixture *fixture,
                    gconstpointer    user_data)
{
    LrgTutorialStep *step;
    gboolean skipped;

    step = lrg_tutorial_step_new_text ("Text", NULL);
    lrg_tutorial_add_step (fixture->tutorial, step);

    lrg_tutorial_start (fixture->tutorial);

    skipped = lrg_tutorial_skip (fixture->tutorial);
    g_assert_true (skipped);
    g_assert_cmpint (lrg_tutorial_get_state (fixture->tutorial), ==, LRG_TUTORIAL_STATE_SKIPPED);

    lrg_tutorial_step_free (step);
}

static void
test_tutorial_progress (TutorialFixture *fixture,
                        gconstpointer    user_data)
{
    LrgTutorialStep *step;
    gfloat progress;

    step = lrg_tutorial_step_new_text ("Text", NULL);
    lrg_tutorial_add_step (fixture->tutorial, step);
    lrg_tutorial_add_step (fixture->tutorial, step);
    lrg_tutorial_add_step (fixture->tutorial, step);
    lrg_tutorial_add_step (fixture->tutorial, step);

    lrg_tutorial_start (fixture->tutorial);

    progress = lrg_tutorial_get_progress (fixture->tutorial);
    g_assert_cmpfloat_with_epsilon (progress, 0.0f, 0.001f);

    lrg_tutorial_advance (fixture->tutorial);
    progress = lrg_tutorial_get_progress (fixture->tutorial);
    g_assert_cmpfloat_with_epsilon (progress, 0.25f, 0.001f);

    lrg_tutorial_advance (fixture->tutorial);
    progress = lrg_tutorial_get_progress (fixture->tutorial);
    g_assert_cmpfloat_with_epsilon (progress, 0.5f, 0.001f);

    lrg_tutorial_step_free (step);
}

static void
test_tutorial_go_to_step (TutorialFixture *fixture,
                          gconstpointer    user_data)
{
    LrgTutorialStep *step;
    gboolean success;

    step = lrg_tutorial_step_new_text ("Text", NULL);
    lrg_tutorial_add_step (fixture->tutorial, step);
    lrg_tutorial_add_step (fixture->tutorial, step);
    lrg_tutorial_add_step (fixture->tutorial, step);

    lrg_tutorial_start (fixture->tutorial);

    success = lrg_tutorial_go_to_step (fixture->tutorial, 2);
    g_assert_true (success);
    g_assert_cmpuint (lrg_tutorial_get_current_step_index (fixture->tutorial), ==, 2);

    success = lrg_tutorial_go_to_step (fixture->tutorial, 0);
    g_assert_true (success);
    g_assert_cmpuint (lrg_tutorial_get_current_step_index (fixture->tutorial), ==, 0);

    /* Out of bounds should fail */
    success = lrg_tutorial_go_to_step (fixture->tutorial, 100);
    g_assert_false (success);

    lrg_tutorial_step_free (step);
}

static void
test_tutorial_reset (TutorialFixture *fixture,
                     gconstpointer    user_data)
{
    LrgTutorialStep *step;

    step = lrg_tutorial_step_new_text ("Text", NULL);
    lrg_tutorial_add_step (fixture->tutorial, step);

    lrg_tutorial_start (fixture->tutorial);
    g_assert_cmpint (lrg_tutorial_get_state (fixture->tutorial), ==, LRG_TUTORIAL_STATE_ACTIVE);

    lrg_tutorial_reset (fixture->tutorial);
    g_assert_cmpint (lrg_tutorial_get_state (fixture->tutorial), ==, LRG_TUTORIAL_STATE_INACTIVE);
    g_assert_cmpuint (lrg_tutorial_get_current_step_index (fixture->tutorial), ==, G_MAXUINT);

    lrg_tutorial_step_free (step);
}

/* ========================================================================== */
/*                        Tutorial Manager Tests                              */
/* ========================================================================== */

typedef struct
{
    LrgTutorialManager *manager;
} ManagerFixture;

static void
manager_fixture_set_up (ManagerFixture *fixture,
                        gconstpointer   user_data)
{
    fixture->manager = lrg_tutorial_manager_new ();
}

static void
manager_fixture_tear_down (ManagerFixture *fixture,
                           gconstpointer   user_data)
{
    g_object_unref (fixture->manager);
}

static void
test_tutorial_manager_new (void)
{
    LrgTutorialManager *manager;

    manager = lrg_tutorial_manager_new ();

    g_assert_nonnull (manager);
    g_assert_null (lrg_tutorial_manager_get_active_tutorial (manager));

    g_object_unref (manager);
}

static void
test_tutorial_manager_register (ManagerFixture *fixture,
                                gconstpointer   user_data)
{
    LrgTutorial *tutorial;
    gboolean registered;

    tutorial = lrg_tutorial_new ("test_tutorial", "Test Tutorial");

    registered = lrg_tutorial_manager_register (fixture->manager, tutorial);
    g_assert_true (registered);

    /* Duplicate registration should fail */
    registered = lrg_tutorial_manager_register (fixture->manager, tutorial);
    g_assert_false (registered);

    g_object_unref (tutorial);
}

static void
test_tutorial_manager_get_tutorial (ManagerFixture *fixture,
                                    gconstpointer   user_data)
{
    LrgTutorial *tutorial;
    LrgTutorial *retrieved;

    tutorial = lrg_tutorial_new ("my_tutorial", "My Tutorial");
    lrg_tutorial_manager_register (fixture->manager, tutorial);

    retrieved = lrg_tutorial_manager_get_tutorial (fixture->manager, "my_tutorial");
    g_assert_nonnull (retrieved);
    g_assert_true (retrieved == tutorial);

    retrieved = lrg_tutorial_manager_get_tutorial (fixture->manager, "nonexistent");
    g_assert_null (retrieved);

    g_object_unref (tutorial);
}

static void
test_tutorial_manager_unregister (ManagerFixture *fixture,
                                  gconstpointer   user_data)
{
    LrgTutorial *tutorial;
    gboolean unregistered;

    tutorial = lrg_tutorial_new ("to_remove", "To Remove");
    lrg_tutorial_manager_register (fixture->manager, tutorial);

    unregistered = lrg_tutorial_manager_unregister (fixture->manager, "to_remove");
    g_assert_true (unregistered);

    g_assert_null (lrg_tutorial_manager_get_tutorial (fixture->manager, "to_remove"));

    /* Second unregister should fail */
    unregistered = lrg_tutorial_manager_unregister (fixture->manager, "to_remove");
    g_assert_false (unregistered);

    g_object_unref (tutorial);
}

static void
test_tutorial_manager_get_tutorials (ManagerFixture *fixture,
                                     gconstpointer   user_data)
{
    LrgTutorial *tutorial1;
    LrgTutorial *tutorial2;
    GList *tutorials;

    tutorial1 = lrg_tutorial_new ("tut1", "Tutorial 1");
    tutorial2 = lrg_tutorial_new ("tut2", "Tutorial 2");

    lrg_tutorial_manager_register (fixture->manager, tutorial1);
    lrg_tutorial_manager_register (fixture->manager, tutorial2);

    tutorials = lrg_tutorial_manager_get_tutorials (fixture->manager);
    g_assert_nonnull (tutorials);
    g_assert_cmpuint (g_list_length (tutorials), ==, 2);

    g_list_free (tutorials);
    g_object_unref (tutorial1);
    g_object_unref (tutorial2);
}

static void
test_tutorial_manager_start_tutorial (ManagerFixture *fixture,
                                      gconstpointer   user_data)
{
    LrgTutorial *tutorial;
    LrgTutorialStep *step;
    gboolean started;

    tutorial = lrg_tutorial_new ("starter", "Starter Tutorial");
    step = lrg_tutorial_step_new_text ("Welcome!", NULL);
    lrg_tutorial_add_step (tutorial, step);

    lrg_tutorial_manager_register (fixture->manager, tutorial);

    started = lrg_tutorial_manager_start_tutorial (fixture->manager, "starter");
    g_assert_true (started);

    g_assert_nonnull (lrg_tutorial_manager_get_active_tutorial (fixture->manager));
    g_assert_true (lrg_tutorial_manager_get_active_tutorial (fixture->manager) == tutorial);

    /* Starting nonexistent should fail */
    started = lrg_tutorial_manager_start_tutorial (fixture->manager, "nonexistent");
    g_assert_false (started);

    lrg_tutorial_step_free (step);
    g_object_unref (tutorial);
}

static void
test_tutorial_manager_stop_active (ManagerFixture *fixture,
                                   gconstpointer   user_data)
{
    LrgTutorial *tutorial;
    LrgTutorialStep *step;

    tutorial = lrg_tutorial_new ("active", "Active Tutorial");
    step = lrg_tutorial_step_new_text ("Text", NULL);
    lrg_tutorial_add_step (tutorial, step);

    lrg_tutorial_manager_register (fixture->manager, tutorial);
    lrg_tutorial_manager_start_tutorial (fixture->manager, "active");

    g_assert_nonnull (lrg_tutorial_manager_get_active_tutorial (fixture->manager));

    lrg_tutorial_manager_stop_active (fixture->manager);

    g_assert_null (lrg_tutorial_manager_get_active_tutorial (fixture->manager));

    lrg_tutorial_step_free (step);
    g_object_unref (tutorial);
}

static void
test_tutorial_manager_completion (ManagerFixture *fixture,
                                  gconstpointer   user_data)
{
    g_assert_false (lrg_tutorial_manager_is_completed (fixture->manager, "any_tutorial"));

    lrg_tutorial_manager_mark_completed (fixture->manager, "completed_one");
    g_assert_true (lrg_tutorial_manager_is_completed (fixture->manager, "completed_one"));
    g_assert_false (lrg_tutorial_manager_is_completed (fixture->manager, "other_tutorial"));

    lrg_tutorial_manager_clear_completion (fixture->manager, "completed_one");
    g_assert_false (lrg_tutorial_manager_is_completed (fixture->manager, "completed_one"));
}

static void
test_tutorial_manager_clear_all_completions (ManagerFixture *fixture,
                                             gconstpointer   user_data)
{
    lrg_tutorial_manager_mark_completed (fixture->manager, "tut1");
    lrg_tutorial_manager_mark_completed (fixture->manager, "tut2");
    lrg_tutorial_manager_mark_completed (fixture->manager, "tut3");

    g_assert_true (lrg_tutorial_manager_is_completed (fixture->manager, "tut1"));
    g_assert_true (lrg_tutorial_manager_is_completed (fixture->manager, "tut2"));
    g_assert_true (lrg_tutorial_manager_is_completed (fixture->manager, "tut3"));

    lrg_tutorial_manager_clear_all_completions (fixture->manager);

    g_assert_false (lrg_tutorial_manager_is_completed (fixture->manager, "tut1"));
    g_assert_false (lrg_tutorial_manager_is_completed (fixture->manager, "tut2"));
    g_assert_false (lrg_tutorial_manager_is_completed (fixture->manager, "tut3"));
}

static void
test_tutorial_manager_advance_active (ManagerFixture *fixture,
                                      gconstpointer   user_data)
{
    LrgTutorial *tutorial;
    LrgTutorialStep *step;
    gboolean advanced;

    tutorial = lrg_tutorial_new ("advance_test", "Advance Test");
    step = lrg_tutorial_step_new_text ("Step", NULL);
    lrg_tutorial_add_step (tutorial, step);
    lrg_tutorial_add_step (tutorial, step);

    lrg_tutorial_manager_register (fixture->manager, tutorial);
    lrg_tutorial_manager_start_tutorial (fixture->manager, "advance_test");

    g_assert_cmpuint (lrg_tutorial_get_current_step_index (tutorial), ==, 0);

    advanced = lrg_tutorial_manager_advance_active (fixture->manager);
    g_assert_true (advanced);
    g_assert_cmpuint (lrg_tutorial_get_current_step_index (tutorial), ==, 1);

    lrg_tutorial_step_free (step);
    g_object_unref (tutorial);
}

/* ========================================================================== */
/*                          Highlight Widget Tests                            */
/* ========================================================================== */

typedef struct
{
    LrgHighlight *highlight;
} HighlightFixture;

static void
highlight_fixture_set_up (HighlightFixture *fixture,
                          gconstpointer     user_data)
{
    fixture->highlight = lrg_highlight_new ();
}

static void
highlight_fixture_tear_down (HighlightFixture *fixture,
                             gconstpointer     user_data)
{
    g_object_unref (fixture->highlight);
}

static void
test_highlight_new (void)
{
    LrgHighlight *highlight;

    highlight = lrg_highlight_new ();

    g_assert_nonnull (highlight);
    g_assert_cmpint (lrg_highlight_get_style (highlight), ==, LRG_HIGHLIGHT_STYLE_OUTLINE);
    g_assert_null (lrg_highlight_get_target (highlight));

    g_object_unref (highlight);
}

static void
test_highlight_style (HighlightFixture *fixture,
                      gconstpointer     user_data)
{
    lrg_highlight_set_style (fixture->highlight, LRG_HIGHLIGHT_STYLE_GLOW);
    g_assert_cmpint (lrg_highlight_get_style (fixture->highlight), ==, LRG_HIGHLIGHT_STYLE_GLOW);

    lrg_highlight_set_style (fixture->highlight, LRG_HIGHLIGHT_STYLE_SPOTLIGHT);
    g_assert_cmpint (lrg_highlight_get_style (fixture->highlight), ==, LRG_HIGHLIGHT_STYLE_SPOTLIGHT);

    lrg_highlight_set_style (fixture->highlight, LRG_HIGHLIGHT_STYLE_DARKEN_OTHERS);
    g_assert_cmpint (lrg_highlight_get_style (fixture->highlight), ==, LRG_HIGHLIGHT_STYLE_DARKEN_OTHERS);
}

static void
test_highlight_rect (HighlightFixture *fixture,
                     gconstpointer     user_data)
{
    (void)user_data;

    /* Test setting target rect - verify no crash */
    lrg_highlight_set_target_rect (fixture->highlight, 100.0f, 200.0f, 50.0f, 75.0f);

    /* Target should now be NULL since rect mode doesn't use widget target */
    g_assert_null (lrg_highlight_get_target (fixture->highlight));
}

static void
test_highlight_animation (HighlightFixture *fixture,
                          gconstpointer     user_data)
{
    g_assert_true (lrg_highlight_get_animated (fixture->highlight));

    lrg_highlight_set_animated (fixture->highlight, FALSE);
    g_assert_false (lrg_highlight_get_animated (fixture->highlight));

    lrg_highlight_set_pulse_speed (fixture->highlight, 3.0f);
    g_assert_cmpfloat_with_epsilon (lrg_highlight_get_pulse_speed (fixture->highlight), 3.0f, 0.001f);
}

static void
test_highlight_appearance (HighlightFixture *fixture,
                           gconstpointer     user_data)
{
    lrg_highlight_set_padding (fixture->highlight, 10.0f);
    g_assert_cmpfloat_with_epsilon (lrg_highlight_get_padding (fixture->highlight), 10.0f, 0.001f);

    lrg_highlight_set_outline_thickness (fixture->highlight, 3.0f);
    g_assert_cmpfloat_with_epsilon (lrg_highlight_get_outline_thickness (fixture->highlight), 3.0f, 0.001f);

    lrg_highlight_set_corner_radius (fixture->highlight, 5.0f);
    g_assert_cmpfloat_with_epsilon (lrg_highlight_get_corner_radius (fixture->highlight), 5.0f, 0.001f);
}

/* ========================================================================== */
/*                        Input Prompt Widget Tests                           */
/* ========================================================================== */

typedef struct
{
    LrgInputPrompt *prompt;
} InputPromptFixture;

static void
input_prompt_fixture_set_up (InputPromptFixture *fixture,
                             gconstpointer       user_data)
{
    fixture->prompt = lrg_input_prompt_new ();
}

static void
input_prompt_fixture_tear_down (InputPromptFixture *fixture,
                                gconstpointer       user_data)
{
    g_object_unref (fixture->prompt);
}

static void
test_input_prompt_new (void)
{
    LrgInputPrompt *prompt;

    prompt = lrg_input_prompt_new ();

    g_assert_nonnull (prompt);
    g_assert_cmpint (lrg_input_prompt_get_device_type (prompt), ==, LRG_INPUT_DEVICE_KEYBOARD);

    g_object_unref (prompt);
}

static void
test_input_prompt_with_action (void)
{
    LrgInputPrompt *prompt;

    prompt = lrg_input_prompt_new_with_action ("jump");

    g_assert_nonnull (prompt);
    g_assert_cmpstr (lrg_input_prompt_get_action_name (prompt), ==, "jump");

    g_object_unref (prompt);
}

static void
test_input_prompt_action (InputPromptFixture *fixture,
                          gconstpointer       user_data)
{
    lrg_input_prompt_set_action_name (fixture->prompt, "attack");
    g_assert_cmpstr (lrg_input_prompt_get_action_name (fixture->prompt), ==, "attack");

    lrg_input_prompt_set_action_name (fixture->prompt, "dodge");
    g_assert_cmpstr (lrg_input_prompt_get_action_name (fixture->prompt), ==, "dodge");
}

static void
test_input_prompt_device_type (InputPromptFixture *fixture,
                               gconstpointer       user_data)
{
    lrg_input_prompt_set_device_type (fixture->prompt, LRG_INPUT_DEVICE_GAMEPAD);
    g_assert_cmpint (lrg_input_prompt_get_device_type (fixture->prompt), ==, LRG_INPUT_DEVICE_GAMEPAD);

    lrg_input_prompt_set_device_type (fixture->prompt, LRG_INPUT_DEVICE_KEYBOARD);
    g_assert_cmpint (lrg_input_prompt_get_device_type (fixture->prompt), ==, LRG_INPUT_DEVICE_KEYBOARD);
}

static void
test_input_prompt_gamepad_style (InputPromptFixture *fixture,
                                 gconstpointer       user_data)
{
    g_assert_cmpint (lrg_input_prompt_get_gamepad_style (fixture->prompt), ==, LRG_GAMEPAD_STYLE_XBOX);

    lrg_input_prompt_set_gamepad_style (fixture->prompt, LRG_GAMEPAD_STYLE_PLAYSTATION);
    g_assert_cmpint (lrg_input_prompt_get_gamepad_style (fixture->prompt), ==, LRG_GAMEPAD_STYLE_PLAYSTATION);

    lrg_input_prompt_set_gamepad_style (fixture->prompt, LRG_GAMEPAD_STYLE_NINTENDO);
    g_assert_cmpint (lrg_input_prompt_get_gamepad_style (fixture->prompt), ==, LRG_GAMEPAD_STYLE_NINTENDO);
}

static void
test_input_prompt_text (InputPromptFixture *fixture,
                        gconstpointer       user_data)
{
    (void)user_data;

    lrg_input_prompt_set_prompt_text (fixture->prompt, "Press to continue");
    g_assert_cmpstr (lrg_input_prompt_get_prompt_text (fixture->prompt), ==, "Press to continue");
}

/* ========================================================================== */
/*                       Tooltip Arrow Widget Tests                           */
/* ========================================================================== */

typedef struct
{
    LrgTooltipArrow *arrow;
} TooltipArrowFixture;

static void
tooltip_arrow_fixture_set_up (TooltipArrowFixture *fixture,
                              gconstpointer        user_data)
{
    fixture->arrow = lrg_tooltip_arrow_new ();
}

static void
tooltip_arrow_fixture_tear_down (TooltipArrowFixture *fixture,
                                 gconstpointer        user_data)
{
    g_object_unref (fixture->arrow);
}

static void
test_tooltip_arrow_new (void)
{
    LrgTooltipArrow *arrow;

    arrow = lrg_tooltip_arrow_new ();

    g_assert_nonnull (arrow);
    g_assert_cmpint (lrg_tooltip_arrow_get_direction (arrow), ==, LRG_ARROW_DIRECTION_DOWN);

    g_object_unref (arrow);
}

static void
test_tooltip_arrow_with_direction (void)
{
    LrgTooltipArrow *arrow;

    arrow = lrg_tooltip_arrow_new_with_direction (LRG_ARROW_DIRECTION_LEFT);

    g_assert_nonnull (arrow);
    g_assert_cmpint (lrg_tooltip_arrow_get_direction (arrow), ==, LRG_ARROW_DIRECTION_LEFT);

    g_object_unref (arrow);
}

static void
test_tooltip_arrow_direction (TooltipArrowFixture *fixture,
                              gconstpointer        user_data)
{
    lrg_tooltip_arrow_set_direction (fixture->arrow, LRG_ARROW_DIRECTION_UP);
    g_assert_cmpint (lrg_tooltip_arrow_get_direction (fixture->arrow), ==, LRG_ARROW_DIRECTION_UP);

    lrg_tooltip_arrow_set_direction (fixture->arrow, LRG_ARROW_DIRECTION_RIGHT);
    g_assert_cmpint (lrg_tooltip_arrow_get_direction (fixture->arrow), ==, LRG_ARROW_DIRECTION_RIGHT);

    lrg_tooltip_arrow_set_direction (fixture->arrow, LRG_ARROW_DIRECTION_AUTO);
    g_assert_cmpint (lrg_tooltip_arrow_get_direction (fixture->arrow), ==, LRG_ARROW_DIRECTION_AUTO);
}

static void
test_tooltip_arrow_target_position (TooltipArrowFixture *fixture,
                                    gconstpointer        user_data)
{
    lrg_tooltip_arrow_set_target_position (fixture->arrow, 150.0f, 250.0f);

    /* Target position is internal - verify arrow doesn't crash when set */
    g_assert_nonnull (fixture->arrow);
}

static void
test_tooltip_arrow_appearance (TooltipArrowFixture *fixture,
                               gconstpointer        user_data)
{
    lrg_tooltip_arrow_set_size (fixture->arrow, 32.0f);
    g_assert_cmpfloat_with_epsilon (lrg_tooltip_arrow_get_size (fixture->arrow), 32.0f, 0.001f);

    lrg_tooltip_arrow_set_offset (fixture->arrow, 15.0f);
    g_assert_cmpfloat_with_epsilon (lrg_tooltip_arrow_get_offset (fixture->arrow), 15.0f, 0.001f);
}

static void
test_tooltip_arrow_animation (TooltipArrowFixture *fixture,
                              gconstpointer        user_data)
{
    g_assert_true (lrg_tooltip_arrow_get_animated (fixture->arrow));

    lrg_tooltip_arrow_set_animated (fixture->arrow, FALSE);
    g_assert_false (lrg_tooltip_arrow_get_animated (fixture->arrow));

    lrg_tooltip_arrow_set_bounce_amount (fixture->arrow, 10.0f);
    g_assert_cmpfloat_with_epsilon (lrg_tooltip_arrow_get_bounce_amount (fixture->arrow), 10.0f, 0.001f);

    lrg_tooltip_arrow_set_bounce_speed (fixture->arrow, 4.0f);
    g_assert_cmpfloat_with_epsilon (lrg_tooltip_arrow_get_bounce_speed (fixture->arrow), 4.0f, 0.001f);
}

/* ========================================================================== */
/*                       YAML Serialization Tests                             */
/* ========================================================================== */

static void
test_tutorial_yaml_roundtrip (void)
{
    g_autofree gchar *tmp_path = NULL;
    g_autoptr(GError) error = NULL;
    LrgTutorial *tutorial;
    LrgTutorial *loaded;
    LrgTutorialStep *step;
    LrgTutorialStep *loaded_step;
    gboolean saved;
    gint fd;

    /* Create a tutorial with various step types */
    tutorial = lrg_tutorial_new ("tut_intro", "Introduction Tutorial");
    lrg_tutorial_set_description (tutorial, "Learn the basics of the game");
    lrg_tutorial_set_repeatable (tutorial, TRUE);
    lrg_tutorial_set_skippable (tutorial, TRUE);

    /* Add a text step */
    step = lrg_tutorial_step_new_text ("Welcome to the game!", "Narrator");
    lrg_tutorial_step_set_id (step, "step_welcome");
    lrg_tutorial_step_set_can_skip (step, FALSE);
    lrg_tutorial_step_set_arrow_direction (step, LRG_ARROW_DIRECTION_DOWN);
    lrg_tutorial_add_step (tutorial, step);
    lrg_tutorial_step_free (step);

    /* Add a highlight step */
    step = lrg_tutorial_step_new_highlight ("inventory_button", LRG_HIGHLIGHT_STYLE_GLOW);
    lrg_tutorial_step_set_id (step, "step_highlight");
    lrg_tutorial_step_set_blocks_input (step, TRUE);
    lrg_tutorial_add_step (tutorial, step);
    lrg_tutorial_step_free (step);

    /* Add an input step */
    step = lrg_tutorial_step_new_input ("open_inventory", TRUE);
    lrg_tutorial_step_set_id (step, "step_input");
    lrg_tutorial_add_step (tutorial, step);
    lrg_tutorial_step_free (step);

    /* Add a delay step */
    step = lrg_tutorial_step_new_delay (2.5f);
    lrg_tutorial_step_set_id (step, "step_delay");
    lrg_tutorial_step_set_auto_advance (step, TRUE);
    lrg_tutorial_add_step (tutorial, step);
    lrg_tutorial_step_free (step);

    /* Add a condition step */
    step = lrg_tutorial_step_new_condition ("has_picked_up_item");
    lrg_tutorial_step_set_id (step, "step_condition");
    lrg_tutorial_add_step (tutorial, step);
    lrg_tutorial_step_free (step);

    /* Save to temp file */
    fd = g_file_open_tmp ("test_tutorial_XXXXXX.yaml", &tmp_path, &error);
    g_assert_no_error (error);
    close (fd);

    saved = lrg_tutorial_save_to_file (tutorial, tmp_path, &error);
    g_assert_no_error (error);
    g_assert_true (saved);

    /* Load back */
    loaded = lrg_tutorial_new_from_file (tmp_path, &error);
    g_assert_no_error (error);
    g_assert_nonnull (loaded);

    /* Verify data roundtrip */
    g_assert_cmpstr (lrg_tutorial_get_id (loaded), ==, "tut_intro");
    g_assert_cmpstr (lrg_tutorial_get_name (loaded), ==, "Introduction Tutorial");
    g_assert_cmpstr (lrg_tutorial_get_description (loaded), ==, "Learn the basics of the game");
    g_assert_true (lrg_tutorial_is_repeatable (loaded));
    g_assert_true (lrg_tutorial_is_skippable (loaded));
    g_assert_cmpuint (lrg_tutorial_get_step_count (loaded), ==, 5);

    /* Verify text step */
    loaded_step = lrg_tutorial_get_step (loaded, 0);
    g_assert_nonnull (loaded_step);
    g_assert_cmpint (lrg_tutorial_step_get_step_type (loaded_step), ==, LRG_TUTORIAL_STEP_TEXT);
    g_assert_cmpstr (lrg_tutorial_step_get_id (loaded_step), ==, "step_welcome");
    g_assert_cmpstr (lrg_tutorial_step_get_text (loaded_step), ==, "Welcome to the game!");
    g_assert_cmpstr (lrg_tutorial_step_get_speaker (loaded_step), ==, "Narrator");
    g_assert_false (lrg_tutorial_step_get_can_skip (loaded_step));
    g_assert_cmpint (lrg_tutorial_step_get_arrow_direction (loaded_step), ==, LRG_ARROW_DIRECTION_DOWN);

    /* Verify highlight step */
    loaded_step = lrg_tutorial_get_step (loaded, 1);
    g_assert_cmpint (lrg_tutorial_step_get_step_type (loaded_step), ==, LRG_TUTORIAL_STEP_HIGHLIGHT);
    g_assert_cmpstr (lrg_tutorial_step_get_target_id (loaded_step), ==, "inventory_button");
    g_assert_cmpint (lrg_tutorial_step_get_highlight_style (loaded_step), ==, LRG_HIGHLIGHT_STYLE_GLOW);
    g_assert_true (lrg_tutorial_step_get_blocks_input (loaded_step));

    /* Verify input step */
    loaded_step = lrg_tutorial_get_step (loaded, 2);
    g_assert_cmpint (lrg_tutorial_step_get_step_type (loaded_step), ==, LRG_TUTORIAL_STEP_INPUT);
    g_assert_cmpstr (lrg_tutorial_step_get_action_name (loaded_step), ==, "open_inventory");
    g_assert_true (lrg_tutorial_step_get_show_prompt (loaded_step));

    /* Verify delay step */
    loaded_step = lrg_tutorial_get_step (loaded, 3);
    g_assert_cmpint (lrg_tutorial_step_get_step_type (loaded_step), ==, LRG_TUTORIAL_STEP_DELAY);
    g_assert_cmpfloat_with_epsilon (lrg_tutorial_step_get_duration (loaded_step), 2.5f, 0.01f);
    g_assert_true (lrg_tutorial_step_get_auto_advance (loaded_step));

    /* Verify condition step */
    loaded_step = lrg_tutorial_get_step (loaded, 4);
    g_assert_cmpint (lrg_tutorial_step_get_step_type (loaded_step), ==, LRG_TUTORIAL_STEP_CONDITION);
    g_assert_cmpstr (lrg_tutorial_step_get_condition_id (loaded_step), ==, "has_picked_up_item");

    g_object_unref (loaded);
    g_object_unref (tutorial);
    g_unlink (tmp_path);
}

/* ========================================================================== */
/*                              Main Entry                                    */
/* ========================================================================== */

int
main (int argc, char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Tutorial Step tests */
    g_test_add_func ("/tutorial/step/new", test_tutorial_step_new);
    g_test_add_func ("/tutorial/step/new_text", test_tutorial_step_new_text);
    g_test_add_func ("/tutorial/step/new_highlight", test_tutorial_step_new_highlight);
    g_test_add_func ("/tutorial/step/new_input", test_tutorial_step_new_input);
    g_test_add_func ("/tutorial/step/new_condition", test_tutorial_step_new_condition);
    g_test_add_func ("/tutorial/step/new_delay", test_tutorial_step_new_delay);
    g_test_add_func ("/tutorial/step/copy", test_tutorial_step_copy);
    g_test_add_func ("/tutorial/step/id", test_tutorial_step_id);
    g_test_add_func ("/tutorial/step/properties", test_tutorial_step_properties);

    /* Tutorial tests */
    g_test_add ("/tutorial/tutorial/new", TutorialFixture, NULL,
                tutorial_fixture_set_up, test_tutorial_new, tutorial_fixture_tear_down);
    g_test_add ("/tutorial/tutorial/description", TutorialFixture, NULL,
                tutorial_fixture_set_up, test_tutorial_description, tutorial_fixture_tear_down);
    g_test_add ("/tutorial/tutorial/repeatable", TutorialFixture, NULL,
                tutorial_fixture_set_up, test_tutorial_repeatable, tutorial_fixture_tear_down);
    g_test_add ("/tutorial/tutorial/skippable", TutorialFixture, NULL,
                tutorial_fixture_set_up, test_tutorial_skippable, tutorial_fixture_tear_down);
    g_test_add ("/tutorial/tutorial/add_step", TutorialFixture, NULL,
                tutorial_fixture_set_up, test_tutorial_add_step, tutorial_fixture_tear_down);
    g_test_add ("/tutorial/tutorial/get_step", TutorialFixture, NULL,
                tutorial_fixture_set_up, test_tutorial_get_step, tutorial_fixture_tear_down);
    g_test_add ("/tutorial/tutorial/remove_step", TutorialFixture, NULL,
                tutorial_fixture_set_up, test_tutorial_remove_step, tutorial_fixture_tear_down);
    g_test_add ("/tutorial/tutorial/clear_steps", TutorialFixture, NULL,
                tutorial_fixture_set_up, test_tutorial_clear_steps, tutorial_fixture_tear_down);
    g_test_add ("/tutorial/tutorial/start", TutorialFixture, NULL,
                tutorial_fixture_set_up, test_tutorial_start, tutorial_fixture_tear_down);
    g_test_add ("/tutorial/tutorial/advance", TutorialFixture, NULL,
                tutorial_fixture_set_up, test_tutorial_advance, tutorial_fixture_tear_down);
    g_test_add ("/tutorial/tutorial/pause_resume", TutorialFixture, NULL,
                tutorial_fixture_set_up, test_tutorial_pause_resume, tutorial_fixture_tear_down);
    g_test_add ("/tutorial/tutorial/skip", TutorialFixture, NULL,
                tutorial_fixture_set_up, test_tutorial_skip, tutorial_fixture_tear_down);
    g_test_add ("/tutorial/tutorial/progress", TutorialFixture, NULL,
                tutorial_fixture_set_up, test_tutorial_progress, tutorial_fixture_tear_down);
    g_test_add ("/tutorial/tutorial/go_to_step", TutorialFixture, NULL,
                tutorial_fixture_set_up, test_tutorial_go_to_step, tutorial_fixture_tear_down);
    g_test_add ("/tutorial/tutorial/reset", TutorialFixture, NULL,
                tutorial_fixture_set_up, test_tutorial_reset, tutorial_fixture_tear_down);

    /* YAML Serialization tests */
    g_test_add_func ("/tutorial/tutorial/yaml_roundtrip", test_tutorial_yaml_roundtrip);

    /* Tutorial Manager tests */
    g_test_add_func ("/tutorial/manager/new", test_tutorial_manager_new);
    g_test_add ("/tutorial/manager/register", ManagerFixture, NULL,
                manager_fixture_set_up, test_tutorial_manager_register, manager_fixture_tear_down);
    g_test_add ("/tutorial/manager/get_tutorial", ManagerFixture, NULL,
                manager_fixture_set_up, test_tutorial_manager_get_tutorial, manager_fixture_tear_down);
    g_test_add ("/tutorial/manager/unregister", ManagerFixture, NULL,
                manager_fixture_set_up, test_tutorial_manager_unregister, manager_fixture_tear_down);
    g_test_add ("/tutorial/manager/get_tutorials", ManagerFixture, NULL,
                manager_fixture_set_up, test_tutorial_manager_get_tutorials, manager_fixture_tear_down);
    g_test_add ("/tutorial/manager/start_tutorial", ManagerFixture, NULL,
                manager_fixture_set_up, test_tutorial_manager_start_tutorial, manager_fixture_tear_down);
    g_test_add ("/tutorial/manager/stop_active", ManagerFixture, NULL,
                manager_fixture_set_up, test_tutorial_manager_stop_active, manager_fixture_tear_down);
    g_test_add ("/tutorial/manager/completion", ManagerFixture, NULL,
                manager_fixture_set_up, test_tutorial_manager_completion, manager_fixture_tear_down);
    g_test_add ("/tutorial/manager/clear_all_completions", ManagerFixture, NULL,
                manager_fixture_set_up, test_tutorial_manager_clear_all_completions, manager_fixture_tear_down);
    g_test_add ("/tutorial/manager/advance_active", ManagerFixture, NULL,
                manager_fixture_set_up, test_tutorial_manager_advance_active, manager_fixture_tear_down);

    /* Highlight tests */
    g_test_add_func ("/tutorial/highlight/new", test_highlight_new);
    g_test_add ("/tutorial/highlight/style", HighlightFixture, NULL,
                highlight_fixture_set_up, test_highlight_style, highlight_fixture_tear_down);
    g_test_add ("/tutorial/highlight/rect", HighlightFixture, NULL,
                highlight_fixture_set_up, test_highlight_rect, highlight_fixture_tear_down);
    g_test_add ("/tutorial/highlight/animation", HighlightFixture, NULL,
                highlight_fixture_set_up, test_highlight_animation, highlight_fixture_tear_down);
    g_test_add ("/tutorial/highlight/appearance", HighlightFixture, NULL,
                highlight_fixture_set_up, test_highlight_appearance, highlight_fixture_tear_down);

    /* Input Prompt tests */
    g_test_add_func ("/tutorial/input_prompt/new", test_input_prompt_new);
    g_test_add_func ("/tutorial/input_prompt/with_action", test_input_prompt_with_action);
    g_test_add ("/tutorial/input_prompt/action", InputPromptFixture, NULL,
                input_prompt_fixture_set_up, test_input_prompt_action, input_prompt_fixture_tear_down);
    g_test_add ("/tutorial/input_prompt/device_type", InputPromptFixture, NULL,
                input_prompt_fixture_set_up, test_input_prompt_device_type, input_prompt_fixture_tear_down);
    g_test_add ("/tutorial/input_prompt/gamepad_style", InputPromptFixture, NULL,
                input_prompt_fixture_set_up, test_input_prompt_gamepad_style, input_prompt_fixture_tear_down);
    g_test_add ("/tutorial/input_prompt/text", InputPromptFixture, NULL,
                input_prompt_fixture_set_up, test_input_prompt_text, input_prompt_fixture_tear_down);

    /* Tooltip Arrow tests */
    g_test_add_func ("/tutorial/tooltip_arrow/new", test_tooltip_arrow_new);
    g_test_add_func ("/tutorial/tooltip_arrow/with_direction", test_tooltip_arrow_with_direction);
    g_test_add ("/tutorial/tooltip_arrow/direction", TooltipArrowFixture, NULL,
                tooltip_arrow_fixture_set_up, test_tooltip_arrow_direction, tooltip_arrow_fixture_tear_down);
    g_test_add ("/tutorial/tooltip_arrow/target_position", TooltipArrowFixture, NULL,
                tooltip_arrow_fixture_set_up, test_tooltip_arrow_target_position, tooltip_arrow_fixture_tear_down);
    g_test_add ("/tutorial/tooltip_arrow/appearance", TooltipArrowFixture, NULL,
                tooltip_arrow_fixture_set_up, test_tooltip_arrow_appearance, tooltip_arrow_fixture_tear_down);
    g_test_add ("/tutorial/tooltip_arrow/animation", TooltipArrowFixture, NULL,
                tooltip_arrow_fixture_set_up, test_tooltip_arrow_animation, tooltip_arrow_fixture_tear_down);

    return g_test_run ();
}
