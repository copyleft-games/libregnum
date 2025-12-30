/* test-dlc.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the DLC module.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgModManifest *manifest;
} DlcFixture;

static void
dlc_fixture_set_up (DlcFixture    *fixture,
                    gconstpointer  user_data)
{
    (void)user_data;

    fixture->manifest = lrg_mod_manifest_new ("test-dlc");
}

static void
dlc_fixture_tear_down (DlcFixture    *fixture,
                       gconstpointer  user_data)
{
    (void)user_data;

    g_clear_object (&fixture->manifest);
}

/* ==========================================================================
 * Enum Tests
 * ========================================================================== */

static void
test_dlc_error_quark (void)
{
    GQuark quark;

    quark = LRG_DLC_ERROR;

    g_assert_cmpuint (quark, !=, 0);
    g_assert_cmpstr (g_quark_to_string (quark), ==, "lrg-dlc-error-quark");
}

static void
test_dlc_error_get_type (void)
{
    GType type;
    GEnumClass *enum_class;

    type = lrg_dlc_error_get_type ();

    g_assert_true (g_type_is_a (type, G_TYPE_ENUM));
    g_assert_cmpstr (g_type_name (type), ==, "LrgDlcError");

    /* Verify enum values */
    enum_class = g_type_class_ref (type);
    g_assert_nonnull (enum_class);

    g_assert_nonnull (g_enum_get_value (enum_class, LRG_DLC_ERROR_FAILED));
    g_assert_nonnull (g_enum_get_value (enum_class, LRG_DLC_ERROR_NOT_OWNED));
    g_assert_nonnull (g_enum_get_value (enum_class, LRG_DLC_ERROR_VERIFICATION_FAILED));
    g_assert_nonnull (g_enum_get_value (enum_class, LRG_DLC_ERROR_INVALID_LICENSE));
    g_assert_nonnull (g_enum_get_value (enum_class, LRG_DLC_ERROR_STEAM_UNAVAILABLE));
    g_assert_nonnull (g_enum_get_value (enum_class, LRG_DLC_ERROR_CONTENT_GATED));

    g_type_class_unref (enum_class);
}

static void
test_dlc_type_get_type (void)
{
    GType type;
    GEnumClass *enum_class;

    type = lrg_dlc_type_get_type ();

    g_assert_true (g_type_is_a (type, G_TYPE_ENUM));
    g_assert_cmpstr (g_type_name (type), ==, "LrgDlcType");

    /* Verify enum values */
    enum_class = g_type_class_ref (type);
    g_assert_nonnull (enum_class);

    g_assert_nonnull (g_enum_get_value (enum_class, LRG_DLC_TYPE_EXPANSION));
    g_assert_nonnull (g_enum_get_value (enum_class, LRG_DLC_TYPE_COSMETIC));
    g_assert_nonnull (g_enum_get_value (enum_class, LRG_DLC_TYPE_QUEST));
    g_assert_nonnull (g_enum_get_value (enum_class, LRG_DLC_TYPE_ITEM));
    g_assert_nonnull (g_enum_get_value (enum_class, LRG_DLC_TYPE_CHARACTER));
    g_assert_nonnull (g_enum_get_value (enum_class, LRG_DLC_TYPE_MAP));

    g_type_class_unref (enum_class);
}

static void
test_dlc_ownership_state_get_type (void)
{
    GType type;
    GEnumClass *enum_class;

    type = lrg_dlc_ownership_state_get_type ();

    g_assert_true (g_type_is_a (type, G_TYPE_ENUM));
    g_assert_cmpstr (g_type_name (type), ==, "LrgDlcOwnershipState");

    /* Verify enum values */
    enum_class = g_type_class_ref (type);
    g_assert_nonnull (enum_class);

    g_assert_nonnull (g_enum_get_value (enum_class, LRG_DLC_OWNERSHIP_UNKNOWN));
    g_assert_nonnull (g_enum_get_value (enum_class, LRG_DLC_OWNERSHIP_NOT_OWNED));
    g_assert_nonnull (g_enum_get_value (enum_class, LRG_DLC_OWNERSHIP_OWNED));
    g_assert_nonnull (g_enum_get_value (enum_class, LRG_DLC_OWNERSHIP_TRIAL));
    g_assert_nonnull (g_enum_get_value (enum_class, LRG_DLC_OWNERSHIP_ERROR));

    g_type_class_unref (enum_class);
}

