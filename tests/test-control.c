/* test-control.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the control module: LrgCommandParam schemas, parameter
 * checking and text arguments, LrgCommandCall completion (at once, later,
 * cancelled), the registry (unknown commands, reserved names, listings,
 * the text form, events and topic globs), and LrgControlServer with
 * LrgControlClient over a real Unix socket in a temporary runtime
 * directory: the text dialect, JSON-RPC requests, notifications,
 * batches, protocol errors, async replies in request order, the token,
 * event subscriptions, the socket's permissions, a stale socket being
 * replaced and a live one refused, and instance listing.  The server
 * runs in its own thread (it only polls); the client blocks in the test.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <libregnum.h>

#include <string.h>
#include <sys/stat.h>

/* a short runtime directory: Unix socket paths are limited to 107 bytes */
static gchar *runtime_dir = NULL;

static void
use_short_runtime_dir (void)
{
    g_setenv ("XDG_RUNTIME_DIR", runtime_dir, TRUE);
}

/* ==========================================================================
 * Commands used by the tests
 * ========================================================================== */

/* add: a + b */
static void
cmd_add (LrgCommand     *command,
         LrgCommandCall *call,
         gpointer        user_data)
{
    JsonNode *node = json_node_new (JSON_NODE_VALUE);

    json_node_set_double (node, lrg_command_call_get_double (call, "a", 0) +
                                lrg_command_call_get_double (call, "b", 0));
    lrg_command_call_return_value (call, node);
}

/* echo: the parameters back */
static void
cmd_echo (LrgCommand     *command,
          LrgCommandCall *call,
          gpointer        user_data)
{
    JsonNode *node = json_node_new (JSON_NODE_OBJECT);

    json_node_set_object (node, lrg_command_call_get_params (call));
    lrg_command_call_return_value (call, node);
}

/* fail: always NOT_FOUND */
static void
cmd_fail (LrgCommand     *command,
          LrgCommandCall *call,
          gpointer        user_data)
{
    lrg_command_call_return_new_error (call, LRG_COMMAND_ERROR_NOT_FOUND, "no %s", "thing");
}

/* later: completes when the test (or the server thread) releases it */
typedef struct
{
    GPtrArray *pending;
    gboolean   cancelled_seen;
} Later;

static void
on_later_cancelled (LrgCommandCall *call,
                    gpointer        user_data)
{
    Later *later = user_data;

    later->cancelled_seen = TRUE;
}

static void
cmd_later (LrgCommand     *command,
           LrgCommandCall *call,
           gpointer        user_data)
{
    Later *later = user_data;

    g_signal_connect (call, "cancelled", G_CALLBACK (on_later_cancelled), later);
    g_ptr_array_add (later->pending, g_object_ref (call));
}

/* publish: an event of the given topic, then null */
static void
cmd_publish (LrgCommand     *command,
             LrgCommandCall *call,
             gpointer        user_data)
{
    LrgCommandRegistry *registry = user_data;
    g_autoptr(JsonNode) data = json_node_new (JSON_NODE_OBJECT);
    JsonObject *object = json_object_new ();

    json_object_set_string_member (object, "text", "hello");
    json_node_take_object (data, object);
    lrg_command_registry_publish (registry, lrg_command_call_get_string (call, "topic"), data);
    lrg_command_call_return_value (call, NULL);
}

static LrgCommandRegistry *
make_registry (Later *later)
{
    LrgCommandRegistry *registry = lrg_command_registry_new ();
    LrgCommand *command;
    LrgCommandParam *param;
    static const gchar *const modes[] = { "fast", "slow", NULL };

    command = lrg_command_registry_add (registry, "add", "math", "Add two numbers", cmd_add, NULL, NULL);
    lrg_command_set_flags (command, LRG_COMMAND_FLAG_READ_ONLY);
    lrg_command_add_param_full (command, "a", LRG_COMMAND_PARAM_NUMBER, LRG_COMMAND_PARAM_FLAG_REQUIRED, "first");
    param = lrg_command_add_param_full (command, "b", LRG_COMMAND_PARAM_NUMBER, LRG_COMMAND_PARAM_FLAG_NONE, "second");
    {
        g_autoptr(JsonNode) two = json_node_new (JSON_NODE_VALUE);

        json_node_set_double (two, 2.0);
        lrg_command_param_set_default (param, two);
    }

    command = lrg_command_registry_add (registry, "echo", "misc", "Echo", cmd_echo, NULL, NULL);
    param = lrg_command_add_param_full (command, "count", LRG_COMMAND_PARAM_INT, LRG_COMMAND_PARAM_FLAG_NONE, NULL);
    lrg_command_param_set_range (param, 0, 10);
    param = lrg_command_add_param_full (command, "mode", LRG_COMMAND_PARAM_STRING, LRG_COMMAND_PARAM_FLAG_NONE, NULL);
    lrg_command_param_set_choices (param, modes);
    lrg_command_add_param_full (command, "on", LRG_COMMAND_PARAM_BOOLEAN, LRG_COMMAND_PARAM_FLAG_NONE, NULL);
    lrg_command_add_param_full (command, "text", LRG_COMMAND_PARAM_STRING,
                                LRG_COMMAND_PARAM_FLAG_REST, "the rest of the words");

    lrg_command_registry_add (registry, "fail", "misc", "Fails", cmd_fail, NULL, NULL);

    if (later != NULL)
    {
        command = lrg_command_registry_add (registry, "later", "misc", "Completes later", cmd_later, later, NULL);
        lrg_command_set_flags (command, LRG_COMMAND_FLAG_ASYNC);
    }

    command = lrg_command_registry_add (registry, "publish", "misc", "Publishes", cmd_publish, registry, NULL);
    lrg_command_add_param_full (command, "topic", LRG_COMMAND_PARAM_STRING, LRG_COMMAND_PARAM_FLAG_REQUIRED, NULL);
    lrg_command_registry_add_topic (registry, "messages", "message lines");
    lrg_command_registry_add_topic (registry, "combat", "hits");
    return registry;
}

