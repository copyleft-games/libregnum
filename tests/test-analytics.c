/* test-analytics.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the Analytics module.
 */

#include <glib.h>
#include <gio/gio.h>
#include <libregnum.h>

/* ==========================================================================
 * Mock Analytics Backend
 *
 * Records events for verification rather than sending them anywhere.
 * ========================================================================== */

#define TEST_TYPE_MOCK_BACKEND (test_mock_backend_get_type ())
G_DECLARE_FINAL_TYPE (TestMockBackend, test_mock_backend, TEST, MOCK_BACKEND, LrgAnalyticsBackend)

struct _TestMockBackend
{
    LrgAnalyticsBackend parent_instance;

    GPtrArray *events;
    gboolean   flush_called;
    gboolean   should_fail;
};

G_DEFINE_TYPE (TestMockBackend, test_mock_backend, LRG_TYPE_ANALYTICS_BACKEND)

static gboolean
test_mock_backend_send_event (LrgAnalyticsBackend  *backend,
                              LrgAnalyticsEvent    *event,
                              GError              **error)
{
    TestMockBackend *self = TEST_MOCK_BACKEND (backend);

    if (self->should_fail)
    {
        g_set_error (error,
                     LRG_ANALYTICS_ERROR,
                     LRG_ANALYTICS_ERROR_BACKEND,
                     "Mock backend failure");
        return FALSE;
    }

    g_ptr_array_add (self->events, g_object_ref (event));
    return TRUE;
}

static gboolean
test_mock_backend_flush (LrgAnalyticsBackend  *backend,
                         GError              **error)
{
    TestMockBackend *self = TEST_MOCK_BACKEND (backend);

    if (self->should_fail)
    {
        g_set_error (error,
                     LRG_ANALYTICS_ERROR,
                     LRG_ANALYTICS_ERROR_BACKEND,
                     "Mock backend flush failure");
        return FALSE;
    }

    self->flush_called = TRUE;
    return TRUE;
}

static void
test_mock_backend_finalize (GObject *object)
{
    TestMockBackend *self = TEST_MOCK_BACKEND (object);

    g_ptr_array_unref (self->events);

    G_OBJECT_CLASS (test_mock_backend_parent_class)->finalize (object);
}

static void
test_mock_backend_class_init (TestMockBackendClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgAnalyticsBackendClass *backend_class = LRG_ANALYTICS_BACKEND_CLASS (klass);

    object_class->finalize = test_mock_backend_finalize;

    backend_class->send_event = test_mock_backend_send_event;
    backend_class->flush = test_mock_backend_flush;
}

static void
test_mock_backend_init (TestMockBackend *self)
{
    self->events = g_ptr_array_new_with_free_func (g_object_unref);
    self->flush_called = FALSE;
    self->should_fail = FALSE;
}

static TestMockBackend *
test_mock_backend_new (void)
{
    return g_object_new (TEST_TYPE_MOCK_BACKEND,
                         "name", "mock",
                         NULL);
}

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgAnalytics    *analytics;
    TestMockBackend *backend;
} AnalyticsFixture;

static void
analytics_fixture_set_up (AnalyticsFixture *fixture,
                          gconstpointer     user_data)
{
    /* Create a fresh analytics instance */
    fixture->analytics = g_object_new (LRG_TYPE_ANALYTICS, NULL);
    g_assert_nonnull (fixture->analytics);

    /* Create mock backend */
    fixture->backend = test_mock_backend_new ();
    g_assert_nonnull (fixture->backend);

    /* Set the backend on analytics */
    lrg_analytics_set_backend (fixture->analytics, LRG_ANALYTICS_BACKEND (fixture->backend));
    lrg_analytics_set_enabled (fixture->analytics, TRUE);
}

static void
analytics_fixture_tear_down (AnalyticsFixture *fixture,
                             gconstpointer     user_data)
{
    g_clear_object (&fixture->backend);
    g_clear_object (&fixture->analytics);
}

/* ==========================================================================
 * Test Cases - LrgAnalyticsEvent
 * ========================================================================== */

