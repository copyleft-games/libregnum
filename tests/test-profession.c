/* test-profession.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the profession module: skill bands, profession/recipe/
 * gather node definitions and LrgProfessionState (learning, training,
 * skill-ups, crafting/gathering checks and GVariant persistence).
 */

#include <glib.h>
#include <glib-object.h>
#include <math.h>
#include <string.h>

#include "lrg-enums.h"
#include "profession/lrg-skill-band.h"
#include "profession/lrg-profession-def.h"
#include "profession/lrg-recipe-def.h"
#include "profession/lrg-gather-node-def.h"
#include "profession/lrg-profession-state.h"
#include "inventory/lrg-item-def.h"
#include "inventory/lrg-item-stack.h"
#include "inventory/lrg-inventory.h"

/* ========================================================================== */
/*                                  Helpers                                   */
/* ========================================================================== */

/*
 * make_profession:
 * Builds a four-tier profession: Apprentice 75 (lvl 5), Journeyman 150
 * (lvl 10), Expert 225 (lvl 20), Artisan 300 (lvl 35).
 */
static LrgProfessionDef *
make_profession (const gchar       *id,
                 LrgProfessionKind  kind)
{
    LrgProfessionDef *def;

    def = lrg_profession_def_new (id, kind, LRG_PROFESSION_CATEGORY_CRAFTING);
    g_assert_true (lrg_profession_def_add_tier (def, lrg_profession_tier_new ("Apprentice", 75, 5, 10), NULL));
    g_assert_true (lrg_profession_def_add_tier (def, lrg_profession_tier_new ("Journeyman", 150, 10, 500), NULL));
    g_assert_true (lrg_profession_def_add_tier (def, lrg_profession_tier_new ("Expert", 225, 20, 5000), NULL));
    g_assert_true (lrg_profession_def_add_tier (def, lrg_profession_tier_new ("Artisan", 300, 35, 50000), NULL));
    return def;
}

/* Single-tier profession without a level requirement */
static LrgProfessionDef *
make_simple_profession (const gchar       *id,
                        LrgProfessionKind  kind)
{
    LrgProfessionDef *def;

    def = lrg_profession_def_new (id, kind, LRG_PROFESSION_CATEGORY_SERVICE);
    g_assert_true (lrg_profession_def_add_tier (def, lrg_profession_tier_new (NULL, 75, 0, 0), NULL));
    return def;
}

/* Serialises a state so tests can assert "unchanged" after a rejection */
static GVariant *
snapshot (LrgProfessionState *state)
{
    GVariant *v = lrg_profession_state_to_variant (state);

    g_assert_nonnull (v);
    g_assert_false (g_variant_is_floating (v));
    return v;
}

static void
assert_unchanged (LrgProfessionState *state,
                  GVariant           *before)
{
    g_autoptr(GVariant) after = snapshot (state);

    g_assert_true (g_variant_equal (before, after));
}

/* Counts entries in a GHashTable<utf8, GUINT_TO_POINTER(count)> */
static guint
table_count (const gchar *item_id,
             gpointer     user_data)
{
    return GPOINTER_TO_UINT (g_hash_table_lookup ((GHashTable *)user_data, item_id));
}

