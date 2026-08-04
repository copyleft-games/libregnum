/* test-dialog-yaml.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the dialog YAML loader and node metadata.
 */

#include <glib.h>
#include <gio/gio.h>

#ifndef LIBREGNUM_COMPILATION
#endif
#include "dialog/lrg-dialog-response.h"
#include "dialog/lrg-dialog-node.h"
#include "dialog/lrg-dialog-tree.h"
#include "lrg-enums.h"

/* ==========================================================================
 * Test Data
 * ========================================================================== */

static const gchar test_yaml[] =
    "id: briefing-01\n"
    "title: \"Mission 1 Briefing\"\n"
    "start: intro\n"
    "nodes:\n"
    "  - id: intro\n"
    "    speaker: hashbro\n"
    "    text: \"Crispy on the outside...\"\n"
    "    next: middle\n"
    "    metadata:\n"
    "      portrait: hashbro-determined\n"
    "      voice: vo-intro-01\n"
    "    conditions: [\"flag:met-maple\"]\n"
    "    effects: [\"set-flag:briefed\"]\n"
    "  - id: middle\n"
    "    text: \"An auto-chained line.\"\n"
    "  - id: choice\n"
    "    text: \"Ready to go?\"\n"
    "    responses:\n"
    "      - {text: \"Let's go\", next: outro, conditions: [\"flag:ready\"], effects: [\"set-flag:go\"]}\n"
    "      - {text: \"Not yet\", next: outro}\n"
    "  - id: outro\n"
    "    text: \"Good luck out there.\"\n";

static LrgDialogTree *
load_test_tree (void)
{
    LrgDialogTree     *tree;
    g_autoptr(GError)  error = NULL;

    tree = lrg_dialog_tree_new_from_data (test_yaml, -1, &error);
    g_assert_no_error (error);
    g_assert_nonnull (tree);

    return tree;
}

/* ==========================================================================
 * Loader Tests
 * ========================================================================== */

static void
test_yaml_tree_fields (void)
{
    g_autoptr(LrgDialogTree) tree = NULL;

    tree = load_test_tree ();

    g_assert_cmpstr (lrg_dialog_tree_get_id (tree), ==, "briefing-01");
    g_assert_cmpstr (lrg_dialog_tree_get_title (tree), ==, "Mission 1 Briefing");
    g_assert_cmpstr (lrg_dialog_tree_get_start_node_id (tree), ==, "intro");
    g_assert_cmpuint (lrg_dialog_tree_get_node_count (tree), ==, 4);
}

static void
test_yaml_node_fields (void)
{
    g_autoptr(LrgDialogTree) tree = NULL;
    LrgDialogNode            *node;

    tree = load_test_tree ();

    node = lrg_dialog_tree_get_node (tree, "intro");
    g_assert_nonnull (node);
    g_assert_cmpstr (lrg_dialog_node_get_speaker (node), ==, "hashbro");
    g_assert_cmpstr (lrg_dialog_node_get_text (node), ==,
                     "Crispy on the outside...");

    /* Explicit next is respected */
    g_assert_cmpstr (lrg_dialog_node_get_next_node_id (node), ==, "middle");
}

static void
test_yaml_auto_chain (void)
{
    g_autoptr(LrgDialogTree) tree = NULL;
    LrgDialogNode            *node;

    tree = load_test_tree ();

    /* 'middle' has neither next nor responses: chained to 'choice' */
    node = lrg_dialog_tree_get_node (tree, "middle");
    g_assert_nonnull (node);
    g_assert_cmpstr (lrg_dialog_node_get_next_node_id (node), ==, "choice");

    /* 'choice' has responses, so it must not be auto-chained */
    node = lrg_dialog_tree_get_node (tree, "choice");
    g_assert_nonnull (node);
    g_assert_null (lrg_dialog_node_get_next_node_id (node));

    /* The last node stays terminal */
    node = lrg_dialog_tree_get_node (tree, "outro");
    g_assert_nonnull (node);
    g_assert_null (lrg_dialog_node_get_next_node_id (node));
    g_assert_true (lrg_dialog_node_is_terminal (node));
}

