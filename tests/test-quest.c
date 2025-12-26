/* test-quest.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the quest system.
 */

#include <glib.h>
#include <glib-object.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "lrg-enums.h"
#include "quest/lrg-quest-objective.h"
#include "quest/lrg-quest-def.h"
#include "quest/lrg-quest-instance.h"
#include "quest/lrg-quest-log.h"

/* ========================================================================== */
/*                           LrgQuestObjective Tests                          */
/* ========================================================================== */

static void
test_objective_new (void)
{
    LrgQuestObjective *obj;

    obj = lrg_quest_objective_new ("obj1", "Kill 5 goblins", LRG_QUEST_OBJECTIVE_KILL);
    g_assert_nonnull (obj);
    g_assert_cmpstr (lrg_quest_objective_get_id (obj), ==, "obj1");
    g_assert_cmpstr (lrg_quest_objective_get_description (obj), ==, "Kill 5 goblins");
    g_assert_cmpint (lrg_quest_objective_get_objective_type (obj), ==, LRG_QUEST_OBJECTIVE_KILL);

    lrg_quest_objective_free (obj);
}

static void
test_objective_copy (void)
{
    LrgQuestObjective *orig;
    LrgQuestObjective *copy;

    orig = lrg_quest_objective_new ("obj1", "Test objective", LRG_QUEST_OBJECTIVE_KILL);
    lrg_quest_objective_set_target_id (orig, "goblin");
    lrg_quest_objective_set_target_count (orig, 10);
    lrg_quest_objective_set_current_count (orig, 5);

    copy = lrg_quest_objective_copy (orig);
    g_assert_nonnull (copy);
    g_assert_cmpstr (lrg_quest_objective_get_id (copy), ==, "obj1");
    g_assert_cmpstr (lrg_quest_objective_get_target_id (copy), ==, "goblin");
    g_assert_cmpuint (lrg_quest_objective_get_target_count (copy), ==, 10);
    g_assert_cmpuint (lrg_quest_objective_get_current_count (copy), ==, 5);

    lrg_quest_objective_free (orig);
    lrg_quest_objective_free (copy);
}

static void
test_objective_progress (void)
{
    LrgQuestObjective *obj;

    obj = lrg_quest_objective_new ("obj1", "Collect 10 items", LRG_QUEST_OBJECTIVE_COLLECT);
    lrg_quest_objective_set_target_count (obj, 10);

    g_assert_cmpuint (lrg_quest_objective_get_current_count (obj), ==, 0);
    g_assert_false (lrg_quest_objective_is_complete (obj));
    g_assert_cmpfloat_with_epsilon (lrg_quest_objective_get_progress (obj), 0.0, 0.01);

    lrg_quest_objective_increment (obj, 5);
    g_assert_cmpuint (lrg_quest_objective_get_current_count (obj), ==, 5);
    g_assert_false (lrg_quest_objective_is_complete (obj));
    g_assert_cmpfloat_with_epsilon (lrg_quest_objective_get_progress (obj), 0.5, 0.01);

    lrg_quest_objective_increment (obj, 5);
    g_assert_cmpuint (lrg_quest_objective_get_current_count (obj), ==, 10);
    g_assert_true (lrg_quest_objective_is_complete (obj));
    g_assert_cmpfloat_with_epsilon (lrg_quest_objective_get_progress (obj), 1.0, 0.01);

    lrg_quest_objective_free (obj);
}

static void
test_objective_increment_overflow (void)
{
    LrgQuestObjective *obj;

    /* Test that incrementing beyond target marks complete but doesn't clamp */
    obj = lrg_quest_objective_new ("obj1", "Collect 5 items", LRG_QUEST_OBJECTIVE_COLLECT);
    lrg_quest_objective_set_target_count (obj, 5);

    lrg_quest_objective_increment (obj, 10);
    /* Count is not clamped - can exceed target */
    g_assert_cmpuint (lrg_quest_objective_get_current_count (obj), ==, 10);
    /* But objective is marked complete */
    g_assert_true (lrg_quest_objective_is_complete (obj));

    lrg_quest_objective_free (obj);
}

