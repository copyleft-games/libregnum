/* test-ai.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the AI module (blackboard, behavior trees).
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgBlackboard *blackboard;
} BlackboardFixture;

typedef struct
{
    LrgBlackboard *blackboard;
} BTNodeFixture;

typedef struct
{
    LrgBehaviorTree *tree;
} BehaviorTreeFixture;

static void
blackboard_fixture_set_up (BlackboardFixture *fixture,
                           gconstpointer      user_data)
{
    (void)user_data;
    fixture->blackboard = lrg_blackboard_new ();
    g_assert_nonnull (fixture->blackboard);
}

static void
blackboard_fixture_tear_down (BlackboardFixture *fixture,
                              gconstpointer      user_data)
{
    (void)user_data;
    g_clear_object (&fixture->blackboard);
}

static void
bt_node_fixture_set_up (BTNodeFixture *fixture,
                        gconstpointer  user_data)
{
    (void)user_data;
    fixture->blackboard = lrg_blackboard_new ();
    g_assert_nonnull (fixture->blackboard);
}

static void
bt_node_fixture_tear_down (BTNodeFixture *fixture,
                           gconstpointer  user_data)
{
    (void)user_data;
    g_clear_object (&fixture->blackboard);
}

static void
behavior_tree_fixture_set_up (BehaviorTreeFixture *fixture,
                              gconstpointer        user_data)
{
    (void)user_data;
    fixture->tree = lrg_behavior_tree_new ();
    g_assert_nonnull (fixture->tree);
}

static void
behavior_tree_fixture_tear_down (BehaviorTreeFixture *fixture,
                                 gconstpointer        user_data)
{
    (void)user_data;
    g_clear_object (&fixture->tree);
}

/* ==========================================================================
 * Helper action/condition functions
 * ========================================================================== */

static gint g_action_call_count = 0;
static gint g_condition_call_count = 0;

static LrgBTStatus
action_success (LrgBlackboard *blackboard,
                gfloat         delta_time,
                gpointer       user_data)
{
    (void)blackboard;
    (void)delta_time;
    (void)user_data;
    g_action_call_count++;
    return LRG_BT_STATUS_SUCCESS;
}

static LrgBTStatus
action_failure (LrgBlackboard *blackboard,
                gfloat         delta_time,
                gpointer       user_data)
{
    (void)blackboard;
    (void)delta_time;
    (void)user_data;
    g_action_call_count++;
    return LRG_BT_STATUS_FAILURE;
}

static LrgBTStatus
action_running (LrgBlackboard *blackboard,
                gfloat         delta_time,
                gpointer       user_data)
{
    (void)blackboard;
    (void)delta_time;
    (void)user_data;
    g_action_call_count++;
    return LRG_BT_STATUS_RUNNING;
}

static LrgBTStatus
action_increment_counter (LrgBlackboard *blackboard,
                          gfloat         delta_time,
                          gpointer       user_data)
{
    gint counter;

    (void)delta_time;
    (void)user_data;

    counter = lrg_blackboard_get_int (blackboard, "counter", 0);
    lrg_blackboard_set_int (blackboard, "counter", counter + 1);
    g_action_call_count++;

    return LRG_BT_STATUS_SUCCESS;
}

static gboolean
condition_true (LrgBlackboard *blackboard,
                gpointer       user_data)
{
    (void)blackboard;
    (void)user_data;
    g_condition_call_count++;
    return TRUE;
}

static gboolean
condition_false (LrgBlackboard *blackboard,
                 gpointer       user_data)
{
    (void)blackboard;
    (void)user_data;
    g_condition_call_count++;
    return FALSE;
}

static gboolean
condition_check_counter (LrgBlackboard *blackboard,
                         gpointer       user_data)
{
    gint threshold;
    gint counter;

    threshold = GPOINTER_TO_INT (user_data);
    counter = lrg_blackboard_get_int (blackboard, "counter", 0);
    g_condition_call_count++;

    return (counter >= threshold);
}

/* ==========================================================================
 * Blackboard Tests
 * ========================================================================== */

static void
test_blackboard_new (BlackboardFixture *fixture,
                     gconstpointer      user_data)
{
    (void)user_data;

    g_assert_true (LRG_IS_BLACKBOARD (fixture->blackboard));
}

static void
test_blackboard_int (BlackboardFixture *fixture,
                     gconstpointer      user_data)
{
    gint value;

    (void)user_data;

    /* Default value when key doesn't exist */
    value = lrg_blackboard_get_int (fixture->blackboard, "health", -1);
    g_assert_cmpint (value, ==, -1);

    /* Set and get */
    lrg_blackboard_set_int (fixture->blackboard, "health", 100);
    value = lrg_blackboard_get_int (fixture->blackboard, "health", -1);
    g_assert_cmpint (value, ==, 100);

    /* Overwrite */
    lrg_blackboard_set_int (fixture->blackboard, "health", 50);
    value = lrg_blackboard_get_int (fixture->blackboard, "health", -1);
    g_assert_cmpint (value, ==, 50);
}

static void
test_blackboard_float (BlackboardFixture *fixture,
                       gconstpointer      user_data)
{
    gfloat value;

    (void)user_data;

    /* Default value when key doesn't exist */
    value = lrg_blackboard_get_float (fixture->blackboard, "speed", -1.0f);
    g_assert_cmpfloat_with_epsilon (value, -1.0f, 0.001f);

    /* Set and get */
    lrg_blackboard_set_float (fixture->blackboard, "speed", 5.5f);
    value = lrg_blackboard_get_float (fixture->blackboard, "speed", -1.0f);
    g_assert_cmpfloat_with_epsilon (value, 5.5f, 0.001f);
}

