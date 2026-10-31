/* test-decision.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The decision layer without a model: LrgDecisionRequest's wire JSON,
 * LrgJudgments read from a response (Nouls in bands, Choices, Scores,
 * the blackboard), LrgDecider's scripted backend and its named ones,
 * LrgDecisionBrain's topics (one question out each, the cache, failures,
 * the journal, the status) and the behavior tree's judgment conditions.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <string.h>
#include <libregnum.h>

/* a response to the questions of make_request() */
static const gchar RESPONSE[] =
    "{\"model\":\"jev-1.14.0\",\"answers\":{"
    "\"quest_target\":{\"type\":\"noul\",\"noul\":0.92},"
    "\"caster\":{\"type\":\"noul\",\"noul\":0.5},"
    "\"role\":{\"type\":\"choice\",\"choice\":\"damage\",\"confidence\":0.8,"
    "\"probabilities\":{\"damage\":0.85,\"heal\":0.1,\"buff\":0.05}},"
    "\"danger\":{\"type\":\"score\",\"score\":1.4,\"probabilities\":{\"0\":0.1,\"1\":0.4,\"2\":0.5}}},"
    "\"usage\":{\"input_tokens\":300,\"output_tokens\":20}}";

static LrgDecisionRequest *
make_request (void)
{
    static const gchar * const roles[] = { "damage", "heal", "buff", NULL };
    static const gchar * const levels[] = { "an easy fight", "a fair fight", "a fight likely lost", NULL };
    LrgDecisionRequest *request = lrg_decision_request_new ();
    g_autoptr(GError) error = NULL;

    g_assert_true (lrg_decision_request_set_state_json (request,
                   "{\"player\":{\"level\":3},\"target\":{\"name\":\"Kobold Vermin\"}}", &error));
    g_assert_true (lrg_decision_request_add_noul (request, "quest_target",
                   "Does a quest objective ask the player to kill `target`?",
                   "an objective names the target's kind", NULL, &error));
    g_assert_true (lrg_decision_request_add_noul (request, "caster", "Does `target` cast spells?", NULL, NULL,
                                                  &error));
    g_assert_true (lrg_decision_request_add_choice (request, "role", "What does the spell do?", roles, NULL,
                                                    &error));
    g_assert_true (lrg_decision_request_add_score (request, "danger", "How dangerous is the fight?", levels,
                                                   &error));
    g_assert_no_error (error);
    return request;
}

/* the wire JSON: the state as an object, each question typed */
static void
test_request_json (void)
{
    g_autoptr(LrgDecisionRequest) request = make_request ();
    g_autoptr(JsonNode) root = NULL;
    g_autofree gchar *json = NULL;
    g_autoptr(GError) error = NULL;
    JsonObject *object;
    JsonObject *questions;

    g_assert_cmpuint (lrg_decision_request_get_n_questions (request), ==, 4);
    json = lrg_decision_request_dup_json (request, "jev-latest");
    root = json_from_string (json, &error);
    g_assert_no_error (error);
    object = json_node_get_object (root);
    g_assert_cmpstr (json_object_get_string_member (object, "model"), ==, "jev-latest");
    g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (
                     json_object_get_object_member (object, "state"), "target"), "name"), ==, "Kobold Vermin");
    questions = json_object_get_object_member (object, "questions");
    g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (questions, "quest_target"),
                     "type"), ==, "noul");
    g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (
                     json_object_get_object_member (questions, "quest_target"), "criteria"), "true"), ==,
                     "an objective names the target's kind");
    g_assert_false (json_object_has_member (json_object_get_object_member (questions, "caster"), "criteria"));
    g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (
                     json_object_get_object_member (questions, "role"), "criteria"), "heal"), ==, "heal");
    g_assert_cmpuint (json_array_get_length (json_object_get_array_member (
                      json_object_get_object_member (questions, "danger"), "criteria")), ==, 3);

    /* no model named: none in the JSON */
    g_free (json);
    json = lrg_decision_request_dup_json (request, NULL);
    g_assert_null (strstr (json, "\"model\""));
}

