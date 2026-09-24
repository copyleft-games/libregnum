/* test-cooldown-aura.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgCooldownSet, LrgAura, LrgAuraSet and LrgVital.
 */

#include <glib.h>
#include <glib-object.h>
#include <math.h>
#include <string.h>

#include "lrg-enums.h"
#include "progression/lrg-ability-def.h"
#include "progression/lrg-cooldown-set.h"
#include "progression/lrg-aura.h"
#include "progression/lrg-aura-set.h"
#include "progression/lrg-vital.h"

/* ========================================================================== */
/*                                  Helpers                                   */
/* ========================================================================== */

static void
expect_critical (void)
{
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*assertion*failed*");
}

/* Copy of @v with the first byte of @needle replaced by 0xFF (bad UTF-8). */
static GVariant *
corrupt_string (GVariant    *v,
                const gchar *needle)
{
    gsize size;
    gsize nlen;
    gsize i;
    guint8 *data;
    GBytes *bytes;
    GVariant *result;
    gboolean found;

    size = g_variant_get_size (v);
    nlen = strlen (needle);
    data = g_memdup2 (g_variant_get_data (v), size);
    found = FALSE;
    for (i = 0; i + nlen <= size; i++)
    {
        if (memcmp (data + i, needle, nlen) == 0)
        {
            data[i] = 0xFF;
            found = TRUE;
            break;
        }
    }
    g_assert_true (found);
    bytes = g_bytes_new_take (data, size);
    result = g_variant_new_from_bytes (g_variant_get_type (v), bytes, FALSE);
    g_bytes_unref (bytes);
    return g_variant_ref_sink (result);
}

static gchar *
make_long_id (gsize len)
{
    gchar *id = g_malloc (len + 1);

    memset (id, 'a', len);
    id[len] = '\0';
    return id;
}

/* ========================================================================== */
/*                               LrgCooldownSet                               */
/* ========================================================================== */

static void
test_cooldown_defaults_properties (void)
{
    g_autoptr(LrgCooldownSet) set = lrg_cooldown_set_new ();
    gdouble duration;

    g_assert_cmpfloat (lrg_cooldown_set_get_global_duration (set), ==, 1.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_global_remaining (set), ==, 0.0);
    g_assert_cmpuint (lrg_cooldown_set_get_count (set), ==, 0);
    g_assert_true (lrg_cooldown_set_is_ready (set, "anything", "cat", TRUE));

    g_object_set (set, "global-duration", 1.5, NULL);
    g_object_get (set, "global-duration", &duration, NULL);
    g_assert_cmpfloat (duration, ==, 1.5);
    lrg_cooldown_set_set_global_duration (set, 0.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_global_duration (set), ==, 0.0);

    expect_critical ();
    lrg_cooldown_set_set_global_duration (set, NAN);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_cooldown_set_set_global_duration (set, -1.0);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_cooldown_set_set_global_duration (set, LRG_COOLDOWN_SET_MAX_SECONDS + 1.0);
    g_test_assert_expected_messages ();
    g_assert_cmpfloat (lrg_cooldown_set_get_global_duration (set), ==, 0.0);
}

static void
test_cooldown_start_tick (void)
{
    g_autoptr(LrgCooldownSet) set = lrg_cooldown_set_new ();

    lrg_cooldown_set_start (set, "fireball", 5.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_remaining (set, "fireball"), ==, 5.0);
    g_assert_cmpuint (lrg_cooldown_set_get_count (set), ==, 1);
    g_assert_false (lrg_cooldown_set_is_ready (set, "fireball", NULL, FALSE));
    g_assert_true (lrg_cooldown_set_is_ready (set, "frostbolt", NULL, FALSE));
    g_assert_cmpfloat (lrg_cooldown_set_get_remaining (set, "frostbolt"), ==, 0.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_remaining (set, NULL), ==, 0.0);

    lrg_cooldown_set_tick (set, 2.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_remaining (set, "fireball"), ==, 3.0);

    /* exactly finishing removes the entry */
    lrg_cooldown_set_tick (set, 3.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_remaining (set, "fireball"), ==, 0.0);
    g_assert_cmpuint (lrg_cooldown_set_get_count (set), ==, 0);
    g_assert_true (lrg_cooldown_set_is_ready (set, "fireball", NULL, FALSE));

    /* overshooting also removes it */
    lrg_cooldown_set_start (set, "fireball", 1.0);
    lrg_cooldown_set_tick (set, 100.0);
    g_assert_cmpuint (lrg_cooldown_set_get_count (set), ==, 0);

    /* restarting replaces the remaining time, even with a shorter one */
    lrg_cooldown_set_start (set, "blink", 15.0);
    lrg_cooldown_set_start (set, "blink", 4.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_remaining (set, "blink"), ==, 4.0);

    /* zero or negative clears; oversized clamps to a day */
    lrg_cooldown_set_start (set, "blink", 0.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_remaining (set, "blink"), ==, 0.0);
    lrg_cooldown_set_start (set, "blink", 3.0);
    lrg_cooldown_set_start (set, "blink", -3.0);
    g_assert_cmpuint (lrg_cooldown_set_get_count (set), ==, 0);
    lrg_cooldown_set_start (set, "hearth", 1e9);
    g_assert_cmpfloat (lrg_cooldown_set_get_remaining (set, "hearth"), ==, LRG_COOLDOWN_SET_MAX_SECONDS);
}

static void
test_cooldown_tick_ignores_bad_delta (void)
{
    g_autoptr(LrgCooldownSet) set = lrg_cooldown_set_new ();

    lrg_cooldown_set_start (set, "a", 5.0);
    lrg_cooldown_set_start_category (set, "c", 5.0);
    lrg_cooldown_set_start_global (set, 5.0);

    lrg_cooldown_set_tick (set, NAN);
    lrg_cooldown_set_tick (set, INFINITY);
    lrg_cooldown_set_tick (set, -INFINITY);
    lrg_cooldown_set_tick (set, -1.0);
    lrg_cooldown_set_tick (set, 0.0);

    g_assert_cmpfloat (lrg_cooldown_set_get_remaining (set, "a"), ==, 5.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_category_remaining (set, "c"), ==, 5.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_global_remaining (set), ==, 5.0);
}

static void
test_cooldown_category_and_global (void)
{
    g_autoptr(LrgCooldownSet) set = lrg_cooldown_set_new ();

    lrg_cooldown_set_start_category (set, "potion", 60.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_category_remaining (set, "potion"), ==, 60.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_category_remaining (set, NULL), ==, 0.0);
    g_assert_false (lrg_cooldown_set_is_ready (set, "healing_potion", "potion", FALSE));
    g_assert_true (lrg_cooldown_set_is_ready (set, "healing_potion", NULL, FALSE));
    g_assert_true (lrg_cooldown_set_is_ready (set, "bandage", "first_aid", FALSE));
    /* category cooldowns do not appear as key cooldowns */
    g_assert_cmpuint (lrg_cooldown_set_get_count (set), ==, 0);

    lrg_cooldown_set_start_global (set, 1.5);
    g_assert_cmpfloat (lrg_cooldown_set_get_global_remaining (set), ==, 1.5);
    g_assert_false (lrg_cooldown_set_is_ready (set, "strike", NULL, TRUE));
    g_assert_true (lrg_cooldown_set_is_ready (set, "strike", NULL, FALSE));
    g_assert_true (lrg_cooldown_set_is_ready (set, NULL, NULL, FALSE));
    g_assert_false (lrg_cooldown_set_is_ready (set, NULL, NULL, TRUE));

    lrg_cooldown_set_tick (set, 1.5);
    g_assert_cmpfloat (lrg_cooldown_set_get_global_remaining (set), ==, 0.0);
    g_assert_true (lrg_cooldown_set_is_ready (set, "strike", NULL, TRUE));
    g_assert_cmpfloat (lrg_cooldown_set_get_category_remaining (set, "potion"), ==, 58.5);

    /* global clamps into range; negative becomes 0 */
    lrg_cooldown_set_start_global (set, -2.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_global_remaining (set), ==, 0.0);
    lrg_cooldown_set_start_global (set, 1e12);
    g_assert_cmpfloat (lrg_cooldown_set_get_global_remaining (set), ==, LRG_COOLDOWN_SET_MAX_SECONDS);
    lrg_cooldown_set_start_category (set, "potion", 0.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_category_remaining (set, "potion"), ==, 0.0);
}

static void
test_cooldown_reset_clear (void)
{
    g_autoptr(LrgCooldownSet) set = lrg_cooldown_set_new ();

    lrg_cooldown_set_start (set, "a", 5.0);
    lrg_cooldown_set_start (set, "b", 5.0);
    lrg_cooldown_set_start_category (set, "c", 5.0);
    lrg_cooldown_set_start_global (set, 1.0);

    lrg_cooldown_set_reset (set, "a");
    lrg_cooldown_set_reset (set, "missing");
    lrg_cooldown_set_reset (set, NULL);
    g_assert_cmpfloat (lrg_cooldown_set_get_remaining (set, "a"), ==, 0.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_remaining (set, "b"), ==, 5.0);

    lrg_cooldown_set_clear (set);
    g_assert_cmpuint (lrg_cooldown_set_get_count (set), ==, 0);
    g_assert_cmpfloat (lrg_cooldown_set_get_category_remaining (set, "c"), ==, 0.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_global_remaining (set), ==, 0.0);
    g_assert_true (lrg_cooldown_set_is_ready (set, "b", "c", TRUE));
}