static void
test_blackboard_bool (BlackboardFixture *fixture,
                      gconstpointer      user_data)
{
    gboolean value;

    (void)user_data;

    /* Default value when key doesn't exist */
    value = lrg_blackboard_get_bool (fixture->blackboard, "visible", FALSE);
    g_assert_false (value);

    /* Set and get */
    lrg_blackboard_set_bool (fixture->blackboard, "visible", TRUE);
    value = lrg_blackboard_get_bool (fixture->blackboard, "visible", FALSE);
    g_assert_true (value);
}

static void
test_blackboard_string (BlackboardFixture *fixture,
                        gconstpointer      user_data)
{
    const gchar *value;

    (void)user_data;

    /* Default value when key doesn't exist */
    value = lrg_blackboard_get_string (fixture->blackboard, "target");
    g_assert_null (value);

    /* Set and get */
    lrg_blackboard_set_string (fixture->blackboard, "target", "enemy1");
    value = lrg_blackboard_get_string (fixture->blackboard, "target");
    g_assert_cmpstr (value, ==, "enemy1");

    /* Set to NULL */
    lrg_blackboard_set_string (fixture->blackboard, "target", NULL);
    value = lrg_blackboard_get_string (fixture->blackboard, "target");
    g_assert_null (value);
}

static void
test_blackboard_object (BlackboardFixture *fixture,
                        gconstpointer      user_data)
{
    g_autoptr(LrgBlackboard) other = NULL;
    GObject *value;

    (void)user_data;

    other = lrg_blackboard_new ();

    /* Default value when key doesn't exist */
    value = lrg_blackboard_get_object (fixture->blackboard, "data");
    g_assert_null (value);

    /* Set and get */
    lrg_blackboard_set_object (fixture->blackboard, "data", G_OBJECT (other));
    value = lrg_blackboard_get_object (fixture->blackboard, "data");
    g_assert_true (value == G_OBJECT (other));
}

static void
test_blackboard_has_key (BlackboardFixture *fixture,
                         gconstpointer      user_data)
{
    (void)user_data;

    g_assert_false (lrg_blackboard_has_key (fixture->blackboard, "test"));

    lrg_blackboard_set_int (fixture->blackboard, "test", 42);
    g_assert_true (lrg_blackboard_has_key (fixture->blackboard, "test"));
}

static void
test_blackboard_remove (BlackboardFixture *fixture,
                        gconstpointer      user_data)
{
    gboolean removed;

    (void)user_data;

    /* Remove non-existent key */
    removed = lrg_blackboard_remove (fixture->blackboard, "test");
    g_assert_false (removed);

    /* Remove existing key */
    lrg_blackboard_set_int (fixture->blackboard, "test", 42);
    removed = lrg_blackboard_remove (fixture->blackboard, "test");
    g_assert_true (removed);
    g_assert_false (lrg_blackboard_has_key (fixture->blackboard, "test"));
}

static void
test_blackboard_clear (BlackboardFixture *fixture,
                       gconstpointer      user_data)
{
    (void)user_data;

    lrg_blackboard_set_int (fixture->blackboard, "a", 1);
    lrg_blackboard_set_int (fixture->blackboard, "b", 2);
    lrg_blackboard_set_int (fixture->blackboard, "c", 3);

    g_assert_true (lrg_blackboard_has_key (fixture->blackboard, "a"));
    g_assert_true (lrg_blackboard_has_key (fixture->blackboard, "b"));
    g_assert_true (lrg_blackboard_has_key (fixture->blackboard, "c"));

    lrg_blackboard_clear (fixture->blackboard);

    g_assert_false (lrg_blackboard_has_key (fixture->blackboard, "a"));
    g_assert_false (lrg_blackboard_has_key (fixture->blackboard, "b"));
    g_assert_false (lrg_blackboard_has_key (fixture->blackboard, "c"));
}

static void
test_blackboard_get_keys (BlackboardFixture *fixture,
                          gconstpointer      user_data)
{
    GList *keys;

    (void)user_data;

    /* Empty blackboard */
    keys = lrg_blackboard_get_keys (fixture->blackboard);
    g_assert_cmpuint (g_list_length (keys), ==, 0);
    g_list_free (keys);

    /* With keys */
    lrg_blackboard_set_int (fixture->blackboard, "a", 1);
    lrg_blackboard_set_int (fixture->blackboard, "b", 2);

    keys = lrg_blackboard_get_keys (fixture->blackboard);
    g_assert_cmpuint (g_list_length (keys), ==, 2);
    g_list_free (keys);
}

/* ==========================================================================
 * BT Action Node Tests
 * ========================================================================== */

static void
test_bt_action_success (BTNodeFixture *fixture,
                        gconstpointer  user_data)
{
    g_autoptr(LrgBTAction) action = NULL;
    LrgBTStatus status;

    (void)user_data;

    g_action_call_count = 0;
    action = lrg_bt_action_new_simple (action_success);
    g_assert_nonnull (action);

    status = lrg_bt_node_tick (LRG_BT_NODE (action), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_SUCCESS);
    g_assert_cmpint (g_action_call_count, ==, 1);
}

