/* test-steam.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for Steam integration module.
 *
 * These tests verify the GObject interface and stub behavior.
 * They do NOT require Steam to be running or the Steam SDK.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Cases - LrgSteamService Interface
 * ========================================================================== */

static void
test_steam_service_interface_type (void)
{
    GType type;

    type = lrg_steam_service_get_type ();
    g_assert_cmpuint (type, !=, G_TYPE_INVALID);
    g_assert_true (G_TYPE_IS_INTERFACE (type));
}

/* ==========================================================================
 * Test Cases - LrgSteamStub Construction
 * ========================================================================== */

static void
test_steam_stub_new (void)
{
    g_autoptr(LrgSteamStub) stub = NULL;

    stub = lrg_steam_stub_new ();

    g_assert_nonnull (stub);
    g_assert_true (LRG_IS_STEAM_STUB (stub));
    g_assert_true (LRG_IS_STEAM_SERVICE (stub));
}

static void
test_steam_stub_is_available (void)
{
    g_autoptr(LrgSteamStub) stub = NULL;

    stub = lrg_steam_stub_new ();

    /* Stub should report Steam as not available */
    g_assert_false (lrg_steam_service_is_available (LRG_STEAM_SERVICE (stub)));
}

static void
test_steam_stub_init (void)
{
    g_autoptr(LrgSteamStub) stub = NULL;
    g_autoptr(GError)       error = NULL;
    gboolean                result;

    stub = lrg_steam_stub_new ();

    /* Stub init should succeed (allows running without Steam) */
    result = lrg_steam_service_init (LRG_STEAM_SERVICE (stub), 480, &error);
    g_assert_no_error (error);
    g_assert_true (result);

    /* Still not available (no actual Steam) */
    g_assert_false (lrg_steam_service_is_available (LRG_STEAM_SERVICE (stub)));
}

static void
test_steam_stub_shutdown (void)
{
    g_autoptr(LrgSteamStub) stub = NULL;
    g_autoptr(GError)       error = NULL;

    stub = lrg_steam_stub_new ();

    lrg_steam_service_init (LRG_STEAM_SERVICE (stub), 480, &error);
    g_assert_no_error (error);

    /* Shutdown should not crash */
    lrg_steam_service_shutdown (LRG_STEAM_SERVICE (stub));
}

static void
test_steam_stub_run_callbacks (void)
{
    g_autoptr(LrgSteamStub) stub = NULL;
    g_autoptr(GError)       error = NULL;

    stub = lrg_steam_stub_new ();

    lrg_steam_service_init (LRG_STEAM_SERVICE (stub), 480, &error);
    g_assert_no_error (error);

    /* Run callbacks should not crash */
    lrg_steam_service_run_callbacks (LRG_STEAM_SERVICE (stub));
    lrg_steam_service_run_callbacks (LRG_STEAM_SERVICE (stub));
    lrg_steam_service_run_callbacks (LRG_STEAM_SERVICE (stub));
}

/* ==========================================================================
 * Test Cases - LrgSteamClient Construction
 * ========================================================================== */

static void
test_steam_client_new (void)
{
    g_autoptr(LrgSteamClient) client = NULL;

    client = lrg_steam_client_new ();

    g_assert_nonnull (client);
    g_assert_true (LRG_IS_STEAM_CLIENT (client));
    g_assert_true (LRG_IS_STEAM_SERVICE (client));
}

static void
test_steam_client_init_without_steam (void)
{
    g_autoptr(LrgSteamClient) client = NULL;
    g_autoptr(GError)         error = NULL;
    gboolean                  result;

    client = lrg_steam_client_new ();

    /* Without Steam running, init should fail */
    result = lrg_steam_service_init (LRG_STEAM_SERVICE (client), 480, &error);

    /*
     * The result depends on whether the library was built with STEAM=1
     * and whether Steam is running. In test environment, we expect failure.
     */
#ifndef LRG_ENABLE_STEAM
    /* Built without Steam support - should fail with NOT_SUPPORTED */
    g_assert_false (result);
    g_assert_error (error, LRG_STEAM_CLIENT_ERROR, LRG_STEAM_CLIENT_ERROR_NOT_SUPPORTED);
#else
    /* Built with Steam support but Steam likely not running */
    if (!result)
    {
        g_assert_nonnull (error);
        g_assert_cmpuint (error->domain, ==, LRG_STEAM_CLIENT_ERROR);
    }
#endif
}

