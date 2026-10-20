/* lrg-control-server.c - A game's control socket (text + JSON-RPC 2.0)
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * The server never blocks and never needs a main loop: every
 * lrg_control_server_poll() accepts what waits on the listening socket,
 * reads what each connection sent (non-blocking), runs the complete
 * lines, turns finished calls into reply lines in request order, and
 * writes as much as the peer takes.  A call that completes later (an
 * %LRG_COMMAND_FLAG_ASYNC command) keeps its place in the connection's
 * reply queue until it does; events are written as they are published,
 * so the events a request causes go out before its reply.
 */

#include "config.h"
#include "lrg-control-server.h"

#include <errno.h>
#include <string.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <glib/gstdio.h>

#ifdef __linux__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/socket.h>
#endif
#ifdef G_OS_UNIX
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

/**
 * SECTION:lrg-control-server
 * @title: LrgControlServer
 * @short_description: A Unix control socket speaking text and JSON-RPC
 *
 * See lrg-control-server.h and docs/modules/control/index.org.
 */

#define READ_CHUNK (16 * 1024)

typedef struct _Client Client;
typedef struct _Entry Entry;

/* one reply slot in a connection's queue, in request order */
struct _Entry
{
    gboolean        text;          /* the text dialect answers */
    gboolean        notification;  /* JSON-RPC without id: nothing is sent */
    JsonNode       *id;            /* owned, nullable */
    LrgCommandCall *call;          /* owned, nullable */
    gchar          *ready;         /* owned: a reply made at once */
    GPtrArray      *batch;         /* owned Entry list of a batch, nullable */
};

struct _Client
{
    LrgControlServer *server;      /* borrowed */
    GSocket          *socket;      /* owned */
    guint             number;
    GString          *in;
    GString          *out;
    gboolean          authed;
    gboolean          closing;     /* close once the output is written */
    gboolean          dead;        /* drop at the end of this poll */
    gboolean          text_events; /* subscribed from the text dialect */
    GPtrArray        *patterns;    /* owned strings, NULL: not subscribed */
    GQueue           *entries;     /* Entry */
    gint              pid;
    gint              uid;
};

struct _LrgControlServer
{
    GObject             parent_instance;

    LrgCommandRegistry *registry;    /* owned */
    gulong              event_handler;
    gchar              *token;
    gchar              *server_name;
    gchar              *instance;
    JsonObject         *info;        /* owned, nullable */
    gchar              *path;
    GSocket            *listener;    /* owned, nullable */
    GPtrArray          *clients;     /* Client */
    guint               next_number;
    GSource            *source;      /* owned, nullable */
    gboolean            polling;
};

G_DEFINE_TYPE (LrgControlServer, lrg_control_server, G_TYPE_OBJECT)

/* ------------------------------------------------------------------ */
/* entries                                                              */

static void
entry_free (gpointer data)
{
    Entry *entry = data;

    if (entry == NULL)
        return;
    if (entry->call != NULL)
    {
        /* a call nobody will read: let its handler stop */
        if (!lrg_command_call_is_done (entry->call))
            lrg_command_call_cancel (entry->call);
        g_object_unref (entry->call);
    }
    g_clear_pointer (&entry->id, json_node_unref);
    g_free (entry->ready);
    g_clear_pointer (&entry->batch, g_ptr_array_unref);
    g_free (entry);
}

static Entry *
entry_new (gboolean  text,
           JsonNode *id)
{
    Entry *entry;

    entry = g_new0 (Entry, 1);
    entry->text = text;
    if (id != NULL)
        entry->id = json_node_copy (id);
    return entry;
}

static gboolean
entry_is_done (Entry *entry)
{
    guint i;

    if (entry->batch != NULL)
    {
        for (i = 0; i < entry->batch->len; i++)
            if (!entry_is_done (g_ptr_array_index (entry->batch, i)))
                return FALSE;
        return TRUE;
    }
    if (entry->ready != NULL)
        return TRUE;
    return entry->call == NULL || lrg_command_call_is_done (entry->call);
}

/* ------------------------------------------------------------------ */
/* JSON helpers                                                         */

static gchar *
node_to_line (JsonNode *node)
{
    g_autoptr(JsonGenerator) generator = json_generator_new ();

    json_generator_set_pretty (generator, FALSE);
    json_generator_set_root (generator, node);
    return json_generator_to_data (generator, NULL);
}

static JsonNode *
response_node (JsonNode     *id,
               JsonNode     *result,
               const GError *error,
               const gchar  *method)
{
    g_autoptr(JsonBuilder) builder = json_builder_new ();

    json_builder_begin_object (builder);
    json_builder_set_member_name (builder, "jsonrpc");
    json_builder_add_string_value (builder, "2.0");
    json_builder_set_member_name (builder, "id");
    json_builder_add_value (builder, id != NULL ? json_node_copy (id) : json_node_new (JSON_NODE_NULL));
    if (error != NULL)
    {
        json_builder_set_member_name (builder, "error");
        json_builder_begin_object (builder);
        json_builder_set_member_name (builder, "code");
        json_builder_add_int_value (builder, lrg_command_error_to_jsonrpc_code (error));
        json_builder_set_member_name (builder, "message");
        json_builder_add_string_value (builder, error->message);
        if (method != NULL)
        {
            json_builder_set_member_name (builder, "data");
            json_builder_begin_object (builder);
            json_builder_set_member_name (builder, "method");
            json_builder_add_string_value (builder, method);
            json_builder_end_object (builder);
        }
        json_builder_end_object (builder);
    }
    else
    {
        json_builder_set_member_name (builder, "result");
        json_builder_add_value (builder, result != NULL ? json_node_copy (result)
                                                        : json_node_new (JSON_NODE_NULL));
    }
    json_builder_end_object (builder);
    return json_builder_get_root (builder);
}

