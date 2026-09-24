/* test-ability.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgAbilityEffect, LrgAbilityDef and LrgSpellbook.
 */

#include <glib.h>
#include <glib-object.h>
#include <math.h>
#include <string.h>

#include "lrg-enums.h"
#include "progression/lrg-ability-def.h"
#include "progression/lrg-spellbook.h"

/* ========================================================================== */
/*                                  Helpers                                   */
/* ========================================================================== */

static void
expect_critical (void)
{
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*assertion*failed*");
}

static void
count_notify (GObject    *object,
              GParamSpec *pspec,
              gpointer    user_data)
{
    (void) object;
    (void) pspec;
    (*(guint *) user_data)++;
}

/*
 * corrupt_string:
 *
 * Returns a copy of @v whose serialised bytes have the first byte of
 * @needle replaced with 0xFF, producing invalid UTF-8 (non-normal form).
 */
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
/*                               LrgAbilityEffect                             */
/* ========================================================================== */

static void
test_effect_new_copy_free (void)
{
    LrgAbilityEffect *effect;
    LrgAbilityEffect *copy;

    effect = lrg_ability_effect_new ("dot");
    g_assert_cmpstr (effect->kind, ==, "dot");
    g_assert_null (effect->aura_id);
    g_assert_null (effect->stat);
    g_assert_cmpfloat (effect->amount, ==, 0.0);
    g_assert_cmpfloat (effect->scale, ==, 0.0);
    g_assert_cmpfloat (effect->duration, ==, 0.0);
    g_assert_cmpfloat (effect->period, ==, 0.0);
    g_assert_cmpfloat (effect->radius, ==, 0.0);
    g_assert_cmpuint (effect->max_targets, ==, 1);

    effect->aura_id = g_strdup ("corruption");
    effect->stat = g_strdup ("shadow");
    effect->amount = 12.5;
    effect->scale = 0.2;
    effect->duration = 18.0;
    effect->period = 3.0;
    effect->radius = 8.0;
    effect->max_targets = 5;

    copy = g_boxed_copy (LRG_TYPE_ABILITY_EFFECT, effect);
    g_assert_true (copy != effect);
    g_assert_true (copy->kind != effect->kind);
    g_assert_cmpstr (copy->kind, ==, "dot");
    g_assert_cmpstr (copy->aura_id, ==, "corruption");
    g_assert_cmpstr (copy->stat, ==, "shadow");
    g_assert_cmpfloat (copy->amount, ==, 12.5);
    g_assert_cmpfloat (copy->scale, ==, 0.2);
    g_assert_cmpfloat (copy->duration, ==, 18.0);
    g_assert_cmpfloat (copy->period, ==, 3.0);
    g_assert_cmpfloat (copy->radius, ==, 8.0);
    g_assert_cmpuint (copy->max_targets, ==, 5);

    lrg_ability_effect_free (effect);
    g_boxed_free (LRG_TYPE_ABILITY_EFFECT, copy);
    lrg_ability_effect_free (NULL);

    expect_critical ();
    g_assert_null (lrg_ability_effect_new (""));
    g_test_assert_expected_messages ();
}

/* ========================================================================== */
/*                                LrgAbilityDef                               */
/* ========================================================================== */

static void
test_def_defaults (void)
{
    g_autoptr(LrgAbilityDef) def = lrg_ability_def_new ("fireball");

    g_assert_cmpstr (lrg_ability_def_get_id (def), ==, "fireball");
    g_assert_null (lrg_ability_def_get_name (def));
    g_assert_null (lrg_ability_def_get_description (def));
    g_assert_null (lrg_ability_def_get_icon (def));
    g_assert_null (lrg_ability_def_get_class_id (def));
    g_assert_cmpuint (lrg_ability_def_get_min_level (def), ==, 1);
    g_assert_cmpuint (lrg_ability_def_get_rank (def), ==, 1);
    g_assert_null (lrg_ability_def_get_resource (def));
    g_assert_cmpuint (lrg_ability_def_get_cost (def), ==, 0);
    g_assert_cmpfloat (lrg_ability_def_get_cooldown (def), ==, 0.0);
    g_assert_null (lrg_ability_def_get_category (def));
    g_assert_cmpfloat (lrg_ability_def_get_category_cooldown (def), ==, 0.0);
    g_assert_true (lrg_ability_def_get_triggers_gcd (def));
    g_assert_cmpfloat (lrg_ability_def_get_cast_time (def), ==, 0.0);
    g_assert_false (lrg_ability_def_get_channel (def));
    g_assert_cmpfloat (lrg_ability_def_get_range (def), ==, 0.0);
    g_assert_false (lrg_ability_def_get_passive (def));
    g_assert_cmpint (lrg_ability_def_get_learn_source (def), ==, LRG_ABILITY_LEARN_SOURCE_TRAINER);
    g_assert_cmpuint (lrg_ability_def_get_trainer_cost (def), ==, 0);
    g_assert_null (lrg_ability_def_get_required_spec (def));
    g_assert_null (lrg_ability_def_get_required_talent (def));
    g_assert_null (lrg_ability_def_get_replaces (def));
    g_assert_null (lrg_ability_def_get_target (def));
    g_assert_cmpuint (lrg_ability_def_get_tags (def)->len, ==, 0);
    g_assert_cmpuint (lrg_ability_def_get_effects (def)->len, ==, 0);

    expect_critical ();
    g_assert_null (lrg_ability_def_new (""));
    g_test_assert_expected_messages ();
    expect_critical ();
    g_assert_null (lrg_ability_def_new (NULL));
    g_test_assert_expected_messages ();
}