static void
test_cooldown_start_rejections (void)
{
    g_autoptr(LrgCooldownSet) set = lrg_cooldown_set_new ();
    g_autofree gchar *long_key = make_long_id (129);

    expect_critical ();
    lrg_cooldown_set_start (set, NULL, 1.0);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_cooldown_set_start (set, "", 1.0);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_cooldown_set_start (set, long_key, 1.0);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_cooldown_set_start (set, "a", NAN);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_cooldown_set_start_category (set, "", 1.0);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_cooldown_set_start_category (set, "c", INFINITY);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_cooldown_set_start_global (set, NAN);
    g_test_assert_expected_messages ();

    g_assert_cmpuint (lrg_cooldown_set_get_count (set), ==, 0);
    g_assert_cmpfloat (lrg_cooldown_set_get_global_remaining (set), ==, 0.0);
}

static void
test_cooldown_eviction (void)
{
    g_autoptr(LrgCooldownSet) set = lrg_cooldown_set_new ();
    guint i;

    for (i = 0; i < LRG_COOLDOWN_SET_MAX_ENTRIES; i++)
    {
        g_autofree gchar *key = g_strdup_printf ("k%04u", i);

        /* k0000 has the least time left */
        lrg_cooldown_set_start (set, key, 10.0 + (gdouble) i);
    }
    g_assert_cmpuint (lrg_cooldown_set_get_count (set), ==, LRG_COOLDOWN_SET_MAX_ENTRIES);

    /* restarting an existing key never evicts */
    lrg_cooldown_set_start (set, "k0500", 1.0);
    g_assert_cmpuint (lrg_cooldown_set_get_count (set), ==, LRG_COOLDOWN_SET_MAX_ENTRIES);
    g_assert_cmpfloat (lrg_cooldown_set_get_remaining (set, "k0000"), ==, 10.0);

    /* a new key evicts the least remaining entry (now k0500) */
    lrg_cooldown_set_start (set, "new", 30.0);
    g_assert_cmpuint (lrg_cooldown_set_get_count (set), ==, LRG_COOLDOWN_SET_MAX_ENTRIES);
    g_assert_cmpfloat (lrg_cooldown_set_get_remaining (set, "new"), ==, 30.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_remaining (set, "k0500"), ==, 0.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_remaining (set, "k0000"), ==, 10.0);

    /* ties go to the smallest key */
    lrg_cooldown_set_start (set, "k0001", 10.0);
    lrg_cooldown_set_start (set, "another", 30.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_remaining (set, "k0000"), ==, 0.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_remaining (set, "k0001"), ==, 10.0);
}

static LrgAbilityDef *
make_ability (const gchar *id,
              gdouble      cooldown,
              const gchar *category,
              gdouble      category_cooldown,
              gboolean     gcd)
{
    return g_object_new (LRG_TYPE_ABILITY_DEF,
                         "id", id,
                         "cooldown", cooldown,
                         "category", category,
                         "category-cooldown", category_cooldown,
                         "triggers-gcd", gcd,
                         NULL);
}

static void
test_cooldown_abilities (void)
{
    g_autoptr(LrgCooldownSet) set = lrg_cooldown_set_new ();
    g_autoptr(LrgAbilityDef) shield_bash = make_ability ("shield_bash", 8.0, "interrupt", 2.0, TRUE);
    g_autoptr(LrgAbilityDef) pummel = make_ability ("pummel", 0.0, "interrupt", 2.0, FALSE);
    g_autoptr(LrgAbilityDef) strike = make_ability ("strike", 0.0, NULL, 0.0, TRUE);
    g_autoptr(LrgAbilityDef) trinket = make_ability ("trinket", 0.0, NULL, 0.0, FALSE);

    g_assert_true (lrg_cooldown_set_ability_ready (set, shield_bash));
    lrg_cooldown_set_start_ability (set, shield_bash);

    g_assert_cmpfloat (lrg_cooldown_set_get_remaining (set, "shield_bash"), ==, 8.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_category_remaining (set, "interrupt"), ==, 2.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_global_remaining (set), ==, 1.0);

    g_assert_false (lrg_cooldown_set_ability_ready (set, shield_bash));
    g_assert_false (lrg_cooldown_set_ability_ready (set, pummel));   /* category */
    g_assert_false (lrg_cooldown_set_ability_ready (set, strike));   /* GCD */
    g_assert_true (lrg_cooldown_set_ability_ready (set, trinket));   /* off GCD */

    lrg_cooldown_set_tick (set, 1.0);
    g_assert_true (lrg_cooldown_set_ability_ready (set, strike));
    g_assert_false (lrg_cooldown_set_ability_ready (set, pummel));

    lrg_cooldown_set_tick (set, 1.0);
    g_assert_true (lrg_cooldown_set_ability_ready (set, pummel));
    g_assert_false (lrg_cooldown_set_ability_ready (set, shield_bash));

    /* using an off-GCD ability with no cooldowns changes nothing */
    lrg_cooldown_set_start_ability (set, trinket);
    g_assert_cmpfloat (lrg_cooldown_set_get_global_remaining (set), ==, 0.0);
    g_assert_cmpuint (lrg_cooldown_set_get_count (set), ==, 1);

    /* the configured GCD length is honoured */
    lrg_cooldown_set_set_global_duration (set, 0.75);
    lrg_cooldown_set_start_ability (set, strike);
    g_assert_cmpfloat (lrg_cooldown_set_get_global_remaining (set), ==, 0.75);
    g_assert_cmpfloat (lrg_cooldown_set_get_remaining (set, "strike"), ==, 0.0);

    lrg_cooldown_set_tick (set, 6.0);
    g_assert_true (lrg_cooldown_set_ability_ready (set, shield_bash));
}

/* ------------------------------ persistence ------------------------------- */

static void
assert_cooldown_rejected (GVariant *variant)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgCooldownSet) set = NULL;

    set = lrg_cooldown_set_new_from_variant (variant, &error);
    g_assert_null (set);
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
}

static GVariant *
cooldown_variant_parsed (const gchar *text)
{
    return g_variant_ref_sink (g_variant_new_parsed (text));
}

static void
test_cooldown_variant_round_trip (void)
{
    g_autoptr(LrgCooldownSet) set = lrg_cooldown_set_new ();
    g_autoptr(LrgCooldownSet) restored = NULL;
    g_autoptr(GVariant) first = NULL;
    g_autoptr(GVariant) second = NULL;
    g_autoptr(GVariant) expected = NULL;
    g_autoptr(GError) error = NULL;

    lrg_cooldown_set_start (set, "zeta", 3.25);
    lrg_cooldown_set_start (set, "alpha", 12.0);
    lrg_cooldown_set_start_category (set, "potion", 60.0);
    lrg_cooldown_set_start_global (set, 0.5);

    first = lrg_cooldown_set_to_variant (set);
    g_assert_false (g_variant_is_floating (first));
    g_assert_cmpstr (g_variant_get_type_string (first), ==, LRG_COOLDOWN_SET_VARIANT_TYPE);
    expected = cooldown_variant_parsed ("(0.5, [('alpha', 12.0), ('zeta', 3.25)], [('potion', 60.0)])");
    g_assert_true (g_variant_equal (first, expected));

    restored = lrg_cooldown_set_new_from_variant (first, &error);
    g_assert_no_error (error);
    g_assert_nonnull (restored);
    g_assert_cmpfloat (lrg_cooldown_set_get_remaining (restored, "zeta"), ==, 3.25);
    g_assert_cmpfloat (lrg_cooldown_set_get_category_remaining (restored, "potion"), ==, 60.0);
    g_assert_cmpfloat (lrg_cooldown_set_get_global_remaining (restored), ==, 0.5);

    second = lrg_cooldown_set_to_variant (restored);
    g_assert_true (g_variant_equal (first, second));

    /* zero entries are accepted as already finished */
    {
        g_autoptr(GVariant) zeros = cooldown_variant_parsed ("(0.0, [('a', 0.0), ('b', 2.0)], [('c', 0.0)])");
        g_autoptr(LrgCooldownSet) z = lrg_cooldown_set_new_from_variant (zeros, NULL);

        g_assert_nonnull (z);
        g_assert_cmpuint (lrg_cooldown_set_get_count (z), ==, 1);
        g_assert_cmpfloat (lrg_cooldown_set_get_remaining (z, "b"), ==, 2.0);
        g_assert_cmpfloat (lrg_cooldown_set_get_category_remaining (z, "c"), ==, 0.0);
    }

    /* boundary: exactly one day is accepted */
    {
        g_autoptr(GVariant) day = cooldown_variant_parsed ("(86400.0, [('a', 86400.0)], @a(sd) [])");
        g_autoptr(LrgCooldownSet) d = lrg_cooldown_set_new_from_variant (day, NULL);

        g_assert_nonnull (d);
    }
}

static GVariant *
cooldown_variant_many (guint n_keys,
                       guint n_categories)
{
    GVariantBuilder keys;
    GVariantBuilder cats;
    guint i;

    g_variant_builder_init (&keys, G_VARIANT_TYPE ("a(sd)"));
    for (i = 0; i < n_keys; i++)
    {
        g_autofree gchar *k = g_strdup_printf ("k%05u", i);

        g_variant_builder_add (&keys, "(sd)", k, 1.0);
    }
    g_variant_builder_init (&cats, G_VARIANT_TYPE ("a(sd)"));
    for (i = 0; i < n_categories; i++)
    {
        g_autofree gchar *c = g_strdup_printf ("c%05u", i);

        g_variant_builder_add (&cats, "(sd)", c, 1.0);
    }
    return g_variant_ref_sink (g_variant_new ("(da(sd)a(sd))", 0.0, &keys, &cats));
}