/* a protocol-level error (-32700, -32600) with its own code */
static gchar *
protocol_error_line (JsonNode    *id,
                     gint         code,
                     const gchar *message)
{
    g_autoptr(JsonBuilder) builder = json_builder_new ();
    g_autoptr(JsonNode) root = NULL;

    json_builder_begin_object (builder);
    json_builder_set_member_name (builder, "jsonrpc");
    json_builder_add_string_value (builder, "2.0");
    json_builder_set_member_name (builder, "id");
    json_builder_add_value (builder, id != NULL ? json_node_copy (id) : json_node_new (JSON_NODE_NULL));
    json_builder_set_member_name (builder, "error");
    json_builder_begin_object (builder);
    json_builder_set_member_name (builder, "code");
    json_builder_add_int_value (builder, code);
    json_builder_set_member_name (builder, "message");
    json_builder_add_string_value (builder, message);
    json_builder_end_object (builder);
    json_builder_end_object (builder);
    root = json_builder_get_root (builder);
    return node_to_line (root);
}

/* the reply line of a finished entry (NULL: nothing to send) */
static gchar *
entry_reply (Entry *entry)
{
    if (entry->batch != NULL)
    {
        g_autoptr(JsonBuilder) builder = NULL;
        g_autoptr(JsonNode) root = NULL;
        guint sent;
        guint i;

        builder = json_builder_new ();
        json_builder_begin_array (builder);
        sent = 0;
        for (i = 0; i < entry->batch->len; i++)
        {
            Entry *item = g_ptr_array_index (entry->batch, i);
            g_autofree gchar *line = NULL;
            g_autoptr(JsonParser) parser = NULL;

            line = entry_reply (item);
            if (line == NULL)
                continue;
            parser = json_parser_new ();
            if (json_parser_load_from_data (parser, line, -1, NULL))
            {
                json_builder_add_value (builder, json_node_copy (json_parser_get_root (parser)));
                sent++;
            }
        }
        json_builder_end_array (builder);
        if (sent == 0)
            return NULL;
        root = json_builder_get_root (builder);
        return node_to_line (root);
    }
    if (entry->ready != NULL)
        return entry->notification ? NULL : g_strdup (entry->ready);
    if (entry->notification || entry->call == NULL)
        return NULL;

    if (entry->text)
    {
        const GError *error = lrg_command_call_get_error (entry->call);

        if (error != NULL)
            return g_strdup_printf ("ERROR %d %s", lrg_command_error_to_jsonrpc_code (error),
                                    error->message);
        {
            g_autofree gchar *json = node_to_line (lrg_command_call_get_result (entry->call));

            return g_strdup_printf ("OK %s", json);
        }
    }
    else
    {
        g_autoptr(JsonNode) response = NULL;

        response = response_node (entry->id, lrg_command_call_get_result (entry->call),
                                  lrg_command_call_get_error (entry->call),
                                  lrg_command_call_get_name (entry->call));
        return node_to_line (response);
    }
}

/* ------------------------------------------------------------------ */
/* connections                                                          */

static void
client_free (gpointer data)
{
    Client *client = data;

    g_queue_free_full (client->entries, entry_free);
    if (client->socket != NULL)
    {
        g_socket_close (client->socket, NULL);
        g_object_unref (client->socket);
    }
    g_string_free (client->in, TRUE);
    g_string_free (client->out, TRUE);
    g_clear_pointer (&client->patterns, g_ptr_array_unref);
    g_free (client);
}

static void
client_queue_line (Client      *client,
                   const gchar *line)
{
    if (client->dead)
        return;
    g_string_append (client->out, line);
    g_string_append_c (client->out, '\n');
    if (client->out->len > LRG_CONTROL_MAX_PENDING_OUTPUT)
    {
        g_debug ("control: connection %u does not read its output; dropped", client->number);
        client->dead = TRUE;
    }
}

/* finished entries at the head of the queue become output */
static void
client_flush_entries (Client *client)
{
    while (!g_queue_is_empty (client->entries))
    {
        Entry *entry = g_queue_peek_head (client->entries);
        g_autofree gchar *line = NULL;

        if (!entry_is_done (entry))
            break;
        g_queue_pop_head (client->entries);
        line = entry_reply (entry);
        if (line != NULL)
            client_queue_line (client, line);
        entry_free (entry);
    }
}

/* writes what the peer takes now */
static void
client_write (Client *client)
{
    while (client->out->len > 0 && !client->dead)
    {
        g_autoptr(GError) error = NULL;
        gssize sent;

        sent = g_socket_send_with_blocking (client->socket, client->out->str, client->out->len,
                                            FALSE, NULL, &error);
        if (sent < 0)
        {
            if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
                client->dead = TRUE;
            return;
        }
        g_string_erase (client->out, 0, sent);
    }
    if (client->closing && client->out->len == 0 && g_queue_is_empty (client->entries))
        client->dead = TRUE;
}

/* ------------------------------------------------------------------ */
/* built-in methods                                                     */

static JsonNode *
string_node (const gchar *text)
{
    JsonNode *node = json_node_new (JSON_NODE_VALUE);

    json_node_set_string (node, text);
    return node;
}