static void
test_def_setters (void)
{
    g_autoptr(LrgAbilityDef) def = lrg_ability_def_new ("heal");

    lrg_ability_def_set_name (def, "Heal");
    lrg_ability_def_set_description (def, "Heals a friend");
    lrg_ability_def_set_icon (def, "icon_heal");
    lrg_ability_def_set_class_id (def, "priest");
    lrg_ability_def_set_min_level (def, 4);
    lrg_ability_def_set_rank (def, 2);
    lrg_ability_def_set_resource (def, "mana");
    lrg_ability_def_set_cost (def, 35);
    lrg_ability_def_set_cooldown (def, 6.0);
    lrg_ability_def_set_category (def, "heals");
    lrg_ability_def_set_category_cooldown (def, 1.5);
    lrg_ability_def_set_triggers_gcd (def, FALSE);
    lrg_ability_def_set_cast_time (def, 2.5);
    lrg_ability_def_set_channel (def, TRUE);
    lrg_ability_def_set_range (def, 40.0);
    lrg_ability_def_set_passive (def, TRUE);
    lrg_ability_def_set_learn_source (def, LRG_ABILITY_LEARN_SOURCE_QUEST);
    lrg_ability_def_set_trainer_cost (def, 900);
    lrg_ability_def_set_required_spec (def, "holy");
    lrg_ability_def_set_required_talent (def, "holy_t3");
    lrg_ability_def_set_replaces (def, "heal_r1");
    lrg_ability_def_set_target (def, "ally");

    g_assert_cmpstr (lrg_ability_def_get_name (def), ==, "Heal");
    g_assert_cmpstr (lrg_ability_def_get_description (def), ==, "Heals a friend");
    g_assert_cmpstr (lrg_ability_def_get_icon (def), ==, "icon_heal");
    g_assert_cmpstr (lrg_ability_def_get_class_id (def), ==, "priest");
    g_assert_cmpuint (lrg_ability_def_get_min_level (def), ==, 4);
    g_assert_cmpuint (lrg_ability_def_get_rank (def), ==, 2);
    g_assert_cmpstr (lrg_ability_def_get_resource (def), ==, "mana");
    g_assert_cmpuint (lrg_ability_def_get_cost (def), ==, 35);
    g_assert_cmpfloat (lrg_ability_def_get_cooldown (def), ==, 6.0);
    g_assert_cmpstr (lrg_ability_def_get_category (def), ==, "heals");
    g_assert_cmpfloat (lrg_ability_def_get_category_cooldown (def), ==, 1.5);
    g_assert_false (lrg_ability_def_get_triggers_gcd (def));
    g_assert_cmpfloat (lrg_ability_def_get_cast_time (def), ==, 2.5);
    g_assert_true (lrg_ability_def_get_channel (def));
    g_assert_cmpfloat (lrg_ability_def_get_range (def), ==, 40.0);
    g_assert_true (lrg_ability_def_get_passive (def));
    g_assert_cmpint (lrg_ability_def_get_learn_source (def), ==, LRG_ABILITY_LEARN_SOURCE_QUEST);
    g_assert_cmpuint (lrg_ability_def_get_trainer_cost (def), ==, 900);
    g_assert_cmpstr (lrg_ability_def_get_required_spec (def), ==, "holy");
    g_assert_cmpstr (lrg_ability_def_get_required_talent (def), ==, "holy_t3");
    g_assert_cmpstr (lrg_ability_def_get_replaces (def), ==, "heal_r1");
    g_assert_cmpstr (lrg_ability_def_get_target (def), ==, "ally");

    /* nullable strings can be cleared */
    lrg_ability_def_set_class_id (def, NULL);
    g_assert_null (lrg_ability_def_get_class_id (def));
}

static void
test_def_properties (void)
{
    g_autoptr(LrgAbilityDef) def = NULL;
    g_autofree gchar *id = NULL;
    g_autofree gchar *name = NULL;
    g_autofree gchar *class_id = NULL;
    g_autofree gchar *resource = NULL;
    g_autofree gchar *category = NULL;
    g_autofree gchar *spec = NULL;
    g_autofree gchar *talent = NULL;
    g_autofree gchar *replaces = NULL;
    g_autofree gchar *target = NULL;
    g_autofree gchar *icon = NULL;
    g_autofree gchar *description = NULL;
    guint min_level;
    guint rank;
    guint cost;
    guint trainer_cost;
    gdouble cooldown;
    gdouble category_cooldown;
    gdouble cast_time;
    gdouble range;
    gboolean gcd;
    gboolean channel;
    gboolean passive;
    LrgAbilityLearnSource source;

    def = g_object_new (LRG_TYPE_ABILITY_DEF,
                        "id", "frostbolt",
                        "name", "Frostbolt",
                        "description", "Chills",
                        "icon", "icon_frost",
                        "class-id", "mage",
                        "min-level", 4,
                        "rank", 3,
                        "resource", "mana",
                        "cost", 25,
                        "cooldown", 0.5,
                        "category", "frost",
                        "category-cooldown", 0.25,
                        "triggers-gcd", FALSE,
                        "cast-time", 2.0,
                        "channel", TRUE,
                        "range", 30.0,
                        "passive", TRUE,
                        "learn-source", LRG_ABILITY_LEARN_SOURCE_AUTO,
                        "trainer-cost", 10,
                        "required-spec", "frost_tree",
                        "required-talent", "ice_node",
                        "replaces", "frostbolt_r2",
                        "target", "enemy",
                        NULL);

    g_object_get (def,
                  "id", &id, "name", &name, "description", &description, "icon", &icon,
                  "class-id", &class_id, "min-level", &min_level, "rank", &rank,
                  "resource", &resource, "cost", &cost, "cooldown", &cooldown,
                  "category", &category, "category-cooldown", &category_cooldown,
                  "triggers-gcd", &gcd, "cast-time", &cast_time, "channel", &channel,
                  "range", &range, "passive", &passive, "learn-source", &source,
                  "trainer-cost", &trainer_cost, "required-spec", &spec,
                  "required-talent", &talent, "replaces", &replaces, "target", &target,
                  NULL);

    g_assert_cmpstr (id, ==, "frostbolt");
    g_assert_cmpstr (name, ==, "Frostbolt");
    g_assert_cmpstr (description, ==, "Chills");
    g_assert_cmpstr (icon, ==, "icon_frost");
    g_assert_cmpstr (class_id, ==, "mage");
    g_assert_cmpuint (min_level, ==, 4);
    g_assert_cmpuint (rank, ==, 3);
    g_assert_cmpstr (resource, ==, "mana");
    g_assert_cmpuint (cost, ==, 25);
    g_assert_cmpfloat (cooldown, ==, 0.5);
    g_assert_cmpstr (category, ==, "frost");
    g_assert_cmpfloat (category_cooldown, ==, 0.25);
    g_assert_false (gcd);
    g_assert_cmpfloat (cast_time, ==, 2.0);
    g_assert_true (channel);
    g_assert_cmpfloat (range, ==, 30.0);
    g_assert_true (passive);
    g_assert_cmpint (source, ==, LRG_ABILITY_LEARN_SOURCE_AUTO);
    g_assert_cmpuint (trainer_cost, ==, 10);
    g_assert_cmpstr (spec, ==, "frost_tree");
    g_assert_cmpstr (talent, ==, "ice_node");
    g_assert_cmpstr (replaces, ==, "frostbolt_r2");
    g_assert_cmpstr (target, ==, "enemy");
}