static void
test_analytics_event_new (void)
{
    g_autoptr(LrgAnalyticsEvent) event = NULL;

    event = lrg_analytics_event_new ("test_event");
    g_assert_nonnull (event);
    g_assert_cmpstr (lrg_analytics_event_get_name (event), ==, "test_event");
    g_assert_nonnull (lrg_analytics_event_get_timestamp (event));
}

static void
test_analytics_event_properties (void)
{
    g_autoptr(LrgAnalyticsEvent) event = NULL;
    const gchar *str_value;
    GList *keys;

    event = lrg_analytics_event_new ("test_event");

    /* Set string property and verify via get_property_string */
    lrg_analytics_event_set_property_string (event, "screen", "main_menu");
    str_value = lrg_analytics_event_get_property_string (event, "screen");
    g_assert_nonnull (str_value);
    g_assert_cmpstr (str_value, ==, "main_menu");

    /* Set int property */
    lrg_analytics_event_set_property_int (event, "level", 5);

    /* Set double property */
    lrg_analytics_event_set_property_double (event, "score", 1234.5);

    /* Set bool property */
    lrg_analytics_event_set_property_boolean (event, "first_time", TRUE);

    /* Verify all keys are present */
    keys = lrg_analytics_event_get_property_keys (event);
    g_assert_nonnull (keys);
    g_assert_cmpuint (g_list_length (keys), ==, 4);
    /* Keys are owned by the hash table, only free the list structure */
    g_list_free (keys);
}

static void
test_analytics_event_to_json (void)
{
    g_autoptr(LrgAnalyticsEvent) event = NULL;
    g_autofree gchar *json = NULL;

    event = lrg_analytics_event_new ("test_event");
    lrg_analytics_event_set_property_string (event, "screen", "main_menu");
    lrg_analytics_event_set_property_int (event, "level", 5);

    json = lrg_analytics_event_to_json (event);
    g_assert_nonnull (json);
    g_assert_true (g_str_has_prefix (json, "{"));
    /* JSON uses "name" field for the event name */
    g_assert_true (g_strstr_len (json, -1, "\"name\":\"test_event\"") != NULL);
    /* Properties are nested under "properties" object */
    g_assert_true (g_strstr_len (json, -1, "\"screen\":\"main_menu\"") != NULL);
}

static void
test_analytics_event_to_yaml (void)
{
    g_autoptr(LrgAnalyticsEvent) event = NULL;
    g_autofree gchar *yaml = NULL;

    event = lrg_analytics_event_new ("test_event");
    lrg_analytics_event_set_property_string (event, "key", "value");

    yaml = lrg_analytics_event_to_yaml (event);
    g_assert_nonnull (yaml);
    /* YAML uses "name" field for the event name */
    g_assert_true (g_strstr_len (yaml, -1, "name:") != NULL);
    g_assert_true (g_strstr_len (yaml, -1, "test_event") != NULL);
}

/* ==========================================================================
 * Test Cases - LrgConsent
 * ========================================================================== */

static void
test_consent_new (void)
{
    g_autoptr(LrgConsent) consent = NULL;

    consent = lrg_consent_new (NULL);
    g_assert_nonnull (consent);

    /* By default, consent should not be granted */
    g_assert_false (lrg_consent_get_analytics_enabled (consent));
    g_assert_false (lrg_consent_get_crash_reporting_enabled (consent));
}

static void
test_consent_set_enabled (void)
{
    g_autoptr(LrgConsent) consent = NULL;

    consent = lrg_consent_new (NULL);

    lrg_consent_set_analytics_enabled (consent, TRUE);
    g_assert_true (lrg_consent_get_analytics_enabled (consent));

    lrg_consent_set_crash_reporting_enabled (consent, TRUE);
    g_assert_true (lrg_consent_get_crash_reporting_enabled (consent));

    /* Consent date should be set when consent changes */
    g_assert_nonnull (lrg_consent_get_consent_date (consent));
}