static JsonNode *
discover_node (LrgControlServer *self,
               Client           *client)
{
    g_autoptr(JsonBuilder) builder = json_builder_new ();
    g_autoptr(GPtrArray) commands = NULL;
    g_auto(GStrv) topics = NULL;
    static const gchar *const builtins[] = {
        "auth", "events.subscribe", "events.unsubscribe", "rpc.describe", "rpc.discover", "rpc.ping", NULL
    };
    guint i;

    commands = lrg_command_registry_list (self->registry, FALSE);
    topics = lrg_command_registry_list_topics (self->registry);
    json_builder_begin_object (builder);
    json_builder_set_member_name (builder, "protocol");
    json_builder_add_string_value (builder, "lrg-control");
    json_builder_set_member_name (builder, "version");
    json_builder_add_int_value (builder, LRG_CONTROL_PROTOCOL_VERSION);
    json_builder_set_member_name (builder, "server");
    json_builder_add_string_value (builder, self->server_name != NULL ? self->server_name : "libregnum");
    json_builder_set_member_name (builder, "instance");
    json_builder_add_string_value (builder, self->instance != NULL ? self->instance : "");
    json_builder_set_member_name (builder, "auth_required");
    json_builder_add_boolean_value (builder, self->token != NULL);
    json_builder_set_member_name (builder, "caller");
    json_builder_begin_object (builder);
    json_builder_set_member_name (builder, "pid");
    json_builder_add_int_value (builder, client->pid);
    json_builder_set_member_name (builder, "connection");
    json_builder_add_int_value (builder, client->number);
    json_builder_set_member_name (builder, "authed");
    json_builder_add_boolean_value (builder, client->authed);
    json_builder_end_object (builder);
    if (self->info != NULL)
    {
        g_autoptr(JsonNode) info = json_node_new (JSON_NODE_OBJECT);

        json_node_set_object (info, self->info);
        json_builder_set_member_name (builder, "info");
        json_builder_add_value (builder, json_node_copy (info));
    }
    json_builder_set_member_name (builder, "methods");
    json_builder_begin_array (builder);
    for (i = 0; builtins[i] != NULL; i++)
        json_builder_add_string_value (builder, builtins[i]);
    for (i = 0; i < commands->len; i++)
        json_builder_add_string_value (builder, lrg_command_get_name (g_ptr_array_index (commands, i)));
    json_builder_end_array (builder);
    json_builder_set_member_name (builder, "events");
    json_builder_begin_array (builder);
    for (i = 0; topics[i] != NULL; i++)
        json_builder_add_string_value (builder, topics[i]);
    json_builder_end_array (builder);
    json_builder_end_object (builder);
    return json_builder_get_root (builder);
}

/* what `help` lists: name, group and summary of every command */
static JsonNode *
help_node (LrgControlServer *self)
{
    g_autoptr(GPtrArray) commands = NULL;
    g_autoptr(JsonBuilder) builder = json_builder_new ();
    guint i;

    commands = lrg_command_registry_list (self->registry, FALSE);
    json_builder_begin_array (builder);
    for (i = 0; i < commands->len; i++)
    {
        LrgCommand *command = g_ptr_array_index (commands, i);

        json_builder_begin_object (builder);
        json_builder_set_member_name (builder, "name");
        json_builder_add_string_value (builder, lrg_command_get_name (command));
        json_builder_set_member_name (builder, "group");
        json_builder_add_string_value (builder, lrg_command_get_group (command) != NULL ?
                                       lrg_command_get_group (command) : "");
        json_builder_set_member_name (builder, "summary");
        json_builder_add_string_value (builder, lrg_command_get_summary (command) != NULL ?
                                       lrg_command_get_summary (command) : "");
        json_builder_end_object (builder);
    }
    json_builder_end_array (builder);
    return json_builder_get_root (builder);
}

static JsonNode *
subscribe (Client             *client,
           const gchar *const *patterns,
           gboolean            add,
           gboolean            text)
{
    g_autoptr(JsonBuilder) builder = json_builder_new ();
    guint i;

    if (client->patterns == NULL || !add)
    {
        g_clear_pointer (&client->patterns, g_ptr_array_unref);
        client->patterns = g_ptr_array_new_with_free_func (g_free);
    }
    if (patterns == NULL || patterns[0] == NULL)
        g_ptr_array_add (client->patterns, g_strdup ("*"));
    else
        for (i = 0; patterns[i] != NULL; i++)
            g_ptr_array_add (client->patterns, g_strdup (patterns[i]));
    client->text_events = text;

    json_builder_begin_object (builder);
    json_builder_set_member_name (builder, "subscribed");
    json_builder_add_boolean_value (builder, TRUE);
    json_builder_set_member_name (builder, "events");
    json_builder_begin_array (builder);
    for (i = 0; i < client->patterns->len; i++)
        json_builder_add_string_value (builder, g_ptr_array_index (client->patterns, i));
    json_builder_end_array (builder);
    json_builder_end_object (builder);
    return json_builder_get_root (builder);
}

static JsonNode *
unsubscribe (Client *client)
{
    g_autoptr(JsonBuilder) builder = json_builder_new ();

    g_clear_pointer (&client->patterns, g_ptr_array_unref);
    json_builder_begin_object (builder);
    json_builder_set_member_name (builder, "subscribed");
    json_builder_add_boolean_value (builder, FALSE);
    json_builder_end_object (builder);
    return json_builder_get_root (builder);
}

/* a constant-time token comparison (no timing hints on the secret) */
static gboolean
token_equal (const gchar *a,
             const gchar *b)
{
    gsize la;
    gsize lb;
    gsize i;
    guint diff;

    if (a == NULL || b == NULL)
        return FALSE;
    la = strlen (a);
    lb = strlen (b);
    diff = (guint)(la ^ lb);
    for (i = 0; i < la && i < lb; i++)
        diff |= (guint)(a[i] ^ b[i]);
    return diff == 0;
}

/*
 * runs a built-in method; returns FALSE when @method is not one.
 * On TRUE, *result or *error is set.
 */