/* ==========================================================================
 * Parameters, calls, the registry
 * ========================================================================== */

static void
test_param_schema (void)
{
    g_autoptr(LrgCommandRegistry) registry = make_registry (NULL);
    LrgCommand *command = lrg_command_registry_lookup (registry, "echo");
    g_autoptr(JsonNode) schema = NULL;
    g_autoptr(JsonNode) describe = NULL;
    JsonObject *object;
    JsonObject *props;

    schema = lrg_command_get_input_schema (command);
    object = json_node_get_object (schema);
    g_assert_cmpstr (json_object_get_string_member (object, "type"), ==, "object");
    g_assert_false (json_object_get_boolean_member (object, "additionalProperties"));
    props = json_object_get_object_member (object, "properties");
    g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (props, "count"), "type"),
                     ==, "integer");
    g_assert_cmpfloat (json_object_get_double_member (json_object_get_object_member (props, "count"),
                                                      "maximum"), ==, 10.0);
    g_assert_cmpuint (json_array_get_length (json_object_get_array_member (
                          json_object_get_object_member (props, "mode"), "enum")), ==, 2);

    /* required lists only the required ones */
    schema = (json_node_unref (schema), lrg_command_get_input_schema (lrg_command_registry_lookup (registry, "add")));
    g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (schema),
                                                                          "required")), ==, 1);

    describe = lrg_command_describe (lrg_command_registry_lookup (registry, "add"));
    object = json_node_get_object (describe);
    g_assert_cmpstr (json_object_get_string_member (object, "group"), ==, "math");
    g_assert_true (json_object_get_boolean_member (object, "read_only"));
    g_assert_cmpuint (json_array_get_length (json_object_get_array_member (object, "params")), ==, 2);
}

static void
test_check_params (void)
{
    g_autoptr(LrgCommandRegistry) registry = make_registry (NULL);
    LrgCommand *echo = lrg_command_registry_lookup (registry, "echo");
    LrgCommand *add = lrg_command_registry_lookup (registry, "add");
    g_autoptr(JsonObject) in = json_object_new ();
    g_autoptr(JsonObject) out = NULL;
    g_autoptr(GError) error = NULL;

    /* an int given as 3.0 is accepted */
    json_object_set_double_member (in, "count", 3.0);
    out = lrg_command_check_params (echo, in, &error);
    g_assert_no_error (error);
    g_assert_cmpint (json_object_get_int_member (out, "count"), ==, 3);
    g_clear_pointer (&out, json_object_unref);

    /* 3.5 is no int */
    json_object_set_double_member (in, "count", 3.5);
    out = lrg_command_check_params (echo, in, &error);
    g_assert_null (out);
    g_assert_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_INVALID_PARAMS);
    g_clear_error (&error);

    /* out of range */
    json_object_set_int_member (in, "count", 11);
    g_assert_null (lrg_command_check_params (echo, in, &error));
    g_assert_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_INVALID_PARAMS);
    g_clear_error (&error);
    json_object_remove_member (in, "count");

    /* a choice not listed */
    json_object_set_string_member (in, "mode", "medium");
    g_assert_null (lrg_command_check_params (echo, in, &error));
    g_assert_nonnull (strstr (error->message, "fast, slow"));
    g_clear_error (&error);
    json_object_remove_member (in, "mode");

    /* an unknown name names the accepted ones */
    json_object_set_string_member (in, "widht", "x");
    g_assert_null (lrg_command_check_params (echo, in, &error));
    g_assert_nonnull (strstr (error->message, "widht"));
    g_assert_nonnull (strstr (error->message, "count"));
    g_clear_error (&error);
    json_object_remove_member (in, "widht");

    /* a string where a boolean belongs */
    json_object_set_string_member (in, "on", "yes");
    g_assert_null (lrg_command_check_params (echo, in, &error));
    g_clear_error (&error);
    json_object_remove_member (in, "on");

    /* missing required; defaults filled; null counts as absent */
    g_assert_null (lrg_command_check_params (add, NULL, &error));
    g_assert_nonnull (strstr (error->message, "missing required parameter a"));
    g_clear_error (&error);
    json_object_set_int_member (in, "a", 5);
    json_object_set_null_member (in, "b");
    out = lrg_command_check_params (add, in, &error);
    g_assert_no_error (error);
    g_assert_cmpfloat (json_object_get_double_member (out, "a"), ==, 5.0);
    g_assert_cmpfloat (json_object_get_double_member (out, "b"), ==, 2.0);
}