/* ==========================================================================
 * Ownership Interface Tests
 * ========================================================================== */

static void
test_dlc_ownership_interface (void)
{
    GType type;

    type = LRG_TYPE_DLC_OWNERSHIP;

    g_assert_true (G_TYPE_IS_INTERFACE (type));
    g_assert_cmpstr (g_type_name (type), ==, "LrgDlcOwnership");
}

static void
test_dlc_ownership_manifest_new (void)
{
    g_autoptr(LrgDlcOwnershipManifest) checker = lrg_dlc_ownership_manifest_new ();

    g_assert_nonnull (checker);
    g_assert_true (LRG_IS_DLC_OWNERSHIP_MANIFEST (checker));
    g_assert_true (LRG_IS_DLC_OWNERSHIP (checker));
}

static void
test_dlc_ownership_manifest_check (void)
{
    g_autoptr(LrgDlcOwnershipManifest) checker = lrg_dlc_ownership_manifest_new ();
    g_autoptr(GError) error = NULL;
    gboolean owned;

    /* Register a DLC as owned */
    lrg_dlc_ownership_manifest_set_owned (checker, "dlc-1", TRUE);

    /* Check ownership */
    owned = lrg_dlc_ownership_check_ownership (LRG_DLC_OWNERSHIP (checker), "dlc-1", &error);
    g_assert_no_error (error);
    g_assert_true (owned);

    /* Check unregistered DLC (should return FALSE with error) */
    owned = lrg_dlc_ownership_check_ownership (LRG_DLC_OWNERSHIP (checker), "dlc-unknown", &error);
    g_assert_error (error, LRG_DLC_ERROR, LRG_DLC_ERROR_NOT_OWNED);
    g_assert_false (owned);
    g_clear_error (&error);
}

static void
test_dlc_ownership_manifest_backend_id (void)
{
    g_autoptr(LrgDlcOwnershipManifest) checker = lrg_dlc_ownership_manifest_new ();
    const gchar *backend_id;

    backend_id = lrg_dlc_ownership_get_backend_id (LRG_DLC_OWNERSHIP (checker));
    g_assert_cmpstr (backend_id, ==, "manifest");
}

static void
test_dlc_ownership_license_new (void)
{
    g_autoptr(LrgDlcOwnershipLicense) checker = lrg_dlc_ownership_license_new ("test-license.dat");

    g_assert_nonnull (checker);
    g_assert_true (LRG_IS_DLC_OWNERSHIP_LICENSE (checker));
    g_assert_true (LRG_IS_DLC_OWNERSHIP (checker));
}

static void
test_dlc_ownership_license_backend_id (void)
{
    g_autoptr(LrgDlcOwnershipLicense) checker = lrg_dlc_ownership_license_new ("test-license.dat");
    const gchar *backend_id;

    backend_id = lrg_dlc_ownership_get_backend_id (LRG_DLC_OWNERSHIP (checker));
    g_assert_cmpstr (backend_id, ==, "license");
}

static void
test_dlc_ownership_steam_new (void)
{
    g_autoptr(LrgDlcOwnershipSteam) checker = lrg_dlc_ownership_steam_new ();

    g_assert_nonnull (checker);
    g_assert_true (LRG_IS_DLC_OWNERSHIP_STEAM (checker));
    g_assert_true (LRG_IS_DLC_OWNERSHIP (checker));
}

static void
test_dlc_ownership_steam_backend_id (void)
{
    g_autoptr(LrgDlcOwnershipSteam) checker = lrg_dlc_ownership_steam_new ();
    const gchar *backend_id;

    backend_id = lrg_dlc_ownership_get_backend_id (LRG_DLC_OWNERSHIP (checker));
    g_assert_cmpstr (backend_id, ==, "steam");
}

static void
test_dlc_ownership_steam_register (void)
{
    g_autoptr(LrgDlcOwnershipSteam) checker = lrg_dlc_ownership_steam_new ();

    /* Register a DLC with Steam App ID */
    lrg_dlc_ownership_steam_register_dlc (checker, "expansion-1", 123456);

    /* Unregister it */
    lrg_dlc_ownership_steam_unregister_dlc (checker, "expansion-1");

    /* This should not crash */
}

/* ==========================================================================
 * DLC Subclass Tests
 * ========================================================================== */