static gboolean
run_builtin (LrgControlServer  *self,
             Client            *client,
             const gchar       *method,
             JsonObject        *params,
             JsonNode         **result,
             GError           **error)
{
    *result = NULL;
    if (strcmp (method, "rpc.ping") == 0)
    {
        *result = string_node ("pong");
        return TRUE;
    }
    if (strcmp (method, "rpc.discover") == 0)
    {
        *result = discover_node (self, client);
        return TRUE;
    }
    if (strcmp (method, "auth") == 0)
    {
        const gchar *token = NULL;

        if (params != NULL && json_object_has_member (params, "token") &&
            JSON_NODE_HOLDS_VALUE (json_object_get_member (params, "token")) &&
            json_node_get_value_type (json_object_get_member (params, "token")) == G_TYPE_STRING)
            token = json_object_get_string_member (params, "token");
        if (self->token == NULL)
        {
            client->authed = TRUE;
            *result = json_node_new (JSON_NODE_VALUE);
            json_node_set_boolean (*result, TRUE);
            return TRUE;
        }
        if (token == NULL)
        {
            g_set_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_INVALID_PARAMS,
                         "auth: missing required parameter token");
            return TRUE;
        }
        if (!token_equal (token, self->token))
        {
            g_set_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_UNAUTHORIZED,
                         "auth: wrong token");
            return TRUE;
        }
        client->authed = TRUE;
        *result = json_node_new (JSON_NODE_VALUE);
        json_node_set_boolean (*result, TRUE);
        return TRUE;
    }

    /* --- everything below needs the token --- */
    if (self->token != NULL && !client->authed &&
        (strcmp (method, "rpc.describe") == 0 || g_str_has_prefix (method, "events.")))
    {
        g_set_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_UNAUTHORIZED,
                     "%s: authenticate first (auth token=...)", method);
        return TRUE;
    }
    if (strcmp (method, "rpc.describe") == 0)
    {
        const gchar *name = NULL;
        LrgCommand *command;

        if (params != NULL && json_object_has_member (params, "method") &&
            JSON_NODE_HOLDS_VALUE (json_object_get_member (params, "method")))
            name = json_object_get_string_member (params, "method");
        if (name == NULL)
        {
            *result = lrg_command_registry_describe (self->registry);
            return TRUE;
        }
        command = lrg_command_registry_lookup (self->registry, name);
        if (command == NULL)
        {
            g_set_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_NOT_FOUND,
                         "rpc.describe: no method %s", name);
            return TRUE;
        }
        *result = lrg_command_describe (command);
        return TRUE;
    }
    if (strcmp (method, "events.subscribe") == 0)
    {
        g_autoptr(GPtrArray) patterns = g_ptr_array_new ();
        gboolean add = FALSE;

        if (params != NULL && json_object_has_member (params, "events"))
        {
            JsonNode *events = json_object_get_member (params, "events");

            if (JSON_NODE_HOLDS_ARRAY (events))
            {
                JsonArray *array = json_node_get_array (events);
                guint i;

                for (i = 0; i < json_array_get_length (array); i++)
                {
                    JsonNode *item = json_array_get_element (array, i);

                    if (!JSON_NODE_HOLDS_VALUE (item) || json_node_get_value_type (item) != G_TYPE_STRING)
                    {
                        g_set_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_INVALID_PARAMS,
                                     "events.subscribe: events must be an array of strings");
                        return TRUE;
                    }
                    g_ptr_array_add (patterns, (gpointer)json_node_get_string (item));
                }
            }
            else if (!JSON_NODE_HOLDS_NULL (events))
            {
                g_set_error (error, LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_INVALID_PARAMS,
                             "events.subscribe: events must be an array of strings");
                return TRUE;
            }
        }
        if (params != NULL && json_object_has_member (params, "add"))
            add = json_object_get_boolean_member (params, "add");
        g_ptr_array_add (patterns, NULL);
        *result = subscribe (client, (const gchar *const *)patterns->pdata, add, FALSE);
        return TRUE;
    }
    if (strcmp (method, "events.unsubscribe") == 0)
    {
        *result = unsubscribe (client);
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* requests                                                             */

/* one JSON-RPC request object -> an entry */
static Entry *
handle_request (LrgControlServer *self,
                Client           *client,
                JsonNode         *request)
{
    JsonObject *object;
    JsonNode *id = NULL;
    JsonNode *params_node;
    JsonObject *params = NULL;
    const gchar *method;
    Entry *entry;
    JsonNode *result = NULL;
    GError *error = NULL;
    gboolean notification;

    if (!JSON_NODE_HOLDS_OBJECT (request))
    {
        entry = entry_new (FALSE, NULL);
        entry->ready = protocol_error_line (NULL, -32600, "not a request object");
        return entry;
    }
    object = json_node_get_object (request);
    notification = !json_object_has_member (object, "id");
    if (!notification)
    {
        id = json_object_get_member (object, "id");
        if (!JSON_NODE_HOLDS_NULL (id) &&
            !(JSON_NODE_HOLDS_VALUE (id) &&
              (json_node_get_value_type (id) == G_TYPE_STRING ||
               json_node_get_value_type (id) == G_TYPE_INT64)))
        {
            entry = entry_new (FALSE, NULL);
            entry->ready = protocol_error_line (NULL, -32600, "id must be a string, an integer or null");
            return entry;
        }
    }
    entry = entry_new (FALSE, id);
    entry->notification = notification;

    if (json_object_has_member (object, "jsonrpc"))
    {
        JsonNode *version = json_object_get_member (object, "jsonrpc");

        if (!JSON_NODE_HOLDS_VALUE (version) || json_node_get_value_type (version) != G_TYPE_STRING ||
            strcmp (json_node_get_string (version), "2.0") != 0)
        {
            entry->ready = protocol_error_line (id, -32600, "jsonrpc must be \"2.0\"");
            return entry;
        }
    }
    if (!json_object_has_member (object, "method") ||
        !JSON_NODE_HOLDS_VALUE (json_object_get_member (object, "method")) ||
        json_node_get_value_type (json_object_get_member (object, "method")) != G_TYPE_STRING)
    {
        entry->ready = protocol_error_line (id, -32600, "a request needs a method");
        return entry;
    }
    method = json_object_get_string_member (object, "method");
    params_node = json_object_has_member (object, "params") ? json_object_get_member (object, "params") : NULL;
    if (params_node != NULL && !JSON_NODE_HOLDS_NULL (params_node))
    {
        if (!JSON_NODE_HOLDS_OBJECT (params_node))
        {
            g_autoptr(JsonNode) response = NULL;
            g_autoptr(GError) perror = g_error_new (LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_INVALID_PARAMS,
                                                    "%s: params must be an object", method);

            response = response_node (id, NULL, perror, method);
            entry->ready = node_to_line (response);
            return entry;
        }
        params = json_node_get_object (params_node);
    }

    /* --- the built-in methods --- */
    if (run_builtin (self, client, method, params, &result, &error))
    {
        g_autoptr(JsonNode) response = response_node (id, result, error, method);

        entry->ready = node_to_line (response);
        g_clear_pointer (&result, json_node_unref);
        g_clear_error (&error);
        return entry;
    }

    /* --- the token --- */
    if (self->token != NULL && !client->authed)
    {
        g_autoptr(JsonNode) response = NULL;
        g_autoptr(GError) aerror = g_error_new (LRG_COMMAND_ERROR, LRG_COMMAND_ERROR_UNAUTHORIZED,
                                                "%s: authenticate first (auth token=...)", method);

        response = response_node (id, NULL, aerror, method);
        entry->ready = node_to_line (response);
        return entry;
    }

    /* --- a command --- */
    entry->call = lrg_command_registry_invoke (self->registry, method, params, "socket");
    return entry;
}

/* a text-dialect line -> an entry */
static Entry *
handle_text (LrgControlServer *self,
             Client           *client,
             const gchar      *line)
{
    g_auto(GStrv) argv = NULL;
    Entry *entry;
    gint argc;

    entry = entry_new (TRUE, NULL);
    if (!g_shell_parse_argv (line, &argc, &argv, NULL))
    {
        entry->ready = g_strdup ("ERROR -32602 cannot read the command (check the quotes)");
        return entry;
    }

    if (strcmp (argv[0], "ping") == 0)
    {
        entry->ready = g_strdup ("OK pong");
        return entry;
    }
    if (strcmp (argv[0], "auth") == 0)
    {
        g_autoptr(JsonObject) params = json_object_new ();
        JsonNode *result = NULL;
        GError *error = NULL;

        if (argc > 1)
            json_object_set_string_member (params, "token", argv[1]);
        run_builtin (self, client, "auth", params, &result, &error);
        if (error != NULL)
            entry->ready = g_strdup_printf ("ERROR %d %s", lrg_command_error_to_jsonrpc_code (error),
                                            error->message);
        else
            entry->ready = g_strdup ("OK true");
        g_clear_pointer (&result, json_node_unref);
        g_clear_error (&error);
        return entry;
    }
    if (strcmp (argv[0], "discover") == 0)
    {
        g_autoptr(JsonNode) node = discover_node (self, client);
        g_autofree gchar *json = node_to_line (node);

        entry->ready = g_strdup_printf ("OK %s", json);
        return entry;
    }
    if (self->token != NULL && !client->authed)
    {
        entry->ready = g_strdup ("ERROR -32005 authenticate first (auth TOKEN)");
        return entry;
    }
    if (strcmp (argv[0], "help") == 0)
    {
        g_autoptr(JsonNode) node = NULL;
        g_autofree gchar *json = NULL;

        if (argc > 1)
        {
            LrgCommand *command = lrg_command_registry_lookup (self->registry, argv[1]);

            if (command == NULL)
            {
                entry->ready = g_strdup_printf ("ERROR -32601 unknown command '%s'", argv[1]);
                return entry;
            }
            node = lrg_command_describe (command);
        }
        else
        {
            node = help_node (self);
        }
        json = node_to_line (node);
        entry->ready = g_strdup_printf ("OK %s", json);
        return entry;
    }
    if (strcmp (argv[0], "subscribe") == 0)
    {
        g_autoptr(JsonNode) node = subscribe (client, (const gchar *const *)(argv + 1), FALSE, TRUE);
        g_autofree gchar *json = node_to_line (node);

        entry->ready = g_strdup_printf ("OK %s", json);
        return entry;
    }
    if (strcmp (argv[0], "unsubscribe") == 0)
    {
        g_autoptr(JsonNode) node = unsubscribe (client);
        g_autofree gchar *json = node_to_line (node);

        entry->ready = g_strdup_printf ("OK %s", json);
        return entry;
    }

    entry->call = lrg_command_registry_invoke_line (self->registry, line, "socket");
    return entry;
}

static void
handle_line (LrgControlServer *self,
             Client           *client,
             const gchar      *line)
{
    const gchar *p;

    for (p = line; *p == ' ' || *p == '\t'; p++)
        ;
    if (*p == '\0')
        return;

    if (*p == '{' || *p == '[')
    {
        g_autoptr(JsonParser) parser = json_parser_new ();
        JsonNode *root;

        if (!json_parser_load_from_data (parser, p, -1, NULL) ||
            (root = json_parser_get_root (parser)) == NULL)
        {
            Entry *entry = entry_new (FALSE, NULL);

            entry->ready = protocol_error_line (NULL, -32700, "parse error: not JSON");
            g_queue_push_tail (client->entries, entry);
            return;
        }
        if (JSON_NODE_HOLDS_ARRAY (root))
        {
            JsonArray *array = json_node_get_array (root);
            Entry *batch;
            guint i;

            batch = entry_new (FALSE, NULL);
            if (json_array_get_length (array) == 0)
            {
                batch->ready = protocol_error_line (NULL, -32600, "an empty batch");
                g_queue_push_tail (client->entries, batch);
                return;
            }
            batch->batch = g_ptr_array_new_with_free_func (entry_free);
            for (i = 0; i < json_array_get_length (array); i++)
                g_ptr_array_add (batch->batch,
                                 handle_request (self, client, json_array_get_element (array, i)));
            g_queue_push_tail (client->entries, batch);
            return;
        }
        g_queue_push_tail (client->entries, handle_request (self, client, root));
        return;
    }
    g_queue_push_tail (client->entries, handle_text (self, client, p));
}

/* reads what is there; runs complete lines */
static guint
client_read (LrgControlServer *self,
             Client           *client)
{
    gchar buffer[READ_CHUNK];
    guint handled = 0;

    while (!client->dead && !client->closing)
    {
        g_autoptr(GError) error = NULL;
        gssize got;
        gchar *newline;

        got = g_socket_receive_with_blocking (client->socket, buffer, sizeof (buffer), FALSE, NULL, &error);
        if (got < 0)
        {
            if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
                client->dead = TRUE;
            break;
        }
        if (got == 0)
        {
            /*
             * a half-close (shutdown for writing) still gets every reply;
             * a peer that went away entirely (HUP) cancels what it asked
             */
            if (g_socket_condition_check (client->socket, G_IO_HUP | G_IO_ERR) != 0)
                client->dead = TRUE;
            else
                client->closing = TRUE;
            break;
        }
        g_string_append_len (client->in, buffer, got);

        /* --- complete lines --- */
        while ((newline = memchr (client->in->str, '\n', client->in->len)) != NULL)
        {
            gsize length = (gsize)(newline - client->in->str);
            g_autofree gchar *line = g_strndup (client->in->str, length);

            g_string_erase (client->in, 0, (gssize)length + 1);
            if (length > 0 && line[length - 1] == '\r')
                line[length - 1] = '\0';
            handle_line (self, client, line);
            handled++;
        }
        if (client->in->len > LRG_CONTROL_MAX_LINE)
        {
            Entry *entry = entry_new (FALSE, NULL);

            entry->ready = protocol_error_line (NULL, -32600, "line too long");
            g_queue_push_tail (client->entries, entry);
            g_string_truncate (client->in, 0);
            client->closing = TRUE;
            break;
        }
    }
    return handled;
}

/* ------------------------------------------------------------------ */
/* events                                                               */

static void
on_event (LrgCommandRegistry *registry,
          const gchar        *topic,
          JsonNode           *data,
          guint64             seq,
          gpointer            user_data)
{
    LrgControlServer *self = user_data;
    g_autofree gchar *json_line = NULL;
    g_autofree gchar *text_line = NULL;
    guint i;

    for (i = 0; i < self->clients->len; i++)
    {
        Client *client = g_ptr_array_index (self->clients, i);
        gboolean match = FALSE;
        guint p;

        if (client->patterns == NULL || client->dead)
            continue;
        if (self->token != NULL && !client->authed)
            continue;
        for (p = 0; p < client->patterns->len && !match; p++)
            match = lrg_command_topic_matches (g_ptr_array_index (client->patterns, p), topic);
        if (!match)
            continue;

        if (client->text_events)
        {
            if (text_line == NULL)
            {
                g_autofree gchar *json = NULL;

                if (data != NULL)
                    json = node_to_line (data);
                text_line = g_strdup_printf ("EVENT %s %s", topic, json != NULL ? json : "null");
            }
            client_queue_line (client, text_line);
        }
        else
        {
            if (json_line == NULL)
            {
                g_autoptr(JsonBuilder) builder = json_builder_new ();
                g_autoptr(JsonNode) root = NULL;

                json_builder_begin_object (builder);
                json_builder_set_member_name (builder, "jsonrpc");
                json_builder_add_string_value (builder, "2.0");
                json_builder_set_member_name (builder, "method");
                json_builder_add_string_value (builder, "event");
                json_builder_set_member_name (builder, "params");
                json_builder_begin_object (builder);
                json_builder_set_member_name (builder, "event");
                json_builder_add_string_value (builder, topic);
                json_builder_set_member_name (builder, "seq");
                json_builder_add_int_value (builder, (gint64)seq);
                json_builder_set_member_name (builder, "time");
                json_builder_add_int_value (builder, g_get_real_time () / 1000);
                json_builder_set_member_name (builder, "data");
                json_builder_add_value (builder, data != NULL ? json_node_copy (data)
                                                              : json_node_new (JSON_NODE_NULL));
                json_builder_end_object (builder);
                json_builder_end_object (builder);
                root = json_builder_get_root (builder);
                json_line = node_to_line (root);
            }
            client_queue_line (client, json_line);
        }
    }
}

/* ------------------------------------------------------------------ */
/* the object                                                           */

static void
lrg_control_server_dispose (GObject *object)
{
    LrgControlServer *self = LRG_CONTROL_SERVER (object);

    lrg_control_server_close (self);
    if (self->event_handler != 0)
    {
        g_signal_handler_disconnect (self->registry, self->event_handler);
        self->event_handler = 0;
    }
    if (self->source != NULL)
    {
        g_source_destroy (self->source);
        g_clear_pointer (&self->source, g_source_unref);
    }

    G_OBJECT_CLASS (lrg_control_server_parent_class)->dispose (object);
}

static void
lrg_control_server_finalize (GObject *object)
{
    LrgControlServer *self = LRG_CONTROL_SERVER (object);

    g_clear_object (&self->registry);
    g_free (self->token);
    g_free (self->server_name);
    g_free (self->instance);
    g_clear_pointer (&self->info, json_object_unref);
    g_free (self->path);
    g_ptr_array_unref (self->clients);

    G_OBJECT_CLASS (lrg_control_server_parent_class)->finalize (object);
}

static void
lrg_control_server_class_init (LrgControlServerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_control_server_dispose;
    object_class->finalize = lrg_control_server_finalize;
}

static void
lrg_control_server_init (LrgControlServer *self)
{
    self->clients = g_ptr_array_new_with_free_func (client_free);
}

LrgControlServer *
lrg_control_server_new (LrgCommandRegistry *registry)
{
    LrgControlServer *self;

    g_return_val_if_fail (LRG_IS_COMMAND_REGISTRY (registry), NULL);

    self = g_object_new (LRG_TYPE_CONTROL_SERVER, NULL);
    self->registry = g_object_ref (registry);
    self->event_handler = g_signal_connect (registry, "event", G_CALLBACK (on_event), self);
    return self;
}

LrgCommandRegistry *
lrg_control_server_get_registry (LrgControlServer *self)
{
    g_return_val_if_fail (LRG_IS_CONTROL_SERVER (self), NULL);

    return self->registry;
}

void
lrg_control_server_set_token (LrgControlServer *self,
                              const gchar      *token)
{
    g_return_if_fail (LRG_IS_CONTROL_SERVER (self));

    g_free (self->token);
    self->token = (token != NULL && token[0] != '\0') ? g_strdup (token) : NULL;
}

void
lrg_control_server_set_identity (LrgControlServer *self,
                                 const gchar      *server,
                                 const gchar      *instance)
{
    g_return_if_fail (LRG_IS_CONTROL_SERVER (self));

    g_free (self->server_name);
    self->server_name = g_strdup (server);
    g_free (self->instance);
    self->instance = g_strdup (instance);
}

void
lrg_control_server_set_info (LrgControlServer *self,
                             JsonObject       *info)
{
    JsonNode *copy;

    g_return_if_fail (LRG_IS_CONTROL_SERVER (self));

    g_clear_pointer (&self->info, json_object_unref);
    if (info == NULL)
        return;
    {
        g_autoptr(JsonNode) node = json_node_new (JSON_NODE_OBJECT);

        json_node_set_object (node, info);
        copy = json_node_copy (node);
    }
    self->info = json_object_ref (json_node_get_object (copy));
    json_node_unref (copy);
}

gboolean
lrg_control_socket_is_live (const gchar *path)
{
    g_autoptr(GSocket) socket = NULL;
    g_autoptr(GSocketAddress) address = NULL;

    g_return_val_if_fail (path != NULL, FALSE);

    if (!g_file_test (path, G_FILE_TEST_EXISTS))
        return FALSE;
    socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, NULL);
    if (socket == NULL)
        return FALSE;
    address = g_unix_socket_address_new (path);
    return g_socket_connect (socket, address, NULL, NULL);
}