/* the mistakes a request refuses */
static void
test_request_errors (void)
{
    static const gchar * const twice[] = { "a", "a", NULL };
    static const gchar * const one[] = { "only", NULL };
    static const gchar * const none[] = { NULL };
    g_autoptr(LrgDecisionRequest) request = lrg_decision_request_new ();
    g_autoptr(GError) error = NULL;

    g_assert_false (lrg_decision_request_set_state_json (request, "\"a string\"", &error));
    g_assert_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_INVALID);
    g_clear_error (&error);
    g_assert_false (lrg_decision_request_add_noul (request, "", "q?", NULL, NULL, &error));
    g_assert_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_INVALID);
    g_clear_error (&error);
    g_assert_true (lrg_decision_request_add_noul (request, "q", "q?", NULL, NULL, &error));
    g_assert_false (lrg_decision_request_add_noul (request, "q", "again?", NULL, NULL, &error));
    g_assert_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_INVALID);
    g_clear_error (&error);
    g_assert_false (lrg_decision_request_add_choice (request, "c", "which?", twice, NULL, &error));
    g_assert_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_INVALID);
    g_clear_error (&error);
    g_assert_false (lrg_decision_request_add_choice (request, "c", "which?", none, NULL, &error));
    g_clear_error (&error);
    g_assert_false (lrg_decision_request_add_score (request, "s", "how?", one, &error));
    g_assert_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_INVALID);
    g_clear_error (&error);
    g_assert_cmpuint (lrg_decision_request_get_n_questions (request), ==, 1);
}

/* the answers: bands, choices, scores, tokens, the blackboard */
static void
test_judgments (void)
{
    g_autoptr(LrgJudgments) j = NULL;
    g_autoptr(LrgBlackboard) board = lrg_blackboard_new ();
    g_autoptr(GError) error = NULL;
    g_auto(GStrv) ids = NULL;
    gint64 in;
    gint64 out;

    j = lrg_judgments_new_from_json (RESPONSE, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (lrg_judgments_get_model (j), ==, "jev-1.14.0");
    g_assert_true (lrg_judgments_has (j, "role"));
    g_assert_false (lrg_judgments_has (j, "nothing"));
    g_assert_cmpfloat_with_epsilon (lrg_judgments_get_noul (j, "quest_target"), 0.92, 1e-9);
    g_assert_cmpfloat (lrg_judgments_get_noul (j, "role"), ==, -1.0);
    g_assert_cmpint (lrg_judgments_get_band (j, "quest_target", LRG_JUDGMENT_YES, LRG_JUDGMENT_NO), ==,
                     LRG_JUDGMENT_BAND_YES);
    g_assert_cmpint (lrg_judgments_get_band (j, "caster", LRG_JUDGMENT_YES, LRG_JUDGMENT_NO), ==,
                     LRG_JUDGMENT_BAND_UNSETTLED);
    g_assert_cmpint (lrg_judgments_get_band (j, "caster", 0.4, 0.2), ==, LRG_JUDGMENT_BAND_YES);
    g_assert_cmpint (lrg_judgments_get_band (j, "nothing", LRG_JUDGMENT_YES, LRG_JUDGMENT_NO), ==,
                     LRG_JUDGMENT_BAND_MISSING);
    g_assert_cmpstr (lrg_judgments_get_choice (j, "role"), ==, "damage");
    g_assert_null (lrg_judgments_get_choice (j, "caster"));
    g_assert_cmpfloat_with_epsilon (lrg_judgments_get_probability (j, "role", "heal"), 0.1, 1e-9);
    g_assert_cmpfloat_with_epsilon (lrg_judgments_get_probability (j, "danger", "2"), 0.5, 1e-9);
    g_assert_cmpfloat (lrg_judgments_get_probability (j, "role", "nothing"), ==, -1.0);
    g_assert_cmpfloat_with_epsilon (lrg_judgments_get_score (j, "danger"), 1.4, 1e-9);
    lrg_judgments_get_tokens (j, &in, &out);
    g_assert_cmpint (in, ==, 300);
    g_assert_cmpint (out, ==, 20);
    ids = lrg_judgments_dup_ids (j);
    g_assert_cmpuint (g_strv_length (ids), ==, 4);

    lrg_judgments_to_blackboard (j, board, "combat.");
    g_assert_cmpfloat_with_epsilon (lrg_blackboard_get_float (board, "combat.quest_target", -1.0f), 0.92f, 1e-6f);
    g_assert_cmpstr (lrg_blackboard_get_string (board, "combat.role"), ==, "damage");
    g_assert_cmpfloat_with_epsilon (lrg_blackboard_get_float (board, "combat.danger", -1.0f), 1.4f, 1e-6f);

    /* not a response */
    g_assert_null (lrg_judgments_new_from_json ("{\"model\":\"x\"}", &error));
    g_assert_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_INVALID);
}