static void
test_bt_action_failure (BTNodeFixture *fixture,
                        gconstpointer  user_data)
{
    g_autoptr(LrgBTAction) action = NULL;
    LrgBTStatus status;

    (void)user_data;

    g_action_call_count = 0;
    action = lrg_bt_action_new_simple (action_failure);

    status = lrg_bt_node_tick (LRG_BT_NODE (action), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_FAILURE);
    g_assert_cmpint (g_action_call_count, ==, 1);
}

static void
test_bt_action_running (BTNodeFixture *fixture,
                        gconstpointer  user_data)
{
    g_autoptr(LrgBTAction) action = NULL;
    LrgBTStatus status;

    (void)user_data;

    g_action_call_count = 0;
    action = lrg_bt_action_new_simple (action_running);

    status = lrg_bt_node_tick (LRG_BT_NODE (action), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_RUNNING);
    g_assert_true (lrg_bt_node_is_running (LRG_BT_NODE (action)));
}

static void
test_bt_action_with_blackboard (BTNodeFixture *fixture,
                                gconstpointer  user_data)
{
    g_autoptr(LrgBTAction) action = NULL;
    LrgBTStatus status;
    gint counter;

    (void)user_data;

    g_action_call_count = 0;
    lrg_blackboard_set_int (fixture->blackboard, "counter", 0);
    action = lrg_bt_action_new_simple (action_increment_counter);

    /* First tick */
    status = lrg_bt_node_tick (LRG_BT_NODE (action), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_SUCCESS);
    counter = lrg_blackboard_get_int (fixture->blackboard, "counter", -1);
    g_assert_cmpint (counter, ==, 1);

    /* Second tick */
    lrg_bt_node_reset (LRG_BT_NODE (action));
    status = lrg_bt_node_tick (LRG_BT_NODE (action), fixture->blackboard, 0.016f);
    counter = lrg_blackboard_get_int (fixture->blackboard, "counter", -1);
    g_assert_cmpint (counter, ==, 2);
}

/* ==========================================================================
 * BT Condition Node Tests
 * ========================================================================== */

static void
test_bt_condition_true (BTNodeFixture *fixture,
                        gconstpointer  user_data)
{
    g_autoptr(LrgBTCondition) condition = NULL;
    LrgBTStatus status;

    (void)user_data;

    g_condition_call_count = 0;
    condition = lrg_bt_condition_new_simple (condition_true);

    status = lrg_bt_node_tick (LRG_BT_NODE (condition), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_SUCCESS);
    g_assert_cmpint (g_condition_call_count, ==, 1);
}

static void
test_bt_condition_false (BTNodeFixture *fixture,
                         gconstpointer  user_data)
{
    g_autoptr(LrgBTCondition) condition = NULL;
    LrgBTStatus status;

    (void)user_data;

    g_condition_call_count = 0;
    condition = lrg_bt_condition_new_simple (condition_false);

    status = lrg_bt_node_tick (LRG_BT_NODE (condition), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_FAILURE);
    g_assert_cmpint (g_condition_call_count, ==, 1);
}

static void
test_bt_condition_with_user_data (BTNodeFixture *fixture,
                                  gconstpointer  user_data)
{
    g_autoptr(LrgBTCondition) condition = NULL;
    LrgBTStatus status;

    (void)user_data;

    lrg_blackboard_set_int (fixture->blackboard, "counter", 5);
    condition = lrg_bt_condition_new (condition_check_counter,
                                      GINT_TO_POINTER (3),  /* threshold = 3 */
                                      NULL);

    status = lrg_bt_node_tick (LRG_BT_NODE (condition), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_SUCCESS);  /* 5 >= 3 */
}

/* ==========================================================================
 * BT Wait Node Tests
 * ========================================================================== */

static void
test_bt_wait_duration (BTNodeFixture *fixture,
                       gconstpointer  user_data)
{
    g_autoptr(LrgBTWait) wait = NULL;
    LrgBTStatus status;

    (void)user_data;

    wait = lrg_bt_wait_new (1.0f);  /* 1 second */
    g_assert_cmpfloat_with_epsilon (lrg_bt_wait_get_duration (wait), 1.0f, 0.001f);

    /* First tick - should still be running */
    status = lrg_bt_node_tick (LRG_BT_NODE (wait), fixture->blackboard, 0.5f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_RUNNING);

    /* Second tick - should complete */
    status = lrg_bt_node_tick (LRG_BT_NODE (wait), fixture->blackboard, 0.6f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_SUCCESS);
}

/* ==========================================================================
 * BT Sequence Node Tests
 * ========================================================================== */

static void
test_bt_sequence_all_success (BTNodeFixture *fixture,
                              gconstpointer  user_data)
{
    g_autoptr(LrgBTSequence) sequence = NULL;
    g_autoptr(LrgBTAction) action1 = NULL;
    g_autoptr(LrgBTAction) action2 = NULL;
    g_autoptr(LrgBTAction) action3 = NULL;
    LrgBTStatus status;

    (void)user_data;

    g_action_call_count = 0;
    sequence = lrg_bt_sequence_new ();
    action1 = lrg_bt_action_new_simple (action_success);
    action2 = lrg_bt_action_new_simple (action_success);
    action3 = lrg_bt_action_new_simple (action_success);

    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (sequence), LRG_BT_NODE (action1));
    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (sequence), LRG_BT_NODE (action2));
    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (sequence), LRG_BT_NODE (action3));

    g_assert_cmpuint (lrg_bt_composite_get_child_count (LRG_BT_COMPOSITE (sequence)), ==, 3);

    status = lrg_bt_node_tick (LRG_BT_NODE (sequence), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_SUCCESS);
    g_assert_cmpint (g_action_call_count, ==, 3);  /* All actions ran */
}

