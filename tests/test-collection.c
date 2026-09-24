/* test-collection.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the collection module: collectible / mount / pet
 * definitions, the owned collection with its persistence contract, and the
 * companion brain decision table.
 */

#include <glib.h>
#include <glib-object.h>
#include <math.h>
#include <string.h>

#include "lrg-enums.h"
#include "collection/lrg-collectible-def.h"
#include "collection/lrg-mount-def.h"
#include "collection/lrg-pet-def.h"
#include "collection/lrg-collection.h"
#include "collection/lrg-companion-brain.h"

#define EPS (1e-9)

/* ========================================================================== */
/*                                 Helpers                                    */
/* ========================================================================== */

static void
count_notify (GObject    *object,
              GParamSpec *pspec,
              gpointer    user_data)
{
    guint *counter = user_data;

    (void)object;
    (void)pspec;
    (*counter)++;
}

/* snapshot:
 * Serialises a collection so later calls can assert it is unchanged. */
static GVariant *
snapshot (LrgCollection *collection)
{
    return lrg_collection_to_variant (collection);
}

static void
assert_unchanged (LrgCollection *collection,
                  GVariant      *before)
{
    g_autoptr(GVariant) after = lrg_collection_to_variant (collection);

    g_assert_true (g_variant_equal (before, after));
}

static void
assert_restore_invalid (GVariant *variant)
{
    g_autoptr(GError)        error = NULL;
    g_autoptr(LrgCollection) restored = NULL;

    restored = lrg_collection_new_from_variant (variant, &error);
    g_assert_null (restored);
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
}

/* build_variant:
 * Builds a (a(usb)a(us)) from parallel arrays; returned non-floating. */
static GVariant *
build_variant (guint              n_entries,
               const guint       *kinds,
               const gchar      **ids,
               const gboolean    *favs,
               guint              n_active,
               const guint       *active_kinds,
               const gchar      **active_ids)
{
    GVariantBuilder entries;
    GVariantBuilder actives;
    guint           i;

    g_variant_builder_init (&entries, G_VARIANT_TYPE ("a(usb)"));
    g_variant_builder_init (&actives, G_VARIANT_TYPE ("a(us)"));
    for (i = 0; i < n_entries; i++)
        g_variant_builder_add (&entries, "(usb)", kinds[i], ids[i], favs[i]);
    for (i = 0; i < n_active; i++)
        g_variant_builder_add (&actives, "(us)", active_kinds[i], active_ids[i]);

    return g_variant_ref_sink (g_variant_new ("(a(usb)a(us))", &entries, &actives));
}

/* ========================================================================== */
/*                            LrgCollectibleDef                               */
/* ========================================================================== */

static void
test_collectible_def_defaults (void)
{
    g_autoptr(LrgCollectibleDef) def = NULL;
    g_autofree gchar            *id = NULL;
    LrgCollectibleKind           kind;
    LrgCollectibleRarity         rarity;
    guint                        level;

    def = lrg_collectible_def_new ("title_champion", LRG_COLLECTIBLE_KIND_TITLE);
    g_assert_nonnull (def);
    g_assert_true (LRG_IS_COLLECTIBLE_DEF (def));
    g_assert_false (LRG_IS_MOUNT_DEF (def));
    g_assert_false (LRG_IS_PET_DEF (def));

    g_assert_cmpstr (lrg_collectible_def_get_id (def), ==, "title_champion");
    g_assert_cmpint (lrg_collectible_def_get_kind (def), ==, LRG_COLLECTIBLE_KIND_TITLE);
    g_assert_null (lrg_collectible_def_get_name (def));
    g_assert_null (lrg_collectible_def_get_description (def));
    g_assert_null (lrg_collectible_def_get_icon (def));
    g_assert_null (lrg_collectible_def_get_source (def));
    g_assert_null (lrg_collectible_def_get_model (def));
    g_assert_cmpint (lrg_collectible_def_get_rarity (def), ==, LRG_COLLECTIBLE_RARITY_COMMON);
    g_assert_cmpuint (lrg_collectible_def_get_required_level (def), ==, 0);

    g_object_get (def, "id", &id, "kind", &kind, "rarity", &rarity,
                  "required-level", &level, NULL);
    g_assert_cmpstr (id, ==, "title_champion");
    g_assert_cmpint (kind, ==, LRG_COLLECTIBLE_KIND_TITLE);
    g_assert_cmpint (rarity, ==, LRG_COLLECTIBLE_RARITY_COMMON);
    g_assert_cmpuint (level, ==, 0);
}

static void
test_collectible_def_g_object_new_default_kind (void)
{
    g_autoptr(LrgCollectibleDef) def = NULL;
    g_autoptr(LrgCollectibleDef) toy = NULL;

    /* No kind passed: base default is TOY. */
    def = g_object_new (LRG_TYPE_COLLECTIBLE_DEF, "id", "thing", NULL);
    g_assert_cmpint (lrg_collectible_def_get_kind (def), ==, LRG_COLLECTIBLE_KIND_TOY);

    toy = g_object_new (LRG_TYPE_COLLECTIBLE_DEF, "id", "mount_like",
                        "kind", LRG_COLLECTIBLE_KIND_MOUNT, NULL);
    g_assert_cmpint (lrg_collectible_def_get_kind (toy), ==, LRG_COLLECTIBLE_KIND_MOUNT);
}

static void
test_collectible_def_properties (void)
{
    g_autoptr(LrgCollectibleDef) def = NULL;
    g_autofree gchar            *name = NULL;
    g_autofree gchar            *description = NULL;
    g_autofree gchar            *icon = NULL;
    g_autofree gchar            *source = NULL;
    g_autofree gchar            *model = NULL;
    LrgCollectibleRarity         rarity;
    guint                        level;

    def = lrg_collectible_def_new ("toy_drum", LRG_COLLECTIBLE_KIND_TOY);

    /* Set through GObject, read through getters. */
    g_object_set (def,
                  "name", "War Drum",
                  "description", "Loud.",
                  "icon", "icon_drum",
                  "rarity", LRG_COLLECTIBLE_RARITY_EPIC,
                  "source", "Vendor: Drummer",
                  "model", "models/drum.glb",
                  "required-level", 42u,
                  NULL);
    g_assert_cmpstr (lrg_collectible_def_get_name (def), ==, "War Drum");
    g_assert_cmpstr (lrg_collectible_def_get_description (def), ==, "Loud.");
    g_assert_cmpstr (lrg_collectible_def_get_icon (def), ==, "icon_drum");
    g_assert_cmpint (lrg_collectible_def_get_rarity (def), ==, LRG_COLLECTIBLE_RARITY_EPIC);
    g_assert_cmpstr (lrg_collectible_def_get_source (def), ==, "Vendor: Drummer");
    g_assert_cmpstr (lrg_collectible_def_get_model (def), ==, "models/drum.glb");
    g_assert_cmpuint (lrg_collectible_def_get_required_level (def), ==, 42);

    /* Set through setters, read through GObject. */
    lrg_collectible_def_set_name (def, "Drum");
    lrg_collectible_def_set_description (def, NULL);
    lrg_collectible_def_set_icon (def, "i2");
    lrg_collectible_def_set_rarity (def, LRG_COLLECTIBLE_RARITY_LEGENDARY);
    lrg_collectible_def_set_source (def, NULL);
    lrg_collectible_def_set_model (def, "m2");
    lrg_collectible_def_set_required_level (def, 7);
    g_object_get (def, "name", &name, "description", &description, "icon", &icon,
                  "rarity", &rarity, "source", &source, "model", &model,
                  "required-level", &level, NULL);
    g_assert_cmpstr (name, ==, "Drum");
    g_assert_null (description);
    g_assert_cmpstr (icon, ==, "i2");
    g_assert_cmpint (rarity, ==, LRG_COLLECTIBLE_RARITY_LEGENDARY);
    g_assert_null (source);
    g_assert_cmpstr (model, ==, "m2");
    g_assert_cmpuint (level, ==, 7);
}

static void
test_collectible_def_notify_only_on_change (void)
{
    g_autoptr(LrgCollectibleDef) def = NULL;
    guint                        notified = 0;

    def = lrg_collectible_def_new ("t", LRG_COLLECTIBLE_KIND_TITLE);
    g_signal_connect (def, "notify", G_CALLBACK (count_notify), &notified);

    lrg_collectible_def_set_name (def, "A");
    g_assert_cmpuint (notified, ==, 1);
    lrg_collectible_def_set_name (def, "A");
    g_assert_cmpuint (notified, ==, 1);
    lrg_collectible_def_set_name (def, NULL);
    g_assert_cmpuint (notified, ==, 2);
    lrg_collectible_def_set_name (def, NULL);
    g_assert_cmpuint (notified, ==, 2);

    lrg_collectible_def_set_rarity (def, LRG_COLLECTIBLE_RARITY_COMMON);
    g_assert_cmpuint (notified, ==, 2);
    lrg_collectible_def_set_rarity (def, LRG_COLLECTIBLE_RARITY_RARE);
    g_assert_cmpuint (notified, ==, 3);

    lrg_collectible_def_set_required_level (def, 0);
    g_assert_cmpuint (notified, ==, 3);
    g_object_set (def, "required-level", 5u, NULL);
    g_assert_cmpuint (notified, ==, 4);
    g_object_set (def, "required-level", 5u, NULL);
    g_assert_cmpuint (notified, ==, 4);
}