static void
test_consent_grant_all (void)
{
    g_autoptr(LrgConsent) consent = NULL;

    consent = lrg_consent_new (NULL);

    lrg_consent_set_all (consent, TRUE);
    g_assert_true (lrg_consent_get_analytics_enabled (consent));
    g_assert_true (lrg_consent_get_crash_reporting_enabled (consent));
}

static void
test_consent_revoke_all (void)
{
    g_autoptr(LrgConsent) consent = NULL;

    consent = lrg_consent_new (NULL);

    lrg_consent_set_all (consent, TRUE);
    g_assert_true (lrg_consent_get_analytics_enabled (consent));

    lrg_consent_set_all (consent, FALSE);
    g_assert_false (lrg_consent_get_analytics_enabled (consent));
    g_assert_false (lrg_consent_get_crash_reporting_enabled (consent));
}

static void
test_consent_requires_prompt (void)
{
    g_autoptr(LrgConsent) consent = NULL;

    consent = lrg_consent_new (NULL);

    /* New consent should require a prompt */
    g_assert_true (lrg_consent_requires_prompt (consent));

    /* After granting consent, no prompt needed */
    lrg_consent_set_all (consent, TRUE);
    g_assert_false (lrg_consent_requires_prompt (consent));
}

/* ==========================================================================
 * Test Cases - LrgAnalyticsBackend
 * ========================================================================== */

static void
test_analytics_backend_enabled (void)
{
    g_autoptr(TestMockBackend) backend = NULL;

    backend = test_mock_backend_new ();
    g_assert_nonnull (backend);

    /* Default should be enabled */
    g_assert_true (lrg_analytics_backend_get_enabled (LRG_ANALYTICS_BACKEND (backend)));

    lrg_analytics_backend_set_enabled (LRG_ANALYTICS_BACKEND (backend), FALSE);
    g_assert_false (lrg_analytics_backend_get_enabled (LRG_ANALYTICS_BACKEND (backend)));
}

static void
test_analytics_backend_name (void)
{
    g_autoptr(TestMockBackend) backend = NULL;

    backend = test_mock_backend_new ();

    g_assert_cmpstr (lrg_analytics_backend_get_name (LRG_ANALYTICS_BACKEND (backend)),
                     ==, "mock");
}

static void
test_analytics_backend_send_event (void)
{
    g_autoptr(TestMockBackend) backend = NULL;
    g_autoptr(LrgAnalyticsEvent) event = NULL;
    g_autoptr(GError) error = NULL;
    gboolean result;

    backend = test_mock_backend_new ();
    event = lrg_analytics_event_new ("test_event");

    result = lrg_analytics_backend_send_event (LRG_ANALYTICS_BACKEND (backend),
                                               event, &error);
    g_assert_true (result);
    g_assert_no_error (error);
    g_assert_cmpuint (backend->events->len, ==, 1);
}

static void
test_analytics_backend_flush (void)
{
    g_autoptr(TestMockBackend) backend = NULL;
    g_autoptr(GError) error = NULL;
    gboolean result;

    backend = test_mock_backend_new ();

    result = lrg_analytics_backend_flush (LRG_ANALYTICS_BACKEND (backend), &error);
    g_assert_true (result);
    g_assert_no_error (error);
    g_assert_true (backend->flush_called);
}

/* ==========================================================================
 * Test Cases - HTTP backend against a local, deliberately slow server
 * ========================================================================== */

/* A one-thread HTTP/1.1 server: answers every POST with 200 after
 * @delay_ms, counting requests. Enough for libsoup's keep-alive client. */
typedef struct
{
    GSocketListener *listener;
    guint16          port;
    guint            delay_ms;
    gint             requests;
    GThread         *thread;
    GCancellable    *cancellable;
} SlowServer;

