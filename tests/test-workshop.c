/* test-workshop.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for Steam Workshop integration.
 */

#include <glib.h>
#include <glib-object.h>

#include "../src/lrg-types.h"
#include "../src/lrg-enums.h"
#include "../src/steam/lrg-workshop-item.h"
#include "../src/steam/lrg-workshop-query.h"
#include "../src/steam/lrg-workshop-manager.h"

/* ==========================================================================
 * Workshop Item Tests
 * ========================================================================== */

static void
test_workshop_item_new (void)
{
    g_autoptr(LrgWorkshopItem) item = NULL;

    item = lrg_workshop_item_new (12345);
    g_assert_nonnull (item);
    g_assert_cmpuint (lrg_workshop_item_get_file_id (item), ==, 12345);
}

static void
test_workshop_item_title (void)
{
    g_autoptr(LrgWorkshopItem) item = NULL;

    item = lrg_workshop_item_new (1);
    g_assert_null (lrg_workshop_item_get_title (item));

    lrg_workshop_item_set_title (item, "Test Mod");
    g_assert_cmpstr (lrg_workshop_item_get_title (item), ==, "Test Mod");

    lrg_workshop_item_set_title (item, NULL);
    g_assert_null (lrg_workshop_item_get_title (item));
}

static void
test_workshop_item_description (void)
{
    g_autoptr(LrgWorkshopItem) item = NULL;

    item = lrg_workshop_item_new (1);

    lrg_workshop_item_set_description (item, "A great mod");
    g_assert_cmpstr (lrg_workshop_item_get_description (item), ==, "A great mod");
}

static void
test_workshop_item_visibility (void)
{
    g_autoptr(LrgWorkshopItem) item = NULL;

    item = lrg_workshop_item_new (1);
    g_assert_cmpint (lrg_workshop_item_get_visibility (item), ==, LRG_WORKSHOP_ITEM_VISIBILITY_PUBLIC);

    lrg_workshop_item_set_visibility (item, LRG_WORKSHOP_ITEM_VISIBILITY_PRIVATE);
    g_assert_cmpint (lrg_workshop_item_get_visibility (item), ==, LRG_WORKSHOP_ITEM_VISIBILITY_PRIVATE);
}

static void
test_workshop_item_tags (void)
{
    g_autoptr(LrgWorkshopItem) item = NULL;
    const gchar * const *tags;

    item = lrg_workshop_item_new (1);

    /* Add tags */
    lrg_workshop_item_add_tag (item, "weapons");
    lrg_workshop_item_add_tag (item, "armor");

    tags = lrg_workshop_item_get_tags (item);
    g_assert_nonnull (tags);
    g_assert_cmpstr (tags[0], ==, "weapons");
    g_assert_cmpstr (tags[1], ==, "armor");

    /* Remove tag */
    g_assert_true (lrg_workshop_item_remove_tag (item, "weapons"));
    g_assert_false (lrg_workshop_item_remove_tag (item, "nonexistent"));

    tags = lrg_workshop_item_get_tags (item);
    g_assert_cmpstr (tags[0], ==, "armor");
}

static void
test_workshop_item_tags_set (void)
{
    g_autoptr(LrgWorkshopItem) item = NULL;
    const gchar *new_tags[] = { "magic", "spells", NULL };
    const gchar * const *tags;

    item = lrg_workshop_item_new (1);

    lrg_workshop_item_set_tags (item, new_tags);
    tags = lrg_workshop_item_get_tags (item);
    g_assert_cmpstr (tags[0], ==, "magic");
    g_assert_cmpstr (tags[1], ==, "spells");
}