static void
test_cooldown_variant_hostile (void)
{
    g_autofree gchar *long_key = make_long_id (129);
    g_autoptr(GVariant) v = NULL;

    /* wrong type */
    v = cooldown_variant_parsed ("(0.0, @a(sd) [])");
    assert_cooldown_rejected (v);
    g_clear_pointer (&v, g_variant_unref);
    v = cooldown_variant_parsed ("(0.0, [('a', 1)], @a(sd) [])");   /* a(si) */
    assert_cooldown_rejected (v);
    g_clear_pointer (&v, g_variant_unref);

    /* non-finite and out-of-range global */
    v = g_variant_ref_sink (g_variant_new ("(d@a(sd)@a(sd))", NAN,
                                           g_variant_new_array (G_VARIANT_TYPE ("(sd)"), NULL, 0),
                                           g_variant_new_array (G_VARIANT_TYPE ("(sd)"), NULL, 0)));
    assert_cooldown_rejected (v);
    g_clear_pointer (&v, g_variant_unref);
    v = cooldown_variant_parsed ("(-1.0, @a(sd) [], @a(sd) [])");
    assert_cooldown_rejected (v);
    g_clear_pointer (&v, g_variant_unref);
    v = cooldown_variant_parsed ("(86400.5, @a(sd) [], @a(sd) [])");
    assert_cooldown_rejected (v);
    g_clear_pointer (&v, g_variant_unref);

    /* entry times */
    v = g_variant_ref_sink (g_variant_new_parsed ("(0.0, [('a', %d)], @a(sd) [])", INFINITY));
    assert_cooldown_rejected (v);
    g_clear_pointer (&v, g_variant_unref);
    v = g_variant_ref_sink (g_variant_new_parsed ("(0.0, @a(sd) [], [('c', %d)])", NAN));
    assert_cooldown_rejected (v);
    g_clear_pointer (&v, g_variant_unref);
    v = cooldown_variant_parsed ("(0.0, [('a', -0.5)], @a(sd) [])");
    assert_cooldown_rejected (v);
    g_clear_pointer (&v, g_variant_unref);
    v = cooldown_variant_parsed ("(0.0, @a(sd) [], [('c', 90000.0)])");
    assert_cooldown_rejected (v);
    g_clear_pointer (&v, g_variant_unref);

    /* names: empty, overlong, duplicate, invalid UTF-8 */
    v = cooldown_variant_parsed ("(0.0, [('', 1.0)], @a(sd) [])");
    assert_cooldown_rejected (v);
    g_clear_pointer (&v, g_variant_unref);
    v = g_variant_ref_sink (g_variant_new_parsed ("(0.0, [(%s, 1.0)], @a(sd) [])", long_key));
    assert_cooldown_rejected (v);
    g_clear_pointer (&v, g_variant_unref);
    v = cooldown_variant_parsed ("(0.0, [('a', 1.0), ('a', 2.0)], @a(sd) [])");
    assert_cooldown_rejected (v);
    g_clear_pointer (&v, g_variant_unref);
    v = cooldown_variant_parsed ("(0.0, @a(sd) [], [('c', 1.0), ('c', 2.0)])");
    assert_cooldown_rejected (v);
    g_clear_pointer (&v, g_variant_unref);
    {
        g_autoptr(GVariant) good = cooldown_variant_parsed ("(0.0, [('fireball', 1.0)], @a(sd) [])");

        v = corrupt_string (good, "fireball");
        assert_cooldown_rejected (v);
        g_clear_pointer (&v, g_variant_unref);
    }

    /* oversized arrays; exactly the maximum is fine */
    v = cooldown_variant_many (LRG_COOLDOWN_SET_MAX_ENTRIES + 1, 0);
    assert_cooldown_rejected (v);
    g_clear_pointer (&v, g_variant_unref);
    v = cooldown_variant_many (0, LRG_COOLDOWN_SET_MAX_ENTRIES + 1);
    assert_cooldown_rejected (v);
    g_clear_pointer (&v, g_variant_unref);
    v = cooldown_variant_many (LRG_COOLDOWN_SET_MAX_ENTRIES, LRG_COOLDOWN_SET_MAX_ENTRIES);
    {
        g_autoptr(LrgCooldownSet) ok = lrg_cooldown_set_new_from_variant (v, NULL);

        g_assert_nonnull (ok);
        g_assert_cmpuint (lrg_cooldown_set_get_count (ok), ==, LRG_COOLDOWN_SET_MAX_ENTRIES);
    }
}

/* ========================================================================== */
/*                                   LrgAura                                  */
/* ========================================================================== */

static void
test_aura_boxed (void)
{
    LrgAura *aura;
    LrgAura *copy;

    aura = lrg_aura_new ("renew", LRG_AURA_KIND_BUFF, 15.0);
    g_assert_cmpstr (aura->id, ==, "renew");
    g_assert_cmpint (aura->kind, ==, LRG_AURA_KIND_BUFF);
    g_assert_cmpuint (aura->source, ==, 0);
    g_assert_null (aura->dispel);
    g_assert_null (aura->stat);
    g_assert_cmpfloat (aura->duration, ==, 15.0);
    g_assert_cmpfloat (aura->remaining, ==, 15.0);
    g_assert_cmpfloat (aura->period, ==, 0.0);
    g_assert_cmpfloat (aura->tick_left, ==, 0.0);
    g_assert_cmpuint (aura->stacks, ==, 1);
    g_assert_cmpuint (aura->max_stacks, ==, 1);
    g_assert_true (lrg_aura_is_valid (aura));

    aura->dispel = g_strdup ("magic");
    aura->stat = g_strdup ("hot");
    aura->magnitude = 3.0;
    aura->max_stacks = 5;
    aura->stacks = 4;
    g_assert_cmpfloat (lrg_aura_get_total (aura), ==, 12.0);

    copy = g_boxed_copy (LRG_TYPE_AURA, aura);
    g_assert_true (copy->dispel != aura->dispel);
    g_assert_cmpstr (copy->dispel, ==, "magic");
    g_assert_cmpstr (copy->stat, ==, "hot");
    g_assert_cmpuint (copy->stacks, ==, 4);
    lrg_aura_free (aura);
    g_boxed_free (LRG_TYPE_AURA, copy);
    lrg_aura_free (NULL);

    expect_critical ();
    g_assert_null (lrg_aura_new ("", LRG_AURA_KIND_BUFF, 1.0));
    g_test_assert_expected_messages ();
    expect_critical ();
    g_assert_null (lrg_aura_new ("x", LRG_AURA_KIND_BUFF, NAN));
    g_test_assert_expected_messages ();
}

static void
test_aura_validity (void)
{
    g_autoptr(LrgAura) aura = lrg_aura_new ("a", LRG_AURA_KIND_DEBUFF, 10.0);
    g_autofree gchar *long_id = make_long_id (129);

#define CHECK_INVALID(stmt, restore) \
    G_STMT_START { stmt; g_assert_false (lrg_aura_is_valid (aura)); restore; \
                   g_assert_true (lrg_aura_is_valid (aura)); } G_STMT_END

    CHECK_INVALID (aura->kind = (LrgAuraKind) 7, aura->kind = LRG_AURA_KIND_DEBUFF);
    CHECK_INVALID (aura->magnitude = NAN, aura->magnitude = 0.0);
    CHECK_INVALID (aura->magnitude = INFINITY, aura->magnitude = 0.0);
    CHECK_INVALID (aura->duration = -1.0, aura->duration = 10.0);
    CHECK_INVALID (aura->duration = 86401.0, aura->duration = 10.0);
    CHECK_INVALID (aura->remaining = 11.0, aura->remaining = 10.0);
    CHECK_INVALID (aura->period = NAN, aura->period = 0.0);
    CHECK_INVALID (aura->tick_left = 1.0, aura->tick_left = 0.0);
    CHECK_INVALID (aura->stacks = 0, aura->stacks = 1);
    CHECK_INVALID (aura->stacks = 2, aura->stacks = 1);
    CHECK_INVALID (aura->max_stacks = 0, aura->max_stacks = 1);
    CHECK_INVALID ((aura->max_stacks = LRG_AURA_MAX_STACKS + 1, aura->stacks = 1),
                   aura->max_stacks = 1);
    CHECK_INVALID ((g_free (aura->id), aura->id = g_strdup ("")),
                   (g_free (aura->id), aura->id = g_strdup ("a")));
    CHECK_INVALID ((g_free (aura->id), aura->id = g_strdup (long_id)),
                   (g_free (aura->id), aura->id = g_strdup ("a")));
    CHECK_INVALID (aura->stat = g_strdup (""), g_clear_pointer (&aura->stat, g_free));
    CHECK_INVALID (aura->dispel = g_strdup (long_id), g_clear_pointer (&aura->dispel, g_free));
    CHECK_INVALID (aura->dispel = g_strdup ("bad\xff"), g_clear_pointer (&aura->dispel, g_free));

#undef CHECK_INVALID

    /* permanent auras keep remaining at 0 */
    aura->duration = 0.0;
    aura->remaining = 0.0;
    g_assert_true (lrg_aura_is_valid (aura));
    aura->remaining = 1.0;
    g_assert_false (lrg_aura_is_valid (aura));
}