static void
test_collectible_def_can_obtain (void)
{
    g_autoptr(LrgCollectibleDef) def = NULL;
    g_autoptr(GError)            error = NULL;

    def = lrg_collectible_def_new ("t", LRG_COLLECTIBLE_KIND_TITLE);
    g_assert_true (lrg_collectible_def_can_obtain (def, 0, NULL));

    lrg_collectible_def_set_required_level (def, 10);
    g_assert_false (lrg_collectible_def_can_obtain (def, 9, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT);
    g_clear_error (&error);

    g_assert_true (lrg_collectible_def_can_obtain (def, 10, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_collectible_def_can_obtain (def, 11, NULL));
}

static void
test_collectible_def_invalid_rarity (void)
{
    g_autoptr(LrgCollectibleDef) def = NULL;

    def = lrg_collectible_def_new ("t", LRG_COLLECTIBLE_KIND_TITLE);
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*rarity*");
    lrg_collectible_def_set_rarity (def, (LrgCollectibleRarity)99);
    g_test_assert_expected_messages ();
    g_assert_cmpint (lrg_collectible_def_get_rarity (def), ==, LRG_COLLECTIBLE_RARITY_COMMON);
}

/* ========================================================================== */
/*                               LrgMountDef                                  */
/* ========================================================================== */

static void
test_mount_def_defaults (void)
{
    g_autoptr(LrgMountDef) mount = NULL;
    gdouble                speed;
    guint                  riding;
    gboolean               flying;
    guint                  passengers;
    LrgCollectibleKind     kind;

    mount = lrg_mount_def_new ("mount_horse");
    g_assert_true (LRG_IS_MOUNT_DEF (mount));
    g_assert_true (LRG_IS_COLLECTIBLE_DEF (mount));
    g_assert_cmpstr (lrg_collectible_def_get_id (LRG_COLLECTIBLE_DEF (mount)), ==, "mount_horse");
    g_assert_cmpint (lrg_collectible_def_get_kind (LRG_COLLECTIBLE_DEF (mount)), ==,
                     LRG_COLLECTIBLE_KIND_MOUNT);
    g_assert_cmpfloat_with_epsilon (lrg_mount_def_get_speed_multiplier (mount), 1.0, EPS);
    g_assert_cmpuint (lrg_mount_def_get_required_riding (mount), ==, 1);
    g_assert_false (lrg_mount_def_get_flying (mount));
    g_assert_cmpuint (lrg_mount_def_get_passengers (mount), ==, 0);

    g_object_get (mount, "speed-multiplier", &speed, "required-riding", &riding,
                  "flying", &flying, "passengers", &passengers, "kind", &kind, NULL);
    g_assert_cmpfloat_with_epsilon (speed, 1.0, EPS);
    g_assert_cmpuint (riding, ==, 1);
    g_assert_false (flying);
    g_assert_cmpuint (passengers, ==, 0);
    g_assert_cmpint (kind, ==, LRG_COLLECTIBLE_KIND_MOUNT);
}

static void
test_mount_def_kind_pinned (void)
{
    g_autoptr(LrgMountDef) mount = NULL;
    g_autoptr(LrgMountDef) plain = NULL;

    /* Construct with a lying kind: the subclass pins MOUNT. */
    mount = g_object_new (LRG_TYPE_MOUNT_DEF, "id", "m", "kind", LRG_COLLECTIBLE_KIND_PET, NULL);
    g_assert_cmpint (lrg_collectible_def_get_kind (LRG_COLLECTIBLE_DEF (mount)), ==,
                     LRG_COLLECTIBLE_KIND_MOUNT);

    plain = g_object_new (LRG_TYPE_MOUNT_DEF, "id", "m2", NULL);
    g_assert_cmpint (lrg_collectible_def_get_kind (LRG_COLLECTIBLE_DEF (plain)), ==,
                     LRG_COLLECTIBLE_KIND_MOUNT);
}

static void
test_mount_def_properties (void)
{
    g_autoptr(LrgMountDef) mount = NULL;
    guint                  notified = 0;

    mount = lrg_mount_def_new ("mount_gryphon");
    g_signal_connect (mount, "notify", G_CALLBACK (count_notify), &notified);

    g_object_set (mount, "speed-multiplier", 2.5, "required-riding", 3u,
                  "flying", TRUE, "passengers", 2u, NULL);
    g_assert_cmpuint (notified, ==, 4);
    g_assert_cmpfloat_with_epsilon (lrg_mount_def_get_speed_multiplier (mount), 2.5, EPS);
    g_assert_cmpuint (lrg_mount_def_get_required_riding (mount), ==, 3);
    g_assert_true (lrg_mount_def_get_flying (mount));
    g_assert_cmpuint (lrg_mount_def_get_passengers (mount), ==, 2);

    /* Same values: no notifications. */
    lrg_mount_def_set_speed_multiplier (mount, 2.5);
    lrg_mount_def_set_required_riding (mount, 3);
    lrg_mount_def_set_flying (mount, TRUE);
    lrg_mount_def_set_passengers (mount, 2);
    g_assert_cmpuint (notified, ==, 4);

    lrg_mount_def_set_flying (mount, FALSE);
    g_assert_false (lrg_mount_def_get_flying (mount));
    g_assert_cmpuint (notified, ==, 5);
}

static void
test_mount_def_invalid_setters (void)
{
    g_autoptr(LrgMountDef) mount = NULL;

    mount = lrg_mount_def_new ("m");

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*multiplier*");
    lrg_mount_def_set_speed_multiplier (mount, 0.5);
    g_test_assert_expected_messages ();

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*isfinite*");
    lrg_mount_def_set_speed_multiplier (mount, NAN);
    g_test_assert_expected_messages ();

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*multiplier*");
    lrg_mount_def_set_speed_multiplier (mount, LRG_MOUNT_DEF_MAX_SPEED_MULTIPLIER + 1.0);
    g_test_assert_expected_messages ();

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*passengers*");
    lrg_mount_def_set_passengers (mount, LRG_MOUNT_DEF_MAX_PASSENGERS + 1);
    g_test_assert_expected_messages ();

    g_assert_cmpfloat_with_epsilon (lrg_mount_def_get_speed_multiplier (mount), 1.0, EPS);
    g_assert_cmpuint (lrg_mount_def_get_passengers (mount), ==, 0);

    /* Boundaries are accepted. */
    lrg_mount_def_set_speed_multiplier (mount, 1.0);
    lrg_mount_def_set_speed_multiplier (mount, LRG_MOUNT_DEF_MAX_SPEED_MULTIPLIER);
    lrg_mount_def_set_passengers (mount, LRG_MOUNT_DEF_MAX_PASSENGERS);
    g_assert_cmpuint (lrg_mount_def_get_passengers (mount), ==, LRG_MOUNT_DEF_MAX_PASSENGERS);
}

static void
test_mount_def_can_ride (void)
{
    g_autoptr(LrgMountDef) mount = NULL;

    mount = lrg_mount_def_new ("mount_wolf");

    /* Default: riding tier 1, no level requirement. */
    g_assert_false (lrg_mount_def_can_ride (mount, 0, 60));
    g_assert_true (lrg_mount_def_can_ride (mount, 1, 0));
    g_assert_true (lrg_mount_def_can_ride (mount, 5, 1));

    /* Epic tier gate. */
    lrg_mount_def_set_required_riding (mount, 2);
    g_assert_false (lrg_mount_def_can_ride (mount, 1, 60));
    g_assert_true (lrg_mount_def_can_ride (mount, 2, 60));
    g_assert_true (lrg_mount_def_can_ride (mount, 3, 60));

    /* Level gate combines with the tier gate. */
    lrg_collectible_def_set_required_level (LRG_COLLECTIBLE_DEF (mount), 40);
    g_assert_false (lrg_mount_def_can_ride (mount, 2, 39));
    g_assert_true (lrg_mount_def_can_ride (mount, 2, 40));
    g_assert_false (lrg_mount_def_can_ride (mount, 1, 40));
    g_assert_false (lrg_mount_def_can_ride (mount, 0, 0));

    /* Tier 0 requirement: anyone at level. */
    lrg_mount_def_set_required_riding (mount, 0);
    g_assert_true (lrg_mount_def_can_ride (mount, 0, 40));
}

static void
test_mount_def_speed (void)
{
    g_autoptr(LrgMountDef) mount = NULL;

    mount = lrg_mount_def_new ("m");
    g_assert_cmpfloat_with_epsilon (lrg_mount_def_get_speed (mount, 7.0, 1.6), 11.2, 1e-9);

    lrg_mount_def_set_speed_multiplier (mount, 1.5);
    g_assert_cmpfloat_with_epsilon (lrg_mount_def_get_speed (mount, 7.0, 2.0), 21.0, 1e-9);
    g_assert_cmpfloat_with_epsilon (lrg_mount_def_get_speed (mount, 0.0, 2.0), 0.0, 1e-12);

    /* Hostile inputs clamp to 0. */
    g_assert_cmpfloat (lrg_mount_def_get_speed (mount, -1.0, 2.0), ==, 0.0);
    g_assert_cmpfloat (lrg_mount_def_get_speed (mount, 7.0, -2.0), ==, 0.0);
    g_assert_cmpfloat (lrg_mount_def_get_speed (mount, NAN, 2.0), ==, 0.0);
    g_assert_cmpfloat (lrg_mount_def_get_speed (mount, 7.0, INFINITY), ==, 0.0);
    g_assert_cmpfloat (lrg_mount_def_get_speed (mount, G_MAXDOUBLE, G_MAXDOUBLE), ==, 0.0);
}

/* ========================================================================== */
/*                                LrgPetDef                                   */
/* ========================================================================== */

static void
test_pet_def_defaults (void)
{
    g_autoptr(LrgPetDef) pet = NULL;
    LrgCompanionKind     ck;
    gdouble              fd;
    gdouble              fa;
    guint                health;
    guint                damage;
    gdouble              ar;
    gdouble              ai;
    gdouble              ms;

    pet = lrg_pet_def_new ("pet_cat", LRG_COMPANION_KIND_VANITY);
    g_assert_true (LRG_IS_PET_DEF (pet));
    g_assert_true (LRG_IS_COLLECTIBLE_DEF (pet));
    g_assert_false (LRG_IS_MOUNT_DEF (pet));
    g_assert_cmpint (lrg_collectible_def_get_kind (LRG_COLLECTIBLE_DEF (pet)), ==,
                     LRG_COLLECTIBLE_KIND_PET);
    g_assert_cmpint (lrg_pet_def_get_companion_kind (pet), ==, LRG_COMPANION_KIND_VANITY);
    g_assert_cmpfloat_with_epsilon (lrg_pet_def_get_follow_distance (pet), 2.0, EPS);
    g_assert_cmpfloat_with_epsilon (lrg_pet_def_get_follow_angle (pet), 2.356, EPS);
    g_assert_cmpuint (lrg_pet_def_get_health (pet), ==, 0);
    g_assert_cmpuint (lrg_pet_def_get_damage (pet), ==, 0);
    g_assert_cmpfloat_with_epsilon (lrg_pet_def_get_attack_range (pet), 3.0, EPS);
    g_assert_cmpfloat_with_epsilon (lrg_pet_def_get_attack_interval (pet), 2.0, EPS);
    g_assert_cmpfloat_with_epsilon (lrg_pet_def_get_move_speed (pet), 7.0, EPS);
    g_assert_cmpuint (lrg_pet_def_get_abilities (pet)->len, ==, 0);

    g_object_get (pet, "companion-kind", &ck, "follow-distance", &fd,
                  "follow-angle", &fa, "health", &health, "damage", &damage,
                  "attack-range", &ar, "attack-interval", &ai, "move-speed", &ms,
                  NULL);
    g_assert_cmpint (ck, ==, LRG_COMPANION_KIND_VANITY);
    g_assert_cmpfloat_with_epsilon (fd, 2.0, EPS);
    g_assert_cmpfloat_with_epsilon (fa, 2.356, EPS);
    g_assert_cmpuint (health, ==, 0);
    g_assert_cmpuint (damage, ==, 0);
    g_assert_cmpfloat_with_epsilon (ar, 3.0, EPS);
    g_assert_cmpfloat_with_epsilon (ai, 2.0, EPS);
    g_assert_cmpfloat_with_epsilon (ms, 7.0, EPS);
}

static void
test_pet_def_kind_pinned (void)
{
    g_autoptr(LrgPetDef) pet = NULL;
    g_autoptr(LrgPetDef) combat = NULL;

    pet = g_object_new (LRG_TYPE_PET_DEF, "id", "p", "kind", LRG_COLLECTIBLE_KIND_MOUNT, NULL);
    g_assert_cmpint (lrg_collectible_def_get_kind (LRG_COLLECTIBLE_DEF (pet)), ==,
                     LRG_COLLECTIBLE_KIND_PET);

    combat = lrg_pet_def_new ("pet_wolf", LRG_COMPANION_KIND_COMBAT);
    g_assert_cmpint (lrg_pet_def_get_companion_kind (combat), ==, LRG_COMPANION_KIND_COMBAT);
    g_assert_cmpint (lrg_collectible_def_get_kind (LRG_COLLECTIBLE_DEF (combat)), ==,
                     LRG_COLLECTIBLE_KIND_PET);
}

static void
test_pet_def_properties (void)
{
    g_autoptr(LrgPetDef) pet = NULL;
    guint                notified = 0;

    pet = lrg_pet_def_new ("pet_wolf", LRG_COMPANION_KIND_COMBAT);
    g_signal_connect (pet, "notify", G_CALLBACK (count_notify), &notified);

    g_object_set (pet, "companion-kind", LRG_COMPANION_KIND_VANITY,
                  "follow-distance", 3.5, "follow-angle", -1.0,
                  "health", 500u, "damage", 25u, "attack-range", 1.5,
                  "attack-interval", 1.25, "move-speed", 9.0, NULL);
    g_assert_cmpuint (notified, ==, 8);
    g_assert_cmpint (lrg_pet_def_get_companion_kind (pet), ==, LRG_COMPANION_KIND_VANITY);
    g_assert_cmpfloat_with_epsilon (lrg_pet_def_get_follow_distance (pet), 3.5, EPS);
    g_assert_cmpfloat_with_epsilon (lrg_pet_def_get_follow_angle (pet), -1.0, EPS);
    g_assert_cmpuint (lrg_pet_def_get_health (pet), ==, 500);
    g_assert_cmpuint (lrg_pet_def_get_damage (pet), ==, 25);
    g_assert_cmpfloat_with_epsilon (lrg_pet_def_get_attack_range (pet), 1.5, EPS);
    g_assert_cmpfloat_with_epsilon (lrg_pet_def_get_attack_interval (pet), 1.25, EPS);
    g_assert_cmpfloat_with_epsilon (lrg_pet_def_get_move_speed (pet), 9.0, EPS);

    /* Re-applying the same values notifies nothing. */
    lrg_pet_def_set_companion_kind (pet, LRG_COMPANION_KIND_VANITY);
    lrg_pet_def_set_follow_distance (pet, 3.5);
    lrg_pet_def_set_follow_angle (pet, -1.0);
    lrg_pet_def_set_health (pet, 500);
    lrg_pet_def_set_damage (pet, 25);
    lrg_pet_def_set_attack_range (pet, 1.5);
    lrg_pet_def_set_attack_interval (pet, 1.25);
    lrg_pet_def_set_move_speed (pet, 9.0);
    g_assert_cmpuint (notified, ==, 8);
}

static void
test_pet_def_invalid_setters (void)
{
    g_autoptr(LrgPetDef) pet = NULL;

    pet = lrg_pet_def_new ("p", LRG_COMPANION_KIND_COMBAT);

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*isfinite*");
    lrg_pet_def_set_follow_distance (pet, NAN);
    g_test_assert_expected_messages ();

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*min*");
    lrg_pet_def_set_follow_distance (pet, -1.0);
    g_test_assert_expected_messages ();

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*min*");
    lrg_pet_def_set_follow_angle (pet, 7.0);
    g_test_assert_expected_messages ();

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*min*");
    lrg_pet_def_set_attack_interval (pet, 0.0);
    g_test_assert_expected_messages ();

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*isfinite*");
    lrg_pet_def_set_move_speed (pet, INFINITY);
    g_test_assert_expected_messages ();

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*min*");
    lrg_pet_def_set_attack_range (pet, -0.5);
    g_test_assert_expected_messages ();

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*kind*");
    lrg_pet_def_set_companion_kind (pet, (LrgCompanionKind)7);
    g_test_assert_expected_messages ();

    g_assert_cmpfloat_with_epsilon (lrg_pet_def_get_follow_distance (pet), 2.0, EPS);
    g_assert_cmpfloat_with_epsilon (lrg_pet_def_get_follow_angle (pet), 2.356, EPS);
    g_assert_cmpfloat_with_epsilon (lrg_pet_def_get_attack_interval (pet), 2.0, EPS);
    g_assert_cmpfloat_with_epsilon (lrg_pet_def_get_move_speed (pet), 7.0, EPS);
    g_assert_cmpfloat_with_epsilon (lrg_pet_def_get_attack_range (pet), 3.0, EPS);
    g_assert_cmpint (lrg_pet_def_get_companion_kind (pet), ==, LRG_COMPANION_KIND_COMBAT);
}

static void
test_pet_def_abilities (void)
{
    g_autoptr(LrgPetDef) pet = NULL;
    GPtrArray           *abilities;
    guint                i;

    pet = lrg_pet_def_new ("pet_wolf", LRG_COMPANION_KIND_COMBAT);
    g_assert_false (lrg_pet_def_has_ability (pet, "bite"));
    g_assert_false (lrg_pet_def_has_ability (pet, NULL));

    lrg_pet_def_add_ability (pet, "bite");
    lrg_pet_def_add_ability (pet, "growl");
    lrg_pet_def_add_ability (pet, "bite");   /* duplicate ignored */
    lrg_pet_def_add_ability (pet, "dash");

    abilities = lrg_pet_def_get_abilities (pet);
    g_assert_cmpuint (abilities->len, ==, 3);
    g_assert_cmpstr (g_ptr_array_index (abilities, 0), ==, "bite");
    g_assert_cmpstr (g_ptr_array_index (abilities, 1), ==, "growl");
    g_assert_cmpstr (g_ptr_array_index (abilities, 2), ==, "dash");
    g_assert_true (lrg_pet_def_has_ability (pet, "growl"));
    g_assert_false (lrg_pet_def_has_ability (pet, "claw"));

    /* Empty ids are rejected. */
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*ability_id*");
    lrg_pet_def_add_ability (pet, "");
    g_test_assert_expected_messages ();
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*ability_id*");
    lrg_pet_def_add_ability (pet, NULL);
    g_test_assert_expected_messages ();
    g_assert_cmpuint (abilities->len, ==, 3);

    /* Capacity limit. */
    for (i = abilities->len; i < LRG_PET_DEF_MAX_ABILITIES; i++)
    {
        g_autofree gchar *id = g_strdup_printf ("ab%u", i);
        lrg_pet_def_add_ability (pet, id);
    }
    g_assert_cmpuint (abilities->len, ==, LRG_PET_DEF_MAX_ABILITIES);
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*LRG_PET_DEF_MAX_ABILITIES*");
    lrg_pet_def_add_ability (pet, "one_too_many");
    g_test_assert_expected_messages ();
    g_assert_cmpuint (abilities->len, ==, LRG_PET_DEF_MAX_ABILITIES);
    /* A duplicate at capacity is still silently ignored. */
    lrg_pet_def_add_ability (pet, "bite");
    g_assert_cmpuint (abilities->len, ==, LRG_PET_DEF_MAX_ABILITIES);
}

/* ========================================================================== */
/*                              LrgCollection                                 */
/* ========================================================================== */

typedef struct
{
    LrgCollection *collection;
} CollectionFixture;

static void
collection_fixture_setup (CollectionFixture *fixture,
                          gconstpointer      user_data)
{
    (void)user_data;
    fixture->collection = lrg_collection_new ();

    /* Insert out of order to prove sorting. */
    g_assert_true (lrg_collection_add (fixture->collection, LRG_COLLECTIBLE_KIND_MOUNT, "mount_wolf", NULL));
    g_assert_true (lrg_collection_add (fixture->collection, LRG_COLLECTIBLE_KIND_MOUNT, "mount_horse", NULL));
    g_assert_true (lrg_collection_add (fixture->collection, LRG_COLLECTIBLE_KIND_PET, "pet_owl", NULL));
    g_assert_true (lrg_collection_add (fixture->collection, LRG_COLLECTIBLE_KIND_PET, "pet_cat", NULL));
    g_assert_true (lrg_collection_add (fixture->collection, LRG_COLLECTIBLE_KIND_TITLE, "title_the_bold", NULL));
    g_assert_true (lrg_collection_add (fixture->collection, LRG_COLLECTIBLE_KIND_MOUNT, "mount_gryphon", NULL));
}

static void
collection_fixture_teardown (CollectionFixture *fixture,
                             gconstpointer      user_data)
{
    (void)user_data;
    g_clear_object (&fixture->collection);
}

static void
test_collection_empty (void)
{
    g_autoptr(LrgCollection) c = lrg_collection_new ();
    g_autoptr(GPtrArray)     ids = NULL;
    guint                    k;

    g_assert_cmpuint (lrg_collection_get_total_count (c), ==, 0);
    for (k = LRG_COLLECTIBLE_KIND_MOUNT; k <= LRG_COLLECTIBLE_KIND_TOY; k++)
    {
        g_autoptr(GPtrArray) kind_ids = lrg_collection_get_ids (c, (LrgCollectibleKind)k);

        g_assert_cmpuint (lrg_collection_get_count (c, (LrgCollectibleKind)k), ==, 0);
        g_assert_cmpuint (kind_ids->len, ==, 0);
        g_assert_null (lrg_collection_get_active (c, (LrgCollectibleKind)k));
    }
    g_assert_false (lrg_collection_has (c, "x"));
    g_assert_false (lrg_collection_has (c, NULL));
    g_assert_false (lrg_collection_is_favorite (c, "x"));
    g_assert_false (lrg_collection_is_favorite (c, NULL));
    g_assert_false (lrg_collection_get_kind (c, "x", NULL));
    g_assert_false (lrg_collection_remove (c, "x"));
    g_assert_false (lrg_collection_remove (c, NULL));

    /* Unknown kinds read as empty rather than crashing. */
    ids = lrg_collection_get_ids (c, (LrgCollectibleKind)99);
    g_assert_cmpuint (ids->len, ==, 0);
    g_assert_cmpuint (lrg_collection_get_count (c, (LrgCollectibleKind)99), ==, 0);
    g_assert_null (lrg_collection_get_active (c, (LrgCollectibleKind)99));
}

static void
test_collection_add_has_count_ids (CollectionFixture *fixture,
                                   gconstpointer      user_data)
{
    LrgCollection        *c = fixture->collection;
    g_autoptr(GPtrArray)  mounts = NULL;
    g_autoptr(GPtrArray)  pets = NULL;
    g_autoptr(GPtrArray)  titles = NULL;
    g_autoptr(GPtrArray)  toys = NULL;
    LrgCollectibleKind    kind;

    (void)user_data;

    g_assert_cmpuint (lrg_collection_get_total_count (c), ==, 6);
    g_assert_cmpuint (lrg_collection_get_count (c, LRG_COLLECTIBLE_KIND_MOUNT), ==, 3);
    g_assert_cmpuint (lrg_collection_get_count (c, LRG_COLLECTIBLE_KIND_PET), ==, 2);
    g_assert_cmpuint (lrg_collection_get_count (c, LRG_COLLECTIBLE_KIND_TITLE), ==, 1);
    g_assert_cmpuint (lrg_collection_get_count (c, LRG_COLLECTIBLE_KIND_TOY), ==, 0);

    mounts = lrg_collection_get_ids (c, LRG_COLLECTIBLE_KIND_MOUNT);
    g_assert_cmpuint (mounts->len, ==, 3);
    g_assert_cmpstr (g_ptr_array_index (mounts, 0), ==, "mount_gryphon");
    g_assert_cmpstr (g_ptr_array_index (mounts, 1), ==, "mount_horse");
    g_assert_cmpstr (g_ptr_array_index (mounts, 2), ==, "mount_wolf");

    pets = lrg_collection_get_ids (c, LRG_COLLECTIBLE_KIND_PET);
    g_assert_cmpuint (pets->len, ==, 2);
    g_assert_cmpstr (g_ptr_array_index (pets, 0), ==, "pet_cat");
    g_assert_cmpstr (g_ptr_array_index (pets, 1), ==, "pet_owl");

    titles = lrg_collection_get_ids (c, LRG_COLLECTIBLE_KIND_TITLE);
    g_assert_cmpuint (titles->len, ==, 1);
    toys = lrg_collection_get_ids (c, LRG_COLLECTIBLE_KIND_TOY);
    g_assert_cmpuint (toys->len, ==, 0);

    g_assert_true (lrg_collection_has (c, "pet_owl"));
    g_assert_false (lrg_collection_has (c, "pet_dog"));
    g_assert_true (lrg_collection_get_kind (c, "pet_owl", &kind));
    g_assert_cmpint (kind, ==, LRG_COLLECTIBLE_KIND_PET);
    g_assert_true (lrg_collection_get_kind (c, "mount_wolf", NULL));

    /* A toy, and an id at the maximum length, are accepted. */
    {
        gchar max_id[LRG_COLLECTION_MAX_ID_LENGTH + 1];

        memset (max_id, 'x', LRG_COLLECTION_MAX_ID_LENGTH);
        max_id[LRG_COLLECTION_MAX_ID_LENGTH] = '\0';
        g_assert_true (lrg_collection_add (c, LRG_COLLECTIBLE_KIND_TOY, max_id, NULL));
        g_assert_true (lrg_collection_has (c, max_id));
        g_assert_true (lrg_collection_add (c, LRG_COLLECTIBLE_KIND_TOY, "toy_\xc3\xa9t\xc3\xa9", NULL));
        g_assert_cmpuint (lrg_collection_get_count (c, LRG_COLLECTIBLE_KIND_TOY), ==, 2);
    }
}

static void
test_collection_add_rejections (CollectionFixture *fixture,
                                gconstpointer      user_data)
{
    LrgCollection       *c = fixture->collection;
    g_autoptr(GVariant)  before = snapshot (c);
    g_autoptr(GError)    error = NULL;
    gchar                long_id[LRG_COLLECTION_MAX_ID_LENGTH + 2];

    (void)user_data;

    /* Unknown kind. */
    g_assert_false (lrg_collection_add (c, (LrgCollectibleKind)4, "new_thing", &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    assert_unchanged (c, before);
    g_assert_false (lrg_collection_has (c, "new_thing"));

    /* NULL id. */
    g_assert_false (lrg_collection_add (c, LRG_COLLECTIBLE_KIND_TOY, NULL, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    assert_unchanged (c, before);

    /* Empty id. */
    g_assert_false (lrg_collection_add (c, LRG_COLLECTIBLE_KIND_TOY, "", &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    assert_unchanged (c, before);

    /* Overlong id (129 bytes). */
    memset (long_id, 'y', LRG_COLLECTION_MAX_ID_LENGTH + 1);
    long_id[LRG_COLLECTION_MAX_ID_LENGTH + 1] = '\0';
    g_assert_false (lrg_collection_add (c, LRG_COLLECTIBLE_KIND_TOY, long_id, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    assert_unchanged (c, before);

    /* Invalid UTF-8. */
    g_assert_false (lrg_collection_add (c, LRG_COLLECTIBLE_KIND_TOY, "bad\xff", &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    g_assert_false (lrg_collection_add (c, LRG_COLLECTIBLE_KIND_TOY, "\xc3", &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    assert_unchanged (c, before);

    /* Duplicate id, same kind. */
    g_assert_false (lrg_collection_add (c, LRG_COLLECTIBLE_KIND_MOUNT, "mount_wolf", &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_DUPLICATE);
    g_clear_error (&error);
    assert_unchanged (c, before);

    /* Duplicate id, different kind: ids are unique across kinds. */
    g_assert_false (lrg_collection_add (c, LRG_COLLECTIBLE_KIND_PET, "mount_wolf", &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_DUPLICATE);
    g_clear_error (&error);
    assert_unchanged (c, before);
    g_assert_cmpuint (lrg_collection_get_count (c, LRG_COLLECTIBLE_KIND_PET), ==, 2);
}

static void
test_collection_add_limit (void)
{
    g_autoptr(LrgCollection) c = lrg_collection_new ();
    g_autoptr(GError)        error = NULL;
    g_autoptr(GVariant)      before = NULL;
    g_autoptr(LrgCollection) restored = NULL;
    guint                    i;

    for (i = 0; i < LRG_COLLECTION_MAX_ENTRIES; i++)
    {
        g_autofree gchar *id = g_strdup_printf ("toy_%05u", i);
        g_assert_true (lrg_collection_add (c, (LrgCollectibleKind)(i % 4), id, NULL));
    }
    g_assert_cmpuint (lrg_collection_get_total_count (c), ==, LRG_COLLECTION_MAX_ENTRIES);

    before = snapshot (c);
    g_assert_false (lrg_collection_add (c, LRG_COLLECTIBLE_KIND_TOY, "overflow", &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT);
    g_clear_error (&error);
    assert_unchanged (c, before);

    /* A full collection still round-trips (the maximum is accepted). */
    restored = lrg_collection_new_from_variant (before, &error);
    g_assert_no_error (error);
    g_assert_nonnull (restored);
    g_assert_cmpuint (lrg_collection_get_total_count (restored), ==, LRG_COLLECTION_MAX_ENTRIES);

    /* Duplicate check wins over the limit check. */
    g_assert_false (lrg_collection_add (c, LRG_COLLECTIBLE_KIND_TOY, "toy_00000", &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_DUPLICATE);
}

static void
test_collection_favorites (CollectionFixture *fixture,
                           gconstpointer      user_data)
{
    LrgCollection        *c = fixture->collection;
    g_autoptr(GError)     error = NULL;
    g_autoptr(GPtrArray)  favs = NULL;
    g_autoptr(GVariant)   before = NULL;

    (void)user_data;

    g_assert_false (lrg_collection_is_favorite (c, "mount_wolf"));
    g_assert_true (lrg_collection_set_favorite (c, "mount_wolf", TRUE, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_collection_set_favorite (c, "mount_gryphon", TRUE, NULL));
    g_assert_true (lrg_collection_set_favorite (c, "pet_cat", TRUE, NULL));
    g_assert_true (lrg_collection_is_favorite (c, "mount_wolf"));
    g_assert_false (lrg_collection_is_favorite (c, "mount_horse"));

    favs = lrg_collection_get_favorites (c, LRG_COLLECTIBLE_KIND_MOUNT);
    g_assert_cmpuint (favs->len, ==, 2);
    g_assert_cmpstr (g_ptr_array_index (favs, 0), ==, "mount_gryphon");
    g_assert_cmpstr (g_ptr_array_index (favs, 1), ==, "mount_wolf");
    g_clear_pointer (&favs, g_ptr_array_unref);

    /* Idempotent set, then unset. */
    g_assert_true (lrg_collection_set_favorite (c, "mount_wolf", TRUE, NULL));
    g_assert_true (lrg_collection_set_favorite (c, "mount_wolf", FALSE, NULL));
    g_assert_false (lrg_collection_is_favorite (c, "mount_wolf"));
    favs = lrg_collection_get_favorites (c, LRG_COLLECTIBLE_KIND_MOUNT);
    g_assert_cmpuint (favs->len, ==, 1);
    g_clear_pointer (&favs, g_ptr_array_unref);

    favs = lrg_collection_get_favorites (c, (LrgCollectibleKind)42);
    g_assert_cmpuint (favs->len, ==, 0);
    g_clear_pointer (&favs, g_ptr_array_unref);

    /* Rejections leave state untouched. */
    before = snapshot (c);
    g_assert_false (lrg_collection_set_favorite (c, "mount_unicorn", TRUE, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_NOT_FOUND);
    g_clear_error (&error);
    g_assert_false (lrg_collection_set_favorite (c, NULL, TRUE, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_NOT_FOUND);
    g_clear_error (&error);
    assert_unchanged (c, before);
    g_assert_false (lrg_collection_is_favorite (c, "mount_unicorn"));
}

static void
test_collection_active (CollectionFixture *fixture,
                        gconstpointer      user_data)
{
    LrgCollection       *c = fixture->collection;
    g_autoptr(GError)    error = NULL;
    g_autoptr(GVariant)  before = NULL;

    (void)user_data;

    g_assert_true (lrg_collection_set_active (c, LRG_COLLECTIBLE_KIND_MOUNT, "mount_horse", &error));
    g_assert_no_error (error);
    g_assert_true (lrg_collection_set_active (c, LRG_COLLECTIBLE_KIND_PET, "pet_owl", NULL));
    g_assert_true (lrg_collection_set_active (c, LRG_COLLECTIBLE_KIND_TITLE, "title_the_bold", NULL));
    g_assert_cmpstr (lrg_collection_get_active (c, LRG_COLLECTIBLE_KIND_MOUNT), ==, "mount_horse");
    g_assert_cmpstr (lrg_collection_get_active (c, LRG_COLLECTIBLE_KIND_PET), ==, "pet_owl");
    g_assert_cmpstr (lrg_collection_get_active (c, LRG_COLLECTIBLE_KIND_TITLE), ==, "title_the_bold");
    g_assert_null (lrg_collection_get_active (c, LRG_COLLECTIBLE_KIND_TOY));

    /* Switching and re-setting the same id. */
    g_assert_true (lrg_collection_set_active (c, LRG_COLLECTIBLE_KIND_MOUNT, "mount_wolf", NULL));
    g_assert_true (lrg_collection_set_active (c, LRG_COLLECTIBLE_KIND_MOUNT, "mount_wolf", NULL));
    g_assert_cmpstr (lrg_collection_get_active (c, LRG_COLLECTIBLE_KIND_MOUNT), ==, "mount_wolf");

    before = snapshot (c);

    /* Kind mismatch: a pet cannot be the active mount. */
    g_assert_false (lrg_collection_set_active (c, LRG_COLLECTIBLE_KIND_MOUNT, "pet_cat", &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    assert_unchanged (c, before);
    g_assert_cmpstr (lrg_collection_get_active (c, LRG_COLLECTIBLE_KIND_MOUNT), ==, "mount_wolf");

    /* Not owned. */
    g_assert_false (lrg_collection_set_active (c, LRG_COLLECTIBLE_KIND_PET, "pet_dragon", &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_NOT_FOUND);
    g_clear_error (&error);
    assert_unchanged (c, before);
    g_assert_cmpstr (lrg_collection_get_active (c, LRG_COLLECTIBLE_KIND_PET), ==, "pet_owl");

    /* Unknown kind (with and without id). */
    g_assert_false (lrg_collection_set_active (c, (LrgCollectibleKind)9, "pet_cat", &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    g_assert_false (lrg_collection_set_active (c, (LrgCollectibleKind)9, NULL, &error));
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
    g_clear_error (&error);
    assert_unchanged (c, before);

    /* NULL clears; clearing an empty slot also succeeds. */
    g_assert_true (lrg_collection_set_active (c, LRG_COLLECTIBLE_KIND_TITLE, NULL, &error));
    g_assert_no_error (error);
    g_assert_null (lrg_collection_get_active (c, LRG_COLLECTIBLE_KIND_TITLE));
    g_assert_true (lrg_collection_set_active (c, LRG_COLLECTIBLE_KIND_TOY, NULL, NULL));
    g_assert_cmpstr (lrg_collection_get_active (c, LRG_COLLECTIBLE_KIND_MOUNT), ==, "mount_wolf");
}

static void
test_collection_remove (CollectionFixture *fixture,
                        gconstpointer      user_data)
{
    LrgCollection        *c = fixture->collection;
    g_autoptr(GPtrArray)  mounts = NULL;
    g_autoptr(GVariant)   before = NULL;

    (void)user_data;

    g_assert_true (lrg_collection_set_active (c, LRG_COLLECTIBLE_KIND_MOUNT, "mount_horse", NULL));
    g_assert_true (lrg_collection_set_active (c, LRG_COLLECTIBLE_KIND_PET, "pet_cat", NULL));
    g_assert_true (lrg_collection_set_favorite (c, "mount_horse", TRUE, NULL));
    g_assert_true (lrg_collection_set_favorite (c, "mount_wolf", TRUE, NULL));

    /* Removing a non-active mount keeps the active one. */
    g_assert_true (lrg_collection_remove (c, "mount_wolf"));
    g_assert_false (lrg_collection_has (c, "mount_wolf"));
    g_assert_false (lrg_collection_is_favorite (c, "mount_wolf"));
    g_assert_cmpstr (lrg_collection_get_active (c, LRG_COLLECTIBLE_KIND_MOUNT), ==, "mount_horse");

    /* Removing the active mount clears the active slot and favourite. */
    g_assert_true (lrg_collection_remove (c, "mount_horse"));
    g_assert_null (lrg_collection_get_active (c, LRG_COLLECTIBLE_KIND_MOUNT));
    g_assert_false (lrg_collection_is_favorite (c, "mount_horse"));
    g_assert_cmpstr (lrg_collection_get_active (c, LRG_COLLECTIBLE_KIND_PET), ==, "pet_cat");

    mounts = lrg_collection_get_ids (c, LRG_COLLECTIBLE_KIND_MOUNT);
    g_assert_cmpuint (mounts->len, ==, 1);
    g_assert_cmpstr (g_ptr_array_index (mounts, 0), ==, "mount_gryphon");
    g_assert_cmpuint (lrg_collection_get_total_count (c), ==, 4);

    /* Unknown / repeated / NULL removals change nothing. */
    before = snapshot (c);
    g_assert_false (lrg_collection_remove (c, "mount_horse"));
    g_assert_false (lrg_collection_remove (c, "nope"));
    g_assert_false (lrg_collection_remove (c, NULL));
    assert_unchanged (c, before);

    /* Re-adding starts fresh: not favourite, not active. */
    g_assert_true (lrg_collection_add (c, LRG_COLLECTIBLE_KIND_MOUNT, "mount_horse", NULL));
    g_assert_false (lrg_collection_is_favorite (c, "mount_horse"));
    g_assert_null (lrg_collection_get_active (c, LRG_COLLECTIBLE_KIND_MOUNT));

    /* An id freed from one kind may be re-added as another kind. */
    g_assert_true (lrg_collection_remove (c, "pet_cat"));
    g_assert_null (lrg_collection_get_active (c, LRG_COLLECTIBLE_KIND_PET));
    g_assert_true (lrg_collection_add (c, LRG_COLLECTIBLE_KIND_TOY, "pet_cat", NULL));
    g_assert_cmpuint (lrg_collection_get_count (c, LRG_COLLECTIBLE_KIND_TOY), ==, 1);
}

static void
test_collection_variant_round_trip (CollectionFixture *fixture,
                                    gconstpointer      user_data)
{
    LrgCollection            *c = fixture->collection;
    g_autoptr(GVariant)       first = NULL;
    g_autoptr(GVariant)       second = NULL;
    g_autoptr(GVariant)       entries = NULL;
    g_autoptr(GVariant)       actives = NULL;
    g_autoptr(LrgCollection)  restored = NULL;
    g_autoptr(GError)         error = NULL;
    guint32                   kind;
    const gchar              *id;
    gboolean                  fav;

    (void)user_data;

    g_assert_true (lrg_collection_add (c, LRG_COLLECTIBLE_KIND_TOY, "toy_drum", NULL));
    g_assert_true (lrg_collection_set_favorite (c, "pet_owl", TRUE, NULL));
    g_assert_true (lrg_collection_set_favorite (c, "toy_drum", TRUE, NULL));
    g_assert_true (lrg_collection_set_active (c, LRG_COLLECTIBLE_KIND_PET, "pet_owl", NULL));
    g_assert_true (lrg_collection_set_active (c, LRG_COLLECTIBLE_KIND_TOY, "toy_drum", NULL));
    g_assert_true (lrg_collection_set_active (c, LRG_COLLECTIBLE_KIND_MOUNT, "mount_wolf", NULL));

    first = lrg_collection_to_variant (c);
    g_assert_false (g_variant_is_floating (first));
    g_assert_cmpstr (g_variant_get_type_string (first), ==, LRG_COLLECTION_VARIANT_TYPE);
    g_assert_true (g_variant_is_normal_form (first));

    /* Order: sorted by (kind, id); actives sorted by kind. */
    entries = g_variant_get_child_value (first, 0);
    g_assert_cmpuint (g_variant_n_children (entries), ==, 7);
    g_variant_get_child (entries, 0, "(u&sb)", &kind, &id, &fav);
    g_assert_cmpuint (kind, ==, LRG_COLLECTIBLE_KIND_MOUNT);
    g_assert_cmpstr (id, ==, "mount_gryphon");
    g_assert_false (fav);
    g_variant_get_child (entries, 3, "(u&sb)", &kind, &id, &fav);
    g_assert_cmpuint (kind, ==, LRG_COLLECTIBLE_KIND_PET);
    g_assert_cmpstr (id, ==, "pet_cat");
    g_variant_get_child (entries, 4, "(u&sb)", &kind, &id, &fav);
    g_assert_cmpstr (id, ==, "pet_owl");
    g_assert_true (fav);
    g_variant_get_child (entries, 6, "(u&sb)", &kind, &id, &fav);
    g_assert_cmpuint (kind, ==, LRG_COLLECTIBLE_KIND_TOY);
    g_assert_cmpstr (id, ==, "toy_drum");

    actives = g_variant_get_child_value (first, 1);
    g_assert_cmpuint (g_variant_n_children (actives), ==, 3);
    g_variant_get_child (actives, 0, "(u&s)", &kind, &id);
    g_assert_cmpuint (kind, ==, LRG_COLLECTIBLE_KIND_MOUNT);
    g_assert_cmpstr (id, ==, "mount_wolf");
    g_variant_get_child (actives, 2, "(u&s)", &kind, &id);
    g_assert_cmpuint (kind, ==, LRG_COLLECTIBLE_KIND_TOY);

    restored = lrg_collection_new_from_variant (first, &error);
    g_assert_no_error (error);
    g_assert_nonnull (restored);
    second = lrg_collection_to_variant (restored);
    g_assert_true (g_variant_equal (first, second));

    g_assert_cmpstr (lrg_collection_get_active (restored, LRG_COLLECTIBLE_KIND_PET), ==, "pet_owl");
    g_assert_null (lrg_collection_get_active (restored, LRG_COLLECTIBLE_KIND_TITLE));
    g_assert_true (lrg_collection_is_favorite (restored, "toy_drum"));
    g_assert_false (lrg_collection_is_favorite (restored, "mount_wolf"));
    g_assert_cmpuint (lrg_collection_get_count (restored, LRG_COLLECTIBLE_KIND_MOUNT), ==, 3);
}

static void
test_collection_variant_empty_and_unsorted (void)
{
    g_autoptr(LrgCollection) empty = lrg_collection_new ();
    g_autoptr(GVariant)      v = lrg_collection_to_variant (empty);
    g_autoptr(GVariant)      v2 = NULL;
    g_autoptr(LrgCollection) restored = NULL;
    g_autoptr(GVariant)      unsorted = NULL;
    g_autoptr(LrgCollection) from_unsorted = NULL;
    g_autoptr(GVariant)      canonical = NULL;
    g_autoptr(GError)        error = NULL;
    g_autoptr(GVariant)      entries = NULL;
    const guint              kinds[] = { 3, 0, 0 };
    const gchar             *ids[] = { "toy_b", "mount_z", "mount_a" };
    const gboolean           favs[] = { TRUE, FALSE, TRUE };
    const guint              akinds[] = { 3, 0 };
    const gchar             *aids[] = { "toy_b", "mount_a" };
    const gchar             *id;
    guint32                  kind;
    gboolean                 fav;

    restored = lrg_collection_new_from_variant (v, &error);
    g_assert_no_error (error);
    v2 = lrg_collection_to_variant (restored);
    g_assert_true (g_variant_equal (v, v2));
    g_assert_cmpuint (lrg_collection_get_total_count (restored), ==, 0);

    /* Unsorted input is accepted and re-emitted canonically. */
    unsorted = build_variant (3, kinds, ids, favs, 2, akinds, aids);
    from_unsorted = lrg_collection_new_from_variant (unsorted, &error);
    g_assert_no_error (error);
    g_assert_nonnull (from_unsorted);
    canonical = lrg_collection_to_variant (from_unsorted);
    entries = g_variant_get_child_value (canonical, 0);
    g_variant_get_child (entries, 0, "(u&sb)", &kind, &id, &fav);
    g_assert_cmpstr (id, ==, "mount_a");
    g_assert_true (fav);
    g_variant_get_child (entries, 2, "(u&sb)", &kind, &id, &fav);
    g_assert_cmpstr (id, ==, "toy_b");
    g_assert_cmpuint (kind, ==, 3);
    g_assert_cmpstr (lrg_collection_get_active (from_unsorted, LRG_COLLECTIBLE_KIND_MOUNT), ==, "mount_a");
    g_assert_cmpstr (lrg_collection_get_active (from_unsorted, LRG_COLLECTIBLE_KIND_TOY), ==, "toy_b");
}

static void
test_collection_variant_floating_consumed (void)
{
    g_autoptr(LrgCollection) restored = NULL;
    g_autoptr(GError)        error = NULL;
    GVariantBuilder          entries;
    GVariantBuilder          actives;

    g_variant_builder_init (&entries, G_VARIANT_TYPE ("a(usb)"));
    g_variant_builder_init (&actives, G_VARIANT_TYPE ("a(us)"));
    g_variant_builder_add (&entries, "(usb)", 1u, "pet_cat", TRUE);
    g_variant_builder_add (&actives, "(us)", 1u, "pet_cat");

    /* A floating variant is sunk and released by the call (valgrind-checked). */
    restored = lrg_collection_new_from_variant (g_variant_new ("(a(usb)a(us))", &entries, &actives),
                                                &error);
    g_assert_no_error (error);
    g_assert_true (lrg_collection_is_favorite (restored, "pet_cat"));
    g_assert_cmpstr (lrg_collection_get_active (restored, LRG_COLLECTIBLE_KIND_PET), ==, "pet_cat");

    /* Floating and rejected: still consumed without a leak. */
    g_clear_object (&restored);
    restored = lrg_collection_new_from_variant (g_variant_new_string ("nope"), &error);
    g_assert_null (restored);
    g_assert_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID);
}

static void
test_collection_variant_wrong_type (void)
{
    g_autoptr(GVariant) s = g_variant_ref_sink (g_variant_new_string ("collection"));
    g_autoptr(GVariant) near_miss = NULL;
    g_autoptr(GVariant) extra = NULL;
    g_autoptr(GVariant) swapped = NULL;

    assert_restore_invalid (s);

    near_miss = g_variant_ref_sink (g_variant_new_parsed ("(@a(usb) [], @a(ss) [])"));
    assert_restore_invalid (near_miss);

    extra = g_variant_ref_sink (g_variant_new_parsed ("(@a(usb) [], @a(us) [], 1)"));
    assert_restore_invalid (extra);

    swapped = g_variant_ref_sink (g_variant_new_parsed ("(@a(us) [], @a(usb) [])"));
    assert_restore_invalid (swapped);
}

static void
test_collection_variant_not_normal (void)
{
    g_autoptr(GVariant) good = NULL;
    g_autoptr(GVariant) bad = NULL;
    g_autoptr(GVariant) truncated = NULL;
    g_autoptr(GBytes)   bytes = NULL;
    const guint         kinds[] = { 0 };
    const gchar        *ids[] = { "ABCD" };
    const gboolean      favs[] = { FALSE };
    gsize               size;
    guint8             *data;
    gsize               i;
    gboolean            patched = FALSE;

    good = build_variant (1, kinds, ids, favs, 0, NULL, NULL);
    size = g_variant_get_size (good);
    data = g_malloc (size);
    g_variant_store (good, data);

    /* Corrupt the id bytes into invalid UTF-8. */
    for (i = 0; i + 3 < size; i++)
    {
        if (memcmp (data + i, "ABCD", 4) == 0)
        {
            data[i + 1] = 0xff;
            patched = TRUE;
            break;
        }
    }
    g_assert_true (patched);
    bytes = g_bytes_new_take (data, size);
    bad = g_variant_ref_sink (g_variant_new_from_bytes (G_VARIANT_TYPE (LRG_COLLECTION_VARIANT_TYPE),
                                                        bytes, FALSE));
    g_assert_false (g_variant_is_normal_form (bad));
    assert_restore_invalid (bad);

    /* Truncated serialisation (wrong framing). */
    {
        g_autoptr(GBytes) whole = g_variant_get_data_as_bytes (good);
        g_autoptr(GBytes) part = g_bytes_new_from_bytes (whole, 0, g_bytes_get_size (whole) - 2);

        truncated = g_variant_ref_sink (g_variant_new_from_bytes (
            G_VARIANT_TYPE (LRG_COLLECTION_VARIANT_TYPE), part, FALSE));
        assert_restore_invalid (truncated);
    }
}

static void
test_collection_variant_hostile_entries (void)
{
    gchar long_id[LRG_COLLECTION_MAX_ID_LENGTH + 2];

    memset (long_id, 'z', LRG_COLLECTION_MAX_ID_LENGTH + 1);
    long_id[LRG_COLLECTION_MAX_ID_LENGTH + 1] = '\0';

    /* Unknown kind value in an entry. */
    {
        const guint     kinds[] = { 4 };
        const gchar    *ids[] = { "thing" };
        const gboolean  favs[] = { FALSE };
        g_autoptr(GVariant) v = build_variant (1, kinds, ids, favs, 0, NULL, NULL);
        assert_restore_invalid (v);
    }
    {
        const guint     kinds[] = { G_MAXUINT32 };
        const gchar    *ids[] = { "thing" };
        const gboolean  favs[] = { FALSE };
        g_autoptr(GVariant) v = build_variant (1, kinds, ids, favs, 0, NULL, NULL);
        assert_restore_invalid (v);
    }
    /* Empty id. */
    {
        const guint     kinds[] = { 0 };
        const gchar    *ids[] = { "" };
        const gboolean  favs[] = { FALSE };
        g_autoptr(GVariant) v = build_variant (1, kinds, ids, favs, 0, NULL, NULL);
        assert_restore_invalid (v);
    }
    /* Overlong id. */
    {
        const guint     kinds[] = { 0 };
        const gchar    *ids[] = { long_id };
        const gboolean  favs[] = { FALSE };
        g_autoptr(GVariant) v = build_variant (1, kinds, ids, favs, 0, NULL, NULL);
        assert_restore_invalid (v);
    }
    /* Duplicate id with the same kind. */
    {
        const guint     kinds[] = { 0, 0 };
        const gchar    *ids[] = { "mount_a", "mount_a" };
        const gboolean  favs[] = { FALSE, TRUE };
        g_autoptr(GVariant) v = build_variant (2, kinds, ids, favs, 0, NULL, NULL);
        assert_restore_invalid (v);
    }
    /* Duplicate id across kinds. */
    {
        const guint     kinds[] = { 0, 1 };
        const gchar    *ids[] = { "shared", "shared" };
        const gboolean  favs[] = { FALSE, FALSE };
        g_autoptr(GVariant) v = build_variant (2, kinds, ids, favs, 0, NULL, NULL);
        assert_restore_invalid (v);
    }
    /* Oversized entries array. */
    {
        GVariantBuilder     entries;
        g_autoptr(GVariant) v = NULL;
        guint               i;

        g_variant_builder_init (&entries, G_VARIANT_TYPE ("a(usb)"));
        for (i = 0; i < LRG_COLLECTION_MAX_ENTRIES + 1; i++)
        {
            g_autofree gchar *id = g_strdup_printf ("e%05u", i);
            g_variant_builder_add (&entries, "(usb)", 3u, id, FALSE);
        }
        v = g_variant_ref_sink (g_variant_new ("(a(usb)@a(us))", &entries,
                                               g_variant_new_array (G_VARIANT_TYPE ("(us)"), NULL, 0)));
        assert_restore_invalid (v);
    }
}

static void
test_collection_variant_hostile_actives (void)
{
    const guint     kinds[] = { 0, 1, 2, 3 };
    const gchar    *ids[] = { "mount_a", "pet_a", "title_a", "toy_a" };
    const gboolean  favs[] = { FALSE, FALSE, FALSE, FALSE };

    /* Unknown kind in an active row. */
    {
        const guint  ak[] = { 7 };
        const gchar *ai[] = { "mount_a" };
        g_autoptr(GVariant) v = build_variant (4, kinds, ids, favs, 1, ak, ai);
        assert_restore_invalid (v);
    }
    /* Active id not owned. */
    {
        const guint  ak[] = { 0 };
        const gchar *ai[] = { "mount_missing" };
        g_autoptr(GVariant) v = build_variant (4, kinds, ids, favs, 1, ak, ai);
        assert_restore_invalid (v);
    }
    /* Active id owned with a different kind. */
    {
        const guint  ak[] = { 0 };
        const gchar *ai[] = { "pet_a" };
        g_autoptr(GVariant) v = build_variant (4, kinds, ids, favs, 1, ak, ai);
        assert_restore_invalid (v);
    }
    /* Empty active id. */
    {
        const guint  ak[] = { 0 };
        const gchar *ai[] = { "" };
        g_autoptr(GVariant) v = build_variant (4, kinds, ids, favs, 1, ak, ai);
        assert_restore_invalid (v);
    }
    /* Duplicate active kind. */
    {
        const guint  ak[] = { 0, 0 };
        const gchar *ai[] = { "mount_a", "mount_a" };
        g_autoptr(GVariant) v = build_variant (4, kinds, ids, favs, 2, ak, ai);
        assert_restore_invalid (v);
    }
    /* More active rows than kinds. */
    {
        const guint  ak[] = { 0, 1, 2, 3, 0 };
        const gchar *ai[] = { "mount_a", "pet_a", "title_a", "toy_a", "mount_a" };
        g_autoptr(GVariant) v = build_variant (4, kinds, ids, favs, 5, ak, ai);
        assert_restore_invalid (v);
    }
    /* Every kind active at once is fine. */
    {
        const guint  ak[] = { 3, 2, 1, 0 };
        const gchar *ai[] = { "toy_a", "title_a", "pet_a", "mount_a" };
        g_autoptr(GVariant)      v = build_variant (4, kinds, ids, favs, 4, ak, ai);
        g_autoptr(GError)        error = NULL;
        g_autoptr(LrgCollection) c = lrg_collection_new_from_variant (v, &error);

        g_assert_no_error (error);
        g_assert_cmpstr (lrg_collection_get_active (c, LRG_COLLECTIBLE_KIND_TITLE), ==, "title_a");
    }
}

/* ========================================================================== */
/*                           LrgCompanionBrain                                */
/* ========================================================================== */

static void
test_companion_boxed (void)
{
    g_autoptr(LrgCompanionInput)  in = lrg_companion_input_new ();
    g_autoptr(LrgCompanionInput)  in_copy = NULL;
    g_autoptr(LrgCompanionOutput) out = lrg_companion_output_new ();
    g_autoptr(LrgCompanionOutput) out_copy = NULL;
    LrgCompanionInput            *boxed;

    /* Zero-initialised. */
    g_assert_cmpfloat (in->x, ==, 0.0);
    g_assert_cmpuint (in->owner_target, ==, 0);
    g_assert_false (in->alive);
    g_assert_false (in->owner_in_combat);
    g_assert_cmpint (out->action, ==, LRG_COMPANION_ACTION_IDLE);
    g_assert_cmpuint (out->target, ==, 0);

    in->x = 3.0;
    in->owner_target = 77;
    in->alive = TRUE;
    in_copy = lrg_companion_input_copy (in);
    g_assert_cmpfloat (in_copy->x, ==, 3.0);
    g_assert_cmpuint (in_copy->owner_target, ==, 77);
    g_assert_true (in_copy->alive);
    g_assert_true (in_copy != in);

    out->action = LRG_COMPANION_ACTION_ATTACK;
    out->target = 5;
    out->dest_x = 1.5;
    out_copy = lrg_companion_output_copy (out);
    g_assert_cmpint (out_copy->action, ==, LRG_COMPANION_ACTION_ATTACK);
    g_assert_cmpuint (out_copy->target, ==, 5);
    g_assert_cmpfloat (out_copy->dest_x, ==, 1.5);

    /* Registered boxed types copy through the generic API. */
    g_assert_true (G_TYPE_IS_BOXED (LRG_TYPE_COMPANION_INPUT));
    g_assert_true (G_TYPE_IS_BOXED (LRG_TYPE_COMPANION_OUTPUT));
    boxed = g_boxed_copy (LRG_TYPE_COMPANION_INPUT, in);
    g_assert_cmpuint (boxed->owner_target, ==, 77);
    g_boxed_free (LRG_TYPE_COMPANION_INPUT, boxed);

    lrg_companion_input_free (NULL);
    lrg_companion_output_free (NULL);
}

static void
test_companion_brain_defaults (void)
{
    g_autoptr(LrgCompanionBrain) brain = lrg_companion_brain_new (LRG_COMPANION_KIND_COMBAT);
    LrgCompanionStance           stance;
    LrgCompanionKind             kind;
    gdouble                      fd;
    gdouble                      fa;
    gdouble                      leash;
    gdouble                      tp;
    gdouble                      ar;

    g_assert_cmpint (lrg_companion_brain_get_kind (brain), ==, LRG_COMPANION_KIND_COMBAT);
    g_assert_cmpint (lrg_companion_brain_get_stance (brain), ==, LRG_COMPANION_STANCE_ASSIST);
    g_assert_cmpfloat_with_epsilon (lrg_companion_brain_get_follow_distance (brain), 2.0, EPS);
    g_assert_cmpfloat_with_epsilon (lrg_companion_brain_get_follow_angle (brain), 2.356, EPS);
    g_assert_cmpfloat_with_epsilon (lrg_companion_brain_get_leash_range (brain), 30.0, EPS);
    g_assert_cmpfloat_with_epsilon (lrg_companion_brain_get_teleport_range (brain), 60.0, EPS);
    g_assert_cmpfloat_with_epsilon (lrg_companion_brain_get_attack_range (brain), 3.0, EPS);
    g_assert_cmpuint (lrg_companion_brain_get_target (brain), ==, 0);
    g_assert_false (lrg_companion_brain_is_commanded (brain));

    g_object_set (brain, "stance", LRG_COMPANION_STANCE_DEFENSIVE, "kind", LRG_COMPANION_KIND_VANITY,
                  "follow-distance", 4.0, "follow-angle", 1.0, "leash-range", 40.0,
                  "teleport-range", 80.0, "attack-range", 5.0, NULL);
    g_object_get (brain, "stance", &stance, "kind", &kind, "follow-distance", &fd,
                  "follow-angle", &fa, "leash-range", &leash, "teleport-range", &tp,
                  "attack-range", &ar, NULL);
    g_assert_cmpint (stance, ==, LRG_COMPANION_STANCE_DEFENSIVE);
    g_assert_cmpint (kind, ==, LRG_COMPANION_KIND_VANITY);
    g_assert_cmpfloat_with_epsilon (fd, 4.0, EPS);
    g_assert_cmpfloat_with_epsilon (fa, 1.0, EPS);
    g_assert_cmpfloat_with_epsilon (leash, 40.0, EPS);
    g_assert_cmpfloat_with_epsilon (tp, 80.0, EPS);
    g_assert_cmpfloat_with_epsilon (ar, 5.0, EPS);
}

static void
test_companion_brain_invalid_setters (void)
{
    g_autoptr(LrgCompanionBrain) brain = lrg_companion_brain_new (LRG_COMPANION_KIND_COMBAT);
    guint                        notified = 0;

    g_signal_connect (brain, "notify", G_CALLBACK (count_notify), &notified);

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*isfinite*");
    lrg_companion_brain_set_leash_range (brain, NAN);
    g_test_assert_expected_messages ();
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*min*");
    lrg_companion_brain_set_teleport_range (brain, -1.0);
    g_test_assert_expected_messages ();
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*min*");
    lrg_companion_brain_set_attack_range (brain, 1e9);
    g_test_assert_expected_messages ();
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*isfinite*");
    lrg_companion_brain_set_follow_angle (brain, INFINITY);
    g_test_assert_expected_messages ();
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*min*");
    lrg_companion_brain_set_follow_distance (brain, -2.0);
    g_test_assert_expected_messages ();
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*stance*");
    lrg_companion_brain_set_stance (brain, (LrgCompanionStance)12);
    g_test_assert_expected_messages ();
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*kind*");
    lrg_companion_brain_set_kind (brain, (LrgCompanionKind)12);
    g_test_assert_expected_messages ();

    g_assert_cmpuint (notified, ==, 0);
    g_assert_cmpfloat_with_epsilon (lrg_companion_brain_get_leash_range (brain), 30.0, EPS);
    g_assert_cmpint (lrg_companion_brain_get_stance (brain), ==, LRG_COMPANION_STANCE_ASSIST);
    g_assert_cmpint (lrg_companion_brain_get_kind (brain), ==, LRG_COMPANION_KIND_COMBAT);

    /* Same-value sets do not notify; a change does exactly once. */
    lrg_companion_brain_set_stance (brain, LRG_COMPANION_STANCE_ASSIST);
    lrg_companion_brain_set_leash_range (brain, 30.0);
    g_assert_cmpuint (notified, ==, 0);
    lrg_companion_brain_set_stance (brain, LRG_COMPANION_STANCE_PASSIVE);
    g_assert_cmpuint (notified, ==, 1);
}

static void
test_companion_brain_new_for_pet (void)
{
    g_autoptr(LrgPetDef)         pet = lrg_pet_def_new ("pet_wolf", LRG_COMPANION_KIND_COMBAT);
    g_autoptr(LrgCompanionBrain) brain = NULL;

    lrg_pet_def_set_follow_distance (pet, 3.0);
    lrg_pet_def_set_follow_angle (pet, -0.5);
    lrg_pet_def_set_attack_range (pet, 1.25);

    brain = lrg_companion_brain_new_for_pet (pet);
    g_assert_cmpint (lrg_companion_brain_get_kind (brain), ==, LRG_COMPANION_KIND_COMBAT);
    g_assert_cmpfloat_with_epsilon (lrg_companion_brain_get_follow_distance (brain), 3.0, EPS);
    g_assert_cmpfloat_with_epsilon (lrg_companion_brain_get_follow_angle (brain), -0.5, EPS);
    g_assert_cmpfloat_with_epsilon (lrg_companion_brain_get_attack_range (brain), 1.25, EPS);
    g_assert_cmpint (lrg_companion_brain_get_stance (brain), ==, LRG_COMPANION_STANCE_ASSIST);
}

static void
test_companion_follow_point (void)
{
    g_autoptr(LrgCompanionBrain) brain = lrg_companion_brain_new (LRG_COMPANION_KIND_VANITY);
    gdouble                      x;
    gdouble                      z;

    /* follow-angle 0 puts the point straight ahead of the owner. */
    lrg_companion_brain_set_follow_angle (brain, 0.0);
    lrg_companion_brain_set_follow_distance (brain, 2.0);

    /* Facing 0 looks down -Z. */
    lrg_companion_brain_follow_point (brain, 10.0, 20.0, 0.0, &x, &z);
    g_assert_cmpfloat_with_epsilon (x, 10.0, EPS);
    g_assert_cmpfloat_with_epsilon (z, 18.0, EPS);

    /* +pi/2 rotates toward -X. */
    lrg_companion_brain_follow_point (brain, 10.0, 20.0, G_PI / 2.0, &x, &z);
    g_assert_cmpfloat_with_epsilon (x, 8.0, EPS);
    g_assert_cmpfloat_with_epsilon (z, 20.0, EPS);

    /* -pi/2 faces +X. */
    lrg_companion_brain_follow_point (brain, 10.0, 20.0, -G_PI / 2.0, &x, &z);
    g_assert_cmpfloat_with_epsilon (x, 12.0, EPS);
    g_assert_cmpfloat_with_epsilon (z, 20.0, EPS);

    /* pi faces +Z. */
    lrg_companion_brain_follow_point (brain, 10.0, 20.0, G_PI, &x, &z);
    g_assert_cmpfloat_with_epsilon (x, 10.0, EPS);
    g_assert_cmpfloat_with_epsilon (z, 22.0, EPS);

    /* The follow angle composes with facing: facing 0, angle pi => behind. */
    lrg_companion_brain_set_follow_angle (brain, G_PI);
    lrg_companion_brain_follow_point (brain, 0.0, 0.0, 0.0, &x, &z);
    g_assert_cmpfloat_with_epsilon (x, 0.0, EPS);
    g_assert_cmpfloat_with_epsilon (z, 2.0, EPS);
    lrg_companion_brain_follow_point (brain, 0.0, 0.0, G_PI / 2.0, &x, &z);
    g_assert_cmpfloat_with_epsilon (x, 2.0, EPS);
    g_assert_cmpfloat_with_epsilon (z, 0.0, EPS);

    /* Default angle (3pi/4-ish): behind-left of an owner facing -Z,
     * i.e. -X (left) and +Z (behind). */
    lrg_companion_brain_set_follow_angle (brain, 2.356);
    lrg_companion_brain_follow_point (brain, 0.0, 0.0, 0.0, &x, &z);
    g_assert_cmpfloat_with_epsilon (x, -2.0 * sin (2.356), EPS);
    g_assert_cmpfloat_with_epsilon (z, -2.0 * cos (2.356), EPS);
    g_assert_cmpfloat (x, <, -1.4);
    g_assert_cmpfloat (z, >, 1.4);

    /* Owner facing +X (-pi/2): behind is -X, left is -Z. */
    lrg_companion_brain_follow_point (brain, 0.0, 0.0, -G_PI / 2.0, &x, &z);
    g_assert_cmpfloat (x, <, -1.4);
    g_assert_cmpfloat (z, <, -1.4);

    /* Owner facing -X (+pi/2): behind is +X, left is +Z. */
    lrg_companion_brain_follow_point (brain, 0.0, 0.0, G_PI / 2.0, &x, &z);
    g_assert_cmpfloat (x, >, 1.4);
    g_assert_cmpfloat (z, >, 1.4);

    /* Zero distance collapses onto the owner. */
    lrg_companion_brain_set_follow_distance (brain, 0.0);
    lrg_companion_brain_follow_point (brain, 5.0, -5.0, 1.234, &x, &z);
    g_assert_cmpfloat_with_epsilon (x, 5.0, EPS);
    g_assert_cmpfloat_with_epsilon (z, -5.0, EPS);
}

/* Decision-table fixture ------------------------------------------------- */

#define HOSTILE_OWNER_TARGET   (10u)
#define HOSTILE_OWNER_ATTACKER (20u)
#define HOSTILE_SELF_ATTACKER  (30u)

typedef enum
{
    SIT_NONE,
    SIT_OWNER_TARGET_ALIVE,
    SIT_OWNER_TARGET_DEAD,
    SIT_OWNER_ATTACKED,
    SIT_SELF_ATTACKED,
    N_SITS
} Situation;

/* base_input:
 * Owner at the origin facing -Z; the companion sits exactly on the default
 * follow point so the no-target baseline is IDLE. Hostiles are all within
 * attack range of that spot. */
static void
base_input (LrgCompanionBrain *brain,
            LrgCompanionInput *in)
{
    memset (in, 0, sizeof (*in));
    in->alive = TRUE;
    in->owner_x = 0.0;
    in->owner_z = 0.0;
    in->owner_facing = 0.0;
    lrg_companion_brain_follow_point (brain, 0.0, 0.0, 0.0, &in->x, &in->z);
}

static void
apply_situation (LrgCompanionInput *in,
                 Situation          sit)
{
    switch (sit)
    {
    case SIT_NONE:
        break;
    case SIT_OWNER_TARGET_ALIVE:
    case SIT_OWNER_TARGET_DEAD:
        in->owner_target = HOSTILE_OWNER_TARGET;
        in->owner_target_alive = (sit == SIT_OWNER_TARGET_ALIVE);
        in->owner_target_x = 0.0;
        in->owner_target_z = 0.5;
        in->owner_in_combat = TRUE;
        break;
    case SIT_OWNER_ATTACKED:
        in->owner_attacker = HOSTILE_OWNER_ATTACKER;
        in->owner_attacker_x = -0.5;
        in->owner_attacker_z = 0.5;
        in->owner_in_combat = TRUE;
        break;
    case SIT_SELF_ATTACKED:
        in->self_attacker = HOSTILE_SELF_ATTACKER;
        in->self_attacker_x = -1.0;
        in->self_attacker_z = 1.0;
        break;
    case N_SITS:
    default:
        g_assert_not_reached ();
    }
}

static void
test_companion_decision_table (void)
{
    /* expected target per [stance][situation]; 0 => IDLE */
    static const guint expected[4][N_SITS] = {
        /* PASSIVE */    { 0, 0,                    0, 0,                      0 },
        /* DEFENSIVE */  { 0, 0,                    0, HOSTILE_OWNER_ATTACKER, HOSTILE_SELF_ATTACKER },
        /* ASSIST */     { 0, HOSTILE_OWNER_TARGET, 0, HOSTILE_OWNER_ATTACKER, HOSTILE_SELF_ATTACKER },
        /* AGGRESSIVE */ { 0, HOSTILE_OWNER_TARGET, 0, HOSTILE_OWNER_ATTACKER, HOSTILE_SELF_ATTACKER },
    };
    guint kind;
    guint stance;
    guint sit;

    for (kind = LRG_COMPANION_KIND_VANITY; kind <= LRG_COMPANION_KIND_COMBAT; kind++)
    {
        for (stance = LRG_COMPANION_STANCE_PASSIVE; stance <= LRG_COMPANION_STANCE_AGGRESSIVE; stance++)
        {
            for (sit = 0; sit < N_SITS; sit++)
            {
                g_autoptr(LrgCompanionBrain) brain = lrg_companion_brain_new ((LrgCompanionKind)kind);
                LrgCompanionInput            in;
                LrgCompanionOutput           out;
                guint                        want;

                lrg_companion_brain_set_stance (brain, (LrgCompanionStance)stance);
                base_input (brain, &in);
                apply_situation (&in, (Situation)sit);

                /* Pre-fill garbage to prove think() writes every field. */
                out.action = LRG_COMPANION_ACTION_RETURN;
                out.dest_x = 999.0;
                out.dest_z = 999.0;
                out.target = 12345;
                lrg_companion_brain_think (brain, &in, &out);

                want = (kind == LRG_COMPANION_KIND_VANITY) ? 0 : expected[stance][sit];
                if (g_test_verbose ())
                    g_test_message ("kind=%u stance=%u sit=%u -> action=%d target=%u",
                                    kind, stance, sit, out.action, out.target);

                if (want == 0)
                {
                    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_IDLE);
                    g_assert_cmpuint (out.target, ==, 0);
                    g_assert_cmpfloat_with_epsilon (out.dest_x, in.x, EPS);
                    g_assert_cmpfloat_with_epsilon (out.dest_z, in.z, EPS);
                }
                else
                {
                    /* All hostiles are within attack range: hold position. */
                    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_ATTACK);
                    g_assert_cmpuint (out.target, ==, want);
                    g_assert_cmpfloat_with_epsilon (out.dest_x, in.x, EPS);
                    g_assert_cmpfloat_with_epsilon (out.dest_z, in.z, EPS);
                }
                g_assert_cmpuint (lrg_companion_brain_get_target (brain), ==, want);
                g_assert_false (lrg_companion_brain_is_commanded (brain));
            }
        }
    }
}

static void
test_companion_priorities (void)
{
    g_autoptr(LrgCompanionBrain) brain = NULL;
    LrgCompanionInput            in;
    LrgCompanionOutput           out;

    /* ASSIST: owner target beats attackers. */
    brain = lrg_companion_brain_new (LRG_COMPANION_KIND_COMBAT);
    base_input (brain, &in);
    apply_situation (&in, SIT_OWNER_TARGET_ALIVE);
    apply_situation (&in, SIT_OWNER_ATTACKED);
    apply_situation (&in, SIT_SELF_ATTACKED);
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpuint (out.target, ==, HOSTILE_OWNER_TARGET);
    g_clear_object (&brain);

    /* AGGRESSIVE: same as ASSIST. */
    brain = lrg_companion_brain_new (LRG_COMPANION_KIND_COMBAT);
    lrg_companion_brain_set_stance (brain, LRG_COMPANION_STANCE_AGGRESSIVE);
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpuint (out.target, ==, HOSTILE_OWNER_TARGET);
    g_clear_object (&brain);

    /* DEFENSIVE: owner attacker beats self attacker; owner target ignored. */
    brain = lrg_companion_brain_new (LRG_COMPANION_KIND_COMBAT);
    lrg_companion_brain_set_stance (brain, LRG_COMPANION_STANCE_DEFENSIVE);
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpuint (out.target, ==, HOSTILE_OWNER_ATTACKER);
    g_clear_object (&brain);

    /* ASSIST with a dead owner target falls back to attackers. */
    brain = lrg_companion_brain_new (LRG_COMPANION_KIND_COMBAT);
    base_input (brain, &in);
    apply_situation (&in, SIT_OWNER_TARGET_DEAD);
    apply_situation (&in, SIT_SELF_ATTACKED);
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_ATTACK);
    g_assert_cmpuint (out.target, ==, HOSTILE_SELF_ATTACKER);
    g_clear_object (&brain);

    /* A hostile with a non-finite position is unknown. */
    brain = lrg_companion_brain_new (LRG_COMPANION_KIND_COMBAT);
    base_input (brain, &in);
    apply_situation (&in, SIT_OWNER_TARGET_ALIVE);
    in.owner_target_x = NAN;
    apply_situation (&in, SIT_OWNER_ATTACKED);
    in.owner_attacker_z = INFINITY;
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_IDLE);
    g_assert_cmpuint (out.target, ==, 0);

    /* ...but the same id may still be resolved through another slot. */
    in.self_attacker = HOSTILE_OWNER_TARGET;
    in.self_attacker_x = 0.0;
    in.self_attacker_z = 0.0;
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_ATTACK);
    g_assert_cmpuint (out.target, ==, HOSTILE_OWNER_TARGET);

    /* A dead owner target is invalid even if it is also listed as attacker. */
    in.owner_target_x = 0.0;
    in.owner_target_alive = FALSE;
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_IDLE);
    g_assert_cmpuint (lrg_companion_brain_get_target (brain), ==, 0);
}

static void
test_companion_target_persistence (void)
{
    g_autoptr(LrgCompanionBrain) brain = lrg_companion_brain_new (LRG_COMPANION_KIND_COMBAT);
    LrgCompanionInput            in;
    LrgCompanionOutput           out;

    /* ASSIST picks the owner attacker when there is no owner target. */
    base_input (brain, &in);
    apply_situation (&in, SIT_OWNER_ATTACKED);
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpuint (out.target, ==, HOSTILE_OWNER_ATTACKER);

    /* The owner now targets something else: the current target is kept. */
    apply_situation (&in, SIT_OWNER_TARGET_ALIVE);
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpuint (out.target, ==, HOSTILE_OWNER_ATTACKER);

    /* The attacker disappears: reselect by stance (owner target). */
    in.owner_attacker = 0;
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpuint (out.target, ==, HOSTILE_OWNER_TARGET);

    /* The owner target dies: target dropped, back to IDLE. */
    in.owner_target_alive = FALSE;
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_IDLE);
    g_assert_cmpuint (lrg_companion_brain_get_target (brain), ==, 0);
}

static void
test_companion_stance_passive_drops_auto_target (void)
{
    g_autoptr(LrgCompanionBrain) brain = lrg_companion_brain_new (LRG_COMPANION_KIND_COMBAT);
    LrgCompanionInput            in;
    LrgCompanionOutput           out;

    lrg_companion_brain_set_stance (brain, LRG_COMPANION_STANCE_DEFENSIVE);
    base_input (brain, &in);
    apply_situation (&in, SIT_SELF_ATTACKED);
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpuint (out.target, ==, HOSTILE_SELF_ATTACKER);

    /* Changing stance keeps the target until the next think. */
    lrg_companion_brain_set_stance (brain, LRG_COMPANION_STANCE_PASSIVE);
    g_assert_cmpuint (lrg_companion_brain_get_target (brain), ==, HOSTILE_SELF_ATTACKER);
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_IDLE);
    g_assert_cmpuint (lrg_companion_brain_get_target (brain), ==, 0);
}

static void
test_companion_commands (void)
{
    g_autoptr(LrgCompanionBrain) brain = lrg_companion_brain_new (LRG_COMPANION_KIND_COMBAT);
    g_autoptr(LrgCompanionBrain) vanity = lrg_companion_brain_new (LRG_COMPANION_KIND_VANITY);
    LrgCompanionInput            in;
    LrgCompanionOutput           out;

    /* Explicit attack overrides PASSIVE. */
    lrg_companion_brain_set_stance (brain, LRG_COMPANION_STANCE_PASSIVE);
    lrg_companion_brain_command_attack (brain, HOSTILE_OWNER_TARGET);
    g_assert_cmpuint (lrg_companion_brain_get_target (brain), ==, HOSTILE_OWNER_TARGET);
    g_assert_true (lrg_companion_brain_is_commanded (brain));
    base_input (brain, &in);
    apply_situation (&in, SIT_OWNER_TARGET_ALIVE);
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_ATTACK);
    g_assert_cmpuint (out.target, ==, HOSTILE_OWNER_TARGET);
    g_assert_true (lrg_companion_brain_is_commanded (brain));

    /* Explicit attack overrides the stance's own choice. */
    lrg_companion_brain_set_stance (brain, LRG_COMPANION_STANCE_ASSIST);
    apply_situation (&in, SIT_SELF_ATTACKED);
    lrg_companion_brain_command_attack (brain, HOSTILE_SELF_ATTACKER);
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpuint (out.target, ==, HOSTILE_SELF_ATTACKER);

    /* command_follow clears; the stance may then re-acquire. */
    lrg_companion_brain_command_follow (brain);
    g_assert_cmpuint (lrg_companion_brain_get_target (brain), ==, 0);
    g_assert_false (lrg_companion_brain_is_commanded (brain));
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpuint (out.target, ==, HOSTILE_OWNER_TARGET);
    g_assert_false (lrg_companion_brain_is_commanded (brain));

    /* In PASSIVE, command_follow means back to following. */
    lrg_companion_brain_set_stance (brain, LRG_COMPANION_STANCE_PASSIVE);
    lrg_companion_brain_command_attack (brain, HOSTILE_OWNER_TARGET);
    lrg_companion_brain_command_follow (brain);
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_IDLE);

    /* An ordered target that is not in the snapshot is dropped. */
    lrg_companion_brain_command_attack (brain, 999);
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_IDLE);
    g_assert_cmpuint (lrg_companion_brain_get_target (brain), ==, 0);
    g_assert_false (lrg_companion_brain_is_commanded (brain));

    /* command_attack(0) clears. */
    lrg_companion_brain_command_attack (brain, HOSTILE_OWNER_TARGET);
    lrg_companion_brain_command_attack (brain, 0);
    g_assert_cmpuint (lrg_companion_brain_get_target (brain), ==, 0);
    g_assert_false (lrg_companion_brain_is_commanded (brain));

    /* VANITY ignores attack orders entirely. */
    lrg_companion_brain_command_attack (vanity, HOSTILE_OWNER_TARGET);
    g_assert_cmpuint (lrg_companion_brain_get_target (vanity), ==, 0);
    g_assert_false (lrg_companion_brain_is_commanded (vanity));
    base_input (vanity, &in);
    apply_situation (&in, SIT_OWNER_TARGET_ALIVE);
    lrg_companion_brain_think (vanity, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_IDLE);
    g_assert_cmpuint (out.target, ==, 0);

    /* Switching a fighting brain to VANITY clears its target. */
    lrg_companion_brain_set_stance (brain, LRG_COMPANION_STANCE_ASSIST);
    base_input (brain, &in);
    apply_situation (&in, SIT_OWNER_TARGET_ALIVE);
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpuint (lrg_companion_brain_get_target (brain), ==, HOSTILE_OWNER_TARGET);
    lrg_companion_brain_set_kind (brain, LRG_COMPANION_KIND_VANITY);
    g_assert_cmpuint (lrg_companion_brain_get_target (brain), ==, 0);
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_IDLE);
}

static void
test_companion_dead (void)
{
    g_autoptr(LrgCompanionBrain) brain = lrg_companion_brain_new (LRG_COMPANION_KIND_COMBAT);
    LrgCompanionInput            in;
    LrgCompanionOutput           out;

    lrg_companion_brain_command_attack (brain, HOSTILE_OWNER_TARGET);
    base_input (brain, &in);
    apply_situation (&in, SIT_OWNER_TARGET_ALIVE);
    apply_situation (&in, SIT_SELF_ATTACKED);
    in.alive = FALSE;
    in.x = 50.0;       /* far away too: still IDLE, never TELEPORT */
    in.z = 50.0;
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_IDLE);
    g_assert_cmpuint (out.target, ==, 0);
    g_assert_cmpfloat (out.dest_x, ==, 50.0);
    g_assert_cmpfloat (out.dest_z, ==, 50.0);
    g_assert_cmpuint (lrg_companion_brain_get_target (brain), ==, 0);
    g_assert_false (lrg_companion_brain_is_commanded (brain));

    /* Garbage geometry behaves like death (never NaN destinations). */
    base_input (brain, &in);
    in.x = NAN;
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_IDLE);
    g_assert_cmpfloat (out.dest_x, ==, 0.0);
    g_assert_cmpfloat (out.dest_z, ==, 0.0);

    base_input (brain, &in);
    in.owner_facing = INFINITY;
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_IDLE);
    g_assert_cmpfloat_with_epsilon (out.dest_x, in.x, EPS);

    base_input (brain, &in);
    in.owner_z = NAN;
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_IDLE);
}

static void
test_companion_attack_range_vs_chase (void)
{
    g_autoptr(LrgCompanionBrain) brain = lrg_companion_brain_new (LRG_COMPANION_KIND_COMBAT);
    LrgCompanionInput            in;
    LrgCompanionOutput           out;

    /* Companion at (0,-2) (owner at origin), target straight down -Z. */
    memset (&in, 0, sizeof (in));
    in.alive = TRUE;
    in.x = 0.0;
    in.z = -2.0;
    in.owner_target = HOSTILE_OWNER_TARGET;
    in.owner_target_alive = TRUE;
    in.owner_target_x = 0.0;

    /* Exactly at attack range: hold position and strike. */
    in.owner_target_z = -5.0;
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_ATTACK);
    g_assert_cmpuint (out.target, ==, HOSTILE_OWNER_TARGET);
    g_assert_cmpfloat (out.dest_x, ==, 0.0);
    g_assert_cmpfloat (out.dest_z, ==, -2.0);

    /* Just outside: chase toward the target's position. */
    in.owner_target_z = -5.5;
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_ATTACK);
    g_assert_cmpuint (out.target, ==, HOSTILE_OWNER_TARGET);
    g_assert_cmpfloat (out.dest_x, ==, 0.0);
    g_assert_cmpfloat (out.dest_z, ==, -5.5);

    /* A far-away target is still chased while the companion is leashed. */
    in.owner_target_z = -200.0;
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_ATTACK);
    g_assert_cmpfloat (out.dest_z, ==, -200.0);

    /* A custom range. */
    lrg_companion_brain_set_attack_range (brain, 10.0);
    in.owner_target_z = -11.5;
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpfloat (out.dest_z, ==, -2.0);
}

static void
test_companion_leash (void)
{
    g_autoptr(LrgCompanionBrain) brain = lrg_companion_brain_new (LRG_COMPANION_KIND_COMBAT);
    LrgCompanionInput            in;
    LrgCompanionOutput           out;
    gdouble                      fx;
    gdouble                      fz;

    lrg_companion_brain_follow_point (brain, 0.0, 0.0, 0.0, &fx, &fz);

    /* Inside the leash (exactly 30): fight. */
    memset (&in, 0, sizeof (in));
    in.alive = TRUE;
    in.x = 30.0;
    in.z = 0.0;
    in.self_attacker = HOSTILE_SELF_ATTACKER;
    in.self_attacker_x = 31.0;
    in.self_attacker_z = 0.0;
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_ATTACK);
    g_assert_cmpuint (lrg_companion_brain_get_target (brain), ==, HOSTILE_SELF_ATTACKER);

    /* Past the leash: RETURN to the follow point, target cleared. */
    in.x = 30.5;
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_RETURN);
    g_assert_cmpuint (out.target, ==, 0);
    g_assert_cmpfloat_with_epsilon (out.dest_x, fx, EPS);
    g_assert_cmpfloat_with_epsilon (out.dest_z, fz, EPS);
    g_assert_cmpuint (lrg_companion_brain_get_target (brain), ==, 0);

    /* Leash also breaks an explicit order. */
    lrg_companion_brain_command_attack (brain, HOSTILE_SELF_ATTACKER);
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_RETURN);
    g_assert_false (lrg_companion_brain_is_commanded (brain));

    /* Leashed without any hostiles: still RETURN. */
    in.self_attacker = 0;
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_RETURN);

    /* Far beyond the teleport range: TELEPORT wins over RETURN. */
    in.x = 200.0;
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_TELEPORT);
    g_assert_cmpfloat_with_epsilon (out.dest_x, fx, EPS);
    g_assert_cmpfloat_with_epsilon (out.dest_z, fz, EPS);

    /* Back within the leash, the fight resumes. */
    in.x = 1.0;
    in.self_attacker = HOSTILE_SELF_ATTACKER;
    in.self_attacker_x = 1.0;
    in.self_attacker_z = 1.0;
    lrg_companion_brain_think (brain, &in, &out);
    g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_ATTACK);
}