static void
test_bt_sequence_fails_on_first_failure (BTNodeFixture *fixture,
                                         gconstpointer  user_data)
{
    g_autoptr(LrgBTSequence) sequence = NULL;
    g_autoptr(LrgBTAction) action1 = NULL;
    g_autoptr(LrgBTAction) action2 = NULL;
    g_autoptr(LrgBTAction) action3 = NULL;
    LrgBTStatus status;

    (void)user_data;

    g_action_call_count = 0;
    sequence = lrg_bt_sequence_new ();
    action1 = lrg_bt_action_new_simple (action_success);
    action2 = lrg_bt_action_new_simple (action_failure);  /* Fails here */
    action3 = lrg_bt_action_new_simple (action_success);

    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (sequence), LRG_BT_NODE (action1));
    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (sequence), LRG_BT_NODE (action2));
    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (sequence), LRG_BT_NODE (action3));

    status = lrg_bt_node_tick (LRG_BT_NODE (sequence), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_FAILURE);
    g_assert_cmpint (g_action_call_count, ==, 2);  /* Only first two ran */
}

static void
test_bt_sequence_running (BTNodeFixture *fixture,
                          gconstpointer  user_data)
{
    g_autoptr(LrgBTSequence) sequence = NULL;
    g_autoptr(LrgBTAction) action1 = NULL;
    g_autoptr(LrgBTAction) action2 = NULL;
    LrgBTStatus status;

    (void)user_data;

    g_action_call_count = 0;
    sequence = lrg_bt_sequence_new ();
    action1 = lrg_bt_action_new_simple (action_success);
    action2 = lrg_bt_action_new_simple (action_running);

    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (sequence), LRG_BT_NODE (action1));
    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (sequence), LRG_BT_NODE (action2));

    status = lrg_bt_node_tick (LRG_BT_NODE (sequence), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_RUNNING);
}

/* ==========================================================================
 * BT Selector Node Tests
 * ========================================================================== */

static void
test_bt_selector_first_success (BTNodeFixture *fixture,
                                gconstpointer  user_data)
{
    g_autoptr(LrgBTSelector) selector = NULL;
    g_autoptr(LrgBTAction) action1 = NULL;
    g_autoptr(LrgBTAction) action2 = NULL;
    LrgBTStatus status;

    (void)user_data;

    g_action_call_count = 0;
    selector = lrg_bt_selector_new ();
    action1 = lrg_bt_action_new_simple (action_success);
    action2 = lrg_bt_action_new_simple (action_success);

    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (selector), LRG_BT_NODE (action1));
    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (selector), LRG_BT_NODE (action2));

    status = lrg_bt_node_tick (LRG_BT_NODE (selector), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_SUCCESS);
    g_assert_cmpint (g_action_call_count, ==, 1);  /* Only first ran */
}

static void
test_bt_selector_all_fail (BTNodeFixture *fixture,
                           gconstpointer  user_data)
{
    g_autoptr(LrgBTSelector) selector = NULL;
    g_autoptr(LrgBTAction) action1 = NULL;
    g_autoptr(LrgBTAction) action2 = NULL;
    g_autoptr(LrgBTAction) action3 = NULL;
    LrgBTStatus status;

    (void)user_data;

    g_action_call_count = 0;
    selector = lrg_bt_selector_new ();
    action1 = lrg_bt_action_new_simple (action_failure);
    action2 = lrg_bt_action_new_simple (action_failure);
    action3 = lrg_bt_action_new_simple (action_failure);

    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (selector), LRG_BT_NODE (action1));
    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (selector), LRG_BT_NODE (action2));
    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (selector), LRG_BT_NODE (action3));

    status = lrg_bt_node_tick (LRG_BT_NODE (selector), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_FAILURE);
    g_assert_cmpint (g_action_call_count, ==, 3);  /* All ran */
}

static void
test_bt_selector_fallback (BTNodeFixture *fixture,
                           gconstpointer  user_data)
{
    g_autoptr(LrgBTSelector) selector = NULL;
    g_autoptr(LrgBTAction) action1 = NULL;
    g_autoptr(LrgBTAction) action2 = NULL;
    g_autoptr(LrgBTAction) action3 = NULL;
    LrgBTStatus status;

    (void)user_data;

    g_action_call_count = 0;
    selector = lrg_bt_selector_new ();
    action1 = lrg_bt_action_new_simple (action_failure);
    action2 = lrg_bt_action_new_simple (action_failure);
    action3 = lrg_bt_action_new_simple (action_success);  /* Fallback succeeds */

    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (selector), LRG_BT_NODE (action1));
    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (selector), LRG_BT_NODE (action2));
    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (selector), LRG_BT_NODE (action3));

    status = lrg_bt_node_tick (LRG_BT_NODE (selector), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_SUCCESS);
    g_assert_cmpint (g_action_call_count, ==, 3);
}

/* ==========================================================================
 * BT Parallel Node Tests
 * ========================================================================== */