/* ========================================================================== */
/*                                 LrgAuraSet                                 */
/* ========================================================================== */

/* Records signal emissions as "signal:id" strings. */
typedef struct
{
    GPtrArray  *log;
    LrgAuraSet *set;
    gboolean    expired_was_present;
    gchar      *remove_on_expire;   /* id removed from the set in the expire handler */
    guint       ticks_before_self_removal;
} AuraRecorder;

static void
on_ticked (LrgAuraSet *set,
           LrgAura    *aura,
           gpointer    user_data)
{
    AuraRecorder *rec = user_data;

    g_ptr_array_add (rec->log, g_strdup_printf ("tick:%s", aura->id));
    if (rec->ticks_before_self_removal > 0)
    {
        rec->ticks_before_self_removal--;
        if (rec->ticks_before_self_removal == 0)
            lrg_aura_set_remove (set, aura->id, aura->source);
    }
}

static void
on_expired (LrgAuraSet *set,
            LrgAura    *aura,
            gpointer    user_data)
{
    AuraRecorder *rec = user_data;

    g_ptr_array_add (rec->log, g_strdup_printf ("expire:%s", aura->id));
    rec->expired_was_present = (lrg_aura_set_get (set, aura->id, aura->source) == aura);
    if (rec->remove_on_expire != NULL)
        lrg_aura_set_remove_all_by_id (set, rec->remove_on_expire);
}

static void
on_removed (LrgAuraSet *set,
            LrgAura    *aura,
            gpointer    user_data)
{
    AuraRecorder *rec = user_data;

    /* already detached when the signal runs */
    g_assert_null (lrg_aura_set_get (set, aura->id, aura->source));
    g_ptr_array_add (rec->log, g_strdup_printf ("remove:%s", aura->id));
}

static void
recorder_init (AuraRecorder *rec,
               LrgAuraSet   *set)
{
    memset (rec, 0, sizeof (*rec));
    rec->log = g_ptr_array_new_with_free_func (g_free);
    rec->set = set;
    g_signal_connect (set, "aura-ticked", G_CALLBACK (on_ticked), rec);
    g_signal_connect (set, "aura-expired", G_CALLBACK (on_expired), rec);
    g_signal_connect (set, "aura-removed", G_CALLBACK (on_removed), rec);
}

static void
recorder_clear (AuraRecorder *rec)
{
    g_clear_pointer (&rec->log, g_ptr_array_unref);
    g_clear_pointer (&rec->remove_on_expire, g_free);
}

static void
assert_log (AuraRecorder *rec,
            const gchar  *expected)
{
    g_autoptr(GString) joined = g_string_new (NULL);
    guint i;

    for (i = 0; i < rec->log->len; i++)
    {
        if (i > 0)
            g_string_append_c (joined, ',');
        g_string_append (joined, g_ptr_array_index (rec->log, i));
    }
    g_assert_cmpstr (joined->str, ==, expected);
    g_ptr_array_set_size (rec->log, 0);
}

static guint
count_log (AuraRecorder *rec,
           const gchar  *entry)
{
    guint n = 0;
    guint i;

    for (i = 0; i < rec->log->len; i++)
    {
        if (g_strcmp0 (g_ptr_array_index (rec->log, i), entry) == 0)
            n++;
    }
    return n;
}

static LrgAura *
make_aura (const gchar *id,
           guint        source,
           LrgAuraKind  kind,
           gdouble      duration,
           gdouble      period)
{
    LrgAura *aura = lrg_aura_new (id, kind, duration);

    aura->source = source;
    aura->period = period;
    return aura;
}

static void
test_aura_set_properties (void)
{
    g_autoptr(LrgAuraSet) set = g_object_new (LRG_TYPE_AURA_SET, NULL);
    guint capacity;

    g_assert_cmpuint (lrg_aura_set_get_capacity (set), ==, LRG_AURA_SET_DEFAULT_CAPACITY);
    g_object_set (set, "capacity", 3, NULL);
    g_object_get (set, "capacity", &capacity, NULL);
    g_assert_cmpuint (capacity, ==, 3);

    {
        g_autoptr(LrgAura) a = make_aura ("a", 0, LRG_AURA_KIND_BUFF, 0.0, 0.0);
        g_autoptr(LrgAura) b = make_aura ("b", 0, LRG_AURA_KIND_BUFF, 0.0, 0.0);

        lrg_aura_set_apply (set, a);
        lrg_aura_set_apply (set, b);
    }
    /* cannot shrink below the current count */
    expect_critical ();
    lrg_aura_set_set_capacity (set, 1);
    g_test_assert_expected_messages ();
    g_assert_cmpuint (lrg_aura_set_get_capacity (set), ==, 3);
    lrg_aura_set_set_capacity (set, 2);
    g_assert_cmpuint (lrg_aura_set_get_capacity (set), ==, 2);

    expect_critical ();
    g_assert_null (lrg_aura_set_new (0));
    g_test_assert_expected_messages ();
    expect_critical ();
    g_assert_null (lrg_aura_set_new (LRG_AURA_SET_MAX_CAPACITY + 1));
    g_test_assert_expected_messages ();
}

static void
test_aura_set_apply (void)
{
    g_autoptr(LrgAuraSet) set = lrg_aura_set_new (3);
    g_autoptr(LrgAura) sunder = make_aura ("sunder", 7, LRG_AURA_KIND_DEBUFF, 30.0, 0.0);
    g_autoptr(LrgAura) other_source = make_aura ("sunder", 8, LRG_AURA_KIND_DEBUFF, 30.0, 0.0);
    LrgAura *stored;

    sunder->max_stacks = 3;
    sunder->magnitude = 2.0;
    sunder->stat = g_strdup ("armor_reduction");
    /* incoming remaining/tick_left are ignored */
    sunder->remaining = 1.0;

    g_assert_cmpint (lrg_aura_set_apply (set, sunder), ==, LRG_AURA_APPLY_RESULT_ADDED);
    stored = lrg_aura_set_get (set, "sunder", 7);
    g_assert_nonnull (stored);
    g_assert_true (stored != sunder);
    g_assert_cmpfloat (stored->remaining, ==, 30.0);
    g_assert_cmpuint (stored->stacks, ==, 1);

    lrg_aura_set_tick (set, 10.0);
    g_assert_cmpfloat (stored->remaining, ==, 20.0);

    g_assert_cmpint (lrg_aura_set_apply (set, sunder), ==, LRG_AURA_APPLY_RESULT_STACKED);
    g_assert_cmpuint (stored->stacks, ==, 2);
    g_assert_cmpfloat (stored->remaining, ==, 30.0);
    g_assert_cmpint (lrg_aura_set_apply (set, sunder), ==, LRG_AURA_APPLY_RESULT_STACKED);
    g_assert_cmpuint (stored->stacks, ==, 3);
    g_assert_cmpint (lrg_aura_set_apply (set, sunder), ==, LRG_AURA_APPLY_RESULT_REFRESHED);
    g_assert_cmpuint (stored->stacks, ==, 3);
    g_assert_cmpfloat (lrg_aura_set_sum_stat (set, "armor_reduction"), ==, 6.0);

    /* a different source is a separate aura */
    g_assert_cmpint (lrg_aura_set_apply (set, other_source), ==, LRG_AURA_APPLY_RESULT_ADDED);
    g_assert_cmpuint (lrg_aura_set_get_count (set), ==, 2);
    g_assert_true (lrg_aura_set_has (set, "sunder"));
    g_assert_false (lrg_aura_set_has (set, "rend"));
    g_assert_false (lrg_aura_set_has (set, NULL));
    g_assert_null (lrg_aura_set_get (set, "sunder", 9));
    g_assert_null (lrg_aura_set_get (set, NULL, 7));

    /* a refresh with new data updates magnitude and a lower stack cap */
    sunder->magnitude = 5.0;
    sunder->max_stacks = 2;
    g_assert_cmpint (lrg_aura_set_apply (set, sunder), ==, LRG_AURA_APPLY_RESULT_REFRESHED);
    g_assert_cmpuint (stored->stacks, ==, 2);
    g_assert_cmpfloat (stored->magnitude, ==, 5.0);
    g_assert_cmpuint (stored->max_stacks, ==, 2);
}