gboolean
lrg_control_server_listen (LrgControlServer  *self,
                           const gchar       *path,
                           GError           **error)
{
    g_autoptr(GSocket) socket = NULL;
    g_autoptr(GSocketAddress) address = NULL;
    g_autofree gchar *dir = NULL;
#ifdef G_OS_UNIX
    mode_t old_mask;
#endif
    gboolean bound;

    g_return_val_if_fail (LRG_IS_CONTROL_SERVER (self), FALSE);
    g_return_val_if_fail (path != NULL && path[0] != '\0', FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (self->listener != NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "already listening on %s", self->path);
        return FALSE;
    }
    /* sockaddr_un holds 108 bytes with the terminator: longer paths cannot be bound */
    if (strlen (path) >= 108)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FILENAME_TOO_LONG,
                     "the socket path %s is longer than a Unix socket allows (107 bytes)", path);
        return FALSE;
    }

    /* --- the directory: the owner's only --- */
    dir = g_path_get_dirname (path);
    if (g_mkdir_with_parents (dir, 0700) != 0)
    {
        gint saved = errno;

        g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
                     "cannot create %s: %s", dir, g_strerror (saved));
        return FALSE;
    }

    /* --- a socket left by a process that is gone is removed --- */
    if (g_file_test (path, G_FILE_TEST_EXISTS))
    {
        if (lrg_control_socket_is_live (path))
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_ADDRESS_IN_USE,
                         "another process listens on %s", path);
            return FALSE;
        }
        if (g_unlink (path) != 0)
        {
            gint saved = errno;

            g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
                         "cannot remove the stale socket %s: %s", path, g_strerror (saved));
            return FALSE;
        }
    }

    socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, error);
    if (socket == NULL)
        return FALSE;
    address = g_unix_socket_address_new (path);

    /* the socket file is created 0600 (no window where others could connect) */