static void
test_objective_location (void)
{
    LrgQuestObjective *obj;

    obj = lrg_quest_objective_new ("obj1", "Reach the town", LRG_QUEST_OBJECTIVE_REACH);
    lrg_quest_objective_set_location (obj, "town_square");

    g_assert_cmpstr (lrg_quest_objective_get_location (obj), ==, "town_square");

    lrg_quest_objective_free (obj);
}

/* ========================================================================== */
/*                             LrgQuestDef Tests                              */
/* ========================================================================== */

static void
test_quest_def_new (void)
{
    g_autoptr(LrgQuestDef) def = NULL;

    def = lrg_quest_def_new ("quest1");
    g_assert_nonnull (def);
    g_assert_cmpstr (lrg_quest_def_get_id (def), ==, "quest1");
    g_assert_null (lrg_quest_def_get_name (def));
    g_assert_null (lrg_quest_def_get_description (def));
}

static void
test_quest_def_properties (void)
{
    g_autoptr(LrgQuestDef) def = NULL;

    def = lrg_quest_def_new ("quest1");
    lrg_quest_def_set_name (def, "The Lost Artifact");
    lrg_quest_def_set_description (def, "Find the ancient artifact.");
    lrg_quest_def_set_giver_npc (def, "npc_wizard");

    g_assert_cmpstr (lrg_quest_def_get_name (def), ==, "The Lost Artifact");
    g_assert_cmpstr (lrg_quest_def_get_description (def), ==, "Find the ancient artifact.");
    g_assert_cmpstr (lrg_quest_def_get_giver_npc (def), ==, "npc_wizard");
}

static void
test_quest_def_stages (void)
{
    g_autoptr(LrgQuestDef) def = NULL;
    LrgQuestObjective     *obj1;
    LrgQuestObjective     *obj2;
    GPtrArray             *stages;

    def = lrg_quest_def_new ("quest1");

    obj1 = lrg_quest_objective_new ("stage1", "Talk to the wizard", LRG_QUEST_OBJECTIVE_INTERACT);
    lrg_quest_def_add_stage (def, obj1);

    obj2 = lrg_quest_objective_new ("stage2", "Kill the dragon", LRG_QUEST_OBJECTIVE_KILL);
    lrg_quest_objective_set_target_count (obj2, 1);
    lrg_quest_def_add_stage (def, obj2);

    g_assert_cmpuint (lrg_quest_def_get_stage_count (def), ==, 2);

    stages = lrg_quest_def_get_stages (def);
    g_assert_nonnull (stages);
    g_assert_cmpuint (stages->len, ==, 2);

    g_assert_true (lrg_quest_def_get_stage (def, 0) == obj1);
    g_assert_true (lrg_quest_def_get_stage (def, 1) == obj2);
    g_assert_null (lrg_quest_def_get_stage (def, 2));
}

static void
test_quest_def_prerequisites (void)
{
    g_autoptr(LrgQuestDef) def = NULL;
    GPtrArray             *prereqs;

    def = lrg_quest_def_new ("quest2");
    lrg_quest_def_add_prerequisite (def, "quest1");
    lrg_quest_def_add_prerequisite (def, "quest_intro");

    prereqs = lrg_quest_def_get_prerequisites (def);
    g_assert_nonnull (prereqs);
    g_assert_cmpuint (prereqs->len, ==, 2);
}

static void
test_quest_def_rewards (void)
{
    g_autoptr(LrgQuestDef) def = NULL;
    GHashTable            *items;

    def = lrg_quest_def_new ("quest1");

    lrg_quest_def_set_reward_gold (def, 100);
    lrg_quest_def_set_reward_xp (def, 500);
    lrg_quest_def_add_reward_item (def, "sword_legendary", 1);
    lrg_quest_def_add_reward_item (def, "potion_health", 5);

    g_assert_cmpint (lrg_quest_def_get_reward_gold (def), ==, 100);
    g_assert_cmpint (lrg_quest_def_get_reward_xp (def), ==, 500);

    items = lrg_quest_def_get_reward_items (def);
    g_assert_nonnull (items);
    g_assert_cmpuint (g_hash_table_size (items), ==, 2);
}