static void
test_aura_set_apply_rejections (void)
{
    g_autoptr(LrgAuraSet) set = lrg_aura_set_new (2);
    g_autoptr(LrgAura) a = make_aura ("a", 0, LRG_AURA_KIND_BUFF, 10.0, 0.0);
    g_autoptr(LrgAura) b = make_aura ("b", 0, LRG_AURA_KIND_BUFF, 10.0, 0.0);
    g_autoptr(LrgAura) c = make_aura ("c", 0, LRG_AURA_KIND_BUFF, 10.0, 0.0);
    g_autoptr(LrgAura) bad = make_aura ("bad", 0, LRG_AURA_KIND_BUFF, 10.0, 0.0);
    GPtrArray *all;

    g_assert_cmpint (lrg_aura_set_apply (set, a), ==, LRG_AURA_APPLY_RESULT_ADDED);
    g_assert_cmpint (lrg_aura_set_apply (set, b), ==, LRG_AURA_APPLY_RESULT_ADDED);

    /* full: new aura rejected, existing ones may still refresh */
    g_assert_cmpint (lrg_aura_set_apply (set, c), ==, LRG_AURA_APPLY_RESULT_REJECTED);
    g_assert_cmpuint (lrg_aura_set_get_count (set), ==, 2);
    g_assert_false (lrg_aura_set_has (set, "c"));
    g_assert_cmpint (lrg_aura_set_apply (set, a), ==, LRG_AURA_APPLY_RESULT_REFRESHED);

    /* invalid auras are rejected before any change */
    lrg_aura_set_remove (set, "b", 0);
    bad->magnitude = NAN;
    g_assert_cmpint (lrg_aura_set_apply (set, bad), ==, LRG_AURA_APPLY_RESULT_REJECTED);
    bad->magnitude = 0.0;
    bad->stacks = 3;
    g_assert_cmpint (lrg_aura_set_apply (set, bad), ==, LRG_AURA_APPLY_RESULT_REJECTED);
    bad->stacks = 1;
    bad->duration = LRG_AURA_MAX_SECONDS + 1.0;
    g_assert_cmpint (lrg_aura_set_apply (set, bad), ==, LRG_AURA_APPLY_RESULT_REJECTED);
    bad->duration = 10.0;
    bad->period = -2.0;
    g_assert_cmpint (lrg_aura_set_apply (set, bad), ==, LRG_AURA_APPLY_RESULT_REJECTED);
    bad->period = 0.0;
    bad->kind = (LrgAuraKind) 5;
    g_assert_cmpint (lrg_aura_set_apply (set, bad), ==, LRG_AURA_APPLY_RESULT_REJECTED);
    g_assert_cmpuint (lrg_aura_set_get_count (set), ==, 1);

    /* an invalid refresh leaves the existing aura untouched */
    a->magnitude = INFINITY;
    g_assert_cmpint (lrg_aura_set_apply (set, a), ==, LRG_AURA_APPLY_RESULT_REJECTED);
    all = lrg_aura_set_get_all (set);
    g_assert_cmpfloat (((LrgAura *) g_ptr_array_index (all, 0))->magnitude, ==, 0.0);
}

static void
test_aura_set_periodic (void)
{
    g_autoptr(LrgAuraSet) set = lrg_aura_set_new (8);
    g_autoptr(LrgAura) dot = make_aura ("corruption", 1, LRG_AURA_KIND_DEBUFF, 10.0, 2.0);
    AuraRecorder rec;

    recorder_init (&rec, set);
    g_assert_cmpint (lrg_aura_set_apply (set, dot), ==, LRG_AURA_APPLY_RESULT_ADDED);
    g_assert_cmpfloat (lrg_aura_set_get (set, "corruption", 1)->tick_left, ==, 2.0);

    lrg_aura_set_tick (set, 1.0);
    assert_log (&rec, "");
    lrg_aura_set_tick (set, 1.0);
    assert_log (&rec, "tick:corruption");

    /* refresh keeps the tick phase */
    lrg_aura_set_tick (set, 0.5);
    g_assert_cmpfloat (lrg_aura_set_get (set, "corruption", 1)->tick_left, ==, 1.5);
    g_assert_cmpint (lrg_aura_set_apply (set, dot), ==, LRG_AURA_APPLY_RESULT_REFRESHED);
    g_assert_cmpfloat (lrg_aura_set_get (set, "corruption", 1)->tick_left, ==, 1.5);
    g_assert_cmpfloat (lrg_aura_set_get (set, "corruption", 1)->remaining, ==, 10.0);

    /* one huge tick: ticks at 1.5, 3.5, 5.5, 7.5, 9.5 then expiry; none after */
    lrg_aura_set_tick (set, 1000.0);
    assert_log (&rec, "tick:corruption,tick:corruption,tick:corruption,tick:corruption,"
                      "tick:corruption,expire:corruption");
    g_assert_true (rec.expired_was_present);
    g_assert_cmpuint (lrg_aura_set_get_count (set), ==, 0);

    /* exactly aligned final tick fires with the expiry: 10s / 2s = 5 ticks */
    g_assert_cmpint (lrg_aura_set_apply (set, dot), ==, LRG_AURA_APPLY_RESULT_ADDED);
    lrg_aura_set_tick (set, 10.0);
    g_assert_cmpuint (count_log (&rec, "tick:corruption"), ==, 5);
    g_assert_cmpuint (count_log (&rec, "expire:corruption"), ==, 1);
    g_assert_cmpstr (g_ptr_array_index (rec.log, rec.log->len - 1), ==, "expire:corruption");

    recorder_clear (&rec);
}

static void
test_aura_set_tick_bound (void)
{
    g_autoptr(LrgAuraSet) set = lrg_aura_set_new (8);
    g_autoptr(LrgAura) aura = make_aura ("aura_of_pain", 0, LRG_AURA_KIND_DEBUFF, 0.0, 1.0);
    AuraRecorder rec;
    LrgAura *stored;

    recorder_init (&rec, set);
    lrg_aura_set_apply (set, aura);
    stored = lrg_aura_set_get (set, "aura_of_pain", 0);

    /* multiple periods in one tick on a permanent aura */
    lrg_aura_set_tick (set, 5.0);
    g_assert_cmpuint (rec.log->len, ==, 5);
    g_assert_cmpfloat (stored->tick_left, ==, 1.0);
    g_ptr_array_set_size (rec.log, 0);

    /* bounded to 100 per call; excess dropped, phase reset to one period */
    lrg_aura_set_tick (set, 1e6);
    g_assert_cmpuint (rec.log->len, ==, LRG_AURA_SET_MAX_TICKS_PER_CALL);
    g_assert_cmpfloat (stored->tick_left, ==, 1.0);
    g_ptr_array_set_size (rec.log, 0);

    /* permanent auras never expire */
    g_assert_cmpuint (lrg_aura_set_get_count (set), ==, 1);

    /* bad deltas are ignored */
    lrg_aura_set_tick (set, NAN);
    lrg_aura_set_tick (set, INFINITY);
    lrg_aura_set_tick (set, -5.0);
    lrg_aura_set_tick (set, 0.0);
    g_assert_cmpuint (rec.log->len, ==, 0);
    g_assert_cmpfloat (stored->tick_left, ==, 1.0);

    recorder_clear (&rec);
}

static void
test_aura_set_expiry_order (void)
{
    g_autoptr(LrgAuraSet) set = lrg_aura_set_new (8);
    g_autoptr(LrgAura) first = make_aura ("first", 0, LRG_AURA_KIND_BUFF, 1.0, 0.0);
    g_autoptr(LrgAura) second = make_aura ("second", 0, LRG_AURA_KIND_BUFF, 1.0, 0.0);
    g_autoptr(LrgAura) third = make_aura ("third", 0, LRG_AURA_KIND_BUFF, 5.0, 0.0);
    g_autoptr(LrgAura) forever = make_aura ("forever", 0, LRG_AURA_KIND_BUFF, 0.0, 0.0);
    AuraRecorder rec;

    recorder_init (&rec, set);
    lrg_aura_set_apply (set, first);
    lrg_aura_set_apply (set, forever);
    lrg_aura_set_apply (set, second);
    lrg_aura_set_apply (set, third);

    lrg_aura_set_tick (set, 0.5);
    assert_log (&rec, "");
    lrg_aura_set_tick (set, 0.5);
    assert_log (&rec, "expire:first,expire:second");
    g_assert_true (rec.expired_was_present);
    g_assert_cmpuint (lrg_aura_set_get_count (set), ==, 2);
    g_assert_cmpstr (((LrgAura *) g_ptr_array_index (lrg_aura_set_get_all (set), 0))->id, ==, "forever");
    g_assert_cmpstr (((LrgAura *) g_ptr_array_index (lrg_aura_set_get_all (set), 1))->id, ==, "third");

    recorder_clear (&rec);
}

static void
test_aura_set_reentrant_handlers (void)
{
    g_autoptr(LrgAuraSet) set = lrg_aura_set_new (8);
    g_autoptr(LrgAura) trigger = make_aura ("trigger", 0, LRG_AURA_KIND_BUFF, 1.0, 0.0);
    g_autoptr(LrgAura) victim = make_aura ("victim", 0, LRG_AURA_KIND_BUFF, 1.0, 0.0);
    g_autoptr(LrgAura) self_removing = make_aura ("shield", 0, LRG_AURA_KIND_BUFF, 0.0, 1.0);
    AuraRecorder rec;

    recorder_init (&rec, set);

    /* the first expiry removes the second aura: it is removed, not expired */
    rec.remove_on_expire = g_strdup ("victim");
    lrg_aura_set_apply (set, trigger);
    lrg_aura_set_apply (set, victim);
    lrg_aura_set_tick (set, 2.0);
    assert_log (&rec, "expire:trigger,remove:victim");
    g_assert_cmpuint (lrg_aura_set_get_count (set), ==, 0);
    g_clear_pointer (&rec.remove_on_expire, g_free);

    /* a periodic aura removing itself in its tick handler stops ticking */
    rec.ticks_before_self_removal = 2;
    lrg_aura_set_apply (set, self_removing);
    lrg_aura_set_tick (set, 10.0);
    assert_log (&rec, "tick:shield,tick:shield,remove:shield");
    g_assert_cmpuint (lrg_aura_set_get_count (set), ==, 0);

    recorder_clear (&rec);
}

