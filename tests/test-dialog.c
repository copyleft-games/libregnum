/* test-dialog.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the dialog system.
 */

#include <glib.h>

#ifndef LIBREGNUM_COMPILATION
#endif
#include "dialog/lrg-dialog-response.h"
#include "dialog/lrg-dialog-node.h"
#include "dialog/lrg-dialog-tree.h"
#include "dialog/lrg-dialog-runner.h"
#include "lrg-enums.h"

/* ==========================================================================
 * Response Tests
 * ========================================================================== */

static void
test_response_new (void)
{
    LrgDialogResponse *resp;

    resp = lrg_dialog_response_new ("r1", "Hello!", "node2");
    g_assert_nonnull (resp);
    g_assert_cmpstr (lrg_dialog_response_get_id (resp), ==, "r1");
    g_assert_cmpstr (lrg_dialog_response_get_text (resp), ==, "Hello!");
    g_assert_cmpstr (lrg_dialog_response_get_next_node_id (resp), ==, "node2");

    lrg_dialog_response_free (resp);
}

static void
test_response_copy (void)
{
    LrgDialogResponse *resp;
    LrgDialogResponse *copy;

    resp = lrg_dialog_response_new ("r1", "Test", "node2");
    lrg_dialog_response_add_condition (resp, "has_key");
    lrg_dialog_response_add_effect (resp, "set_flag");

    copy = lrg_dialog_response_copy (resp);
    g_assert_nonnull (copy);
    g_assert_cmpstr (lrg_dialog_response_get_id (copy), ==, "r1");
    g_assert_cmpuint (lrg_dialog_response_get_conditions (copy)->len, ==, 1);
    g_assert_cmpuint (lrg_dialog_response_get_effects (copy)->len, ==, 1);

    lrg_dialog_response_free (resp);
    lrg_dialog_response_free (copy);
}

static void
test_response_conditions (void)
{
    LrgDialogResponse *resp;
    GPtrArray         *conditions;

    resp = lrg_dialog_response_new ("r1", "Test", NULL);

    g_assert_cmpuint (lrg_dialog_response_get_conditions (resp)->len, ==, 0);

    lrg_dialog_response_add_condition (resp, "cond1");
    lrg_dialog_response_add_condition (resp, "cond2");

    conditions = lrg_dialog_response_get_conditions (resp);
    g_assert_cmpuint (conditions->len, ==, 2);
    g_assert_cmpstr (g_ptr_array_index (conditions, 0), ==, "cond1");
    g_assert_cmpstr (g_ptr_array_index (conditions, 1), ==, "cond2");

    lrg_dialog_response_free (resp);
}

static void
test_response_effects (void)
{
    LrgDialogResponse *resp;
    GPtrArray         *effects;

    resp = lrg_dialog_response_new ("r1", "Test", NULL);

    lrg_dialog_response_add_effect (resp, "effect1");
    lrg_dialog_response_add_effect (resp, "effect2");

    effects = lrg_dialog_response_get_effects (resp);
    g_assert_cmpuint (effects->len, ==, 2);

    lrg_dialog_response_free (resp);
}

/* ==========================================================================
 * Node Tests
 * ========================================================================== */

static void
test_node_new (void)
{
    g_autoptr(LrgDialogNode) node = NULL;

    node = lrg_dialog_node_new ("node1");
    g_assert_nonnull (node);
    g_assert_cmpstr (lrg_dialog_node_get_id (node), ==, "node1");
    g_assert_null (lrg_dialog_node_get_speaker (node));
    g_assert_null (lrg_dialog_node_get_text (node));
}

static void
test_node_properties (void)
{
    g_autoptr(LrgDialogNode) node = NULL;

    node = lrg_dialog_node_new ("node1");

    lrg_dialog_node_set_speaker (node, "NPC");
    g_assert_cmpstr (lrg_dialog_node_get_speaker (node), ==, "NPC");

    lrg_dialog_node_set_text (node, "Hello, traveler!");
    g_assert_cmpstr (lrg_dialog_node_get_text (node), ==, "Hello, traveler!");
    g_assert_cmpstr (lrg_dialog_node_get_display_text (node), ==, "Hello, traveler!");

    lrg_dialog_node_set_next_node_id (node, "node2");
    g_assert_cmpstr (lrg_dialog_node_get_next_node_id (node), ==, "node2");
}