/* ids past 2^53 (world-quest uids, entity ids) stay exact */
static void
test_int64_ids (void)
{
    g_autoptr(LrgCommandRegistry) registry = NULL;
    LrgCommand *command;
    LrgCommandParam *param;
    g_autoptr(JsonObject) in = NULL;
    g_autoptr(JsonObject) out = NULL;
    g_autoptr(GError) error = NULL;
    const gint64 quest = G_GINT64_CONSTANT (4611686018427387906);       /* 2^62 + 2 */
    const gint64 past_mantissa = G_GINT64_CONSTANT (9007199254740993);  /* 2^53 + 1 */
    const gint64 negative = G_GINT64_CONSTANT (-9223372036854775764);

    registry = lrg_command_registry_new ();
    command = lrg_command_registry_add (registry, "mark", "act", "Mark an id", cmd_echo, NULL, NULL);
    param = lrg_command_add_param_full (command, "quest", LRG_COMMAND_PARAM_INT,
                                        LRG_COMMAND_PARAM_FLAG_NONE, NULL);

    in = json_object_new ();
    json_object_set_int_member (in, "quest", quest);
    out = lrg_command_check_params (command, in, &error);
    g_assert_no_error (error);
    g_assert_true (json_object_get_int_member (out, "quest") == quest);
    g_clear_pointer (&out, json_object_unref);

    json_object_set_int_member (in, "quest", past_mantissa);
    out = lrg_command_check_params (command, in, &error);
    g_assert_no_error (error);
    g_assert_true (json_object_get_int_member (out, "quest") == past_mantissa);
    g_clear_pointer (&out, json_object_unref);

    json_object_set_int_member (in, "quest", negative);
    out = lrg_command_check_params (command, in, &error);
    g_assert_no_error (error);
    g_assert_true (json_object_get_int_member (out, "quest") == negative);
    g_clear_pointer (&out, json_object_unref);

    /* 3.0 is an integer; 1.5 and a magnitude past gint64 are not */
    json_object_set_double_member (in, "quest", 3.0);
    out = lrg_command_check_params (command, in, &error);
    g_assert_no_error (error);
    g_assert_true (json_object_get_int_member (out, "quest") == 3);
    g_clear_pointer (&out, json_object_unref);
    json_object_set_double_member (in, "quest", 1.5);
    g_assert_null (lrg_command_check_params (command, in, &error));
    g_assert_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_INVALID_PARAMS);
    g_clear_error (&error);
    json_object_set_double_member (in, "quest", 1e20);
    g_assert_null (lrg_command_check_params (command, in, &error));
    g_assert_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_INVALID_PARAMS);
    g_clear_error (&error);

    json_object_set_double_member (in, "quest", 9007199254740992.0);
    out = lrg_command_check_params (command, in, &error);
    g_assert_no_error (error);
    g_assert_true (json_object_get_int_member (out, "quest") == G_GINT64_CONSTANT (9007199254740992));
    g_clear_pointer (&out, json_object_unref);

    /* the text form still goes through the same check */
    {
        g_autofree gchar *line = NULL;
        g_autofree gchar *name = NULL;
        g_autoptr(JsonObject) params = NULL;
        g_autoptr(JsonObject) checked = NULL;

        line = g_strdup_printf ("mark quest=%" G_GINT64_FORMAT, quest);
        g_assert_true (lrg_command_registry_parse_line (registry, line, &name, &params, &error));
        g_assert_no_error (error);
        g_assert_true (json_object_get_int_member (params, "quest") == quest);
        checked = lrg_command_check_params (command, params, &error);
        g_assert_no_error (error);
        g_assert_true (json_object_get_int_member (checked, "quest") == quest);
    }
    {
        g_autofree gchar *name = NULL;
        g_autoptr(JsonObject) params = NULL;

        g_assert_true (lrg_command_registry_parse_line (registry, "mark quest=3.0", &name, &params, &error));
        g_assert_no_error (error);
        g_assert_true (json_object_get_int_member (params, "quest") == 3);
        g_clear_pointer (&name, g_free);
        g_clear_pointer (&params, json_object_unref);
        g_assert_false (lrg_command_registry_parse_line (registry,
                                                         "mark quest=999999999999999999999",
                                                         &name, &params, &error));
        g_assert_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_INVALID_PARAMS);
        g_clear_error (&error);
    }

    /* a small range still refuses the id; a wide one keeps it */
    lrg_command_param_set_range (param, 0, 10);
    json_object_set_int_member (in, "quest", quest);
    g_assert_null (lrg_command_check_params (command, in, &error));
    g_assert_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_INVALID_PARAMS);
    g_clear_error (&error);
    json_object_set_int_member (in, "quest", 10);
    out = lrg_command_check_params (command, in, &error);
    g_assert_no_error (error);
    g_assert_true (json_object_get_int_member (out, "quest") == 10);
    g_clear_pointer (&out, json_object_unref);
    lrg_command_param_set_range (param, 0, 1e19);
    json_object_set_int_member (in, "quest", quest);
    out = lrg_command_check_params (command, in, &error);
    g_assert_no_error (error);
    g_assert_true (json_object_get_int_member (out, "quest") == quest);
}