static void
test_expansion_pack_new (void)
{
    g_autoptr(LrgModManifest) manifest = lrg_mod_manifest_new ("expansion-test");
    g_autoptr(LrgExpansionPack) pack = lrg_expansion_pack_new (manifest, "/test/path");

    g_assert_nonnull (pack);
    g_assert_true (LRG_IS_EXPANSION_PACK (pack));
    g_assert_true (LRG_IS_DLC (pack));
    g_assert_true (LRG_IS_MOD (pack));
}

static void
test_expansion_pack_properties (void)
{
    g_autoptr(LrgModManifest) manifest = lrg_mod_manifest_new ("expansion-test");
    g_autoptr(LrgExpansionPack) pack = lrg_expansion_pack_new (manifest, "/test/path");
    GPtrArray *areas;

    /* Set properties */
    lrg_expansion_pack_set_campaign_name (pack, "The Dark Campaign");
    lrg_expansion_pack_set_level_cap_increase (pack, 10);
    lrg_expansion_pack_add_new_area (pack, "area-1");
    lrg_expansion_pack_add_new_area (pack, "area-2");

    /* Verify properties */
    g_assert_cmpstr (lrg_expansion_pack_get_campaign_name (pack), ==, "The Dark Campaign");
    g_assert_cmpint (lrg_expansion_pack_get_level_cap_increase (pack), ==, 10);

    areas = lrg_expansion_pack_get_new_areas (pack);
    g_assert_cmpuint (areas->len, ==, 2);
}

static void
test_cosmetic_pack_new (void)
{
    g_autoptr(LrgModManifest) manifest = lrg_mod_manifest_new ("cosmetic-test");
    g_autoptr(LrgCosmeticPack) pack = lrg_cosmetic_pack_new (manifest, "/test/path");

    g_assert_nonnull (pack);
    g_assert_true (LRG_IS_COSMETIC_PACK (pack));
    g_assert_true (LRG_IS_DLC (pack));
}

static void
test_cosmetic_pack_items (void)
{
    g_autoptr(LrgModManifest) manifest = lrg_mod_manifest_new ("cosmetic-test");
    g_autoptr(LrgCosmeticPack) pack = lrg_cosmetic_pack_new (manifest, "/test/path");
    GPtrArray *skins;
    GPtrArray *effects;

    /* Add skin and effect IDs */
    lrg_cosmetic_pack_add_skin_id (pack, "skin-dragon");
    lrg_cosmetic_pack_add_skin_id (pack, "skin-phoenix");
    lrg_cosmetic_pack_add_effect_id (pack, "effect-fire");

    /* Verify */
    skins = lrg_cosmetic_pack_get_skin_ids (pack);
    g_assert_cmpuint (skins->len, ==, 2);

    effects = lrg_cosmetic_pack_get_effect_ids (pack);
    g_assert_cmpuint (effects->len, ==, 1);
}

static void
test_quest_pack_new (void)
{
    g_autoptr(LrgModManifest) manifest = lrg_mod_manifest_new ("quest-test");
    g_autoptr(LrgQuestPack) pack = lrg_quest_pack_new (manifest, "/test/path");

    g_assert_nonnull (pack);
    g_assert_true (LRG_IS_QUEST_PACK (pack));
    g_assert_true (LRG_IS_DLC (pack));
}

static void
test_quest_pack_quests (void)
{
    g_autoptr(LrgModManifest) manifest = lrg_mod_manifest_new ("quest-test");
    g_autoptr(LrgQuestPack) pack = lrg_quest_pack_new (manifest, "/test/path");
    GPtrArray *quests;

    /* Set properties */
    lrg_quest_pack_add_quest_id (pack, "quest-dragon");
    lrg_quest_pack_add_quest_id (pack, "quest-treasure");
    lrg_quest_pack_set_estimated_hours (pack, 5);

    /* Verify */
    quests = lrg_quest_pack_get_quest_ids (pack);
    g_assert_cmpuint (quests->len, ==, 2);
    g_assert_cmpuint (lrg_quest_pack_get_estimated_hours (pack), ==, 5);
}

static void
test_item_pack_new (void)
{
    g_autoptr(LrgModManifest) manifest = lrg_mod_manifest_new ("item-test");
    g_autoptr(LrgItemPack) pack = lrg_item_pack_new (manifest, "/test/path");

    g_assert_nonnull (pack);
    g_assert_true (LRG_IS_ITEM_PACK (pack));
    g_assert_true (LRG_IS_DLC (pack));
}

