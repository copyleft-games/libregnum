/* test-font-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for font manager module.
 */

#define LIBREGNUM_COMPILATION 1

#include <glib.h>
#include <glib-object.h>
#include "../src/libregnum.h"

/*
 * ============================================================================
 * LrgFontManager Tests
 * ============================================================================
 */

static void
test_font_manager_get_default (void)
{
    LrgFontManager *mgr1 = lrg_font_manager_get_default ();
    LrgFontManager *mgr2 = lrg_font_manager_get_default ();

    g_assert_nonnull (mgr1);
    g_assert_true (mgr1 == mgr2);  /* Same singleton instance */
}

static void
test_font_manager_initialize_with_sizes_validation (void)
{
    LrgFontManager *mgr = NULL;

    /* Create a fresh instance for this test */
    mgr = g_object_new (LRG_TYPE_FONT_MANAGER, NULL);

    /* Invalid sizes should fail via g_return_val_if_fail */
    /* Note: These log warnings but don't crash */
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL,
                           "*size_small > 0*");
    lrg_font_manager_initialize_with_sizes (mgr, 0, 16, 24, NULL);
    g_test_assert_expected_messages ();

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL,
                           "*size_normal > 0*");
    lrg_font_manager_initialize_with_sizes (mgr, 12, 0, 24, NULL);
    g_test_assert_expected_messages ();

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL,
                           "*size_large > 0*");
    lrg_font_manager_initialize_with_sizes (mgr, 12, 16, 0, NULL);
    g_test_assert_expected_messages ();

    g_object_unref (mgr);
}

static void
test_font_manager_load_font_validation (void)
{
    LrgFontManager *mgr = NULL;

    mgr = g_object_new (LRG_TYPE_FONT_MANAGER, NULL);

    /* NULL name should fail */
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL,
                           "*name != NULL*");
    lrg_font_manager_load_font (mgr, NULL, "/path/to/font.ttf", 16, NULL);
    g_test_assert_expected_messages ();

    /* NULL path should fail */
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL,
                           "*path != NULL*");
    lrg_font_manager_load_font (mgr, "test", NULL, 16, NULL);
    g_test_assert_expected_messages ();

    /* Invalid size should fail */
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL,
                           "*size > 0*");
    lrg_font_manager_load_font (mgr, "test", "/path/to/font.ttf", 0, NULL);
    g_test_assert_expected_messages ();

    g_object_unref (mgr);
}

static void
test_font_manager_has_font (void)
{
    LrgFontManager *mgr = NULL;

    mgr = g_object_new (LRG_TYPE_FONT_MANAGER, NULL);

    /* Should not have any fonts initially */
    g_assert_false (lrg_font_manager_has_font (mgr, "ui-normal"));
    g_assert_false (lrg_font_manager_has_font (mgr, "nonexistent"));

    g_object_unref (mgr);
}

static void
test_font_manager_default_font_name (void)
{
    LrgFontManager *mgr = NULL;

    mgr = g_object_new (LRG_TYPE_FONT_MANAGER, NULL);

    /* Default font name should be NULL initially */
    g_assert_null (lrg_font_manager_get_default_font_name (mgr));

    /* Setting to non-loaded font should warn */
    g_test_expect_message ("Libregnum-Text", G_LOG_LEVEL_WARNING,
                           "*not loaded*");
    lrg_font_manager_set_default_font_name (mgr, "nonexistent");
    g_test_assert_expected_messages ();

    g_object_unref (mgr);
}

static void
test_font_manager_get_font_names (void)
{
    LrgFontManager *mgr = NULL;
    GPtrArray      *names = NULL;

    mgr = g_object_new (LRG_TYPE_FONT_MANAGER, NULL);

    names = lrg_font_manager_get_font_names (mgr);
    g_assert_nonnull (names);
    g_assert_cmpuint (names->len, ==, 0);

    g_ptr_array_unref (names);
    g_object_unref (mgr);
}

static void
test_font_manager_unload_all (void)
{
    LrgFontManager *mgr = NULL;
    GPtrArray      *names = NULL;

    mgr = g_object_new (LRG_TYPE_FONT_MANAGER, NULL);

    /* Unload all should not crash on empty manager */
    lrg_font_manager_unload_all (mgr);

    names = lrg_font_manager_get_font_names (mgr);
    g_assert_cmpuint (names->len, ==, 0);

    g_ptr_array_unref (names);
    g_object_unref (mgr);
}

/*
 * ============================================================================
 * Main
 * ============================================================================
 */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Font manager singleton test */
    g_test_add_func ("/font-manager/get-default", test_font_manager_get_default);

    /* Validation tests */
    g_test_add_func ("/font-manager/initialize-with-sizes/validation",
                     test_font_manager_initialize_with_sizes_validation);
    g_test_add_func ("/font-manager/load-font/validation",
                     test_font_manager_load_font_validation);

    /* API tests */
    g_test_add_func ("/font-manager/has-font", test_font_manager_has_font);
    g_test_add_func ("/font-manager/default-font-name", test_font_manager_default_font_name);
    g_test_add_func ("/font-manager/get-font-names", test_font_manager_get_font_names);
    g_test_add_func ("/font-manager/unload-all", test_font_manager_unload_all);

    return g_test_run ();
}