static void
test_workshop_item_state (void)
{
    g_autoptr(LrgWorkshopItem) item = NULL;

    item = lrg_workshop_item_new (1);
    g_assert_cmpuint (lrg_workshop_item_get_state (item), ==, LRG_WORKSHOP_ITEM_STATE_NONE);

    g_object_set (item, "state",
                  LRG_WORKSHOP_ITEM_STATE_SUBSCRIBED | LRG_WORKSHOP_ITEM_STATE_INSTALLED,
                  NULL);

    g_assert_true (lrg_workshop_item_is_subscribed (item));
    g_assert_true (lrg_workshop_item_is_installed (item));
    g_assert_false (lrg_workshop_item_needs_update (item));
}

static void
test_workshop_item_properties (void)
{
    g_autoptr(LrgWorkshopItem) item = NULL;
    guint64 file_id;
    gchar *title;
    gfloat score;

    item = g_object_new (LRG_TYPE_WORKSHOP_ITEM,
                         "file-id", (guint64)999,
                         "title", "Property Test",
                         "score", 0.85f,
                         NULL);

    g_object_get (item,
                  "file-id", &file_id,
                  "title", &title,
                  "score", &score,
                  NULL);

    g_assert_cmpuint (file_id, ==, 999);
    g_assert_cmpstr (title, ==, "Property Test");
    g_assert_cmpfloat_with_epsilon (score, 0.85f, 0.001f);

    g_free (title);
}

/* ==========================================================================
 * Workshop Query Tests
 * ========================================================================== */

static void
test_workshop_query_new (void)
{
    g_autoptr(LrgWorkshopQuery) query = NULL;

    query = lrg_workshop_query_new (LRG_WORKSHOP_QUERY_RANKED_BY_VOTE);
    g_assert_nonnull (query);
    g_assert_cmpint (lrg_workshop_query_get_query_type (query), ==, LRG_WORKSHOP_QUERY_RANKED_BY_VOTE);
}

static void
test_workshop_query_new_for_user (void)
{
    g_autoptr(LrgWorkshopQuery) query = NULL;

    query = lrg_workshop_query_new_for_user (76561198012345678);
    g_assert_nonnull (query);
    g_assert_true (lrg_workshop_query_is_user_query (query));
    g_assert_cmpuint (lrg_workshop_query_get_user_id (query), ==, 76561198012345678);
}

static void
test_workshop_query_content_type (void)
{
    g_autoptr(LrgWorkshopQuery) query = NULL;

    query = lrg_workshop_query_new (LRG_WORKSHOP_QUERY_RANKED_BY_VOTE);
    g_assert_cmpint (lrg_workshop_query_get_content_type (query), ==, LRG_WORKSHOP_CONTENT_ITEMS);

    lrg_workshop_query_set_content_type (query, LRG_WORKSHOP_CONTENT_COLLECTIONS);
    g_assert_cmpint (lrg_workshop_query_get_content_type (query), ==, LRG_WORKSHOP_CONTENT_COLLECTIONS);
}

static void
test_workshop_query_search_text (void)
{
    g_autoptr(LrgWorkshopQuery) query = NULL;

    query = lrg_workshop_query_new (LRG_WORKSHOP_QUERY_RANKED_BY_TEXT_SEARCH);
    g_assert_null (lrg_workshop_query_get_search_text (query));

    lrg_workshop_query_set_search_text (query, "sword");
    g_assert_cmpstr (lrg_workshop_query_get_search_text (query), ==, "sword");

    lrg_workshop_query_set_search_text (query, NULL);
    g_assert_null (lrg_workshop_query_get_search_text (query));
}