static void
test_bt_parallel_require_one (BTNodeFixture *fixture,
                              gconstpointer  user_data)
{
    g_autoptr(LrgBTParallel) parallel = NULL;
    g_autoptr(LrgBTAction) action1 = NULL;
    g_autoptr(LrgBTAction) action2 = NULL;
    LrgBTStatus status;

    (void)user_data;

    g_action_call_count = 0;
    parallel = lrg_bt_parallel_new (LRG_BT_PARALLEL_REQUIRE_ONE);
    action1 = lrg_bt_action_new_simple (action_failure);
    action2 = lrg_bt_action_new_simple (action_success);

    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (parallel), LRG_BT_NODE (action1));
    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (parallel), LRG_BT_NODE (action2));

    status = lrg_bt_node_tick (LRG_BT_NODE (parallel), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_SUCCESS);  /* One succeeded */
    g_assert_cmpint (g_action_call_count, ==, 2);  /* Both ran */
}

static void
test_bt_parallel_require_all (BTNodeFixture *fixture,
                              gconstpointer  user_data)
{
    g_autoptr(LrgBTParallel) parallel = NULL;
    g_autoptr(LrgBTAction) action1 = NULL;
    g_autoptr(LrgBTAction) action2 = NULL;
    LrgBTStatus status;

    (void)user_data;

    g_action_call_count = 0;
    parallel = lrg_bt_parallel_new (LRG_BT_PARALLEL_REQUIRE_ALL);
    action1 = lrg_bt_action_new_simple (action_failure);
    action2 = lrg_bt_action_new_simple (action_success);

    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (parallel), LRG_BT_NODE (action1));
    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (parallel), LRG_BT_NODE (action2));

    status = lrg_bt_node_tick (LRG_BT_NODE (parallel), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_FAILURE);  /* One failed */
    g_assert_cmpint (g_action_call_count, ==, 2);
}

/* ==========================================================================
 * BT Decorator Tests
 * ========================================================================== */

static void
test_bt_inverter_success_to_failure (BTNodeFixture *fixture,
                                     gconstpointer  user_data)
{
    g_autoptr(LrgBTAction) action = NULL;
    g_autoptr(LrgBTInverter) inverter = NULL;
    LrgBTStatus status;

    (void)user_data;

    action = lrg_bt_action_new_simple (action_success);
    inverter = lrg_bt_inverter_new (LRG_BT_NODE (action));

    status = lrg_bt_node_tick (LRG_BT_NODE (inverter), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_FAILURE);
}

static void
test_bt_inverter_failure_to_success (BTNodeFixture *fixture,
                                     gconstpointer  user_data)
{
    g_autoptr(LrgBTAction) action = NULL;
    g_autoptr(LrgBTInverter) inverter = NULL;
    LrgBTStatus status;

    (void)user_data;

    action = lrg_bt_action_new_simple (action_failure);
    inverter = lrg_bt_inverter_new (LRG_BT_NODE (action));

    status = lrg_bt_node_tick (LRG_BT_NODE (inverter), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_SUCCESS);
}

static void
test_bt_inverter_running_unchanged (BTNodeFixture *fixture,
                                    gconstpointer  user_data)
{
    g_autoptr(LrgBTAction) action = NULL;
    g_autoptr(LrgBTInverter) inverter = NULL;
    LrgBTStatus status;

    (void)user_data;

    action = lrg_bt_action_new_simple (action_running);
    inverter = lrg_bt_inverter_new (LRG_BT_NODE (action));

    status = lrg_bt_node_tick (LRG_BT_NODE (inverter), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_RUNNING);
}

static void
test_bt_succeeder (BTNodeFixture *fixture,
                   gconstpointer  user_data)
{
    g_autoptr(LrgBTAction) action = NULL;
    g_autoptr(LrgBTSucceeder) succeeder = NULL;
    LrgBTStatus status;

    (void)user_data;

    action = lrg_bt_action_new_simple (action_failure);
    succeeder = lrg_bt_succeeder_new (LRG_BT_NODE (action));

    status = lrg_bt_node_tick (LRG_BT_NODE (succeeder), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_SUCCESS);
}

static void
test_bt_failer (BTNodeFixture *fixture,
                gconstpointer  user_data)
{
    g_autoptr(LrgBTAction) action = NULL;
    g_autoptr(LrgBTFailer) failer = NULL;
    LrgBTStatus status;

    (void)user_data;

    action = lrg_bt_action_new_simple (action_success);
    failer = lrg_bt_failer_new (LRG_BT_NODE (action));

    status = lrg_bt_node_tick (LRG_BT_NODE (failer), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_FAILURE);
}

static void
test_bt_repeater_finite (BTNodeFixture *fixture,
                         gconstpointer  user_data)
{
    g_autoptr(LrgBTAction) action = NULL;
    g_autoptr(LrgBTRepeater) repeater = NULL;
    LrgBTStatus status;

    (void)user_data;

    g_action_call_count = 0;
    action = lrg_bt_action_new_simple (action_success);
    repeater = lrg_bt_repeater_new (LRG_BT_NODE (action), 3);

    g_assert_cmpuint (lrg_bt_repeater_get_count (repeater), ==, 3);

    /* Need to tick multiple times for repeater */
    status = lrg_bt_node_tick (LRG_BT_NODE (repeater), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_RUNNING);

    status = lrg_bt_node_tick (LRG_BT_NODE (repeater), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_RUNNING);

    status = lrg_bt_node_tick (LRG_BT_NODE (repeater), fixture->blackboard, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_SUCCESS);

    g_assert_cmpint (g_action_call_count, ==, 3);
}

/* ==========================================================================
 * Behavior Tree Tests
 * ========================================================================== */

static void
test_behavior_tree_new (BehaviorTreeFixture *fixture,
                        gconstpointer        user_data)
{
    (void)user_data;

    g_assert_true (LRG_IS_BEHAVIOR_TREE (fixture->tree));
    g_assert_null (lrg_behavior_tree_get_root (fixture->tree));
    g_assert_nonnull (lrg_behavior_tree_get_blackboard (fixture->tree));
}

