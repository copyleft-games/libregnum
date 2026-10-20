/* lrg-control-client.c - A blocking client for a game's control socket
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"
#include "lrg-control-client.h"
#include "lrg-command.h"

#include <string.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>

/**
 * SECTION:lrg-control-client
 * @title: LrgControlClient
 * @short_description: Talk to an #LrgControlServer from a tool
 *
 * See lrg-control-client.h.
 */

#define DEFAULT_TIMEOUT_MS (30000)

struct _LrgControlClient
{
    GObject   parent_instance;

    GSocket  *socket;     /* owned, nullable */
    GString  *in;         /* bytes read, not yet a line */
    GQueue   *events;     /* JsonNode (event params) read while waiting */
    gint64    next_id;
    guint     timeout_ms;
};

G_DEFINE_TYPE (LrgControlClient, lrg_control_client, G_TYPE_OBJECT)

static void
lrg_control_client_finalize (GObject *object)
{
    LrgControlClient *self = LRG_CONTROL_CLIENT (object);

    lrg_control_client_close (self);
    g_string_free (self->in, TRUE);
    g_queue_free_full (self->events, (GDestroyNotify)json_node_unref);

    G_OBJECT_CLASS (lrg_control_client_parent_class)->finalize (object);
}

static void
lrg_control_client_class_init (LrgControlClientClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_control_client_finalize;
}

static void
lrg_control_client_init (LrgControlClient *self)
{
    self->in = g_string_new (NULL);
    self->events = g_queue_new ();
    self->next_id = 1;
    self->timeout_ms = DEFAULT_TIMEOUT_MS;
}

LrgControlClient *
lrg_control_client_new (void)
{
    return g_object_new (LRG_TYPE_CONTROL_CLIENT, NULL);
}

gboolean
lrg_control_client_connect (LrgControlClient  *self,
                            const gchar       *path,
                            GError           **error)
{
    g_autoptr(GSocket) socket = NULL;
    g_autoptr(GSocketAddress) address = NULL;

    g_return_val_if_fail (LRG_IS_CONTROL_CLIENT (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    lrg_control_client_close (self);
    socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, error);
    if (socket == NULL)
        return FALSE;
    address = g_unix_socket_address_new (path);
    if (!g_socket_connect (socket, address, NULL, error))
        return FALSE;
    self->socket = g_steal_pointer (&socket);
    g_string_truncate (self->in, 0);
    return TRUE;
}

void
lrg_control_client_set_timeout (LrgControlClient *self,
                                guint             timeout_ms)
{
    g_return_if_fail (LRG_IS_CONTROL_CLIENT (self));

    self->timeout_ms = timeout_ms;
}

void
lrg_control_client_close (LrgControlClient *self)
{
    g_return_if_fail (LRG_IS_CONTROL_CLIENT (self));

    if (self->socket != NULL)
    {
        g_socket_close (self->socket, NULL);
        g_clear_object (&self->socket);
    }
}

/* writes all of @text and a newline */
static gboolean
send_line (LrgControlClient  *self,
           const gchar       *text,
           GError           **error)
{
    g_autofree gchar *line = NULL;
    gsize length;
    gsize done;

    if (self->socket == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, "not connected");
        return FALSE;
    }
    line = g_strconcat (text, "\n", NULL);
    length = strlen (line);
    done = 0;
    while (done < length)
    {
        gssize sent;

        sent = g_socket_send (self->socket, line + done, length - done, NULL, error);
        if (sent < 0)
            return FALSE;
        done += (gsize)sent;
    }
    return TRUE;
}

/*
 * the next line, waiting at most @timeout_ms (0: forever).  NULL without
 * an error on a timeout; G_IO_ERROR_CLOSED when the server hung up.
 */
static gchar *
read_line (LrgControlClient  *self,
           guint              timeout_ms,
           GError           **error)
{
    gint64 deadline;

    if (self->socket == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, "not connected");
        return NULL;
    }
    deadline = timeout_ms > 0 ? g_get_monotonic_time () + (gint64)timeout_ms * 1000 : -1;
    for (;;)
    {
        gchar *newline;
        gchar buffer[16 * 1024];
        gssize got;
        gint64 wait_us;

        newline = memchr (self->in->str, '\n', self->in->len);
        if (newline != NULL)
        {
            gsize length = (gsize)(newline - self->in->str);
            gchar *line = g_strndup (self->in->str, length);

            g_string_erase (self->in, 0, (gssize)length + 1);
            if (length > 0 && line[length - 1] == '\r')
                line[length - 1] = '\0';
            return line;
        }

        if (deadline >= 0)
        {
            wait_us = deadline - g_get_monotonic_time ();
            if (wait_us <= 0)
                return NULL;
        }
        else
        {
            wait_us = -1;
        }
        if (!g_socket_condition_timed_wait (self->socket, G_IO_IN | G_IO_HUP | G_IO_ERR,
                                            wait_us, NULL, NULL))
            return NULL; /* timed out */
        got = g_socket_receive (self->socket, buffer, sizeof (buffer), NULL, error);
        if (got < 0)
            return NULL;
        if (got == 0)
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_CLOSED, "the server closed the connection");
            return NULL;
        }
        g_string_append_len (self->in, buffer, got);
    }
}