static void
test_def_notify (void)
{
    g_autoptr(LrgAbilityDef) def = lrg_ability_def_new ("strike");
    guint any = 0;

    g_signal_connect (def, "notify", G_CALLBACK (count_notify), &any);

    lrg_ability_def_set_name (def, "Strike");
    g_assert_cmpuint (any, ==, 1);
    lrg_ability_def_set_name (def, "Strike");
    g_assert_cmpuint (any, ==, 1);
    lrg_ability_def_set_cooldown (def, 0.0);
    g_assert_cmpuint (any, ==, 1);
    lrg_ability_def_set_cooldown (def, 3.0);
    g_assert_cmpuint (any, ==, 2);
    lrg_ability_def_set_triggers_gcd (def, TRUE);
    g_assert_cmpuint (any, ==, 2);
    lrg_ability_def_set_triggers_gcd (def, FALSE);
    g_assert_cmpuint (any, ==, 3);
    lrg_ability_def_set_learn_source (def, LRG_ABILITY_LEARN_SOURCE_TRAINER);
    g_assert_cmpuint (any, ==, 3);
    lrg_ability_def_set_learn_source (def, LRG_ABILITY_LEARN_SOURCE_ITEM);
    g_assert_cmpuint (any, ==, 4);
    g_object_set (def, "cost", 10, NULL);
    g_assert_cmpuint (any, ==, 5);
    g_object_set (def, "cost", 10, NULL);
    g_assert_cmpuint (any, ==, 5);
}

static void
test_def_setter_rejections (void)
{
    g_autoptr(LrgAbilityDef) def = lrg_ability_def_new ("strike");

    lrg_ability_def_set_cooldown (def, 5.0);
    lrg_ability_def_set_range (def, 5.0);

    expect_critical ();
    lrg_ability_def_set_cooldown (def, NAN);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_ability_def_set_cooldown (def, -1.0);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_ability_def_set_cooldown (def, LRG_ABILITY_MAX_COOLDOWN + 1.0);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_ability_def_set_category_cooldown (def, INFINITY);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_ability_def_set_cast_time (def, -0.5);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_ability_def_set_range (def, NAN);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_ability_def_set_rank (def, 0);
    g_test_assert_expected_messages ();
    expect_critical ();
    lrg_ability_def_set_learn_source (def, (LrgAbilityLearnSource) 99);
    g_test_assert_expected_messages ();

    g_assert_cmpfloat (lrg_ability_def_get_cooldown (def), ==, 5.0);
    g_assert_cmpfloat (lrg_ability_def_get_category_cooldown (def), ==, 0.0);
    g_assert_cmpfloat (lrg_ability_def_get_cast_time (def), ==, 0.0);
    g_assert_cmpfloat (lrg_ability_def_get_range (def), ==, 5.0);
    g_assert_cmpuint (lrg_ability_def_get_rank (def), ==, 1);
    g_assert_cmpint (lrg_ability_def_get_learn_source (def), ==, LRG_ABILITY_LEARN_SOURCE_TRAINER);

    /* boundary: exactly the maximum is accepted */
    lrg_ability_def_set_cooldown (def, LRG_ABILITY_MAX_COOLDOWN);
    g_assert_cmpfloat (lrg_ability_def_get_cooldown (def), ==, LRG_ABILITY_MAX_COOLDOWN);
}

static void
test_def_tags_effects (void)
{
    g_autoptr(LrgAbilityDef) def = lrg_ability_def_new ("flame_strike");
    GPtrArray *tags;
    GPtrArray *effects;
    LrgAbilityEffect *effect;

    lrg_ability_def_add_tag (def, "fire");
    lrg_ability_def_add_tag (def, "aoe");
    lrg_ability_def_add_tag (def, "fire");
    tags = lrg_ability_def_get_tags (def);
    g_assert_cmpuint (tags->len, ==, 2);
    g_assert_cmpstr (g_ptr_array_index (tags, 0), ==, "fire");
    g_assert_cmpstr (g_ptr_array_index (tags, 1), ==, "aoe");
    g_assert_true (lrg_ability_def_has_tag (def, "aoe"));
    g_assert_false (lrg_ability_def_has_tag (def, "frost"));
    g_assert_false (lrg_ability_def_has_tag (def, NULL));

    expect_critical ();
    lrg_ability_def_add_tag (def, "");
    g_test_assert_expected_messages ();
    g_assert_cmpuint (tags->len, ==, 2);

    effect = lrg_ability_effect_new ("damage");
    effect->amount = 40.0;
    lrg_ability_def_add_effect (def, effect);
    effect = lrg_ability_effect_new ("dot");
    effect->aura_id = g_strdup ("burning");
    lrg_ability_def_add_effect (def, effect);

    effects = lrg_ability_def_get_effects (def);
    g_assert_cmpuint (effects->len, ==, 2);
    g_assert_cmpstr (((LrgAbilityEffect *) g_ptr_array_index (effects, 0))->kind, ==, "damage");
    g_assert_cmpfloat (((LrgAbilityEffect *) g_ptr_array_index (effects, 0))->amount, ==, 40.0);
    g_assert_cmpstr (((LrgAbilityEffect *) g_ptr_array_index (effects, 1))->aura_id, ==, "burning");
}