static void
test_parse_arguments (void)
{
    g_autoptr(LrgCommandRegistry) registry = make_registry (NULL);
    g_autoptr(GError) error = NULL;
    g_autofree gchar *name = NULL;
    g_autoptr(JsonObject) params = NULL;

    /* positional, named, the rest */
    g_assert_true (lrg_command_registry_parse_line (registry, "echo 4 mode=slow yes 'hello there' friend",
                                                    &name, &params, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (name, ==, "echo");
    g_assert_cmpint (json_object_get_int_member (params, "count"), ==, 4);
    g_assert_cmpstr (json_object_get_string_member (params, "mode"), ==, "slow");
    g_assert_true (json_object_get_boolean_member (params, "on"));
    g_assert_cmpstr (json_object_get_string_member (params, "text"), ==, "hello there friend");
    g_clear_pointer (&name, g_free);
    g_clear_pointer (&params, json_object_unref);

    /* a negative number is a value, not an option */
    g_assert_true (lrg_command_registry_parse_line (registry, "add -1.5 4", &name, &params, &error));
    g_assert_cmpfloat (json_object_get_double_member (params, "a"), ==, -1.5);
    g_clear_pointer (&name, g_free);
    g_clear_pointer (&params, json_object_unref);

    /* bad words */
    g_assert_false (lrg_command_registry_parse_line (registry, "add one", &name, &params, &error));
    g_assert_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_INVALID_PARAMS);
    g_clear_error (&error);
    g_assert_false (lrg_command_registry_parse_line (registry, "add 1 2 3", &name, &params, &error));
    g_assert_nonnull (strstr (error->message, "too many"));
    g_clear_error (&error);
    g_assert_false (lrg_command_registry_parse_line (registry, "nope 1", &name, &params, &error));
    g_assert_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_UNKNOWN_COMMAND);
    g_clear_error (&error);
    g_assert_false (lrg_command_registry_parse_line (registry, "echo 'unclosed", &name, &params, &error));
    g_clear_error (&error);
    g_assert_false (lrg_command_registry_parse_line (registry, "   ", &name, &params, &error));
    g_clear_error (&error);
}