static void
test_steam_client_is_logged_on (void)
{
    g_autoptr(LrgSteamClient) client = NULL;

    client = lrg_steam_client_new ();

    /* Without initialization, should return FALSE */
    g_assert_false (lrg_steam_client_is_logged_on (client));
}

static void
test_steam_client_get_steam_id (void)
{
    g_autoptr(LrgSteamClient) client = NULL;
    guint64                   steam_id;

    client = lrg_steam_client_new ();

    /* Without initialization, should return 0 */
    steam_id = lrg_steam_client_get_steam_id (client);
    g_assert_cmpuint (steam_id, ==, 0);
}

static void
test_steam_client_get_persona_name (void)
{
    g_autoptr(LrgSteamClient) client = NULL;
    const gchar              *name;

    client = lrg_steam_client_new ();

    /* Without initialization, should return NULL */
    name = lrg_steam_client_get_persona_name (client);
    g_assert_null (name);
}

static void
test_steam_client_get_app_id (void)
{
    g_autoptr(LrgSteamClient) client = NULL;
    guint32                   app_id;

    client = lrg_steam_client_new ();

    /* Without initialization, should return 0 */
    app_id = lrg_steam_client_get_app_id (client);
    g_assert_cmpuint (app_id, ==, 0);
}

/* ==========================================================================
 * Test Cases - LrgSteamAchievements
 * ========================================================================== */

static void
test_steam_achievements_new (void)
{
    g_autoptr(LrgSteamClient)       client = NULL;
    g_autoptr(LrgSteamAchievements) achievements = NULL;

    client = lrg_steam_client_new ();
    achievements = lrg_steam_achievements_new (client);

    g_assert_nonnull (achievements);
    g_assert_true (LRG_IS_STEAM_ACHIEVEMENTS (achievements));
}

static void
test_steam_achievements_unlock (void)
{
    g_autoptr(LrgSteamClient)       client = NULL;
    g_autoptr(LrgSteamAchievements) achievements = NULL;
    gboolean                        result;

    client = lrg_steam_client_new ();
    achievements = lrg_steam_achievements_new (client);

    /* Without Steam SDK, unlock returns TRUE as no-op success */
    result = lrg_steam_achievements_unlock (achievements, "ACH_TEST", NULL);
    g_assert_true (result);
}

static void
test_steam_achievements_is_unlocked (void)
{
    g_autoptr(LrgSteamClient)       client = NULL;
    g_autoptr(LrgSteamAchievements) achievements = NULL;
    gboolean                        unlocked;

    client = lrg_steam_client_new ();
    achievements = lrg_steam_achievements_new (client);

    /* Without Steam, is_unlocked should return FALSE */
    unlocked = lrg_steam_achievements_is_unlocked (achievements, "ACH_TEST");
    g_assert_false (unlocked);
}

/* ==========================================================================
 * Test Cases - LrgSteamCloud
 * ========================================================================== */

static void
test_steam_cloud_new (void)
{
    g_autoptr(LrgSteamClient) client = NULL;
    g_autoptr(LrgSteamCloud)  cloud = NULL;

    client = lrg_steam_client_new ();
    cloud = lrg_steam_cloud_new (client);

    g_assert_nonnull (cloud);
    g_assert_true (LRG_IS_STEAM_CLOUD (cloud));
}

static void
test_steam_cloud_write (void)
{
    g_autoptr(LrgSteamClient) client = NULL;
    g_autoptr(LrgSteamCloud)  cloud = NULL;
    g_autoptr(GBytes)         data = NULL;
    gboolean                  result;

    client = lrg_steam_client_new ();
    cloud = lrg_steam_cloud_new (client);

    data = g_bytes_new_static ("test data", 9);

    /* Without Steam SDK, write returns TRUE as no-op success */
    result = lrg_steam_cloud_write (cloud, "save.dat", data, NULL);
    g_assert_true (result);
}

static void
test_steam_cloud_read (void)
{
    g_autoptr(LrgSteamClient) client = NULL;
    g_autoptr(LrgSteamCloud)  cloud = NULL;
    g_autoptr(GBytes)         data = NULL;

    client = lrg_steam_client_new ();
    cloud = lrg_steam_cloud_new (client);

    /* Without Steam, read should return NULL */
    data = lrg_steam_cloud_read (cloud, "save.dat", NULL);
    g_assert_null (data);
}