/* the scripted model: counts its calls, answers RESPONSE or fails */
typedef struct
{
    guint    calls;
    gboolean fail;
    gchar   *last_request;
} Script;

static gchar *
script_answer (LrgDecider   *decider,
               const gchar  *request_json,
               gpointer      user_data,
               GError      **error)
{
    Script *script = user_data;

    script->calls++;
    g_free (script->last_request);
    script->last_request = g_strdup (request_json);
    if (script->fail)
    {
        g_set_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_FAILED, "the model is away");
        return NULL;
    }
    return g_strdup (RESPONSE);
}

/* the main loop turns until @brain's topic has no question out */
static void
wait_for (LrgDecisionBrain *brain,
          const gchar      *topic)
{
    guint i;

    for (i = 0; i < 1000 && lrg_decision_brain_is_busy (brain, topic); i++)
        g_main_context_iteration (NULL, TRUE);
    g_assert_false (lrg_decision_brain_is_busy (brain, topic));
}

static void
count_signal (LrgDecisionBrain *brain,
              const gchar      *topic,
              gpointer          value,
              gpointer          user_data)
{
    (*(guint *)user_data)++;
}

/* named deciders: an unknown name; in an AI=1 build the backends exist
 * and no request is sent making them */
static void
test_decider_names (void)
{
    g_autoptr(LrgDecider) unknown = NULL;
    g_autoptr(GError) error = NULL;

    unknown = lrg_decider_new ("telepathy", &error);
    g_assert_null (unknown);
    g_assert_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_UNSUPPORTED);
    g_clear_error (&error);
    if (lrg_decider_is_built_in ())
    {
        g_autoptr(LrgDecider) jev = lrg_decider_new ("jev", &error);
        g_autoptr(LrgDecider) ollama = lrg_decider_new ("ollama", &error);

        g_assert_no_error (error);
        g_assert_cmpstr (lrg_decider_get_provider (jev), ==, "typesafe");
        g_assert_cmpstr (lrg_decider_get_provider (ollama), ==, "ollama");
        lrg_decider_set_model (jev, "jev-1.14.0");
        lrg_decider_set_timeout_ms (jev, 5000);
        g_assert_cmpstr (lrg_decider_get_model (jev), ==, "jev-1.14.0");
        g_assert_cmpuint (lrg_decider_get_timeout_ms (jev), ==, 5000);
    }
    else
    {
        g_autoptr(LrgDecider) jev = lrg_decider_new ("jev", &error);

        g_assert_null (jev);
        g_assert_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_UNSUPPORTED);
    }
}

/* the brain: one question out per topic, answers to the blackboard, the
 * cache, a failure keeps the last answers, the journal, the status */