static void
test_behavior_tree_set_root (BehaviorTreeFixture *fixture,
                             gconstpointer        user_data)
{
    g_autoptr(LrgBTAction) action = NULL;
    LrgBTNode *root;

    (void)user_data;

    action = lrg_bt_action_new_simple (action_success);
    lrg_behavior_tree_set_root (fixture->tree, LRG_BT_NODE (action));

    root = lrg_behavior_tree_get_root (fixture->tree);
    g_assert_true (root == LRG_BT_NODE (action));
}

static void
test_behavior_tree_tick (BehaviorTreeFixture *fixture,
                         gconstpointer        user_data)
{
    g_autoptr(LrgBTAction) action = NULL;
    LrgBTStatus status;

    (void)user_data;

    g_action_call_count = 0;
    action = lrg_bt_action_new_simple (action_success);
    lrg_behavior_tree_set_root (fixture->tree, LRG_BT_NODE (action));

    status = lrg_behavior_tree_tick (fixture->tree, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_SUCCESS);
    g_assert_cmpint (g_action_call_count, ==, 1);
}

static void
test_behavior_tree_blackboard (BehaviorTreeFixture *fixture,
                               gconstpointer        user_data)
{
    g_autoptr(LrgBTAction) action = NULL;
    LrgBlackboard *bb;
    gint counter;

    (void)user_data;

    bb = lrg_behavior_tree_get_blackboard (fixture->tree);
    g_assert_nonnull (bb);

    lrg_blackboard_set_int (bb, "counter", 0);

    action = lrg_bt_action_new_simple (action_increment_counter);
    lrg_behavior_tree_set_root (fixture->tree, LRG_BT_NODE (action));

    lrg_behavior_tree_tick (fixture->tree, 0.016f);
    counter = lrg_blackboard_get_int (bb, "counter", -1);
    g_assert_cmpint (counter, ==, 1);
}

static void
test_behavior_tree_reset (BehaviorTreeFixture *fixture,
                          gconstpointer        user_data)
{
    g_autoptr(LrgBTAction) action = NULL;
    LrgBTStatus status;

    (void)user_data;

    action = lrg_bt_action_new_simple (action_running);
    lrg_behavior_tree_set_root (fixture->tree, LRG_BT_NODE (action));

    status = lrg_behavior_tree_tick (fixture->tree, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_RUNNING);
    g_assert_true (lrg_behavior_tree_is_running (fixture->tree));

    lrg_behavior_tree_reset (fixture->tree);
    g_assert_false (lrg_behavior_tree_is_running (fixture->tree));
}

static void
test_behavior_tree_complex (BehaviorTreeFixture *fixture,
                            gconstpointer        user_data)
{
    /*
     * Build a simple AI tree:
     * Selector
     *   ├── Sequence (try attack)
     *   │     ├── Condition (enemy in range?)
     *   │     └── Action (attack)
     *   └── Action (wander)
     */
    g_autoptr(LrgBTSelector) root = NULL;
    g_autoptr(LrgBTSequence) attack_seq = NULL;
    g_autoptr(LrgBTCondition) in_range = NULL;
    g_autoptr(LrgBTAction) attack = NULL;
    g_autoptr(LrgBTAction) wander = NULL;
    LrgBlackboard *bb;
    LrgBTStatus status;

    (void)user_data;

    g_action_call_count = 0;
    g_condition_call_count = 0;

    /* Build tree */
    root = lrg_bt_selector_new ();
    attack_seq = lrg_bt_sequence_new ();
    in_range = lrg_bt_condition_new (condition_check_counter,
                                     GINT_TO_POINTER (5),  /* Need counter >= 5 */
                                     NULL);
    attack = lrg_bt_action_new_simple (action_success);
    wander = lrg_bt_action_new_simple (action_increment_counter);

    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (attack_seq), LRG_BT_NODE (in_range));
    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (attack_seq), LRG_BT_NODE (attack));

    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (root), LRG_BT_NODE (attack_seq));
    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (root), LRG_BT_NODE (wander));

    lrg_behavior_tree_set_root (fixture->tree, LRG_BT_NODE (root));

    /* Initialize blackboard */
    bb = lrg_behavior_tree_get_blackboard (fixture->tree);
    lrg_blackboard_set_int (bb, "counter", 0);

    /* Tick 1-5: counter < 5, so condition fails, wander runs */
    status = lrg_behavior_tree_tick (fixture->tree, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_SUCCESS);
    g_assert_cmpint (lrg_blackboard_get_int (bb, "counter", -1), ==, 1);

    lrg_behavior_tree_reset (fixture->tree);
    lrg_behavior_tree_tick (fixture->tree, 0.016f);
    g_assert_cmpint (lrg_blackboard_get_int (bb, "counter", -1), ==, 2);

    lrg_behavior_tree_reset (fixture->tree);
    lrg_behavior_tree_tick (fixture->tree, 0.016f);
    g_assert_cmpint (lrg_blackboard_get_int (bb, "counter", -1), ==, 3);

    lrg_behavior_tree_reset (fixture->tree);
    lrg_behavior_tree_tick (fixture->tree, 0.016f);
    g_assert_cmpint (lrg_blackboard_get_int (bb, "counter", -1), ==, 4);

    lrg_behavior_tree_reset (fixture->tree);
    lrg_behavior_tree_tick (fixture->tree, 0.016f);
    g_assert_cmpint (lrg_blackboard_get_int (bb, "counter", -1), ==, 5);

    /* Tick 6: counter >= 5, condition passes, attack runs (no wander) */
    g_action_call_count = 0;
    lrg_behavior_tree_reset (fixture->tree);
    status = lrg_behavior_tree_tick (fixture->tree, 0.016f);
    g_assert_cmpint (status, ==, LRG_BT_STATUS_SUCCESS);
    /* Counter should still be 5 (attack doesn't increment) */
    g_assert_cmpint (lrg_blackboard_get_int (bb, "counter", -1), ==, 5);
}