/* A subclass whose can_use fails without a caster. */
#define TEST_TYPE_GATED_ABILITY (test_gated_ability_get_type ())
G_DECLARE_FINAL_TYPE (TestGatedAbility, test_gated_ability, TEST, GATED_ABILITY, LrgAbilityDef)

struct _TestGatedAbility
{
    LrgAbilityDef parent_instance;
};

G_DEFINE_TYPE (TestGatedAbility, test_gated_ability, LRG_TYPE_ABILITY_DEF)

static gboolean
test_gated_ability_can_use (LrgAbilityDef  *self,
                            gpointer        caster,
                            GError        **error)
{
    (void) self;
    if (caster == NULL)
    {
        g_set_error_literal (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT,
                             "needs a caster");
        return FALSE;
    }
    return TRUE;
}

static void
test_gated_ability_class_init (TestGatedAbilityClass *klass)
{
    LRG_ABILITY_DEF_CLASS (klass)->can_use = test_gated_ability_can_use;
}

static void
test_gated_ability_init (TestGatedAbility *self)
{
    (void) self;
}

static void
test_def_can_use (void)
{
    g_autoptr(LrgAbilityDef) plain = lrg_ability_def_new ("plain");
    g_autoptr(LrgAbilityDef) gated = NULL;
    g_autoptr(GError) error = NULL;
    gint caster = 1;

    g_assert_true (lrg_ability_def_can_use (plain, NULL, &error));
    g_assert_no_error (error);

    gated = g_object_new (TEST_TYPE_GATED_ABILITY, "id", "gated", NULL);
    g_assert_true (LRG_IS_ABILITY_DEF (gated));
    g_assert_false (lrg_ability_def_can_use (gated, NULL, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);
    g_assert_true (lrg_ability_def_can_use (gated, &caster, &error));
    g_assert_no_error (error);
    /* subclasses still get the base properties */
    g_assert_cmpuint (lrg_ability_def_get_min_level (gated), ==, 1);
}

static void
test_def_eligibility (void)
{
    g_autoptr(LrgAbilityDef) any_class = lrg_ability_def_new ("first_aid");
    g_autoptr(LrgAbilityDef) warrior = lrg_ability_def_new ("charge");
    g_autoptr(LrgAbilityDef) spec = lrg_ability_def_new ("mortal_strike");

    /* NULL class: any class (including none) */
    g_assert_true (lrg_ability_def_is_eligible (any_class, "mage", 1, NULL));
    g_assert_true (lrg_ability_def_is_eligible (any_class, NULL, 1, NULL));

    lrg_ability_def_set_class_id (warrior, "warrior");
    lrg_ability_def_set_min_level (warrior, 4);
    g_assert_true (lrg_ability_def_is_eligible (warrior, "warrior", 4, NULL));
    g_assert_true (lrg_ability_def_is_eligible (warrior, "warrior", 60, "arms"));
    g_assert_false (lrg_ability_def_is_eligible (warrior, "warrior", 3, NULL));
    g_assert_false (lrg_ability_def_is_eligible (warrior, "mage", 10, NULL));
    g_assert_false (lrg_ability_def_is_eligible (warrior, NULL, 10, NULL));

    lrg_ability_def_set_class_id (spec, "warrior");
    lrg_ability_def_set_required_spec (spec, "arms");
    g_assert_true (lrg_ability_def_is_eligible (spec, "warrior", 1, "arms"));
    g_assert_false (lrg_ability_def_is_eligible (spec, "warrior", 1, "fury"));
    g_assert_false (lrg_ability_def_is_eligible (spec, "warrior", 1, NULL));

    /* level 0 abilities are eligible at any level */
    lrg_ability_def_set_min_level (any_class, 0);
    g_assert_true (lrg_ability_def_is_eligible (any_class, NULL, 0, NULL));
}

/* ========================================================================== */
/*                                LrgSpellbook                                */
/* ========================================================================== */

static void
test_book_learn_forget (void)
{
    g_autoptr(LrgSpellbook) book = lrg_spellbook_new ();
    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) known = NULL;

    g_assert_cmpuint (lrg_spellbook_get_known_count (book), ==, 0);
    g_assert_true (lrg_spellbook_learn (book, "fireball", &error));
    g_assert_no_error (error);
    g_assert_true (lrg_spellbook_learn (book, "arcane_missiles", NULL));
    g_assert_true (lrg_spellbook_learn (book, "blink", NULL));
    g_assert_true (lrg_spellbook_knows (book, "blink"));
    g_assert_false (lrg_spellbook_knows (book, "frostbolt"));
    g_assert_false (lrg_spellbook_knows (book, NULL));
    g_assert_cmpuint (lrg_spellbook_get_known_count (book), ==, 3);

    known = lrg_spellbook_get_known (book);
    g_assert_cmpuint (known->len, ==, 3);
    g_assert_cmpstr (g_ptr_array_index (known, 0), ==, "arcane_missiles");
    g_assert_cmpstr (g_ptr_array_index (known, 1), ==, "blink");
    g_assert_cmpstr (g_ptr_array_index (known, 2), ==, "fireball");

    g_assert_true (lrg_spellbook_forget (book, "blink"));
    g_assert_false (lrg_spellbook_forget (book, "blink"));
    g_assert_false (lrg_spellbook_forget (book, NULL));
    g_assert_false (lrg_spellbook_knows (book, "blink"));
    g_assert_cmpuint (lrg_spellbook_get_known_count (book), ==, 2);
}