static void
test_node_responses (void)
{
    g_autoptr(LrgDialogNode) node = NULL;
    LrgDialogResponse        *resp1;
    LrgDialogResponse        *resp2;

    node = lrg_dialog_node_new ("node1");
    g_assert_cmpuint (lrg_dialog_node_get_response_count (node), ==, 0);

    resp1 = lrg_dialog_response_new ("r1", "Option 1", "node2");
    resp2 = lrg_dialog_response_new ("r2", "Option 2", "node3");

    lrg_dialog_node_add_response (node, resp1);
    lrg_dialog_node_add_response (node, resp2);

    g_assert_cmpuint (lrg_dialog_node_get_response_count (node), ==, 2);
    g_assert_nonnull (lrg_dialog_node_get_response (node, 0));
    g_assert_nonnull (lrg_dialog_node_get_response (node, 1));
    g_assert_null (lrg_dialog_node_get_response (node, 2));
}

static void
test_node_conditions (void)
{
    g_autoptr(LrgDialogNode) node = NULL;

    node = lrg_dialog_node_new ("node1");

    /* No conditions = pass */
    g_assert_true (lrg_dialog_node_evaluate_conditions (node, NULL));

    lrg_dialog_node_add_condition (node, "has_gold");
    g_assert_cmpuint (lrg_dialog_node_get_conditions (node)->len, ==, 1);
}

static void
test_node_terminal (void)
{
    g_autoptr(LrgDialogNode) node = NULL;
    LrgDialogResponse        *resp;

    node = lrg_dialog_node_new ("node1");

    /* No next_node_id and no responses = terminal */
    g_assert_true (lrg_dialog_node_is_terminal (node));

    lrg_dialog_node_set_next_node_id (node, "node2");
    g_assert_false (lrg_dialog_node_is_terminal (node));

    lrg_dialog_node_set_next_node_id (node, NULL);
    g_assert_true (lrg_dialog_node_is_terminal (node));

    resp = lrg_dialog_response_new ("r1", "Ok", NULL);
    lrg_dialog_node_add_response (node, resp);
    g_assert_false (lrg_dialog_node_is_terminal (node));
}

/* ==========================================================================
 * Tree Tests
 * ========================================================================== */

static void
test_tree_new (void)
{
    g_autoptr(LrgDialogTree) tree = NULL;

    tree = lrg_dialog_tree_new ("greeting");
    g_assert_nonnull (tree);
    g_assert_cmpstr (lrg_dialog_tree_get_id (tree), ==, "greeting");
    g_assert_null (lrg_dialog_tree_get_start_node_id (tree));
}

static void
test_tree_nodes (void)
{
    g_autoptr(LrgDialogTree) tree = NULL;
    LrgDialogNode            *node1;
    LrgDialogNode            *node2;

    tree = lrg_dialog_tree_new ("test");

    node1 = lrg_dialog_node_new ("start");
    node2 = lrg_dialog_node_new ("end");

    lrg_dialog_tree_add_node (tree, node1);
    lrg_dialog_tree_add_node (tree, node2);

    g_assert_cmpuint (lrg_dialog_tree_get_node_count (tree), ==, 2);
    g_assert_true (lrg_dialog_tree_get_node (tree, "start") == node1);
    g_assert_true (lrg_dialog_tree_get_node (tree, "end") == node2);
    g_assert_null (lrg_dialog_tree_get_node (tree, "missing"));
}

static void
test_tree_start_node (void)
{
    g_autoptr(LrgDialogTree) tree = NULL;
    LrgDialogNode            *node;

    tree = lrg_dialog_tree_new ("test");
    node = lrg_dialog_node_new ("start");

    lrg_dialog_tree_add_node (tree, node);
    lrg_dialog_tree_set_start_node_id (tree, "start");

    g_assert_cmpstr (lrg_dialog_tree_get_start_node_id (tree), ==, "start");
    g_assert_true (lrg_dialog_tree_get_start_node (tree) == node);
}

