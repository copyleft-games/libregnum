/* lrg-analytics-backend-http.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAnalyticsBackendHttp - HTTP analytics backend.
 */

#include "config.h"

#include "lrg-analytics-backend-http.h"
#include "../lrg-log.h"

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

struct _LrgAnalyticsBackendHttp
{
    LrgAnalyticsBackend  parent_instance;

    gchar              *endpoint_url;
    gchar              *api_key;
    GHashTable         *custom_headers;
    LrgAnalyticsFormat  format;

    guint               batch_size;
    guint               flush_interval;
    guint               retry_count;
    guint               retry_delay_ms;

    GPtrArray          *pending_events;
    SoupSession        *session;
    guint               flush_source_id;
};

enum
{
    PROP_0,
    PROP_ENDPOINT_URL,
    PROP_API_KEY,
    PROP_FORMAT,
    PROP_BATCH_SIZE,
    PROP_FLUSH_INTERVAL,
    PROP_RETRY_COUNT,
    PROP_RETRY_DELAY_MS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (LrgAnalyticsBackendHttp, lrg_analytics_backend_http, LRG_TYPE_ANALYTICS_BACKEND)

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

static gchar *
build_batch_json (LrgAnalyticsBackendHttp *self)
{
    g_autoptr(JsonBuilder) builder = NULL;
    g_autoptr(JsonGenerator) generator = NULL;
    g_autoptr(JsonNode) root = NULL;
    guint i;

    builder = json_builder_new ();

    json_builder_begin_object (builder);
    json_builder_set_member_name (builder, "events");
    json_builder_begin_array (builder);

    for (i = 0; i < self->pending_events->len; i++)
    {
        LrgAnalyticsEvent *event = g_ptr_array_index (self->pending_events, i);
        g_autofree gchar *event_json = lrg_analytics_event_to_json (event);
        g_autoptr(JsonParser) parser = json_parser_new ();

        if (json_parser_load_from_data (parser, event_json, -1, NULL))
        {
            JsonNode *event_node = json_parser_get_root (parser);
            json_builder_add_value (builder, json_node_copy (event_node));
        }
    }

    json_builder_end_array (builder);
    json_builder_end_object (builder);

    root = json_builder_get_root (builder);
    generator = json_generator_new ();
    json_generator_set_root (generator, root);

    return json_generator_to_data (generator, NULL);
}

static gchar *
build_batch_yaml (LrgAnalyticsBackendHttp *self)
{
    GString *yaml;
    guint i;

    yaml = g_string_new ("events:\n");

    for (i = 0; i < self->pending_events->len; i++)
    {
        LrgAnalyticsEvent *event = g_ptr_array_index (self->pending_events, i);
        g_autofree gchar *event_yaml = lrg_analytics_event_to_yaml (event);
        gchar **lines;
        guint j;

        g_string_append (yaml, "  - ");

        /* Indent each line of the event YAML */
        lines = g_strsplit (event_yaml, "\n", -1);
        for (j = 0; lines[j] != NULL; j++)
        {
            if (lines[j][0] == '\0')
                continue;

            if (j == 0)
            {
                g_string_append_printf (yaml, "%s\n", lines[j]);
            }
            else
            {
                g_string_append_printf (yaml, "    %s\n", lines[j]);
            }
        }
        g_strfreev (lines);
    }

    return g_string_free (yaml, FALSE);
}

static gboolean
send_batch (LrgAnalyticsBackendHttp  *self,
            GError                  **error)
{
    g_autoptr(SoupMessage) msg = NULL;
    g_autofree gchar *payload = NULL;
    const gchar *content_type;
    GHashTableIter iter;
    gpointer key, value;
    guint status;
    guint retry;

    if (self->pending_events->len == 0)
        return TRUE;

    /* Build payload */
    if (self->format == LRG_ANALYTICS_FORMAT_YAML)
    {
        payload = build_batch_yaml (self);
        content_type = "application/yaml";
    }
    else
    {
        payload = build_batch_json (self);
        content_type = "application/json";
    }

    lrg_debug (LRG_LOG_DOMAIN_ANALYTICS,
                   "Sending batch of %u events to %s",
                   self->pending_events->len,
                   self->endpoint_url);

    for (retry = 0; retry <= self->retry_count; retry++)
    {
        g_autoptr(GBytes) body = NULL;
        g_autoptr(GInputStream) stream = NULL;
        g_autoptr(GError) local_error = NULL;

        if (retry > 0)
        {
            lrg_debug (LRG_LOG_DOMAIN_ANALYTICS, "Retry %u/%u", retry, self->retry_count);
            g_usleep (self->retry_delay_ms * 1000);
        }

        msg = soup_message_new ("POST", self->endpoint_url);
        if (msg == NULL)
        {
            g_set_error (error,
                         LRG_ANALYTICS_ERROR,
                         LRG_ANALYTICS_ERROR_NETWORK,
                         "Invalid endpoint URL: %s", self->endpoint_url);
            return FALSE;
        }

        /* Set body */
        body = g_bytes_new (payload, strlen (payload));
        soup_message_set_request_body_from_bytes (msg, content_type, body);

        /* Set headers */
        if (self->api_key != NULL)
        {
            g_autofree gchar *auth = g_strdup_printf ("Bearer %s", self->api_key);
            soup_message_headers_append (soup_message_get_request_headers (msg),
                                         "Authorization", auth);
        }

        g_hash_table_iter_init (&iter, self->custom_headers);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            soup_message_headers_append (soup_message_get_request_headers (msg),
                                         (const gchar *)key,
                                         (const gchar *)value);
        }

        /* Send request synchronously */
        stream = soup_session_send (self->session, msg, NULL, &local_error);

        status = soup_message_get_status (msg);

        if (stream != NULL && status >= 200 && status < 300)
        {
            lrg_debug (LRG_LOG_DOMAIN_ANALYTICS,
                           "Successfully sent %u events (status %u)",
                           self->pending_events->len, status);

            /* Clear pending events on success */
            g_ptr_array_set_size (self->pending_events, 0);
            return TRUE;
        }

        lrg_warning (LRG_LOG_DOMAIN_ANALYTICS,
                        "Failed to send analytics (status %u): %s",
                        status,
                        local_error != NULL ? local_error->message : "unknown error");

        g_clear_object (&msg);
    }