static void
test_quest_def_check_prerequisites_empty (void)
{
    g_autoptr(LrgQuestDef) def = NULL;

    def = lrg_quest_def_new ("quest1");
    g_assert_true (lrg_quest_def_check_prerequisites (def, NULL));
}

/* ========================================================================== */
/*                           LrgQuestInstance Tests                           */
/* ========================================================================== */

typedef struct
{
    LrgQuestDef *def;
} QuestInstanceFixture;

static void
instance_fixture_set_up (QuestInstanceFixture *fixture,
                         gconstpointer         user_data)
{
    LrgQuestObjective *obj1;
    LrgQuestObjective *obj2;

    fixture->def = lrg_quest_def_new ("test_quest");
    lrg_quest_def_set_name (fixture->def, "Test Quest");

    obj1 = lrg_quest_objective_new ("stage1", "Kill 5 goblins", LRG_QUEST_OBJECTIVE_KILL);
    lrg_quest_objective_set_target_id (obj1, "goblin");
    lrg_quest_objective_set_target_count (obj1, 5);
    lrg_quest_def_add_stage (fixture->def, obj1);

    obj2 = lrg_quest_objective_new ("stage2", "Return to NPC", LRG_QUEST_OBJECTIVE_INTERACT);
    lrg_quest_objective_set_target_id (obj2, "npc_quest_giver");
    lrg_quest_objective_set_target_count (obj2, 1);
    lrg_quest_def_add_stage (fixture->def, obj2);

    (void)user_data;
}

static void
instance_fixture_tear_down (QuestInstanceFixture *fixture,
                            gconstpointer         user_data)
{
    g_clear_object (&fixture->def);
    (void)user_data;
}

static void
test_instance_new (QuestInstanceFixture *fixture,
                   gconstpointer         user_data)
{
    g_autoptr(LrgQuestInstance) instance = NULL;

    instance = lrg_quest_instance_new (fixture->def);
    g_assert_nonnull (instance);
    g_assert_true (lrg_quest_instance_get_quest_def (instance) == fixture->def);
    g_assert_cmpint (lrg_quest_instance_get_state (instance), ==, LRG_QUEST_STATE_AVAILABLE);
    g_assert_cmpuint (lrg_quest_instance_get_current_stage (instance), ==, 0);

    (void)user_data;
}

static void
test_instance_state_changes (QuestInstanceFixture *fixture,
                             gconstpointer         user_data)
{
    g_autoptr(LrgQuestInstance) instance = NULL;

    instance = lrg_quest_instance_new (fixture->def);

    lrg_quest_instance_set_state (instance, LRG_QUEST_STATE_ACTIVE);
    g_assert_cmpint (lrg_quest_instance_get_state (instance), ==, LRG_QUEST_STATE_ACTIVE);

    lrg_quest_instance_fail (instance);
    g_assert_cmpint (lrg_quest_instance_get_state (instance), ==, LRG_QUEST_STATE_FAILED);

    (void)user_data;
}

static void
test_instance_update_progress (QuestInstanceFixture *fixture,
                               gconstpointer         user_data)
{
    g_autoptr(LrgQuestInstance) instance = NULL;
    LrgQuestObjective          *obj;
    gboolean                    updated;

    instance = lrg_quest_instance_new (fixture->def);
    lrg_quest_instance_set_state (instance, LRG_QUEST_STATE_ACTIVE);

    /* Wrong objective type should not update */
    updated = lrg_quest_instance_update_progress (instance,
                                                   LRG_QUEST_OBJECTIVE_COLLECT,
                                                   "goblin", 1);
    g_assert_false (updated);

    /* Wrong target should not update */
    updated = lrg_quest_instance_update_progress (instance,
                                                   LRG_QUEST_OBJECTIVE_KILL,
                                                   "dragon", 1);
    g_assert_false (updated);

    /* Correct update */
    updated = lrg_quest_instance_update_progress (instance,
                                                   LRG_QUEST_OBJECTIVE_KILL,
                                                   "goblin", 3);
    g_assert_true (updated);

    obj = lrg_quest_instance_get_current_objective (instance);
    g_assert_cmpuint (lrg_quest_objective_get_current_count (obj), ==, 3);

    (void)user_data;
}