static void
test_companion_follow_thresholds (void)
{
    guint kind;

    for (kind = LRG_COMPANION_KIND_VANITY; kind <= LRG_COMPANION_KIND_COMBAT; kind++)
    {
        g_autoptr(LrgCompanionBrain) brain = lrg_companion_brain_new ((LrgCompanionKind)kind);
        LrgCompanionInput            in;
        LrgCompanionOutput           out;

        /* follow-angle 0, distance 2: follow point exactly (0,-2). A huge
         * leash isolates the follow rule for combat companions. */
        lrg_companion_brain_set_follow_angle (brain, 0.0);
        lrg_companion_brain_set_leash_range (brain, 1000.0);

        memset (&in, 0, sizeof (in));
        in.alive = TRUE;

        /* On the point: IDLE. */
        in.x = 0.0;
        in.z = -2.0;
        lrg_companion_brain_think (brain, &in, &out);
        g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_IDLE);

        /* Exactly the arrive distance: still IDLE. */
        in.z = -2.0 + LRG_COMPANION_ARRIVE_DISTANCE;
        lrg_companion_brain_think (brain, &in, &out);
        g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_IDLE);
        g_assert_cmpfloat (out.dest_z, ==, in.z);

        /* Just beyond: FOLLOW to the point. */
        in.z = -1.0;
        lrg_companion_brain_think (brain, &in, &out);
        g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_FOLLOW);
        g_assert_cmpfloat_with_epsilon (out.dest_x, 0.0, EPS);
        g_assert_cmpfloat_with_epsilon (out.dest_z, -2.0, EPS);
        g_assert_cmpuint (out.target, ==, 0);

        /* Exactly the teleport range: FOLLOW. */
        in.z = 58.0;
        lrg_companion_brain_think (brain, &in, &out);
        g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_FOLLOW);

        /* Beyond it: TELEPORT to the point. */
        in.z = 58.5;
        lrg_companion_brain_think (brain, &in, &out);
        g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_TELEPORT);
        g_assert_cmpfloat_with_epsilon (out.dest_x, 0.0, EPS);
        g_assert_cmpfloat_with_epsilon (out.dest_z, -2.0, EPS);

        /* The owner turning moves the point: facing -X puts it at (-2,0). */
        in.owner_facing = G_PI / 2.0;
        in.x = 0.0;
        in.z = 0.0;
        lrg_companion_brain_think (brain, &in, &out);
        g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_FOLLOW);
        g_assert_cmpfloat_with_epsilon (out.dest_x, -2.0, EPS);
        g_assert_cmpfloat_with_epsilon (out.dest_z, 0.0, EPS);
    }
}