static void
test_tree_remove_node (void)
{
    g_autoptr(LrgDialogTree) tree = NULL;
    LrgDialogNode            *node;

    tree = lrg_dialog_tree_new ("test");
    node = lrg_dialog_node_new ("start");

    lrg_dialog_tree_add_node (tree, node);
    g_assert_cmpuint (lrg_dialog_tree_get_node_count (tree), ==, 1);

    g_assert_true (lrg_dialog_tree_remove_node (tree, "start"));
    g_assert_cmpuint (lrg_dialog_tree_get_node_count (tree), ==, 0);

    g_assert_false (lrg_dialog_tree_remove_node (tree, "missing"));
}

static void
test_tree_validate_valid (void)
{
    g_autoptr(LrgDialogTree) tree = NULL;
    LrgDialogNode            *node1;
    LrgDialogNode            *node2;
    g_autoptr(GError)         error = NULL;

    tree = lrg_dialog_tree_new ("test");

    node1 = lrg_dialog_node_new ("start");
    lrg_dialog_node_set_next_node_id (node1, "end");

    node2 = lrg_dialog_node_new ("end");

    lrg_dialog_tree_add_node (tree, node1);
    lrg_dialog_tree_add_node (tree, node2);
    lrg_dialog_tree_set_start_node_id (tree, "start");

    g_assert_true (lrg_dialog_tree_validate (tree, &error));
    g_assert_no_error (error);
}

static void
test_tree_validate_invalid_start (void)
{
    g_autoptr(LrgDialogTree) tree = NULL;
    g_autoptr(GError)         error = NULL;

    tree = lrg_dialog_tree_new ("test");
    lrg_dialog_tree_set_start_node_id (tree, "missing");

    g_assert_false (lrg_dialog_tree_validate (tree, &error));
    g_assert_error (error, LRG_DIALOG_ERROR, LRG_DIALOG_ERROR_INVALID_NODE);
}

static void
test_tree_validate_invalid_next (void)
{
    g_autoptr(LrgDialogTree) tree = NULL;
    LrgDialogNode            *node;
    g_autoptr(GError)         error = NULL;

    tree = lrg_dialog_tree_new ("test");

    node = lrg_dialog_node_new ("start");
    lrg_dialog_node_set_next_node_id (node, "missing");

    lrg_dialog_tree_add_node (tree, node);
    lrg_dialog_tree_set_start_node_id (tree, "start");

    g_assert_false (lrg_dialog_tree_validate (tree, &error));
    g_assert_error (error, LRG_DIALOG_ERROR, LRG_DIALOG_ERROR_INVALID_NODE);
}

/* ==========================================================================
 * Runner Tests
 * ========================================================================== */

static LrgDialogTree *
create_test_tree (void)
{
    LrgDialogTree     *tree;
    LrgDialogNode     *node1;
    LrgDialogNode     *node2;
    LrgDialogNode     *node3;
    LrgDialogResponse *resp1;
    LrgDialogResponse *resp2;

    tree = lrg_dialog_tree_new ("test");

    /* Node 1: NPC greeting with choices */
    node1 = lrg_dialog_node_new ("greeting");
    lrg_dialog_node_set_speaker (node1, "NPC");
    lrg_dialog_node_set_text (node1, "Hello traveler! What brings you here?");

    resp1 = lrg_dialog_response_new ("r1", "I'm looking for work.", "work");
    resp2 = lrg_dialog_response_new ("r2", "Just passing through.", "farewell");

    lrg_dialog_node_add_response (node1, resp1);
    lrg_dialog_node_add_response (node1, resp2);

    /* Node 2: Work dialog */
    node2 = lrg_dialog_node_new ("work");
    lrg_dialog_node_set_speaker (node2, "NPC");
    lrg_dialog_node_set_text (node2, "We have plenty of work for adventurers!");
    /* Terminal - no next, no responses */

    /* Node 3: Farewell */
    node3 = lrg_dialog_node_new ("farewell");
    lrg_dialog_node_set_speaker (node3, "NPC");
    lrg_dialog_node_set_text (node3, "Safe travels!");

    lrg_dialog_tree_add_node (tree, node1);
    lrg_dialog_tree_add_node (tree, node2);
    lrg_dialog_tree_add_node (tree, node3);
    lrg_dialog_tree_set_start_node_id (tree, "greeting");

    return tree;
}