static void
slow_server_serve (SlowServer        *server,
                   GSocketConnection *connection)
{
    GInputStream               *in;
    GOutputStream              *out;
    g_autoptr(GDataInputStream) data = NULL;

    in = g_io_stream_get_input_stream (G_IO_STREAM (connection));
    out = g_io_stream_get_output_stream (G_IO_STREAM (connection));
    data = g_data_input_stream_new (in);
    g_data_input_stream_set_newline_type (data, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);

    for (;;)
    {
        gsize   content_length = 0;
        gchar  *line;
        gboolean got_request = FALSE;
        static const gchar reply[] = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";

        /* Request line and headers, up to the empty line. */
        while ((line = g_data_input_stream_read_line (data, NULL, server->cancellable,
                                                      NULL)) != NULL)
        {
            gboolean end = line[0] == '\0';

            got_request = TRUE;
            if (g_ascii_strncasecmp (line, "Content-Length:", 15) == 0)
                content_length = (gsize) g_ascii_strtoull (line + 15, NULL, 10);
            g_free (line);
            if (end)
                break;
        }
        if (!got_request)
            return;
        if (content_length > 0)
        {
            g_autofree gchar *body = g_malloc (content_length);

            if (!g_input_stream_read_all (G_INPUT_STREAM (data), body, content_length,
                                          NULL, NULL, NULL))
                return;
        }

        g_usleep ((gulong) server->delay_ms * 1000);
        g_atomic_int_inc (&server->requests);
        if (!g_output_stream_write_all (out, reply, sizeof reply - 1, NULL, NULL, NULL))
            return;
    }
}

static gpointer
slow_server_thread (gpointer user_data)
{
    SlowServer *server = user_data;

    for (;;)
    {
        g_autoptr(GSocketConnection) connection = NULL;

        connection = g_socket_listener_accept (server->listener, NULL,
                                               server->cancellable, NULL);
        if (connection == NULL)
            return NULL;
        slow_server_serve (server, connection);
    }
}

static SlowServer *
slow_server_new (guint delay_ms)
{
    SlowServer *server;

    server = g_new0 (SlowServer, 1);
    server->delay_ms = delay_ms;
    server->cancellable = g_cancellable_new ();
    server->listener = g_socket_listener_new ();
    server->port = g_socket_listener_add_any_inet_port (server->listener, NULL, NULL);
    g_assert_cmpuint (server->port, !=, 0);
    server->thread = g_thread_new ("slow-server", slow_server_thread, server);
    return server;
}

/* Cancelling wakes the server thread from accept() or a read. */
static void
slow_server_free (SlowServer *server)
{
    g_cancellable_cancel (server->cancellable);
    g_thread_join (server->thread);
    g_socket_listener_close (server->listener);
    g_object_unref (server->listener);
    g_object_unref (server->cancellable);
    g_free (server);
}

/* A full batch must not wait on the network: the POST happens on the
 * sender thread, and flush waits for it. */
static void
test_analytics_http_batch_does_not_block (void)
{
    g_autoptr(LrgAnalyticsBackendHttp) backend = NULL;
    g_autoptr(GError)                  error = NULL;
    g_autofree gchar                  *url = NULL;
    SlowServer                        *server;
    gint64                             start;
    gint64                             elapsed;
    guint                              i;

    server = slow_server_new (300);
    url = g_strdup_printf ("http://127.0.0.1:%u/events", server->port);
    backend = lrg_analytics_backend_http_new (url);
    lrg_analytics_backend_http_set_batch_size (backend, 4);
    lrg_analytics_backend_http_set_flush_interval (backend, 0);

    start = g_get_monotonic_time ();
    for (i = 0; i < 8; i++)
    {
        g_autoptr(LrgAnalyticsEvent) event = lrg_analytics_event_new ("tick");

        g_assert_true (lrg_analytics_backend_send_event (LRG_ANALYTICS_BACKEND (backend),
                                                         event, &error));
        g_assert_no_error (error);
    }
    elapsed = g_get_monotonic_time () - start;

    /* Two full batches were queued; nothing waited out a 300 ms reply. */
    g_assert_cmpint (elapsed, <, 150 * G_TIME_SPAN_MILLISECOND);
    g_assert_cmpuint (lrg_analytics_backend_http_get_pending_count (backend), >, 0);

    g_assert_true (lrg_analytics_backend_flush (LRG_ANALYTICS_BACKEND (backend), &error));
    g_assert_no_error (error);
    g_assert_cmpint (g_atomic_int_get (&server->requests), ==, 2);
    g_assert_cmpuint (lrg_analytics_backend_http_get_pending_count (backend), ==, 0);

    g_clear_object (&backend);
    slow_server_free (server);
}