/* an event notification? keeps its params */
static gboolean
take_event (LrgControlClient *self,
            JsonNode         *root)
{
    JsonObject *object;

    if (!JSON_NODE_HOLDS_OBJECT (root))
        return FALSE;
    object = json_node_get_object (root);
    if (json_object_has_member (object, "id") || !json_object_has_member (object, "method"))
        return FALSE;
    if (g_strcmp0 (json_object_get_string_member (object, "method"), "event") != 0)
        return FALSE;
    if (json_object_has_member (object, "params"))
        g_queue_push_tail (self->events, json_node_copy (json_object_get_member (object, "params")));
    return TRUE;
}

JsonNode *
lrg_control_client_call (LrgControlClient  *self,
                         const gchar       *method,
                         JsonObject        *params,
                         GError           **error)
{
    g_autoptr(JsonBuilder) builder = NULL;
    g_autoptr(JsonGenerator) generator = NULL;
    g_autoptr(JsonNode) request = NULL;
    g_autofree gchar *text = NULL;
    gint64 id;
    gint64 deadline;

    g_return_val_if_fail (LRG_IS_CONTROL_CLIENT (self), NULL);
    g_return_val_if_fail (method != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    /* --- the request --- */
    id = self->next_id++;
    builder = json_builder_new ();
    json_builder_begin_object (builder);
    json_builder_set_member_name (builder, "jsonrpc");
    json_builder_add_string_value (builder, "2.0");
    json_builder_set_member_name (builder, "id");
    json_builder_add_int_value (builder, id);
    json_builder_set_member_name (builder, "method");
    json_builder_add_string_value (builder, method);
    if (params != NULL)
    {
        g_autoptr(JsonNode) node = json_node_new (JSON_NODE_OBJECT);

        json_node_set_object (node, params);
        json_builder_set_member_name (builder, "params");
        json_builder_add_value (builder, json_node_copy (node));
    }
    json_builder_end_object (builder);
    request = json_builder_get_root (builder);
    generator = json_generator_new ();
    json_generator_set_root (generator, request);
    text = json_generator_to_data (generator, NULL);
    if (!send_line (self, text, error))
        return NULL;

    /* --- lines until ours: events are kept --- */
    deadline = self->timeout_ms > 0 ? g_get_monotonic_time () + (gint64)self->timeout_ms * 1000 : -1;
    for (;;)
    {
        g_autofree gchar *line = NULL;
        g_autoptr(JsonParser) parser = NULL;
        GError *local = NULL;
        JsonNode *root;
        JsonObject *object;
        guint wait_ms = 0;

        if (deadline >= 0)
        {
            gint64 left = deadline - g_get_monotonic_time ();

            if (left <= 0)
            {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "%s: no reply in time", method);
                return NULL;
            }
            wait_ms = (guint)MAX (left / 1000, 1);
        }
        line = read_line (self, wait_ms, &local);
        if (line == NULL)
        {
            if (local != NULL)
                g_propagate_error (error, local);
            else
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "%s: no reply in time", method);
            return NULL;
        }
        parser = json_parser_new ();
        if (!json_parser_load_from_data (parser, line, -1, NULL))
            continue; /* not ours (a text reply): skip */
        root = json_parser_get_root (parser);
        if (root == NULL || take_event (self, root) || !JSON_NODE_HOLDS_OBJECT (root))
            continue;
        object = json_node_get_object (root);
        if (!json_object_has_member (object, "id") ||
            !JSON_NODE_HOLDS_VALUE (json_object_get_member (object, "id")) ||
            json_node_get_value_type (json_object_get_member (object, "id")) != G_TYPE_INT64 ||
            json_object_get_int_member (object, "id") != id)
        {
            /* a protocol error without our id still answers us */
            if (json_object_has_member (object, "error") &&
                json_object_has_member (object, "id") &&
                JSON_NODE_HOLDS_NULL (json_object_get_member (object, "id")))
            {
                JsonObject *err = json_object_get_object_member (object, "error");

                g_set_error (error, LRG_COMMAND_ERROR,
                             (gint)lrg_command_error_from_jsonrpc_code ((gint)json_object_get_int_member (err, "code")),
                             "%s", json_object_get_string_member (err, "message"));
                return NULL;
            }
            continue;
        }
        if (json_object_has_member (object, "error"))
        {
            JsonObject *err = json_object_get_object_member (object, "error");
            gint code = (gint)json_object_get_int_member (err, "code");

            g_set_error (error, LRG_COMMAND_ERROR, (gint)lrg_command_error_from_jsonrpc_code (code),
                         "%s", json_object_has_member (err, "message") ?
                         json_object_get_string_member (err, "message") : "error");
            return NULL;
        }
        if (!json_object_has_member (object, "result"))
            return json_node_new (JSON_NODE_NULL);
        return json_node_copy (json_object_get_member (object, "result"));
    }
}