static void
test_runner_new (void)
{
    g_autoptr(LrgDialogRunner) runner = NULL;

    runner = lrg_dialog_runner_new ();
    g_assert_nonnull (runner);
    g_assert_null (lrg_dialog_runner_get_tree (runner));
    g_assert_false (lrg_dialog_runner_is_active (runner));
}

static void
test_runner_start (void)
{
    g_autoptr(LrgDialogRunner) runner = NULL;
    g_autoptr(LrgDialogTree)   tree = NULL;
    g_autoptr(GError)           error = NULL;
    LrgDialogNode              *node;

    runner = lrg_dialog_runner_new ();
    tree = create_test_tree ();

    lrg_dialog_runner_set_tree (runner, tree);
    g_assert_true (lrg_dialog_runner_get_tree (runner) == tree);

    g_assert_true (lrg_dialog_runner_start (runner, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_dialog_runner_is_active (runner));

    node = lrg_dialog_runner_get_current_node (runner);
    g_assert_nonnull (node);
    g_assert_cmpstr (lrg_dialog_node_get_id (node), ==, "greeting");
}

static void
test_runner_start_no_tree (void)
{
    g_autoptr(LrgDialogRunner) runner = NULL;
    g_autoptr(GError)           error = NULL;

    runner = lrg_dialog_runner_new ();

    g_assert_false (lrg_dialog_runner_start (runner, &error));
    g_assert_error (error, LRG_DIALOG_ERROR, LRG_DIALOG_ERROR_NO_TREE);
}

static void
test_runner_at_choice (void)
{
    g_autoptr(LrgDialogRunner) runner = NULL;
    g_autoptr(LrgDialogTree)   tree = NULL;

    runner = lrg_dialog_runner_new ();
    tree = create_test_tree ();

    lrg_dialog_runner_set_tree (runner, tree);
    lrg_dialog_runner_start (runner, NULL);

    g_assert_true (lrg_dialog_runner_is_at_choice (runner));
}

static void
test_runner_select_response (void)
{
    g_autoptr(LrgDialogRunner) runner = NULL;
    g_autoptr(LrgDialogTree)   tree = NULL;
    g_autoptr(GError)           error = NULL;
    LrgDialogNode              *node;

    runner = lrg_dialog_runner_new ();
    tree = create_test_tree ();

    lrg_dialog_runner_set_tree (runner, tree);
    lrg_dialog_runner_start (runner, NULL);

    /* Select "I'm looking for work." (index 0) -> goes to "work" */
    g_assert_true (lrg_dialog_runner_select_response (runner, 0, &error));
    g_assert_no_error (error);

    node = lrg_dialog_runner_get_current_node (runner);
    g_assert_cmpstr (lrg_dialog_node_get_id (node), ==, "work");

    /* work is a terminal node, dialog should have ended */
    g_assert_false (lrg_dialog_runner_is_active (runner));
}

static void
test_runner_available_responses (void)
{
    g_autoptr(LrgDialogRunner) runner = NULL;
    g_autoptr(LrgDialogTree)   tree = NULL;
    g_autoptr(GPtrArray)        responses = NULL;

    runner = lrg_dialog_runner_new ();
    tree = create_test_tree ();

    lrg_dialog_runner_set_tree (runner, tree);
    lrg_dialog_runner_start (runner, NULL);

    responses = lrg_dialog_runner_get_available_responses (runner);
    g_assert_cmpuint (responses->len, ==, 2);
}

static void
test_runner_stop (void)
{
    g_autoptr(LrgDialogRunner) runner = NULL;
    g_autoptr(LrgDialogTree)   tree = NULL;

    runner = lrg_dialog_runner_new ();
    tree = create_test_tree ();

    lrg_dialog_runner_set_tree (runner, tree);
    lrg_dialog_runner_start (runner, NULL);
    g_assert_true (lrg_dialog_runner_is_active (runner));

    lrg_dialog_runner_stop (runner);
    g_assert_false (lrg_dialog_runner_is_active (runner));
    g_assert_null (lrg_dialog_runner_get_current_node (runner));
}

static void
test_runner_context (void)
{
    g_autoptr(LrgDialogRunner) runner = NULL;

    runner = lrg_dialog_runner_new ();

    g_assert_null (lrg_dialog_runner_get_variable (runner, "gold"));

    lrg_dialog_runner_set_variable (runner, "gold", "100");
    g_assert_cmpstr (lrg_dialog_runner_get_variable (runner, "gold"), ==, "100");

    lrg_dialog_runner_set_variable (runner, "gold", "200");
    g_assert_cmpstr (lrg_dialog_runner_get_variable (runner, "gold"), ==, "200");
}

static gboolean node_entered_called = FALSE;
static gboolean dialog_ended_called = FALSE;

static void
on_node_entered (LrgDialogRunner *runner,
                 LrgDialogNode   *node,
                 gpointer         user_data)
{
    node_entered_called = TRUE;
}

static void
on_dialog_ended (LrgDialogRunner *runner,
                 gpointer         user_data)
{
    dialog_ended_called = TRUE;
}

static void
test_runner_signals (void)
{
    g_autoptr(LrgDialogRunner) runner = NULL;
    g_autoptr(LrgDialogTree)   tree = NULL;

    runner = lrg_dialog_runner_new ();
    tree = create_test_tree ();

    g_signal_connect (runner, "node-entered", G_CALLBACK (on_node_entered), NULL);
    g_signal_connect (runner, "dialog-ended", G_CALLBACK (on_dialog_ended), NULL);

    node_entered_called = FALSE;
    dialog_ended_called = FALSE;

    lrg_dialog_runner_set_tree (runner, tree);
    lrg_dialog_runner_start (runner, NULL);

    g_assert_true (node_entered_called);
    g_assert_false (dialog_ended_called);

    /* Select response that leads to terminal node */
    lrg_dialog_runner_select_response (runner, 0, NULL);

    g_assert_true (dialog_ended_called);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Response tests */
    g_test_add_func ("/dialog/response/new", test_response_new);
    g_test_add_func ("/dialog/response/copy", test_response_copy);
    g_test_add_func ("/dialog/response/conditions", test_response_conditions);
    g_test_add_func ("/dialog/response/effects", test_response_effects);

    /* Node tests */
    g_test_add_func ("/dialog/node/new", test_node_new);
    g_test_add_func ("/dialog/node/properties", test_node_properties);
    g_test_add_func ("/dialog/node/responses", test_node_responses);
    g_test_add_func ("/dialog/node/conditions", test_node_conditions);
    g_test_add_func ("/dialog/node/terminal", test_node_terminal);

    /* Tree tests */
    g_test_add_func ("/dialog/tree/new", test_tree_new);
    g_test_add_func ("/dialog/tree/nodes", test_tree_nodes);
    g_test_add_func ("/dialog/tree/start_node", test_tree_start_node);
    g_test_add_func ("/dialog/tree/remove_node", test_tree_remove_node);
    g_test_add_func ("/dialog/tree/validate/valid", test_tree_validate_valid);
    g_test_add_func ("/dialog/tree/validate/invalid_start", test_tree_validate_invalid_start);
    g_test_add_func ("/dialog/tree/validate/invalid_next", test_tree_validate_invalid_next);

    /* Runner tests */
    g_test_add_func ("/dialog/runner/new", test_runner_new);
    g_test_add_func ("/dialog/runner/start", test_runner_start);
    g_test_add_func ("/dialog/runner/start_no_tree", test_runner_start_no_tree);
    g_test_add_func ("/dialog/runner/at_choice", test_runner_at_choice);
    g_test_add_func ("/dialog/runner/select_response", test_runner_select_response);
    g_test_add_func ("/dialog/runner/available_responses", test_runner_available_responses);
    g_test_add_func ("/dialog/runner/stop", test_runner_stop);
    g_test_add_func ("/dialog/runner/context", test_runner_context);
    g_test_add_func ("/dialog/runner/signals", test_runner_signals);

    return g_test_run ();
}