static void
test_aura_set_remove_dispel_clear (void)
{
    g_autoptr(LrgAuraSet) set = lrg_aura_set_new (16);
    AuraRecorder rec;
    const struct
    {
        const gchar *id;
        guint        source;
        LrgAuraKind  kind;
        const gchar *dispel;
    } specs[] = {
        { "slow",     1, LRG_AURA_KIND_DEBUFF, "magic" },
        { "curse",    1, LRG_AURA_KIND_DEBUFF, "curse" },
        { "poison",   2, LRG_AURA_KIND_DEBUFF, "poison" },
        { "freeze",   2, LRG_AURA_KIND_DEBUFF, "magic" },
        { "bleed",    3, LRG_AURA_KIND_DEBUFF, NULL },
        { "armor",    4, LRG_AURA_KIND_BUFF,   "magic" },
        { "silence",  5, LRG_AURA_KIND_DEBUFF, "magic" },
        { "slow",     6, LRG_AURA_KIND_DEBUFF, "magic" },
    };
    guint i;

    recorder_init (&rec, set);
    for (i = 0; i < G_N_ELEMENTS (specs); i++)
    {
        g_autoptr(LrgAura) aura = make_aura (specs[i].id, specs[i].source, specs[i].kind, 30.0, 0.0);

        aura->dispel = g_strdup (specs[i].dispel);
        g_assert_cmpint (lrg_aura_set_apply (set, aura), ==, LRG_AURA_APPLY_RESULT_ADDED);
    }

    /* single removal */
    g_assert_true (lrg_aura_set_remove (set, "curse", 1));
    g_assert_false (lrg_aura_set_remove (set, "curse", 1));
    g_assert_false (lrg_aura_set_remove (set, "slow", 99));
    g_assert_false (lrg_aura_set_remove (set, NULL, 1));
    assert_log (&rec, "remove:curse");

    /* dispel magic debuffs oldest first, two at most */
    g_assert_cmpuint (lrg_aura_set_dispel (set, LRG_AURA_KIND_DEBUFF, "magic", 2), ==, 2);
    assert_log (&rec, "remove:slow,remove:freeze");
    g_assert_nonnull (lrg_aura_set_get (set, "silence", 5));
    g_assert_nonnull (lrg_aura_set_get (set, "armor", 4));

    /* max_count 0 removes nothing */
    g_assert_cmpuint (lrg_aura_set_dispel (set, LRG_AURA_KIND_DEBUFF, NULL, 0), ==, 0);

    /* NULL dispel matches any dispellable aura of the kind, never undispellable ones */
    g_assert_cmpuint (lrg_aura_set_dispel (set, LRG_AURA_KIND_DEBUFF, NULL, 100), ==, 3);
    assert_log (&rec, "remove:poison,remove:silence,remove:slow");
    g_assert_nonnull (lrg_aura_set_get (set, "bleed", 3));
    g_assert_nonnull (lrg_aura_set_get (set, "armor", 4));
    g_assert_cmpuint (lrg_aura_set_dispel (set, LRG_AURA_KIND_DEBUFF, "magic", 5), ==, 0);

    /* remove_all_by_id across sources */
    {
        g_autoptr(LrgAura) b1 = make_aura ("bleed", 7, LRG_AURA_KIND_DEBUFF, 30.0, 0.0);

        lrg_aura_set_apply (set, b1);
    }
    g_assert_cmpuint (lrg_aura_set_remove_all_by_id (set, "bleed"), ==, 2);
    g_assert_cmpuint (lrg_aura_set_remove_all_by_id (set, "bleed"), ==, 0);
    g_assert_cmpuint (lrg_aura_set_remove_all_by_id (set, NULL), ==, 0);
    assert_log (&rec, "remove:bleed,remove:bleed");

    /* clear announces every remaining aura in order */
    {
        g_autoptr(LrgAura) extra = make_aura ("extra", 0, LRG_AURA_KIND_BUFF, 30.0, 0.0);

        lrg_aura_set_apply (set, extra);
    }
    lrg_aura_set_clear (set);
    assert_log (&rec, "remove:armor,remove:extra");
    g_assert_cmpuint (lrg_aura_set_get_count (set), ==, 0);
    lrg_aura_set_clear (set);
    assert_log (&rec, "");

    recorder_clear (&rec);
}

static void
test_aura_set_sum_stat (void)
{
    g_autoptr(LrgAuraSet) set = lrg_aura_set_new (8);
    g_autoptr(LrgAura) a = make_aura ("fortitude", 1, LRG_AURA_KIND_BUFF, 0.0, 0.0);
    g_autoptr(LrgAura) b = make_aura ("sunder", 2, LRG_AURA_KIND_DEBUFF, 30.0, 0.0);
    g_autoptr(LrgAura) c = make_aura ("fortitude", 3, LRG_AURA_KIND_BUFF, 0.0, 0.0);

    a->stat = g_strdup ("stamina");
    a->magnitude = 10.0;
    b->stat = g_strdup ("armor");
    b->magnitude = -50.0;
    b->max_stacks = 5;
    c->stat = g_strdup ("stamina");
    c->magnitude = 2.5;

    lrg_aura_set_apply (set, a);
    lrg_aura_set_apply (set, b);
    lrg_aura_set_apply (set, b);
    lrg_aura_set_apply (set, c);

    g_assert_cmpfloat (lrg_aura_set_sum_stat (set, "stamina"), ==, 12.5);
    g_assert_cmpfloat (lrg_aura_set_sum_stat (set, "armor"), ==, -100.0);
    g_assert_cmpfloat (lrg_aura_set_sum_stat (set, "agility"), ==, 0.0);
    g_assert_cmpfloat (lrg_aura_set_sum_stat (set, NULL), ==, 0.0);
}

/* ------------------------------ persistence ------------------------------- */

static void
assert_aura_set_rejected (GVariant *variant)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgAuraSet) set = NULL;

    set = lrg_aura_set_new_from_variant (variant, &error);
    g_assert_null (set);
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
}

/* One aura row with overridable fields. */
typedef struct
{
    const gchar *id;
    guint32      source;
    guint32      kind;
    const gchar *dispel;
    const gchar *stat;
    gdouble      magnitude;
    gdouble      duration;
    gdouble      remaining;
    gdouble      period;
    gdouble      tick_left;
    guint32      stacks;
    guint32      max_stacks;
} AuraRow;

static AuraRow
default_row (void)
{
    AuraRow row = { "renew", 3, LRG_AURA_KIND_BUFF, "magic", "hot",
                    4.0, 15.0, 12.0, 3.0, 1.0, 1, 1 };
    return row;
}

static GVariant *
aura_set_variant (guint32        capacity,
                  const AuraRow *rows,
                  guint          n_rows)
{
    GVariantBuilder builder;
    guint i;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(suussddddduu)"));
    for (i = 0; i < n_rows; i++)
    {
        g_variant_builder_add (&builder, "(suussddddduu)",
                               rows[i].id, rows[i].source, rows[i].kind,
                               rows[i].dispel, rows[i].stat,
                               rows[i].magnitude, rows[i].duration, rows[i].remaining,
                               rows[i].period, rows[i].tick_left,
                               rows[i].stacks, rows[i].max_stacks);
    }
    return g_variant_ref_sink (g_variant_new ("(ua(suussddddduu))", capacity, &builder));
}

static void
assert_row_rejected (AuraRow row)
{
    g_autoptr(GVariant) v = aura_set_variant (4, &row, 1);

    assert_aura_set_rejected (v);
}

static void
test_aura_set_variant_round_trip (void)
{
    g_autoptr(LrgAuraSet) set = lrg_aura_set_new (5);
    g_autoptr(LrgAuraSet) restored = NULL;
    g_autoptr(GVariant) first = NULL;
    g_autoptr(GVariant) second = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgAura) a = make_aura ("renew", 3, LRG_AURA_KIND_BUFF, 15.0, 3.0);
    g_autoptr(LrgAura) b = make_aura ("sunder", 9, LRG_AURA_KIND_DEBUFF, 30.0, 0.0);
    g_autoptr(LrgAura) c = make_aura ("aura", 0, LRG_AURA_KIND_BUFF, 0.0, 0.0);
    LrgAura *r;

    a->dispel = g_strdup ("magic");
    a->stat = g_strdup ("hot");
    a->magnitude = 4.0;
    b->stat = g_strdup ("armor");
    b->magnitude = -12.5;
    b->max_stacks = 5;
    lrg_aura_set_apply (set, a);
    lrg_aura_set_apply (set, b);
    lrg_aura_set_apply (set, b);
    lrg_aura_set_apply (set, c);
    lrg_aura_set_tick (set, 1.25);

    first = lrg_aura_set_to_variant (set);
    g_assert_false (g_variant_is_floating (first));
    g_assert_cmpstr (g_variant_get_type_string (first), ==, LRG_AURA_SET_VARIANT_TYPE);

    restored = lrg_aura_set_new_from_variant (first, &error);
    g_assert_no_error (error);
    g_assert_nonnull (restored);
    g_assert_cmpuint (lrg_aura_set_get_capacity (restored), ==, 5);
    g_assert_cmpuint (lrg_aura_set_get_count (restored), ==, 3);

    r = g_ptr_array_index (lrg_aura_set_get_all (restored), 0);
    g_assert_cmpstr (r->id, ==, "renew");
    g_assert_cmpuint (r->source, ==, 3);
    g_assert_cmpstr (r->dispel, ==, "magic");
    g_assert_cmpstr (r->stat, ==, "hot");
    g_assert_cmpfloat (r->remaining, ==, 13.75);
    g_assert_cmpfloat (r->tick_left, ==, 1.75);
    r = g_ptr_array_index (lrg_aura_set_get_all (restored), 1);
    g_assert_cmpstr (r->id, ==, "sunder");
    g_assert_null (r->dispel);
    g_assert_cmpuint (r->stacks, ==, 2);
    g_assert_cmpuint (r->max_stacks, ==, 5);
    g_assert_cmpint (r->kind, ==, LRG_AURA_KIND_DEBUFF);
    r = g_ptr_array_index (lrg_aura_set_get_all (restored), 2);
    g_assert_null (r->stat);

    second = lrg_aura_set_to_variant (restored);
    g_assert_true (g_variant_equal (first, second));

    /* empty set */
    {
        g_autoptr(LrgAuraSet) empty = lrg_aura_set_new (1);
        g_autoptr(GVariant) v1 = lrg_aura_set_to_variant (empty);
        g_autoptr(LrgAuraSet) back = lrg_aura_set_new_from_variant (v1, NULL);
        g_autoptr(GVariant) v2 = NULL;

        g_assert_nonnull (back);
        v2 = lrg_aura_set_to_variant (back);
        g_assert_true (g_variant_equal (v1, v2));
    }

    /* the same id from two sources is not a duplicate */
    {
        AuraRow rows[2];
        g_autoptr(GVariant) v = NULL;
        g_autoptr(LrgAuraSet) ok = NULL;

        rows[0] = default_row ();
        rows[1] = default_row ();
        rows[1].source = 4;
        v = aura_set_variant (2, rows, 2);
        ok = lrg_aura_set_new_from_variant (v, NULL);
        g_assert_nonnull (ok);
    }
}