static void
test_brain (void)
{
    Script script = { 0, FALSE, NULL };
    g_autoptr(LrgDecider) decider = NULL;
    g_autoptr(LrgDecisionBrain) brain = NULL;
    g_autoptr(LrgDecisionRequest) request = make_request ();
    g_autoptr(LrgBlackboard) board = lrg_blackboard_new ();
    g_autoptr(GError) error = NULL;
    g_autofree gchar *dir = NULL;
    g_autofree gchar *journal = NULL;
    g_autofree gchar *text = NULL;
    g_autofree gchar *status = NULL;
    g_auto(GStrv) lines = NULL;
    LrgJudgments *latest;
    gboolean cached = TRUE;
    gdouble age = 0.0;
    guint answered = 0;
    guint failed = 0;

    decider = lrg_decider_new_scripted (script_answer, &script, NULL);
    g_assert_cmpstr (lrg_decider_get_provider (decider), ==, "scripted");
    brain = lrg_decision_brain_new (decider);
    lrg_decision_brain_set_blackboard (brain, board);
    dir = g_dir_make_tmp ("lrg-decision-XXXXXX", NULL);
    journal = g_build_filename (dir, "logs", "decisions.jsonl", NULL);
    lrg_decision_brain_set_journal (brain, journal);
    g_signal_connect (brain, "answered", G_CALLBACK (count_signal), &answered);
    g_signal_connect (brain, "failed", G_CALLBACK (count_signal), &failed);

    /* nothing yet; asked, busy, a second ask refused */
    g_assert_null (lrg_decision_brain_get_latest (brain, "combat", &age));
    g_assert_cmpfloat (age, ==, -1.0);
    g_assert_true (lrg_decision_brain_ask (brain, "combat", request, &cached, &error));
    g_assert_no_error (error);
    g_assert_false (cached);
    g_assert_true (lrg_decision_brain_is_busy (brain, "combat"));
    g_assert_false (lrg_decision_brain_ask (brain, "combat", request, NULL, &error));
    g_assert_error (error, LRG_DECIDER_ERROR, LRG_DECIDER_ERROR_BUSY);
    g_clear_error (&error);
    /* another topic goes out beside it */
    g_assert_true (lrg_decision_brain_ask (brain, "spells", request, NULL, &error));
    wait_for (brain, "combat");
    wait_for (brain, "spells");
    g_assert_cmpuint (script.calls, ==, 2);
    g_assert_cmpuint (answered, ==, 2);
    g_assert_nonnull (strstr (script.last_request, "\"Kobold Vermin\""));
    latest = lrg_decision_brain_get_latest (brain, "combat", &age);
    g_assert_nonnull (latest);
    g_assert_cmpfloat (age, >=, 0.0);
    g_assert_cmpstr (lrg_judgments_get_choice (latest, "role"), ==, "damage");
    g_assert_cmpfloat_with_epsilon (lrg_blackboard_get_float (board, "combat.quest_target", -1.0f), 0.92f, 1e-6f);
    g_assert_cmpstr (lrg_blackboard_get_string (board, "spells.role"), ==, "damage");

    /* the cache: the same request answered at once, no call */
    lrg_decision_brain_set_cache_seconds (brain, 60.0);
    g_assert_true (lrg_decision_brain_ask (brain, "combat", request, &cached, &error));
    wait_for (brain, "combat");
    g_assert_true (lrg_decision_brain_ask (brain, "combat", request, &cached, &error));
    g_assert_true (cached);
    g_assert_false (lrg_decision_brain_is_busy (brain, "combat"));
    g_assert_cmpuint (script.calls, ==, 3);

    /* a failure: told, the error kept, the last answers stay */
    lrg_decision_brain_set_cache_seconds (brain, 0.0);
    script.fail = TRUE;
    g_assert_true (lrg_decision_brain_ask (brain, "combat", request, NULL, &error));
    wait_for (brain, "combat");
    g_assert_cmpuint (failed, ==, 1);
    g_assert_cmpstr (lrg_decision_brain_get_last_error (brain, "combat"), ==, "the model is away");
    g_assert_nonnull (lrg_decision_brain_get_latest (brain, "combat", NULL));

    /* the journal: a line per decision, the request and the answer or
     * the error as JSON */
    g_assert_true (g_file_get_contents (journal, &text, NULL, NULL));
    lines = g_strsplit (text, "\n", -1);
    g_assert_cmpuint (g_strv_length (lines), ==, 6);   /* five decisions and the end */
    g_assert_nonnull (strstr (lines[0], "\"provider\":\"scripted\""));
    g_assert_nonnull (strstr (lines[0], "\"response\":{\"model\":\"jev-1.14.0\""));
    g_assert_nonnull (strstr (lines[3], "\"cached\":true"));
    g_assert_nonnull (strstr (lines[4], "\"error\":\"the model is away\""));

    status = lrg_decision_brain_dup_status (brain);
    g_assert_nonnull (strstr (status, "provider: scripted\n"));
    g_assert_nonnull (strstr (status, "asked: 4\nanswered: 3\ncached: 1\nfailed: 1\n"));
    g_assert_nonnull (strstr (status, "input-tokens: 900\n"));
    g_assert_nonnull (strstr (status, "{topic: combat, busy: false"));
    g_assert_nonnull (strstr (status, "error: \"the model is away\"}"));

    /* cancelled: the answer never comes */
    script.fail = FALSE;
    g_assert_true (lrg_decision_brain_ask (brain, "chat", request, NULL, &error));
    lrg_decision_brain_cancel (brain);
    g_assert_false (lrg_decision_brain_is_busy (brain, "chat"));
    while (g_main_context_iteration (NULL, FALSE))
        ;
    g_assert_null (lrg_decision_brain_get_latest (brain, "chat", NULL));

    (void)g_remove (journal);
    g_free (text);
    text = g_path_get_dirname (journal);
    (void)g_rmdir (text);
    (void)g_rmdir (dir);
    g_free (script.last_request);
}