static void
test_registry_invoke (void)
{
    Later later = { NULL, FALSE };
    g_autoptr(LrgCommandRegistry) registry = NULL;
    g_autoptr(LrgCommandCall) call = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(JsonNode) result = NULL;
    g_autoptr(GPtrArray) list = NULL;
    g_autoptr(LrgCommand) reserved = NULL;

    later.pending = g_ptr_array_new_with_free_func (g_object_unref);
    registry = make_registry (&later);

    /* at once, from the text form */
    call = lrg_command_registry_invoke_line (registry, "add 1 2.5", "test");
    g_assert_true (lrg_command_call_is_done (call));
    g_assert_cmpfloat (json_node_get_double (lrg_command_call_get_result (call)), ==, 3.5);
    g_assert_cmpstr (lrg_command_call_get_origin (call), ==, "test");
    g_clear_object (&call);

    /* errors complete the call */
    call = lrg_command_registry_invoke (registry, "fail", NULL, NULL);
    g_assert_true (lrg_command_call_is_done (call));
    g_assert_error (lrg_command_call_get_error (call), LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_NOT_FOUND);
    g_assert_cmpint (lrg_command_error_to_jsonrpc_code (lrg_command_call_get_error (call)), ==, -32001);
    g_clear_object (&call);
    call = lrg_command_registry_invoke (registry, "missing", NULL, NULL);
    g_assert_error (lrg_command_call_get_error (call), LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_UNKNOWN_COMMAND);
    g_clear_object (&call);

    /* later: pending until returned; a second return is ignored */
    call = lrg_command_registry_invoke (registry, "later", NULL, NULL);
    g_assert_false (lrg_command_call_is_done (call));
    g_assert_null (lrg_command_call_propagate (call, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PENDING);
    g_clear_error (&error);
    lrg_command_call_return_value (g_ptr_array_index (later.pending, 0), NULL);
    g_assert_true (lrg_command_call_is_done (call));
    lrg_command_call_return_new_error (call, LRG_COMMAND_ERROR_FAILED, "too late");
    g_assert_null (lrg_command_call_get_error (call));
    g_clear_object (&call);

    /* cancelled: the handler hears it, the call fails as cancelled */
    call = lrg_command_registry_invoke (registry, "later", NULL, NULL);
    lrg_command_call_cancel (call);
    g_assert_true (later.cancelled_seen);
    g_assert_error (lrg_command_call_get_error (call), LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_CANCELLED);
    g_clear_object (&call);

    /* call_sync refuses what does not end at once */
    g_assert_null (lrg_command_registry_call_sync (registry, "later", NULL, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PENDING);
    g_clear_error (&error);
    result = lrg_command_registry_call_sync (registry, "add", NULL, &error);
    g_assert_null (result);
    g_assert_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_INVALID_PARAMS);
    g_clear_error (&error);

    /* listings: by group, then name */
    list = lrg_command_registry_list (registry, FALSE);
    g_assert_cmpuint (list->len, ==, 5);
    g_assert_cmpstr (lrg_command_get_name (g_ptr_array_index (list, 0)), ==, "add");
    g_assert_cmpstr (lrg_command_get_name (g_ptr_array_index (list, 1)), ==, "echo");

    /* reserved and taken names */
    reserved = lrg_command_new ("rpc.ping", NULL, "x", cmd_add, NULL, NULL);
    g_assert_false (lrg_command_registry_register (registry, reserved, &error));
    g_clear_error (&error);
    g_clear_object (&reserved);
    reserved = lrg_command_new ("add", NULL, "x", cmd_add, NULL, NULL);
    g_assert_false (lrg_command_registry_register (registry, reserved, &error));
    g_clear_error (&error);
    g_assert_true (lrg_command_registry_unregister (registry, "fail"));
    g_assert_false (lrg_command_registry_unregister (registry, "fail"));

    g_ptr_array_unref (later.pending);
}

static void
on_event (LrgCommandRegistry *registry,
          const gchar        *topic,
          JsonNode           *data,
          guint64             seq,
          gpointer            user_data)
{
    guint64 *last = user_data;

    *last = seq;
}

static void
test_events (void)
{
    g_autoptr(LrgCommandRegistry) registry = make_registry (NULL);
    g_auto(GStrv) topics = NULL;
    guint64 last = 0;

    g_signal_connect (registry, "event", G_CALLBACK (on_event), &last);
    g_assert_cmpuint (lrg_command_registry_publish (registry, "combat", NULL), ==, 1);
    g_assert_cmpuint (lrg_command_registry_publish (registry, "messages", NULL), ==, 2);
    g_assert_cmpuint (last, ==, 2);
    g_assert_cmpuint (lrg_command_registry_get_event_seq (registry), ==, 2);
    topics = lrg_command_registry_list_topics (registry);
    g_assert_cmpstr (topics[0], ==, "combat");
    g_assert_cmpstr (topics[1], ==, "messages");
    g_assert_null (topics[2]);
    g_assert_true (lrg_command_topic_matches ("*", "quest"));
    g_assert_true (lrg_command_topic_matches ("quest.*", "quest.started"));
    g_assert_false (lrg_command_topic_matches ("quest.*", "combat"));
    g_assert_true (lrg_command_topic_matches ("combat", "combat"));
    g_assert_false (lrg_command_topic_matches ("combat", "combatx"));
}

/* ==========================================================================
 * The server and the client
 * ========================================================================== */

typedef struct
{
    gchar              *dir;
    gchar              *path;
    Later               later;
    LrgCommandRegistry *registry;
    LrgControlServer   *server;
    GThread            *thread;
    volatile gint       stop;
    GMutex              lock;  /* the server and the registry belong to the thread */
} Fixture;

static gpointer
server_thread (gpointer data)
{
    Fixture *fixture = data;

    while (!g_atomic_int_get (&fixture->stop))
    {
        g_mutex_lock (&fixture->lock);
        lrg_control_server_poll (fixture->server);
        g_mutex_unlock (&fixture->lock);
        g_usleep (1000);
    }
    return NULL;
}

static void
fixture_setup (Fixture       *fixture,
               gconstpointer  data)
{
    g_autoptr(GError) error = NULL;
    const gchar *token = data;

    use_short_runtime_dir ();
    fixture->dir = lrg_control_runtime_dir ("lrg-control-test");
    fixture->path = g_build_filename (fixture->dir, "game.sock", NULL);
    fixture->later.pending = g_ptr_array_new_with_free_func (g_object_unref);
    fixture->registry = make_registry (&fixture->later);
    fixture->server = lrg_control_server_new (fixture->registry);
    lrg_control_server_set_identity (fixture->server, "test 1.0", "game");
    lrg_control_server_set_token (fixture->server, token);
    g_assert_true (lrg_control_server_listen (fixture->server, fixture->path, &error));
    g_assert_no_error (error);
    g_mutex_init (&fixture->lock);
    fixture->thread = g_thread_new ("control-server", server_thread, fixture);
}

static void
fixture_teardown (Fixture       *fixture,
                  gconstpointer  data)
{
    g_atomic_int_set (&fixture->stop, 1);
    g_thread_join (fixture->thread);
    g_mutex_clear (&fixture->lock);
    g_clear_object (&fixture->server);
    g_ptr_array_unref (fixture->later.pending);
    g_clear_object (&fixture->registry);
    g_assert_false (g_file_test (fixture->path, G_FILE_TEST_EXISTS));
    g_free (fixture->path);
    g_free (fixture->dir);
}

static LrgControlClient *
connect_client (Fixture *fixture)
{
    LrgControlClient *client = lrg_control_client_new ();
    g_autoptr(GError) error = NULL;

    g_assert_true (lrg_control_client_connect (client, fixture->path, &error));
    g_assert_no_error (error);
    lrg_control_client_set_timeout (client, 5000);
    return client;
}

/* a raw exchange: sends @line, returns the next line */
static gchar *
raw_line (LrgControlClient *client,
          const gchar      *line)
{
    g_autoptr(GError) error = NULL;
    gchar *reply = lrg_control_client_call_line (client, line, &error);

    g_assert_no_error (error);
    return reply;
}

static void
test_server_permissions (Fixture       *fixture,
                         gconstpointer  data)
{
    struct stat st;
    g_auto(GStrv) names = NULL;

    g_assert_cmpint (g_stat (fixture->path, &st), ==, 0);
    g_assert_cmpint (st.st_mode & 0777, ==, 0600);
    g_assert_cmpint (g_stat (fixture->dir, &st), ==, 0);
    g_assert_cmpint (st.st_mode & 0077, ==, 0);
    g_assert_true (lrg_control_socket_is_live (fixture->path));
    names = lrg_control_list_instances (fixture->dir, TRUE);
    g_assert_cmpstr (names[0], ==, "game");
    g_assert_null (names[1]);

    /* a second server on a live socket is refused */
    {
        g_autoptr(LrgControlServer) other = lrg_control_server_new (fixture->registry);
        g_autoptr(GError) error = NULL;

        g_assert_false (lrg_control_server_listen (other, fixture->path, &error));
        g_assert_error (error, G_IO_ERROR, G_IO_ERROR_ADDRESS_IN_USE);
    }
}

static void
test_server_stale_socket (void)
{
    g_autofree gchar *dir = NULL;
    g_autofree gchar *path = NULL;
    g_autoptr(GSocket) dead = NULL;
    g_autoptr(GSocketAddress) address = NULL;
    g_autoptr(LrgCommandRegistry) registry = make_registry (NULL);
    g_autoptr(LrgControlServer) server = NULL;
    g_autoptr(GError) error = NULL;
    g_auto(GStrv) live = NULL;
    g_auto(GStrv) all = NULL;

    /* a socket file nobody listens on any more */
    use_short_runtime_dir ();
    dir = lrg_control_runtime_dir ("lrg-control-stale");
    path = g_build_filename (dir, "old.sock", NULL);
    g_mkdir_with_parents (dir, 0700);
    dead = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, &error);
    address = g_unix_socket_address_new (path);
    g_assert_true (g_socket_bind (dead, address, FALSE, &error));
    g_socket_close (dead, NULL);
    g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));
    g_assert_false (lrg_control_socket_is_live (path));
    live = lrg_control_list_instances (dir, TRUE);
    g_assert_null (live[0]);
    all = lrg_control_list_instances (dir, FALSE);
    g_assert_cmpstr (all[0], ==, "old");

    server = lrg_control_server_new (registry);
    g_assert_true (lrg_control_server_listen (server, path, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_control_server_is_listening (server));
    lrg_control_server_close (server);
    g_assert_false (g_file_test (path, G_FILE_TEST_EXISTS));
}