#ifdef G_OS_UNIX
    old_mask = umask (0077);
#endif
    bound = g_socket_bind (socket, address, FALSE, error);
#ifdef G_OS_UNIX
    umask (old_mask);
#endif
    if (!bound)
        return FALSE;
#ifdef G_OS_UNIX
    if (chmod (path, 0600) != 0)
        g_debug ("control: cannot chmod %s: %s", path, g_strerror (errno));
#endif
    if (!g_socket_listen (socket, error))
    {
        g_unlink (path);
        return FALSE;
    }
    g_socket_set_blocking (socket, FALSE);
    self->listener = g_steal_pointer (&socket);
    g_free (self->path);
    self->path = g_strdup (path);
    return TRUE;
}

const gchar *
lrg_control_server_get_path (LrgControlServer *self)
{
    g_return_val_if_fail (LRG_IS_CONTROL_SERVER (self), NULL);

    return self->listener != NULL ? self->path : NULL;
}

gboolean
lrg_control_server_is_listening (LrgControlServer *self)
{
    g_return_val_if_fail (LRG_IS_CONTROL_SERVER (self), FALSE);

    return self->listener != NULL;
}

/* accepts waiting peers of the same user */
static void
accept_clients (LrgControlServer *self)
{
    while (self->listener != NULL)
    {
        g_autoptr(GError) error = NULL;
        g_autoptr(GCredentials) credentials = NULL;
        GSocket *socket;
        Client *client;
        gint uid = -1;
        gint pid = -1;

        socket = g_socket_accept (self->listener, NULL, &error);
        if (socket == NULL)
            break;

        /* --- the owner only (SO_PEERCRED) --- */
#ifdef __linux__
        {
            /* read directly: GCredentials fails when the peer's pid has no
             * number in this pid namespace (a game inside bubblewrap) */
            struct ucred peer;
            socklen_t length = sizeof (peer);

            if (getsockopt (g_socket_get_fd (socket), SOL_SOCKET, SO_PEERCRED, &peer, &length) == 0)
            {
                uid = (gint)peer.uid;
                pid = (gint)peer.pid;
            }
        }
#else
        credentials = g_socket_get_credentials (socket, NULL);
        if (credentials != NULL)
        {
            uid = (gint)g_credentials_get_unix_user (credentials, NULL);
            pid = (gint)g_credentials_get_unix_pid (credentials, NULL);
        }
#endif
#ifdef G_OS_UNIX
        if (uid != (gint)getuid ())
        {
            g_debug ("control: refused a connection from uid %d", uid);
            g_socket_close (socket, NULL);
            g_object_unref (socket);
            continue;
        }
#endif
        g_socket_set_blocking (socket, FALSE);
        client = g_new0 (Client, 1);
        client->server = self;
        client->socket = socket;
        client->number = ++self->next_number;
        client->in = g_string_new (NULL);
        client->out = g_string_new (NULL);
        client->entries = g_queue_new ();
        client->uid = uid;
        client->pid = pid;
        g_ptr_array_add (self->clients, client);
    }
}