/* A batch that cannot be delivered is kept, counted as pending, reported
 * by flush, and does not block the event that filled it. */
static void
test_analytics_http_failed_batch_kept (void)
{
    g_autoptr(LrgAnalyticsBackendHttp) backend = NULL;
    g_autoptr(GError)                  error = NULL;
    g_autoptr(GSocketListener)         probe = NULL;
    g_autofree gchar                  *url = NULL;
    guint16                            port;
    guint                              i;
    GLogLevelFlags                     fatal_mask;

    /* A port that was free a moment ago: connections are refused. */
    probe = g_socket_listener_new ();
    port = g_socket_listener_add_any_inet_port (probe, NULL, NULL);
    g_socket_listener_close (probe);

    /* The failure warnings come from another thread, where neither
     * g_test_expect_message() nor a fatal handler applies: make warnings
     * non-fatal for this test. */
    fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
    url = g_strdup_printf ("http://127.0.0.1:%u/events", port);

    /* With a 10 s retry delay a synchronous send would stall here. */
    backend = lrg_analytics_backend_http_new (url);
    lrg_analytics_backend_http_set_batch_size (backend, 3);
    lrg_analytics_backend_http_set_flush_interval (backend, 0);
    lrg_analytics_backend_http_set_retry_count (backend, 1);
    g_object_set (backend, "retry-delay-ms", 10000, NULL);
    for (i = 0; i < 3; i++)
    {
        g_autoptr(LrgAnalyticsEvent) event = lrg_analytics_event_new ("tick");
        gint64 start = g_get_monotonic_time ();

        g_assert_true (lrg_analytics_backend_send_event (LRG_ANALYTICS_BACKEND (backend),
                                                         event, &error));
        g_assert_cmpint (g_get_monotonic_time () - start, <, G_TIME_SPAN_SECOND);
    }
    g_assert_cmpuint (lrg_analytics_backend_http_get_pending_count (backend), ==, 3);

    /* Finalizing while the sender sleeps between retries returns promptly. */
    {
        gint64 start = g_get_monotonic_time ();

        g_clear_object (&backend);
        g_assert_cmpint (g_get_monotonic_time () - start, <, 5 * G_TIME_SPAN_SECOND);
    }

    /* Without retries: flush reports the failure and keeps the batch,
     * and the next flush tries it again. */
    backend = lrg_analytics_backend_http_new (url);
    lrg_analytics_backend_http_set_batch_size (backend, 3);
    lrg_analytics_backend_http_set_flush_interval (backend, 0);
    lrg_analytics_backend_http_set_retry_count (backend, 0);
    for (i = 0; i < 3; i++)
    {
        g_autoptr(LrgAnalyticsEvent) event = lrg_analytics_event_new ("tick");

        g_assert_true (lrg_analytics_backend_send_event (LRG_ANALYTICS_BACKEND (backend),
                                                         event, &error));
    }
    g_assert_false (lrg_analytics_backend_flush (LRG_ANALYTICS_BACKEND (backend), &error));
    g_assert_error (error, LRG_ANALYTICS_ERROR, LRG_ANALYTICS_ERROR_NETWORK);
    g_clear_error (&error);
    g_assert_cmpuint (lrg_analytics_backend_http_get_pending_count (backend), ==, 3);
    g_assert_false (lrg_analytics_backend_flush (LRG_ANALYTICS_BACKEND (backend), &error));
    g_assert_cmpuint (lrg_analytics_backend_http_get_pending_count (backend), ==, 3);

    g_clear_object (&backend);
    g_log_set_always_fatal (fatal_mask);
}