static void
test_workshop_query_tags (void)
{
    g_autoptr(LrgWorkshopQuery) query = NULL;
    GPtrArray *required;
    GPtrArray *excluded;

    query = lrg_workshop_query_new (LRG_WORKSHOP_QUERY_RANKED_BY_VOTE);

    lrg_workshop_query_add_required_tag (query, "weapons");
    lrg_workshop_query_add_required_tag (query, "magic");
    lrg_workshop_query_add_excluded_tag (query, "nsfw");

    required = lrg_workshop_query_get_required_tags (query);
    g_assert_cmpuint (required->len, ==, 2);
    g_assert_cmpstr (g_ptr_array_index (required, 0), ==, "weapons");
    g_assert_cmpstr (g_ptr_array_index (required, 1), ==, "magic");

    excluded = lrg_workshop_query_get_excluded_tags (query);
    g_assert_cmpuint (excluded->len, ==, 1);
    g_assert_cmpstr (g_ptr_array_index (excluded, 0), ==, "nsfw");

    lrg_workshop_query_clear_tags (query);
    g_assert_cmpuint (lrg_workshop_query_get_required_tags (query)->len, ==, 0);
    g_assert_cmpuint (lrg_workshop_query_get_excluded_tags (query)->len, ==, 0);
}

static void
test_workshop_query_pagination (void)
{
    g_autoptr(LrgWorkshopQuery) query = NULL;

    query = lrg_workshop_query_new (LRG_WORKSHOP_QUERY_RANKED_BY_VOTE);
    g_assert_cmpuint (lrg_workshop_query_get_page (query), ==, 1);

    lrg_workshop_query_set_page (query, 5);
    g_assert_cmpuint (lrg_workshop_query_get_page (query), ==, 5);
}

/* ==========================================================================
 * Workshop Manager Tests
 * ========================================================================== */

static void
test_workshop_manager_new (void)
{
    g_autoptr(LrgWorkshopManager) manager = NULL;

    manager = lrg_workshop_manager_new (480); /* Spacewar app ID for testing */
    g_assert_nonnull (manager);
    g_assert_cmpuint (lrg_workshop_manager_get_app_id (manager), ==, 480);
}

static void
test_workshop_manager_no_steam (void)
{
    /* Test behavior when Steam is not available (built without STEAM=1) */
    g_autoptr(LrgWorkshopManager) manager = NULL;
    g_autoptr(GError) error = NULL;
    GPtrArray *items;

    manager = lrg_workshop_manager_new (480);

    /* These should return empty/false/0 without crashing */
    items = lrg_workshop_manager_get_subscribed_items (manager);
    g_assert_nonnull (items);
    g_assert_cmpuint (items->len, ==, 0);
    g_ptr_array_unref (items);

    g_assert_cmpuint (lrg_workshop_manager_get_subscribed_count (manager), ==, 0);
    g_assert_false (lrg_workshop_manager_is_subscribed (manager, 12345));
    g_assert_cmpuint (lrg_workshop_manager_get_item_state (manager, 12345), ==, LRG_WORKSHOP_ITEM_STATE_NONE);
    g_assert_false (lrg_workshop_manager_is_updating (manager));

    /* Subscribe should fail gracefully without Steam */
#ifndef LRG_ENABLE_STEAM
    g_assert_false (lrg_workshop_manager_subscribe (manager, 12345, &error));
    g_assert_error (error, LRG_WORKSHOP_ERROR, LRG_WORKSHOP_ERROR_NOT_AVAILABLE);
    g_clear_error (&error);
#endif
}

static void
test_workshop_manager_query_without_steam (void)
{
    g_autoptr(LrgWorkshopManager) manager = NULL;
    g_autoptr(LrgWorkshopQuery) query = NULL;
    g_autoptr(GError) error = NULL;

    manager = lrg_workshop_manager_new (480);
    query = lrg_workshop_query_new (LRG_WORKSHOP_QUERY_RANKED_BY_VOTE);

#ifndef LRG_ENABLE_STEAM
    g_assert_false (lrg_workshop_manager_execute_query (manager, query, &error));
    g_assert_error (error, LRG_WORKSHOP_ERROR, LRG_WORKSHOP_ERROR_NOT_AVAILABLE);
#endif
}