static void
test_item_pack_items (void)
{
    g_autoptr(LrgModManifest) manifest = lrg_mod_manifest_new ("item-test");
    g_autoptr(LrgItemPack) pack = lrg_item_pack_new (manifest, "/test/path");
    GPtrArray *items;
    GPtrArray *slots;

    /* Set properties */
    lrg_item_pack_add_item_id (pack, "sword-legendary");
    lrg_item_pack_add_item_id (pack, "armor-legendary");
    lrg_item_pack_add_equipment_slot (pack, "weapon");
    lrg_item_pack_add_equipment_slot (pack, "chest");

    /* Verify */
    items = lrg_item_pack_get_item_ids (pack);
    g_assert_cmpuint (items->len, ==, 2);

    slots = lrg_item_pack_get_equipment_slots (pack);
    g_assert_cmpuint (slots->len, ==, 2);
}

static void
test_character_pack_new (void)
{
    g_autoptr(LrgModManifest) manifest = lrg_mod_manifest_new ("character-test");
    g_autoptr(LrgCharacterPack) pack = lrg_character_pack_new (manifest, "/test/path");

    g_assert_nonnull (pack);
    g_assert_true (LRG_IS_CHARACTER_PACK (pack));
    g_assert_true (LRG_IS_DLC (pack));
}

static void
test_character_pack_characters (void)
{
    g_autoptr(LrgModManifest) manifest = lrg_mod_manifest_new ("character-test");
    g_autoptr(LrgCharacterPack) pack = lrg_character_pack_new (manifest, "/test/path");
    GPtrArray *chars;

    /* Set properties */
    lrg_character_pack_add_character_id (pack, "hero-knight");
    lrg_character_pack_set_is_playable (pack, TRUE);
    lrg_character_pack_set_is_companion (pack, FALSE);

    /* Verify */
    chars = lrg_character_pack_get_character_ids (pack);
    g_assert_cmpuint (chars->len, ==, 1);
    g_assert_true (lrg_character_pack_get_is_playable (pack));
    g_assert_false (lrg_character_pack_get_is_companion (pack));
}

static void
test_map_pack_new (void)
{
    g_autoptr(LrgModManifest) manifest = lrg_mod_manifest_new ("map-test");
    g_autoptr(LrgMapPack) pack = lrg_map_pack_new (manifest, "/test/path");

    g_assert_nonnull (pack);
    g_assert_true (LRG_IS_MAP_PACK (pack));
    g_assert_true (LRG_IS_DLC (pack));
}

static void
test_map_pack_maps (void)
{
    g_autoptr(LrgModManifest) manifest = lrg_mod_manifest_new ("map-test");
    g_autoptr(LrgMapPack) pack = lrg_map_pack_new (manifest, "/test/path");
    GPtrArray *maps;

    /* Set properties */
    lrg_map_pack_add_map_id (pack, "map-desert");
    lrg_map_pack_add_map_id (pack, "map-oasis");
    lrg_map_pack_set_biome_type (pack, "desert");

    /* Verify */
    maps = lrg_map_pack_get_map_ids (pack);
    g_assert_cmpuint (maps->len, ==, 2);
    g_assert_cmpstr (lrg_map_pack_get_biome_type (pack), ==, "desert");
}

/* ==========================================================================
 * Manifest DLC Tests
 * ========================================================================== */

static void
test_manifest_dlc_defaults (DlcFixture    *fixture,
                             gconstpointer  user_data)
{
    (void)user_data;

    /* By default, a manifest is not a DLC */
    g_assert_false (lrg_mod_manifest_is_dlc (fixture->manifest));
}

static void
test_manifest_set_dlc (DlcFixture    *fixture,
                        gconstpointer  user_data)
{
    (void)user_data;

    lrg_mod_manifest_set_is_dlc (fixture->manifest, TRUE);
    g_assert_true (lrg_mod_manifest_is_dlc (fixture->manifest));

    lrg_mod_manifest_set_is_dlc (fixture->manifest, FALSE);
    g_assert_false (lrg_mod_manifest_is_dlc (fixture->manifest));
}

