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
#include <string.h>

#ifndef LIBREGNUM_COMPILATION
#endif
#include "lrg-enums.h"
#include "quest/lrg-quest-objective.h"
#include "quest/lrg-quest-def.h"
#include "quest/lrg-quest-instance.h"
#include "quest/lrg-quest-log.h"
#include "quest/lrg-quest-chain.h"
#include "core/lrg-reset-schedule.h"

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
test_log_abandoned_instance_detached (QuestLogFixture *fixture,
                                      gconstpointer    user_data)
{
    g_autoptr(LrgQuestInstance) abandoned = NULL;
    LrgQuestInstance *restarted;

    abandoned = g_object_ref (lrg_quest_log_start_quest (fixture->log, fixture->def1));
    g_assert_true (lrg_quest_log_abandon_quest (fixture->log, "quest1"));
    restarted = lrg_quest_log_start_quest (fixture->log, fixture->def1);
    g_assert_nonnull (restarted);

    /* A retained instance must not complete or remove the restarted quest. */
    lrg_quest_instance_update_progress (abandoned,
                                         LRG_QUEST_OBJECTIVE_KILL, NULL, 1);
    g_assert_true (lrg_quest_log_is_quest_active (fixture->log, "quest1"));
    g_assert_false (lrg_quest_log_is_quest_completed (fixture->log, "quest1"));
    g_assert_true (lrg_quest_log_get_quest (fixture->log, "quest1") == restarted);
    g_assert_cmpuint (lrg_quest_log_get_active_count (fixture->log), ==, 1);
    g_assert_cmpuint (lrg_quest_log_get_completed_count (fixture->log), ==, 0);

    lrg_quest_instance_update_progress (restarted,
                                         LRG_QUEST_OBJECTIVE_KILL, NULL, 1);
    g_assert_true (lrg_quest_log_is_quest_completed (fixture->log, "quest1"));
}

static void
test_log_instance_outlives_log (QuestLogFixture *fixture,
                               gconstpointer    user_data)
{
    g_autoptr(LrgQuestInstance) instance = NULL;
    gpointer weak_log = fixture->log;

    instance = g_object_ref (lrg_quest_log_start_quest (fixture->log, fixture->def1));
    g_object_add_weak_pointer (G_OBJECT (fixture->log), &weak_log);
    g_clear_object (&fixture->log);
    g_assert_null (weak_log);

    /* Both objective and state changes must be safe after the log is gone. */
    lrg_quest_instance_update_progress (instance,
                                         LRG_QUEST_OBJECTIVE_KILL, NULL, 1);
    g_assert_cmpint (lrg_quest_instance_get_state (instance), ==,
                     LRG_QUEST_STATE_COMPLETE);
}