static void
test_instance_auto_advance (QuestInstanceFixture *fixture,
                            gconstpointer         user_data)
{
    g_autoptr(LrgQuestInstance) instance = NULL;

    instance = lrg_quest_instance_new (fixture->def);
    lrg_quest_instance_set_state (instance, LRG_QUEST_STATE_ACTIVE);

    g_assert_cmpuint (lrg_quest_instance_get_current_stage (instance), ==, 0);

    /* Complete first stage */
    lrg_quest_instance_update_progress (instance,
                                         LRG_QUEST_OBJECTIVE_KILL,
                                         "goblin", 5);

    /* Should auto-advance to stage 1 */
    g_assert_cmpuint (lrg_quest_instance_get_current_stage (instance), ==, 1);

    (void)user_data;
}

static void
test_instance_complete (QuestInstanceFixture *fixture,
                        gconstpointer         user_data)
{
    g_autoptr(LrgQuestInstance) instance = NULL;

    instance = lrg_quest_instance_new (fixture->def);
    lrg_quest_instance_set_state (instance, LRG_QUEST_STATE_ACTIVE);

    /* Complete stage 1 */
    lrg_quest_instance_update_progress (instance,
                                         LRG_QUEST_OBJECTIVE_KILL,
                                         "goblin", 5);

    /* Complete stage 2 */
    lrg_quest_instance_update_progress (instance,
                                         LRG_QUEST_OBJECTIVE_INTERACT,
                                         "npc_quest_giver", 1);

    g_assert_true (lrg_quest_instance_is_complete (instance));
    g_assert_cmpint (lrg_quest_instance_get_state (instance), ==, LRG_QUEST_STATE_COMPLETE);

    (void)user_data;
}

static void
test_instance_progress_calculation (QuestInstanceFixture *fixture,
                                    gconstpointer         user_data)
{
    g_autoptr(LrgQuestInstance) instance = NULL;
    gdouble                     progress;

    instance = lrg_quest_instance_new (fixture->def);
    lrg_quest_instance_set_state (instance, LRG_QUEST_STATE_ACTIVE);

    /* 0/5 goblins killed, stage 0 of 2 */
    progress = lrg_quest_instance_get_progress (instance);
    g_assert_cmpfloat_with_epsilon (progress, 0.0, 0.01);

    /* 2/5 goblins = 0.4 progress on stage 0, so (0 + 0.4) / 2 = 0.2 */
    lrg_quest_instance_update_progress (instance,
                                         LRG_QUEST_OBJECTIVE_KILL,
                                         "goblin", 2);
    progress = lrg_quest_instance_get_progress (instance);
    g_assert_cmpfloat_with_epsilon (progress, 0.2, 0.01);

    /* Complete stage 1, now at stage 1 with 0 progress = 1/2 = 0.5 */
    lrg_quest_instance_update_progress (instance,
                                         LRG_QUEST_OBJECTIVE_KILL,
                                         "goblin", 3);
    progress = lrg_quest_instance_get_progress (instance);
    g_assert_cmpfloat_with_epsilon (progress, 0.5, 0.01);

    (void)user_data;
}

static void
test_instance_not_active_no_progress (QuestInstanceFixture *fixture,
                                      gconstpointer         user_data)
{
    g_autoptr(LrgQuestInstance) instance = NULL;
    gboolean                    updated;

    instance = lrg_quest_instance_new (fixture->def);
    /* Quest is AVAILABLE, not ACTIVE */

    updated = lrg_quest_instance_update_progress (instance,
                                                   LRG_QUEST_OBJECTIVE_KILL,
                                                   "goblin", 1);
    g_assert_false (updated);

    (void)user_data;
}

/* ========================================================================== */
/*                             LrgQuestLog Tests                              */
/* ========================================================================== */

typedef struct
{
    LrgQuestLog *log;
    LrgQuestDef *def1;
    LrgQuestDef *def2;
} QuestLogFixture;