/* ==========================================================================
 * Test Cases - LrgAnalytics Manager
 * ========================================================================== */

static void
test_analytics_singleton (void)
{
    LrgAnalytics *a1;
    LrgAnalytics *a2;

    a1 = lrg_analytics_get_default ();
    a2 = lrg_analytics_get_default ();

    g_assert_nonnull (a1);
    g_assert_true (a1 == a2);
}

static void
test_analytics_enabled (AnalyticsFixture *fixture,
                        gconstpointer     user_data)
{
    lrg_analytics_set_enabled (fixture->analytics, TRUE);
    g_assert_true (lrg_analytics_get_enabled (fixture->analytics));

    lrg_analytics_set_enabled (fixture->analytics, FALSE);
    g_assert_false (lrg_analytics_get_enabled (fixture->analytics));
}

static void
test_analytics_session (AnalyticsFixture *fixture,
                        gconstpointer     user_data)
{
    const gchar *session_id;

    /* Before starting session */
    g_assert_null (lrg_analytics_get_session_id (fixture->analytics));

    /* Start session */
    lrg_analytics_start_session (fixture->analytics);
    session_id = lrg_analytics_get_session_id (fixture->analytics);
    g_assert_nonnull (session_id);
    g_assert_nonnull (lrg_analytics_get_session_start (fixture->analytics));

    /* End session - session_id remains set but session is inactive */
    lrg_analytics_end_session (fixture->analytics);
    /* Session ID is preserved, only cleared on new session start */
    g_assert_cmpstr (lrg_analytics_get_session_id (fixture->analytics), ==, session_id);
}

static void
test_analytics_play_time (AnalyticsFixture *fixture,
                          gconstpointer     user_data)
{
    lrg_analytics_start_session (fixture->analytics);

    g_assert_cmpfloat_with_epsilon (lrg_analytics_get_play_time (fixture->analytics),
                                    0.0, 0.001);

    /* Simulate time passing */
    lrg_analytics_update (fixture->analytics, 1.5f);
    g_assert_cmpfloat_with_epsilon (lrg_analytics_get_play_time (fixture->analytics),
                                    1.5, 0.001);

    lrg_analytics_update (fixture->analytics, 2.0f);
    g_assert_cmpfloat_with_epsilon (lrg_analytics_get_play_time (fixture->analytics),
                                    3.5, 0.001);

    lrg_analytics_end_session (fixture->analytics);
}

static void
test_analytics_track_event (AnalyticsFixture *fixture,
                            gconstpointer     user_data)
{
    g_autoptr(LrgAnalyticsEvent) event = NULL;

    lrg_analytics_start_session (fixture->analytics);

    event = lrg_analytics_event_new ("test_event");
    lrg_analytics_track_event (fixture->analytics, event);

    /* Check that the event was sent to the mock backend */
    g_assert_cmpuint (fixture->backend->events->len, ==, 1);

    lrg_analytics_end_session (fixture->analytics);
}

static void
test_analytics_track_simple (AnalyticsFixture *fixture,
                             gconstpointer     user_data)
{
    LrgAnalyticsEvent *sent;

    lrg_analytics_start_session (fixture->analytics);

    lrg_analytics_track_simple (fixture->analytics, "simple_event");

    /* Check that the event was sent */
    g_assert_cmpuint (fixture->backend->events->len, ==, 1);

    sent = g_ptr_array_index (fixture->backend->events, 0);
    g_assert_cmpstr (lrg_analytics_event_get_name (sent), ==, "simple_event");

    lrg_analytics_end_session (fixture->analytics);
}

static void
test_analytics_track_screen_view (AnalyticsFixture *fixture,
                                  gconstpointer     user_data)
{
    LrgAnalyticsEvent *sent;
    const gchar *screen;

    lrg_analytics_start_session (fixture->analytics);

    lrg_analytics_track_screen_view (fixture->analytics, "main_menu");

    g_assert_cmpuint (fixture->backend->events->len, ==, 1);

    sent = g_ptr_array_index (fixture->backend->events, 0);
    g_assert_cmpstr (lrg_analytics_event_get_name (sent), ==, "screen_view");

    screen = lrg_analytics_event_get_property_string (sent, "screen_name");
    g_assert_nonnull (screen);
    g_assert_cmpstr (screen, ==, "main_menu");

    lrg_analytics_end_session (fixture->analytics);
}