static void
test_aura_set_variant_hostile (void)
{
    g_autofree gchar *long_id = make_long_id (129);
    g_autoptr(GVariant) v = NULL;
    AuraRow row;
    AuraRow rows[3];

    /* wrong type */
    v = g_variant_ref_sink (g_variant_new_parsed ("(uint32 4, @a(susssddddduu) [])"));
    assert_aura_set_rejected (v);
    g_clear_pointer (&v, g_variant_unref);
    v = g_variant_ref_sink (g_variant_new_parsed ("(uint32 4,)"));
    assert_aura_set_rejected (v);
    g_clear_pointer (&v, g_variant_unref);

    /* capacity out of range and more auras than capacity */
    v = aura_set_variant (0, NULL, 0);
    assert_aura_set_rejected (v);
    g_clear_pointer (&v, g_variant_unref);
    v = aura_set_variant (LRG_AURA_SET_MAX_CAPACITY + 1, NULL, 0);
    assert_aura_set_rejected (v);
    g_clear_pointer (&v, g_variant_unref);
    rows[0] = default_row ();
    rows[1] = default_row ();
    rows[1].id = "second";
    v = aura_set_variant (1, rows, 2);
    assert_aura_set_rejected (v);
    g_clear_pointer (&v, g_variant_unref);

    /* duplicates */
    rows[1] = default_row ();
    v = aura_set_variant (4, rows, 2);
    assert_aura_set_rejected (v);
    g_clear_pointer (&v, g_variant_unref);

    /* invalid UTF-8 */
    row = default_row ();
    {
        g_autoptr(GVariant) good = aura_set_variant (4, &row, 1);

        v = corrupt_string (good, "renew");
        assert_aura_set_rejected (v);
        g_clear_pointer (&v, g_variant_unref);
    }

    /* every field */
    row = default_row (); row.id = "";            assert_row_rejected (row);
    row = default_row (); row.id = long_id;       assert_row_rejected (row);
    row = default_row (); row.dispel = long_id;   assert_row_rejected (row);
    row = default_row (); row.stat = long_id;     assert_row_rejected (row);
    row = default_row (); row.kind = 2;           assert_row_rejected (row);
    row = default_row (); row.kind = G_MAXUINT32; assert_row_rejected (row);
    row = default_row (); row.magnitude = NAN;    assert_row_rejected (row);
    row = default_row (); row.magnitude = -INFINITY; assert_row_rejected (row);
    row = default_row (); row.duration = NAN;     assert_row_rejected (row);
    row = default_row (); row.duration = -1.0;    assert_row_rejected (row);
    row = default_row (); row.duration = 86400.5; row.remaining = 1.0; assert_row_rejected (row);
    row = default_row (); row.remaining = 15.5;   assert_row_rejected (row);
    row = default_row (); row.remaining = -0.1;   assert_row_rejected (row);
    row = default_row (); row.remaining = INFINITY; assert_row_rejected (row);
    row = default_row (); row.duration = 0.0; row.remaining = 1.0; assert_row_rejected (row);
    row = default_row (); row.period = 90000.0;   assert_row_rejected (row);
    row = default_row (); row.period = NAN;       assert_row_rejected (row);
    row = default_row (); row.tick_left = 3.5;    assert_row_rejected (row);
    row = default_row (); row.tick_left = -1.0;   assert_row_rejected (row);
    row = default_row (); row.period = 0.0; row.tick_left = 1.0; assert_row_rejected (row);
    row = default_row (); row.stacks = 0;         assert_row_rejected (row);
    row = default_row (); row.stacks = 2;         assert_row_rejected (row);
    row = default_row (); row.max_stacks = 0;     assert_row_rejected (row);
    row = default_row (); row.max_stacks = 1000; row.stacks = 1; assert_row_rejected (row);

    /* boundaries that are accepted */
    row = default_row ();
    row.max_stacks = LRG_AURA_MAX_STACKS;
    row.stacks = LRG_AURA_MAX_STACKS;
    row.duration = LRG_AURA_MAX_SECONDS;
    row.remaining = 0.0;
    row.dispel = "";
    row.stat = "";
    v = aura_set_variant (LRG_AURA_SET_MAX_CAPACITY, &row, 1);
    {
        g_autoptr(LrgAuraSet) ok = lrg_aura_set_new_from_variant (v, NULL);

        g_assert_nonnull (ok);
        g_assert_null (lrg_aura_set_get (ok, "renew", 3)->dispel);
    }
}

/* ========================================================================== */
/*                                   LrgVital                                 */
/* ========================================================================== */

static void
test_vital_new (void)
{
    g_autoptr(LrgVital) mana = lrg_vital_new ("mana", 100.0, 1.0, 5.0);
    g_autoptr(LrgVital) rage = lrg_vital_new ("rage", 100.0, 0.0, -2.0);
    LrgVital *copy;

    g_assert_cmpstr (mana->kind, ==, "mana");
    g_assert_cmpfloat (mana->current, ==, 100.0);
    g_assert_cmpfloat (mana->maximum, ==, 100.0);
    g_assert_cmpfloat (mana->regen, ==, 1.0);
    g_assert_cmpfloat (mana->idle_regen, ==, 5.0);
    g_assert_cmpfloat (lrg_vital_get_fraction (mana), ==, 1.0);
    g_assert_cmpfloat (rage->current, ==, 0.0);
    g_assert_cmpfloat (lrg_vital_get_fraction (rage), ==, 0.0);

    copy = g_boxed_copy (LRG_TYPE_VITAL, mana);
    g_assert_true (copy->kind != mana->kind);
    g_assert_cmpstr (copy->kind, ==, "mana");
    g_assert_cmpfloat (copy->current, ==, 100.0);
    g_boxed_free (LRG_TYPE_VITAL, copy);
    lrg_vital_free (NULL);

    expect_critical ();
    g_assert_null (lrg_vital_new ("", 10.0, 0.0, 0.0));
    g_test_assert_expected_messages ();
    expect_critical ();
    g_assert_null (lrg_vital_new ("x", 0.0, 0.0, 0.0));
    g_test_assert_expected_messages ();
    expect_critical ();
    g_assert_null (lrg_vital_new ("x", NAN, 0.0, 0.0));
    g_test_assert_expected_messages ();
    expect_critical ();
    g_assert_null (lrg_vital_new ("x", 10.0, INFINITY, 0.0));
    g_test_assert_expected_messages ();
}

static void
test_vital_tick (void)
{
    g_autoptr(LrgVital) mana = lrg_vital_new ("mana", 100.0, 1.0, 5.0);
    g_autoptr(LrgVital) rage = lrg_vital_new ("rage", 100.0, 0.0, -2.0);

    g_assert_true (lrg_vital_spend (mana, 60.0));
    lrg_vital_tick (mana, 2.0, TRUE);
    g_assert_cmpfloat (mana->current, ==, 42.0);
    lrg_vital_tick (mana, 2.0, FALSE);
    g_assert_cmpfloat (mana->current, ==, 52.0);
    /* clamped at the maximum */
    lrg_vital_tick (mana, 1000.0, FALSE);
    g_assert_cmpfloat (mana->current, ==, 100.0);

    /* ignored deltas */
    g_assert_true (lrg_vital_spend (mana, 50.0));
    lrg_vital_tick (mana, NAN, FALSE);
    lrg_vital_tick (mana, INFINITY, FALSE);
    lrg_vital_tick (mana, -3.0, FALSE);
    lrg_vital_tick (mana, 0.0, FALSE);
    g_assert_cmpfloat (mana->current, ==, 50.0);

    /* rage decays out of combat, clamped at 0 */
    lrg_vital_gain (rage, 30.0);
    lrg_vital_tick (rage, 5.0, FALSE);
    g_assert_cmpfloat (rage->current, ==, 20.0);
    lrg_vital_tick (rage, 5.0, TRUE);
    g_assert_cmpfloat (rage->current, ==, 20.0);
    lrg_vital_tick (rage, 100.0, FALSE);
    g_assert_cmpfloat (rage->current, ==, 0.0);
}