static void
test_workshop_manager_create_without_steam (void)
{
    g_autoptr(LrgWorkshopManager) manager = NULL;
    g_autoptr(GError) error = NULL;

    manager = lrg_workshop_manager_new (480);

#ifndef LRG_ENABLE_STEAM
    g_assert_false (lrg_workshop_manager_create_item (manager, &error));
    g_assert_error (error, LRG_WORKSHOP_ERROR, LRG_WORKSHOP_ERROR_NOT_AVAILABLE);
#endif
}

static void
test_workshop_manager_install_info (void)
{
    g_autoptr(LrgWorkshopManager) manager = NULL;
    gchar *path = NULL;
    guint64 size = 0;
    guint32 timestamp = 0;

    manager = lrg_workshop_manager_new (480);

    /* Should return FALSE for non-installed items */
    g_assert_false (lrg_workshop_manager_get_install_info (manager, 12345,
                                                            &size, &path, &timestamp));
    g_assert_null (path);
}

/* ==========================================================================
 * Error Domain Tests
 * ========================================================================== */

static void
test_workshop_error_quark (void)
{
    GQuark quark;

    quark = LRG_WORKSHOP_ERROR;
    g_assert_cmpuint (quark, !=, 0);
    g_assert_cmpstr (g_quark_to_string (quark), ==, "lrg-workshop-error-quark");
}

static void
test_workshop_error_type (void)
{
    GType type;
    GEnumClass *enum_class;
    GEnumValue *value;

    type = LRG_TYPE_WORKSHOP_ERROR;
    g_assert_true (G_TYPE_IS_ENUM (type));

    enum_class = g_type_class_ref (type);
    g_assert_nonnull (enum_class);

    /* Verify all error codes have names */
    value = g_enum_get_value (enum_class, LRG_WORKSHOP_ERROR_FAILED);
    g_assert_nonnull (value);
    g_assert_cmpstr (value->value_nick, ==, "failed");

    value = g_enum_get_value (enum_class, LRG_WORKSHOP_ERROR_NOT_AVAILABLE);
    g_assert_nonnull (value);
    g_assert_cmpstr (value->value_nick, ==, "not-available");

    g_type_class_unref (enum_class);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Workshop Item tests */
    g_test_add_func ("/workshop/item/new", test_workshop_item_new);
    g_test_add_func ("/workshop/item/title", test_workshop_item_title);
    g_test_add_func ("/workshop/item/description", test_workshop_item_description);
    g_test_add_func ("/workshop/item/visibility", test_workshop_item_visibility);
    g_test_add_func ("/workshop/item/tags", test_workshop_item_tags);
    g_test_add_func ("/workshop/item/tags-set", test_workshop_item_tags_set);
    g_test_add_func ("/workshop/item/state", test_workshop_item_state);
    g_test_add_func ("/workshop/item/properties", test_workshop_item_properties);

    /* Workshop Query tests */
    g_test_add_func ("/workshop/query/new", test_workshop_query_new);
    g_test_add_func ("/workshop/query/new-for-user", test_workshop_query_new_for_user);
    g_test_add_func ("/workshop/query/content-type", test_workshop_query_content_type);
    g_test_add_func ("/workshop/query/search-text", test_workshop_query_search_text);
    g_test_add_func ("/workshop/query/tags", test_workshop_query_tags);
    g_test_add_func ("/workshop/query/pagination", test_workshop_query_pagination);

    /* Workshop Manager tests */
    g_test_add_func ("/workshop/manager/new", test_workshop_manager_new);
    g_test_add_func ("/workshop/manager/no-steam", test_workshop_manager_no_steam);
    g_test_add_func ("/workshop/manager/query-without-steam", test_workshop_manager_query_without_steam);
    g_test_add_func ("/workshop/manager/create-without-steam", test_workshop_manager_create_without_steam);
    g_test_add_func ("/workshop/manager/install-info", test_workshop_manager_install_info);

    /* Error domain tests */
    g_test_add_func ("/workshop/error/quark", test_workshop_error_quark);
    g_test_add_func ("/workshop/error/type", test_workshop_error_type);

    return g_test_run ();
}