static void
test_analytics_track_game_start (AnalyticsFixture *fixture,
                                 gconstpointer     user_data)
{
    LrgAnalyticsEvent *sent;

    lrg_analytics_start_session (fixture->analytics);

    lrg_analytics_track_game_start (fixture->analytics);

    g_assert_cmpuint (fixture->backend->events->len, ==, 1);

    sent = g_ptr_array_index (fixture->backend->events, 0);
    g_assert_cmpstr (lrg_analytics_event_get_name (sent), ==, "game_start");

    lrg_analytics_end_session (fixture->analytics);
}

static void
test_analytics_track_level (AnalyticsFixture *fixture,
                            gconstpointer     user_data)
{
    LrgAnalyticsEvent *sent;
    const gchar *level_name;

    lrg_analytics_start_session (fixture->analytics);

    lrg_analytics_track_level_start (fixture->analytics, "level_1");

    g_assert_cmpuint (fixture->backend->events->len, ==, 1);
    sent = g_ptr_array_index (fixture->backend->events, 0);
    g_assert_cmpstr (lrg_analytics_event_get_name (sent), ==, "level_start");

    lrg_analytics_track_level_end (fixture->analytics, "level_1", TRUE);

    g_assert_cmpuint (fixture->backend->events->len, ==, 2);
    sent = g_ptr_array_index (fixture->backend->events, 1);
    g_assert_cmpstr (lrg_analytics_event_get_name (sent), ==, "level_end");

    /* Verify level name is present */
    level_name = lrg_analytics_event_get_property_string (sent, "level_name");
    g_assert_nonnull (level_name);
    g_assert_cmpstr (level_name, ==, "level_1");

    lrg_analytics_end_session (fixture->analytics);
}

static void
test_analytics_user_property (AnalyticsFixture *fixture,
                              gconstpointer     user_data)
{
    lrg_analytics_set_user_property (fixture->analytics, "user_type", "premium");

    lrg_analytics_start_session (fixture->analytics);
    lrg_analytics_track_simple (fixture->analytics, "test_event");

    /* User properties should be attached to events */
    g_assert_cmpuint (fixture->backend->events->len, ==, 1);

    lrg_analytics_end_session (fixture->analytics);
}

static void
test_analytics_counter (AnalyticsFixture *fixture,
                        gconstpointer     user_data)
{
    lrg_analytics_start_session (fixture->analytics);

    g_assert_cmpint (lrg_analytics_get_counter (fixture->analytics, "kills"), ==, 0);

    lrg_analytics_increment_counter (fixture->analytics, "kills", 5);
    g_assert_cmpint (lrg_analytics_get_counter (fixture->analytics, "kills"), ==, 5);

    lrg_analytics_increment_counter (fixture->analytics, "kills", 3);
    g_assert_cmpint (lrg_analytics_get_counter (fixture->analytics, "kills"), ==, 8);

    lrg_analytics_end_session (fixture->analytics);
}

static void
test_analytics_disabled_no_events (AnalyticsFixture *fixture,
                                   gconstpointer     user_data)
{
    lrg_analytics_set_enabled (fixture->analytics, FALSE);

    lrg_analytics_start_session (fixture->analytics);
    lrg_analytics_track_simple (fixture->analytics, "test_event");

    /* No events should be sent when disabled */
    g_assert_cmpuint (fixture->backend->events->len, ==, 0);

    lrg_analytics_end_session (fixture->analytics);
}