static void
test_yaml_responses (void)
{
    g_autoptr(LrgDialogTree) tree = NULL;
    LrgDialogNode            *node;
    LrgDialogResponse        *resp;
    GPtrArray                *strings;

    tree = load_test_tree ();

    node = lrg_dialog_tree_get_node (tree, "choice");
    g_assert_nonnull (node);
    g_assert_cmpuint (lrg_dialog_node_get_response_count (node), ==, 2);

    resp = lrg_dialog_node_get_response (node, 0);
    g_assert_nonnull (resp);
    g_assert_cmpstr (lrg_dialog_response_get_text (resp), ==, "Let's go");
    g_assert_cmpstr (lrg_dialog_response_get_next_node_id (resp), ==, "outro");

    strings = lrg_dialog_response_get_conditions (resp);
    g_assert_cmpuint (strings->len, ==, 1);
    g_assert_cmpstr (g_ptr_array_index (strings, 0), ==, "flag:ready");

    strings = lrg_dialog_response_get_effects (resp);
    g_assert_cmpuint (strings->len, ==, 1);
    g_assert_cmpstr (g_ptr_array_index (strings, 0), ==, "set-flag:go");

    resp = lrg_dialog_node_get_response (node, 1);
    g_assert_nonnull (resp);
    g_assert_cmpstr (lrg_dialog_response_get_text (resp), ==, "Not yet");
    g_assert_cmpstr (lrg_dialog_response_get_next_node_id (resp), ==, "outro");
    g_assert_cmpuint (lrg_dialog_response_get_conditions (resp)->len, ==, 0);
    g_assert_cmpuint (lrg_dialog_response_get_effects (resp)->len, ==, 0);
}

static void
test_yaml_conditions_effects (void)
{
    g_autoptr(LrgDialogTree) tree = NULL;
    LrgDialogNode            *node;
    GPtrArray                *strings;

    tree = load_test_tree ();

    node = lrg_dialog_tree_get_node (tree, "intro");
    g_assert_nonnull (node);

    strings = lrg_dialog_node_get_conditions (node);
    g_assert_cmpuint (strings->len, ==, 1);
    g_assert_cmpstr (g_ptr_array_index (strings, 0), ==, "flag:met-maple");

    strings = lrg_dialog_node_get_effects (node);
    g_assert_cmpuint (strings->len, ==, 1);
    g_assert_cmpstr (g_ptr_array_index (strings, 0), ==, "set-flag:briefed");
}

static void
test_yaml_metadata (void)
{
    g_autoptr(LrgDialogTree) tree = NULL;
    LrgDialogNode            *node;
    GList                    *keys;

    tree = load_test_tree ();

    node = lrg_dialog_tree_get_node (tree, "intro");
    g_assert_nonnull (node);

    g_assert_cmpstr (lrg_dialog_node_get_metadata_value (node, "portrait"),
                     ==, "hashbro-determined");
    g_assert_cmpstr (lrg_dialog_node_get_metadata_value (node, "voice"),
                     ==, "vo-intro-01");
    g_assert_null (lrg_dialog_node_get_metadata_value (node, "missing"));

    keys = lrg_dialog_node_get_metadata_keys (node);
    g_assert_cmpuint (g_list_length (keys), ==, 2);
    g_list_free (keys);

    /* Nodes without a metadata block have no keys */
    node = lrg_dialog_tree_get_node (tree, "middle");
    g_assert_nonnull (node);
    g_assert_null (lrg_dialog_node_get_metadata_keys (node));
}

static void
test_yaml_validate (void)
{
    g_autoptr(LrgDialogTree) tree = NULL;
    g_autoptr(GError)         error = NULL;

    tree = load_test_tree ();

    g_assert_true (lrg_dialog_tree_validate (tree, &error));
    g_assert_no_error (error);
}

/* ==========================================================================
 * Error Tests
 * ========================================================================== */

static void
test_yaml_error_empty (void)
{
    LrgDialogTree     *tree;
    g_autoptr(GError)  error = NULL;

    tree = lrg_dialog_tree_new_from_data ("", -1, &error);
    g_assert_null (tree);
    g_assert_nonnull (error);
}