static void
test_manifest_dlc_type (DlcFixture    *fixture,
                         gconstpointer  user_data)
{
    (void)user_data;

    lrg_mod_manifest_set_is_dlc (fixture->manifest, TRUE);

    /* Test each DLC type */
    lrg_mod_manifest_set_dlc_type (fixture->manifest, LRG_DLC_TYPE_EXPANSION);
    g_assert_cmpint (lrg_mod_manifest_get_dlc_type (fixture->manifest), ==, LRG_DLC_TYPE_EXPANSION);

    lrg_mod_manifest_set_dlc_type (fixture->manifest, LRG_DLC_TYPE_COSMETIC);
    g_assert_cmpint (lrg_mod_manifest_get_dlc_type (fixture->manifest), ==, LRG_DLC_TYPE_COSMETIC);

    lrg_mod_manifest_set_dlc_type (fixture->manifest, LRG_DLC_TYPE_QUEST);
    g_assert_cmpint (lrg_mod_manifest_get_dlc_type (fixture->manifest), ==, LRG_DLC_TYPE_QUEST);

    lrg_mod_manifest_set_dlc_type (fixture->manifest, LRG_DLC_TYPE_ITEM);
    g_assert_cmpint (lrg_mod_manifest_get_dlc_type (fixture->manifest), ==, LRG_DLC_TYPE_ITEM);

    lrg_mod_manifest_set_dlc_type (fixture->manifest, LRG_DLC_TYPE_CHARACTER);
    g_assert_cmpint (lrg_mod_manifest_get_dlc_type (fixture->manifest), ==, LRG_DLC_TYPE_CHARACTER);

    lrg_mod_manifest_set_dlc_type (fixture->manifest, LRG_DLC_TYPE_MAP);
    g_assert_cmpint (lrg_mod_manifest_get_dlc_type (fixture->manifest), ==, LRG_DLC_TYPE_MAP);
}

static void
test_manifest_steam_app_id (DlcFixture    *fixture,
                             gconstpointer  user_data)
{
    (void)user_data;

    g_assert_cmpuint (lrg_mod_manifest_get_steam_app_id (fixture->manifest), ==, 0);

    lrg_mod_manifest_set_steam_app_id (fixture->manifest, 123456);
    g_assert_cmpuint (lrg_mod_manifest_get_steam_app_id (fixture->manifest), ==, 123456);
}

static void
test_manifest_store_id (DlcFixture    *fixture,
                         gconstpointer  user_data)
{
    (void)user_data;

    g_assert_null (lrg_mod_manifest_get_store_id (fixture->manifest));

    lrg_mod_manifest_set_store_id (fixture->manifest, "com.example.dlc");
    g_assert_cmpstr (lrg_mod_manifest_get_store_id (fixture->manifest), ==, "com.example.dlc");
}

static void
test_manifest_price_string (DlcFixture    *fixture,
                             gconstpointer  user_data)
{
    (void)user_data;

    g_assert_null (lrg_mod_manifest_get_price_string (fixture->manifest));

    lrg_mod_manifest_set_price_string (fixture->manifest, "$9.99");
    g_assert_cmpstr (lrg_mod_manifest_get_price_string (fixture->manifest), ==, "$9.99");
}

static void
test_manifest_min_game_version (DlcFixture    *fixture,
                                 gconstpointer  user_data)
{
    (void)user_data;

    g_assert_null (lrg_mod_manifest_get_min_game_version (fixture->manifest));

    lrg_mod_manifest_set_min_game_version (fixture->manifest, "1.2.0");
    g_assert_cmpstr (lrg_mod_manifest_get_min_game_version (fixture->manifest), ==, "1.2.0");
}

static void
test_manifest_ownership_method (DlcFixture    *fixture,
                                 gconstpointer  user_data)
{
    (void)user_data;

    g_assert_null (lrg_mod_manifest_get_ownership_method (fixture->manifest));

    lrg_mod_manifest_set_ownership_method (fixture->manifest, "steam");
    g_assert_cmpstr (lrg_mod_manifest_get_ownership_method (fixture->manifest), ==, "steam");
}