static void
log_fixture_set_up (QuestLogFixture *fixture,
                    gconstpointer    user_data)
{
    LrgQuestObjective *obj;

    fixture->log = lrg_quest_log_new ();

    fixture->def1 = lrg_quest_def_new ("quest1");
    lrg_quest_def_set_name (fixture->def1, "First Quest");
    obj = lrg_quest_objective_new ("obj1", "Do something", LRG_QUEST_OBJECTIVE_KILL);
    lrg_quest_objective_set_target_count (obj, 1);
    lrg_quest_def_add_stage (fixture->def1, obj);

    fixture->def2 = lrg_quest_def_new ("quest2");
    lrg_quest_def_set_name (fixture->def2, "Second Quest");
    obj = lrg_quest_objective_new ("obj2", "Do something else", LRG_QUEST_OBJECTIVE_KILL);
    lrg_quest_objective_set_target_count (obj, 1);
    lrg_quest_def_add_stage (fixture->def2, obj);

    (void)user_data;
}

static void
log_fixture_tear_down (QuestLogFixture *fixture,
                       gconstpointer    user_data)
{
    g_clear_object (&fixture->log);
    g_clear_object (&fixture->def1);
    g_clear_object (&fixture->def2);
    (void)user_data;
}

static void
test_log_new (void)
{
    g_autoptr(LrgQuestLog) log = NULL;

    log = lrg_quest_log_new ();
    g_assert_nonnull (log);
    g_assert_cmpuint (lrg_quest_log_get_active_count (log), ==, 0);
    g_assert_cmpuint (lrg_quest_log_get_completed_count (log), ==, 0);
    g_assert_null (lrg_quest_log_get_tracked_quest (log));
}

static void
test_log_start_quest (QuestLogFixture *fixture,
                      gconstpointer    user_data)
{
    LrgQuestInstance *instance;

    instance = lrg_quest_log_start_quest (fixture->log, fixture->def1);
    g_assert_nonnull (instance);
    g_assert_cmpuint (lrg_quest_log_get_active_count (fixture->log), ==, 1);
    g_assert_true (lrg_quest_log_is_quest_active (fixture->log, "quest1"));

    (void)user_data;
}

static void
test_log_start_duplicate (QuestLogFixture *fixture,
                          gconstpointer    user_data)
{
    LrgQuestInstance *instance1;
    LrgQuestInstance *instance2;

    instance1 = lrg_quest_log_start_quest (fixture->log, fixture->def1);
    g_assert_nonnull (instance1);

    instance2 = lrg_quest_log_start_quest (fixture->log, fixture->def1);
    g_assert_null (instance2);

    g_assert_cmpuint (lrg_quest_log_get_active_count (fixture->log), ==, 1);

    (void)user_data;
}

static void
test_log_get_quest (QuestLogFixture *fixture,
                    gconstpointer    user_data)
{
    LrgQuestInstance *started;
    LrgQuestInstance *found;

    started = lrg_quest_log_start_quest (fixture->log, fixture->def1);
    found = lrg_quest_log_get_quest (fixture->log, "quest1");
    g_assert_true (found == started);

    g_assert_null (lrg_quest_log_get_quest (fixture->log, "nonexistent"));

    (void)user_data;
}

static void
test_log_get_active_quests (QuestLogFixture *fixture,
                            gconstpointer    user_data)
{
    GList *active;

    lrg_quest_log_start_quest (fixture->log, fixture->def1);
    lrg_quest_log_start_quest (fixture->log, fixture->def2);

    active = lrg_quest_log_get_active_quests (fixture->log);
    g_assert_cmpuint (g_list_length (active), ==, 2);
    g_list_free (active);

    (void)user_data;
}

static void
test_log_complete_quest (QuestLogFixture *fixture,
                         gconstpointer    user_data)
{
    LrgQuestInstance *instance;

    instance = lrg_quest_log_start_quest (fixture->log, fixture->def1);

    /* Complete the quest by completing its objective */
    lrg_quest_instance_update_progress (instance,
                                         LRG_QUEST_OBJECTIVE_KILL,
                                         NULL, 1);

    g_assert_true (lrg_quest_log_is_quest_completed (fixture->log, "quest1"));
    g_assert_false (lrg_quest_log_is_quest_active (fixture->log, "quest1"));
    g_assert_cmpuint (lrg_quest_log_get_active_count (fixture->log), ==, 0);
    g_assert_cmpuint (lrg_quest_log_get_completed_count (fixture->log), ==, 1);

    (void)user_data;
}