static void
test_yaml_error_missing_node_id (void)
{
    LrgDialogTree     *tree;
    g_autoptr(GError)  error = NULL;
    const gchar       *yaml =
        "id: broken\n"
        "nodes:\n"
        "  - text: \"No id here.\"\n";

    tree = lrg_dialog_tree_new_from_data (yaml, -1, &error);
    g_assert_null (tree);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

static void
test_yaml_error_response_missing_next (void)
{
    LrgDialogTree     *tree;
    g_autoptr(GError)  error = NULL;
    const gchar       *yaml =
        "id: broken\n"
        "nodes:\n"
        "  - id: only\n"
        "    text: \"Choose.\"\n"
        "    responses:\n"
        "      - {text: \"Dead end\"}\n";

    tree = lrg_dialog_tree_new_from_data (yaml, -1, &error);
    g_assert_null (tree);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

/* ==========================================================================
 * Metadata API Tests
 * ========================================================================== */

static void
test_metadata_set_get (void)
{
    g_autoptr(LrgDialogNode) node = NULL;

    node = lrg_dialog_node_new ("node1");

    /* Unset key returns NULL */
    g_assert_null (lrg_dialog_node_get_metadata_value (node, "portrait"));

    lrg_dialog_node_set_metadata_value (node, "portrait", "hero-happy");
    g_assert_cmpstr (lrg_dialog_node_get_metadata_value (node, "portrait"),
                     ==, "hero-happy");

    /* Overwrite */
    lrg_dialog_node_set_metadata_value (node, "portrait", "hero-sad");
    g_assert_cmpstr (lrg_dialog_node_get_metadata_value (node, "portrait"),
                     ==, "hero-sad");

    /* Other keys stay unset */
    g_assert_null (lrg_dialog_node_get_metadata_value (node, "voice"));
}

static gboolean
key_list_contains (GList       *keys,
                   const gchar *key)
{
    GList *l;

    for (l = keys; l != NULL; l = l->next)
    {
        if (g_strcmp0 (l->data, key) == 0)
            return TRUE;
    }

    return FALSE;
}

static void
test_metadata_keys (void)
{
    g_autoptr(LrgDialogNode) node = NULL;
    GList                    *keys;

    node = lrg_dialog_node_new ("node1");

    /* No metadata: empty list */
    g_assert_null (lrg_dialog_node_get_metadata_keys (node));

    lrg_dialog_node_set_metadata_value (node, "portrait", "hero");
    lrg_dialog_node_set_metadata_value (node, "voice", "vo-01");
    lrg_dialog_node_set_metadata_value (node, "animation", "wave");

    keys = lrg_dialog_node_get_metadata_keys (node);
    g_assert_cmpuint (g_list_length (keys), ==, 3);
    g_assert_true (key_list_contains (keys, "portrait"));
    g_assert_true (key_list_contains (keys, "voice"));
    g_assert_true (key_list_contains (keys, "animation"));
    g_list_free (keys);

    /* Overwriting does not add a key */
    lrg_dialog_node_set_metadata_value (node, "voice", "vo-02");
    keys = lrg_dialog_node_get_metadata_keys (node);
    g_assert_cmpuint (g_list_length (keys), ==, 3);
    g_list_free (keys);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Loader tests */
    g_test_add_func ("/dialog-yaml/tree_fields", test_yaml_tree_fields);
    g_test_add_func ("/dialog-yaml/node_fields", test_yaml_node_fields);
    g_test_add_func ("/dialog-yaml/auto_chain", test_yaml_auto_chain);
    g_test_add_func ("/dialog-yaml/responses", test_yaml_responses);
    g_test_add_func ("/dialog-yaml/conditions_effects", test_yaml_conditions_effects);
    g_test_add_func ("/dialog-yaml/metadata", test_yaml_metadata);
    g_test_add_func ("/dialog-yaml/validate", test_yaml_validate);

    /* Error tests */
    g_test_add_func ("/dialog-yaml/error/empty", test_yaml_error_empty);
    g_test_add_func ("/dialog-yaml/error/missing_node_id", test_yaml_error_missing_node_id);
    g_test_add_func ("/dialog-yaml/error/response_missing_next", test_yaml_error_response_missing_next);

    /* Metadata API tests */
    g_test_add_func ("/dialog-yaml/metadata_api/set_get", test_metadata_set_get);
    g_test_add_func ("/dialog-yaml/metadata_api/keys", test_metadata_keys);

    return g_test_run ();
}