static void
test_server_text (Fixture       *fixture,
                  gconstpointer  data)
{
    g_autoptr(LrgControlClient) client = connect_client (fixture);
    g_autofree gchar *reply = NULL;

    reply = raw_line (client, "ping");
    g_assert_cmpstr (reply, ==, "OK pong");
    g_free (reply);
    reply = raw_line (client, "add 2 3");
    g_assert_cmpstr (reply, ==, "OK 5.0");
    g_free (reply);
    reply = raw_line (client, "fail");
    g_assert_cmpstr (reply, ==, "ERROR -32001 no thing");
    g_free (reply);
    reply = raw_line (client, "frobnicate");
    g_assert_true (g_str_has_prefix (reply, "ERROR -32601"));
    g_free (reply);
    reply = raw_line (client, "add x");
    g_assert_true (g_str_has_prefix (reply, "ERROR -32602"));
    g_free (reply);
    reply = raw_line (client, "help");
    g_assert_true (g_str_has_prefix (reply, "OK [{\"name\":\"add\""));
    g_free (reply);
    reply = raw_line (client, "help add");
    g_assert_nonnull (strstr (reply, "\"schema\""));
    g_free (reply);
    reply = raw_line (client, "discover");
    g_assert_nonnull (strstr (reply, "\"protocol\":\"lrg-control\""));
    g_assert_nonnull (strstr (reply, "\"instance\":\"game\""));
}

static void
test_server_jsonrpc (Fixture       *fixture,
                     gconstpointer  data)
{
    g_autoptr(LrgControlClient) client = connect_client (fixture);
    g_autoptr(JsonObject) params = json_object_new ();
    g_autoptr(JsonNode) result = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *reply = NULL;

    json_object_set_int_member (params, "a", 40);
    result = lrg_control_client_call (client, "add", params, &error);
    g_assert_no_error (error);
    g_assert_cmpfloat (json_node_get_double (result), ==, 42.0);
    g_clear_pointer (&result, json_node_unref);

    /* a command error keeps its code */
    result = lrg_control_client_call (client, "fail", NULL, &error);
    g_assert_null (result);
    g_assert_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_NOT_FOUND);
    g_clear_error (&error);
    result = lrg_control_client_call (client, "nothing.here", NULL, &error);
    g_assert_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_UNKNOWN_COMMAND);
    g_clear_error (&error);

    /* the built-ins */
    result = lrg_control_client_call (client, "rpc.ping", NULL, &error);
    g_assert_cmpstr (json_node_get_string (result), ==, "pong");
    g_clear_pointer (&result, json_node_unref);
    result = lrg_control_client_call (client, "rpc.discover", NULL, &error);
    g_assert_cmpint (json_object_get_int_member (json_node_get_object (result), "version"), ==,
                     LRG_CONTROL_PROTOCOL_VERSION);
    g_assert_cmpstr (json_object_get_string_member (json_node_get_object (result), "server"), ==, "test 1.0");
    g_clear_pointer (&result, json_node_unref);

    /* raw lines: a notification answers nothing; a batch answers once, in order */
    reply = raw_line (client, "{\"jsonrpc\":\"2.0\",\"method\":\"add\",\"params\":{\"a\":1}}\n"
                              "[{\"jsonrpc\":\"2.0\",\"id\":\"x\",\"method\":\"add\",\"params\":{\"a\":1}},"
                              "{\"method\":\"add\",\"params\":{\"a\":7}},"
                              "{\"id\":2,\"method\":\"fail\"}]");
    g_assert_cmpstr (reply, ==, "[{\"jsonrpc\":\"2.0\",\"id\":\"x\",\"result\":3.0},"
                                "{\"jsonrpc\":\"2.0\",\"id\":2,\"error\":{\"code\":-32001,\"message\":\"no thing\","
                                "\"data\":{\"method\":\"fail\"}}}]");
    g_free (reply);

    /* protocol errors */
    reply = raw_line (client, "{not json");
    g_assert_nonnull (strstr (reply, "-32700"));
    g_free (reply);
    reply = raw_line (client, "[]");
    g_assert_nonnull (strstr (reply, "-32600"));
    g_free (reply);
    reply = raw_line (client, "{\"id\":1,\"method\":\"add\",\"params\":[1,2]}");
    g_assert_nonnull (strstr (reply, "-32602"));
    g_free (reply);
    reply = raw_line (client, "{\"jsonrpc\":\"1.0\",\"id\":1,\"method\":\"add\"}");
    g_assert_nonnull (strstr (reply, "-32600"));
    g_free (reply);
    reply = raw_line (client, "{\"id\":1}");
    g_assert_nonnull (strstr (reply, "-32600"));
    g_free (reply);
    reply = raw_line (client, "{\"id\":7,\"method\":\"add\",\"params\":{\"a\":1,\"c\":2}}");
    g_assert_nonnull (strstr (reply, "-32602"));
    g_assert_nonnull (strstr (reply, "unknown parameter c"));
}