/* ==========================================================================
 * BT Node Properties Tests
 * ========================================================================== */

static void
test_bt_node_name (BTNodeFixture *fixture,
                   gconstpointer  user_data)
{
    g_autoptr(LrgBTAction) action = NULL;
    const gchar *name;

    (void)fixture;
    (void)user_data;

    action = lrg_bt_action_new_simple (action_success);

    /* Default name is NULL */
    name = lrg_bt_node_get_name (LRG_BT_NODE (action));
    g_assert_null (name);

    /* Set name */
    lrg_bt_node_set_name (LRG_BT_NODE (action), "attack_action");
    name = lrg_bt_node_get_name (LRG_BT_NODE (action));
    g_assert_cmpstr (name, ==, "attack_action");
}

static void
test_bt_composite_children (BTNodeFixture *fixture,
                            gconstpointer  user_data)
{
    g_autoptr(LrgBTSequence) sequence = NULL;
    g_autoptr(LrgBTAction) action1 = NULL;
    g_autoptr(LrgBTAction) action2 = NULL;
    GPtrArray *children;
    gboolean removed;

    (void)fixture;
    (void)user_data;

    sequence = lrg_bt_sequence_new ();
    action1 = lrg_bt_action_new_simple (action_success);
    action2 = lrg_bt_action_new_simple (action_success);

    g_assert_cmpuint (lrg_bt_composite_get_child_count (LRG_BT_COMPOSITE (sequence)), ==, 0);

    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (sequence), LRG_BT_NODE (action1));
    lrg_bt_composite_add_child (LRG_BT_COMPOSITE (sequence), LRG_BT_NODE (action2));
    g_assert_cmpuint (lrg_bt_composite_get_child_count (LRG_BT_COMPOSITE (sequence)), ==, 2);

    children = lrg_bt_composite_get_children (LRG_BT_COMPOSITE (sequence));
    g_assert_nonnull (children);
    g_assert_cmpuint (children->len, ==, 2);

    removed = lrg_bt_composite_remove_child (LRG_BT_COMPOSITE (sequence), LRG_BT_NODE (action1));
    g_assert_true (removed);
    g_assert_cmpuint (lrg_bt_composite_get_child_count (LRG_BT_COMPOSITE (sequence)), ==, 1);

    lrg_bt_composite_clear_children (LRG_BT_COMPOSITE (sequence));
    g_assert_cmpuint (lrg_bt_composite_get_child_count (LRG_BT_COMPOSITE (sequence)), ==, 0);
}