guint
lrg_control_server_poll (LrgControlServer *self)
{
    guint handled = 0;
    guint i;

    g_return_val_if_fail (LRG_IS_CONTROL_SERVER (self), 0);

    /* a command run from here may run the game, which polls again */
    if (self->polling)
        return 0;
    self->polling = TRUE;
    g_object_ref (self);

    accept_clients (self);
    for (i = 0; i < self->clients->len; i++)
    {
        Client *client = g_ptr_array_index (self->clients, i);

        handled += client_read (self, client);
    }
    for (i = 0; i < self->clients->len; i++)
    {
        Client *client = g_ptr_array_index (self->clients, i);

        client_flush_entries (client);
        client_write (client);
    }

    /* --- drop the dead connections --- */
    for (i = self->clients->len; i > 0; i--)
    {
        Client *client = g_ptr_array_index (self->clients, i - 1);

        if (client->dead)
            g_ptr_array_remove_index (self->clients, i - 1);
    }

    self->polling = FALSE;
    g_object_unref (self);
    return handled;
}

static gboolean
on_poll_source (gpointer user_data)
{
    lrg_control_server_poll (LRG_CONTROL_SERVER (user_data));
    return G_SOURCE_CONTINUE;
}

void
lrg_control_server_attach (LrgControlServer *self,
                           GMainContext     *context,
                           guint             interval_ms)
{
    g_return_if_fail (LRG_IS_CONTROL_SERVER (self));

    if (self->source != NULL)
    {
        g_source_destroy (self->source);
        g_clear_pointer (&self->source, g_source_unref);
    }
    self->source = g_timeout_source_new (MAX (interval_ms, 1));
    g_source_set_callback (self->source, on_poll_source, self, NULL);
    g_source_set_name (self->source, "LrgControlServer");
    g_source_attach (self->source, context);
}