static void
test_steam_cloud_exists (void)
{
    g_autoptr(LrgSteamClient) client = NULL;
    g_autoptr(LrgSteamCloud)  cloud = NULL;
    gboolean                  exists;

    client = lrg_steam_client_new ();
    cloud = lrg_steam_cloud_new (client);

    /* Without Steam, exists should return FALSE */
    exists = lrg_steam_cloud_exists (cloud, "save.dat");
    g_assert_false (exists);
}

/* ==========================================================================
 * Test Cases - LrgSteamStats
 * ========================================================================== */

static void
test_steam_stats_new (void)
{
    g_autoptr(LrgSteamClient) client = NULL;
    g_autoptr(LrgSteamStats)  stats = NULL;

    client = lrg_steam_client_new ();
    stats = lrg_steam_stats_new (client);

    g_assert_nonnull (stats);
    g_assert_true (LRG_IS_STEAM_STATS (stats));
}

static void
test_steam_stats_get_int (void)
{
    g_autoptr(LrgSteamClient) client = NULL;
    g_autoptr(LrgSteamStats)  stats = NULL;
    gint32                    value = 999;
    gboolean                  result;

    client = lrg_steam_client_new ();
    stats = lrg_steam_stats_new (client);

    /* Without Steam, get_int should return FALSE */
    result = lrg_steam_stats_get_int (stats, "STAT_KILLS", &value);
    g_assert_false (result);
    g_assert_cmpint (value, ==, 0);  /* Should be zeroed on failure */
}

static void
test_steam_stats_set_int (void)
{
    g_autoptr(LrgSteamClient) client = NULL;
    g_autoptr(LrgSteamStats)  stats = NULL;
    gboolean                  result;

    client = lrg_steam_client_new ();
    stats = lrg_steam_stats_new (client);

    /* Without Steam, set_int should handle gracefully */
    result = lrg_steam_stats_set_int (stats, "STAT_KILLS", 100);

    /*
     * Behavior depends on build:
     * - With STEAM=0: Returns TRUE (stub no-op)
     * - With STEAM=1 but no Steam: Returns FALSE
     */
#ifndef LRG_ENABLE_STEAM
    g_assert_true (result);  /* Stub returns TRUE */
#else
    g_assert_false (result);  /* Real implementation fails without Steam */
#endif
}

static void
test_steam_stats_get_float (void)
{
    g_autoptr(LrgSteamClient) client = NULL;
    g_autoptr(LrgSteamStats)  stats = NULL;
    gfloat                    value = 999.0f;
    gboolean                  result;

    client = lrg_steam_client_new ();
    stats = lrg_steam_stats_new (client);

    result = lrg_steam_stats_get_float (stats, "STAT_DISTANCE", &value);
    g_assert_false (result);
    g_assert_cmpfloat_with_epsilon (value, 0.0f, 0.001f);
}

/* ==========================================================================
 * Test Cases - LrgSteamPresence
 * ========================================================================== */

static void
test_steam_presence_new (void)
{
    g_autoptr(LrgSteamClient)   client = NULL;
    g_autoptr(LrgSteamPresence) presence = NULL;

    client = lrg_steam_client_new ();
    presence = lrg_steam_presence_new (client);

    g_assert_nonnull (presence);
    g_assert_true (LRG_IS_STEAM_PRESENCE (presence));
}

static void
test_steam_presence_set (void)
{
    g_autoptr(LrgSteamClient)   client = NULL;
    g_autoptr(LrgSteamPresence) presence = NULL;
    gboolean                    result;

    client = lrg_steam_client_new ();
    presence = lrg_steam_presence_new (client);

    /* Without Steam, set should handle gracefully */
    result = lrg_steam_presence_set (presence, "status", "In Main Menu");

#ifndef LRG_ENABLE_STEAM
    g_assert_true (result);  /* Stub returns TRUE */
#else
    g_assert_false (result);  /* Real implementation fails without Steam */
#endif
}

static void
test_steam_presence_set_status (void)
{
    g_autoptr(LrgSteamClient)   client = NULL;
    g_autoptr(LrgSteamPresence) presence = NULL;
    gboolean                    result;

    client = lrg_steam_client_new ();
    presence = lrg_steam_presence_new (client);

    result = lrg_steam_presence_set_status (presence, "Playing Level 5");

#ifndef LRG_ENABLE_STEAM
    g_assert_true (result);
#else
    g_assert_false (result);
#endif
}

