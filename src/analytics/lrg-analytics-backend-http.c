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
    guint               flush_source_id;

    /* Sender thread (started on the first batch); see "Sender" below */
    GThread            *sender;
    GMutex              lock;
    GCond               cond;
    GQueue             *outbox;       /* HttpRequest *, oldest first */
    gboolean            stalled;      /* head failed; wait for the next kick */
    gboolean            quit;
    GCancellable       *cancellable;
    GError             *send_error;   /* why the head last failed */
};

/*
 * One batch on its way out: everything the sender thread needs, copied at
 * the time the batch was made, so the thread never touches the backend's
 * properties or events.
 */
typedef struct
{
    gchar       *payload;
    const gchar *content_type;
    gchar       *url;
    gchar       *authorization;   /* "Bearer ..." or NULL */
    gchar      **headers;         /* name, value, name, value, ..., NULL */
    guint        n_events;
    guint        retry_count;
    guint        retry_delay_ms;
} HttpRequest;

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

static void
http_request_free (HttpRequest *request)
{
    g_free (request->payload);
    g_free (request->url);
    g_free (request->authorization);
    g_strfreev (request->headers);
    g_free (request);
}

/* ==========================================================================
 * Sender
 *
 * Sending a batch is a blocking HTTP POST with up to retry-count retries
 * retry-delay-ms apart: tens of milliseconds against a healthy server and
 * seconds against an unreachable one. Doing that on the thread that
 * tracked the event (the game loop) froze the game every batch-size
 * events, so batches are posted to a sender thread that sends them in
 * order with its own #SoupSession. One batch is out at a time; events
 * tracked meanwhile collect and form the next one. A batch whose attempts
 * all fail stays queued and is tried again when the next batch fills or
 * on a flush, as unsent events used to be.
 * ========================================================================== */

/* Takes the pending events into a request (on the caller's thread). */
static HttpRequest *
take_request (LrgAnalyticsBackendHttp *self)
{
    HttpRequest    *request;
    GHashTableIter  iter;
    gpointer        key;
    gpointer        value;
    guint           i;

    request = g_new0 (HttpRequest, 1);
    if (self->format == LRG_ANALYTICS_FORMAT_YAML)
    {
        request->payload = build_batch_yaml (self);
        request->content_type = "application/yaml";
    }
    else
    {
        request->payload = build_batch_json (self);
        request->content_type = "application/json";
    }
    request->url = g_strdup (self->endpoint_url);
    if (self->api_key != NULL)
        request->authorization = g_strdup_printf ("Bearer %s", self->api_key);
    request->headers = g_new0 (gchar *, 2 * g_hash_table_size (self->custom_headers) + 1);
    i = 0;
    g_hash_table_iter_init (&iter, self->custom_headers);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        request->headers[i++] = g_strdup (key);
        request->headers[i++] = g_strdup (value);
    }
    request->n_events = self->pending_events->len;
    request->retry_count = self->retry_count;
    request->retry_delay_ms = self->retry_delay_ms;

    g_ptr_array_set_size (self->pending_events, 0);
    return request;
}

/* Sleeps @ms on the sender thread, waking early on quit. Called with the
 * lock held. Returns FALSE when quitting. */
static gboolean
sender_sleep (LrgAnalyticsBackendHttp *self,
              guint                    ms)
{
    gint64 deadline;

    deadline = g_get_monotonic_time () + (gint64) ms * G_TIME_SPAN_MILLISECOND;
    while (!self->quit)
        if (!g_cond_wait_until (&self->cond, &self->lock, deadline))
            break;
    return !self->quit;
}