static void
test_vital_spend_gain (void)
{
    g_autoptr(LrgVital) energy = lrg_vital_new ("energy", 100.0, 10.0, 10.0);

    g_assert_true (lrg_vital_spend (energy, 40.0));
    g_assert_cmpfloat (energy->current, ==, 60.0);

    /* insufficient: atomic, unchanged */
    g_assert_false (lrg_vital_spend (energy, 60.5));
    g_assert_cmpfloat (energy->current, ==, 60.0);
    g_assert_false (lrg_vital_spend (energy, -1.0));
    g_assert_false (lrg_vital_spend (energy, NAN));
    g_assert_false (lrg_vital_spend (energy, INFINITY));
    g_assert_cmpfloat (energy->current, ==, 60.0);

    /* exact and zero amounts */
    g_assert_true (lrg_vital_spend (energy, 0.0));
    g_assert_true (lrg_vital_spend (energy, 60.0));
    g_assert_cmpfloat (energy->current, ==, 0.0);
    g_assert_false (lrg_vital_spend (energy, 0.001));

    lrg_vital_gain (energy, 30.0);
    g_assert_cmpfloat (energy->current, ==, 30.0);
    lrg_vital_gain (energy, 500.0);
    g_assert_cmpfloat (energy->current, ==, 100.0);
    lrg_vital_gain (energy, -250.0);
    g_assert_cmpfloat (energy->current, ==, 0.0);
    lrg_vital_gain (energy, NAN);
    lrg_vital_gain (energy, INFINITY);
    g_assert_cmpfloat (energy->current, ==, 0.0);
}

static void
test_vital_set_maximum (void)
{
    g_autoptr(LrgVital) mana = lrg_vital_new ("mana", 100.0, 1.0, 1.0);

    g_assert_true (lrg_vital_spend (mana, 50.0));
    lrg_vital_set_maximum (mana, 200.0, TRUE);
    g_assert_cmpfloat (mana->maximum, ==, 200.0);
    g_assert_cmpfloat (mana->current, ==, 100.0);
    g_assert_cmpfloat (lrg_vital_get_fraction (mana), ==, 0.5);

    lrg_vital_set_maximum (mana, 400.0, FALSE);
    g_assert_cmpfloat (mana->current, ==, 100.0);
    g_assert_cmpfloat (lrg_vital_get_fraction (mana), ==, 0.25);

    lrg_vital_set_maximum (mana, 30.0, FALSE);
    g_assert_cmpfloat (mana->current, ==, 30.0);
    lrg_vital_set_maximum (mana, 10.0, TRUE);
    g_assert_cmpfloat (mana->current, ==, 10.0);

    expect_critical ();
    lrg_vital_set_maximum (mana, 0.0, TRUE);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_vital_set_maximum (mana, NAN, FALSE);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_vital_set_maximum (mana, LRG_VITAL_MAX_VALUE * 2.0, FALSE);
    g_test_assert_expected_messages ();
    g_assert_cmpfloat (mana->maximum, ==, 10.0);
    g_assert_cmpfloat (mana->current, ==, 10.0);
}

static void
assert_vital_rejected (GVariant *variant)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgVital) vital = NULL;

    vital = lrg_vital_new_from_variant (variant, &error);
    g_assert_null (vital);
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
}

static GVariant *
vital_variant (const gchar *kind,
               gdouble      current,
               gdouble      maximum,
               gdouble      regen,
               gdouble      idle_regen)
{
    return g_variant_ref_sink (g_variant_new ("(sdddd)", kind, current, maximum, regen, idle_regen));
}

static void
test_vital_variant (void)
{
    g_autoptr(LrgVital) rage = lrg_vital_new ("rage", 100.0, 0.0, -2.5);
    g_autoptr(LrgVital) restored = NULL;
    g_autoptr(GVariant) first = NULL;
    g_autoptr(GVariant) second = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *long_kind = make_long_id (129);
    GVariant *v;

    lrg_vital_gain (rage, 37.25);
    first = lrg_vital_to_variant (rage);
    g_assert_false (g_variant_is_floating (first));
    g_assert_cmpstr (g_variant_get_type_string (first), ==, LRG_VITAL_VARIANT_TYPE);

    restored = lrg_vital_new_from_variant (first, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (restored->kind, ==, "rage");
    g_assert_cmpfloat (restored->current, ==, 37.25);
    g_assert_cmpfloat (restored->maximum, ==, 100.0);
    g_assert_cmpfloat (restored->regen, ==, 0.0);
    g_assert_cmpfloat (restored->idle_regen, ==, -2.5);
    second = lrg_vital_to_variant (restored);
    g_assert_true (g_variant_equal (first, second));

    /* hostile */
    v = g_variant_ref_sink (g_variant_new ("(sddd)", "mana", 1.0, 2.0, 3.0));
    assert_vital_rejected (v);
    g_variant_unref (v);
    v = corrupt_string (first, "rage");
    assert_vital_rejected (v);
    g_variant_unref (v);

#define REJECT(args) G_STMT_START { v = vital_variant args; assert_vital_rejected (v); \
                                    g_variant_unref (v); } G_STMT_END
    REJECT (("", 1.0, 10.0, 0.0, 0.0));
    REJECT ((long_kind, 1.0, 10.0, 0.0, 0.0));
    REJECT (("mana", 1.0, 0.0, 0.0, 0.0));
    REJECT (("mana", 0.0, -10.0, 0.0, 0.0));
    REJECT (("mana", 1.0, NAN, 0.0, 0.0));
    REJECT (("mana", 1.0, INFINITY, 0.0, 0.0));
    REJECT (("mana", 1.0, LRG_VITAL_MAX_VALUE * 2.0, 0.0, 0.0));
    REJECT (("mana", 10.5, 10.0, 0.0, 0.0));
    REJECT (("mana", -0.5, 10.0, 0.0, 0.0));
    REJECT (("mana", NAN, 10.0, 0.0, 0.0));
    REJECT (("mana", 1.0, 10.0, INFINITY, 0.0));
    REJECT (("mana", 1.0, 10.0, 0.0, NAN));
    REJECT (("mana", 1.0, 10.0, LRG_VITAL_MAX_VALUE * 2.0, 0.0));
    REJECT (("mana", 1.0, 10.0, 0.0, -LRG_VITAL_MAX_VALUE * 2.0));
#undef REJECT

    /* boundaries accepted */
    v = vital_variant ("mana", LRG_VITAL_MAX_VALUE, LRG_VITAL_MAX_VALUE,
                       -LRG_VITAL_MAX_VALUE, LRG_VITAL_MAX_VALUE);
    {
        g_autoptr(LrgVital) ok = lrg_vital_new_from_variant (v, NULL);

        g_assert_nonnull (ok);
    }
    g_variant_unref (v);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/cooldown-set/defaults-properties", test_cooldown_defaults_properties);
    g_test_add_func ("/cooldown-set/start-tick", test_cooldown_start_tick);
    g_test_add_func ("/cooldown-set/tick-ignores-bad-delta", test_cooldown_tick_ignores_bad_delta);
    g_test_add_func ("/cooldown-set/category-global", test_cooldown_category_and_global);
    g_test_add_func ("/cooldown-set/reset-clear", test_cooldown_reset_clear);
    g_test_add_func ("/cooldown-set/start-rejections", test_cooldown_start_rejections);
    g_test_add_func ("/cooldown-set/eviction", test_cooldown_eviction);
    g_test_add_func ("/cooldown-set/abilities", test_cooldown_abilities);
    g_test_add_func ("/cooldown-set/variant/round-trip", test_cooldown_variant_round_trip);
    g_test_add_func ("/cooldown-set/variant/hostile", test_cooldown_variant_hostile);

    g_test_add_func ("/aura/boxed", test_aura_boxed);
    g_test_add_func ("/aura/validity", test_aura_validity);

    g_test_add_func ("/aura-set/properties", test_aura_set_properties);
    g_test_add_func ("/aura-set/apply", test_aura_set_apply);
    g_test_add_func ("/aura-set/apply-rejections", test_aura_set_apply_rejections);
    g_test_add_func ("/aura-set/periodic", test_aura_set_periodic);
    g_test_add_func ("/aura-set/tick-bound", test_aura_set_tick_bound);
    g_test_add_func ("/aura-set/expiry-order", test_aura_set_expiry_order);
    g_test_add_func ("/aura-set/reentrant-handlers", test_aura_set_reentrant_handlers);
    g_test_add_func ("/aura-set/remove-dispel-clear", test_aura_set_remove_dispel_clear);
    g_test_add_func ("/aura-set/sum-stat", test_aura_set_sum_stat);
    g_test_add_func ("/aura-set/variant/round-trip", test_aura_set_variant_round_trip);
    g_test_add_func ("/aura-set/variant/hostile", test_aura_set_variant_hostile);

    g_test_add_func ("/vital/new", test_vital_new);
    g_test_add_func ("/vital/tick", test_vital_tick);
    g_test_add_func ("/vital/spend-gain", test_vital_spend_gain);
    g_test_add_func ("/vital/set-maximum", test_vital_set_maximum);
    g_test_add_func ("/vital/variant", test_vital_variant);

    return g_test_run ();
}