/* the behavior tree's conditions over the blackboard */
static void
test_bt_conditions (void)
{
    g_autoptr(LrgBlackboard) board = lrg_blackboard_new ();
    g_autoptr(LrgBTCondition) yes = NULL;
    g_autoptr(LrgBTCondition) unsettled = NULL;
    g_autoptr(LrgBTCondition) missing = NULL;
    g_autoptr(LrgBTCondition) choice = NULL;

    yes = lrg_bt_condition_new_judgment ("combat.quest_target", LRG_JUDGMENT_BAND_YES, LRG_JUDGMENT_YES,
                                         LRG_JUDGMENT_NO);
    unsettled = lrg_bt_condition_new_judgment ("combat.caster", LRG_JUDGMENT_BAND_UNSETTLED, LRG_JUDGMENT_YES,
                                               LRG_JUDGMENT_NO);
    missing = lrg_bt_condition_new_judgment ("combat.gone", LRG_JUDGMENT_BAND_MISSING, LRG_JUDGMENT_YES,
                                             LRG_JUDGMENT_NO);
    choice = lrg_bt_condition_new_choice ("spells.role", "heal");
    lrg_blackboard_set_float (board, "combat.quest_target", 0.92f);
    lrg_blackboard_set_float (board, "combat.caster", 0.5f);
    lrg_blackboard_set_string (board, "spells.role", "damage");

    g_assert_cmpint (lrg_bt_node_tick (LRG_BT_NODE (yes), board, 0.016f), ==, LRG_BT_STATUS_SUCCESS);
    g_assert_cmpint (lrg_bt_node_tick (LRG_BT_NODE (unsettled), board, 0.016f), ==, LRG_BT_STATUS_SUCCESS);
    g_assert_cmpint (lrg_bt_node_tick (LRG_BT_NODE (missing), board, 0.016f), ==, LRG_BT_STATUS_SUCCESS);
    g_assert_cmpint (lrg_bt_node_tick (LRG_BT_NODE (choice), board, 0.016f), ==, LRG_BT_STATUS_FAILURE);
    lrg_blackboard_set_float (board, "combat.quest_target", 0.2f);
    lrg_blackboard_set_string (board, "spells.role", "heal");
    g_assert_cmpint (lrg_bt_node_tick (LRG_BT_NODE (yes), board, 0.016f), ==, LRG_BT_STATUS_FAILURE);
    g_assert_cmpint (lrg_bt_node_tick (LRG_BT_NODE (choice), board, 0.016f), ==, LRG_BT_STATUS_SUCCESS);
}

int
main (int    argc,
      char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/ai/decision/request-json", test_request_json);
    g_test_add_func ("/ai/decision/request-errors", test_request_errors);
    g_test_add_func ("/ai/decision/judgments", test_judgments);
    g_test_add_func ("/ai/decision/decider-names", test_decider_names);
    g_test_add_func ("/ai/decision/brain", test_brain);
    g_test_add_func ("/ai/decision/bt-conditions", test_bt_conditions);
    return g_test_run ();
}