gchar *
lrg_control_client_call_line (LrgControlClient  *self,
                              const gchar       *line,
                              GError           **error)
{
    gint64 deadline;

    g_return_val_if_fail (LRG_IS_CONTROL_CLIENT (self), NULL);
    g_return_val_if_fail (line != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (!send_line (self, line, error))
        return NULL;
    deadline = self->timeout_ms > 0 ? g_get_monotonic_time () + (gint64)self->timeout_ms * 1000 : -1;
    for (;;)
    {
        GError *local = NULL;
        gchar *reply;
        guint wait_ms = 0;

        if (deadline >= 0)
        {
            gint64 left = deadline - g_get_monotonic_time ();

            if (left <= 0)
                break;
            wait_ms = (guint)MAX (left / 1000, 1);
        }
        reply = read_line (self, wait_ms, &local);
        if (reply == NULL)
        {
            if (local != NULL)
            {
                g_propagate_error (error, local);
                return NULL;
            }
            break;
        }
        /* text events and JSON events go to the event queue */
        if (g_str_has_prefix (reply, "EVENT "))
        {
            g_auto(GStrv) parts = g_strsplit (reply + 6, " ", 2);
            JsonObject *object = json_object_new ();
            JsonNode *node = json_node_new (JSON_NODE_OBJECT);

            json_object_set_string_member (object, "event", parts[0] != NULL ? parts[0] : "");
            if (parts[0] != NULL && parts[1] != NULL)
            {
                g_autoptr(JsonParser) parser = json_parser_new ();

                if (json_parser_load_from_data (parser, parts[1], -1, NULL))
                    json_object_set_member (object, "data", json_node_copy (json_parser_get_root (parser)));
            }
            json_node_take_object (node, object);
            g_queue_push_tail (self->events, node);
            g_free (reply);
            continue;
        }
        if (reply[0] == '{')
        {
            g_autoptr(JsonParser) parser = json_parser_new ();

            if (json_parser_load_from_data (parser, reply, -1, NULL) &&
                take_event (self, json_parser_get_root (parser)))
            {
                g_free (reply);
                continue;
            }
        }
        return reply;
    }
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "no reply in time");
    return NULL;
}

gboolean
lrg_control_client_subscribe (LrgControlClient    *self,
                              const gchar *const  *topics,
                              GError             **error)
{
    g_autoptr(JsonObject) params = NULL;
    g_autoptr(JsonNode) result = NULL;
    JsonArray *array;
    guint i;

    g_return_val_if_fail (LRG_IS_CONTROL_CLIENT (self), FALSE);

    params = json_object_new ();
    array = json_array_new ();
    for (i = 0; topics != NULL && topics[i] != NULL; i++)
        json_array_add_string_element (array, topics[i]);
    if (json_array_get_length (array) == 0)
        json_array_add_string_element (array, "*");
    json_object_set_array_member (params, "events", array);
    result = lrg_control_client_call (self, "events.subscribe", params, error);
    return result != NULL;
}

JsonNode *
lrg_control_client_read_event (LrgControlClient  *self,
                               guint              timeout_ms,
                               GError           **error)
{
    gint64 deadline;

    g_return_val_if_fail (LRG_IS_CONTROL_CLIENT (self), NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    deadline = timeout_ms > 0 ? g_get_monotonic_time () + (gint64)timeout_ms * 1000 : -1;
    for (;;)
    {
        g_autofree gchar *line = NULL;
        g_autoptr(JsonParser) parser = NULL;
        GError *local = NULL;
        guint wait_ms = 0;

        if (!g_queue_is_empty (self->events))
            return g_queue_pop_head (self->events);
        if (deadline >= 0)
        {
            gint64 left = deadline - g_get_monotonic_time ();

            if (left <= 0)
                return NULL;
            wait_ms = (guint)MAX (left / 1000, 1);
        }
        line = read_line (self, wait_ms, &local);
        if (line == NULL)
        {
            if (local != NULL)
                g_propagate_error (error, local);
            return NULL;
        }
        if (g_str_has_prefix (line, "EVENT "))
        {
            g_auto(GStrv) parts = g_strsplit (line + 6, " ", 2);
            JsonObject *object = json_object_new ();
            JsonNode *node = json_node_new (JSON_NODE_OBJECT);

            json_object_set_string_member (object, "event", parts[0] != NULL ? parts[0] : "");
            if (parts[0] != NULL && parts[1] != NULL)
            {
                g_autoptr(JsonParser) data = json_parser_new ();

                if (json_parser_load_from_data (data, parts[1], -1, NULL))
                    json_object_set_member (object, "data", json_node_copy (json_parser_get_root (data)));
            }
            json_node_take_object (node, object);
            return node;
        }
        parser = json_parser_new ();
        if (json_parser_load_from_data (parser, line, -1, NULL))
            take_event (self, json_parser_get_root (parser));
    }
}
