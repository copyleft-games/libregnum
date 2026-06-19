/* test-standalone-host.c - Tests for the standalone host's window sizing
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Guards the fullscreen-sizing fix: an exclusive-fullscreen window must be sized
 * to the monitor BEFORE toggling fullscreen (raylib keeps the current
 * framebuffer size otherwise, so the scene renders into a small corner
 * rectangle). The pure decision is unit-tested headlessly via
 * lrg_standalone_host_fullscreen_target(); the real window behavior is checked
 * by an opt-in integration test (set LRG_TEST_WINDOW=1; needs a display) so the
 * normal suite never opens a window.
 */

#include <glib.h>
#include <libregnum.h>

/* ---- pure decision (headless, always runs) ------------------------------- */

static void
test_fullscreen_target_fullscreen (void)
{
    gint w = -1, h = -1;

    /* Exclusive fullscreen on a valid monitor -> resize to the monitor. */
    g_assert_true (lrg_standalone_host_fullscreen_target (
        LRG_FULLSCREEN_FULLSCREEN, 1920, 1080, &w, &h));
    g_assert_cmpint (w, ==, 1920);
    g_assert_cmpint (h, ==, 1080);
}

static void
test_fullscreen_target_windowed (void)
{
    gint w = 7, h = 7;

    /* Windowed keeps the requested size: no pre-resize. */
    g_assert_false (lrg_standalone_host_fullscreen_target (
        LRG_FULLSCREEN_WINDOWED, 1920, 1080, &w, &h));
    /* Out params left untouched. */
    g_assert_cmpint (w, ==, 7);
    g_assert_cmpint (h, ==, 7);
}

static void
test_fullscreen_target_borderless (void)
{
    /* Borderless sizes itself: no pre-resize. */
    g_assert_false (lrg_standalone_host_fullscreen_target (
        LRG_FULLSCREEN_BORDERLESS, 1920, 1080, NULL, NULL));
}

static void
test_fullscreen_target_bad_monitor (void)
{
    gint w = 5, h = 5;

    /* Defensive: a zero/invalid monitor size must not request a 0x0 window. */
    g_assert_false (lrg_standalone_host_fullscreen_target (
        LRG_FULLSCREEN_FULLSCREEN, 0, 0, &w, &h));
    g_assert_false (lrg_standalone_host_fullscreen_target (
        LRG_FULLSCREEN_FULLSCREEN, -1, 1080, &w, &h));
    g_assert_false (lrg_standalone_host_fullscreen_target (
        LRG_FULLSCREEN_FULLSCREEN, 1920, -1, &w, &h));
    g_assert_cmpint (w, ==, 5);
    g_assert_cmpint (h, ==, 5);
}

static void
test_fullscreen_target_null_out (void)
{
    /* NULL out-pointers must be tolerated. */
    g_assert_true (lrg_standalone_host_fullscreen_target (
        LRG_FULLSCREEN_FULLSCREEN, 1280, 720, NULL, NULL));
}

/* ---- real window behavior (opt-in; needs a display) ---------------------- */

/* Build a host from a template configured at @w x @h with @mode, and return the
 * actual window width the host produced (or -1). */
static gint
host_window_width (gint w, gint h, LrgFullscreenMode mode)
{
    g_autoptr(GError) error = NULL;
    LrgGameTemplate  *game;
    LrgStandaloneHost *host;
    LrgEngine        *engine;
    LrgWindow        *win;
    gint              actual = -1;

    game = g_object_new (LRG_TYPE_GAME_TEMPLATE,
                         "window-width", w,
                         "window-height", h,
                         "fullscreen-mode", mode,
                         NULL);
    host = lrg_standalone_host_new (game, &error);
    if (host == NULL)
    {
        g_test_message ("host init failed: %s",
                        error != NULL ? error->message : "unknown");
        g_object_unref (game);
        return -1;
    }

    engine = lrg_engine_get_default ();
    win = lrg_engine_get_window (engine);
    if (win != NULL)
        actual = lrg_window_get_width (win);

    lrg_standalone_host_teardown (host);
    g_object_unref (host);
    g_object_unref (game);
    return actual;
}

static void
test_fullscreen_fills_monitor (void)
{
    gint windowed, fullscreen;

    if (g_getenv ("LRG_TEST_WINDOW") == NULL)
    {
        g_test_skip ("set LRG_TEST_WINDOW=1 (needs a display) to run this");
        return;
    }

    /* A small windowed request is honored; exclusive fullscreen must grow to the
     * monitor (before the fix it stayed at the requested 320px). */
    windowed = host_window_width (320, 240, LRG_FULLSCREEN_WINDOWED);
    fullscreen = host_window_width (320, 240, LRG_FULLSCREEN_FULLSCREEN);

    g_test_message ("windowed=%d fullscreen=%d", windowed, fullscreen);
    g_assert_cmpint (windowed, >, 0);
    g_assert_cmpint (fullscreen, >, 0);
    g_assert_cmpint (fullscreen, >, windowed);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/standalone-host/target/fullscreen",
                     test_fullscreen_target_fullscreen);
    g_test_add_func ("/standalone-host/target/windowed",
                     test_fullscreen_target_windowed);
    g_test_add_func ("/standalone-host/target/borderless",
                     test_fullscreen_target_borderless);
    g_test_add_func ("/standalone-host/target/bad-monitor",
                     test_fullscreen_target_bad_monitor);
    g_test_add_func ("/standalone-host/target/null-out",
                     test_fullscreen_target_null_out);
    g_test_add_func ("/standalone-host/fullscreen-fills-monitor",
                     test_fullscreen_fills_monitor);

    return g_test_run ();
}