static void
test_log_abandon_quest (QuestLogFixture *fixture,
                        gconstpointer    user_data)
{
    gboolean abandoned;

    lrg_quest_log_start_quest (fixture->log, fixture->def1);
    g_assert_cmpuint (lrg_quest_log_get_active_count (fixture->log), ==, 1);

    abandoned = lrg_quest_log_abandon_quest (fixture->log, "quest1");
    g_assert_true (abandoned);
    g_assert_cmpuint (lrg_quest_log_get_active_count (fixture->log), ==, 0);
    g_assert_false (lrg_quest_log_is_quest_active (fixture->log, "quest1"));

    /* Cannot abandon non-existent quest */
    abandoned = lrg_quest_log_abandon_quest (fixture->log, "quest1");
    g_assert_false (abandoned);

    (void)user_data;
}

static void
test_log_track_quest (QuestLogFixture *fixture,
                      gconstpointer    user_data)
{
    LrgQuestInstance *instance;
    gboolean          tracked;

    instance = lrg_quest_log_start_quest (fixture->log, fixture->def1);

    tracked = lrg_quest_log_track_quest (fixture->log, "quest1");
    g_assert_true (tracked);
    g_assert_true (lrg_quest_log_get_tracked_quest (fixture->log) == instance);

    /* Cannot track non-existent quest */
    tracked = lrg_quest_log_track_quest (fixture->log, "nonexistent");
    g_assert_false (tracked);

    (void)user_data;
}

static void
test_log_tracked_cleared_on_complete (QuestLogFixture *fixture,
                                      gconstpointer    user_data)
{
    LrgQuestInstance *instance;

    instance = lrg_quest_log_start_quest (fixture->log, fixture->def1);
    lrg_quest_log_track_quest (fixture->log, "quest1");
    g_assert_nonnull (lrg_quest_log_get_tracked_quest (fixture->log));

    /* Complete the quest */
    lrg_quest_instance_update_progress (instance,
                                         LRG_QUEST_OBJECTIVE_KILL,
                                         NULL, 1);

    g_assert_null (lrg_quest_log_get_tracked_quest (fixture->log));

    (void)user_data;
}

static void
test_log_tracked_cleared_on_abandon (QuestLogFixture *fixture,
                                     gconstpointer    user_data)
{
    lrg_quest_log_start_quest (fixture->log, fixture->def1);
    lrg_quest_log_track_quest (fixture->log, "quest1");
    g_assert_nonnull (lrg_quest_log_get_tracked_quest (fixture->log));

    lrg_quest_log_abandon_quest (fixture->log, "quest1");
    g_assert_null (lrg_quest_log_get_tracked_quest (fixture->log));

    (void)user_data;
}

static gboolean quest_started_called = FALSE;
static gboolean quest_completed_called = FALSE;

static void
on_quest_started (LrgQuestLog      *log,
                  LrgQuestInstance *quest,
                  gpointer          user_data)
{
    quest_started_called = TRUE;
    (void)log;
    (void)quest;
    (void)user_data;
}

static void
on_quest_completed (LrgQuestLog      *log,
                    LrgQuestInstance *quest,
                    gpointer          user_data)
{
    quest_completed_called = TRUE;
    (void)log;
    (void)quest;
    (void)user_data;
}

static void
test_log_signals (QuestLogFixture *fixture,
                  gconstpointer    user_data)
{
    LrgQuestInstance *instance;

    quest_started_called = FALSE;
    quest_completed_called = FALSE;

    g_signal_connect (fixture->log, "quest-started",
                      G_CALLBACK (on_quest_started), NULL);
    g_signal_connect (fixture->log, "quest-completed",
                      G_CALLBACK (on_quest_completed), NULL);

    instance = lrg_quest_log_start_quest (fixture->log, fixture->def1);
    g_assert_true (quest_started_called);
    g_assert_false (quest_completed_called);

    lrg_quest_instance_update_progress (instance,
                                         LRG_QUEST_OBJECTIVE_KILL,
                                         NULL, 1);
    g_assert_true (quest_completed_called);

    (void)user_data;
}

