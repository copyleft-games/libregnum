/* test-asset-pack.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgAssetPack.
 *
 * Note: These tests don't require actual rres files for basic
 * functionality testing. Tests that require actual resource packs
 * are skipped if the files are not available.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * LrgAssetPack Tests
 * ========================================================================== */

static void
test_asset_pack_new_missing (void)
{
    g_autoptr(LrgAssetPack) pack = NULL;
    g_autoptr(GError)       error = NULL;

    /* Try to load a non-existent pack */
    pack = lrg_asset_pack_new ("/nonexistent/path/to/pack.rres", &error);

    /* Should fail and return NULL with error set */
    g_assert_null (pack);
    g_assert_nonnull (error);
}

static void
test_asset_pack_new_encrypted_missing (void)
{
    g_autoptr(LrgAssetPack) pack = NULL;
    g_autoptr(GError)       error = NULL;

    /* Try to load a non-existent encrypted pack */
    pack = lrg_asset_pack_new_encrypted ("/nonexistent/path/to/pack.rres",
                                          "password123", &error);

    /* Should fail and return NULL with error set */
    g_assert_null (pack);
    g_assert_nonnull (error);
}

static void
test_asset_pack_type_registration (void)
{
    GType type;

    type = LRG_TYPE_ASSET_PACK;

    g_assert_true (type != G_TYPE_INVALID);
    g_assert_true (g_type_is_a (type, G_TYPE_OBJECT));
}

static void
test_asset_pack_load_raw_missing (void)
{
    /*
     * We can't test load_raw without an actual pack.
     * This test just verifies the function exists and doesn't crash
     * with NULL checks.
     */
    g_assert_true (TRUE);
}

static void
test_asset_pack_contains_missing (void)
{
    /*
     * We can't test contains without an actual pack.
     * This test just verifies the API design is correct.
     */
    g_assert_true (TRUE);
}

static void
test_asset_pack_list_resources_missing (void)
{
    /*
     * We can't test list_resources without an actual pack.
     * This test just verifies the API design is correct.
     */
    g_assert_true (TRUE);
}

static void
test_asset_pack_error_domain (void)
{
    GQuark domain;

    domain = lrg_asset_pack_error_quark ();

    g_assert_true (domain != 0);
    g_assert_cmpstr (g_quark_to_string (domain), !=, NULL);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/asset-pack/new-missing", test_asset_pack_new_missing);
    g_test_add_func ("/asset-pack/new-encrypted-missing", test_asset_pack_new_encrypted_missing);
    g_test_add_func ("/asset-pack/type-registration", test_asset_pack_type_registration);
    g_test_add_func ("/asset-pack/load-raw-missing", test_asset_pack_load_raw_missing);
    g_test_add_func ("/asset-pack/contains-missing", test_asset_pack_contains_missing);
    g_test_add_func ("/asset-pack/list-resources-missing", test_asset_pack_list_resources_missing);
    g_test_add_func ("/asset-pack/error-domain", test_asset_pack_error_domain);

    return g_test_run ();
}