static void
test_bt_decorator_child (BTNodeFixture *fixture,
                         gconstpointer  user_data)
{
    g_autoptr(LrgBTInverter) inverter = NULL;
    g_autoptr(LrgBTAction) action1 = NULL;
    g_autoptr(LrgBTAction) action2 = NULL;
    LrgBTNode *child;

    (void)fixture;
    (void)user_data;

    inverter = lrg_bt_inverter_new (NULL);
    action1 = lrg_bt_action_new_simple (action_success);
    action2 = lrg_bt_action_new_simple (action_failure);

    child = lrg_bt_decorator_get_child (LRG_BT_DECORATOR (inverter));
    g_assert_null (child);

    lrg_bt_decorator_set_child (LRG_BT_DECORATOR (inverter), LRG_BT_NODE (action1));
    child = lrg_bt_decorator_get_child (LRG_BT_DECORATOR (inverter));
    g_assert_true (child == LRG_BT_NODE (action1));

    lrg_bt_decorator_set_child (LRG_BT_DECORATOR (inverter), LRG_BT_NODE (action2));
    child = lrg_bt_decorator_get_child (LRG_BT_DECORATOR (inverter));
    g_assert_true (child == LRG_BT_NODE (action2));
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Blackboard tests */
    g_test_add ("/ai/blackboard/new", BlackboardFixture, NULL,
                blackboard_fixture_set_up, test_blackboard_new,
                blackboard_fixture_tear_down);
    g_test_add ("/ai/blackboard/int", BlackboardFixture, NULL,
                blackboard_fixture_set_up, test_blackboard_int,
                blackboard_fixture_tear_down);
    g_test_add ("/ai/blackboard/float", BlackboardFixture, NULL,
                blackboard_fixture_set_up, test_blackboard_float,
                blackboard_fixture_tear_down);
    g_test_add ("/ai/blackboard/bool", BlackboardFixture, NULL,
                blackboard_fixture_set_up, test_blackboard_bool,
                blackboard_fixture_tear_down);
    g_test_add ("/ai/blackboard/string", BlackboardFixture, NULL,
                blackboard_fixture_set_up, test_blackboard_string,
                blackboard_fixture_tear_down);
    g_test_add ("/ai/blackboard/object", BlackboardFixture, NULL,
                blackboard_fixture_set_up, test_blackboard_object,
                blackboard_fixture_tear_down);
    g_test_add ("/ai/blackboard/has-key", BlackboardFixture, NULL,
                blackboard_fixture_set_up, test_blackboard_has_key,
                blackboard_fixture_tear_down);
    g_test_add ("/ai/blackboard/remove", BlackboardFixture, NULL,
                blackboard_fixture_set_up, test_blackboard_remove,
                blackboard_fixture_tear_down);
    g_test_add ("/ai/blackboard/clear", BlackboardFixture, NULL,
                blackboard_fixture_set_up, test_blackboard_clear,
                blackboard_fixture_tear_down);
    g_test_add ("/ai/blackboard/get-keys", BlackboardFixture, NULL,
                blackboard_fixture_set_up, test_blackboard_get_keys,
                blackboard_fixture_tear_down);

    /* BT Action tests */
    g_test_add ("/ai/bt/action/success", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_action_success,
                bt_node_fixture_tear_down);
    g_test_add ("/ai/bt/action/failure", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_action_failure,
                bt_node_fixture_tear_down);
    g_test_add ("/ai/bt/action/running", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_action_running,
                bt_node_fixture_tear_down);
    g_test_add ("/ai/bt/action/with-blackboard", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_action_with_blackboard,
                bt_node_fixture_tear_down);

    /* BT Condition tests */
    g_test_add ("/ai/bt/condition/true", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_condition_true,
                bt_node_fixture_tear_down);
    g_test_add ("/ai/bt/condition/false", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_condition_false,
                bt_node_fixture_tear_down);
    g_test_add ("/ai/bt/condition/with-user-data", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_condition_with_user_data,
                bt_node_fixture_tear_down);

    /* BT Wait tests */
    g_test_add ("/ai/bt/wait/duration", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_wait_duration,
                bt_node_fixture_tear_down);

    /* BT Sequence tests */
    g_test_add ("/ai/bt/sequence/all-success", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_sequence_all_success,
                bt_node_fixture_tear_down);
    g_test_add ("/ai/bt/sequence/fails-on-first-failure", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_sequence_fails_on_first_failure,
                bt_node_fixture_tear_down);
    g_test_add ("/ai/bt/sequence/running", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_sequence_running,
                bt_node_fixture_tear_down);

    /* BT Selector tests */
    g_test_add ("/ai/bt/selector/first-success", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_selector_first_success,
                bt_node_fixture_tear_down);
    g_test_add ("/ai/bt/selector/all-fail", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_selector_all_fail,
                bt_node_fixture_tear_down);
    g_test_add ("/ai/bt/selector/fallback", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_selector_fallback,
                bt_node_fixture_tear_down);

    /* BT Parallel tests */
    g_test_add ("/ai/bt/parallel/require-one", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_parallel_require_one,
                bt_node_fixture_tear_down);
    g_test_add ("/ai/bt/parallel/require-all", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_parallel_require_all,
                bt_node_fixture_tear_down);

    /* BT Decorator tests */
    g_test_add ("/ai/bt/inverter/success-to-failure", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_inverter_success_to_failure,
                bt_node_fixture_tear_down);
    g_test_add ("/ai/bt/inverter/failure-to-success", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_inverter_failure_to_success,
                bt_node_fixture_tear_down);
    g_test_add ("/ai/bt/inverter/running-unchanged", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_inverter_running_unchanged,
                bt_node_fixture_tear_down);
    g_test_add ("/ai/bt/succeeder", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_succeeder,
                bt_node_fixture_tear_down);
    g_test_add ("/ai/bt/failer", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_failer,
                bt_node_fixture_tear_down);
    g_test_add ("/ai/bt/repeater/finite", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_repeater_finite,
                bt_node_fixture_tear_down);

    /* Behavior Tree tests */
    g_test_add ("/ai/behavior-tree/new", BehaviorTreeFixture, NULL,
                behavior_tree_fixture_set_up, test_behavior_tree_new,
                behavior_tree_fixture_tear_down);
    g_test_add ("/ai/behavior-tree/set-root", BehaviorTreeFixture, NULL,
                behavior_tree_fixture_set_up, test_behavior_tree_set_root,
                behavior_tree_fixture_tear_down);
    g_test_add ("/ai/behavior-tree/tick", BehaviorTreeFixture, NULL,
                behavior_tree_fixture_set_up, test_behavior_tree_tick,
                behavior_tree_fixture_tear_down);
    g_test_add ("/ai/behavior-tree/blackboard", BehaviorTreeFixture, NULL,
                behavior_tree_fixture_set_up, test_behavior_tree_blackboard,
                behavior_tree_fixture_tear_down);
    g_test_add ("/ai/behavior-tree/reset", BehaviorTreeFixture, NULL,
                behavior_tree_fixture_set_up, test_behavior_tree_reset,
                behavior_tree_fixture_tear_down);
    g_test_add ("/ai/behavior-tree/complex", BehaviorTreeFixture, NULL,
                behavior_tree_fixture_set_up, test_behavior_tree_complex,
                behavior_tree_fixture_tear_down);

    /* BT Node property tests */
    g_test_add ("/ai/bt/node/name", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_node_name,
                bt_node_fixture_tear_down);
    g_test_add ("/ai/bt/composite/children", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_composite_children,
                bt_node_fixture_tear_down);
    g_test_add ("/ai/bt/decorator/child", BTNodeFixture, NULL,
                bt_node_fixture_set_up, test_bt_decorator_child,
                bt_node_fixture_tear_down);

    return g_test_run ();
}