/* POSTs one request with its retries, on the sender thread. */
static gboolean
send_request (LrgAnalyticsBackendHttp  *self,
              SoupSession              *session,
              HttpRequest              *request,
              GError                  **error)
{
    guint status;
    guint retry;
    guint i;

    lrg_debug (LRG_LOG_DOMAIN_ANALYTICS,
               "Sending batch of %u events to %s",
               request->n_events, request->url);

    for (retry = 0; retry <= request->retry_count; retry++)
    {
        g_autoptr(SoupMessage) msg = NULL;
        g_autoptr(GBytes) body = NULL;
        g_autoptr(GInputStream) stream = NULL;
        g_autoptr(GError) local_error = NULL;

        if (retry > 0)
        {
            gboolean awake;

            lrg_debug (LRG_LOG_DOMAIN_ANALYTICS, "Retry %u/%u", retry, request->retry_count);
            g_mutex_lock (&self->lock);
            awake = sender_sleep (self, request->retry_delay_ms);
            g_mutex_unlock (&self->lock);
            if (!awake)
                break;
        }

        msg = soup_message_new ("POST", request->url);
        if (msg == NULL)
        {
            g_set_error (error,
                         LRG_ANALYTICS_ERROR,
                         LRG_ANALYTICS_ERROR_NETWORK,
                         "Invalid endpoint URL: %s", request->url);
            return FALSE;
        }

        /* Set body */
        body = g_bytes_new_static (request->payload, strlen (request->payload));
        soup_message_set_request_body_from_bytes (msg, request->content_type, body);

        /* Set headers */
        if (request->authorization != NULL)
            soup_message_headers_append (soup_message_get_request_headers (msg),
                                         "Authorization", request->authorization);
        for (i = 0; request->headers[i] != NULL; i += 2)
            soup_message_headers_append (soup_message_get_request_headers (msg),
                                         request->headers[i], request->headers[i + 1]);

        /* Blocking is fine here: this is the sender thread. */
        stream = soup_session_send (session, msg, self->cancellable, &local_error);
        if (stream != NULL)
            g_input_stream_close (stream, NULL, NULL);

        status = soup_message_get_status (msg);

        if (stream != NULL && status >= 200 && status < 300)
        {
            lrg_debug (LRG_LOG_DOMAIN_ANALYTICS,
                       "Successfully sent %u events (status %u)",
                       request->n_events, status);
            return TRUE;
        }

        if (g_cancellable_is_cancelled (self->cancellable))
            break;

        lrg_warning (LRG_LOG_DOMAIN_ANALYTICS,
                     "Failed to send analytics (status %u): %s",
                     status,
                     local_error != NULL ? local_error->message : "unknown error");
    }

    g_set_error (error,
                 LRG_ANALYTICS_ERROR,
                 LRG_ANALYTICS_ERROR_NETWORK,
                 "Failed to send analytics after %u retries", request->retry_count + 1);

    return FALSE;
}

static gpointer
sender_thread (gpointer data)
{
    LrgAnalyticsBackendHttp *self = data;
    g_autoptr(SoupSession)   session = NULL;

    session = soup_session_new ();

    g_mutex_lock (&self->lock);
    for (;;)
    {
        HttpRequest       *request;
        g_autoptr(GError)  error = NULL;
        gboolean           sent;

        while (!self->quit && (self->stalled || g_queue_is_empty (self->outbox)))
            g_cond_wait (&self->cond, &self->lock);
        if (self->quit)
            break;

        /* The head stays queued while it is sent: a failure leaves it there. */
        request = g_queue_peek_head (self->outbox);
        g_mutex_unlock (&self->lock);

        sent = send_request (self, session, request, &error);

        g_mutex_lock (&self->lock);
        if (sent)
        {
            http_request_free (g_queue_pop_head (self->outbox));
            g_clear_error (&self->send_error);
        }
        else
        {
            self->stalled = TRUE;
            g_clear_error (&self->send_error);
            self->send_error = g_steal_pointer (&error);
        }
        g_cond_broadcast (&self->cond);
    }
    g_mutex_unlock (&self->lock);

    return NULL;
}

/* Hands the pending events to the sender as one batch, and wakes it (it
 * also retries a stalled head). One batch is out at a time: while one is
 * being sent, or failed and awaits a retry, new events keep collecting
 * and go out together afterwards, as unsent events always did. Returns
 * TRUE if a batch was handed over. Caller's thread. */