static void
test_log_track_foreign_instance (QuestLogFixture *fixture,
                                gconstpointer    user_data)
{
    g_autoptr(LrgQuestInstance) foreign = NULL;
    LrgQuestInstance *active;

    active = lrg_quest_log_start_quest (fixture->log, fixture->def1);
    foreign = lrg_quest_instance_new (fixture->def1);
    lrg_quest_instance_set_state (foreign, LRG_QUEST_STATE_ACTIVE);

    /* Matching IDs do not make an instance a member of this log. */
    lrg_quest_log_set_tracked_quest (fixture->log, foreign);
    g_assert_null (lrg_quest_log_get_tracked_quest (fixture->log));
    lrg_quest_log_set_tracked_quest (fixture->log, active);
    lrg_quest_log_set_tracked_quest (fixture->log, foreign);
    g_assert_true (lrg_quest_log_get_tracked_quest (fixture->log) == active);

    lrg_quest_instance_update_progress (active,
                                         LRG_QUEST_OBJECTIVE_KILL, NULL, 1);
    g_assert_null (lrg_quest_log_get_tracked_quest (fixture->log));
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
/*                     MMO progression: shared test helpers                   */
/* ========================================================================== */

#define DAY_SECONDS  ((gint64)86400)
#define WEEK_SECONDS ((gint64)604800)

/*
 * unix_utc:
 *
 * Returns: Unix seconds of a UTC calendar time.
 */
static gint64
unix_utc (gint year,
          gint month,
          gint day,
          gint hour,
          gint minute,
          gint second)
{
    g_autoptr(GDateTime) dt = NULL;

    dt = g_date_time_new_utc (year, month, day, hour, minute, (gdouble)second);
    g_assert_nonnull (dt);
    return g_date_time_to_unix (dt);
}

/*
 * simple_def:
 * @id: quest ID
 * @n_stages: number of stages
 * @target: target count of each stage's single KILL objective
 *
 * Returns: (transfer full): a definition whose stages are "kill anything @target times"
 */
static LrgQuestDef *
simple_def (const gchar *id,
            guint        n_stages,
            guint        target)
{
    LrgQuestDef *def;
    guint        i;

    def = lrg_quest_def_new (id);
    for (i = 0; i < n_stages; i++)
    {
        g_autofree gchar  *obj_id = g_strdup_printf ("o%u", i);
        LrgQuestObjective *obj = lrg_quest_objective_new (obj_id, "Kill things", LRG_QUEST_OBJECTIVE_KILL);
        lrg_quest_objective_set_target_count (obj, target);
        lrg_quest_def_add_stage (def, obj);
    }
    return def;
}

/*
 * make_objective:
 *
 * Returns: (transfer full): an objective with the given type, target and count
 */
static LrgQuestObjective *
make_objective (const gchar           *id,
                LrgQuestObjectiveType  type,
                const gchar           *target_id,
                guint                  target_count)
{
    LrgQuestObjective *obj;

    obj = lrg_quest_objective_new (id, id, type);
    lrg_quest_objective_set_target_id (obj, target_id);
    lrg_quest_objective_set_target_count (obj, target_count);
    return obj;
}

/*
 * parallel_def:
 *
 * Returns: (transfer full): stage 0 = kill 3 wolves AND collect 2 pelts;
 *   stage 1 = talk to the elder once
 */
static LrgQuestDef *
parallel_def (const gchar *id)
{
    LrgQuestDef *def;

    def = lrg_quest_def_new (id);
    lrg_quest_def_add_stage (def, make_objective ("wolves", LRG_QUEST_OBJECTIVE_KILL, "wolf", 3));
    g_assert_true (lrg_quest_def_add_stage_objective (def, 0,
                   make_objective ("pelts", LRG_QUEST_OBJECTIVE_COLLECT, "pelt", 2)));
    lrg_quest_def_add_stage (def, make_objective ("elder", LRG_QUEST_OBJECTIVE_INTERACT, "elder", 1));
    return def;
}

/*
 * finish_all_stages:
 *
 * Drives every objective of every stage to its target through
 * lrg_quest_instance_update_progress().
 */
static void
finish_all_stages (LrgQuestInstance *instance)
{
    guint guard;

    for (guard = 0; guard < 64 && lrg_quest_instance_get_state (instance) == LRG_QUEST_STATE_ACTIVE; guard++)
    {
        LrgQuestObjective *obj = NULL;
        guint              j;

        for (j = 0; j < lrg_quest_instance_get_objective_count (instance); j++)
        {
            obj = lrg_quest_instance_get_objective (instance, j);
            if (!lrg_quest_objective_is_complete (obj))
                break;
        }
        g_assert_nonnull (obj);
        lrg_quest_instance_update_progress (instance,
                                            lrg_quest_objective_get_objective_type (obj),
                                            lrg_quest_objective_get_target_id (obj),
                                            lrg_quest_objective_get_target_count (obj));
    }
    g_assert_cmpint (lrg_quest_instance_get_state (instance), ==, LRG_QUEST_STATE_COMPLETE);
}

static void
count_signal (gpointer instance,
              gpointer arg,
              gpointer user_data)
{
    guint *count = user_data;

    (*count)++;
    (void)instance;
    (void)arg;
}

static void
count_notify_signal (GObject    *object,
                     GParamSpec *pspec,
                     gpointer    user_data)
{
    guint *count = user_data;

    (*count)++;
    (void)object;
    (void)pspec;
}

/* ========================================================================== */
/*                         LrgQuestDef MMO additions                          */
/* ========================================================================== */

static void
test_def_mmo_properties (void)
{
    g_autoptr(LrgQuestDef) def = NULL;
    g_autofree gchar *category = NULL;
    g_autofree gchar *chain_id = NULL;
    g_autofree gchar *zone = NULL;
    guint             min_level;
    LrgQuestRepeat    repeat;
    guint             notifications;

    def = lrg_quest_def_new ("q");
    g_assert_cmpuint (lrg_quest_def_get_min_level (def), ==, 0);
    g_assert_cmpint (lrg_quest_def_get_repeat (def), ==, LRG_QUEST_REPEAT_NONE);
    g_assert_null (lrg_quest_def_get_category (def));
    g_assert_null (lrg_quest_def_get_chain_id (def));
    g_assert_null (lrg_quest_def_get_zone (def));
    g_assert_cmpuint (lrg_quest_def_get_exclusives (def)->len, ==, 0);

    notifications = 0;
    g_signal_connect (def, "notify", G_CALLBACK (count_notify_signal), &notifications);

    lrg_quest_def_set_min_level (def, 12);
    lrg_quest_def_set_repeat (def, LRG_QUEST_REPEAT_WEEKLY);
    lrg_quest_def_set_category (def, "daily");
    lrg_quest_def_set_chain_id (def, "westfall");
    lrg_quest_def_set_zone (def, "elwynn");
    g_assert_cmpuint (notifications, ==, 5);

    /* Same values do not notify again. */
    lrg_quest_def_set_min_level (def, 12);
    lrg_quest_def_set_repeat (def, LRG_QUEST_REPEAT_WEEKLY);
    lrg_quest_def_set_category (def, "daily");
    lrg_quest_def_set_chain_id (def, "westfall");
    lrg_quest_def_set_zone (def, "elwynn");
    g_assert_cmpuint (notifications, ==, 5);

    g_object_get (def,
                  "min-level", &min_level,
                  "repeat", &repeat,
                  "category", &category,
                  "chain-id", &chain_id,
                  "zone", &zone,
                  NULL);
    g_assert_cmpuint (min_level, ==, 12);
    g_assert_cmpint (repeat, ==, LRG_QUEST_REPEAT_WEEKLY);
    g_assert_cmpstr (category, ==, "daily");
    g_assert_cmpstr (chain_id, ==, "westfall");
    g_assert_cmpstr (zone, ==, "elwynn");

    /* g_object_set with a nullable string clears it. */
    g_object_set (def, "category", NULL, "repeat", LRG_QUEST_REPEAT_DAILY, NULL);
    g_assert_null (lrg_quest_def_get_category (def));
    g_assert_cmpint (lrg_quest_def_get_repeat (def), ==, LRG_QUEST_REPEAT_DAILY);
    g_assert_cmpuint (notifications, ==, 7);
}

static void
test_def_exclusives (void)
{
    g_autoptr(LrgQuestDef) def = NULL;
    GPtrArray             *ex;

    def = lrg_quest_def_new ("horde");
    lrg_quest_def_add_exclusive (def, "alliance");
    lrg_quest_def_add_exclusive (def, "neutral");
    lrg_quest_def_add_exclusive (def, "alliance");

    ex = lrg_quest_def_get_exclusives (def);
    g_assert_cmpuint (ex->len, ==, 2);
    g_assert_cmpstr (g_ptr_array_index (ex, 0), ==, "alliance");
    g_assert_cmpstr (g_ptr_array_index (ex, 1), ==, "neutral");
}

static void
test_def_stage_objectives (void)
{
    g_autoptr(LrgQuestDef) def = NULL;
    LrgQuestObjective     *first;
    GPtrArray             *objectives;
    guint                  i;

    def = lrg_quest_def_new ("q");

    /* No stage yet: rejected and freed (valgrind checks the free). */
    g_assert_false (lrg_quest_def_add_stage_objective (def, 0,
                    make_objective ("x", LRG_QUEST_OBJECTIVE_KILL, NULL, 1)));
    g_assert_null (lrg_quest_def_get_stage_objectives (def, 0));
    g_assert_cmpuint (lrg_quest_def_get_stage_objective_count (def, 0), ==, 0);

    first = make_objective ("a", LRG_QUEST_OBJECTIVE_KILL, "wolf", 3);
    lrg_quest_def_add_stage (def, first);
    g_assert_true (lrg_quest_def_add_stage_objective (def, 0,
                   make_objective ("b", LRG_QUEST_OBJECTIVE_COLLECT, "pelt", 2)));

    objectives = lrg_quest_def_get_stage_objectives (def, 0);
    g_assert_nonnull (objectives);
    g_assert_cmpuint (objectives->len, ==, 2);
    g_assert_true (g_ptr_array_index (objectives, 0) == first);
    g_assert_true (lrg_quest_def_get_stage (def, 0) == first);
    g_assert_cmpstr (lrg_quest_objective_get_id (g_ptr_array_index (objectives, 1)), ==, "b");

    /* Legacy views still show one stage with one objective each. */
    g_assert_cmpuint (lrg_quest_def_get_stage_count (def), ==, 1);
    g_assert_cmpuint (lrg_quest_def_get_stages (def)->len, ==, 1);

    /* Duplicate objective IDs within a stage are rejected. */
    g_assert_false (lrg_quest_def_add_stage_objective (def, 0,
                    make_objective ("b", LRG_QUEST_OBJECTIVE_KILL, NULL, 1)));
    g_assert_cmpuint (lrg_quest_def_get_stage_objective_count (def, 0), ==, 2);

    /* Out-of-range stage. */
    g_assert_false (lrg_quest_def_add_stage_objective (def, 1,
                    make_objective ("c", LRG_QUEST_OBJECTIVE_KILL, NULL, 1)));
    g_assert_null (lrg_quest_def_get_stage_objectives (def, 1));

    /* Fill to the cap of 16, then one more is rejected. */
    for (i = 2; i < LRG_QUEST_MAX_STAGE_OBJECTIVES; i++)
    {
        g_autofree gchar *id = g_strdup_printf ("fill%u", i);
        g_assert_true (lrg_quest_def_add_stage_objective (def, 0,
                       make_objective (id, LRG_QUEST_OBJECTIVE_KILL, NULL, 1)));
    }
    g_assert_cmpuint (lrg_quest_def_get_stage_objective_count (def, 0), ==, LRG_QUEST_MAX_STAGE_OBJECTIVES);
    g_assert_false (lrg_quest_def_add_stage_objective (def, 0,
                    make_objective ("overflow", LRG_QUEST_OBJECTIVE_KILL, NULL, 1)));
    g_assert_cmpuint (lrg_quest_def_get_stage_objective_count (def, 0), ==, LRG_QUEST_MAX_STAGE_OBJECTIVES);
}

static void
test_def_check_prerequisites_log (void)
{
    g_autoptr(LrgQuestDef) def = NULL;
    g_autoptr(LrgQuestDef) plain = NULL;
    g_autoptr(LrgQuestDef) rival = NULL;
    g_autoptr(LrgQuestLog) log = NULL;

    def = simple_def ("second", 1, 1);
    lrg_quest_def_add_prerequisite (def, "first");
    lrg_quest_def_add_exclusive (def, "rival");
    plain = simple_def ("plain", 1, 1);
    rival = simple_def ("rival", 1, 1);
    log = lrg_quest_log_new ();

    /* Non-log players keep the historical "no prerequisites" rule. */
    g_assert_false (lrg_quest_def_check_prerequisites (def, NULL));
    g_assert_false (lrg_quest_def_check_prerequisites (def, plain));
    g_assert_true (lrg_quest_def_check_prerequisites (plain, NULL));
    g_assert_true (lrg_quest_def_check_prerequisites (plain, rival));

    /* With a log: prerequisites must be completed. */
    g_assert_true (lrg_quest_def_check_prerequisites (plain, log));
    g_assert_false (lrg_quest_def_check_prerequisites (def, log));
    lrg_quest_log_mark_completed (log, "first", 1, 0);
    g_assert_true (lrg_quest_def_check_prerequisites (def, log));

    /* An active exclusive quest blocks it ... */
    g_assert_nonnull (lrg_quest_log_start_quest (log, rival));
    g_assert_false (lrg_quest_def_check_prerequisites (def, log));

    /* ... and so does a completed one. */
    g_assert_true (lrg_quest_log_abandon_quest (log, "rival"));
    g_assert_true (lrg_quest_def_check_prerequisites (def, log));
    lrg_quest_log_mark_completed (log, "rival", 1, 0);
    g_assert_false (lrg_quest_def_check_prerequisites (def, log));
}

/* ========================================================================== */
/*                     LrgQuestInstance parallel objectives                   */
/* ========================================================================== */

static void
test_instance_parallel_objectives (void)
{
    g_autoptr(LrgQuestDef)      def = NULL;
    g_autoptr(LrgQuestInstance) instance = NULL;
    guint                       stage_signals;
    guint                       objective_signals;

    def = parallel_def ("hunt");
    instance = lrg_quest_instance_new (def);
    lrg_quest_instance_set_state (instance, LRG_QUEST_STATE_ACTIVE);

    stage_signals = 0;
    objective_signals = 0;
    g_signal_connect (instance, "stage-advanced", G_CALLBACK (count_signal), &stage_signals);
    g_signal_connect (instance, "objective-updated", G_CALLBACK (count_signal), &objective_signals);

    g_assert_cmpuint (lrg_quest_instance_get_objective_count (instance), ==, 2);
    g_assert_cmpstr (lrg_quest_objective_get_id (lrg_quest_instance_get_current_objective (instance)), ==, "wolves");
    g_assert_cmpstr (lrg_quest_objective_get_id (lrg_quest_instance_get_objective (instance, 1)), ==, "pelts");
    g_assert_null (lrg_quest_instance_get_objective (instance, 2));

    /* Completing objective 0 alone does not advance the stage. */
    g_assert_true (lrg_quest_instance_update_progress (instance, LRG_QUEST_OBJECTIVE_KILL, "wolf", 5));
    g_assert_cmpuint (lrg_quest_instance_get_current_stage (instance), ==, 0);
    g_assert_cmpuint (lrg_quest_instance_get_objective_progress (instance, 0), ==, 5);
    g_assert_cmpuint (lrg_quest_instance_get_objective_progress (instance, 1), ==, 0);
    g_assert_cmpfloat_with_epsilon (lrg_quest_instance_get_progress (instance), 0.25, 1e-9);

    /* A completed objective is not incremented any further. */
    g_assert_false (lrg_quest_instance_update_progress (instance, LRG_QUEST_OBJECTIVE_KILL, "wolf", 1));
    g_assert_cmpuint (lrg_quest_instance_get_objective_progress (instance, 0), ==, 5);

    /* Wrong target does not match. */
    g_assert_false (lrg_quest_instance_update_progress (instance, LRG_QUEST_OBJECTIVE_COLLECT, "ore", 1));
    g_assert_true (lrg_quest_instance_update_progress (instance, LRG_QUEST_OBJECTIVE_COLLECT, "pelt", 1));
    g_assert_cmpuint (stage_signals, ==, 0);
    g_assert_cmpfloat_with_epsilon (lrg_quest_instance_get_progress (instance), 0.375, 1e-9);

    /* Finishing the last parallel objective advances exactly once. */
    g_assert_true (lrg_quest_instance_update_progress (instance, LRG_QUEST_OBJECTIVE_COLLECT, NULL, 1));
    g_assert_cmpuint (stage_signals, ==, 1);
    g_assert_cmpuint (objective_signals, ==, 3);
    g_assert_cmpuint (lrg_quest_instance_get_current_stage (instance), ==, 1);
    g_assert_cmpuint (lrg_quest_instance_get_objective_count (instance), ==, 1);
    g_assert_cmpuint (lrg_quest_instance_get_objective_progress (instance, 0), ==, 0);

    g_assert_true (lrg_quest_instance_update_progress (instance, LRG_QUEST_OBJECTIVE_INTERACT, "elder", 1));
    g_assert_true (lrg_quest_instance_is_complete (instance));
    g_assert_cmpuint (lrg_quest_instance_get_objective_count (instance), ==, 0);
    g_assert_null (lrg_quest_instance_get_current_objective (instance));
    g_assert_cmpfloat_with_epsilon (lrg_quest_instance_get_progress (instance), 1.0, 1e-9);

    /* Nothing left to advance. */
    g_assert_false (lrg_quest_instance_advance_stage (instance));
    g_assert_cmpuint (lrg_quest_instance_get_current_stage (instance), ==, 2);
}

static void
test_instance_update_matches_every_objective (void)
{
    g_autoptr(LrgQuestDef)      def = NULL;
    g_autoptr(LrgQuestInstance) instance = NULL;

    /* Two objectives both count wolves; an untargeted one counts any kill. */
    def = lrg_quest_def_new ("q");
    lrg_quest_def_add_stage (def, make_objective ("a", LRG_QUEST_OBJECTIVE_KILL, "wolf", 2));
    lrg_quest_def_add_stage_objective (def, 0, make_objective ("b", LRG_QUEST_OBJECTIVE_KILL, "wolf", 4));
    lrg_quest_def_add_stage_objective (def, 0, make_objective ("c", LRG_QUEST_OBJECTIVE_KILL, NULL, 10));
    lrg_quest_def_add_stage_objective (def, 0, make_objective ("d", LRG_QUEST_OBJECTIVE_KILL, "bear", 1));

    instance = lrg_quest_instance_new (def);
    lrg_quest_instance_set_state (instance, LRG_QUEST_STATE_ACTIVE);

    g_assert_true (lrg_quest_instance_update_progress (instance, LRG_QUEST_OBJECTIVE_KILL, "wolf", 2));
    g_assert_cmpuint (lrg_quest_instance_get_objective_progress (instance, 0), ==, 2);
    g_assert_cmpuint (lrg_quest_instance_get_objective_progress (instance, 1), ==, 2);
    g_assert_cmpuint (lrg_quest_instance_get_objective_progress (instance, 2), ==, 2);
    g_assert_cmpuint (lrg_quest_instance_get_objective_progress (instance, 3), ==, 0);

    /* A NULL target from the caller matches every targeted objective too. */
    g_assert_true (lrg_quest_instance_update_progress (instance, LRG_QUEST_OBJECTIVE_KILL, NULL, 1));
    g_assert_cmpuint (lrg_quest_instance_get_objective_progress (instance, 0), ==, 2);
    g_assert_cmpuint (lrg_quest_instance_get_objective_progress (instance, 1), ==, 3);
    g_assert_cmpuint (lrg_quest_instance_get_objective_progress (instance, 2), ==, 3);
    g_assert_cmpuint (lrg_quest_instance_get_objective_progress (instance, 3), ==, 1);
    g_assert_cmpint (lrg_quest_instance_get_state (instance), ==, LRG_QUEST_STATE_ACTIVE);

    g_assert_true (lrg_quest_instance_update_progress (instance, LRG_QUEST_OBJECTIVE_KILL, "any", 7));
    g_assert_true (lrg_quest_instance_update_progress (instance, LRG_QUEST_OBJECTIVE_KILL, "wolf", 1));
    g_assert_true (lrg_quest_instance_is_complete (instance));
}

static void
test_instance_restore_helpers (void)
{
    g_autoptr(LrgQuestDef)      def = NULL;
    g_autoptr(LrgQuestInstance) instance = NULL;
    guint                       stage_signals;

    def = parallel_def ("hunt");
    instance = lrg_quest_instance_new (def);
    lrg_quest_instance_set_state (instance, LRG_QUEST_STATE_ACTIVE);
    stage_signals = 0;
    g_signal_connect (instance, "stage-advanced", G_CALLBACK (count_signal), &stage_signals);

    /* Clamped to the target, completion derived from the clamped value. */
    g_assert_true (lrg_quest_instance_set_objective_progress (instance, 0, 99));
    g_assert_cmpuint (lrg_quest_instance_get_objective_progress (instance, 0), ==, 3);
    g_assert_true (lrg_quest_objective_is_complete (lrg_quest_instance_get_objective (instance, 0)));
    g_assert_true (lrg_quest_instance_set_objective_progress (instance, 0, 1));
    g_assert_false (lrg_quest_objective_is_complete (lrg_quest_instance_get_objective (instance, 0)));
    g_assert_false (lrg_quest_instance_set_objective_progress (instance, 2, 1));
    g_assert_cmpuint (lrg_quest_instance_get_objective_progress (instance, 2), ==, 0);

    /* Restoring never auto-advances. */
    g_assert_true (lrg_quest_instance_set_objective_progress (instance, 0, 3));
    g_assert_true (lrg_quest_instance_set_objective_progress (instance, 1, 2));
    g_assert_cmpuint (lrg_quest_instance_get_current_stage (instance), ==, 0);
    g_assert_cmpuint (stage_signals, ==, 0);

    /* set_stage resets progress, keeps the state and does not signal. */
    g_assert_true (lrg_quest_instance_set_stage (instance, 1));
    g_assert_cmpuint (lrg_quest_instance_get_current_stage (instance), ==, 1);
    g_assert_cmpuint (lrg_quest_instance_get_objective_count (instance), ==, 1);
    g_assert_cmpuint (lrg_quest_instance_get_objective_progress (instance, 0), ==, 0);
    g_assert_true (lrg_quest_instance_set_stage (instance, 0));
    g_assert_cmpuint (lrg_quest_instance_get_objective_progress (instance, 0), ==, 0);
    g_assert_cmpuint (lrg_quest_instance_get_objective_progress (instance, 1), ==, 0);
    g_assert_true (lrg_quest_instance_set_stage (instance, 2));
    g_assert_cmpuint (lrg_quest_instance_get_objective_count (instance), ==, 0);
    g_assert_false (lrg_quest_instance_set_objective_progress (instance, 0, 1));
    g_assert_false (lrg_quest_instance_set_stage (instance, 3));
    g_assert_cmpuint (lrg_quest_instance_get_current_stage (instance), ==, 2);
    g_assert_cmpint (lrg_quest_instance_get_state (instance), ==, LRG_QUEST_STATE_ACTIVE);
    g_assert_cmpuint (stage_signals, ==, 0);
}

static void
test_instance_complete_stage_not_stuck (void)
{
    g_autoptr(LrgQuestDef)      def = NULL;
    g_autoptr(LrgQuestInstance) instance = NULL;

    def = parallel_def ("hunt");
    instance = lrg_quest_instance_new (def);
    lrg_quest_instance_set_state (instance, LRG_QUEST_STATE_ACTIVE);

    /* A restored, fully complete stage advances on the next update. */
    lrg_quest_instance_set_objective_progress (instance, 0, 3);
    lrg_quest_instance_set_objective_progress (instance, 1, 2);
    g_assert_false (lrg_quest_instance_update_progress (instance, LRG_QUEST_OBJECTIVE_REACH, "x", 1));
    g_assert_cmpuint (lrg_quest_instance_get_current_stage (instance), ==, 1);

    /* An inactive instance never advances. */
    lrg_quest_instance_set_state (instance, LRG_QUEST_STATE_FAILED);
    g_assert_true (lrg_quest_instance_set_stage (instance, 0));
    lrg_quest_instance_set_objective_progress (instance, 0, 3);
    lrg_quest_instance_set_objective_progress (instance, 1, 2);
    g_assert_false (lrg_quest_instance_update_progress (instance, LRG_QUEST_OBJECTIVE_KILL, NULL, 1));
    g_assert_cmpuint (lrg_quest_instance_get_current_stage (instance), ==, 0);
}

static void
test_objective_increment_saturates (void)
{
    g_autoptr(LrgQuestObjective) obj = NULL;

    obj = lrg_quest_objective_new ("o", "Count", LRG_QUEST_OBJECTIVE_CUSTOM);
    lrg_quest_objective_set_target_count (obj, 5);
    g_assert_cmpuint (lrg_quest_objective_increment (obj, G_MAXUINT - 1), ==, G_MAXUINT - 1);
    g_assert_cmpuint (lrg_quest_objective_increment (obj, 10), ==, G_MAXUINT);
    g_assert_cmpuint (lrg_quest_objective_increment (obj, G_MAXUINT), ==, G_MAXUINT);
    g_assert_true (lrg_quest_objective_is_complete (obj));
}

/* ========================================================================== */
/*                          LrgQuestLog MMO additions                         */
/* ========================================================================== */

typedef struct
{
    LrgQuestLog      *log;
    LrgResetSchedule *schedule;
    GHashTable       *defs;   /* utf8 -> LrgQuestDef (owned) */
    gint64            reset;  /* a daily reset instant: 2026-09-24 04:00 UTC (Thursday) */
} ProgressionFixture;

static LrgQuestDef *
fixture_add (ProgressionFixture *fixture,
             LrgQuestDef        *def)
{
    g_hash_table_replace (fixture->defs, g_strdup (lrg_quest_def_get_id (def)), def);
    return def;
}

static LrgQuestDef *
fixture_def (ProgressionFixture *fixture,
             const gchar        *id)
{
    LrgQuestDef *def = g_hash_table_lookup (fixture->defs, id);
    g_assert_nonnull (def);
    return def;
}

static void
progression_set_up (ProgressionFixture *fixture,
                    gconstpointer       user_data)
{
    LrgQuestDef *def;

    fixture->log = lrg_quest_log_new ();
    fixture->schedule = lrg_reset_schedule_new (4, G_DATE_TUESDAY, 0);
    fixture->defs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
    fixture->reset = unix_utc (2026, 9, 24, 4, 0, 0);

    def = fixture_add (fixture, simple_def ("intro", 1, 1));
    lrg_quest_def_set_min_level (def, 1);

    def = fixture_add (fixture, simple_def ("story2", 2, 1));
    lrg_quest_def_add_prerequisite (def, "intro");
    lrg_quest_def_set_min_level (def, 5);

    def = fixture_add (fixture, simple_def ("daily_a", 1, 2));
    lrg_quest_def_set_repeat (def, LRG_QUEST_REPEAT_DAILY);
    lrg_quest_def_set_category (def, "daily");

    def = fixture_add (fixture, simple_def ("daily_b", 1, 1));
    lrg_quest_def_set_repeat (def, LRG_QUEST_REPEAT_DAILY);
    lrg_quest_def_set_category (def, "daily");

    def = fixture_add (fixture, simple_def ("weekly_a", 1, 1));
    lrg_quest_def_set_repeat (def, LRG_QUEST_REPEAT_WEEKLY);
    lrg_quest_def_set_category (def, "weekly");

    def = fixture_add (fixture, simple_def ("horde", 1, 1));
    lrg_quest_def_add_exclusive (def, "alliance");
    def = fixture_add (fixture, simple_def ("alliance", 1, 1));
    lrg_quest_def_add_exclusive (def, "horde");

    fixture_add (fixture, parallel_def ("hunt"));
    fixture_add (fixture, simple_def ("once", 1, 1));
    fixture_add (fixture, simple_def ("empty", 0, 1));

    (void)user_data;
}

static void
progression_tear_down (ProgressionFixture *fixture,
                       gconstpointer       user_data)
{
    g_clear_object (&fixture->log);
    g_clear_object (&fixture->schedule);
    g_clear_pointer (&fixture->defs, g_hash_table_unref);
    (void)user_data;
}

/*
 * assert_start_rejected:
 *
 * Asserts that can_start and start_quest_checked both fail with @code
 * and that the log snapshot is byte-for-byte unchanged afterwards.
 */
static void
assert_start_rejected (ProgressionFixture *fixture,
                       const gchar        *quest_id,
                       guint               level,
                       LrgResetSchedule   *schedule,
                       gint64              now,
                       gint                code,
                       const gchar        *message_part)
{
    g_autoptr(GVariant) before = NULL;
    g_autoptr(GVariant) after = NULL;
    g_autoptr(GError)   error = NULL;
    g_autoptr(GError)   error2 = NULL;
    LrgQuestDef        *def = fixture_def (fixture, quest_id);
    LrgQuestInstance   *active_before = lrg_quest_log_get_quest (fixture->log, quest_id);

    before = lrg_quest_log_to_variant (fixture->log);

    g_assert_false (lrg_quest_log_can_start (fixture->log, def, level, schedule, now, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, code);
    if (message_part != NULL)
        g_assert_nonnull (strstr (error->message, message_part));

    g_assert_null (lrg_quest_log_start_quest_checked (fixture->log, def, level, schedule, now, &error2));
    g_assert_error (error2, LRG_PROGRESSION_ERROR, code);

    after = lrg_quest_log_to_variant (fixture->log);
    g_assert_true (g_variant_equal (before, after));
    g_assert_true (lrg_quest_log_get_quest (fixture->log, quest_id) == active_before);
}

/*
 * start_finish_turn_in:
 *
 * Starts @quest_id through the checked path, completes it and turns it in.
 */
static void
start_finish_turn_in (ProgressionFixture *fixture,
                      const gchar        *quest_id,
                      gint64              now)
{
    g_autoptr(GError) error = NULL;
    LrgQuestInstance *instance;

    instance = lrg_quest_log_start_quest_checked (fixture->log, fixture_def (fixture, quest_id),
                                                  60, fixture->schedule, now, &error);
    g_assert_no_error (error);
    g_assert_nonnull (instance);
    finish_all_stages (instance);
    g_assert_true (lrg_quest_log_turn_in (fixture->log, quest_id, now, &error));
    g_assert_no_error (error);
}

static void
test_log_can_start_happy (ProgressionFixture *fixture,
                          gconstpointer       user_data)
{
    g_autoptr(GError) error = NULL;

    /* Minimum level is inclusive; a NULL schedule is fine for non-repeatables. */
    g_assert_true (lrg_quest_log_can_start (fixture->log, fixture_def (fixture, "intro"), 1,
                                            NULL, fixture->reset, &error));
    g_assert_no_error (error);

    /* Repeatables that were never completed do not need a schedule. */
    g_assert_true (lrg_quest_log_can_start (fixture->log, fixture_def (fixture, "daily_a"), 1,
                                            NULL, fixture->reset, &error));
    g_assert_no_error (error);

    /* can_start never mutates. */
    g_assert_cmpuint (lrg_quest_log_get_active_count (fixture->log), ==, 0);
    (void)user_data;
}

static void
test_log_can_start_rejections (ProgressionFixture *fixture,
                               gconstpointer       user_data)
{
    g_autoptr(GError) error = NULL;
    gint64 now = fixture->reset;

    /* 2. Level. */
    assert_start_rejected (fixture, "intro", 0, fixture->schedule, now,
                           LRG_PROGRESSION_ERROR_REQUIREMENT, "level 1");

    /* Order: a level failure is reported before a missing prerequisite. */
    assert_start_rejected (fixture, "story2", 4, fixture->schedule, now,
                           LRG_PROGRESSION_ERROR_REQUIREMENT, "level 5");

    /* 3. Prerequisite. */
    assert_start_rejected (fixture, "story2", 5, fixture->schedule, now,
                           LRG_PROGRESSION_ERROR_REQUIREMENT, "requires intro");

    /* 1. Already active (reported before level). */
    g_assert_nonnull (lrg_quest_log_start_quest_checked (fixture->log, fixture_def (fixture, "intro"),
                                                         1, NULL, now, &error));
    g_assert_no_error (error);
    assert_start_rejected (fixture, "intro", 0, fixture->schedule, now,
                           LRG_PROGRESSION_ERROR_DUPLICATE, "already active");

    /* 4. Exclusive quest active, then completed. */
    g_assert_nonnull (lrg_quest_log_start_quest_checked (fixture->log, fixture_def (fixture, "horde"),
                                                         1, NULL, now, &error));
    assert_start_rejected (fixture, "alliance", 60, fixture->schedule, now,
                           LRG_PROGRESSION_ERROR_REQUIREMENT, "exclusive");
    finish_all_stages (lrg_quest_log_get_quest (fixture->log, "horde"));
    g_assert_true (lrg_quest_log_turn_in (fixture->log, "horde", now, &error));
    assert_start_rejected (fixture, "alliance", 60, fixture->schedule, now,
                           LRG_PROGRESSION_ERROR_REQUIREMENT, "exclusive");

    /* 5. Completed and not repeatable. */
    assert_start_rejected (fixture, "horde", 60, fixture->schedule, now,
                           LRG_PROGRESSION_ERROR_DUPLICATE, "already completed");

    /* 6. Completed repeatable without a schedule, then within the period. */
    start_finish_turn_in (fixture, "daily_a", now);
    assert_start_rejected (fixture, "daily_a", 60, NULL, now + 10,
                           LRG_PROGRESSION_ERROR_INVALID, NULL);
    assert_start_rejected (fixture, "daily_a", 60, fixture->schedule, now + 10,
                           LRG_PROGRESSION_ERROR_NOT_READY, "day");

    /* A completion recorded in the future (clock skew) is not ready either. */
    lrg_quest_log_mark_completed (fixture->log, "daily_b", 1, now + 3 * DAY_SECONDS);
    assert_start_rejected (fixture, "daily_b", 60, fixture->schedule, now,
                           LRG_PROGRESSION_ERROR_NOT_READY, NULL);

    (void)user_data;
}

static void
test_log_prerequisite_satisfied (ProgressionFixture *fixture,
                                 gconstpointer       user_data)
{
    g_autoptr(GError) error = NULL;

    start_finish_turn_in (fixture, "intro", fixture->reset);
    g_assert_true (lrg_quest_log_can_start (fixture->log, fixture_def (fixture, "story2"), 5,
                                            NULL, fixture->reset, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_quest_def_check_prerequisites (fixture_def (fixture, "story2"), fixture->log));
    (void)user_data;
}

static void
test_log_capacity_limits (ProgressionFixture *fixture,
                          gconstpointer       user_data)
{
    g_autoptr(GPtrArray) extra = NULL;
    g_autoptr(GError)    error = NULL;
    guint                i;

    /* Fill the active set to the snapshot bound. */
    extra = g_ptr_array_new_with_free_func (g_object_unref);
    for (i = 0; i < LRG_QUEST_LOG_MAX_ACTIVE; i++)
    {
        g_autofree gchar *id = g_strdup_printf ("filler%03u", i);
        LrgQuestDef      *def = simple_def (id, 1, 1);

        g_ptr_array_add (extra, def);
        g_assert_nonnull (lrg_quest_log_start_quest_checked (fixture->log, def, 1, NULL,
                                                             fixture->reset, &error));
        g_assert_no_error (error);
    }
    assert_start_rejected (fixture, "once", 60, fixture->schedule, fixture->reset,
                           LRG_PROGRESSION_ERROR_LIMIT, NULL);
    g_assert_cmpuint (lrg_quest_log_get_active_count (fixture->log), ==, LRG_QUEST_LOG_MAX_ACTIVE);
    (void)user_data;
}

static void
test_log_record_limit (ProgressionFixture *fixture,
                       gconstpointer       user_data)
{
    g_autoptr(GError) error = NULL;
    guint             i;

    for (i = 0; i < LRG_QUEST_LOG_MAX_COMPLETED - 1; i++)
    {
        g_autofree gchar *id = g_strdup_printf ("done%05u", i);
        lrg_quest_log_mark_completed (fixture->log, id, 1, 1);
    }
    lrg_quest_log_mark_completed (fixture->log, "daily_a", 1, fixture->reset - DAY_SECONDS);
    g_assert_cmpuint (lrg_quest_log_get_completed_count (fixture->log), ==, LRG_QUEST_LOG_MAX_COMPLETED);

    /* A new record would not fit ... */
    assert_start_rejected (fixture, "once", 60, fixture->schedule, fixture->reset,
                           LRG_PROGRESSION_ERROR_LIMIT, NULL);

    /* ... but a repeat of an already recorded quest does. */
    g_assert_true (lrg_quest_log_can_start (fixture->log, fixture_def (fixture, "daily_a"), 60,
                                            fixture->schedule, fixture->reset, &error));
    g_assert_no_error (error);
    (void)user_data;
}

static void
test_log_turn_in_flow (ProgressionFixture *fixture,
                       gconstpointer       user_data)
{
    g_autoptr(GError) error = NULL;
    LrgQuestInstance *instance;
    guint             ready = 0;
    guint             completed = 0;

    g_signal_connect (fixture->log, "quest-ready", G_CALLBACK (count_signal), &ready);
    g_signal_connect (fixture->log, "quest-completed", G_CALLBACK (count_signal), &completed);

    instance = lrg_quest_log_start_quest_checked (fixture->log, fixture_def (fixture, "hunt"),
                                                  1, NULL, fixture->reset, &error);
    g_assert_no_error (error);
    g_assert_true (lrg_quest_log_track_quest (fixture->log, "hunt"));

    /* Not complete yet: REQUIREMENT, nothing changes. */
    g_assert_false (lrg_quest_log_turn_in (fixture->log, "hunt", fixture->reset, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);
    g_assert_true (lrg_quest_log_is_quest_active (fixture->log, "hunt"));
    g_assert_cmpuint (lrg_quest_log_get_completion_count (fixture->log, "hunt"), ==, 0);

    /* Finishing every stage keeps it active and tracked, awaiting turn-in. */
    finish_all_stages (instance);
    g_assert_cmpuint (ready, ==, 1);
    g_assert_cmpuint (completed, ==, 0);
    g_assert_true (lrg_quest_log_is_quest_active (fixture->log, "hunt"));
    g_assert_false (lrg_quest_log_is_quest_completed (fixture->log, "hunt"));
    g_assert_true (lrg_quest_log_get_tracked_quest (fixture->log) == instance);

    /* Unknown / inactive IDs: NOT_FOUND. */
    g_assert_false (lrg_quest_log_turn_in (fixture->log, "nope", fixture->reset, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_NOT_FOUND);
    g_clear_error (&error);
    g_assert_false (lrg_quest_log_turn_in (fixture->log, "once", fixture->reset, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_NOT_FOUND);
    g_clear_error (&error);

    g_assert_true (lrg_quest_log_turn_in (fixture->log, "hunt", fixture->reset + 5, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (completed, ==, 1);
    g_assert_false (lrg_quest_log_is_quest_active (fixture->log, "hunt"));
    g_assert_true (lrg_quest_log_is_quest_completed (fixture->log, "hunt"));
    g_assert_cmpuint (lrg_quest_log_get_completion_count (fixture->log, "hunt"), ==, 1);
    g_assert_cmpint (lrg_quest_log_get_last_completed (fixture->log, "hunt"), ==, fixture->reset + 5);
    g_assert_null (lrg_quest_log_get_tracked_quest (fixture->log));
    g_assert_true (lrg_quest_log_get_quest (fixture->log, "hunt") == instance);
    g_assert_cmpuint (lrg_quest_log_get_completed_count (fixture->log), ==, 1);

    /* Turning in twice is NOT_FOUND (no longer active). */
    g_assert_false (lrg_quest_log_turn_in (fixture->log, "hunt", fixture->reset + 6, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_NOT_FOUND);
    g_assert_cmpuint (lrg_quest_log_get_completion_count (fixture->log, "hunt"), ==, 1);

    /* A turned-in instance no longer drives the log. */
    lrg_quest_instance_set_state (instance, LRG_QUEST_STATE_FAILED);
    g_assert_true (lrg_quest_log_is_quest_completed (fixture->log, "hunt"));
    (void)user_data;
}

static void
test_log_turn_in_failed_quest (ProgressionFixture *fixture,
                               gconstpointer       user_data)
{
    g_autoptr(GError) error = NULL;
    LrgQuestInstance *instance;

    instance = lrg_quest_log_start_quest_checked (fixture->log, fixture_def (fixture, "once"),
                                                  1, NULL, fixture->reset, &error);
    lrg_quest_instance_fail (instance);
    g_assert_false (lrg_quest_log_turn_in (fixture->log, "once", fixture->reset, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_assert_true (lrg_quest_log_is_quest_active (fixture->log, "once"));
    g_assert_false (lrg_quest_log_is_quest_completed (fixture->log, "once"));
    (void)user_data;
}

static void
test_log_repeat_daily (ProgressionFixture *fixture,
                       gconstpointer       user_data)
{
    g_autoptr(GError) error = NULL;
    LrgQuestInstance *instance;
    gint64            first = fixture->reset + 3600;
    gint64            next_reset = fixture->reset + DAY_SECONDS;

    start_finish_turn_in (fixture, "daily_a", first);
    g_assert_cmpuint (lrg_quest_log_get_completion_count (fixture->log, "daily_a"), ==, 1);

    /* Same period, up to one second before the reset: not ready. */
    assert_start_rejected (fixture, "daily_a", 1, fixture->schedule, next_reset - 1,
                           LRG_PROGRESSION_ERROR_NOT_READY, NULL);

    /* Exactly at the reset instant it is available again. */
    instance = lrg_quest_log_start_quest_checked (fixture->log, fixture_def (fixture, "daily_a"),
                                                  1, fixture->schedule, next_reset, &error);
    g_assert_no_error (error);
    g_assert_nonnull (instance);

    /* The history survives the restart. */
    g_assert_true (lrg_quest_log_is_quest_active (fixture->log, "daily_a"));
    g_assert_true (lrg_quest_log_is_quest_completed (fixture->log, "daily_a"));
    g_assert_cmpuint (lrg_quest_log_get_completion_count (fixture->log, "daily_a"), ==, 1);
    g_assert_cmpint (lrg_quest_log_get_last_completed (fixture->log, "daily_a"), ==, first);
    g_assert_true (lrg_quest_log_get_quest (fixture->log, "daily_a") == instance);

    finish_all_stages (instance);
    g_assert_true (lrg_quest_log_turn_in (fixture->log, "daily_a", next_reset + 10, &error));
    g_assert_cmpuint (lrg_quest_log_get_completion_count (fixture->log, "daily_a"), ==, 2);
    g_assert_cmpint (lrg_quest_log_get_last_completed (fixture->log, "daily_a"), ==, next_reset + 10);

    /* Skipping several days is fine too. */
    assert_start_rejected (fixture, "daily_a", 1, fixture->schedule, next_reset + 20,
                           LRG_PROGRESSION_ERROR_NOT_READY, NULL);
    start_finish_turn_in (fixture, "daily_a", next_reset + 5 * DAY_SECONDS);
    g_assert_cmpuint (lrg_quest_log_get_completion_count (fixture->log, "daily_a"), ==, 3);
    (void)user_data;
}

static void
test_log_repeat_weekly (ProgressionFixture *fixture,
                        gconstpointer       user_data)
{
    g_autoptr(GError) error = NULL;
    gint64            tuesday = unix_utc (2026, 9, 29, 4, 0, 0);

    /* Completed on Thursday; blocked until Tuesday 04:00. */
    start_finish_turn_in (fixture, "weekly_a", fixture->reset);
    assert_start_rejected (fixture, "weekly_a", 1, fixture->schedule, fixture->reset + DAY_SECONDS,
                           LRG_PROGRESSION_ERROR_NOT_READY, "week");
    assert_start_rejected (fixture, "weekly_a", 1, fixture->schedule, tuesday - 1,
                           LRG_PROGRESSION_ERROR_NOT_READY, NULL);
    g_assert_true (lrg_quest_log_can_start (fixture->log, fixture_def (fixture, "weekly_a"), 1,
                                            fixture->schedule, tuesday, &error));
    g_assert_no_error (error);

    /* Completed exactly at the Tuesday reset; blocked until the next one. */
    start_finish_turn_in (fixture, "weekly_a", tuesday);
    assert_start_rejected (fixture, "weekly_a", 1, fixture->schedule, tuesday + WEEK_SECONDS - 1,
                           LRG_PROGRESSION_ERROR_NOT_READY, NULL);
    g_assert_true (lrg_quest_log_can_start (fixture->log, fixture_def (fixture, "weekly_a"), 1,
                                            fixture->schedule, tuesday + WEEK_SECONDS, &error));
    g_assert_cmpuint (lrg_quest_log_get_completion_count (fixture->log, "weekly_a"), ==, 2);
    (void)user_data;
}

static void
test_log_repeat_negative_time (ProgressionFixture *fixture,
                               gconstpointer       user_data)
{
    g_autoptr(GError) error = NULL;
    gint64            before_epoch = unix_utc (1969, 12, 30, 12, 0, 0);

    /* Reset schedules work before 1970 as well. */
    start_finish_turn_in (fixture, "daily_b", before_epoch);
    g_assert_cmpint (lrg_quest_log_get_last_completed (fixture->log, "daily_b"), ==, before_epoch);
    assert_start_rejected (fixture, "daily_b", 1, fixture->schedule, unix_utc (1969, 12, 31, 3, 59, 59),
                           LRG_PROGRESSION_ERROR_NOT_READY, NULL);
    g_assert_true (lrg_quest_log_can_start (fixture->log, fixture_def (fixture, "daily_b"), 1,
                                            fixture->schedule, unix_utc (1969, 12, 31, 4, 0, 0), &error));
    g_assert_no_error (error);
    (void)user_data;
}

static void
test_log_count_completed_in_period (ProgressionFixture *fixture,
                                    gconstpointer       user_data)
{
    LrgQuestLog      *log = fixture->log;
    LrgResetSchedule *s = fixture->schedule;
    gint64            now = fixture->reset + 7200;

    g_assert_cmpuint (lrg_quest_log_count_completed_in_period (log, NULL, s, LRG_RESET_PERIOD_DAILY,
                                                               now, fixture->defs), ==, 0);

    start_finish_turn_in (fixture, "daily_a", fixture->reset);          /* at the reset instant */
    start_finish_turn_in (fixture, "daily_b", fixture->reset + 3600);
    start_finish_turn_in (fixture, "weekly_a", fixture->reset + 60);
    start_finish_turn_in (fixture, "once", fixture->reset + 60);       /* not repeatable */
    lrg_quest_log_mark_completed (log, "unknown_daily", 1, fixture->reset + 60); /* no def */

    g_assert_cmpuint (lrg_quest_log_count_completed_in_period (log, "daily", s, LRG_RESET_PERIOD_DAILY,
                                                               now, fixture->defs), ==, 2);
    g_assert_cmpuint (lrg_quest_log_count_completed_in_period (log, "weekly", s, LRG_RESET_PERIOD_DAILY,
                                                               now, fixture->defs), ==, 1);
    g_assert_cmpuint (lrg_quest_log_count_completed_in_period (log, NULL, s, LRG_RESET_PERIOD_DAILY,
                                                               now, fixture->defs), ==, 3);
    g_assert_cmpuint (lrg_quest_log_count_completed_in_period (log, "story", s, LRG_RESET_PERIOD_DAILY,
                                                               now, fixture->defs), ==, 0);

    /* One second before the reset those completions are in the previous day. */
    g_assert_cmpuint (lrg_quest_log_count_completed_in_period (log, NULL, s, LRG_RESET_PERIOD_DAILY,
                                                               fixture->reset - 1, fixture->defs), ==, 0);

    /* Next day: daily counts reset, the week still counts them. */
    g_assert_cmpuint (lrg_quest_log_count_completed_in_period (log, "daily", s, LRG_RESET_PERIOD_DAILY,
                                                               now + DAY_SECONDS, fixture->defs), ==, 0);
    g_assert_cmpuint (lrg_quest_log_count_completed_in_period (log, "daily", s, LRG_RESET_PERIOD_WEEKLY,
                                                               now + DAY_SECONDS, fixture->defs), ==, 2);
    g_assert_cmpuint (lrg_quest_log_count_completed_in_period (log, NULL, s, LRG_RESET_PERIOD_WEEKLY,
                                                               unix_utc (2026, 9, 29, 4, 0, 0),
                                                               fixture->defs), ==, 0);

    /* Unknown completion times never count. */
    lrg_quest_log_mark_completed (log, "daily_a", 4, 0);
    g_assert_cmpuint (lrg_quest_log_count_completed_in_period (log, "daily", s, LRG_RESET_PERIOD_DAILY,
                                                               now, fixture->defs), ==, 1);
    (void)user_data;
}

static void
test_log_mark_completed (ProgressionFixture *fixture,
                         gconstpointer       user_data)
{
    g_autoptr(GPtrArray) ids = NULL;
    guint                notifications = 0;

    g_signal_connect (fixture->log, "notify::completed-count",
                      G_CALLBACK (count_notify_signal), &notifications);

    lrg_quest_log_mark_completed (fixture->log, "zeta", 3, 1234);
    lrg_quest_log_mark_completed (fixture->log, "alpha", 1, -50);
    g_assert_cmpuint (notifications, ==, 2);
    g_assert_cmpuint (lrg_quest_log_get_completed_count (fixture->log), ==, 2);
    g_assert_cmpuint (lrg_quest_log_get_completion_count (fixture->log, "zeta"), ==, 3);
    g_assert_cmpint (lrg_quest_log_get_last_completed (fixture->log, "zeta"), ==, 1234);
    g_assert_cmpint (lrg_quest_log_get_last_completed (fixture->log, "alpha"), ==, -50);
    g_assert_true (lrg_quest_log_is_quest_completed (fixture->log, "zeta"));
    g_assert_null (lrg_quest_log_get_quest (fixture->log, "zeta"));
    g_assert_cmpuint (lrg_quest_log_get_completion_count (fixture->log, "never"), ==, 0);
    g_assert_cmpint (lrg_quest_log_get_last_completed (fixture->log, "never"), ==, 0);

    /* Replacing does not add a record. */
    lrg_quest_log_mark_completed (fixture->log, "zeta", 1, 99);
    g_assert_cmpuint (notifications, ==, 2);
    g_assert_cmpuint (lrg_quest_log_get_completion_count (fixture->log, "zeta"), ==, 1);

    ids = lrg_quest_log_get_completed_ids (fixture->log);
    g_assert_cmpuint (ids->len, ==, 2);
    g_assert_cmpstr (g_ptr_array_index (ids, 0), ==, "alpha");
    g_assert_cmpstr (g_ptr_array_index (ids, 1), ==, "zeta");

    /* Programmer errors are refused without changes. */
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*count >= 1*");
    lrg_quest_log_mark_completed (fixture->log, "beta", 0, 1);
    g_test_assert_expected_messages ();
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*quest_id_is_valid*");
    lrg_quest_log_mark_completed (fixture->log, "", 1, 1);
    g_test_assert_expected_messages ();
    g_assert_cmpuint (lrg_quest_log_get_completed_count (fixture->log), ==, 2);

    /* The legacy start path refuses marked quests. */
    lrg_quest_log_mark_completed (fixture->log, "intro", 1, 0);
    g_assert_null (lrg_quest_log_start_quest (fixture->log, fixture_def (fixture, "intro")));
    (void)user_data;
}

static void
test_log_legacy_completion_record (ProgressionFixture *fixture,
                                   gconstpointer       user_data)
{
    LrgQuestInstance *instance;

    /* Legacy quests auto-complete and get a record with an unknown time. */
    instance = lrg_quest_log_start_quest (fixture->log, fixture_def (fixture, "once"));
    lrg_quest_instance_update_progress (instance, LRG_QUEST_OBJECTIVE_KILL, NULL, 1);
    g_assert_true (lrg_quest_log_is_quest_completed (fixture->log, "once"));
    g_assert_cmpuint (lrg_quest_log_get_completion_count (fixture->log, "once"), ==, 1);
    g_assert_cmpint (lrg_quest_log_get_last_completed (fixture->log, "once"), ==, 0);

    /* Re-setting the state of a completed instance is ignored by the log. */
    lrg_quest_instance_set_state (instance, LRG_QUEST_STATE_FAILED);
    lrg_quest_instance_set_state (instance, LRG_QUEST_STATE_COMPLETE);
    g_assert_cmpuint (lrg_quest_log_get_completion_count (fixture->log, "once"), ==, 1);
    (void)user_data;
}

static void
test_log_abandon_checked (ProgressionFixture *fixture,
                          gconstpointer       user_data)
{
    g_autoptr(GError) error = NULL;
    LrgQuestInstance *instance;

    instance = lrg_quest_log_start_quest_checked (fixture->log, fixture_def (fixture, "once"),
                                                  1, NULL, fixture->reset, &error);
    finish_all_stages (instance);
    g_assert_true (lrg_quest_log_abandon_quest (fixture->log, "once"));

    /* Restarting after abandon works and is a fresh turn-in quest. */
    instance = lrg_quest_log_start_quest_checked (fixture->log, fixture_def (fixture, "once"),
                                                  1, NULL, fixture->reset, &error);
    g_assert_no_error (error);
    g_assert_cmpint (lrg_quest_instance_get_state (instance), ==, LRG_QUEST_STATE_ACTIVE);
    finish_all_stages (instance);
    g_assert_true (lrg_quest_log_is_quest_active (fixture->log, "once"));
    g_assert_true (lrg_quest_log_turn_in (fixture->log, "once", fixture->reset, &error));
    (void)user_data;
}

/* ========================================================================== */
/*                          LrgQuestLog persistence                           */
/* ========================================================================== */

typedef struct
{
    const gchar *id;
    guint        stage;
    guint        n_values;
    guint        values[20];
} ActiveSpec;

typedef struct
{
    const gchar *id;
    guint        count;
    gint64       last;
} DoneSpec;

/*
 * build_snapshot:
 *
 * Returns: (transfer full): a non-floating "(a(suau)a(sux))" built from specs
 */
static GVariant *
build_snapshot (const ActiveSpec *active,
                guint             n_active,
                const DoneSpec   *done,
                guint             n_done)
{
    GVariantBuilder builder;
    guint           i;
    guint           j;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("(a(suau)a(sux))"));
    g_variant_builder_open (&builder, G_VARIANT_TYPE ("a(suau)"));
    for (i = 0; i < n_active; i++)
    {
        g_variant_builder_open (&builder, G_VARIANT_TYPE ("(suau)"));
        g_variant_builder_add (&builder, "s", active[i].id);
        g_variant_builder_add (&builder, "u", (guint32)active[i].stage);
        g_variant_builder_open (&builder, G_VARIANT_TYPE ("au"));
        for (j = 0; j < active[i].n_values; j++)
            g_variant_builder_add (&builder, "u", (guint32)active[i].values[j]);
        g_variant_builder_close (&builder);
        g_variant_builder_close (&builder);
    }
    g_variant_builder_close (&builder);
    g_variant_builder_open (&builder, G_VARIANT_TYPE ("a(sux)"));
    for (i = 0; i < n_done; i++)
        g_variant_builder_add (&builder, "(sux)", done[i].id, (guint32)done[i].count, done[i].last);
    g_variant_builder_close (&builder);
    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

static void
assert_snapshot_rejected (GVariant   *snapshot,
                          GHashTable *defs,
                          gint        code)
{
    g_autoptr(GError)      error = NULL;
    g_autoptr(LrgQuestLog) log = NULL;

    log = lrg_quest_log_new_from_variant (snapshot, defs, &error);
    g_assert_null (log);
    g_assert_error (error, LRG_PROGRESSION_ERROR, code);
}

static void
test_log_variant_round_trip (ProgressionFixture *fixture,
                             gconstpointer       user_data)
{
    g_autoptr(GVariant)    first = NULL;
    g_autoptr(GVariant)    second = NULL;
    g_autoptr(LrgQuestLog) restored = NULL;
    g_autoptr(GError)      error = NULL;
    LrgQuestInstance      *hunt;
    LrgQuestInstance      *instance;
    guint                  ready = 0;

    /* Build a varied log: history, a partial parallel stage, one awaiting turn-in. */
    start_finish_turn_in (fixture, "intro", fixture->reset - 5);
    start_finish_turn_in (fixture, "daily_a", fixture->reset);
    lrg_quest_log_mark_completed (fixture->log, "weekly_a", 7, -86400);
    hunt = lrg_quest_log_start_quest_checked (fixture->log, fixture_def (fixture, "hunt"),
                                              1, NULL, fixture->reset, &error);
    g_assert_no_error (error);
    lrg_quest_instance_update_progress (hunt, LRG_QUEST_OBJECTIVE_KILL, "wolf", 9); /* over target */
    lrg_quest_instance_update_progress (hunt, LRG_QUEST_OBJECTIVE_COLLECT, "pelt", 1);
    instance = lrg_quest_log_start_quest_checked (fixture->log, fixture_def (fixture, "story2"),
                                                  5, NULL, fixture->reset, &error);
    g_assert_no_error (error);
    lrg_quest_instance_update_progress (instance, LRG_QUEST_OBJECTIVE_KILL, NULL, 1);
    instance = lrg_quest_log_start_quest_checked (fixture->log, fixture_def (fixture, "once"),
                                                  1, NULL, fixture->reset, &error);
    finish_all_stages (instance);
    lrg_quest_log_start_quest_checked (fixture->log, fixture_def (fixture, "empty"),
                                       1, NULL, fixture->reset, &error);
    g_assert_no_error (error);

    first = lrg_quest_log_to_variant (fixture->log);
    g_assert_false (g_variant_is_floating (first));
    g_assert_cmpstr (g_variant_get_type_string (first), ==, LRG_QUEST_LOG_VARIANT_TYPE);
    g_assert_true (g_variant_is_normal_form (first));

    /* Exact expected content (sorted, clamped). */
    {
        g_autofree gchar *text = g_variant_print (first, FALSE);
        g_assert_cmpstr (text, ==,
                         "([('empty', 0, []), ('hunt', 0, [3, 1]), ('once', 1, []), ('story2', 1, [0])], "
                         "[('daily_a', 1, 1790222400), ('intro', 1, 1790222395), ('weekly_a', 7, -86400)])");
    }

    restored = lrg_quest_log_new_from_variant (first, fixture->defs, &error);
    g_assert_no_error (error);
    g_assert_nonnull (restored);
    second = lrg_quest_log_to_variant (restored);
    g_assert_true (g_variant_equal (first, second));

    /* Restored runtime state. */
    g_assert_cmpuint (lrg_quest_log_get_active_count (restored), ==, 4);
    g_assert_cmpuint (lrg_quest_log_get_completed_count (restored), ==, 3);
    g_assert_null (lrg_quest_log_get_tracked_quest (restored));
    g_assert_cmpint (lrg_quest_instance_get_state (lrg_quest_log_get_quest (restored, "once")), ==,
                     LRG_QUEST_STATE_COMPLETE);
    g_assert_cmpint (lrg_quest_instance_get_state (lrg_quest_log_get_quest (restored, "empty")), ==,
                     LRG_QUEST_STATE_COMPLETE);
    g_assert_cmpint (lrg_quest_instance_get_state (lrg_quest_log_get_quest (restored, "hunt")), ==,
                     LRG_QUEST_STATE_ACTIVE);
    g_assert_cmpint (lrg_quest_instance_get_state (lrg_quest_log_get_quest (restored, "intro")), ==,
                     LRG_QUEST_STATE_COMPLETE);
    g_assert_cmpuint (lrg_quest_log_get_completion_count (restored, "weekly_a"), ==, 7);
    g_assert_cmpint (lrg_quest_log_get_last_completed (restored, "weekly_a"), ==, -86400);

    /* Restored quests keep working: progress, ready signal and turn-in. */
    g_signal_connect (restored, "quest-ready", G_CALLBACK (count_signal), &ready);
    hunt = lrg_quest_log_get_quest (restored, "hunt");
    g_assert_cmpuint (lrg_quest_instance_get_objective_progress (hunt, 0), ==, 3);
    g_assert_cmpuint (lrg_quest_instance_get_objective_progress (hunt, 1), ==, 1);
    finish_all_stages (hunt);
    g_assert_cmpuint (ready, ==, 1);
    g_assert_true (lrg_quest_log_turn_in (restored, "hunt", fixture->reset + 9, &error));
    g_assert_true (lrg_quest_log_turn_in (restored, "once", fixture->reset + 9, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_quest_log_get_completed_count (restored), ==, 5);

    /* The original is untouched by restoring. */
    g_assert_true (lrg_quest_log_is_quest_active (fixture->log, "hunt"));
    (void)user_data;
}

static void
test_log_variant_empty_and_short_progress (ProgressionFixture *fixture,
                                           gconstpointer       user_data)
{
    g_autoptr(GVariant)    empty = NULL;
    g_autoptr(GVariant)    again = NULL;
    g_autoptr(GVariant)    shorter = NULL;
    g_autoptr(GVariant)    full = NULL;
    g_autoptr(LrgQuestLog) log = NULL;
    g_autoptr(LrgQuestLog) log2 = NULL;
    g_autoptr(GError)      error = NULL;
    ActiveSpec             a = { "hunt", 0, 1, { 2 } };

    empty = lrg_quest_log_to_variant (fixture->log);
    log = lrg_quest_log_new_from_variant (empty, fixture->defs, &error);
    g_assert_no_error (error);
    again = lrg_quest_log_to_variant (log);
    g_assert_true (g_variant_equal (empty, again));

    /* Fewer values than objectives: missing values are zero. */
    shorter = build_snapshot (&a, 1, NULL, 0);
    log2 = lrg_quest_log_new_from_variant (shorter, fixture->defs, &error);
    g_assert_no_error (error);
    full = lrg_quest_log_to_variant (log2);
    {
        g_autofree gchar *text = g_variant_print (full, FALSE);
        g_assert_cmpstr (text, ==, "([('hunt', 0, [2, 0])], [])");
    }
    (void)user_data;
}

static void
test_log_variant_boundaries_accepted (ProgressionFixture *fixture,
                                      gconstpointer       user_data)
{
    g_autoptr(GVariant)    snapshot = NULL;
    g_autoptr(GVariant)    again = NULL;
    g_autoptr(LrgQuestLog) log = NULL;
    g_autoptr(GError)      error = NULL;
    g_autofree gchar      *long_id = g_strnfill (128, 'q');
    ActiveSpec             active[2];
    DoneSpec               done[2];

    /* A 128-byte UTF-8 ID, progress equal to the target, stage == count, extreme values. */
    fixture_add (fixture, simple_def (long_id, 1, 1));
    active[0] = (ActiveSpec){ "hunt", 0, 2, { 3, 2 } };
    active[1] = (ActiveSpec){ long_id, 1, 0, { 0 } };
    done[0] = (DoneSpec){ "intro", G_MAXUINT32, G_MININT64 };
    done[1] = (DoneSpec){ "\xc3\xa9t\xc3\xa9", 1, G_MAXINT64 };
    fixture_add (fixture, simple_def ("\xc3\xa9t\xc3\xa9", 1, 1));

    snapshot = build_snapshot (active, 2, done, 2);
    log = lrg_quest_log_new_from_variant (snapshot, fixture->defs, &error);
    g_assert_no_error (error);
    g_assert_nonnull (log);
    again = lrg_quest_log_to_variant (log);
    g_assert_true (g_variant_equal (snapshot, again));
    (void)user_data;
}

static void
test_log_variant_hostile (ProgressionFixture *fixture,
                          gconstpointer       user_data)
{
    g_autofree gchar *long_id = g_strnfill (129, 'q');
    GHashTable       *defs = fixture->defs;

    /* Wrong type strings. */
    {
        g_autoptr(GVariant) v = g_variant_ref_sink (g_variant_new_string ("nope"));
        assert_snapshot_rejected (v, defs, LRG_PROGRESSION_ERROR_INVALID);
    }
    {
        g_autoptr(GVariant) v = g_variant_ref_sink (g_variant_new_parsed ("(@a(suau) [], @a(suu) [])"));
        assert_snapshot_rejected (v, defs, LRG_PROGRESSION_ERROR_INVALID);
    }
    {
        g_autoptr(GVariant) v = g_variant_ref_sink (g_variant_new_parsed ("(@a(sua(u)) [], @a(sux) [])"));
        assert_snapshot_rejected (v, defs, LRG_PROGRESSION_ERROR_INVALID);
    }

    /* Unknown IDs: NOT_FOUND. */
    {
        ActiveSpec a = { "ghost", 0, 0, { 0 } };
        g_autoptr(GVariant) v = build_snapshot (&a, 1, NULL, 0);
        assert_snapshot_rejected (v, defs, LRG_PROGRESSION_ERROR_NOT_FOUND);
    }
    {
        DoneSpec d = { "ghost", 1, 5 };
        g_autoptr(GVariant) v = build_snapshot (NULL, 0, &d, 1);
        assert_snapshot_rejected (v, defs, LRG_PROGRESSION_ERROR_NOT_FOUND);
    }
    {
        /* A value that is not an LrgQuestDef counts as unknown. */
        g_autoptr(GHashTable) bad = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_object_unref);
        DoneSpec d = { "intro", 1, 5 };
        g_autoptr(GVariant) v = build_snapshot (NULL, 0, &d, 1);
        g_hash_table_insert (bad, (gpointer)"intro", lrg_reset_schedule_new (0, 2, 0));
        assert_snapshot_rejected (v, bad, LRG_PROGRESSION_ERROR_NOT_FOUND);
    }

    /* Bad IDs: empty, overlong. */
    {
        ActiveSpec a = { "", 0, 0, { 0 } };
        g_autoptr(GVariant) v = build_snapshot (&a, 1, NULL, 0);
        assert_snapshot_rejected (v, defs, LRG_PROGRESSION_ERROR_INVALID);
    }
    {
        DoneSpec d = { "", 1, 5 };
        g_autoptr(GVariant) v = build_snapshot (NULL, 0, &d, 1);
        assert_snapshot_rejected (v, defs, LRG_PROGRESSION_ERROR_INVALID);
    }
    {
        ActiveSpec a = { NULL, 0, 0, { 0 } };
        g_autoptr(GVariant) v = NULL;
        fixture_add (fixture, simple_def (long_id, 1, 1));
        a.id = long_id;
        v = build_snapshot (&a, 1, NULL, 0);
        assert_snapshot_rejected (v, defs, LRG_PROGRESSION_ERROR_INVALID);
    }
    {
        DoneSpec d = { NULL, 1, 5 };
        g_autoptr(GVariant) v = NULL;
        d.id = long_id;
        v = build_snapshot (NULL, 0, &d, 1);
        assert_snapshot_rejected (v, defs, LRG_PROGRESSION_ERROR_INVALID);
    }

    /* Duplicates in either array. */
    {
        ActiveSpec a[2] = { { "intro", 0, 0, { 0 } }, { "intro", 0, 0, { 0 } } };
        g_autoptr(GVariant) v = build_snapshot (a, 2, NULL, 0);
        assert_snapshot_rejected (v, defs, LRG_PROGRESSION_ERROR_INVALID);
    }
    {
        DoneSpec d[2] = { { "intro", 1, 5 }, { "intro", 2, 6 } };
        g_autoptr(GVariant) v = build_snapshot (NULL, 0, d, 2);
        assert_snapshot_rejected (v, defs, LRG_PROGRESSION_ERROR_INVALID);
    }

    /* Out-of-range stage and progress. */
    {
        ActiveSpec a = { "hunt", 3, 0, { 0 } };
        g_autoptr(GVariant) v = build_snapshot (&a, 1, NULL, 0);
        assert_snapshot_rejected (v, defs, LRG_PROGRESSION_ERROR_INVALID);
    }
    {
        ActiveSpec a = { "hunt", G_MAXUINT32, 0, { 0 } };
        g_autoptr(GVariant) v = build_snapshot (&a, 1, NULL, 0);
        assert_snapshot_rejected (v, defs, LRG_PROGRESSION_ERROR_INVALID);
    }
    {
        ActiveSpec a = { "hunt", 0, 2, { 4, 0 } };  /* wolves target is 3 */
        g_autoptr(GVariant) v = build_snapshot (&a, 1, NULL, 0);
        assert_snapshot_rejected (v, defs, LRG_PROGRESSION_ERROR_INVALID);
    }
    {
        ActiveSpec a = { "hunt", 0, 2, { 0, G_MAXUINT32 } };
        g_autoptr(GVariant) v = build_snapshot (&a, 1, NULL, 0);
        assert_snapshot_rejected (v, defs, LRG_PROGRESSION_ERROR_INVALID);
    }
    {
        ActiveSpec a = { "hunt", 0, 3, { 0, 0, 0 } };  /* only 2 objectives */
        g_autoptr(GVariant) v = build_snapshot (&a, 1, NULL, 0);
        assert_snapshot_rejected (v, defs, LRG_PROGRESSION_ERROR_INVALID);
    }
    {
        ActiveSpec a = { "hunt", 2, 1, { 0 } };  /* stage == count takes no values */
        g_autoptr(GVariant) v = build_snapshot (&a, 1, NULL, 0);
        assert_snapshot_rejected (v, defs, LRG_PROGRESSION_ERROR_INVALID);
    }
    {
        ActiveSpec a = { "hunt", 0, 17, { 0 } };  /* more than 16 values */
        g_autoptr(GVariant) v = build_snapshot (&a, 1, NULL, 0);
        assert_snapshot_rejected (v, defs, LRG_PROGRESSION_ERROR_INVALID);
    }

    /* Zero completion count. */
    {
        DoneSpec d = { "intro", 0, 5 };
        g_autoptr(GVariant) v = build_snapshot (NULL, 0, &d, 1);
        assert_snapshot_rejected (v, defs, LRG_PROGRESSION_ERROR_INVALID);
    }

    /* A single bad entry rejects a snapshot that is otherwise valid. */
    {
        ActiveSpec a[2] = { { "hunt", 0, 2, { 1, 1 } }, { "ghost", 0, 0, { 0 } } };
        DoneSpec   d = { "intro", 1, 5 };
        g_autoptr(GVariant) v = build_snapshot (a, 2, &d, 1);
        assert_snapshot_rejected (v, defs, LRG_PROGRESSION_ERROR_NOT_FOUND);
    }
    (void)user_data;
}

static void
test_log_variant_oversized (ProgressionFixture *fixture,
                            gconstpointer       user_data)
{
    GVariantBuilder builder;
    guint           i;

    /* 257 active entries (content is irrelevant: the bound is checked first). */
    {
        g_autoptr(GVariant) v = NULL;
        g_variant_builder_init (&builder, G_VARIANT_TYPE ("(a(suau)a(sux))"));
        g_variant_builder_open (&builder, G_VARIANT_TYPE ("a(suau)"));
        for (i = 0; i <= LRG_QUEST_LOG_MAX_ACTIVE; i++)
            g_variant_builder_add (&builder, "(su@au)", "intro", (guint32)0,
                                   g_variant_new_array (G_VARIANT_TYPE_UINT32, NULL, 0));
        g_variant_builder_close (&builder);
        g_variant_builder_open (&builder, G_VARIANT_TYPE ("a(sux)"));
        g_variant_builder_close (&builder);
        v = g_variant_ref_sink (g_variant_builder_end (&builder));
        assert_snapshot_rejected (v, fixture->defs, LRG_PROGRESSION_ERROR_INVALID);
    }

    /* 8193 completed entries. */
    {
        g_autoptr(GVariant) v = NULL;
        g_variant_builder_init (&builder, G_VARIANT_TYPE ("(a(suau)a(sux))"));
        g_variant_builder_open (&builder, G_VARIANT_TYPE ("a(suau)"));
        g_variant_builder_close (&builder);
        g_variant_builder_open (&builder, G_VARIANT_TYPE ("a(sux)"));
        for (i = 0; i <= LRG_QUEST_LOG_MAX_COMPLETED; i++)
            g_variant_builder_add (&builder, "(sux)", "intro", (guint32)1, (gint64)1);
        g_variant_builder_close (&builder);
        v = g_variant_ref_sink (g_variant_builder_end (&builder));
        assert_snapshot_rejected (v, fixture->defs, LRG_PROGRESSION_ERROR_INVALID);
    }
    (void)user_data;
}

static void
test_log_variant_non_normal (ProgressionFixture *fixture,
                             gconstpointer       user_data)
{
    DoneSpec             d = { "intro", 1, 5 };
    g_autoptr(GVariant)  valid = NULL;
    gsize                size;
    guint8              *data;
    gsize                i;
    gboolean             found;

    valid = build_snapshot (NULL, 0, &d, 1);
    size = g_variant_get_size (valid);

    /* Invalid UTF-8 inside the ID: patch the 'i' of "intro" to 0xff. */
    data = g_memdup2 (g_variant_get_data (valid), size);
    found = FALSE;
    for (i = 0; i + 5 <= size; i++)
    {
        if (memcmp (data + i, "intro", 5) == 0)
        {
            data[i] = 0xff;
            found = TRUE;
            break;
        }
    }
    g_assert_true (found);
    {
        g_autoptr(GBytes)   bytes = g_bytes_new_take (data, size);
        g_autoptr(GVariant) v = g_variant_ref_sink (
            g_variant_new_from_bytes (G_VARIANT_TYPE ("(a(suau)a(sux))"), bytes, FALSE));
        g_assert_false (g_variant_is_normal_form (v));
        assert_snapshot_rejected (v, fixture->defs, LRG_PROGRESSION_ERROR_INVALID);
    }

    /* Corrupted framing offsets: flip the last byte (an offset). */
    data = g_memdup2 (g_variant_get_data (valid), size);
    data[size - 1] ^= 0x7f;
    {
        g_autoptr(GBytes)   bytes = g_bytes_new_take (data, size);
        g_autoptr(GVariant) v = g_variant_ref_sink (
            g_variant_new_from_bytes (G_VARIANT_TYPE ("(a(suau)a(sux))"), bytes, FALSE));
        g_assert_false (g_variant_is_normal_form (v));
        assert_snapshot_rejected (v, fixture->defs, LRG_PROGRESSION_ERROR_INVALID);
    }
    (void)user_data;
}

/* ========================================================================== */
/*                              LrgQuestChain                                 */
/* ========================================================================== */

static void
test_chain_basics (void)
{
    g_autoptr(LrgQuestChain) chain = NULL;
    g_autofree gchar        *id = NULL;
    g_autofree gchar        *name = NULL;
    g_autofree gchar        *storyline = NULL;
    g_autofree gchar        *description = NULL;
    GPtrArray               *ids;
    guint                    notifications = 0;

    chain = lrg_quest_chain_new ("westfall", "Westfall Stew");
    g_assert_cmpstr (lrg_quest_chain_get_id (chain), ==, "westfall");
    g_assert_cmpstr (lrg_quest_chain_get_name (chain), ==, "Westfall Stew");
    g_assert_null (lrg_quest_chain_get_storyline (chain));
    g_assert_null (lrg_quest_chain_get_description (chain));
    g_assert_cmpuint (lrg_quest_chain_get_length (chain), ==, 0);

    g_signal_connect (chain, "notify", G_CALLBACK (count_notify_signal), &notifications);
    lrg_quest_chain_set_name (chain, "Stew");
    lrg_quest_chain_set_storyline (chain, "Defias");
    lrg_quest_chain_set_description (chain, "Feed the militia");
    lrg_quest_chain_set_description (chain, "Feed the militia");
    g_assert_cmpuint (notifications, ==, 3);
    g_object_get (chain, "id", &id, "name", &name, "storyline", &storyline,
                  "description", &description, NULL);
    g_assert_cmpstr (id, ==, "westfall");
    g_assert_cmpstr (name, ==, "Stew");
    g_assert_cmpstr (storyline, ==, "Defias");
    g_assert_cmpstr (description, ==, "Feed the militia");
    lrg_quest_chain_set_storyline (chain, NULL);
    g_assert_null (lrg_quest_chain_get_storyline (chain));

    g_assert_true (lrg_quest_chain_add_quest (chain, "a"));
    g_assert_true (lrg_quest_chain_add_quest (chain, "b"));
    g_assert_true (lrg_quest_chain_add_quest (chain, "c"));
    g_assert_false (lrg_quest_chain_add_quest (chain, "b"));
    g_assert_cmpuint (lrg_quest_chain_get_length (chain), ==, 3);
    ids = lrg_quest_chain_get_quest_ids (chain);
    g_assert_cmpstr (g_ptr_array_index (ids, 0), ==, "a");
    g_assert_cmpstr (g_ptr_array_index (ids, 2), ==, "c");
    g_assert_cmpint (lrg_quest_chain_index_of (chain, "a"), ==, 0);
    g_assert_cmpint (lrg_quest_chain_index_of (chain, "c"), ==, 2);
    g_assert_cmpint (lrg_quest_chain_index_of (chain, "z"), ==, -1);
}

static void
test_chain_progress (void)
{
    g_autoptr(LrgQuestChain) chain = NULL;
    g_autoptr(LrgQuestChain) empty = NULL;
    g_autoptr(LrgQuestLog)   log = NULL;

    chain = lrg_quest_chain_new ("c", NULL);
    lrg_quest_chain_add_quest (chain, "a");
    lrg_quest_chain_add_quest (chain, "b");
    lrg_quest_chain_add_quest (chain, "c");
    log = lrg_quest_log_new ();

    g_assert_cmpstr (lrg_quest_chain_get_next (chain, log), ==, "a");
    g_assert_cmpuint (lrg_quest_chain_get_progress (chain, log), ==, 0);
    g_assert_false (lrg_quest_chain_is_complete (chain, log));

    lrg_quest_log_mark_completed (log, "a", 1, 0);
    g_assert_cmpstr (lrg_quest_chain_get_next (chain, log), ==, "b");
    g_assert_cmpuint (lrg_quest_chain_get_progress (chain, log), ==, 1);

    /* Out-of-order completion: next is still the first gap. */
    lrg_quest_log_mark_completed (log, "c", 1, 0);
    g_assert_cmpstr (lrg_quest_chain_get_next (chain, log), ==, "b");
    g_assert_cmpuint (lrg_quest_chain_get_progress (chain, log), ==, 2);
    g_assert_false (lrg_quest_chain_is_complete (chain, log));

    lrg_quest_log_mark_completed (log, "b", 1, 0);
    g_assert_null (lrg_quest_chain_get_next (chain, log));
    g_assert_cmpuint (lrg_quest_chain_get_progress (chain, log), ==, 3);
    g_assert_true (lrg_quest_chain_is_complete (chain, log));

    /* An empty chain is complete. */
    empty = lrg_quest_chain_new ("e", "Empty");
    g_assert_null (lrg_quest_chain_get_next (empty, log));
    g_assert_true (lrg_quest_chain_is_complete (empty, log));
}

static void
test_chain_link_prerequisites (void)
{
    g_autoptr(LrgQuestChain) chain = NULL;
    g_autoptr(GHashTable)    defs = NULL;
    g_autoptr(GError)        error = NULL;
    g_autoptr(LrgQuestLog)   log = NULL;
    LrgQuestDef             *c;
    GPtrArray               *prereqs;

    defs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
    g_hash_table_insert (defs, g_strdup ("a"), simple_def ("a", 1, 1));
    g_hash_table_insert (defs, g_strdup ("b"), simple_def ("b", 1, 1));
    c = simple_def ("c", 1, 1);
    lrg_quest_def_add_prerequisite (c, "b");
    g_hash_table_insert (defs, g_strdup ("c"), c);

    chain = lrg_quest_chain_new ("chain", "Chain");
    lrg_quest_chain_add_quest (chain, "a");
    lrg_quest_chain_add_quest (chain, "b");
    lrg_quest_chain_add_quest (chain, "c");
    lrg_quest_chain_add_quest (chain, "missing");

    /* A missing definition rejects the whole link without modifying anything. */
    g_assert_false (lrg_quest_chain_link_prerequisites (chain, defs, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_NOT_FOUND);
    g_clear_error (&error);
    g_assert_cmpuint (lrg_quest_def_get_prerequisites (g_hash_table_lookup (defs, "b"))->len, ==, 0);
    g_assert_cmpuint (lrg_quest_def_get_prerequisites (c)->len, ==, 1);

    g_hash_table_insert (defs, g_strdup ("missing"), simple_def ("missing", 1, 1));
    g_assert_true (lrg_quest_chain_link_prerequisites (chain, defs, &error));
    g_assert_no_error (error);
    /* Idempotent: running it twice adds nothing. */
    g_assert_true (lrg_quest_chain_link_prerequisites (chain, defs, &error));

    g_assert_cmpuint (lrg_quest_def_get_prerequisites (g_hash_table_lookup (defs, "a"))->len, ==, 0);
    prereqs = lrg_quest_def_get_prerequisites (g_hash_table_lookup (defs, "b"));
    g_assert_cmpuint (prereqs->len, ==, 1);
    g_assert_cmpstr (g_ptr_array_index (prereqs, 0), ==, "a");
    g_assert_cmpuint (lrg_quest_def_get_prerequisites (c)->len, ==, 1);
    prereqs = lrg_quest_def_get_prerequisites (g_hash_table_lookup (defs, "missing"));
    g_assert_cmpuint (prereqs->len, ==, 1);
    g_assert_cmpstr (g_ptr_array_index (prereqs, 0), ==, "c");

    /* The linked chain now gates can_start in order. */
    log = lrg_quest_log_new ();
    g_assert_false (lrg_quest_log_can_start (log, g_hash_table_lookup (defs, "b"), 1, NULL, 0, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);
    lrg_quest_log_mark_completed (log, "a", 1, 0);
    g_assert_true (lrg_quest_log_can_start (log, g_hash_table_lookup (defs, "b"), 1, NULL, 0, &error));
    g_assert_cmpstr (lrg_quest_chain_get_next (chain, log), ==, "b");
}

static void
test_chain_programmer_errors (void)
{
    g_autoptr(LrgQuestChain) chain = NULL;
    LrgQuestChain           *bad;

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*id != NULL*");
    bad = lrg_quest_chain_new ("", NULL);
    g_test_assert_expected_messages ();
    g_assert_null (bad);

    chain = lrg_quest_chain_new ("c", NULL);
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*quest_id != NULL*");
    g_assert_false (lrg_quest_chain_add_quest (chain, ""));
    g_test_assert_expected_messages ();
    g_assert_cmpuint (lrg_quest_chain_get_length (chain), ==, 0);
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
    g_test_add ("/quest/log/abandoned-instance-detached", QuestLogFixture, NULL,
                log_fixture_set_up, test_log_abandoned_instance_detached, log_fixture_tear_down);
    g_test_add ("/quest/log/instance-outlives-log", QuestLogFixture, NULL,
                log_fixture_set_up, test_log_instance_outlives_log, log_fixture_tear_down);
    g_test_add ("/quest/log/track-foreign-instance", QuestLogFixture, NULL,
                log_fixture_set_up, test_log_track_foreign_instance, log_fixture_tear_down);
    g_test_add ("/quest/log/track-quest", QuestLogFixture, NULL,
                log_fixture_set_up, test_log_track_quest, log_fixture_tear_down);
    g_test_add ("/quest/log/tracked-cleared-on-complete", QuestLogFixture, NULL,
                log_fixture_set_up, test_log_tracked_cleared_on_complete, log_fixture_tear_down);
    g_test_add ("/quest/log/tracked-cleared-on-abandon", QuestLogFixture, NULL,
                log_fixture_set_up, test_log_tracked_cleared_on_abandon, log_fixture_tear_down);
    g_test_add ("/quest/log/signals", QuestLogFixture, NULL,
                log_fixture_set_up, test_log_signals, log_fixture_tear_down);

    /* LrgQuestDef MMO additions */
    g_test_add_func ("/quest/def/mmo-properties", test_def_mmo_properties);
    g_test_add_func ("/quest/def/exclusives", test_def_exclusives);
    g_test_add_func ("/quest/def/stage-objectives", test_def_stage_objectives);
    g_test_add_func ("/quest/def/check-prerequisites-log", test_def_check_prerequisites_log);

    /* LrgQuestInstance parallel objectives */
    g_test_add_func ("/quest/objective/increment-saturates", test_objective_increment_saturates);
    g_test_add_func ("/quest/instance/parallel-objectives", test_instance_parallel_objectives);
    g_test_add_func ("/quest/instance/update-matches-every-objective", test_instance_update_matches_every_objective);
    g_test_add_func ("/quest/instance/restore-helpers", test_instance_restore_helpers);
    g_test_add_func ("/quest/instance/complete-stage-not-stuck", test_instance_complete_stage_not_stuck);

    /* LrgQuestLog MMO additions */
#define ADD_PROGRESSION(path, func)     g_test_add (path, ProgressionFixture, NULL, progression_set_up, func, progression_tear_down)
    ADD_PROGRESSION ("/quest/log/can-start-happy", test_log_can_start_happy);
    ADD_PROGRESSION ("/quest/log/can-start-rejections", test_log_can_start_rejections);
    ADD_PROGRESSION ("/quest/log/prerequisite-satisfied", test_log_prerequisite_satisfied);
    ADD_PROGRESSION ("/quest/log/capacity-limit", test_log_capacity_limits);
    ADD_PROGRESSION ("/quest/log/record-limit", test_log_record_limit);
    ADD_PROGRESSION ("/quest/log/turn-in-flow", test_log_turn_in_flow);
    ADD_PROGRESSION ("/quest/log/turn-in-failed-quest", test_log_turn_in_failed_quest);
    ADD_PROGRESSION ("/quest/log/repeat-daily", test_log_repeat_daily);
    ADD_PROGRESSION ("/quest/log/repeat-weekly", test_log_repeat_weekly);
    ADD_PROGRESSION ("/quest/log/repeat-negative-time", test_log_repeat_negative_time);
    ADD_PROGRESSION ("/quest/log/count-completed-in-period", test_log_count_completed_in_period);
    ADD_PROGRESSION ("/quest/log/mark-completed", test_log_mark_completed);
    ADD_PROGRESSION ("/quest/log/legacy-completion-record", test_log_legacy_completion_record);
    ADD_PROGRESSION ("/quest/log/abandon-checked", test_log_abandon_checked);
    ADD_PROGRESSION ("/quest/log/variant/round-trip", test_log_variant_round_trip);
    ADD_PROGRESSION ("/quest/log/variant/empty-and-short-progress", test_log_variant_empty_and_short_progress);
    ADD_PROGRESSION ("/quest/log/variant/boundaries-accepted", test_log_variant_boundaries_accepted);
    ADD_PROGRESSION ("/quest/log/variant/hostile", test_log_variant_hostile);
    ADD_PROGRESSION ("/quest/log/variant/oversized", test_log_variant_oversized);
    ADD_PROGRESSION ("/quest/log/variant/non-normal", test_log_variant_non_normal);
#undef ADD_PROGRESSION

    /* LrgQuestChain */
    g_test_add_func ("/quest/chain/basics", test_chain_basics);
    g_test_add_func ("/quest/chain/progress", test_chain_progress);
    g_test_add_func ("/quest/chain/link-prerequisites", test_chain_link_prerequisites);
    g_test_add_func ("/quest/chain/programmer-errors", test_chain_programmer_errors);

    return g_test_run ();
}