    g_set_error (error,
                 LRG_ANALYTICS_ERROR,
                 LRG_ANALYTICS_ERROR_NETWORK,
                 "Failed to send analytics after %u retries", self->retry_count + 1);

    return FALSE;
}

static gboolean
flush_timer_callback (gpointer user_data)
{
    LrgAnalyticsBackendHttp *self = LRG_ANALYTICS_BACKEND_HTTP (user_data);
    g_autoptr(GError) error = NULL;

    if (self->pending_events->len > 0)
    {
        if (!send_batch (self, &error))
        {
            lrg_warning (LRG_LOG_DOMAIN_ANALYTICS, "Auto-flush failed: %s", error->message);
        }
    }

    return G_SOURCE_CONTINUE;
}

static void
update_flush_timer (LrgAnalyticsBackendHttp *self)
{
    if (self->flush_source_id != 0)
    {
        g_source_remove (self->flush_source_id);
        self->flush_source_id = 0;
    }

    if (self->flush_interval > 0)
    {
        self->flush_source_id = g_timeout_add_seconds (self->flush_interval,
                                                        flush_timer_callback,
                                                        self);
    }
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

static gboolean
lrg_analytics_backend_http_send_event (LrgAnalyticsBackend  *backend,
                                       LrgAnalyticsEvent    *event,
                                       GError              **error)
{
    LrgAnalyticsBackendHttp *self = LRG_ANALYTICS_BACKEND_HTTP (backend);

    if (!lrg_analytics_backend_is_enabled (backend))
    {
        lrg_debug (LRG_LOG_DOMAIN_ANALYTICS, "Backend disabled, dropping event");
        return TRUE;
    }

    /* Add to pending queue */
    g_ptr_array_add (self->pending_events, g_object_ref (event));

    lrg_debug (LRG_LOG_DOMAIN_ANALYTICS,
                   "Queued event '%s' (%u pending)",
                   lrg_analytics_event_get_name (event),
                   self->pending_events->len);

    /* Flush if batch is full */
    if (self->pending_events->len >= self->batch_size)
    {
        return send_batch (self, error);
    }

    return TRUE;
}

static gboolean
lrg_analytics_backend_http_flush (LrgAnalyticsBackend  *backend,
                                  GError              **error)
{
    LrgAnalyticsBackendHttp *self = LRG_ANALYTICS_BACKEND_HTTP (backend);

    return send_batch (self, error);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_analytics_backend_http_finalize (GObject *object)
{
    LrgAnalyticsBackendHttp *self = LRG_ANALYTICS_BACKEND_HTTP (object);

    if (self->flush_source_id != 0)
    {
        g_source_remove (self->flush_source_id);
        self->flush_source_id = 0;
    }

    g_clear_pointer (&self->endpoint_url, g_free);
    g_clear_pointer (&self->api_key, g_free);
    g_clear_pointer (&self->custom_headers, g_hash_table_unref);
    g_clear_pointer (&self->pending_events, g_ptr_array_unref);
    g_clear_object (&self->session);

    G_OBJECT_CLASS (lrg_analytics_backend_http_parent_class)->finalize (object);
}

static void
lrg_analytics_backend_http_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
    LrgAnalyticsBackendHttp *self = LRG_ANALYTICS_BACKEND_HTTP (object);

    switch (prop_id)
    {
    case PROP_ENDPOINT_URL:
        g_value_set_string (value, self->endpoint_url);
        break;
    case PROP_API_KEY:
        g_value_set_string (value, self->api_key);
        break;
    case PROP_FORMAT:
        g_value_set_enum (value, self->format);
        break;
    case PROP_BATCH_SIZE:
        g_value_set_uint (value, self->batch_size);
        break;
    case PROP_FLUSH_INTERVAL:
        g_value_set_uint (value, self->flush_interval);
        break;
    case PROP_RETRY_COUNT:
        g_value_set_uint (value, self->retry_count);
        break;
    case PROP_RETRY_DELAY_MS:
        g_value_set_uint (value, self->retry_delay_ms);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_analytics_backend_http_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
    LrgAnalyticsBackendHttp *self = LRG_ANALYTICS_BACKEND_HTTP (object);

    switch (prop_id)
    {
    case PROP_ENDPOINT_URL:
        g_clear_pointer (&self->endpoint_url, g_free);
        self->endpoint_url = g_value_dup_string (value);
        break;
    case PROP_API_KEY:
        g_clear_pointer (&self->api_key, g_free);
        self->api_key = g_value_dup_string (value);
        break;
    case PROP_FORMAT:
        self->format = g_value_get_enum (value);
        break;
    case PROP_BATCH_SIZE:
        self->batch_size = CLAMP (g_value_get_uint (value), 1, 1000);
        break;
    case PROP_FLUSH_INTERVAL:
        self->flush_interval = g_value_get_uint (value);
        update_flush_timer (self);
        break;
    case PROP_RETRY_COUNT:
        self->retry_count = CLAMP (g_value_get_uint (value), 0, 10);
        break;
    case PROP_RETRY_DELAY_MS:
        self->retry_delay_ms = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_analytics_backend_http_class_init (LrgAnalyticsBackendHttpClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgAnalyticsBackendClass *backend_class = LRG_ANALYTICS_BACKEND_CLASS (klass);

    object_class->finalize = lrg_analytics_backend_http_finalize;
    object_class->get_property = lrg_analytics_backend_http_get_property;
    object_class->set_property = lrg_analytics_backend_http_set_property;

    /* Override virtual methods */
    backend_class->send_event = lrg_analytics_backend_http_send_event;
    backend_class->flush = lrg_analytics_backend_http_flush;

    properties[PROP_ENDPOINT_URL] =
        g_param_spec_string ("endpoint-url",
                             "Endpoint URL",
                             "HTTP endpoint URL",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_API_KEY] =
        g_param_spec_string ("api-key",
                             "API Key",
                             "Authentication API key",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_FORMAT] =
        g_param_spec_enum ("format",
                           "Format",
                           "Payload format (JSON or YAML)",
                           LRG_TYPE_ANALYTICS_FORMAT,
                           LRG_ANALYTICS_FORMAT_JSON,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    properties[PROP_BATCH_SIZE] =
        g_param_spec_uint ("batch-size",
                           "Batch Size",
                           "Events to batch before sending",
                           1, 1000, 10,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    properties[PROP_FLUSH_INTERVAL] =
        g_param_spec_uint ("flush-interval",
                           "Flush Interval",
                           "Seconds between automatic flushes",
                           0, 3600, 60,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    properties[PROP_RETRY_COUNT] =
        g_param_spec_uint ("retry-count",
                           "Retry Count",
                           "Number of retry attempts",
                           0, 10, 3,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    properties[PROP_RETRY_DELAY_MS] =
        g_param_spec_uint ("retry-delay-ms",
                           "Retry Delay",
                           "Delay between retries in ms",
                           0, 60000, 1000,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_analytics_backend_http_init (LrgAnalyticsBackendHttp *self)
{
    self->format = LRG_ANALYTICS_FORMAT_JSON;
    self->batch_size = 10;
    self->flush_interval = 60;
    self->retry_count = 3;
    self->retry_delay_ms = 1000;

    self->custom_headers = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    self->pending_events = g_ptr_array_new_with_free_func (g_object_unref);
    self->session = soup_session_new ();
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgAnalyticsBackendHttp *
lrg_analytics_backend_http_new (const gchar *endpoint_url)
{
    g_return_val_if_fail (endpoint_url != NULL, NULL);

    return g_object_new (LRG_TYPE_ANALYTICS_BACKEND_HTTP,
                         "name", "http",
                         "endpoint-url", endpoint_url,
                         NULL);
}

const gchar *
lrg_analytics_backend_http_get_endpoint_url (LrgAnalyticsBackendHttp *self)
{
    g_return_val_if_fail (LRG_IS_ANALYTICS_BACKEND_HTTP (self), NULL);

    return self->endpoint_url;
}

const gchar *
lrg_analytics_backend_http_get_api_key (LrgAnalyticsBackendHttp *self)
{
    g_return_val_if_fail (LRG_IS_ANALYTICS_BACKEND_HTTP (self), NULL);

    return self->api_key;
}

void
lrg_analytics_backend_http_set_api_key (LrgAnalyticsBackendHttp *self,
                                        const gchar             *api_key)
{
    g_return_if_fail (LRG_IS_ANALYTICS_BACKEND_HTTP (self));

    g_clear_pointer (&self->api_key, g_free);
    self->api_key = g_strdup (api_key);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_API_KEY]);
}

void
lrg_analytics_backend_http_set_header (LrgAnalyticsBackendHttp *self,
                                       const gchar             *name,
                                       const gchar             *value)
{
    g_return_if_fail (LRG_IS_ANALYTICS_BACKEND_HTTP (self));
    g_return_if_fail (name != NULL);

    if (value != NULL)
    {
        g_hash_table_insert (self->custom_headers, g_strdup (name), g_strdup (value));
    }
    else
    {
        g_hash_table_remove (self->custom_headers, name);
    }
}

LrgAnalyticsFormat
lrg_analytics_backend_http_get_format (LrgAnalyticsBackendHttp *self)
{
    g_return_val_if_fail (LRG_IS_ANALYTICS_BACKEND_HTTP (self), LRG_ANALYTICS_FORMAT_JSON);

    return self->format;
}

void
lrg_analytics_backend_http_set_format (LrgAnalyticsBackendHttp *self,
                                       LrgAnalyticsFormat       format)
{
    g_return_if_fail (LRG_IS_ANALYTICS_BACKEND_HTTP (self));

    if (self->format != format)
    {
        self->format = format;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FORMAT]);
    }
}

guint
lrg_analytics_backend_http_get_batch_size (LrgAnalyticsBackendHttp *self)
{
    g_return_val_if_fail (LRG_IS_ANALYTICS_BACKEND_HTTP (self), 0);

    return self->batch_size;
}

void
lrg_analytics_backend_http_set_batch_size (LrgAnalyticsBackendHttp *self,
                                           guint                    batch_size)
{
    g_return_if_fail (LRG_IS_ANALYTICS_BACKEND_HTTP (self));

    batch_size = CLAMP (batch_size, 1, 1000);
    if (self->batch_size != batch_size)
    {
        self->batch_size = batch_size;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BATCH_SIZE]);
    }
}

guint
lrg_analytics_backend_http_get_flush_interval (LrgAnalyticsBackendHttp *self)
{
    g_return_val_if_fail (LRG_IS_ANALYTICS_BACKEND_HTTP (self), 0);

    return self->flush_interval;
}

void
lrg_analytics_backend_http_set_flush_interval (LrgAnalyticsBackendHttp *self,
                                               guint                    interval)
{
    g_return_if_fail (LRG_IS_ANALYTICS_BACKEND_HTTP (self));

    if (self->flush_interval != interval)
    {
        self->flush_interval = interval;
        update_flush_timer (self);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLUSH_INTERVAL]);
    }
}

guint
lrg_analytics_backend_http_get_retry_count (LrgAnalyticsBackendHttp *self)
{
    g_return_val_if_fail (LRG_IS_ANALYTICS_BACKEND_HTTP (self), 0);

    return self->retry_count;
}

void
lrg_analytics_backend_http_set_retry_count (LrgAnalyticsBackendHttp *self,
                                            guint                    count)
{
    g_return_if_fail (LRG_IS_ANALYTICS_BACKEND_HTTP (self));

    count = CLAMP (count, 0, 10);
    if (self->retry_count != count)
    {
        self->retry_count = count;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RETRY_COUNT]);
    }
}

guint
lrg_analytics_backend_http_get_pending_count (LrgAnalyticsBackendHttp *self)
{
    g_return_val_if_fail (LRG_IS_ANALYTICS_BACKEND_HTTP (self), 0);

    return self->pending_events->len;
}