static GHashTable *
new_item_table (void)
{
    return g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void
put_items (GHashTable  *table,
           const gchar *item_id,
           guint        count)
{
    g_hash_table_replace (table, g_strdup (item_id), GUINT_TO_POINTER (count));
}

/* Restores @variant expecting failure with @code */
static void
assert_restore_fails (GVariant   *variant,
                      GHashTable *professions,
                      GHashTable *recipes,
                      gint        code)
{
    g_autoptr(GError) error = NULL;
    LrgProfessionState *state;

    g_variant_ref_sink (variant);
    state = lrg_profession_state_new_from_variant_full (variant, professions, recipes, &error);
    g_assert_null (state);
    g_assert_error (error, LRG_PROGRESSION_ERROR, code);
    g_test_message ("rejected: %s", error->message);
    g_variant_unref (variant);
}

/* Counts notify emissions on an object */
static void
count_notify (GObject    *object,
              GParamSpec *pspec,
              gpointer    user_data)
{
    (*(guint *)user_data)++;
}

/* ========================================================================== */
/*                                LrgSkillBand                                */
/* ========================================================================== */

static void
test_band_boxed (void)
{
    g_autoptr(LrgSkillBand) band = lrg_skill_band_new (1, 2, 3, 4);
    LrgSkillBand *copy;

    g_assert_cmpuint (band->orange, ==, 1);
    g_assert_cmpuint (band->yellow, ==, 2);
    g_assert_cmpuint (band->green, ==, 3);
    g_assert_cmpuint (band->grey, ==, 4);

    copy = g_boxed_copy (LRG_TYPE_SKILL_BAND, band);
    g_assert_true (copy != band);
    g_assert_cmpmem (copy, sizeof *copy, band, sizeof *band);
    g_boxed_free (LRG_TYPE_SKILL_BAND, copy);

    lrg_skill_band_free (NULL);
}

static void
test_band_validity (void)
{
    LrgSkillBand band;

    band.orange = 1; band.yellow = 2; band.green = 3; band.grey = 4;
    g_assert_true (lrg_skill_band_is_valid (&band));

    band.orange = 5; band.yellow = 5; band.green = 5; band.grey = 5;
    g_assert_true (lrg_skill_band_is_valid (&band));

    band.orange = 0; band.yellow = 0; band.green = 0; band.grey = 0;
    g_assert_true (lrg_skill_band_is_valid (&band));

    band.orange = 3; band.yellow = 2; band.green = 3; band.grey = 4;
    g_assert_false (lrg_skill_band_is_valid (&band));

    band.orange = 1; band.yellow = 4; band.green = 3; band.grey = 5;
    g_assert_false (lrg_skill_band_is_valid (&band));

    band.orange = 1; band.yellow = 2; band.green = 5; band.grey = 4;
    g_assert_false (lrg_skill_band_is_valid (&band));

    band.orange = 1; band.yellow = 2; band.green = 3; band.grey = LRG_PROFESSION_SKILL_LIMIT;
    g_assert_true (lrg_skill_band_is_valid (&band));

    band.grey = LRG_PROFESSION_SKILL_LIMIT + 1;
    g_assert_false (lrg_skill_band_is_valid (&band));
}

static void
test_band_difficulty_thresholds (void)
{
    g_autoptr(LrgSkillBand) band = lrg_skill_band_new (100, 125, 150, 175);

    g_assert_cmpint (lrg_skill_band_get_difficulty (band, 0), ==, LRG_SKILL_DIFFICULTY_UNAVAILABLE);
    g_assert_cmpint (lrg_skill_band_get_difficulty (band, 99), ==, LRG_SKILL_DIFFICULTY_UNAVAILABLE);
    g_assert_cmpint (lrg_skill_band_get_difficulty (band, 100), ==, LRG_SKILL_DIFFICULTY_ORANGE);
    g_assert_cmpint (lrg_skill_band_get_difficulty (band, 124), ==, LRG_SKILL_DIFFICULTY_ORANGE);
    g_assert_cmpint (lrg_skill_band_get_difficulty (band, 125), ==, LRG_SKILL_DIFFICULTY_YELLOW);
    g_assert_cmpint (lrg_skill_band_get_difficulty (band, 149), ==, LRG_SKILL_DIFFICULTY_YELLOW);
    g_assert_cmpint (lrg_skill_band_get_difficulty (band, 150), ==, LRG_SKILL_DIFFICULTY_GREEN);
    g_assert_cmpint (lrg_skill_band_get_difficulty (band, 174), ==, LRG_SKILL_DIFFICULTY_GREEN);
    g_assert_cmpint (lrg_skill_band_get_difficulty (band, 175), ==, LRG_SKILL_DIFFICULTY_GREY);
    g_assert_cmpint (lrg_skill_band_get_difficulty (band, G_MAXUINT), ==, LRG_SKILL_DIFFICULTY_GREY);
}

static void
test_band_chance_exact (void)
{
    g_autoptr(LrgSkillBand) band = lrg_skill_band_new (100, 125, 150, 175);

    /* UNAVAILABLE */
    g_assert_cmpfloat (lrg_skill_band_get_chance (band, 0), ==, 0.0);
    g_assert_cmpfloat (lrg_skill_band_get_chance (band, 99), ==, 0.0);
    /* ORANGE */
    g_assert_cmpfloat (lrg_skill_band_get_chance (band, 100), ==, 1.0);
    g_assert_cmpfloat (lrg_skill_band_get_chance (band, 124), ==, 1.0);
    /* YELLOW: (175 - skill) / 50 */
    g_assert_cmpfloat (lrg_skill_band_get_chance (band, 125), ==, 1.0);
    g_assert_cmpfloat (lrg_skill_band_get_chance (band, 149), ==, 26.0 / 50.0);
    /* GREEN */
    g_assert_cmpfloat (lrg_skill_band_get_chance (band, 150), ==, 0.5);
    g_assert_cmpfloat (lrg_skill_band_get_chance (band, 174), ==, 1.0 / 50.0);
    /* GREY */
    g_assert_cmpfloat (lrg_skill_band_get_chance (band, 175), ==, 0.0);
    g_assert_cmpfloat (lrg_skill_band_get_chance (band, G_MAXUINT), ==, 0.0);
}

static void
test_band_collapsed (void)
{
    g_autoptr(LrgSkillBand) all_equal = lrg_skill_band_new (10, 10, 10, 10);
    g_autoptr(LrgSkillBand) no_green = lrg_skill_band_new (10, 10, 20, 20);
    g_autoptr(LrgSkillBand) no_yellow = lrg_skill_band_new (10, 20, 20, 30);

    /* Everything collapses: unavailable then straight to grey */
    g_assert_cmpint (lrg_skill_band_get_difficulty (all_equal, 9), ==, LRG_SKILL_DIFFICULTY_UNAVAILABLE);
    g_assert_cmpint (lrg_skill_band_get_difficulty (all_equal, 10), ==, LRG_SKILL_DIFFICULTY_GREY);
    g_assert_cmpfloat (lrg_skill_band_get_chance (all_equal, 10), ==, 0.0);

    /* No orange and no green */
    g_assert_cmpint (lrg_skill_band_get_difficulty (no_green, 10), ==, LRG_SKILL_DIFFICULTY_YELLOW);
    g_assert_cmpfloat (lrg_skill_band_get_chance (no_green, 10), ==, 1.0);
    g_assert_cmpfloat (lrg_skill_band_get_chance (no_green, 19), ==, 0.1);
    g_assert_cmpint (lrg_skill_band_get_difficulty (no_green, 20), ==, LRG_SKILL_DIFFICULTY_GREY);

    /* No yellow: orange straight to green */
    g_assert_cmpint (lrg_skill_band_get_difficulty (no_yellow, 19), ==, LRG_SKILL_DIFFICULTY_ORANGE);
    g_assert_cmpint (lrg_skill_band_get_difficulty (no_yellow, 20), ==, LRG_SKILL_DIFFICULTY_GREEN);
    g_assert_cmpfloat (lrg_skill_band_get_chance (no_yellow, 20), ==, 1.0);
    g_assert_cmpfloat (lrg_skill_band_get_chance (no_yellow, 29), ==, 0.1);
    g_assert_cmpint (lrg_skill_band_get_difficulty (no_yellow, 30), ==, LRG_SKILL_DIFFICULTY_GREY);
}

static void
test_band_invalid (void)
{
    g_autoptr(LrgSkillBand) band = lrg_skill_band_new (50, 40, 60, 70);
    g_autoptr(LrgSkillBand) huge = lrg_skill_band_new (1, 2, 3, LRG_PROFESSION_SKILL_LIMIT + 1);
    guint skill;

    for (skill = 0; skill <= 80; skill++)
    {
        g_assert_cmpint (lrg_skill_band_get_difficulty (band, skill), ==, LRG_SKILL_DIFFICULTY_UNAVAILABLE);
        g_assert_cmpfloat (lrg_skill_band_get_chance (band, skill), ==, 0.0);
        g_assert_cmpfloat (lrg_skill_band_get_chance (huge, skill), ==, 0.0);
    }
}

static void
test_band_default (void)
{
    g_autoptr(LrgSkillBand) normal = lrg_skill_band_new_default (100);
    g_autoptr(LrgSkillBand) zero = lrg_skill_band_new_default (0);
    g_autoptr(LrgSkillBand) near_top = lrg_skill_band_new_default (LRG_PROFESSION_SKILL_LIMIT - 20);
    g_autoptr(LrgSkillBand) beyond = lrg_skill_band_new_default (G_MAXUINT);

    g_assert_cmpuint (normal->orange, ==, 100);
    g_assert_cmpuint (normal->yellow, ==, 115);
    g_assert_cmpuint (normal->green, ==, 130);
    g_assert_cmpuint (normal->grey, ==, 145);

    g_assert_cmpuint (zero->orange, ==, 0);
    g_assert_cmpuint (zero->grey, ==, 45);

    g_assert_cmpuint (near_top->orange, ==, LRG_PROFESSION_SKILL_LIMIT - 20);
    g_assert_cmpuint (near_top->yellow, ==, LRG_PROFESSION_SKILL_LIMIT - 5);
    g_assert_cmpuint (near_top->green, ==, LRG_PROFESSION_SKILL_LIMIT);
    g_assert_cmpuint (near_top->grey, ==, LRG_PROFESSION_SKILL_LIMIT);
    g_assert_true (lrg_skill_band_is_valid (near_top));

    g_assert_cmpuint (beyond->orange, ==, LRG_PROFESSION_SKILL_LIMIT);
    g_assert_cmpuint (beyond->grey, ==, LRG_PROFESSION_SKILL_LIMIT);
    g_assert_true (lrg_skill_band_is_valid (beyond));
}

/* ========================================================================== */
/*                        LrgProfessionTier / Def                             */
/* ========================================================================== */

static void
test_tier_boxed (void)
{
    g_autoptr(LrgProfessionTier) tier = lrg_profession_tier_new ("Expert", 225, 20, 5000);
    g_autoptr(LrgProfessionTier) anon = lrg_profession_tier_new (NULL, 1, 0, 0);
    LrgProfessionTier *copy;

    copy = g_boxed_copy (LRG_TYPE_PROFESSION_TIER, tier);
    g_assert_cmpstr (copy->name, ==, "Expert");
    g_assert_true (copy->name != tier->name);
    g_assert_cmpuint (copy->skill_cap, ==, 225);
    g_assert_cmpuint (copy->required_level, ==, 20);
    g_assert_cmpuint (copy->cost, ==, 5000);
    g_boxed_free (LRG_TYPE_PROFESSION_TIER, copy);

    copy = lrg_profession_tier_copy (anon);
    g_assert_null (copy->name);
    lrg_profession_tier_free (copy);
    lrg_profession_tier_free (NULL);
}

static void
test_def_properties (void)
{
    g_autoptr(LrgProfessionDef) def = NULL;
    g_autofree gchar *id = NULL;
    g_autofree gchar *name = NULL;
    g_autofree gchar *description = NULL;
    g_autofree gchar *icon = NULL;
    LrgProfessionKind kind;
    LrgProfessionCategory category;
    guint notifications = 0;

    def = lrg_profession_def_new ("mining", LRG_PROFESSION_KIND_PRIMARY,
                                  LRG_PROFESSION_CATEGORY_GATHERING);
    g_signal_connect (def, "notify", G_CALLBACK (count_notify), &notifications);

    g_object_set (def, "name", "Mining", "description", "Dig ore",
                  "icon", "pick", "kind", LRG_PROFESSION_KIND_SECONDARY,
                  "category", LRG_PROFESSION_CATEGORY_SERVICE, NULL);
    g_assert_cmpuint (notifications, ==, 5);

    g_object_get (def, "id", &id, "name", &name, "description", &description,
                  "icon", &icon, "kind", &kind, "category", &category, NULL);
    g_assert_cmpstr (id, ==, "mining");
    g_assert_cmpstr (name, ==, "Mining");
    g_assert_cmpstr (description, ==, "Dig ore");
    g_assert_cmpstr (icon, ==, "pick");
    g_assert_cmpint (kind, ==, LRG_PROFESSION_KIND_SECONDARY);
    g_assert_cmpint (category, ==, LRG_PROFESSION_CATEGORY_SERVICE);

    /* Same values: no notification */
    lrg_profession_def_set_name (def, "Mining");
    lrg_profession_def_set_description (def, "Dig ore");
    lrg_profession_def_set_icon (def, "pick");
    lrg_profession_def_set_kind (def, LRG_PROFESSION_KIND_SECONDARY);
    lrg_profession_def_set_category (def, LRG_PROFESSION_CATEGORY_SERVICE);
    g_assert_cmpuint (notifications, ==, 5);

    /* Accessors */
    lrg_profession_def_set_name (def, NULL);
    g_assert_null (lrg_profession_def_get_name (def));
    g_assert_cmpuint (notifications, ==, 6);
    g_assert_cmpstr (lrg_profession_def_get_id (def), ==, "mining");
    g_assert_cmpstr (lrg_profession_def_get_description (def), ==, "Dig ore");
    g_assert_cmpstr (lrg_profession_def_get_icon (def), ==, "pick");
    g_assert_cmpint (lrg_profession_def_get_kind (def), ==, LRG_PROFESSION_KIND_SECONDARY);
    g_assert_cmpint (lrg_profession_def_get_category (def), ==, LRG_PROFESSION_CATEGORY_SERVICE);
}

static void
test_def_tiers (void)
{
    g_autoptr(LrgProfessionDef) def = make_profession ("tailoring", LRG_PROFESSION_KIND_PRIMARY);
    g_autoptr(LrgProfessionDef) empty = lrg_profession_def_new ("empty", LRG_PROFESSION_KIND_PRIMARY,
                                                                LRG_PROFESSION_CATEGORY_CRAFTING);
    const LrgProfessionTier *tier;

    g_assert_cmpuint (lrg_profession_def_get_tier_count (def), ==, 4);
    g_assert_cmpuint (lrg_profession_def_get_max_skill (def), ==, 300);

    tier = lrg_profession_def_get_tier (def, 1);
    g_assert_nonnull (tier);
    g_assert_cmpstr (tier->name, ==, "Journeyman");
    g_assert_cmpuint (tier->skill_cap, ==, 150);
    g_assert_cmpuint (tier->required_level, ==, 10);
    g_assert_cmpuint (tier->cost, ==, 500);
    g_assert_null (lrg_profession_def_get_tier (def, 4));

    g_assert_cmpint (lrg_profession_def_get_tier_for_cap (def, 75), ==, 0);
    g_assert_cmpint (lrg_profession_def_get_tier_for_cap (def, 300), ==, 3);
    g_assert_cmpint (lrg_profession_def_get_tier_for_cap (def, 76), ==, -1);
    g_assert_cmpint (lrg_profession_def_get_tier_for_cap (def, 0), ==, -1);

    g_assert_cmpint (lrg_profession_def_get_next_tier (def, 0), ==, 0);
    g_assert_cmpint (lrg_profession_def_get_next_tier (def, 75), ==, 1);
    g_assert_cmpint (lrg_profession_def_get_next_tier (def, 100), ==, 1);
    g_assert_cmpint (lrg_profession_def_get_next_tier (def, 299), ==, 3);
    g_assert_cmpint (lrg_profession_def_get_next_tier (def, 300), ==, -1);

    g_assert_cmpuint (lrg_profession_def_get_tier_count (empty), ==, 0);
    g_assert_cmpuint (lrg_profession_def_get_max_skill (empty), ==, 0);
    g_assert_null (lrg_profession_def_get_tier (empty, 0));
    g_assert_cmpint (lrg_profession_def_get_next_tier (empty, 0), ==, -1);
}

static void
test_def_tiers_reject (void)
{
    g_autoptr(LrgProfessionDef) def = lrg_profession_def_new ("x", LRG_PROFESSION_KIND_PRIMARY,
                                                              LRG_PROFESSION_CATEGORY_CRAFTING);
    g_autoptr(LrgProfessionDef) many = lrg_profession_def_new ("many", LRG_PROFESSION_KIND_PRIMARY,
                                                               LRG_PROFESSION_CATEGORY_CRAFTING);
    GError *error = NULL;
    guint i;

    /* Zero cap */
    g_assert_false (lrg_profession_def_add_tier (def, lrg_profession_tier_new ("a", 0, 0, 0), &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    g_assert_cmpuint (lrg_profession_def_get_tier_count (def), ==, 0);

    /* Above the skill limit */
    g_assert_false (lrg_profession_def_add_tier (def, lrg_profession_tier_new ("a", LRG_PROFESSION_SKILL_LIMIT + 1, 0, 0), &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    /* The limit itself is fine */
    g_assert_true (lrg_profession_def_add_tier (def, lrg_profession_tier_new ("a", 100, 0, 0), NULL));

    /* Equal cap: not strictly increasing */
    g_assert_false (lrg_profession_def_add_tier (def, lrg_profession_tier_new ("b", 100, 0, 0), &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    /* Lower cap */
    g_assert_false (lrg_profession_def_add_tier (def, lrg_profession_tier_new ("b", 99, 0, 0), &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    g_assert_cmpuint (lrg_profession_def_get_tier_count (def), ==, 1);
    g_assert_cmpuint (lrg_profession_def_get_max_skill (def), ==, 100);

    g_assert_true (lrg_profession_def_add_tier (def, lrg_profession_tier_new ("c", LRG_PROFESSION_SKILL_LIMIT, 0, 0), NULL));
    g_assert_cmpuint (lrg_profession_def_get_max_skill (def), ==, LRG_PROFESSION_SKILL_LIMIT);

    /* 64 tiers is the cap */
    for (i = 1; i <= 64; i++)
        g_assert_true (lrg_profession_def_add_tier (many, lrg_profession_tier_new (NULL, i, 0, 0), NULL));
    g_assert_false (lrg_profession_def_add_tier (many, lrg_profession_tier_new (NULL, 65, 0, 0), &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT);
    g_clear_error (&error);
    g_assert_cmpuint (lrg_profession_def_get_tier_count (many), ==, 64);
}

/* ========================================================================== */
/*                          LrgRecipeItem / Def                               */
/* ========================================================================== */

static void
test_recipe_item (void)
{
    g_autoptr(LrgRecipeItem) item = lrg_recipe_item_new ("copper_bar", 2, 0.5);
    LrgRecipeItem *copy;

    g_assert_cmpstr (item->item_id, ==, "copper_bar");
    g_assert_cmpuint (item->count, ==, 2);
    g_assert_cmpfloat (item->chance, ==, 0.5);

    copy = g_boxed_copy (LRG_TYPE_RECIPE_ITEM, item);
    g_assert_cmpstr (copy->item_id, ==, "copper_bar");
    g_assert_true (copy->item_id != item->item_id);
    g_assert_cmpuint (copy->count, ==, 2);
    g_assert_cmpfloat (copy->chance, ==, 0.5);
    g_boxed_free (LRG_TYPE_RECIPE_ITEM, copy);
    lrg_recipe_item_free (NULL);
}

static void
test_recipe_item_reject (void)
{
    LrgRecipeItem *item;

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*assertion*");
    item = lrg_recipe_item_new ("x", 0, 1.0);
    g_test_assert_expected_messages ();
    g_assert_null (item);

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*assertion*");
    item = lrg_recipe_item_new ("x", 1, 0.0);
    g_test_assert_expected_messages ();
    g_assert_null (item);

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*assertion*");
    item = lrg_recipe_item_new ("x", 1, 1.0000001);
    g_test_assert_expected_messages ();
    g_assert_null (item);

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*assertion*");
    item = lrg_recipe_item_new ("x", 1, NAN);
    g_test_assert_expected_messages ();
    g_assert_null (item);

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*assertion*");
    item = lrg_recipe_item_new ("", 1, 1.0);
    g_test_assert_expected_messages ();
    g_assert_null (item);
}

static void
test_recipe_properties (void)
{
    g_autoptr(LrgRecipeDef) recipe = lrg_recipe_def_new ("copper_bar", "mining");
    g_autofree gchar *id = NULL;
    g_autofree gchar *name = NULL;
    g_autofree gchar *description = NULL;
    g_autofree gchar *profession_id = NULL;
    g_autofree gchar *station = NULL;
    g_autofree gchar *tool = NULL;
    guint required_skill;
    gdouble craft_time;
    LrgRecipeSource source;
    guint trainer_cost;
    guint required_level;
    guint notifications = 0;

    /* Defaults */
    g_assert_cmpstr (lrg_recipe_def_get_id (recipe), ==, "copper_bar");
    g_assert_cmpstr (lrg_recipe_def_get_profession_id (recipe), ==, "mining");
    g_assert_cmpuint (lrg_recipe_def_get_required_skill (recipe), ==, 0);
    g_assert_cmpfloat (lrg_recipe_def_get_craft_time (recipe), ==, 0.0);
    g_assert_cmpint (lrg_recipe_def_get_source (recipe), ==, LRG_RECIPE_SOURCE_TRAINER);
    g_assert_null (lrg_recipe_def_get_station (recipe));
    g_assert_null (lrg_recipe_def_get_tool (recipe));
    g_assert_cmpuint (lrg_recipe_def_get_reagents (recipe)->len, ==, 0);
    g_assert_cmpuint (lrg_recipe_def_get_products (recipe)->len, ==, 0);

    g_signal_connect (recipe, "notify", G_CALLBACK (count_notify), &notifications);
    g_object_set (recipe,
                  "name", "Smelt Copper", "description", "Bar",
                  "profession-id", "smelting", "required-skill", 25u,
                  "craft-time", 1.5, "source", LRG_RECIPE_SOURCE_DROP,
                  "trainer-cost", 40u, "station", "forge", "tool", "hammer",
                  "required-level", 12u, NULL);
    g_assert_cmpuint (notifications, ==, 10);

    g_object_get (recipe,
                  "id", &id, "name", &name, "description", &description,
                  "profession-id", &profession_id, "required-skill", &required_skill,
                  "craft-time", &craft_time, "source", &source,
                  "trainer-cost", &trainer_cost, "station", &station, "tool", &tool,
                  "required-level", &required_level, NULL);
    g_assert_cmpstr (id, ==, "copper_bar");
    g_assert_cmpstr (name, ==, "Smelt Copper");
    g_assert_cmpstr (description, ==, "Bar");
    g_assert_cmpstr (profession_id, ==, "smelting");
    g_assert_cmpuint (required_skill, ==, 25);
    g_assert_cmpfloat (craft_time, ==, 1.5);
    g_assert_cmpint (source, ==, LRG_RECIPE_SOURCE_DROP);
    g_assert_cmpuint (trainer_cost, ==, 40);
    g_assert_cmpstr (station, ==, "forge");
    g_assert_cmpstr (tool, ==, "hammer");
    g_assert_cmpuint (required_level, ==, 12);

    /* Setting identical values via the C API does not notify */
    lrg_recipe_def_set_name (recipe, "Smelt Copper");
    lrg_recipe_def_set_description (recipe, "Bar");
    lrg_recipe_def_set_profession_id (recipe, "smelting");
    lrg_recipe_def_set_required_skill (recipe, 25);
    lrg_recipe_def_set_craft_time (recipe, 1.5);
    lrg_recipe_def_set_source (recipe, LRG_RECIPE_SOURCE_DROP);
    lrg_recipe_def_set_trainer_cost (recipe, 40);
    lrg_recipe_def_set_station (recipe, "forge");
    lrg_recipe_def_set_tool (recipe, "hammer");
    lrg_recipe_def_set_required_level (recipe, 12);
    g_assert_cmpuint (notifications, ==, 10);

    g_assert_cmpstr (lrg_recipe_def_get_name (recipe), ==, "Smelt Copper");
    g_assert_cmpstr (lrg_recipe_def_get_description (recipe), ==, "Bar");
    g_assert_cmpuint (lrg_recipe_def_get_trainer_cost (recipe), ==, 40);
    g_assert_cmpuint (lrg_recipe_def_get_required_level (recipe), ==, 12);
}

static void
test_recipe_band (void)
{
    g_autoptr(LrgRecipeDef) recipe = lrg_recipe_def_new ("r", "p");
    g_autoptr(LrgSkillBand) band = lrg_skill_band_new (10, 20, 30, 40);
    g_autoptr(LrgSkillBand) bad = lrg_skill_band_new (40, 30, 20, 10);
    const LrgSkillBand *effective;

    /* Default band follows required-skill */
    g_assert_false (lrg_recipe_def_has_explicit_band (recipe));
    effective = lrg_recipe_def_get_band (recipe);
    g_assert_cmpuint (effective->orange, ==, 0);
    g_assert_cmpuint (effective->grey, ==, 45);
    lrg_recipe_def_set_required_skill (recipe, 50);
    effective = lrg_recipe_def_get_band (recipe);
    g_assert_cmpuint (effective->orange, ==, 50);
    g_assert_cmpuint (effective->yellow, ==, 65);
    g_assert_cmpuint (effective->green, ==, 80);
    g_assert_cmpuint (effective->grey, ==, 95);

    /* Explicit band is copied, not referenced */
    lrg_recipe_def_set_band (recipe, band);
    band->grey = 999;
    effective = lrg_recipe_def_get_band (recipe);
    g_assert_true (effective != band);
    g_assert_cmpuint (effective->orange, ==, 10);
    g_assert_cmpuint (effective->grey, ==, 40);
    g_assert_true (lrg_recipe_def_has_explicit_band (recipe));

    /* Required skill no longer changes an explicit band */
    lrg_recipe_def_set_required_skill (recipe, 60);
    g_assert_cmpuint (lrg_recipe_def_get_band (recipe)->orange, ==, 10);

    /* Invalid band rejected, previous band kept */
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*assertion*");
    lrg_recipe_def_set_band (recipe, bad);
    g_test_assert_expected_messages ();
    g_assert_cmpuint (lrg_recipe_def_get_band (recipe)->orange, ==, 10);

    /* NULL resets to the default derived from required-skill */
    lrg_recipe_def_set_band (recipe, NULL);
    g_assert_false (lrg_recipe_def_has_explicit_band (recipe));
    g_assert_cmpuint (lrg_recipe_def_get_band (recipe)->orange, ==, 60);
}

static void
test_recipe_items (void)
{
    g_autoptr(LrgRecipeDef) recipe = lrg_recipe_def_new ("bronze", "smithing");
    GPtrArray *reagents;
    GPtrArray *products;
    LrgRecipeItem *item;

    lrg_recipe_def_add_reagent (recipe, "copper_bar", 1);
    lrg_recipe_def_add_reagent (recipe, "tin_bar", 1);
    lrg_recipe_def_add_reagent (recipe, "copper_bar", 2);
    lrg_recipe_def_add_product (recipe, "bronze_bar", 2, 1.0);
    lrg_recipe_def_add_product (recipe, "bronze_bar", 1, 0.25);

    reagents = lrg_recipe_def_get_reagents (recipe);
    g_assert_cmpuint (reagents->len, ==, 2);
    item = g_ptr_array_index (reagents, 0);
    g_assert_cmpstr (item->item_id, ==, "copper_bar");
    g_assert_cmpuint (item->count, ==, 3);
    g_assert_cmpfloat (item->chance, ==, 1.0);
    item = g_ptr_array_index (reagents, 1);
    g_assert_cmpstr (item->item_id, ==, "tin_bar");

    products = lrg_recipe_def_get_products (recipe);
    g_assert_cmpuint (products->len, ==, 2);
    item = g_ptr_array_index (products, 1);
    g_assert_cmpfloat (item->chance, ==, 0.25);

    /* Merging saturates instead of wrapping */
    lrg_recipe_def_add_reagent (recipe, "tin_bar", G_MAXUINT);
    item = g_ptr_array_index (reagents, 1);
    g_assert_cmpuint (item->count, ==, G_MAXUINT);

    /* Invalid additions are rejected and leave the lists alone */
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*assertion*");
    lrg_recipe_def_add_reagent (recipe, "x", 0);
    g_test_assert_expected_messages ();
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*assertion*");
    lrg_recipe_def_add_product (recipe, "x", 1, 2.0);
    g_test_assert_expected_messages ();
    g_assert_cmpuint (reagents->len, ==, 2);
    g_assert_cmpuint (products->len, ==, 2);
}

/* ========================================================================== */
/*                             LrgGatherNodeDef                               */
/* ========================================================================== */

static void
test_gather_node (void)
{
    g_autoptr(LrgGatherNodeDef) node = lrg_gather_node_def_new ("copper_vein", "mining");
    g_autoptr(LrgSkillBand) band = lrg_skill_band_new (1, 25, 50, 100);
    g_autofree gchar *id = NULL;
    g_autofree gchar *name = NULL;
    g_autofree gchar *profession_id = NULL;
    g_autofree gchar *tool = NULL;
    guint required_skill;
    gdouble respawn;
    guint notifications = 0;
    GPtrArray *yields;
    LrgRecipeItem *item;

    g_assert_cmpuint (lrg_gather_node_def_get_band (node)->orange, ==, 0);

    g_signal_connect (node, "notify", G_CALLBACK (count_notify), &notifications);
    g_object_set (node, "name", "Copper Vein", "profession-id", "mining2",
                  "required-skill", 65u, "respawn", 300.0, "required-tool", "pick", NULL);
    g_assert_cmpuint (notifications, ==, 5);
    lrg_gather_node_def_set_name (node, "Copper Vein");
    lrg_gather_node_def_set_respawn (node, 300.0);
    g_assert_cmpuint (notifications, ==, 5);

    g_object_get (node, "id", &id, "name", &name, "profession-id", &profession_id,
                  "required-skill", &required_skill, "respawn", &respawn,
                  "required-tool", &tool, NULL);
    g_assert_cmpstr (id, ==, "copper_vein");
    g_assert_cmpstr (name, ==, "Copper Vein");
    g_assert_cmpstr (profession_id, ==, "mining2");
    g_assert_cmpuint (required_skill, ==, 65);
    g_assert_cmpfloat (respawn, ==, 300.0);
    g_assert_cmpstr (tool, ==, "pick");

    lrg_gather_node_def_set_profession_id (node, "mining");
    lrg_gather_node_def_set_required_tool (node, NULL);
    lrg_gather_node_def_set_required_skill (node, 1);
    g_assert_cmpstr (lrg_gather_node_def_get_profession_id (node), ==, "mining");
    g_assert_null (lrg_gather_node_def_get_required_tool (node));
    g_assert_cmpuint (lrg_gather_node_def_get_required_skill (node), ==, 1);
    g_assert_cmpstr (lrg_gather_node_def_get_id (node), ==, "copper_vein");
    g_assert_cmpstr (lrg_gather_node_def_get_name (node), ==, "Copper Vein");
    g_assert_cmpfloat (lrg_gather_node_def_get_respawn (node), ==, 300.0);

    /* Default band follows the skill; explicit band copies */
    g_assert_cmpuint (lrg_gather_node_def_get_band (node)->orange, ==, 1);
    lrg_gather_node_def_set_band (node, band);
    g_assert_cmpuint (lrg_gather_node_def_get_band (node)->grey, ==, 100);
    g_assert_true (lrg_gather_node_def_get_band (node) != band);
    lrg_gather_node_def_set_band (node, NULL);
    g_assert_cmpuint (lrg_gather_node_def_get_band (node)->grey, ==, 46);

    lrg_gather_node_def_add_yield (node, "copper_ore", 2, 1.0);
    lrg_gather_node_def_add_yield (node, "malachite", 1, 0.05);
    yields = lrg_gather_node_def_get_yields (node);
    g_assert_cmpuint (yields->len, ==, 2);
    item = g_ptr_array_index (yields, 1);
    g_assert_cmpstr (item->item_id, ==, "malachite");
    g_assert_cmpfloat (item->chance, ==, 0.05);

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*assertion*");
    lrg_gather_node_def_add_yield (node, "bad", 1, -1.0);
    g_test_assert_expected_messages ();
    g_assert_cmpuint (yields->len, ==, 2);
}

/* ========================================================================== */
/*                     LrgProfessionState: professions                        */
/* ========================================================================== */

static void
test_state_properties (void)
{
    g_autoptr(LrgProfessionState) state = lrg_profession_state_new (2);
    g_autoptr(LrgProfessionState) plain = g_object_new (LRG_TYPE_PROFESSION_STATE, NULL);
    guint max_primary;
    guint margin;
    guint notifications = 0;

    g_object_get (state, "max-primary", &max_primary, "train-margin", &margin, NULL);
    g_assert_cmpuint (max_primary, ==, 2);
    g_assert_cmpuint (margin, ==, 25);
    g_assert_cmpuint (lrg_profession_state_get_max_primary (plain), ==, 2);

    g_signal_connect (state, "notify", G_CALLBACK (count_notify), &notifications);
    g_object_set (state, "max-primary", 3u, "train-margin", 10u, NULL);
    g_assert_cmpuint (notifications, ==, 2);
    lrg_profession_state_set_max_primary (state, 3);
    lrg_profession_state_set_train_margin (state, 10);
    g_assert_cmpuint (notifications, ==, 2);
    g_assert_cmpuint (lrg_profession_state_get_max_primary (state), ==, 3);
    g_assert_cmpuint (lrg_profession_state_get_train_margin (state), ==, 10);

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*assertion*");
    lrg_profession_state_set_max_primary (state, LRG_PROFESSION_STATE_MAX_PROFESSIONS + 1);
    g_test_assert_expected_messages ();
    g_assert_cmpuint (lrg_profession_state_get_max_primary (state), ==, 3);
}

static void
test_state_learn (void)
{
    g_autoptr(LrgProfessionState) state = lrg_profession_state_new (2);
    g_autoptr(LrgProfessionDef) mining = make_profession ("mining", LRG_PROFESSION_KIND_PRIMARY);
    g_autoptr(LrgProfessionDef) alchemy = make_profession ("alchemy", LRG_PROFESSION_KIND_PRIMARY);
    g_autoptr(GPtrArray) ids = NULL;

    g_assert_false (lrg_profession_state_knows (state, "mining"));
    g_assert_cmpuint (lrg_profession_state_get_skill (state, "mining"), ==, 0);
    g_assert_cmpuint (lrg_profession_state_get_max_skill (state, "mining"), ==, 0);

    /* Level 5 is exactly tier 0's requirement */
    g_assert_true (lrg_profession_state_learn (state, mining, 5, NULL));
    g_assert_true (lrg_profession_state_learn (state, alchemy, 60, NULL));
    g_assert_true (lrg_profession_state_knows (state, "mining"));
    g_assert_cmpuint (lrg_profession_state_get_skill (state, "mining"), ==, 1);
    g_assert_cmpuint (lrg_profession_state_get_max_skill (state, "mining"), ==, 75);
    g_assert_false (lrg_profession_state_knows (state, NULL));

    ids = lrg_profession_state_get_professions (state);
    g_assert_cmpuint (ids->len, ==, 2);
    g_assert_cmpstr (g_ptr_array_index (ids, 0), ==, "alchemy");
    g_assert_cmpstr (g_ptr_array_index (ids, 1), ==, "mining");
    g_assert_cmpuint (lrg_profession_state_get_primary_count (state, NULL), ==, 2);
}

static void
test_state_learn_rejections (void)
{
    g_autoptr(LrgProfessionState) state = lrg_profession_state_new (1);
    g_autoptr(LrgProfessionDef) mining = make_profession ("mining", LRG_PROFESSION_KIND_PRIMARY);
    g_autoptr(LrgProfessionDef) herbalism = make_profession ("herbalism", LRG_PROFESSION_KIND_PRIMARY);
    g_autoptr(LrgProfessionDef) no_tiers = lrg_profession_def_new ("none", LRG_PROFESSION_KIND_SECONDARY,
                                                                   LRG_PROFESSION_CATEGORY_SERVICE);
    g_autoptr(LrgProfessionDef) empty_id = make_simple_profession ("", LRG_PROFESSION_KIND_SECONDARY);
    g_autoptr(LrgProfessionDef) bad_utf8 = make_simple_profession ("ab\xff", LRG_PROFESSION_KIND_SECONDARY);
    g_autoptr(LrgProfessionDef) long_id = NULL;
    g_autoptr(LrgProfessionDef) max_id = NULL;
    g_autoptr(GVariant) before = NULL;
    g_autofree gchar *long_str = g_strnfill (129, 'a');
    g_autofree gchar *max_str = g_strnfill (128, 'b');
    GError *error = NULL;

    long_id = make_simple_profession (long_str, LRG_PROFESSION_KIND_SECONDARY);
    max_id = make_simple_profession (max_str, LRG_PROFESSION_KIND_SECONDARY);

    /* Level 4 is below the requirement */
    before = snapshot (state);
    g_assert_false (lrg_profession_state_learn (state, mining, 4, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);
    assert_unchanged (state, before);

    /* No tiers */
    g_assert_false (lrg_profession_state_learn (state, no_tiers, 60, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    /* Hostile identifiers */
    g_assert_false (lrg_profession_state_learn (state, empty_id, 60, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    g_assert_false (lrg_profession_state_learn (state, bad_utf8, 60, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    g_assert_false (lrg_profession_state_learn (state, long_id, 60, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    assert_unchanged (state, before);

    /* 128 bytes is accepted */
    g_assert_true (lrg_profession_state_learn (state, max_id, 60, NULL));

    /* Duplicate */
    g_assert_true (lrg_profession_state_learn (state, mining, 60, NULL));
    g_clear_pointer (&before, g_variant_unref);
    before = snapshot (state);
    g_assert_false (lrg_profession_state_learn (state, mining, 60, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_DUPLICATE);
    g_clear_error (&error);

    /* Limit reached for primaries */
    g_assert_false (lrg_profession_state_learn (state, herbalism, 60, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT);
    g_clear_error (&error);
    assert_unchanged (state, before);
}

/* Learns primaries until rejected; returns how many succeeded */
static guint
learn_primaries_until_limit (LrgProfessionState *state)
{
    const gchar *names[] = { "p1", "p2", "p3", "p4" };
    guint learned = 0;
    guint i;

    for (i = 0; i < G_N_ELEMENTS (names); i++)
    {
        g_autoptr(LrgProfessionDef) def = make_simple_profession (names[i], LRG_PROFESSION_KIND_PRIMARY);
        g_autoptr(GVariant) before = snapshot (state);
        GError *error = NULL;

        if (lrg_profession_state_learn (state, def, 1, &error))
        {
            learned++;
            continue;
        }
        g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT);
        g_clear_error (&error);
        assert_unchanged (state, before);
    }
    return learned;
}

static void
test_state_primary_limit (void)
{
    guint max_primary;

    for (max_primary = 0; max_primary <= 2; max_primary++)
    {
        g_autoptr(LrgProfessionState) state = lrg_profession_state_new (max_primary);
        guint i;

        g_assert_cmpuint (learn_primaries_until_limit (state), ==, max_primary);
        g_assert_cmpuint (lrg_profession_state_get_primary_count (state, NULL), ==, max_primary);

        /* Secondaries are never limited by max-primary */
        for (i = 0; i < 10; i++)
        {
            g_autofree gchar *id = g_strdup_printf ("secondary%u", i);
            g_autoptr(LrgProfessionDef) def = make_simple_profession (id, LRG_PROFESSION_KIND_SECONDARY);

            g_assert_true (lrg_profession_state_learn (state, def, 1, NULL));
        }
        g_assert_cmpuint (lrg_profession_state_get_primary_count (state, NULL), ==, max_primary);
    }
}

static void
test_state_profession_cap (void)
{
    g_autoptr(LrgProfessionState) state = lrg_profession_state_new (0);
    g_autoptr(LrgProfessionDef) extra = make_simple_profession ("extra", LRG_PROFESSION_KIND_SECONDARY);
    g_autoptr(GVariant) before = NULL;
    GError *error = NULL;
    guint i;

    for (i = 0; i < LRG_PROFESSION_STATE_MAX_PROFESSIONS; i++)
    {
        g_autofree gchar *id = g_strdup_printf ("s%02u", i);
        g_autoptr(LrgProfessionDef) def = make_simple_profession (id, LRG_PROFESSION_KIND_SECONDARY);

        g_assert_true (lrg_profession_state_learn (state, def, 1, NULL));
    }

    before = snapshot (state);
    g_assert_false (lrg_profession_state_learn (state, extra, 1, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT);
    g_clear_error (&error);
    assert_unchanged (state, before);
}

static void
test_state_unlearn (void)
{
    g_autoptr(LrgProfessionState) state = lrg_profession_state_new (1);
    g_autoptr(LrgProfessionDef) mining = make_profession ("mining", LRG_PROFESSION_KIND_PRIMARY);
    g_autoptr(LrgProfessionDef) herbalism = make_profession ("herbalism", LRG_PROFESSION_KIND_PRIMARY);
    g_autoptr(LrgProfessionDef) cooking = make_simple_profession ("cooking", LRG_PROFESSION_KIND_SECONDARY);
    g_autoptr(LrgRecipeDef) smelt = lrg_recipe_def_new ("smelt_copper", "mining");
    g_autoptr(LrgRecipeDef) smelt2 = lrg_recipe_def_new ("smelt_tin", "mining");
    g_autoptr(LrgRecipeDef) stew = lrg_recipe_def_new ("stew", "cooking");
    g_autoptr(GPtrArray) all = NULL;

    g_assert_true (lrg_profession_state_learn (state, mining, 10, NULL));
    g_assert_true (lrg_profession_state_learn (state, cooking, 10, NULL));
    g_assert_true (lrg_profession_state_learn_recipe (state, smelt, NULL));
    g_assert_true (lrg_profession_state_learn_recipe (state, smelt2, NULL));
    g_assert_true (lrg_profession_state_learn_recipe (state, stew, NULL));

    g_assert_false (lrg_profession_state_unlearn (state, "unknown"));
    g_assert_false (lrg_profession_state_unlearn (state, NULL));

    g_assert_true (lrg_profession_state_unlearn (state, "mining"));
    g_assert_false (lrg_profession_state_knows (state, "mining"));
    g_assert_false (lrg_profession_state_knows_recipe (state, "smelt_copper"));
    g_assert_false (lrg_profession_state_knows_recipe (state, "smelt_tin"));
    g_assert_true (lrg_profession_state_knows_recipe (state, "stew"));
    all = lrg_profession_state_get_recipes (state, NULL);
    g_assert_cmpuint (all->len, ==, 1);
    g_assert_false (lrg_profession_state_unlearn (state, "mining"));

    /* The primary slot is free again */
    g_assert_true (lrg_profession_state_learn (state, herbalism, 10, NULL));

    /* Relearning starts over at skill 1 */
    g_assert_true (lrg_profession_state_unlearn (state, "herbalism"));
    g_assert_true (lrg_profession_state_learn (state, mining, 10, NULL));
    g_assert_cmpuint (lrg_profession_state_get_skill (state, "mining"), ==, 1);
}

/* ========================================================================== */
/*                       LrgProfessionState: training                         */
/* ========================================================================== */

static void
test_state_train (void)
{
    g_autoptr(LrgProfessionState) state = lrg_profession_state_new (2);
    g_autoptr(LrgProfessionDef) mining = make_profession ("mining", LRG_PROFESSION_KIND_PRIMARY);
    const LrgProfessionTier *tier = NULL;

    g_assert_true (lrg_profession_state_learn (state, mining, 60, NULL));

    /* 75 - 25 = 50 is the first trainable skill */
    lrg_profession_state_set_skill (state, "mining", 50, 75);
    g_assert_true (lrg_profession_state_can_train (state, mining, 10, &tier, NULL));
    g_assert_nonnull (tier);
    g_assert_cmpstr (tier->name, ==, "Journeyman");
    g_assert_cmpuint (tier->cost, ==, 500);
    g_assert_true (lrg_profession_state_can_train (state, mining, 10, NULL, NULL));

    g_assert_true (lrg_profession_state_train (state, mining, 10, NULL));
    g_assert_cmpuint (lrg_profession_state_get_max_skill (state, "mining"), ==, 150);
    g_assert_cmpuint (lrg_profession_state_get_skill (state, "mining"), ==, 50);

    lrg_profession_state_set_skill (state, "mining", 150, 150);
    g_assert_true (lrg_profession_state_train (state, mining, 20, NULL));
    g_assert_cmpuint (lrg_profession_state_get_max_skill (state, "mining"), ==, 225);
    lrg_profession_state_set_skill (state, "mining", 200, 225);
    g_assert_true (lrg_profession_state_train (state, mining, 35, NULL));
    g_assert_cmpuint (lrg_profession_state_get_max_skill (state, "mining"), ==, 300);
}

static void
test_state_train_rejections (void)
{
    g_autoptr(LrgProfessionState) state = lrg_profession_state_new (2);
    g_autoptr(LrgProfessionDef) mining = make_profession ("mining", LRG_PROFESSION_KIND_PRIMARY);
    g_autoptr(LrgProfessionDef) unknown = make_profession ("unknown", LRG_PROFESSION_KIND_PRIMARY);
    g_autoptr(GVariant) before = NULL;
    const LrgProfessionTier *tier = (const LrgProfessionTier *)0x1;
    GError *error = NULL;

    g_assert_true (lrg_profession_state_learn (state, mining, 60, NULL));

    /* Not known */
    before = snapshot (state);
    g_assert_false (lrg_profession_state_can_train (state, unknown, 60, &tier, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_NOT_FOUND);
    g_assert_null (tier);
    g_clear_error (&error);
    g_assert_false (lrg_profession_state_train (state, unknown, 60, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_NOT_FOUND);
    g_clear_error (&error);

    /* Skill 49 is one short of the margin */
    lrg_profession_state_set_skill (state, "mining", 49, 75);
    g_clear_pointer (&before, g_variant_unref);
    before = snapshot (state);
    g_assert_false (lrg_profession_state_train (state, mining, 60, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);
    assert_unchanged (state, before);

    /* Level 9 is one short of Journeyman's level 10 */
    lrg_profession_state_set_skill (state, "mining", 75, 75);
    g_clear_pointer (&before, g_variant_unref);
    before = snapshot (state);
    tier = (const LrgProfessionTier *)0x1;
    g_assert_false (lrg_profession_state_can_train (state, mining, 9, &tier, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_assert_null (tier);
    g_clear_error (&error);
    g_assert_false (lrg_profession_state_train (state, mining, 9, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);
    assert_unchanged (state, before);

    /* Margin 0: skill must equal the cap */
    lrg_profession_state_set_train_margin (state, 0);
    lrg_profession_state_set_skill (state, "mining", 74, 75);
    g_assert_false (lrg_profession_state_can_train (state, mining, 60, NULL, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);
    lrg_profession_state_set_skill (state, "mining", 75, 75);
    g_assert_true (lrg_profession_state_can_train (state, mining, 60, NULL, NULL));

    /* A margin larger than the cap saturates at 0 */
    lrg_profession_state_set_train_margin (state, 1000);
    lrg_profession_state_set_skill (state, "mining", 0, 75);
    g_assert_true (lrg_profession_state_can_train (state, mining, 60, NULL, NULL));

    /* Top tier */
    lrg_profession_state_set_skill (state, "mining", 300, 300);
    g_clear_pointer (&before, g_variant_unref);
    before = snapshot (state);
    g_assert_false (lrg_profession_state_can_train (state, mining, 60, NULL, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT);
    g_clear_error (&error);
    g_assert_false (lrg_profession_state_train (state, mining, 60, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT);
    g_clear_error (&error);
    assert_unchanged (state, before);

    /* A cap between tiers (admin override) trains to the next higher tier */
    lrg_profession_state_set_skill (state, "mining", 100, 100);
    g_assert_true (lrg_profession_state_train (state, mining, 60, NULL));
    g_assert_cmpuint (lrg_profession_state_get_max_skill (state, "mining"), ==, 150);
}

/* ========================================================================== */
/*                        LrgProfessionState: recipes                         */
/* ========================================================================== */

static void
test_state_recipes (void)
{
    g_autoptr(LrgProfessionState) state = lrg_profession_state_new (2);
    g_autoptr(LrgProfessionDef) smithing = make_profession ("smithing", LRG_PROFESSION_KIND_PRIMARY);
    g_autoptr(LrgProfessionDef) mining = make_profession ("mining", LRG_PROFESSION_KIND_PRIMARY);
    g_autoptr(LrgRecipeDef) sword = lrg_recipe_def_new ("sword", "smithing");
    g_autoptr(LrgRecipeDef) axe = lrg_recipe_def_new ("axe", "smithing");
    g_autoptr(LrgRecipeDef) smelt = lrg_recipe_def_new ("smelt", "mining");
    g_autoptr(GPtrArray) smithing_recipes = NULL;
    g_autoptr(GPtrArray) all = NULL;
    g_autoptr(GPtrArray) none = NULL;

    g_assert_true (lrg_profession_state_learn (state, smithing, 60, NULL));
    g_assert_true (lrg_profession_state_learn (state, mining, 60, NULL));
    g_assert_true (lrg_profession_state_learn_recipe (state, sword, NULL));
    g_assert_true (lrg_profession_state_learn_recipe (state, axe, NULL));
    g_assert_true (lrg_profession_state_learn_recipe (state, smelt, NULL));

    g_assert_true (lrg_profession_state_knows_recipe (state, "sword"));
    g_assert_false (lrg_profession_state_knows_recipe (state, "shield"));
    g_assert_false (lrg_profession_state_knows_recipe (state, NULL));

    smithing_recipes = lrg_profession_state_get_recipes (state, "smithing");
    g_assert_cmpuint (smithing_recipes->len, ==, 2);
    g_assert_cmpstr (g_ptr_array_index (smithing_recipes, 0), ==, "axe");
    g_assert_cmpstr (g_ptr_array_index (smithing_recipes, 1), ==, "sword");

    all = lrg_profession_state_get_recipes (state, NULL);
    g_assert_cmpuint (all->len, ==, 3);
    g_assert_cmpstr (g_ptr_array_index (all, 0), ==, "axe");
    g_assert_cmpstr (g_ptr_array_index (all, 1), ==, "smelt");
    g_assert_cmpstr (g_ptr_array_index (all, 2), ==, "sword");

    none = lrg_profession_state_get_recipes (state, "alchemy");
    g_assert_cmpuint (none->len, ==, 0);
}

static void
test_state_recipe_rejections (void)
{
    g_autoptr(LrgProfessionState) state = lrg_profession_state_new (2);
    g_autoptr(LrgProfessionDef) smithing = make_profession ("smithing", LRG_PROFESSION_KIND_PRIMARY);
    g_autoptr(LrgRecipeDef) sword = lrg_recipe_def_new ("sword", "smithing");
    g_autoptr(LrgRecipeDef) potion = lrg_recipe_def_new ("potion", "alchemy");
    g_autoptr(LrgRecipeDef) empty = lrg_recipe_def_new ("", "smithing");
    g_autoptr(LrgRecipeDef) orphan = lrg_recipe_def_new ("orphan", NULL);
    g_autoptr(GVariant) before = NULL;
    GError *error = NULL;

    g_assert_true (lrg_profession_state_learn (state, smithing, 60, NULL));
    lrg_recipe_def_set_required_skill (sword, 50);
    lrg_profession_state_set_skill (state, "smithing", 49, 75);
    before = snapshot (state);

    /* Skill 49 < 50 */
    g_assert_false (lrg_profession_state_learn_recipe (state, sword, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);

    /* Profession not known */
    g_assert_false (lrg_profession_state_learn_recipe (state, potion, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);

    /* Invalid identifiers */
    g_assert_false (lrg_profession_state_learn_recipe (state, empty, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    g_assert_false (lrg_profession_state_learn_recipe (state, orphan, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    assert_unchanged (state, before);

    /* Skill 50 == 50 succeeds, then duplicate */
    lrg_profession_state_set_skill (state, "smithing", 50, 75);
    g_assert_true (lrg_profession_state_learn_recipe (state, sword, NULL));
    g_clear_pointer (&before, g_variant_unref);
    before = snapshot (state);
    g_assert_false (lrg_profession_state_learn_recipe (state, sword, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_DUPLICATE);
    g_clear_error (&error);
    assert_unchanged (state, before);
}

static void
test_state_recipe_cap (void)
{
    g_autoptr(LrgProfessionState) state = lrg_profession_state_new (1);
    g_autoptr(LrgProfessionDef) smithing = make_profession ("smithing", LRG_PROFESSION_KIND_PRIMARY);
    g_autoptr(LrgRecipeDef) extra = lrg_recipe_def_new ("zzz_extra", "smithing");
    g_autoptr(GPtrArray) all = NULL;
    GError *error = NULL;
    guint i;

    g_assert_true (lrg_profession_state_learn (state, smithing, 60, NULL));
    for (i = 0; i < LRG_PROFESSION_STATE_MAX_RECIPES; i++)
    {
        g_autofree gchar *id = g_strdup_printf ("r%04u", i);
        g_autoptr(LrgRecipeDef) recipe = lrg_recipe_def_new (id, "smithing");

        g_assert_true (lrg_profession_state_learn_recipe (state, recipe, NULL));
    }

    g_assert_false (lrg_profession_state_learn_recipe (state, extra, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT);
    g_clear_error (&error);
    all = lrg_profession_state_get_recipes (state, NULL);
    g_assert_cmpuint (all->len, ==, LRG_PROFESSION_STATE_MAX_RECIPES);
    g_assert_false (lrg_profession_state_knows_recipe (state, "zzz_extra"));
}

static void
test_state_starter_recipes (void)
{
    g_autoptr(LrgProfessionState) state = lrg_profession_state_new (2);
    g_autoptr(LrgProfessionDef) cooking = make_simple_profession ("cooking", LRG_PROFESSION_KIND_SECONDARY);
    g_autoptr(GPtrArray) recipes = g_ptr_array_new_with_free_func (g_object_unref);
    LrgRecipeDef *recipe;

    recipe = lrg_recipe_def_new ("bread", "cooking");
    lrg_recipe_def_set_source (recipe, LRG_RECIPE_SOURCE_STARTER);
    g_ptr_array_add (recipes, recipe);
    recipe = lrg_recipe_def_new ("feast", "cooking");
    lrg_recipe_def_set_source (recipe, LRG_RECIPE_SOURCE_STARTER);
    lrg_recipe_def_set_required_skill (recipe, 50);
    g_ptr_array_add (recipes, recipe);
    recipe = lrg_recipe_def_new ("soup", "cooking");
    g_ptr_array_add (recipes, recipe);
    recipe = lrg_recipe_def_new ("potion", "alchemy");
    lrg_recipe_def_set_source (recipe, LRG_RECIPE_SOURCE_STARTER);
    g_ptr_array_add (recipes, recipe);

    g_assert_cmpuint (lrg_profession_state_learn_starter_recipes (state, recipes), ==, 0);
    g_assert_true (lrg_profession_state_learn (state, cooking, 1, NULL));
    g_assert_cmpuint (lrg_profession_state_learn_starter_recipes (state, recipes), ==, 1);
    g_assert_true (lrg_profession_state_knows_recipe (state, "bread"));
    g_assert_false (lrg_profession_state_knows_recipe (state, "soup"));
    g_assert_false (lrg_profession_state_knows_recipe (state, "feast"));
    /* Idempotent */
    g_assert_cmpuint (lrg_profession_state_learn_starter_recipes (state, recipes), ==, 0);
    lrg_profession_state_set_skill (state, "cooking", 50, 75);
    g_assert_cmpuint (lrg_profession_state_learn_starter_recipes (state, recipes), ==, 1);
    g_assert_true (lrg_profession_state_knows_recipe (state, "feast"));
}

/* ========================================================================== */
/*                       LrgProfessionState: skill-ups                        */
/* ========================================================================== */

static void
test_state_skill_up (void)
{
    g_autoptr(LrgProfessionState) state = lrg_profession_state_new (2);
    g_autoptr(LrgProfessionDef) mining = make_profession ("mining", LRG_PROFESSION_KIND_PRIMARY);
    g_autoptr(LrgSkillBand) orange = lrg_skill_band_new (0, 1000, 1000, 1000);
    g_autoptr(LrgSkillBand) grey = lrg_skill_band_new (0, 0, 0, 0);
    g_autoptr(LrgSkillBand) yellow = lrg_skill_band_new (1, 10, 20, 30);
    g_autoptr(LrgSkillBand) invalid = lrg_skill_band_new (10, 5, 20, 30);
    guint i;

    g_assert_true (lrg_profession_state_learn (state, mining, 60, NULL));

    /* Orange always succeeds, even with the largest roll */
    g_assert_cmpuint (lrg_profession_state_try_skill_up (state, "mining", orange, 0.9999999), ==, 1);
    g_assert_cmpuint (lrg_profession_state_get_skill (state, "mining"), ==, 2);

    /* Grey never succeeds, even with roll 0 */
    g_assert_cmpuint (lrg_profession_state_try_skill_up (state, "mining", grey, 0.0), ==, 0);
    g_assert_cmpuint (lrg_profession_state_get_skill (state, "mining"), ==, 2);

    /* Chance at skill 25 in (1,10,20,30) is exactly 0.25: roll must be below */
    lrg_profession_state_set_skill (state, "mining", 25, 75);
    g_assert_cmpfloat (lrg_skill_band_get_chance (yellow, 25), ==, 0.25);
    g_assert_cmpuint (lrg_profession_state_try_skill_up (state, "mining", yellow, 0.25), ==, 0);
    g_assert_cmpuint (lrg_profession_state_get_skill (state, "mining"), ==, 25);
    g_assert_cmpuint (lrg_profession_state_try_skill_up (state, "mining", yellow, 0.2499), ==, 1);
    g_assert_cmpuint (lrg_profession_state_get_skill (state, "mining"), ==, 26);

    /* Invalid rolls, band and profession grant nothing */
    g_assert_cmpuint (lrg_profession_state_try_skill_up (state, "mining", orange, -0.1), ==, 0);
    g_assert_cmpuint (lrg_profession_state_try_skill_up (state, "mining", orange, 1.0), ==, 0);
    g_assert_cmpuint (lrg_profession_state_try_skill_up (state, "mining", orange, NAN), ==, 0);
    g_assert_cmpuint (lrg_profession_state_try_skill_up (state, "mining", orange, INFINITY), ==, 0);
    g_assert_cmpuint (lrg_profession_state_try_skill_up (state, "mining", orange, -INFINITY), ==, 0);
    g_assert_cmpuint (lrg_profession_state_try_skill_up (state, "mining", invalid, 0.0), ==, 0);
    g_assert_cmpuint (lrg_profession_state_try_skill_up (state, "herbalism", orange, 0.0), ==, 0);
    g_assert_cmpuint (lrg_profession_state_try_skill_up (state, NULL, orange, 0.0), ==, 0);
    g_assert_cmpuint (lrg_profession_state_get_skill (state, "mining"), ==, 26);

    /* Never exceeds the cap */
    for (i = 0; i < 1000; i++)
        lrg_profession_state_try_skill_up (state, "mining", orange, 0.0);
    g_assert_cmpuint (lrg_profession_state_get_skill (state, "mining"), ==, 75);
    g_assert_cmpuint (lrg_profession_state_try_skill_up (state, "mining", orange, 0.0), ==, 0);
    g_assert_cmpuint (lrg_profession_state_get_skill (state, "mining"), ==, 75);
}

static void
test_state_set_skill (void)
{
    g_autoptr(LrgProfessionState) state = lrg_profession_state_new (2);
    g_autoptr(GHashTable) defs = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_object_unref);
    LrgProfessionDef *cooking = make_simple_profession ("cooking", LRG_PROFESSION_KIND_SECONDARY);

    g_hash_table_insert (defs, (gpointer)"cooking", cooking);

    /* Adds unknown professions; skill clamps to the cap */
    lrg_profession_state_set_skill (state, "cooking", 500, 300);
    g_assert_true (lrg_profession_state_knows (state, "cooking"));
    g_assert_cmpuint (lrg_profession_state_get_skill (state, "cooking"), ==, 300);
    g_assert_cmpuint (lrg_profession_state_get_max_skill (state, "cooking"), ==, 300);

    /* Unknown kind fails closed as primary until definitions say otherwise */
    g_assert_cmpuint (lrg_profession_state_get_primary_count (state, NULL), ==, 1);
    g_assert_cmpuint (lrg_profession_state_get_primary_count (state, defs), ==, 0);

    lrg_profession_state_set_skill (state, "cooking", 0, 1);
    g_assert_cmpuint (lrg_profession_state_get_skill (state, "cooking"), ==, 0);
    g_assert_cmpuint (lrg_profession_state_get_max_skill (state, "cooking"), ==, 1);

    /* Invalid arguments are programmer errors */
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*assertion*");
    lrg_profession_state_set_skill (state, "cooking", 1, 0);
    g_test_assert_expected_messages ();
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*assertion*");
    lrg_profession_state_set_skill (state, "", 1, 1);
    g_test_assert_expected_messages ();
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*assertion*");
    lrg_profession_state_set_skill (state, "cooking", 1, LRG_PROFESSION_SKILL_LIMIT + 1);
    g_test_assert_expected_messages ();
    g_assert_cmpuint (lrg_profession_state_get_max_skill (state, "cooking"), ==, 1);
}

/* ========================================================================== */
/*                  LrgProfessionState: crafting / gathering                  */
/* ========================================================================== */

typedef struct
{
    LrgProfessionState *state;
    LrgProfessionDef   *smithing;
    LrgRecipeDef       *sword;
    GHashTable         *items;
} CraftFixture;

static void
craft_fixture_setup (CraftFixture  *fixture,
                     gconstpointer  user_data)
{
    fixture->state = lrg_profession_state_new (2);
    fixture->smithing = make_profession ("smithing", LRG_PROFESSION_KIND_PRIMARY);
    fixture->sword = lrg_recipe_def_new ("sword", "smithing");
    lrg_recipe_def_set_required_skill (fixture->sword, 20);
    lrg_recipe_def_add_reagent (fixture->sword, "iron_bar", 3);
    lrg_recipe_def_add_reagent (fixture->sword, "leather", 1);
    lrg_recipe_def_add_product (fixture->sword, "iron_sword", 1, 1.0);
    fixture->items = new_item_table ();

    g_assert_true (lrg_profession_state_learn (fixture->state, fixture->smithing, 60, NULL));
    lrg_profession_state_set_skill (fixture->state, "smithing", 20, 75);
    g_assert_true (lrg_profession_state_learn_recipe (fixture->state, fixture->sword, NULL));
}

static void
craft_fixture_teardown (CraftFixture  *fixture,
                        gconstpointer  user_data)
{
    g_clear_object (&fixture->state);
    g_clear_object (&fixture->smithing);
    g_clear_object (&fixture->sword);
    g_clear_pointer (&fixture->items, g_hash_table_unref);
}

static void
test_craft_callback (CraftFixture  *fixture,
                     gconstpointer  user_data)
{
    GError *error = NULL;

    put_items (fixture->items, "iron_bar", 6);
    put_items (fixture->items, "leather", 2);

    /* Quantity 1 and 2 fit exactly */
    g_assert_true (lrg_profession_state_can_craft (fixture->state, fixture->sword, 1,
                                                   table_count, fixture->items, NULL));
    g_assert_true (lrg_profession_state_can_craft (fixture->state, fixture->sword, 2,
                                                   table_count, fixture->items, NULL));

    /* Quantity 3 needs 9 iron bars */
    g_assert_false (lrg_profession_state_can_craft (fixture->state, fixture->sword, 3,
                                                    table_count, fixture->items, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);

    /* One bar short for quantity 2 */
    put_items (fixture->items, "iron_bar", 5);
    g_assert_false (lrg_profession_state_can_craft (fixture->state, fixture->sword, 2,
                                                    table_count, fixture->items, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);

    /* Missing second reagent */
    put_items (fixture->items, "iron_bar", 100);
    put_items (fixture->items, "leather", 0);
    g_assert_false (lrg_profession_state_can_craft (fixture->state, fixture->sword, 1,
                                                    table_count, fixture->items, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);

    /* Without a counter only knowledge and skill are checked */
    g_assert_true (lrg_profession_state_can_craft (fixture->state, fixture->sword, 1,
                                                   NULL, NULL, NULL));
}

static void
test_craft_overflow (CraftFixture  *fixture,
                     gconstpointer  user_data)
{
    GError *error = NULL;

    /* 3 * G_MAXUINT would wrap in 32 bits; the 64-bit check still rejects */
    put_items (fixture->items, "iron_bar", G_MAXUINT);
    put_items (fixture->items, "leather", G_MAXUINT);
    g_assert_false (lrg_profession_state_can_craft (fixture->state, fixture->sword, G_MAXUINT,
                                                    table_count, fixture->items, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);

    g_assert_true (lrg_profession_state_can_craft (fixture->state, fixture->sword, G_MAXUINT / 3,
                                                   table_count, fixture->items, NULL));
}

static void
test_craft_rejections (CraftFixture  *fixture,
                       gconstpointer  user_data)
{
    g_autoptr(LrgRecipeDef) unknown = lrg_recipe_def_new ("shield", "smithing");
    g_autoptr(LrgRecipeDef) empty = lrg_recipe_def_new ("", "smithing");
    g_autoptr(GVariant) before = snapshot (fixture->state);
    GError *error = NULL;

    put_items (fixture->items, "iron_bar", 100);
    put_items (fixture->items, "leather", 100);

    /* Quantity 0 */
    g_assert_false (lrg_profession_state_can_craft (fixture->state, fixture->sword, 0,
                                                    table_count, fixture->items, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    /* Bad id */
    g_assert_false (lrg_profession_state_can_craft (fixture->state, empty, 1,
                                                    table_count, fixture->items, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    /* Recipe not known */
    g_assert_false (lrg_profession_state_can_craft (fixture->state, unknown, 1,
                                                    table_count, fixture->items, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);

    /* Tool required and missing, then present */
    lrg_recipe_def_set_tool (fixture->sword, "hammer");
    g_assert_false (lrg_profession_state_can_craft (fixture->state, fixture->sword, 1,
                                                    table_count, fixture->items, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);
    put_items (fixture->items, "hammer", 1);
    g_assert_true (lrg_profession_state_can_craft (fixture->state, fixture->sword, 5,
                                                   table_count, fixture->items, NULL));

    /* Skill lowered below the recipe (admin) */
    lrg_profession_state_set_skill (fixture->state, "smithing", 19, 75);
    g_assert_false (lrg_profession_state_can_craft (fixture->state, fixture->sword, 1,
                                                    table_count, fixture->items, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);
    lrg_profession_state_set_skill (fixture->state, "smithing", 20, 75);
    assert_unchanged (fixture->state, before);

    /* Profession forgotten: recipe gone as well */
    g_assert_true (lrg_profession_state_unlearn (fixture->state, "smithing"));
    g_assert_false (lrg_profession_state_can_craft (fixture->state, fixture->sword, 1,
                                                    table_count, fixture->items, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);
}

static void
test_craft_inventory (CraftFixture  *fixture,
                      gconstpointer  user_data)
{
    g_autoptr(LrgInventory) inventory = lrg_inventory_new (10);
    g_autoptr(LrgItemDef) iron = lrg_item_def_new ("iron_bar");
    g_autoptr(LrgItemDef) leather = lrg_item_def_new ("leather");
    GError *error = NULL;

    /* Small stacks so the iron spreads across several slots */
    lrg_item_def_set_stackable (iron, TRUE);
    lrg_item_def_set_max_stack (iron, 4);
    lrg_item_def_set_stackable (leather, TRUE);
    lrg_item_def_set_max_stack (leather, 20);
    g_assert_cmpuint (lrg_inventory_add_item (inventory, iron, 9), ==, 9);
    g_assert_cmpuint (lrg_inventory_add_item (inventory, leather, 3), ==, 3);
    g_assert_cmpuint (lrg_profession_state_inventory_count ("iron_bar", inventory), ==, 9);
    g_assert_cmpuint (lrg_profession_state_inventory_count ("missing", inventory), ==, 0);

    g_assert_true (lrg_profession_state_can_craft (fixture->state, fixture->sword, 3,
                                                   lrg_profession_state_inventory_count,
                                                   inventory, NULL));
    g_assert_false (lrg_profession_state_can_craft (fixture->state, fixture->sword, 4,
                                                    lrg_profession_state_inventory_count,
                                                    inventory, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);

    /* Non-inventory user data is a programmer error */
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*assertion*");
    g_assert_cmpuint (lrg_profession_state_inventory_count ("iron_bar", fixture->state), ==, 0);
    g_test_assert_expected_messages ();
}

static void
test_state_gather (void)
{
    g_autoptr(LrgProfessionState) state = lrg_profession_state_new (2);
    g_autoptr(LrgProfessionDef) mining = make_profession ("mining", LRG_PROFESSION_KIND_PRIMARY);
    g_autoptr(LrgGatherNodeDef) vein = lrg_gather_node_def_new ("mithril", "mining");
    g_autoptr(LrgGatherNodeDef) herb = lrg_gather_node_def_new ("peacebloom", "herbalism");
    g_autoptr(LrgGatherNodeDef) orphan = lrg_gather_node_def_new ("orphan", NULL);
    g_autoptr(GHashTable) items = new_item_table ();
    GError *error = NULL;

    lrg_gather_node_def_set_required_skill (vein, 150);

    /* Profession unknown */
    g_assert_false (lrg_profession_state_can_gather (state, vein, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);

    g_assert_true (lrg_profession_state_learn (state, mining, 60, NULL));

    /* Skill 149 < 150 */
    lrg_profession_state_set_skill (state, "mining", 149, 225);
    g_assert_false (lrg_profession_state_can_gather (state, vein, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);

    /* Skill 150 == 150 */
    lrg_profession_state_set_skill (state, "mining", 150, 225);
    g_assert_true (lrg_profession_state_can_gather (state, vein, NULL));

    g_assert_false (lrg_profession_state_can_gather (state, herb, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);

    g_assert_false (lrg_profession_state_can_gather (state, orphan, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    /* Tool: ignored by can_gather, enforced by can_gather_with */
    lrg_gather_node_def_set_required_tool (vein, "mining_pick");
    g_assert_true (lrg_profession_state_can_gather (state, vein, NULL));
    g_assert_true (lrg_profession_state_can_gather_with (state, vein, NULL, NULL, NULL));
    g_assert_false (lrg_profession_state_can_gather_with (state, vein, table_count, items, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);
    put_items (items, "mining_pick", 1);
    g_assert_true (lrg_profession_state_can_gather_with (state, vein, table_count, items, NULL));
}

/* ========================================================================== */
/*                     LrgProfessionState: persistence                        */
/* ========================================================================== */

typedef struct
{
    GHashTable *professions;  /* utf8 -> LrgProfessionDef */
    GHashTable *recipes;      /* utf8 -> LrgRecipeDef */
} Registry;

static void
registry_setup (Registry      *registry,
                gconstpointer  user_data)
{
    const gchar *recipe_ids[][2] = {
        { "sword", "smithing" }, { "axe", "smithing" },
        { "stew", "cooking" }, { "smelt", "mining" },
    };
    guint i;

    registry->professions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
    registry->recipes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

    g_hash_table_insert (registry->professions, g_strdup ("smithing"),
                         make_profession ("smithing", LRG_PROFESSION_KIND_PRIMARY));
    g_hash_table_insert (registry->professions, g_strdup ("mining"),
                         make_profession ("mining", LRG_PROFESSION_KIND_PRIMARY));
    g_hash_table_insert (registry->professions, g_strdup ("herbalism"),
                         make_profession ("herbalism", LRG_PROFESSION_KIND_PRIMARY));
    g_hash_table_insert (registry->professions, g_strdup ("cooking"),
                         make_profession ("cooking", LRG_PROFESSION_KIND_SECONDARY));
    g_hash_table_insert (registry->professions, g_strdup ("fishing"),
                         make_profession ("fishing", LRG_PROFESSION_KIND_SECONDARY));

    for (i = 0; i < G_N_ELEMENTS (recipe_ids); i++)
        g_hash_table_insert (registry->recipes, g_strdup (recipe_ids[i][0]),
                             lrg_recipe_def_new (recipe_ids[i][0], recipe_ids[i][1]));
}

static void
registry_teardown (Registry      *registry,
                   gconstpointer  user_data)
{
    g_clear_pointer (&registry->professions, g_hash_table_unref);
    g_clear_pointer (&registry->recipes, g_hash_table_unref);
}

static LrgProfessionDef *
reg_prof (Registry    *registry,
          const gchar *id)
{
    return g_hash_table_lookup (registry->professions, id);
}

static LrgRecipeDef *
reg_recipe (Registry    *registry,
            const gchar *id)
{
    return g_hash_table_lookup (registry->recipes, id);
}

/* smithing 120/150, cooking 40/75, recipes axe, stew, sword */
static LrgProfessionState *
build_state (Registry *registry)
{
    LrgProfessionState *state = lrg_profession_state_new (2);

    g_assert_true (lrg_profession_state_learn (state, reg_prof (registry, "smithing"), 60, NULL));
    g_assert_true (lrg_profession_state_learn (state, reg_prof (registry, "cooking"), 60, NULL));
    lrg_profession_state_set_skill (state, "smithing", 60, 75);
    g_assert_true (lrg_profession_state_train (state, reg_prof (registry, "smithing"), 60, NULL));
    lrg_profession_state_set_skill (state, "smithing", 120, 150);
    lrg_profession_state_set_skill (state, "cooking", 40, 75);
    g_assert_true (lrg_profession_state_learn_recipe (state, reg_recipe (registry, "sword"), NULL));
    g_assert_true (lrg_profession_state_learn_recipe (state, reg_recipe (registry, "axe"), NULL));
    g_assert_true (lrg_profession_state_learn_recipe (state, reg_recipe (registry, "stew"), NULL));
    return state;
}

static void
test_variant_round_trip (Registry      *registry,
                         gconstpointer  user_data)
{
    g_autoptr(LrgProfessionState) state = build_state (registry);
    g_autoptr(LrgProfessionState) restored = NULL;
    g_autoptr(LrgProfessionState) restored_full = NULL;
    g_autoptr(GVariant) first = NULL;
    g_autoptr(GVariant) second = NULL;
    g_autoptr(GVariant) third = NULL;
    g_autoptr(GVariant) expected = NULL;
    g_autoptr(GPtrArray) recipes = NULL;
    GError *error = NULL;

    first = lrg_profession_state_to_variant (state);
    g_assert_false (g_variant_is_floating (first));
    g_assert_cmpstr (g_variant_get_type_string (first), ==, LRG_PROFESSION_STATE_VARIANT_TYPE);

    /* Exact, sorted encoding */
    expected = g_variant_ref_sink (g_variant_new_parsed (
        "(uint32 2, [('cooking', uint32 40, uint32 75), ('smithing', 120, 150)],"
        " ['axe', 'stew', 'sword'])"));
    g_assert_true (g_variant_equal (first, expected));

    restored = lrg_profession_state_new_from_variant (first, registry->recipes, &error);
    g_assert_no_error (error);
    g_assert_nonnull (restored);
    second = lrg_profession_state_to_variant (restored);
    g_assert_true (g_variant_equal (first, second));

    restored_full = lrg_profession_state_new_from_variant_full (first, registry->professions,
                                                                registry->recipes, &error);
    g_assert_no_error (error);
    third = lrg_profession_state_to_variant (restored_full);
    g_assert_true (g_variant_equal (first, third));

    /* Restored behaviour matches */
    g_assert_cmpuint (lrg_profession_state_get_skill (restored, "smithing"), ==, 120);
    g_assert_cmpuint (lrg_profession_state_get_max_skill (restored, "smithing"), ==, 150);
    recipes = lrg_profession_state_get_recipes (restored, "smithing");
    g_assert_cmpuint (recipes->len, ==, 2);
    g_assert_cmpuint (lrg_profession_state_get_max_primary (restored), ==, 2);
}

static void
test_variant_round_trip_edges (Registry      *registry,
                               gconstpointer  user_data)
{
    g_autoptr(LrgProfessionState) empty = lrg_profession_state_new (0);
    g_autoptr(LrgProfessionState) restored = NULL;
    g_autoptr(LrgProfessionState) edge = lrg_profession_state_new (64);
    g_autoptr(LrgProfessionState) edge_restored = NULL;
    g_autoptr(GVariant) a = NULL;
    g_autoptr(GVariant) b = NULL;
    g_autoptr(GVariant) c = NULL;
    g_autoptr(GVariant) d = NULL;
    g_autofree gchar *max_id = g_strnfill (128, 'q');

    /* Empty state, NULL recipe table is fine without recipes */
    a = lrg_profession_state_to_variant (empty);
    restored = lrg_profession_state_new_from_variant (a, NULL, NULL);
    g_assert_nonnull (restored);
    b = lrg_profession_state_to_variant (restored);
    g_assert_true (g_variant_equal (a, b));

    /* Skill 0, the skill limit, 128-byte and non-ASCII ids */
    lrg_profession_state_set_skill (edge, "zero", 0, 1);
    lrg_profession_state_set_skill (edge, "top", LRG_PROFESSION_SKILL_LIMIT, LRG_PROFESSION_SKILL_LIMIT);
    lrg_profession_state_set_skill (edge, max_id, 5, 10);
    lrg_profession_state_set_skill (edge, "schmieden-\xc3\xa4", 1, 2);
    c = lrg_profession_state_to_variant (edge);
    edge_restored = lrg_profession_state_new_from_variant (c, NULL, NULL);
    g_assert_nonnull (edge_restored);
    d = lrg_profession_state_to_variant (edge_restored);
    g_assert_true (g_variant_equal (c, d));
}

static void
test_variant_resolves_kinds (Registry      *registry,
                             gconstpointer  user_data)
{
    g_autoptr(LrgProfessionState) state = lrg_profession_state_new (2);
    g_autoptr(LrgProfessionState) plain = NULL;
    g_autoptr(LrgProfessionState) full = NULL;
    g_autoptr(GVariant) v = NULL;
    GError *error = NULL;

    /* One primary plus two secondaries */
    g_assert_true (lrg_profession_state_learn (state, reg_prof (registry, "smithing"), 60, NULL));
    g_assert_true (lrg_profession_state_learn (state, reg_prof (registry, "cooking"), 60, NULL));
    g_assert_true (lrg_profession_state_learn (state, reg_prof (registry, "fishing"), 60, NULL));
    g_assert_cmpuint (lrg_profession_state_get_primary_count (state, NULL), ==, 1);
    v = lrg_profession_state_to_variant (state);

    /* Without definitions every kind is unknown: counts fail closed */
    plain = lrg_profession_state_new_from_variant (v, NULL, NULL);
    g_assert_cmpuint (lrg_profession_state_get_primary_count (plain, NULL), ==, 3);
    g_assert_cmpuint (lrg_profession_state_get_primary_count (plain, registry->professions), ==, 1);
    g_assert_false (lrg_profession_state_learn (plain, reg_prof (registry, "mining"), 60, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT);
    g_clear_error (&error);

    /* With definitions the kinds are exact */
    full = lrg_profession_state_new_from_variant_full (v, registry->professions, NULL, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_profession_state_get_primary_count (full, NULL), ==, 1);
    g_assert_true (lrg_profession_state_learn (full, reg_prof (registry, "mining"), 60, NULL));
    g_assert_false (lrg_profession_state_learn (full, reg_prof (registry, "herbalism"), 60, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT);
    g_clear_error (&error);
}

/* Builds a (ua(suu)as) from simple arrays */
static GVariant *
build_variant (guint32       max_primary,
               const gchar **prof_ids,
               const guint  *skills,
               const guint  *maxes,
               guint         n_prof,
               const gchar **recipe_ids,
               guint         n_recipes)
{
    GVariantBuilder professions;
    GVariantBuilder recipes;
    guint i;

    g_variant_builder_init (&professions, G_VARIANT_TYPE ("a(suu)"));
    for (i = 0; i < n_prof; i++)
        g_variant_builder_add (&professions, "(suu)", prof_ids[i], skills[i], maxes[i]);
    g_variant_builder_init (&recipes, G_VARIANT_TYPE ("as"));
    for (i = 0; i < n_recipes; i++)
        g_variant_builder_add (&recipes, "s", recipe_ids[i]);
    return g_variant_new ("(ua(suu)as)", max_primary, &professions, &recipes);
}

/*
 * patch_bytes:
 * Serialises @variant (consuming a floating ref), overwrites byte @at of
 * the first occurrence of @needle with @byte and rebuilds an untrusted
 * variant of the same type over the raw bytes.
 */
static GVariant *
patch_bytes (GVariant    *variant,
             const gchar *needle,
             gsize        at,
             guchar       byte)
{
    g_autoptr(GVariant) owned = g_variant_ref_sink (variant);
    gsize size = g_variant_get_size (owned);
    guchar *data = g_malloc (size);
    guchar *hit = NULL;
    gsize needle_len = strlen (needle);
    gsize i;
    GBytes *bytes;
    GVariant *out;

    g_variant_store (owned, data);
    for (i = 0; i + needle_len <= size && hit == NULL; i++)
    {
        if (memcmp (data + i, needle, needle_len) == 0)
            hit = data + i;
    }
    g_assert_nonnull (hit);
    hit[at] = byte;
    bytes = g_bytes_new_take (data, size);
    out = g_variant_new_from_bytes (g_variant_get_type (owned), bytes, FALSE);
    g_bytes_unref (bytes);
    return out;
}

static void
test_variant_hostile (Registry      *registry,
                      gconstpointer  user_data)
{
    const gchar *one_prof[] = { "smithing" };
    const gchar *two_prof[] = { "smithing", "smithing" };
    const gchar *mixed[] = { "smithing", "cooking", "mining" };
    const guint ones[] = { 1, 1, 1 };
    const guint seventy_five[] = { 75, 75, 75 };
    const gchar *recipe_sword[] = { "sword" };
    const gchar *recipe_dup[] = { "sword", "sword" };
    const gchar *recipe_stew[] = { "stew" };
    const gchar *recipe_unknown[] = { "nonexistent" };
    const gchar *empty_id[] = { "" };
    g_autofree gchar *long_str = g_strnfill (129, 'x');
    const gchar *long_id[] = { long_str };
    guint skill;
    guint max_skill;
    guint i;
    GVariantBuilder builder;

    /* Wrong type strings */
    assert_restore_fails (g_variant_new ("(ua(sus)as)", 2, NULL, NULL), NULL, NULL,
                          LRG_PROGRESSION_ERROR_INVALID);
    assert_restore_fails (g_variant_new ("(uas)", 2, NULL), NULL, NULL,
                          LRG_PROGRESSION_ERROR_INVALID);
    assert_restore_fails (g_variant_new_string ("nope"), NULL, NULL,
                          LRG_PROGRESSION_ERROR_INVALID);
    assert_restore_fails (g_variant_new ("(ia(suu)as)", 2, NULL, NULL), NULL, NULL,
                          LRG_PROGRESSION_ERROR_INVALID);

    /* Not in normal form: an embedded NUL inside a recipe id */
    assert_restore_fails (patch_bytes (build_variant (2, one_prof, ones, seventy_five, 1, recipe_sword, 1),
                                       "sword", 2, '\0'),
                          NULL, registry->recipes, LRG_PROGRESSION_ERROR_INVALID);
    /* Invalid UTF-8 inside a profession id */
    assert_restore_fails (patch_bytes (build_variant (2, one_prof, ones, seventy_five, 1, NULL, 0),
                                       "smithing", 1, 0xff),
                          NULL, NULL, LRG_PROGRESSION_ERROR_INVALID);

    /* max-primary out of range */
    assert_restore_fails (build_variant (LRG_PROFESSION_STATE_MAX_PROFESSIONS + 1, NULL, NULL, NULL, 0, NULL, 0),
                          NULL, NULL, LRG_PROGRESSION_ERROR_INVALID);
    assert_restore_fails (build_variant (G_MAXUINT32, NULL, NULL, NULL, 0, NULL, 0),
                          NULL, NULL, LRG_PROGRESSION_ERROR_INVALID);

    /* Oversized profession array */
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(suu)"));
    for (i = 0; i <= LRG_PROFESSION_STATE_MAX_PROFESSIONS; i++)
    {
        g_autofree gchar *id = g_strdup_printf ("p%u", i);
        g_variant_builder_add (&builder, "(suu)", id, 1u, 75u);
    }
    assert_restore_fails (g_variant_new ("(ua(suu)as)", 2u, &builder, NULL), NULL, NULL,
                          LRG_PROGRESSION_ERROR_INVALID);

    /* Oversized recipe array (rejected before lookups) */
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));
    for (i = 0; i <= LRG_PROFESSION_STATE_MAX_RECIPES; i++)
    {
        g_autofree gchar *id = g_strdup_printf ("r%u", i);
        g_variant_builder_add (&builder, "s", id);
    }
    assert_restore_fails (g_variant_new ("(ua(suu)as)", 2u, NULL, &builder), NULL, registry->recipes,
                          LRG_PROGRESSION_ERROR_INVALID);

    /* Empty and overlong profession ids */
    assert_restore_fails (build_variant (2, empty_id, ones, seventy_five, 1, NULL, 0),
                          NULL, NULL, LRG_PROGRESSION_ERROR_INVALID);
    assert_restore_fails (build_variant (2, long_id, ones, seventy_five, 1, NULL, 0),
                          NULL, NULL, LRG_PROGRESSION_ERROR_INVALID);

    /* Duplicate professions and recipes */
    assert_restore_fails (build_variant (2, two_prof, ones, seventy_five, 2, NULL, 0),
                          NULL, NULL, LRG_PROGRESSION_ERROR_INVALID);
    assert_restore_fails (build_variant (2, one_prof, ones, seventy_five, 1, recipe_dup, 2),
                          NULL, registry->recipes, LRG_PROGRESSION_ERROR_INVALID);

    /* Empty and overlong recipe ids */
    assert_restore_fails (build_variant (2, one_prof, ones, seventy_five, 1, empty_id, 1),
                          NULL, registry->recipes, LRG_PROGRESSION_ERROR_INVALID);
    assert_restore_fails (build_variant (2, one_prof, ones, seventy_five, 1, long_id, 1),
                          NULL, registry->recipes, LRG_PROGRESSION_ERROR_INVALID);

    /* skill > max, max 0, max beyond the global limit */
    skill = 76; max_skill = 75;
    assert_restore_fails (build_variant (2, one_prof, &skill, &max_skill, 1, NULL, 0),
                          NULL, NULL, LRG_PROGRESSION_ERROR_INVALID);
    skill = 0; max_skill = 0;
    assert_restore_fails (build_variant (2, one_prof, &skill, &max_skill, 1, NULL, 0),
                          NULL, NULL, LRG_PROGRESSION_ERROR_INVALID);
    skill = 1; max_skill = LRG_PROFESSION_SKILL_LIMIT + 1;
    assert_restore_fails (build_variant (2, one_prof, &skill, &max_skill, 1, NULL, 0),
                          NULL, NULL, LRG_PROGRESSION_ERROR_INVALID);

    /* Recipes: unknown id, no table, profession not in snapshot, wrong object type */
    assert_restore_fails (build_variant (2, one_prof, ones, seventy_five, 1, recipe_unknown, 1),
                          NULL, registry->recipes, LRG_PROGRESSION_ERROR_NOT_FOUND);
    assert_restore_fails (build_variant (2, one_prof, ones, seventy_five, 1, recipe_sword, 1),
                          NULL, NULL, LRG_PROGRESSION_ERROR_NOT_FOUND);
    assert_restore_fails (build_variant (2, one_prof, ones, seventy_five, 1, recipe_stew, 1),
                          NULL, registry->recipes, LRG_PROGRESSION_ERROR_INVALID);
    assert_restore_fails (build_variant (2, one_prof, ones, seventy_five, 1, recipe_sword, 1),
                          NULL, registry->professions, LRG_PROGRESSION_ERROR_NOT_FOUND);

    /* A recipe table entry that is not an LrgRecipeDef */
    {
        const gchar *recipe_smithing[] = { "smithing" };
        assert_restore_fails (build_variant (2, one_prof, ones, seventy_five, 1, recipe_smithing, 1),
                              NULL, registry->professions, LRG_PROGRESSION_ERROR_NOT_FOUND);
    }

    /* With definitions: unknown profession, max beyond the def, too many primaries */
    {
        const gchar *unknown[] = { "necromancy" };
        assert_restore_fails (build_variant (2, unknown, ones, seventy_five, 1, NULL, 0),
                              registry->professions, NULL, LRG_PROGRESSION_ERROR_NOT_FOUND);
    }
    skill = 1; max_skill = 301;
    assert_restore_fails (build_variant (2, one_prof, &skill, &max_skill, 1, NULL, 0),
                          registry->professions, NULL, LRG_PROGRESSION_ERROR_INVALID);
    assert_restore_fails (build_variant (1, mixed, ones, seventy_five, 3, NULL, 0),
                          registry->professions, NULL, LRG_PROGRESSION_ERROR_INVALID);
    /* Wrong object type in the profession table */
    {
        const gchar *sword_as_prof[] = { "sword" };
        assert_restore_fails (build_variant (2, sword_as_prof, ones, seventy_five, 1, NULL, 0),
                              registry->recipes, NULL, LRG_PROGRESSION_ERROR_NOT_FOUND);
    }

    /* The same snapshots are fine without the def-level checks */
    {
        g_autoptr(GVariant) v = g_variant_ref_sink (build_variant (1, mixed, ones, seventy_five, 3, NULL, 0));
        g_autoptr(LrgProfessionState) ok = lrg_profession_state_new_from_variant (v, NULL, NULL);
        g_assert_nonnull (ok);
    }
    {
        g_autoptr(GVariant) v = g_variant_ref_sink (build_variant (2, mixed, ones, seventy_five, 3, NULL, 0));
        g_autoptr(LrgProfessionState) ok = lrg_profession_state_new_from_variant_full (v, registry->professions, NULL, NULL);
        g_assert_nonnull (ok);
    }
}

/* ========================================================================== */
/*                                   Main                                     */
/* ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Skill band */
    g_test_add_func ("/profession/band/boxed", test_band_boxed);
    g_test_add_func ("/profession/band/validity", test_band_validity);
    g_test_add_func ("/profession/band/difficulty-thresholds", test_band_difficulty_thresholds);
    g_test_add_func ("/profession/band/chance-exact", test_band_chance_exact);
    g_test_add_func ("/profession/band/collapsed", test_band_collapsed);
    g_test_add_func ("/profession/band/invalid", test_band_invalid);
    g_test_add_func ("/profession/band/default", test_band_default);

    /* Definitions */
    g_test_add_func ("/profession/tier/boxed", test_tier_boxed);
    g_test_add_func ("/profession/def/properties", test_def_properties);
    g_test_add_func ("/profession/def/tiers", test_def_tiers);
    g_test_add_func ("/profession/def/tiers-reject", test_def_tiers_reject);
    g_test_add_func ("/profession/recipe/item", test_recipe_item);
    g_test_add_func ("/profession/recipe/item-reject", test_recipe_item_reject);
    g_test_add_func ("/profession/recipe/properties", test_recipe_properties);
    g_test_add_func ("/profession/recipe/band", test_recipe_band);
    g_test_add_func ("/profession/recipe/items", test_recipe_items);
    g_test_add_func ("/profession/gather-node/all", test_gather_node);

    /* State: professions */
    g_test_add_func ("/profession/state/properties", test_state_properties);
    g_test_add_func ("/profession/state/learn", test_state_learn);
    g_test_add_func ("/profession/state/learn-rejections", test_state_learn_rejections);
    g_test_add_func ("/profession/state/primary-limit", test_state_primary_limit);
    g_test_add_func ("/profession/state/profession-cap", test_state_profession_cap);
    g_test_add_func ("/profession/state/unlearn", test_state_unlearn);
    g_test_add_func ("/profession/state/train", test_state_train);
    g_test_add_func ("/profession/state/train-rejections", test_state_train_rejections);

    /* State: recipes and skill */
    g_test_add_func ("/profession/state/recipes", test_state_recipes);
    g_test_add_func ("/profession/state/recipe-rejections", test_state_recipe_rejections);
    g_test_add_func ("/profession/state/recipe-cap", test_state_recipe_cap);
    g_test_add_func ("/profession/state/starter-recipes", test_state_starter_recipes);
    g_test_add_func ("/profession/state/skill-up", test_state_skill_up);
    g_test_add_func ("/profession/state/set-skill", test_state_set_skill);

    /* State: crafting and gathering */
    g_test_add ("/profession/craft/callback", CraftFixture, NULL,
                craft_fixture_setup, test_craft_callback, craft_fixture_teardown);
    g_test_add ("/profession/craft/overflow", CraftFixture, NULL,
                craft_fixture_setup, test_craft_overflow, craft_fixture_teardown);
    g_test_add ("/profession/craft/rejections", CraftFixture, NULL,
                craft_fixture_setup, test_craft_rejections, craft_fixture_teardown);
    g_test_add ("/profession/craft/inventory", CraftFixture, NULL,
                craft_fixture_setup, test_craft_inventory, craft_fixture_teardown);
    g_test_add_func ("/profession/state/gather", test_state_gather);

    /* State: persistence */
    g_test_add ("/profession/variant/round-trip", Registry, NULL,
                registry_setup, test_variant_round_trip, registry_teardown);
    g_test_add ("/profession/variant/round-trip-edges", Registry, NULL,
                registry_setup, test_variant_round_trip_edges, registry_teardown);
    g_test_add ("/profession/variant/resolves-kinds", Registry, NULL,
                registry_setup, test_variant_resolves_kinds, registry_teardown);
    g_test_add ("/profession/variant/hostile", Registry, NULL,
                registry_setup, test_variant_hostile, registry_teardown);

    return g_test_run ();
}