/* releases the server thread's pending "later" calls in a given order */
typedef struct
{
    Fixture *fixture;
    guint    count;
} Release;

static gpointer
release_thread (gpointer data)
{
    Release *release = data;
    Fixture *fixture = release->fixture;
    gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

    /* wait for both calls, then end the second first */
    for (;;)
    {
        guint n;

        g_mutex_lock (&fixture->lock);
        n = fixture->later.pending->len;
        if (n >= release->count)
        {
            JsonNode *two = json_node_new (JSON_NODE_VALUE);
            JsonNode *one = json_node_new (JSON_NODE_VALUE);

            json_node_set_int (two, 2);
            json_node_set_int (one, 1);
            lrg_command_call_return_value (g_ptr_array_index (fixture->later.pending, 1), two);
            lrg_command_call_return_value (g_ptr_array_index (fixture->later.pending, 0), one);
            g_mutex_unlock (&fixture->lock);
            break;
        }
        g_mutex_unlock (&fixture->lock);
        if (g_get_monotonic_time () > deadline)
            break;
        g_usleep (2000);
    }
    return NULL;
}

static void
test_server_async_order (Fixture       *fixture,
                         gconstpointer  data)
{
    g_autoptr(LrgControlClient) client = connect_client (fixture);
    g_autofree gchar *first = NULL;
    g_autofree gchar *second = NULL;
    g_autofree gchar *third = NULL;
    Release release = { fixture, 2 };
    GThread *thread;
    g_autoptr(GError) error = NULL;

    /* two pending calls and a quick one: replies keep the request order */
    thread = g_thread_new ("release", release_thread, &release);
    first = lrg_control_client_call_line (client, "later\nlater\nadd 1 1", &error);
    g_assert_no_error (error);
    g_thread_join (thread);
    g_assert_cmpstr (first, ==, "OK 1");
    second = lrg_control_client_call_line (client, "", &error);
    /* the empty line answers nothing: these are the queued replies */
    g_assert_cmpstr (second, ==, "OK 2");
    third = lrg_control_client_call_line (client, "", &error);
    g_assert_cmpstr (third, ==, "OK 2.0");
}

static void
test_server_cancel_on_hangup (Fixture       *fixture,
                              gconstpointer  data)
{
    g_autoptr(LrgControlClient) client = connect_client (fixture);
    g_autoptr(GSocket) unused = NULL;
    gint64 deadline;
    gboolean cancelled = FALSE;

    /* a pending call whose client goes away is cancelled */
    {
        g_autoptr(GError) error = NULL;
        g_autofree gchar *reply = NULL;

        lrg_control_client_set_timeout (client, 200);
        reply = lrg_control_client_call_line (client, "later", &error);
        g_assert_null (reply);
        g_assert_error (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT);
    }
    lrg_control_client_close (client);
    deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;
    while (g_get_monotonic_time () < deadline)
    {
        g_mutex_lock (&fixture->lock);
        cancelled = fixture->later.cancelled_seen;
        g_mutex_unlock (&fixture->lock);
        if (cancelled)
            break;
        g_usleep (2000);
    }
    g_assert_true (cancelled);
}

static void
test_server_token (Fixture       *fixture,
                   gconstpointer  data)
{
    g_autoptr(LrgControlClient) client = connect_client (fixture);
    g_autoptr(JsonObject) params = json_object_new ();
    g_autoptr(JsonNode) result = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *reply = NULL;

    /* ping and discover work, commands do not */
    reply = raw_line (client, "ping");
    g_assert_cmpstr (reply, ==, "OK pong");
    g_free (reply);
    reply = raw_line (client, "add 1 2");
    g_assert_true (g_str_has_prefix (reply, "ERROR -32005"));
    g_free (reply);
    result = lrg_control_client_call (client, "add", NULL, &error);
    g_assert_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_UNAUTHORIZED);
    g_clear_error (&error);
    result = lrg_control_client_call (client, "rpc.discover", NULL, &error);
    g_assert_true (json_object_get_boolean_member (json_node_get_object (result), "auth_required"));
    g_clear_pointer (&result, json_node_unref);

    /* a wrong token, then the right one */
    json_object_set_string_member (params, "token", "wrong");
    result = lrg_control_client_call (client, "auth", params, &error);
    g_assert_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_UNAUTHORIZED);
    g_clear_error (&error);
    reply = raw_line (client, "auth s3cret");
    g_assert_cmpstr (reply, ==, "OK true");
    g_free (reply);
    reply = raw_line (client, "add 1 2");
    g_assert_cmpstr (reply, ==, "OK 3.0");
}