static void
test_companion_vanity_never_returns (void)
{
    g_autoptr(LrgCompanionBrain) brain = lrg_companion_brain_new (LRG_COMPANION_KIND_VANITY);
    LrgCompanionInput            in;
    LrgCompanionOutput           out;
    guint                        stance;

    for (stance = LRG_COMPANION_STANCE_PASSIVE; stance <= LRG_COMPANION_STANCE_AGGRESSIVE; stance++)
    {
        lrg_companion_brain_set_stance (brain, (LrgCompanionStance)stance);
        memset (&in, 0, sizeof (in));
        in.alive = TRUE;
        in.x = 35.0;   /* beyond leash, within teleport */
        apply_situation (&in, SIT_OWNER_TARGET_ALIVE);
        apply_situation (&in, SIT_OWNER_ATTACKED);
        apply_situation (&in, SIT_SELF_ATTACKED);
        in.self_attacker_x = 35.0;
        in.self_attacker_z = 0.0;
        lrg_companion_brain_think (brain, &in, &out);
        g_assert_cmpint (out.action, ==, LRG_COMPANION_ACTION_FOLLOW);
        g_assert_cmpuint (out.target, ==, 0);
    }
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Definitions */
    g_test_add_func ("/collection/collectible-def/defaults", test_collectible_def_defaults);
    g_test_add_func ("/collection/collectible-def/g-object-new-kind", test_collectible_def_g_object_new_default_kind);
    g_test_add_func ("/collection/collectible-def/properties", test_collectible_def_properties);
    g_test_add_func ("/collection/collectible-def/notify", test_collectible_def_notify_only_on_change);
    g_test_add_func ("/collection/collectible-def/can-obtain", test_collectible_def_can_obtain);
    g_test_add_func ("/collection/collectible-def/invalid-rarity", test_collectible_def_invalid_rarity);
    g_test_add_func ("/collection/mount-def/defaults", test_mount_def_defaults);
    g_test_add_func ("/collection/mount-def/kind-pinned", test_mount_def_kind_pinned);
    g_test_add_func ("/collection/mount-def/properties", test_mount_def_properties);
    g_test_add_func ("/collection/mount-def/invalid-setters", test_mount_def_invalid_setters);
    g_test_add_func ("/collection/mount-def/can-ride", test_mount_def_can_ride);
    g_test_add_func ("/collection/mount-def/speed", test_mount_def_speed);
    g_test_add_func ("/collection/pet-def/defaults", test_pet_def_defaults);
    g_test_add_func ("/collection/pet-def/kind-pinned", test_pet_def_kind_pinned);
    g_test_add_func ("/collection/pet-def/properties", test_pet_def_properties);
    g_test_add_func ("/collection/pet-def/invalid-setters", test_pet_def_invalid_setters);
    g_test_add_func ("/collection/pet-def/abilities", test_pet_def_abilities);

    /* Collection */
    g_test_add_func ("/collection/collection/empty", test_collection_empty);
    g_test_add ("/collection/collection/add-has-count-ids", CollectionFixture, NULL,
                collection_fixture_setup, test_collection_add_has_count_ids,
                collection_fixture_teardown);
    g_test_add ("/collection/collection/add-rejections", CollectionFixture, NULL,
                collection_fixture_setup, test_collection_add_rejections,
                collection_fixture_teardown);
    g_test_add_func ("/collection/collection/add-limit", test_collection_add_limit);
    g_test_add ("/collection/collection/favorites", CollectionFixture, NULL,
                collection_fixture_setup, test_collection_favorites,
                collection_fixture_teardown);
    g_test_add ("/collection/collection/active", CollectionFixture, NULL,
                collection_fixture_setup, test_collection_active,
                collection_fixture_teardown);
    g_test_add ("/collection/collection/remove", CollectionFixture, NULL,
                collection_fixture_setup, test_collection_remove,
                collection_fixture_teardown);
    g_test_add ("/collection/collection/variant-round-trip", CollectionFixture, NULL,
                collection_fixture_setup, test_collection_variant_round_trip,
                collection_fixture_teardown);
    g_test_add_func ("/collection/collection/variant-empty-unsorted", test_collection_variant_empty_and_unsorted);
    g_test_add_func ("/collection/collection/variant-floating", test_collection_variant_floating_consumed);
    g_test_add_func ("/collection/collection/variant-wrong-type", test_collection_variant_wrong_type);
    g_test_add_func ("/collection/collection/variant-not-normal", test_collection_variant_not_normal);
    g_test_add_func ("/collection/collection/variant-hostile-entries", test_collection_variant_hostile_entries);
    g_test_add_func ("/collection/collection/variant-hostile-actives", test_collection_variant_hostile_actives);

    /* Companion brain */
    g_test_add_func ("/collection/companion/boxed", test_companion_boxed);
    g_test_add_func ("/collection/companion/defaults", test_companion_brain_defaults);
    g_test_add_func ("/collection/companion/invalid-setters", test_companion_brain_invalid_setters);
    g_test_add_func ("/collection/companion/new-for-pet", test_companion_brain_new_for_pet);
    g_test_add_func ("/collection/companion/follow-point", test_companion_follow_point);
    g_test_add_func ("/collection/companion/decision-table", test_companion_decision_table);
    g_test_add_func ("/collection/companion/priorities", test_companion_priorities);
    g_test_add_func ("/collection/companion/target-persistence", test_companion_target_persistence);
    g_test_add_func ("/collection/companion/passive-drops-auto-target", test_companion_stance_passive_drops_auto_target);
    g_test_add_func ("/collection/companion/commands", test_companion_commands);
    g_test_add_func ("/collection/companion/dead", test_companion_dead);
    g_test_add_func ("/collection/companion/attack-range-vs-chase", test_companion_attack_range_vs_chase);
    g_test_add_func ("/collection/companion/leash", test_companion_leash);
    g_test_add_func ("/collection/companion/follow-thresholds", test_companion_follow_thresholds);
    g_test_add_func ("/collection/companion/vanity-never-returns", test_companion_vanity_never_returns);

    return g_test_run ();
}