static void
test_manifest_trial (DlcFixture    *fixture,
                      gconstpointer  user_data)
{
    GPtrArray *ids;

    (void)user_data;

    g_assert_false (lrg_mod_manifest_get_trial_enabled (fixture->manifest));

    lrg_mod_manifest_set_trial_enabled (fixture->manifest, TRUE);
    g_assert_true (lrg_mod_manifest_get_trial_enabled (fixture->manifest));

    /* Trial content IDs */
    ids = lrg_mod_manifest_get_trial_content_ids (fixture->manifest);
    g_assert_nonnull (ids);
    g_assert_cmpuint (ids->len, ==, 0);

    lrg_mod_manifest_add_trial_content_id (fixture->manifest, "level-1");
    lrg_mod_manifest_add_trial_content_id (fixture->manifest, "level-2");

    ids = lrg_mod_manifest_get_trial_content_ids (fixture->manifest);
    g_assert_cmpuint (ids->len, ==, 2);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Enum tests */
    g_test_add_func ("/dlc/enum/error-quark", test_dlc_error_quark);
    g_test_add_func ("/dlc/enum/error-type", test_dlc_error_get_type);
    g_test_add_func ("/dlc/enum/dlc-type", test_dlc_type_get_type);
    g_test_add_func ("/dlc/enum/ownership-state", test_dlc_ownership_state_get_type);

    /* Ownership interface tests */
    g_test_add_func ("/dlc/ownership/interface", test_dlc_ownership_interface);
    g_test_add_func ("/dlc/ownership/manifest/new", test_dlc_ownership_manifest_new);
    g_test_add_func ("/dlc/ownership/manifest/check", test_dlc_ownership_manifest_check);
    g_test_add_func ("/dlc/ownership/manifest/backend-id", test_dlc_ownership_manifest_backend_id);
    g_test_add_func ("/dlc/ownership/license/new", test_dlc_ownership_license_new);
    g_test_add_func ("/dlc/ownership/license/backend-id", test_dlc_ownership_license_backend_id);
    g_test_add_func ("/dlc/ownership/steam/new", test_dlc_ownership_steam_new);
    g_test_add_func ("/dlc/ownership/steam/backend-id", test_dlc_ownership_steam_backend_id);
    g_test_add_func ("/dlc/ownership/steam/register", test_dlc_ownership_steam_register);

    /* DLC subclass tests */
    g_test_add_func ("/dlc/expansion/new", test_expansion_pack_new);
    g_test_add_func ("/dlc/expansion/properties", test_expansion_pack_properties);
    g_test_add_func ("/dlc/cosmetic/new", test_cosmetic_pack_new);
    g_test_add_func ("/dlc/cosmetic/items", test_cosmetic_pack_items);
    g_test_add_func ("/dlc/quest/new", test_quest_pack_new);
    g_test_add_func ("/dlc/quest/quests", test_quest_pack_quests);
    g_test_add_func ("/dlc/item/new", test_item_pack_new);
    g_test_add_func ("/dlc/item/items", test_item_pack_items);
    g_test_add_func ("/dlc/character/new", test_character_pack_new);
    g_test_add_func ("/dlc/character/characters", test_character_pack_characters);
    g_test_add_func ("/dlc/map/new", test_map_pack_new);
    g_test_add_func ("/dlc/map/maps", test_map_pack_maps);

    /* Manifest DLC tests */
    g_test_add ("/dlc/manifest/defaults", DlcFixture, NULL,
                dlc_fixture_set_up, test_manifest_dlc_defaults, dlc_fixture_tear_down);
    g_test_add ("/dlc/manifest/set-dlc", DlcFixture, NULL,
                dlc_fixture_set_up, test_manifest_set_dlc, dlc_fixture_tear_down);
    g_test_add ("/dlc/manifest/dlc-type", DlcFixture, NULL,
                dlc_fixture_set_up, test_manifest_dlc_type, dlc_fixture_tear_down);
    g_test_add ("/dlc/manifest/steam-app-id", DlcFixture, NULL,
                dlc_fixture_set_up, test_manifest_steam_app_id, dlc_fixture_tear_down);
    g_test_add ("/dlc/manifest/store-id", DlcFixture, NULL,
                dlc_fixture_set_up, test_manifest_store_id, dlc_fixture_tear_down);
    g_test_add ("/dlc/manifest/price-string", DlcFixture, NULL,
                dlc_fixture_set_up, test_manifest_price_string, dlc_fixture_tear_down);
    g_test_add ("/dlc/manifest/min-game-version", DlcFixture, NULL,
                dlc_fixture_set_up, test_manifest_min_game_version, dlc_fixture_tear_down);
    g_test_add ("/dlc/manifest/ownership-method", DlcFixture, NULL,
                dlc_fixture_set_up, test_manifest_ownership_method, dlc_fixture_tear_down);
    g_test_add ("/dlc/manifest/trial", DlcFixture, NULL,
                dlc_fixture_set_up, test_manifest_trial, dlc_fixture_tear_down);

    return g_test_run ();
}