static void
test_server_events (Fixture       *fixture,
                    gconstpointer  data)
{
    g_autoptr(LrgControlClient) watcher = connect_client (fixture);
    g_autoptr(LrgControlClient) caller = connect_client (fixture);
    g_autoptr(LrgControlClient) text = connect_client (fixture);
    const gchar *topics[] = { "combat", NULL };
    g_autoptr(GError) error = NULL;
    g_autoptr(JsonNode) event = NULL;
    g_autofree gchar *reply = NULL;
    JsonObject *object;

    g_assert_true (lrg_control_client_subscribe (watcher, topics, &error));
    g_assert_no_error (error);
    reply = raw_line (text, "subscribe com*");
    g_assert_true (g_str_has_prefix (reply, "OK {\"subscribed\":true"));
    g_free (reply);

    /* a topic the watcher did not ask for does not reach it */
    reply = raw_line (caller, "publish messages");
    g_free (reply);
    reply = raw_line (caller, "publish combat");
    g_assert_cmpstr (reply, ==, "OK null");

    event = lrg_control_client_read_event (watcher, 3000, &error);
    g_assert_no_error (error);
    g_assert_nonnull (event);
    object = json_node_get_object (event);
    g_assert_cmpstr (json_object_get_string_member (object, "event"), ==, "combat");
    g_assert_cmpint (json_object_get_int_member (object, "seq"), ==, 2);
    g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (object, "data"), "text"),
                     ==, "hello");
    g_clear_pointer (&event, json_node_unref);
    event = lrg_control_client_read_event (watcher, 100, &error);
    g_assert_null (event);
    g_assert_no_error (error);

    /* the text subscriber sees "EVENT combat {...}" */
    event = lrg_control_client_read_event (text, 3000, &error);
    g_assert_nonnull (event);
    g_assert_cmpstr (json_object_get_string_member (json_node_get_object (event), "event"), ==, "combat");

    /* unsubscribed: nothing more */
    reply = raw_line (text, "unsubscribe");
    g_assert_cmpstr (reply, ==, "OK {\"subscribed\":false}");
    g_free (reply);
    reply = raw_line (caller, "publish combat");
    g_clear_pointer (&event, json_node_unref);
    event = lrg_control_client_read_event (text, 200, &error);
    g_assert_null (event);
}

static void
test_server_long_path (void)
{
    g_autoptr(LrgCommandRegistry) registry = make_registry (NULL);
    g_autoptr(LrgControlServer) server = lrg_control_server_new (registry);
    g_autoptr(GError) error = NULL;
    g_autofree gchar *name = g_strnfill (120, 'x');
    g_autofree gchar *path = g_build_filename (runtime_dir, name, NULL);

    g_assert_false (lrg_control_server_listen (server, path, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FILENAME_TOO_LONG);
}

int
main (int    argc,
      char **argv)
{
    int status;

    g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    runtime_dir = g_dir_make_tmp ("lrgctl-XXXXXX", NULL);
    g_assert_nonnull (runtime_dir);

    g_test_add_func ("/control/param/schema", test_param_schema);
    g_test_add_func ("/control/param/check", test_check_params);
    g_test_add_func ("/control/param/int64", test_int64_ids);
    g_test_add_func ("/control/param/arguments", test_parse_arguments);
    g_test_add_func ("/control/registry/invoke", test_registry_invoke);
    g_test_add_func ("/control/registry/events", test_events);
    g_test_add_func ("/control/server/stale-socket", test_server_stale_socket);
    g_test_add ("/control/server/permissions", Fixture, NULL, fixture_setup, test_server_permissions,
                fixture_teardown);
    g_test_add ("/control/server/text", Fixture, NULL, fixture_setup, test_server_text, fixture_teardown);
    g_test_add ("/control/server/jsonrpc", Fixture, NULL, fixture_setup, test_server_jsonrpc, fixture_teardown);
    g_test_add ("/control/server/async-order", Fixture, NULL, fixture_setup, test_server_async_order,
                fixture_teardown);
    g_test_add ("/control/server/cancel-on-hangup", Fixture, NULL, fixture_setup, test_server_cancel_on_hangup,
                fixture_teardown);
    g_test_add ("/control/server/token", Fixture, "s3cret", fixture_setup, test_server_token, fixture_teardown);
    g_test_add ("/control/server/events", Fixture, NULL, fixture_setup, test_server_events, fixture_teardown);

    g_test_add_func ("/control/server/long-path", test_server_long_path);

    status = g_test_run ();
    {
        g_autofree gchar *a = g_build_filename (runtime_dir, "lrg-control-test", NULL);
        g_autofree gchar *b = g_build_filename (runtime_dir, "lrg-control-stale", NULL);

        g_rmdir (a);
        g_rmdir (b);
    }
    g_rmdir (runtime_dir);
    g_free (runtime_dir);
    return status;
}