/* ========================================================================== */
/*                                    Main                                    */
/* ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* LrgQuestObjective tests */
    g_test_add_func ("/quest/objective/new", test_objective_new);
    g_test_add_func ("/quest/objective/copy", test_objective_copy);
    g_test_add_func ("/quest/objective/progress", test_objective_progress);
    g_test_add_func ("/quest/objective/increment-overflow", test_objective_increment_overflow);
    g_test_add_func ("/quest/objective/location", test_objective_location);

    /* LrgQuestDef tests */
    g_test_add_func ("/quest/def/new", test_quest_def_new);
    g_test_add_func ("/quest/def/properties", test_quest_def_properties);
    g_test_add_func ("/quest/def/stages", test_quest_def_stages);
    g_test_add_func ("/quest/def/prerequisites", test_quest_def_prerequisites);
    g_test_add_func ("/quest/def/rewards", test_quest_def_rewards);
    g_test_add_func ("/quest/def/check-prerequisites-empty", test_quest_def_check_prerequisites_empty);

    /* LrgQuestInstance tests */
    g_test_add ("/quest/instance/new", QuestInstanceFixture, NULL,
                instance_fixture_set_up, test_instance_new, instance_fixture_tear_down);
    g_test_add ("/quest/instance/state-changes", QuestInstanceFixture, NULL,
                instance_fixture_set_up, test_instance_state_changes, instance_fixture_tear_down);
    g_test_add ("/quest/instance/update-progress", QuestInstanceFixture, NULL,
                instance_fixture_set_up, test_instance_update_progress, instance_fixture_tear_down);
    g_test_add ("/quest/instance/auto-advance", QuestInstanceFixture, NULL,
                instance_fixture_set_up, test_instance_auto_advance, instance_fixture_tear_down);
    g_test_add ("/quest/instance/complete", QuestInstanceFixture, NULL,
                instance_fixture_set_up, test_instance_complete, instance_fixture_tear_down);
    g_test_add ("/quest/instance/progress-calculation", QuestInstanceFixture, NULL,
                instance_fixture_set_up, test_instance_progress_calculation, instance_fixture_tear_down);
    g_test_add ("/quest/instance/not-active-no-progress", QuestInstanceFixture, NULL,
                instance_fixture_set_up, test_instance_not_active_no_progress, instance_fixture_tear_down);

    /* LrgQuestLog tests */
    g_test_add_func ("/quest/log/new", test_log_new);
    g_test_add ("/quest/log/start-quest", QuestLogFixture, NULL,
                log_fixture_set_up, test_log_start_quest, log_fixture_tear_down);
    g_test_add ("/quest/log/start-duplicate", QuestLogFixture, NULL,
                log_fixture_set_up, test_log_start_duplicate, log_fixture_tear_down);
    g_test_add ("/quest/log/get-quest", QuestLogFixture, NULL,
                log_fixture_set_up, test_log_get_quest, log_fixture_tear_down);
    g_test_add ("/quest/log/get-active-quests", QuestLogFixture, NULL,
                log_fixture_set_up, test_log_get_active_quests, log_fixture_tear_down);
    g_test_add ("/quest/log/complete-quest", QuestLogFixture, NULL,
                log_fixture_set_up, test_log_complete_quest, log_fixture_tear_down);
    g_test_add ("/quest/log/abandon-quest", QuestLogFixture, NULL,
                log_fixture_set_up, test_log_abandon_quest, log_fixture_tear_down);
    g_test_add ("/quest/log/track-quest", QuestLogFixture, NULL,
                log_fixture_set_up, test_log_track_quest, log_fixture_tear_down);
    g_test_add ("/quest/log/tracked-cleared-on-complete", QuestLogFixture, NULL,
                log_fixture_set_up, test_log_tracked_cleared_on_complete, log_fixture_tear_down);
    g_test_add ("/quest/log/tracked-cleared-on-abandon", QuestLogFixture, NULL,
                log_fixture_set_up, test_log_tracked_cleared_on_abandon, log_fixture_tear_down);
    g_test_add ("/quest/log/signals", QuestLogFixture, NULL,
                log_fixture_set_up, test_log_signals, log_fixture_tear_down);

    return g_test_run ();
}