static void
test_analytics_flush (AnalyticsFixture *fixture,
                      gconstpointer     user_data)
{
    g_autoptr(GError) error = NULL;
    gboolean result;

    lrg_analytics_start_session (fixture->analytics);

    result = lrg_analytics_flush (fixture->analytics, &error);
    g_assert_true (result);
    g_assert_no_error (error);
    g_assert_true (fixture->backend->flush_called);

    lrg_analytics_end_session (fixture->analytics);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* LrgAnalyticsEvent tests */
    g_test_add_func ("/analytics/event/new", test_analytics_event_new);
    g_test_add_func ("/analytics/event/properties", test_analytics_event_properties);
    g_test_add_func ("/analytics/event/to_json", test_analytics_event_to_json);
    g_test_add_func ("/analytics/event/to_yaml", test_analytics_event_to_yaml);

    /* LrgConsent tests */
    g_test_add_func ("/analytics/consent/new", test_consent_new);
    g_test_add_func ("/analytics/consent/set_enabled", test_consent_set_enabled);
    g_test_add_func ("/analytics/consent/grant_all", test_consent_grant_all);
    g_test_add_func ("/analytics/consent/revoke_all", test_consent_revoke_all);
    g_test_add_func ("/analytics/consent/requires_prompt", test_consent_requires_prompt);

    /* LrgAnalyticsBackend tests */
    g_test_add_func ("/analytics/backend/enabled", test_analytics_backend_enabled);
    g_test_add_func ("/analytics/backend/name", test_analytics_backend_name);
    g_test_add_func ("/analytics/backend/send_event", test_analytics_backend_send_event);
    g_test_add_func ("/analytics/backend/flush", test_analytics_backend_flush);
    g_test_add_func ("/analytics/http/batch-does-not-block",
                     test_analytics_http_batch_does_not_block);
    g_test_add_func ("/analytics/http/failed-batch-kept",
                     test_analytics_http_failed_batch_kept);

    /* LrgAnalytics manager tests */
    g_test_add_func ("/analytics/manager/singleton", test_analytics_singleton);

    g_test_add ("/analytics/manager/enabled", AnalyticsFixture, NULL,
                analytics_fixture_set_up, test_analytics_enabled, analytics_fixture_tear_down);
    g_test_add ("/analytics/manager/session", AnalyticsFixture, NULL,
                analytics_fixture_set_up, test_analytics_session, analytics_fixture_tear_down);
    g_test_add ("/analytics/manager/play_time", AnalyticsFixture, NULL,
                analytics_fixture_set_up, test_analytics_play_time, analytics_fixture_tear_down);
    g_test_add ("/analytics/manager/track_event", AnalyticsFixture, NULL,
                analytics_fixture_set_up, test_analytics_track_event, analytics_fixture_tear_down);
    g_test_add ("/analytics/manager/track_simple", AnalyticsFixture, NULL,
                analytics_fixture_set_up, test_analytics_track_simple, analytics_fixture_tear_down);
    g_test_add ("/analytics/manager/track_screen_view", AnalyticsFixture, NULL,
                analytics_fixture_set_up, test_analytics_track_screen_view, analytics_fixture_tear_down);
    g_test_add ("/analytics/manager/track_game_start", AnalyticsFixture, NULL,
                analytics_fixture_set_up, test_analytics_track_game_start, analytics_fixture_tear_down);
    g_test_add ("/analytics/manager/track_level", AnalyticsFixture, NULL,
                analytics_fixture_set_up, test_analytics_track_level, analytics_fixture_tear_down);
    g_test_add ("/analytics/manager/user_property", AnalyticsFixture, NULL,
                analytics_fixture_set_up, test_analytics_user_property, analytics_fixture_tear_down);
    g_test_add ("/analytics/manager/counter", AnalyticsFixture, NULL,
                analytics_fixture_set_up, test_analytics_counter, analytics_fixture_tear_down);
    g_test_add ("/analytics/manager/disabled_no_events", AnalyticsFixture, NULL,
                analytics_fixture_set_up, test_analytics_disabled_no_events, analytics_fixture_tear_down);
    g_test_add ("/analytics/manager/flush", AnalyticsFixture, NULL,
                analytics_fixture_set_up, test_analytics_flush, analytics_fixture_tear_down);

    return g_test_run ();
}