static void
test_book_learn_rejections (void)
{
    g_autoptr(LrgSpellbook) book = lrg_spellbook_new ();
    g_autoptr(GError) error = NULL;
    g_autofree gchar *id128 = make_long_id (128);
    g_autofree gchar *id129 = make_long_id (129);
    guint i;

    g_assert_true (lrg_spellbook_learn (book, "fireball", NULL));

    g_assert_false (lrg_spellbook_learn (book, "fireball", &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_DUPLICATE);
    g_clear_error (&error);

    g_assert_false (lrg_spellbook_learn (book, "", &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    g_assert_false (lrg_spellbook_learn (book, NULL, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    g_assert_false (lrg_spellbook_learn (book, id129, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    g_assert_false (lrg_spellbook_learn (book, "bad\xff\xfeid", &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);

    g_assert_cmpuint (lrg_spellbook_get_known_count (book), ==, 1);

    /* exactly 128 bytes is fine */
    g_assert_true (lrg_spellbook_learn (book, id128, NULL));

    /* fill to the limit, then one more is LIMIT */
    for (i = lrg_spellbook_get_known_count (book); i < LRG_SPELLBOOK_MAX_KNOWN; i++)
    {
        g_autofree gchar *id = g_strdup_printf ("spell_%04u", i);

        g_assert_true (lrg_spellbook_learn (book, id, NULL));
    }
    g_assert_cmpuint (lrg_spellbook_get_known_count (book), ==, LRG_SPELLBOOK_MAX_KNOWN);
    g_assert_false (lrg_spellbook_learn (book, "one_too_many", &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT);
    g_clear_error (&error);
    g_assert_false (lrg_spellbook_knows (book, "one_too_many"));
    g_assert_cmpuint (lrg_spellbook_get_known_count (book), ==, LRG_SPELLBOOK_MAX_KNOWN);
}

/* Fixture with a small class ability list. */
typedef struct
{
    GPtrArray    *defs;
    LrgSpellbook *book;
} BookFixture;

static LrgAbilityDef *
add_def (GPtrArray             *defs,
         const gchar           *id,
         const gchar           *class_id,
         guint                  min_level,
         const gchar           *spec,
         LrgAbilityLearnSource  source)
{
    LrgAbilityDef *def = g_object_new (LRG_TYPE_ABILITY_DEF,
                                       "id", id,
                                       "class-id", class_id,
                                       "min-level", min_level,
                                       "required-spec", spec,
                                       "learn-source", source,
                                       NULL);

    g_ptr_array_add (defs, def);
    return def;
}

static void
book_fixture_setup (BookFixture   *fixture,
                    gconstpointer  data)
{
    (void) data;

    fixture->defs = g_ptr_array_new_with_free_func (g_object_unref);
    fixture->book = lrg_spellbook_new ();

    /* deliberately unsorted input */
    add_def (fixture->defs, "sunder", "warrior", 10, NULL, LRG_ABILITY_LEARN_SOURCE_TRAINER);
    add_def (fixture->defs, "strike", "warrior", 1, NULL, LRG_ABILITY_LEARN_SOURCE_AUTO);
    add_def (fixture->defs, "battle_stance", "warrior", 1, NULL, LRG_ABILITY_LEARN_SOURCE_AUTO);
    add_def (fixture->defs, "rend", "warrior", 4, NULL, LRG_ABILITY_LEARN_SOURCE_TRAINER);
    add_def (fixture->defs, "charge", "warrior", 4, NULL, LRG_ABILITY_LEARN_SOURCE_TRAINER);
    add_def (fixture->defs, "mortal_strike", "warrior", 20, "arms", LRG_ABILITY_LEARN_SOURCE_AUTO);
    add_def (fixture->defs, "fireball", "mage", 1, NULL, LRG_ABILITY_LEARN_SOURCE_AUTO);
    add_def (fixture->defs, "first_aid", NULL, 1, NULL, LRG_ABILITY_LEARN_SOURCE_TRAINER);
    add_def (fixture->defs, "hearthstone", NULL, 1, NULL, LRG_ABILITY_LEARN_SOURCE_AUTO);
    add_def (fixture->defs, "relic_blast", "warrior", 1, NULL, LRG_ABILITY_LEARN_SOURCE_ITEM);
    lrg_ability_def_set_passive (add_def (fixture->defs, "toughness", "warrior", 8, NULL,
                                          LRG_ABILITY_LEARN_SOURCE_TRAINER), TRUE);
}

static void
book_fixture_teardown (BookFixture   *fixture,
                       gconstpointer  data)
{
    (void) data;

    g_clear_pointer (&fixture->defs, g_ptr_array_unref);
    g_clear_object (&fixture->book);
}

static void
assert_def_ids (GPtrArray   *defs,
                const gchar *expected)
{
    g_auto(GStrv) ids = g_strsplit (expected, ",", -1);
    guint i;

    g_assert_cmpuint (defs->len, ==, g_strv_length (ids));
    for (i = 0; i < defs->len; i++)
        g_assert_cmpstr (lrg_ability_def_get_id (g_ptr_array_index (defs, i)), ==, ids[i]);
}

static void
test_book_learnable (BookFixture   *fixture,
                     gconstpointer  data)
{
    g_autoptr(GPtrArray) trainer = NULL;
    g_autoptr(GPtrArray) trainer_low = NULL;
    g_autoptr(GPtrArray) item = NULL;
    g_autoptr(GPtrArray) auto_arms = NULL;
    g_autoptr(GPtrArray) auto_fury = NULL;
    g_autoptr(GPtrArray) after_learning = NULL;

    (void) data;

    /* trainer list at level 10: sorted by (min-level, id); passive included */
    trainer = lrg_spellbook_get_learnable (fixture->book, fixture->defs, "warrior", 10, NULL,
                                           LRG_ABILITY_LEARN_SOURCE_TRAINER);
    assert_def_ids (trainer, "first_aid,charge,rend,toughness,sunder");

    trainer_low = lrg_spellbook_get_learnable (fixture->book, fixture->defs, "warrior", 3, NULL,
                                               LRG_ABILITY_LEARN_SOURCE_TRAINER);
    assert_def_ids (trainer_low, "first_aid");

    item = lrg_spellbook_get_learnable (fixture->book, fixture->defs, "warrior", 60, NULL,
                                        LRG_ABILITY_LEARN_SOURCE_ITEM);
    assert_def_ids (item, "relic_blast");

    auto_arms = lrg_spellbook_get_learnable (fixture->book, fixture->defs, "warrior", 20, "arms",
                                             LRG_ABILITY_LEARN_SOURCE_AUTO);
    assert_def_ids (auto_arms, "battle_stance,hearthstone,strike,mortal_strike");

    auto_fury = lrg_spellbook_get_learnable (fixture->book, fixture->defs, "warrior", 20, "fury",
                                             LRG_ABILITY_LEARN_SOURCE_AUTO);
    assert_def_ids (auto_fury, "battle_stance,hearthstone,strike");

    /* known abilities drop out */
    g_assert_true (lrg_spellbook_learn (fixture->book, "charge", NULL));
    after_learning = lrg_spellbook_get_learnable (fixture->book, fixture->defs, "warrior", 10, NULL,
                                                  LRG_ABILITY_LEARN_SOURCE_TRAINER);
    assert_def_ids (after_learning, "first_aid,rend,toughness,sunder");
}

static void
test_book_learnable_duplicate_ids (BookFixture   *fixture,
                                   gconstpointer  data)
{
    g_autoptr(GPtrArray) learnable = NULL;
    g_autoptr(GPtrArray) empty = g_ptr_array_new ();
    g_autoptr(GPtrArray) none = NULL;

    (void) data;

    /* a second definition with the same id is offered only once */
    add_def (fixture->defs, "strike", "warrior", 1, NULL, LRG_ABILITY_LEARN_SOURCE_AUTO);
    learnable = lrg_spellbook_get_learnable (fixture->book, fixture->defs, "warrior", 1, NULL,
                                             LRG_ABILITY_LEARN_SOURCE_AUTO);
    assert_def_ids (learnable, "battle_stance,hearthstone,strike");
    g_assert_cmpuint (lrg_spellbook_learn_automatic (fixture->book, fixture->defs, "warrior", 1, NULL), ==, 3);

    none = lrg_spellbook_get_learnable (fixture->book, empty, "warrior", 1, NULL,
                                        LRG_ABILITY_LEARN_SOURCE_AUTO);
    g_assert_cmpuint (none->len, ==, 0);
}

static void
test_book_learn_automatic (BookFixture   *fixture,
                           gconstpointer  data)
{
    g_autoptr(GPtrArray) known = NULL;

    (void) data;

    g_assert_cmpuint (lrg_spellbook_learn_automatic (fixture->book, fixture->defs, "warrior", 1, NULL), ==, 3);
    g_assert_true (lrg_spellbook_knows (fixture->book, "strike"));
    g_assert_true (lrg_spellbook_knows (fixture->book, "battle_stance"));
    g_assert_true (lrg_spellbook_knows (fixture->book, "hearthstone"));
    g_assert_false (lrg_spellbook_knows (fixture->book, "fireball"));
    g_assert_false (lrg_spellbook_knows (fixture->book, "charge"));

    /* running again learns nothing new */
    g_assert_cmpuint (lrg_spellbook_learn_automatic (fixture->book, fixture->defs, "warrior", 1, NULL), ==, 0);

    /* level 20 without the spec still skips the spec ability */
    g_assert_cmpuint (lrg_spellbook_learn_automatic (fixture->book, fixture->defs, "warrior", 20, "fury"), ==, 0);
    g_assert_cmpuint (lrg_spellbook_learn_automatic (fixture->book, fixture->defs, "warrior", 20, "arms"), ==, 1);
    g_assert_true (lrg_spellbook_knows (fixture->book, "mortal_strike"));

    known = lrg_spellbook_get_known (fixture->book);
    g_assert_cmpuint (known->len, ==, 4);
}

static void
test_book_bar (void)
{
    g_autoptr(LrgSpellbook) book = lrg_spellbook_new ();
    g_autoptr(GError) error = NULL;

    g_assert_cmpuint (lrg_spellbook_get_bar_size (book), ==, LRG_SPELLBOOK_DEFAULT_BAR_SIZE);
    g_assert_true (lrg_spellbook_learn (book, "fireball", NULL));
    g_assert_true (lrg_spellbook_learn (book, "blink", NULL));

    g_assert_true (lrg_spellbook_bind (book, 0, "fireball", &error));
    g_assert_no_error (error);
    g_assert_true (lrg_spellbook_bind (book, 11, "blink", NULL));
    g_assert_true (lrg_spellbook_bind (book, 5, "fireball", NULL));
    g_assert_cmpstr (lrg_spellbook_get_binding (book, 0), ==, "fireball");
    g_assert_cmpstr (lrg_spellbook_get_binding (book, 5), ==, "fireball");
    g_assert_cmpstr (lrg_spellbook_get_binding (book, 11), ==, "blink");
    g_assert_null (lrg_spellbook_get_binding (book, 1));
    g_assert_null (lrg_spellbook_get_binding (book, 12));
    g_assert_null (lrg_spellbook_get_binding (book, G_MAXUINT));

    /* rebinding replaces */
    g_assert_true (lrg_spellbook_bind (book, 0, "blink", NULL));
    g_assert_cmpstr (lrg_spellbook_get_binding (book, 0), ==, "blink");

    /* clearing a slot */
    g_assert_true (lrg_spellbook_bind (book, 0, NULL, NULL));
    g_assert_null (lrg_spellbook_get_binding (book, 0));
    g_assert_true (lrg_spellbook_bind (book, 0, NULL, NULL));

    /* rejections leave the bar unchanged */
    g_assert_false (lrg_spellbook_bind (book, 12, "fireball", &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    g_assert_false (lrg_spellbook_bind (book, 5, "frostbolt", &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_NOT_FOUND);
    g_clear_error (&error);
    g_assert_cmpstr (lrg_spellbook_get_binding (book, 5), ==, "fireball");

    g_assert_false (lrg_spellbook_set_bar_size (book, 0, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    g_assert_false (lrg_spellbook_set_bar_size (book, LRG_SPELLBOOK_MAX_BAR_SIZE + 1, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    g_assert_cmpuint (lrg_spellbook_get_bar_size (book), ==, 12);
    g_assert_cmpstr (lrg_spellbook_get_binding (book, 11), ==, "blink");

    /* shrinking drops trailing slots; growing again leaves them empty */
    g_assert_true (lrg_spellbook_set_bar_size (book, 6, NULL));
    g_assert_cmpuint (lrg_spellbook_get_bar_size (book), ==, 6);
    g_assert_null (lrg_spellbook_get_binding (book, 11));
    g_assert_cmpstr (lrg_spellbook_get_binding (book, 5), ==, "fireball");
    g_assert_true (lrg_spellbook_set_bar_size (book, LRG_SPELLBOOK_MAX_BAR_SIZE, NULL));
    g_assert_null (lrg_spellbook_get_binding (book, 11));
    g_assert_true (lrg_spellbook_bind (book, 47, "blink", NULL));
    g_assert_cmpstr (lrg_spellbook_get_binding (book, 47), ==, "blink");
    g_assert_true (lrg_spellbook_set_bar_size (book, 1, NULL));
    g_assert_null (lrg_spellbook_get_binding (book, 5));
    g_assert_false (lrg_spellbook_bind (book, 1, "blink", NULL));

    /* forgetting clears every slot bound to that ability */
    g_assert_true (lrg_spellbook_set_bar_size (book, 12, NULL));
    g_assert_true (lrg_spellbook_bind (book, 2, "fireball", NULL));
    g_assert_true (lrg_spellbook_bind (book, 3, "blink", NULL));
    g_assert_true (lrg_spellbook_bind (book, 9, "fireball", NULL));
    g_assert_true (lrg_spellbook_forget (book, "fireball"));
    g_assert_null (lrg_spellbook_get_binding (book, 2));
    g_assert_null (lrg_spellbook_get_binding (book, 9));
    g_assert_cmpstr (lrg_spellbook_get_binding (book, 3), ==, "blink");
}

/* ------------------------------ persistence ------------------------------- */

static GVariant *
build_book_variant (const gchar * const *known,
                    guint32              bar_size,
                    const gchar * const *slots)
{
    GVariantBuilder kb;
    GVariantBuilder sb;
    guint i;

    g_variant_builder_init (&kb, G_VARIANT_TYPE_STRING_ARRAY);
    for (i = 0; known != NULL && known[i] != NULL; i++)
        g_variant_builder_add (&kb, "s", known[i]);
    g_variant_builder_init (&sb, G_VARIANT_TYPE_STRING_ARRAY);
    for (i = 0; slots != NULL && slots[i] != NULL; i++)
        g_variant_builder_add (&sb, "s", slots[i]);
    return g_variant_ref_sink (g_variant_new ("(asuas)", &kb, bar_size, &sb));
}

static void
assert_book_rejected (GVariant *variant)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgSpellbook) book = NULL;

    book = lrg_spellbook_new_from_variant (variant, &error);
    g_assert_null (book);
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
}

static void
test_book_variant_round_trip (void)
{
    g_autoptr(LrgSpellbook) book = lrg_spellbook_new ();
    g_autoptr(LrgSpellbook) restored = NULL;
    g_autoptr(GVariant) first = NULL;
    g_autoptr(GVariant) second = NULL;
    g_autoptr(GError) error = NULL;
    const gchar *expected_known[] = { "blink", "fireball", "frostbolt", NULL };

    g_assert_true (lrg_spellbook_learn (book, "frostbolt", NULL));
    g_assert_true (lrg_spellbook_learn (book, "fireball", NULL));
    g_assert_true (lrg_spellbook_learn (book, "blink", NULL));
    g_assert_true (lrg_spellbook_set_bar_size (book, 4, NULL));
    g_assert_true (lrg_spellbook_bind (book, 0, "fireball", NULL));
    g_assert_true (lrg_spellbook_bind (book, 3, "blink", NULL));
    g_assert_true (lrg_spellbook_bind (book, 2, "fireball", NULL));

    first = lrg_spellbook_to_variant (book);
    g_assert_false (g_variant_is_floating (first));
    g_assert_cmpstr (g_variant_get_type_string (first), ==, LRG_SPELLBOOK_VARIANT_TYPE);
    {
        g_autoptr(GVariant) expected = NULL;
        const gchar *slots[] = { "fireball", "", "fireball", "blink", NULL };

        expected = build_book_variant (expected_known, 4, slots);
        g_assert_true (g_variant_equal (first, expected));
    }

    restored = lrg_spellbook_new_from_variant (first, &error);
    g_assert_no_error (error);
    g_assert_nonnull (restored);
    g_assert_cmpuint (lrg_spellbook_get_known_count (restored), ==, 3);
    g_assert_cmpuint (lrg_spellbook_get_bar_size (restored), ==, 4);
    g_assert_cmpstr (lrg_spellbook_get_binding (restored, 0), ==, "fireball");
    g_assert_null (lrg_spellbook_get_binding (restored, 1));
    g_assert_cmpstr (lrg_spellbook_get_binding (restored, 3), ==, "blink");

    second = lrg_spellbook_to_variant (restored);
    g_assert_true (g_variant_equal (first, second));

    /* an empty book round-trips too */
    {
        g_autoptr(LrgSpellbook) empty = lrg_spellbook_new ();
        g_autoptr(GVariant) v1 = lrg_spellbook_to_variant (empty);
        g_autoptr(LrgSpellbook) back = lrg_spellbook_new_from_variant (v1, NULL);
        g_autoptr(GVariant) v2 = NULL;

        g_assert_nonnull (back);
        v2 = lrg_spellbook_to_variant (back);
        g_assert_true (g_variant_equal (v1, v2));
    }

    /* unsorted known ids are accepted and re-sorted on output */
    {
        const gchar *unsorted[] = { "zeta", "alpha", NULL };
        const gchar *slots[] = { "zeta", NULL };
        g_autoptr(GVariant) v = build_book_variant (unsorted, 1, slots);
        g_autoptr(LrgSpellbook) back = lrg_spellbook_new_from_variant (v, NULL);
        g_autoptr(GPtrArray) known = NULL;

        g_assert_nonnull (back);
        known = lrg_spellbook_get_known (back);
        g_assert_cmpstr (g_ptr_array_index (known, 0), ==, "alpha");
    }
}

static void
test_book_variant_hostile (void)
{
    const gchar *known[] = { "fireball", "blink", NULL };
    const gchar *four_empty[] = { "", "", "", "", NULL };
    g_autofree gchar *long_id = make_long_id (129);
    g_autoptr(GVariant) v = NULL;

    /* wrong type strings */
    v = g_variant_ref_sink (g_variant_new_parsed ("(@as [], uint32 4)"));
    assert_book_rejected (v);
    g_clear_pointer (&v, g_variant_unref);
    v = g_variant_ref_sink (g_variant_new_string ("book"));
    assert_book_rejected (v);
    g_clear_pointer (&v, g_variant_unref);

    /* invalid UTF-8 in a known id (non-normal form) */
    {
        const gchar *slots[] = { "", NULL };
        g_autoptr(GVariant) good = build_book_variant (known, 1, slots);

        v = corrupt_string (good, "blink");
        assert_book_rejected (v);
        g_clear_pointer (&v, g_variant_unref);
    }

    /* empty, overlong and duplicate ids */
    {
        const gchar *empty_id[] = { "fireball", "", NULL };
        const gchar *long_ids[] = { long_id, NULL };
        const gchar *dupes[] = { "fireball", "fireball", NULL };

        v = build_book_variant (empty_id, 4, four_empty);
        assert_book_rejected (v);
        g_clear_pointer (&v, g_variant_unref);
        v = build_book_variant (long_ids, 4, four_empty);
        assert_book_rejected (v);
        g_clear_pointer (&v, g_variant_unref);
        v = build_book_variant (dupes, 4, four_empty);
        assert_book_rejected (v);
        g_clear_pointer (&v, g_variant_unref);
    }

    /* oversized known array */
    {
        g_autoptr(GPtrArray) ids = g_ptr_array_new_with_free_func (g_free);
        guint i;

        for (i = 0; i < LRG_SPELLBOOK_MAX_KNOWN + 1; i++)
            g_ptr_array_add (ids, g_strdup_printf ("s%04u", i));
        g_ptr_array_add (ids, NULL);
        v = build_book_variant ((const gchar * const *) ids->pdata, 4, four_empty);
        assert_book_rejected (v);
        g_clear_pointer (&v, g_variant_unref);

        /* exactly the maximum is accepted */
        g_ptr_array_remove_index (ids, LRG_SPELLBOOK_MAX_KNOWN);
        v = build_book_variant ((const gchar * const *) ids->pdata, 4, four_empty);
        {
            g_autoptr(LrgSpellbook) ok = lrg_spellbook_new_from_variant (v, NULL);

            g_assert_nonnull (ok);
            g_assert_cmpuint (lrg_spellbook_get_known_count (ok), ==, LRG_SPELLBOOK_MAX_KNOWN);
        }
        g_clear_pointer (&v, g_variant_unref);
    }

    /* bar size out of range */
    v = build_book_variant (known, 0, NULL);
    assert_book_rejected (v);
    g_clear_pointer (&v, g_variant_unref);
    {
        g_autoptr(GPtrArray) slots = g_ptr_array_new ();
        guint i;

        for (i = 0; i < LRG_SPELLBOOK_MAX_BAR_SIZE + 1; i++)
            g_ptr_array_add (slots, (gpointer) "");
        g_ptr_array_add (slots, NULL);
        v = build_book_variant (known, LRG_SPELLBOOK_MAX_BAR_SIZE + 1,
                                (const gchar * const *) slots->pdata);
        assert_book_rejected (v);
        g_clear_pointer (&v, g_variant_unref);
    }

    /* slot count does not match bar size */
    v = build_book_variant (known, 5, four_empty);
    assert_book_rejected (v);
    g_clear_pointer (&v, g_variant_unref);
    v = build_book_variant (known, 3, four_empty);
    assert_book_rejected (v);
    g_clear_pointer (&v, g_variant_unref);

    /* bound slot names an unknown ability */
    {
        const gchar *slots[] = { "fireball", "frostbolt", NULL };

        v = build_book_variant (known, 2, slots);
        assert_book_rejected (v);
        g_clear_pointer (&v, g_variant_unref);
    }
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/ability/effect/new-copy-free", test_effect_new_copy_free);
    g_test_add_func ("/ability/def/defaults", test_def_defaults);
    g_test_add_func ("/ability/def/setters", test_def_setters);
    g_test_add_func ("/ability/def/properties", test_def_properties);
    g_test_add_func ("/ability/def/notify", test_def_notify);
    g_test_add_func ("/ability/def/setter-rejections", test_def_setter_rejections);
    g_test_add_func ("/ability/def/tags-effects", test_def_tags_effects);
    g_test_add_func ("/ability/def/can-use", test_def_can_use);
    g_test_add_func ("/ability/def/eligibility", test_def_eligibility);

    g_test_add_func ("/spellbook/learn-forget", test_book_learn_forget);
    g_test_add_func ("/spellbook/learn-rejections", test_book_learn_rejections);
    g_test_add ("/spellbook/learnable", BookFixture, NULL,
                book_fixture_setup, test_book_learnable, book_fixture_teardown);
    g_test_add ("/spellbook/learnable-duplicate-ids", BookFixture, NULL,
                book_fixture_setup, test_book_learnable_duplicate_ids, book_fixture_teardown);
    g_test_add ("/spellbook/learn-automatic", BookFixture, NULL,
                book_fixture_setup, test_book_learn_automatic, book_fixture_teardown);
    g_test_add_func ("/spellbook/bar", test_book_bar);
    g_test_add_func ("/spellbook/variant/round-trip", test_book_variant_round_trip);
    g_test_add_func ("/spellbook/variant/hostile", test_book_variant_hostile);

    return g_test_run ();
}