guint
lrg_control_server_get_n_clients (LrgControlServer *self)
{
    g_return_val_if_fail (LRG_IS_CONTROL_SERVER (self), 0);

    return self->clients->len;
}

void
lrg_control_server_close (LrgControlServer *self)
{
    g_return_if_fail (LRG_IS_CONTROL_SERVER (self));

    g_ptr_array_set_size (self->clients, 0);
    if (self->listener != NULL)
    {
        g_socket_close (self->listener, NULL);
        g_clear_object (&self->listener);
        if (self->path != NULL)
            g_unlink (self->path);
    }
}

gchar *
lrg_control_runtime_dir (const gchar *app)
{
    const gchar *runtime;

    g_return_val_if_fail (app != NULL && app[0] != '\0', NULL);

    runtime = g_getenv ("XDG_RUNTIME_DIR");
    if (runtime != NULL && runtime[0] != '\0' && g_path_is_absolute (runtime))
        return g_build_filename (runtime, app, NULL);
#ifdef G_OS_UNIX
    {
        g_autofree gchar *name = g_strdup_printf ("%s-%u", app, (guint)getuid ());

        return g_build_filename (g_get_tmp_dir (), name, NULL);
    }
#else
    return g_build_filename (g_get_tmp_dir (), app, NULL);
#endif
}

gchar **
lrg_control_list_instances (const gchar *dir,
                            gboolean     live_only)
{
    g_autoptr(GDir) handle = NULL;
    GPtrArray *names;
    const gchar *entry;

    g_return_val_if_fail (dir != NULL, NULL);

    names = g_ptr_array_new ();
    handle = g_dir_open (dir, 0, NULL);
    while (handle != NULL && (entry = g_dir_read_name (handle)) != NULL)
    {
        g_autofree gchar *path = NULL;

        if (!g_str_has_suffix (entry, ".sock"))
            continue;
        path = g_build_filename (dir, entry, NULL);
        if (live_only && !lrg_control_socket_is_live (path))
            continue;
        g_ptr_array_add (names, g_strndup (entry, strlen (entry) - strlen (".sock")));
    }
    /* a few names: a plain exchange sort */
    {
        guint a;
        guint b;

        for (a = 0; a + 1 < names->len; a++)
            for (b = a + 1; b < names->len; b++)
                if (strcmp (g_ptr_array_index (names, a), g_ptr_array_index (names, b)) > 0)
                {
                    gpointer t = names->pdata[a];

                    names->pdata[a] = names->pdata[b];
                    names->pdata[b] = t;
                }
    }
    g_ptr_array_add (names, NULL);
    return (gchar **)g_ptr_array_free (names, FALSE);
}