static gboolean
queue_pending (LrgAnalyticsBackendHttp *self)
{
    HttpRequest *request = NULL;
    gboolean     idle;

    g_mutex_lock (&self->lock);
    idle = g_queue_is_empty (self->outbox);
    self->stalled = FALSE;
    g_cond_broadcast (&self->cond);
    g_mutex_unlock (&self->lock);

    if (!idle || self->pending_events->len == 0)
        return FALSE;

    /* Only this thread adds to the outbox, so it is still empty. */
    request = take_request (self);
    g_mutex_lock (&self->lock);
    g_queue_push_tail (self->outbox, request);
    g_cond_broadcast (&self->cond);
    g_mutex_unlock (&self->lock);

    if (self->sender == NULL)
        self->sender = g_thread_new ("lrg-analytics-http", sender_thread, self);
    return TRUE;
}

/* Sends everything, waiting for the sender: the synchronous flush. */
static gboolean
send_all (LrgAnalyticsBackendHttp  *self,
          GError                  **error)
{
    gboolean ok;

    for (;;)
    {
        queue_pending (self);

        g_mutex_lock (&self->lock);
        while (!self->quit && !self->stalled && !g_queue_is_empty (self->outbox))
            g_cond_wait (&self->cond, &self->lock);
        ok = g_queue_is_empty (self->outbox);
        if (!ok)
        {
            if (self->send_error != NULL)
                g_propagate_error (error, g_error_copy (self->send_error));
            else
                g_set_error (error, LRG_ANALYTICS_ERROR, LRG_ANALYTICS_ERROR_NETWORK,
                             "Failed to send analytics");
        }
        g_mutex_unlock (&self->lock);

        /* Events that collected while a batch was out go next. */
        if (!ok || self->pending_events->len == 0)
            return ok;
    }
}

static gboolean
flush_timer_callback (gpointer user_data)
{
    LrgAnalyticsBackendHttp *self = LRG_ANALYTICS_BACKEND_HTTP (user_data);

    /* Queued, not sent here: the sender thread reports failures. */
    if (self->pending_events->len > 0)
        queue_pending (self);

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

    /* A full batch goes to the sender thread; the caller does not wait.
     * (While the previous batch is still out this only wakes the sender.) */
    if (self->pending_events->len >= self->batch_size)
        queue_pending (self);

    return TRUE;
}

static gboolean
lrg_analytics_backend_http_flush (LrgAnalyticsBackend  *backend,
                                  GError              **error)
{
    LrgAnalyticsBackendHttp *self = LRG_ANALYTICS_BACKEND_HTTP (backend);

    return send_all (self, error);
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

    /* Stop the sender; batches it has not sent are dropped, as pending
     * events always were (lrg_analytics_end_session() flushes first). */
    if (self->sender != NULL)
    {
        g_mutex_lock (&self->lock);
        self->quit = TRUE;
        g_cond_broadcast (&self->cond);
        g_mutex_unlock (&self->lock);
        g_cancellable_cancel (self->cancellable);
        g_thread_join (self->sender);
        self->sender = NULL;
    }
    if (!g_queue_is_empty (self->outbox))
        lrg_debug (LRG_LOG_DOMAIN_ANALYTICS, "Dropping %u unsent batches",
                   g_queue_get_length (self->outbox));
    g_queue_free_full (self->outbox, (GDestroyNotify) http_request_free);
    g_clear_object (&self->cancellable);
    g_clear_error (&self->send_error);
    g_cond_clear (&self->cond);
    g_mutex_clear (&self->lock);

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
    g_mutex_init (&self->lock);
    g_cond_init (&self->cond);
    self->outbox = g_queue_new ();
    self->cancellable = g_cancellable_new ();
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
    GList *l;
    guint  count;

    g_return_val_if_fail (LRG_IS_ANALYTICS_BACKEND_HTTP (self), 0);

    /* Events not yet delivered: still collecting, or in the batch out. */
    count = self->pending_events->len;
    g_mutex_lock (&self->lock);
    for (l = self->outbox->head; l != NULL; l = l->next)
        count += ((HttpRequest *) l->data)->n_events;
    g_mutex_unlock (&self->lock);
    return count;
}
