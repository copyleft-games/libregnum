/* test-optional-features.c - the stubs built when an optional dependency is off
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * libsoup (SOUP=0) and libpq (LIBPQ=0) can be left out of a build: the
 * Android target does, and `make test-minimal-features' builds the native
 * library that way (build/minimal) to run this suite.  Each feature then
 * compiles to a stub that fails with a documented error instead of
 * crashing or pretending to work (docs/reference/error-codes.org,
 * "Features built out").
 *
 * In a build that has the dependency, each case skips: the real
 * implementation is covered by test-analytics and test-mmo.
 */

#include <string.h>
#include <glib.h>
#include <libregnum.h>

/* SOUP=0: the HTTP analytics backend queues events, but every send fails
 * with LRG_ANALYTICS_ERROR_BACKEND and the events stay queued, as they
 * would for an unreachable server. */
static void
test_http_backend_without_soup (void)
{
#ifdef LRG_WITHOUT_SOUP
	g_autoptr(LrgAnalyticsBackendHttp) http = NULL;
	g_autoptr(LrgAnalyticsEvent)       event = NULL;
	g_autoptr(GError)                  error = NULL;

	http = lrg_analytics_backend_http_new ("http://127.0.0.1:9/events");
	g_assert_nonnull (http);
	lrg_analytics_backend_http_set_retry_count (http, 0);

	event = lrg_analytics_event_new ("level_start");
	g_assert_true (lrg_analytics_backend_send_event (LRG_ANALYTICS_BACKEND (http), event, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_analytics_backend_http_get_pending_count (http), ==, 1);

	/* flushing reports the stub's error and keeps the event */
	g_assert_false (lrg_analytics_backend_flush (LRG_ANALYTICS_BACKEND (http), &error));
	g_assert_error (error, LRG_ANALYTICS_ERROR, LRG_ANALYTICS_ERROR_BACKEND);
	g_assert_nonnull (strstr (error->message, "SOUP=0"));
#else
	g_test_skip ("built with libsoup (SOUP=1): test-analytics covers the backend");
#endif
}

/* LIBPQ=0: the PostgreSQL MMO store cannot be opened; it says why with
 * G_IO_ERROR_NOT_SUPPORTED, and the other stores still work. */
static void
test_postgres_store_without_libpq (void)
{
#ifdef LRG_WITHOUT_LIBPQ
	g_autoptr(LrgMmoStore) store = NULL;
	g_autoptr(LrgMmoStore) sqlite = NULL;
	g_autoptr(GError)      error = NULL;

	store = lrg_mmo_store_new_postgres ("dbname=lrg_unused", &error);
	g_assert_null (store);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
	g_assert_nonnull (strstr (error->message, "LIBPQ=0"));

	/* the SQLite store does not need libpq */
	g_clear_error (&error);
	sqlite = lrg_mmo_store_new (":memory:", &error);
	g_assert_no_error (error);
	g_assert_nonnull (sqlite);
#else
	g_test_skip ("built with libpq (LIBPQ=1): test-mmo covers the store");
#endif
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/optional/soup/http-backend", test_http_backend_without_soup);
	g_test_add_func ("/optional/libpq/postgres-store", test_postgres_store_without_libpq);

	return g_test_run ();
}