static void
test_steam_presence_clear (void)
{
    g_autoptr(LrgSteamClient)   client = NULL;
    g_autoptr(LrgSteamPresence) presence = NULL;

    client = lrg_steam_client_new ();
    presence = lrg_steam_presence_new (client);

    /* Clear should not crash */
    lrg_steam_presence_clear (presence);
}

/* ==========================================================================
 * Test Cases - Error Quarks
 * ========================================================================== */

static void
test_steam_client_error_quark (void)
{
    GQuark quark;

    quark = lrg_steam_client_error_quark ();
    g_assert_cmpuint (quark, !=, 0);
    g_assert_cmpstr (g_quark_to_string (quark), ==, "lrg-steam-client-error-quark");
}

static void
test_steam_achievements_error_quark (void)
{
    GQuark quark;

    quark = lrg_steam_achievements_error_quark ();
    g_assert_cmpuint (quark, !=, 0);
    g_assert_cmpstr (g_quark_to_string (quark), ==, "lrg-steam-achievements-error-quark");
}

static void
test_steam_cloud_error_quark (void)
{
    GQuark quark;

    quark = lrg_steam_cloud_error_quark ();
    g_assert_cmpuint (quark, !=, 0);
    g_assert_cmpstr (g_quark_to_string (quark), ==, "lrg-steam-cloud-error-quark");
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Interface */
    g_test_add_func ("/steam/service/interface-type", test_steam_service_interface_type);

    /* Stub */
    g_test_add_func ("/steam/stub/new", test_steam_stub_new);
    g_test_add_func ("/steam/stub/is-available", test_steam_stub_is_available);
    g_test_add_func ("/steam/stub/init", test_steam_stub_init);
    g_test_add_func ("/steam/stub/shutdown", test_steam_stub_shutdown);
    g_test_add_func ("/steam/stub/run-callbacks", test_steam_stub_run_callbacks);

    /* Client */
    g_test_add_func ("/steam/client/new", test_steam_client_new);
    g_test_add_func ("/steam/client/init-without-steam", test_steam_client_init_without_steam);
    g_test_add_func ("/steam/client/is-logged-on", test_steam_client_is_logged_on);
    g_test_add_func ("/steam/client/get-steam-id", test_steam_client_get_steam_id);
    g_test_add_func ("/steam/client/get-persona-name", test_steam_client_get_persona_name);
    g_test_add_func ("/steam/client/get-app-id", test_steam_client_get_app_id);

    /* Achievements */
    g_test_add_func ("/steam/achievements/new", test_steam_achievements_new);
    g_test_add_func ("/steam/achievements/unlock", test_steam_achievements_unlock);
    g_test_add_func ("/steam/achievements/is-unlocked", test_steam_achievements_is_unlocked);

    /* Cloud */
    g_test_add_func ("/steam/cloud/new", test_steam_cloud_new);
    g_test_add_func ("/steam/cloud/write", test_steam_cloud_write);
    g_test_add_func ("/steam/cloud/read", test_steam_cloud_read);
    g_test_add_func ("/steam/cloud/exists", test_steam_cloud_exists);

    /* Stats */
    g_test_add_func ("/steam/stats/new", test_steam_stats_new);
    g_test_add_func ("/steam/stats/get-int", test_steam_stats_get_int);
    g_test_add_func ("/steam/stats/set-int", test_steam_stats_set_int);
    g_test_add_func ("/steam/stats/get-float", test_steam_stats_get_float);

    /* Presence */
    g_test_add_func ("/steam/presence/new", test_steam_presence_new);
    g_test_add_func ("/steam/presence/set", test_steam_presence_set);
    g_test_add_func ("/steam/presence/set-status", test_steam_presence_set_status);
    g_test_add_func ("/steam/presence/clear", test_steam_presence_clear);

    /* Error Quarks */
    g_test_add_func ("/steam/error-quark/client", test_steam_client_error_quark);
    g_test_add_func ("/steam/error-quark/achievements", test_steam_achievements_error_quark);
    g_test_add_func ("/steam/error-quark/cloud", test_steam_cloud_error_quark);

    return g_test_run ();
}
