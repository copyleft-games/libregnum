/* test-deckbuilder.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the deckbuilder module.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgCardDef      *strike;
    LrgCardDef      *defend;
    LrgCardDef      *power_card;
    LrgCardDef      *curse;
    LrgCardPile     *draw_pile;
    LrgCardPile     *discard_pile;
    LrgHand         *hand;
} DeckbuilderFixture;

static void
deckbuilder_fixture_set_up (DeckbuilderFixture *fixture,
                            gconstpointer       user_data)
{
    (void)user_data;

    /* Create a basic attack card */
    fixture->strike = lrg_card_def_new ("strike");
    lrg_card_def_set_name (fixture->strike, "Strike");
    lrg_card_def_set_description (fixture->strike, "Deal 6 damage.");
    lrg_card_def_set_card_type (fixture->strike, LRG_CARD_TYPE_ATTACK);
    lrg_card_def_set_rarity (fixture->strike, LRG_CARD_RARITY_STARTER);
    lrg_card_def_set_base_cost (fixture->strike, 1);
    lrg_card_def_set_target_type (fixture->strike, LRG_CARD_TARGET_SINGLE_ENEMY);
    lrg_card_def_set_upgradeable (fixture->strike, TRUE);

    /* Create a basic skill card */
    fixture->defend = lrg_card_def_new ("defend");
    lrg_card_def_set_name (fixture->defend, "Defend");
    lrg_card_def_set_description (fixture->defend, "Gain 5 block.");
    lrg_card_def_set_card_type (fixture->defend, LRG_CARD_TYPE_SKILL);
    lrg_card_def_set_rarity (fixture->defend, LRG_CARD_RARITY_STARTER);
    lrg_card_def_set_base_cost (fixture->defend, 1);
    lrg_card_def_set_target_type (fixture->defend, LRG_CARD_TARGET_SELF);

    /* Create a power card with Exhaust */
    fixture->power_card = lrg_card_def_new ("demon_form");
    lrg_card_def_set_name (fixture->power_card, "Demon Form");
    lrg_card_def_set_description (fixture->power_card, "At the start of each turn, gain 2 Strength.");
    lrg_card_def_set_card_type (fixture->power_card, LRG_CARD_TYPE_POWER);
    lrg_card_def_set_rarity (fixture->power_card, LRG_CARD_RARITY_RARE);
    lrg_card_def_set_base_cost (fixture->power_card, 3);
    lrg_card_def_set_target_type (fixture->power_card, LRG_CARD_TARGET_SELF);

    /* Create a curse card */
    fixture->curse = lrg_card_def_new ("regret");
    lrg_card_def_set_name (fixture->curse, "Regret");
    lrg_card_def_set_description (fixture->curse, "Unplayable. At the end of your turn, lose 1 HP for each card in hand.");
    lrg_card_def_set_card_type (fixture->curse, LRG_CARD_TYPE_CURSE);
    lrg_card_def_set_rarity (fixture->curse, LRG_CARD_RARITY_SPECIAL);
    lrg_card_def_set_base_cost (fixture->curse, -1);
    lrg_card_def_set_keywords (fixture->curse, LRG_CARD_KEYWORD_UNPLAYABLE);

    /* Create piles */
    fixture->draw_pile = lrg_card_pile_new_with_zone (LRG_ZONE_DRAW);
    fixture->discard_pile = lrg_card_pile_new_with_zone (LRG_ZONE_DISCARD);

    /* Create hand */
    fixture->hand = lrg_hand_new ();
}

static void
deckbuilder_fixture_tear_down (DeckbuilderFixture *fixture,
                               gconstpointer       user_data)
{
    (void)user_data;

    g_clear_object (&fixture->strike);
    g_clear_object (&fixture->defend);
    g_clear_object (&fixture->power_card);
    g_clear_object (&fixture->curse);
    g_clear_object (&fixture->draw_pile);
    g_clear_object (&fixture->discard_pile);
    g_clear_object (&fixture->hand);
}

/* ==========================================================================
 * LrgCardDef Tests
 * ========================================================================== */

static void
test_card_def_new (void)
{
    g_autoptr(LrgCardDef) def = NULL;

    def = lrg_card_def_new ("test_card");

    g_assert_nonnull (def);
    g_assert_cmpstr (lrg_card_def_get_id (def), ==, "test_card");
    g_assert_null (lrg_card_def_get_name (def));
    g_assert_null (lrg_card_def_get_description (def));
    g_assert_cmpint (lrg_card_def_get_card_type (def), ==, LRG_CARD_TYPE_ATTACK);
    g_assert_cmpint (lrg_card_def_get_rarity (def), ==, LRG_CARD_RARITY_COMMON);
    g_assert_cmpint (lrg_card_def_get_base_cost (def), ==, 0);
    g_assert_cmpint (lrg_card_def_get_target_type (def), ==, LRG_CARD_TARGET_NONE);
    g_assert_cmpuint (lrg_card_def_get_keywords (def), ==, LRG_CARD_KEYWORD_NONE);
}

static void
test_card_def_properties (DeckbuilderFixture *fixture,
                          gconstpointer       user_data)
{
    (void)user_data;

    /* Test strike properties */
    g_assert_cmpstr (lrg_card_def_get_id (fixture->strike), ==, "strike");
    g_assert_cmpstr (lrg_card_def_get_name (fixture->strike), ==, "Strike");
    g_assert_cmpstr (lrg_card_def_get_description (fixture->strike), ==, "Deal 6 damage.");
    g_assert_cmpint (lrg_card_def_get_card_type (fixture->strike), ==, LRG_CARD_TYPE_ATTACK);
    g_assert_cmpint (lrg_card_def_get_rarity (fixture->strike), ==, LRG_CARD_RARITY_STARTER);
    g_assert_cmpint (lrg_card_def_get_base_cost (fixture->strike), ==, 1);
    g_assert_cmpint (lrg_card_def_get_target_type (fixture->strike), ==, LRG_CARD_TARGET_SINGLE_ENEMY);

    /* Test curse properties */
    g_assert_cmpint (lrg_card_def_get_card_type (fixture->curse), ==, LRG_CARD_TYPE_CURSE);
    g_assert_true (lrg_card_def_has_keyword (fixture->curse, LRG_CARD_KEYWORD_UNPLAYABLE));
}

static void
test_card_def_keywords (void)
{
    g_autoptr(LrgCardDef) def = NULL;

    def = lrg_card_def_new ("test_card");

    /* Initially no keywords */
    g_assert_cmpuint (lrg_card_def_get_keywords (def), ==, LRG_CARD_KEYWORD_NONE);
    g_assert_false (lrg_card_def_has_keyword (def, LRG_CARD_KEYWORD_EXHAUST));

    /* Add keywords */
    lrg_card_def_add_keyword (def, LRG_CARD_KEYWORD_EXHAUST);
    g_assert_true (lrg_card_def_has_keyword (def, LRG_CARD_KEYWORD_EXHAUST));

    lrg_card_def_add_keyword (def, LRG_CARD_KEYWORD_INNATE);
    g_assert_true (lrg_card_def_has_keyword (def, LRG_CARD_KEYWORD_EXHAUST));
    g_assert_true (lrg_card_def_has_keyword (def, LRG_CARD_KEYWORD_INNATE));

    /* Remove keywords */
    lrg_card_def_remove_keyword (def, LRG_CARD_KEYWORD_EXHAUST);
    g_assert_false (lrg_card_def_has_keyword (def, LRG_CARD_KEYWORD_EXHAUST));
    g_assert_true (lrg_card_def_has_keyword (def, LRG_CARD_KEYWORD_INNATE));
}

static void
test_card_def_upgrade (void)
{
    g_autoptr(LrgCardDef) def = NULL;
    g_autoptr(LrgCardDef) upgraded = NULL;

    def = lrg_card_def_new ("strike");
    lrg_card_def_set_name (def, "Strike");
    lrg_card_def_set_base_cost (def, 1);
    lrg_card_def_set_upgradeable (def, TRUE);
    lrg_card_def_set_upgraded_def_id (def, "strike+");

    upgraded = lrg_card_def_new ("strike+");
    lrg_card_def_set_name (upgraded, "Strike+");
    lrg_card_def_set_base_cost (upgraded, 1);

    g_assert_true (lrg_card_def_get_upgradeable (def));
    g_assert_cmpstr (lrg_card_def_get_upgraded_def_id (def), ==, "strike+");
    g_assert_false (lrg_card_def_get_upgradeable (upgraded));
}

static void
test_card_def_scoring (void)
{
    g_autoptr(LrgCardDef) def = NULL;

    def = lrg_card_def_new ("ace_of_spades");
    lrg_card_def_set_suit (def, LRG_CARD_SUIT_SPADES);
    lrg_card_def_set_rank (def, LRG_CARD_RANK_ACE);
    lrg_card_def_set_chip_value (def, 11);

    g_assert_cmpint (lrg_card_def_get_suit (def), ==, LRG_CARD_SUIT_SPADES);
    g_assert_cmpint (lrg_card_def_get_rank (def), ==, LRG_CARD_RANK_ACE);
    g_assert_cmpint (lrg_card_def_get_chip_value (def), ==, 11);
}

/* ==========================================================================
 * LrgCardInstance Tests
 * ========================================================================== */

static void
test_card_instance_new (DeckbuilderFixture *fixture,
                        gconstpointer       user_data)
{
    g_autoptr(LrgCardInstance) instance = NULL;

    (void)user_data;

    instance = lrg_card_instance_new (fixture->strike);

    g_assert_nonnull (instance);
    g_assert_true (lrg_card_instance_get_def (instance) == fixture->strike);
    g_assert_cmpstr (lrg_card_instance_get_id (instance), ==, "strike");
    g_assert_cmpint (lrg_card_instance_get_upgrade_tier (instance), ==, LRG_CARD_UPGRADE_TIER_BASE);
    g_assert_cmpint (lrg_card_instance_get_zone (instance), ==, LRG_ZONE_LIMBO);
    g_assert_cmpint (lrg_card_instance_get_cost_modifier (instance), ==, 0);
    g_assert_cmpuint (lrg_card_instance_get_times_played (instance), ==, 0);
    g_assert_cmpuint (lrg_card_instance_get_instance_id (instance), >, 0);
}

static void
test_card_instance_upgrade (DeckbuilderFixture *fixture,
                            gconstpointer       user_data)
{
    g_autoptr(LrgCardInstance) instance = NULL;

    (void)user_data;

    instance = lrg_card_instance_new (fixture->strike);

    g_assert_cmpint (lrg_card_instance_get_upgrade_tier (instance), ==, LRG_CARD_UPGRADE_TIER_BASE);

    /* Upgrade */
    g_assert_true (lrg_card_instance_upgrade (instance));
    g_assert_cmpint (lrg_card_instance_get_upgrade_tier (instance), ==, LRG_CARD_UPGRADE_TIER_PLUS);

    /* Upgrade again */
    g_assert_true (lrg_card_instance_upgrade (instance));
    g_assert_cmpint (lrg_card_instance_get_upgrade_tier (instance), ==, LRG_CARD_UPGRADE_TIER_PLUS_PLUS);

    /* Upgrade to ultimate */
    g_assert_true (lrg_card_instance_upgrade (instance));
    g_assert_cmpint (lrg_card_instance_get_upgrade_tier (instance), ==, LRG_CARD_UPGRADE_TIER_ULTIMATE);

    /* Cannot upgrade past ultimate */
    g_assert_false (lrg_card_instance_upgrade (instance));
    g_assert_cmpint (lrg_card_instance_get_upgrade_tier (instance), ==, LRG_CARD_UPGRADE_TIER_ULTIMATE);
}

static void
test_card_instance_zone (DeckbuilderFixture *fixture,
                         gconstpointer       user_data)
{
    g_autoptr(LrgCardInstance) instance = NULL;

    (void)user_data;

    instance = lrg_card_instance_new (fixture->strike);

    /* Initial zone is limbo */
    g_assert_cmpint (lrg_card_instance_get_zone (instance), ==, LRG_ZONE_LIMBO);

    /* Change zones */
    lrg_card_instance_set_zone (instance, LRG_ZONE_DRAW);
    g_assert_cmpint (lrg_card_instance_get_zone (instance), ==, LRG_ZONE_DRAW);

    lrg_card_instance_set_zone (instance, LRG_ZONE_HAND);
    g_assert_cmpint (lrg_card_instance_get_zone (instance), ==, LRG_ZONE_HAND);

    lrg_card_instance_set_zone (instance, LRG_ZONE_DISCARD);
    g_assert_cmpint (lrg_card_instance_get_zone (instance), ==, LRG_ZONE_DISCARD);
}

static void
test_card_instance_cost_modifier (DeckbuilderFixture *fixture,
                                  gconstpointer       user_data)
{
    g_autoptr(LrgCardInstance) instance = NULL;

    (void)user_data;

    instance = lrg_card_instance_new (fixture->strike);

    /* Initially no modifier */
    g_assert_cmpint (lrg_card_instance_get_cost_modifier (instance), ==, 0);

    /* Set modifier */
    lrg_card_instance_set_cost_modifier (instance, -1);
    g_assert_cmpint (lrg_card_instance_get_cost_modifier (instance), ==, -1);

    /* Add to modifier */
    lrg_card_instance_add_cost_modifier (instance, -1);
    g_assert_cmpint (lrg_card_instance_get_cost_modifier (instance), ==, -2);

    /* Clear modifiers */
    lrg_card_instance_clear_temporary_modifiers (instance);
    g_assert_cmpint (lrg_card_instance_get_cost_modifier (instance), ==, 0);
}

static void
test_card_instance_temporary_keywords (DeckbuilderFixture *fixture,
                                       gconstpointer       user_data)
{
    g_autoptr(LrgCardInstance) instance = NULL;

    (void)user_data;

    instance = lrg_card_instance_new (fixture->strike);

    /* Initially no temporary keywords */
    g_assert_cmpuint (lrg_card_instance_get_temporary_keywords (instance), ==, LRG_CARD_KEYWORD_NONE);
    g_assert_false (lrg_card_instance_has_keyword (instance, LRG_CARD_KEYWORD_RETAIN));

    /* Add temporary keyword */
    lrg_card_instance_add_temporary_keyword (instance, LRG_CARD_KEYWORD_RETAIN);
    g_assert_true (lrg_card_instance_has_keyword (instance, LRG_CARD_KEYWORD_RETAIN));

    /* Remove temporary keyword */
    lrg_card_instance_remove_temporary_keyword (instance, LRG_CARD_KEYWORD_RETAIN);
    g_assert_false (lrg_card_instance_has_keyword (instance, LRG_CARD_KEYWORD_RETAIN));
}

static void
test_card_instance_combined_keywords (DeckbuilderFixture *fixture,
                                      gconstpointer       user_data)
{
    g_autoptr(LrgCardInstance) instance = NULL;
    LrgCardKeyword all_keywords;

    (void)user_data;

    /* Use curse which has UNPLAYABLE keyword from definition */
    instance = lrg_card_instance_new (fixture->curse);

    /* Has definition keyword */
    g_assert_true (lrg_card_instance_has_keyword (instance, LRG_CARD_KEYWORD_UNPLAYABLE));

    /* Add temporary keyword */
    lrg_card_instance_add_temporary_keyword (instance, LRG_CARD_KEYWORD_ETHEREAL);
    g_assert_true (lrg_card_instance_has_keyword (instance, LRG_CARD_KEYWORD_ETHEREAL));

    /* Both should be in all_keywords */
    all_keywords = lrg_card_instance_get_all_keywords (instance);
    g_assert_true (all_keywords & LRG_CARD_KEYWORD_UNPLAYABLE);
    g_assert_true (all_keywords & LRG_CARD_KEYWORD_ETHEREAL);
}

static void
test_card_instance_play_count (DeckbuilderFixture *fixture,
                               gconstpointer       user_data)
{
    g_autoptr(LrgCardInstance) instance = NULL;

    (void)user_data;

    instance = lrg_card_instance_new (fixture->strike);

    /* Initially 0 */
    g_assert_cmpuint (lrg_card_instance_get_times_played (instance), ==, 0);

    /* Increment */
    lrg_card_instance_increment_play_count (instance);
    g_assert_cmpuint (lrg_card_instance_get_times_played (instance), ==, 1);

    lrg_card_instance_increment_play_count (instance);
    g_assert_cmpuint (lrg_card_instance_get_times_played (instance), ==, 2);

    /* Reset */
    lrg_card_instance_reset_play_count (instance);
    g_assert_cmpuint (lrg_card_instance_get_times_played (instance), ==, 0);
}

static void
test_card_instance_bonus_chips (DeckbuilderFixture *fixture,
                                gconstpointer       user_data)
{
    g_autoptr(LrgCardInstance) instance = NULL;

    (void)user_data;

    instance = lrg_card_instance_new (fixture->strike);

    /* Initially 0 */
    g_assert_cmpint (lrg_card_instance_get_bonus_chips (instance), ==, 0);

    /* Set bonus chips */
    lrg_card_instance_set_bonus_chips (instance, 10);
    g_assert_cmpint (lrg_card_instance_get_bonus_chips (instance), ==, 10);

    /* Add bonus chips */
    lrg_card_instance_add_bonus_chips (instance, 5);
    g_assert_cmpint (lrg_card_instance_get_bonus_chips (instance), ==, 15);
}

static void
test_card_instance_unique_ids (DeckbuilderFixture *fixture,
                               gconstpointer       user_data)
{
    g_autoptr(LrgCardInstance) instance1 = NULL;
    g_autoptr(LrgCardInstance) instance2 = NULL;
    g_autoptr(LrgCardInstance) instance3 = NULL;
    guint64 id1, id2, id3;

    (void)user_data;

    instance1 = lrg_card_instance_new (fixture->strike);
    instance2 = lrg_card_instance_new (fixture->strike);
    instance3 = lrg_card_instance_new (fixture->defend);

    id1 = lrg_card_instance_get_instance_id (instance1);
    id2 = lrg_card_instance_get_instance_id (instance2);
    id3 = lrg_card_instance_get_instance_id (instance3);

    /* All IDs should be unique */
    g_assert_cmpuint (id1, !=, id2);
    g_assert_cmpuint (id2, !=, id3);
    g_assert_cmpuint (id1, !=, id3);
}

/* ==========================================================================
 * LrgCardPile Tests
 * ========================================================================== */

static void
test_card_pile_new (void)
{
    g_autoptr(LrgCardPile) pile = NULL;

    pile = lrg_card_pile_new ();

    g_assert_nonnull (pile);
    g_assert_cmpuint (lrg_card_pile_get_count (pile), ==, 0);
    g_assert_true (lrg_card_pile_is_empty (pile));
    g_assert_cmpint (lrg_card_pile_get_zone (pile), ==, LRG_ZONE_LIMBO);
}

static void
test_card_pile_new_with_zone (void)
{
    g_autoptr(LrgCardPile) pile = NULL;

    pile = lrg_card_pile_new_with_zone (LRG_ZONE_DRAW);

    g_assert_nonnull (pile);
    g_assert_cmpint (lrg_card_pile_get_zone (pile), ==, LRG_ZONE_DRAW);
}

static void
test_card_pile_add_draw (DeckbuilderFixture *fixture,
                         gconstpointer       user_data)
{
    LrgCardInstance *instance1;
    LrgCardInstance *instance2;
    LrgCardInstance *drawn;

    (void)user_data;

    /* Create instances */
    instance1 = lrg_card_instance_new (fixture->strike);
    instance2 = lrg_card_instance_new (fixture->defend);

    /* Add to pile */
    lrg_card_pile_add (fixture->draw_pile, instance1, LRG_PILE_POSITION_TOP);
    g_assert_cmpuint (lrg_card_pile_get_count (fixture->draw_pile), ==, 1);
    g_assert_false (lrg_card_pile_is_empty (fixture->draw_pile));
    g_assert_cmpint (lrg_card_instance_get_zone (instance1), ==, LRG_ZONE_DRAW);

    lrg_card_pile_add (fixture->draw_pile, instance2, LRG_PILE_POSITION_TOP);
    g_assert_cmpuint (lrg_card_pile_get_count (fixture->draw_pile), ==, 2);

    /* Draw from top - should be instance2 */
    drawn = lrg_card_pile_draw (fixture->draw_pile);
    g_assert_nonnull (drawn);
    g_assert_true (drawn == instance2);
    g_assert_cmpuint (lrg_card_pile_get_count (fixture->draw_pile), ==, 1);
    g_object_unref (drawn);

    /* Draw again - should be instance1 */
    drawn = lrg_card_pile_draw (fixture->draw_pile);
    g_assert_nonnull (drawn);
    g_assert_true (drawn == instance1);
    g_assert_true (lrg_card_pile_is_empty (fixture->draw_pile));
    g_object_unref (drawn);

    /* Draw from empty pile */
    drawn = lrg_card_pile_draw (fixture->draw_pile);
    g_assert_null (drawn);
}

static void
test_card_pile_add_bottom (DeckbuilderFixture *fixture,
                           gconstpointer       user_data)
{
    LrgCardInstance *instance1;
    LrgCardInstance *instance2;
    LrgCardInstance *drawn;

    (void)user_data;

    instance1 = lrg_card_instance_new (fixture->strike);
    instance2 = lrg_card_instance_new (fixture->defend);

    /* Add first to top, second to bottom */
    lrg_card_pile_add (fixture->draw_pile, instance1, LRG_PILE_POSITION_TOP);
    lrg_card_pile_add (fixture->draw_pile, instance2, LRG_PILE_POSITION_BOTTOM);

    /* Draw from top - should be instance1 (top) */
    drawn = lrg_card_pile_draw (fixture->draw_pile);
    g_assert_true (drawn == instance1);
    g_object_unref (drawn);

    /* Next draw should be instance2 */
    drawn = lrg_card_pile_draw (fixture->draw_pile);
    g_assert_true (drawn == instance2);
    g_object_unref (drawn);
}

static void
test_card_pile_draw_bottom (DeckbuilderFixture *fixture,
                            gconstpointer       user_data)
{
    LrgCardInstance *instance1;
    LrgCardInstance *instance2;
    LrgCardInstance *drawn;

    (void)user_data;

    instance1 = lrg_card_instance_new (fixture->strike);
    instance2 = lrg_card_instance_new (fixture->defend);

    lrg_card_pile_add_top (fixture->draw_pile, instance1);
    lrg_card_pile_add_top (fixture->draw_pile, instance2);
    /* Pile order (bottom to top): instance1, instance2 */

    /* Draw from bottom - should be instance1 */
    drawn = lrg_card_pile_draw_bottom (fixture->draw_pile);
    g_assert_true (drawn == instance1);
    g_object_unref (drawn);
}

static void
test_card_pile_peek (DeckbuilderFixture *fixture,
                     gconstpointer       user_data)
{
    LrgCardInstance *instance1;
    LrgCardInstance *instance2;
    LrgCardInstance *peeked;

    (void)user_data;

    instance1 = lrg_card_instance_new (fixture->strike);
    instance2 = lrg_card_instance_new (fixture->defend);

    lrg_card_pile_add_top (fixture->draw_pile, instance1);
    lrg_card_pile_add_top (fixture->draw_pile, instance2);

    /* Peek should return top without removing */
    peeked = lrg_card_pile_peek (fixture->draw_pile);
    g_assert_true (peeked == instance2);
    g_assert_cmpuint (lrg_card_pile_get_count (fixture->draw_pile), ==, 2);

    /* Peek again - same result */
    peeked = lrg_card_pile_peek (fixture->draw_pile);
    g_assert_true (peeked == instance2);
}

static void
test_card_pile_peek_n (DeckbuilderFixture *fixture,
                       gconstpointer       user_data)
{
    LrgCardInstance *instance1;
    LrgCardInstance *instance2;
    LrgCardInstance *instance3;
    g_autoptr(GPtrArray) peeked = NULL;

    (void)user_data;

    instance1 = lrg_card_instance_new (fixture->strike);
    instance2 = lrg_card_instance_new (fixture->defend);
    instance3 = lrg_card_instance_new (fixture->power_card);

    lrg_card_pile_add_top (fixture->draw_pile, instance1);
    lrg_card_pile_add_top (fixture->draw_pile, instance2);
    lrg_card_pile_add_top (fixture->draw_pile, instance3);
    /* Order (bottom to top): instance1, instance2, instance3 */

    /* Peek top 2 */
    peeked = lrg_card_pile_peek_n (fixture->draw_pile, 2);
    g_assert_cmpuint (peeked->len, ==, 2);
    g_assert_true (g_ptr_array_index (peeked, 0) == instance3);  /* Top first */
    g_assert_true (g_ptr_array_index (peeked, 1) == instance2);

    /* Pile unchanged */
    g_assert_cmpuint (lrg_card_pile_get_count (fixture->draw_pile), ==, 3);
}

static void
test_card_pile_shuffle (DeckbuilderFixture *fixture,
                        gconstpointer       user_data)
{
    LrgCardInstance *instances[20];
    guint i;
    gboolean order_changed = FALSE;
    g_autoptr(GRand) rng = NULL;

    (void)user_data;

    /* Create 20 cards and add to pile */
    for (i = 0; i < 20; i++)
    {
        instances[i] = lrg_card_instance_new (fixture->strike);
        lrg_card_pile_add_top (fixture->draw_pile, instances[i]);
    }

    g_assert_cmpuint (lrg_card_pile_get_count (fixture->draw_pile), ==, 20);

    /* Shuffle with seeded RNG */
    rng = g_rand_new_with_seed (12345);
    lrg_card_pile_shuffle (fixture->draw_pile, rng);

    /* Still 20 cards */
    g_assert_cmpuint (lrg_card_pile_get_count (fixture->draw_pile), ==, 20);

    /* Check that order changed (probabilistically) */
    for (i = 0; i < 20; i++)
    {
        if (lrg_card_pile_get_card_at (fixture->draw_pile, i) != instances[i])
        {
            order_changed = TRUE;
            break;
        }
    }
    g_assert_true (order_changed);
}

static void
test_card_pile_contains (DeckbuilderFixture *fixture,
                         gconstpointer       user_data)
{
    LrgCardInstance *instance1;
    LrgCardInstance *instance2;

    (void)user_data;

    instance1 = lrg_card_instance_new (fixture->strike);
    instance2 = lrg_card_instance_new (fixture->defend);

    lrg_card_pile_add_top (fixture->draw_pile, instance1);

    g_assert_true (lrg_card_pile_contains (fixture->draw_pile, instance1));
    g_assert_false (lrg_card_pile_contains (fixture->draw_pile, instance2));

    g_object_unref (instance2);
}

static void
test_card_pile_remove (DeckbuilderFixture *fixture,
                       gconstpointer       user_data)
{
    LrgCardInstance *instance1;
    LrgCardInstance *instance2;
    LrgCardInstance *instance3;

    (void)user_data;

    instance1 = lrg_card_instance_new (fixture->strike);
    instance2 = lrg_card_instance_new (fixture->defend);
    instance3 = lrg_card_instance_new (fixture->power_card);

    lrg_card_pile_add_top (fixture->draw_pile, instance1);
    lrg_card_pile_add_top (fixture->draw_pile, instance2);
    lrg_card_pile_add_top (fixture->draw_pile, instance3);

    /* Remove middle card - ownership returns to caller */
    g_assert_true (lrg_card_pile_remove (fixture->draw_pile, instance2));
    g_assert_cmpuint (lrg_card_pile_get_count (fixture->draw_pile), ==, 2);
    g_assert_false (lrg_card_pile_contains (fixture->draw_pile, instance2));

    /* Can't remove again */
    g_assert_false (lrg_card_pile_remove (fixture->draw_pile, instance2));

    /* Caller owns instance2 now, so unref it */
    g_object_unref (instance2);
}

static void
test_card_pile_find_by_id (DeckbuilderFixture *fixture,
                           gconstpointer       user_data)
{
    LrgCardInstance *strike1;
    LrgCardInstance *strike2;
    LrgCardInstance *defend1;
    LrgCardInstance *found;
    g_autoptr(GPtrArray) all_strikes = NULL;

    (void)user_data;

    strike1 = lrg_card_instance_new (fixture->strike);
    strike2 = lrg_card_instance_new (fixture->strike);
    defend1 = lrg_card_instance_new (fixture->defend);

    lrg_card_pile_add_top (fixture->draw_pile, strike1);
    lrg_card_pile_add_top (fixture->draw_pile, defend1);
    lrg_card_pile_add_top (fixture->draw_pile, strike2);

    /* Find first strike */
    found = lrg_card_pile_find_by_id (fixture->draw_pile, "strike");
    g_assert_nonnull (found);
    g_assert_cmpstr (lrg_card_instance_get_id (found), ==, "strike");

    /* Find all strikes */
    all_strikes = lrg_card_pile_find_all_by_id (fixture->draw_pile, "strike");
    g_assert_cmpuint (all_strikes->len, ==, 2);

    /* Find nonexistent */
    found = lrg_card_pile_find_by_id (fixture->draw_pile, "nonexistent");
    g_assert_null (found);
}

static void
test_card_pile_find_by_type (DeckbuilderFixture *fixture,
                             gconstpointer       user_data)
{
    LrgCardInstance *strike1;
    LrgCardInstance *defend1;
    LrgCardInstance *power1;
    g_autoptr(GPtrArray) attacks = NULL;
    g_autoptr(GPtrArray) skills = NULL;

    (void)user_data;

    strike1 = lrg_card_instance_new (fixture->strike);
    defend1 = lrg_card_instance_new (fixture->defend);
    power1 = lrg_card_instance_new (fixture->power_card);

    lrg_card_pile_add_top (fixture->draw_pile, strike1);
    lrg_card_pile_add_top (fixture->draw_pile, defend1);
    lrg_card_pile_add_top (fixture->draw_pile, power1);

    attacks = lrg_card_pile_find_by_type (fixture->draw_pile, LRG_CARD_TYPE_ATTACK);
    g_assert_cmpuint (attacks->len, ==, 1);

    skills = lrg_card_pile_find_by_type (fixture->draw_pile, LRG_CARD_TYPE_SKILL);
    g_assert_cmpuint (skills->len, ==, 1);
}

static void
test_card_pile_transfer_all (DeckbuilderFixture *fixture,
                             gconstpointer       user_data)
{
    LrgCardInstance *instance1;
    LrgCardInstance *instance2;
    guint transferred;

    (void)user_data;

    instance1 = lrg_card_instance_new (fixture->strike);
    instance2 = lrg_card_instance_new (fixture->defend);

    lrg_card_pile_add_top (fixture->discard_pile, instance1);
    lrg_card_pile_add_top (fixture->discard_pile, instance2);

    g_assert_cmpuint (lrg_card_pile_get_count (fixture->discard_pile), ==, 2);
    g_assert_cmpuint (lrg_card_pile_get_count (fixture->draw_pile), ==, 0);

    transferred = lrg_card_pile_transfer_all (fixture->discard_pile, fixture->draw_pile);

    g_assert_cmpuint (transferred, ==, 2);
    g_assert_cmpuint (lrg_card_pile_get_count (fixture->discard_pile), ==, 0);
    g_assert_cmpuint (lrg_card_pile_get_count (fixture->draw_pile), ==, 2);

    /* Cards should have new zone set */
    g_assert_cmpint (lrg_card_instance_get_zone (instance1), ==, LRG_ZONE_DRAW);
    g_assert_cmpint (lrg_card_instance_get_zone (instance2), ==, LRG_ZONE_DRAW);
}

static void
test_card_pile_clear (DeckbuilderFixture *fixture,
                      gconstpointer       user_data)
{
    LrgCardInstance *instance1;
    LrgCardInstance *instance2;

    (void)user_data;

    instance1 = lrg_card_instance_new (fixture->strike);
    instance2 = lrg_card_instance_new (fixture->defend);

    lrg_card_pile_add_top (fixture->draw_pile, instance1);
    lrg_card_pile_add_top (fixture->draw_pile, instance2);

    g_assert_cmpuint (lrg_card_pile_get_count (fixture->draw_pile), ==, 2);

    lrg_card_pile_clear (fixture->draw_pile);

    g_assert_cmpuint (lrg_card_pile_get_count (fixture->draw_pile), ==, 0);
    g_assert_true (lrg_card_pile_is_empty (fixture->draw_pile));
}

/* ==========================================================================
 * LrgHand Tests
 * ========================================================================== */

static void
test_hand_new (void)
{
    g_autoptr(LrgHand) hand = NULL;

    hand = lrg_hand_new ();

    g_assert_nonnull (hand);
    g_assert_cmpuint (lrg_hand_get_count (hand), ==, 0);
    g_assert_cmpuint (lrg_hand_get_max_size (hand), ==, LRG_HAND_DEFAULT_MAX_SIZE);
    g_assert_true (lrg_hand_is_empty (hand));
    g_assert_false (lrg_hand_is_full (hand));
}

static void
test_hand_new_with_size (void)
{
    g_autoptr(LrgHand) hand = NULL;

    hand = lrg_hand_new_with_size (5);

    g_assert_cmpuint (lrg_hand_get_max_size (hand), ==, 5);
}

static void
test_hand_add (DeckbuilderFixture *fixture,
               gconstpointer       user_data)
{
    LrgCardInstance *instance1;
    LrgCardInstance *instance2;

    (void)user_data;

    instance1 = lrg_card_instance_new (fixture->strike);
    instance2 = lrg_card_instance_new (fixture->defend);

    g_assert_true (lrg_hand_add (fixture->hand, instance1));
    g_assert_cmpuint (lrg_hand_get_count (fixture->hand), ==, 1);
    g_assert_cmpint (lrg_card_instance_get_zone (instance1), ==, LRG_ZONE_HAND);

    g_assert_true (lrg_hand_add (fixture->hand, instance2));
    g_assert_cmpuint (lrg_hand_get_count (fixture->hand), ==, 2);
}

static void
test_hand_add_full (DeckbuilderFixture *fixture,
                    gconstpointer       user_data)
{
    g_autoptr(LrgHand) small_hand = NULL;
    LrgCardInstance *instance1;
    LrgCardInstance *instance2;
    LrgCardInstance *instance3;

    (void)user_data;

    small_hand = lrg_hand_new_with_size (2);

    instance1 = lrg_card_instance_new (fixture->strike);
    instance2 = lrg_card_instance_new (fixture->defend);
    instance3 = lrg_card_instance_new (fixture->power_card);

    g_assert_true (lrg_hand_add (small_hand, instance1));
    g_assert_true (lrg_hand_add (small_hand, instance2));
    g_assert_true (lrg_hand_is_full (small_hand));

    /* Cannot add when full */
    g_assert_false (lrg_hand_add (small_hand, instance3));
    g_assert_cmpuint (lrg_hand_get_count (small_hand), ==, 2);

    g_object_unref (instance3);
}

static void
test_hand_remove (DeckbuilderFixture *fixture,
                  gconstpointer       user_data)
{
    LrgCardInstance *instance1;
    LrgCardInstance *instance2;
    LrgCardInstance *removed;

    (void)user_data;

    instance1 = lrg_card_instance_new (fixture->strike);
    instance2 = lrg_card_instance_new (fixture->defend);

    lrg_hand_add (fixture->hand, instance1);
    lrg_hand_add (fixture->hand, instance2);

    removed = lrg_hand_remove (fixture->hand, instance1);
    g_assert_nonnull (removed);
    g_assert_true (removed == instance1);
    g_assert_cmpuint (lrg_hand_get_count (fixture->hand), ==, 1);
    g_assert_false (lrg_hand_contains (fixture->hand, instance1));
    g_object_unref (removed);
}

static void
test_hand_discard (DeckbuilderFixture *fixture,
                   gconstpointer       user_data)
{
    LrgCardInstance *instance1;
    LrgCardInstance *instance2;

    (void)user_data;

    instance1 = lrg_card_instance_new (fixture->strike);
    instance2 = lrg_card_instance_new (fixture->defend);

    lrg_hand_add (fixture->hand, instance1);
    lrg_hand_add (fixture->hand, instance2);

    g_assert_true (lrg_hand_discard (fixture->hand, instance1, fixture->discard_pile));
    g_assert_cmpuint (lrg_hand_get_count (fixture->hand), ==, 1);
    g_assert_cmpuint (lrg_card_pile_get_count (fixture->discard_pile), ==, 1);
    g_assert_true (lrg_card_pile_contains (fixture->discard_pile, instance1));
}

static void
test_hand_discard_retain (DeckbuilderFixture *fixture,
                          gconstpointer       user_data)
{
    LrgCardInstance *instance1;
    LrgCardInstance *instance2;

    (void)user_data;

    instance1 = lrg_card_instance_new (fixture->strike);
    instance2 = lrg_card_instance_new (fixture->defend);

    /* Give instance1 the Retain keyword */
    lrg_card_instance_add_temporary_keyword (instance1, LRG_CARD_KEYWORD_RETAIN);

    lrg_hand_add (fixture->hand, instance1);
    lrg_hand_add (fixture->hand, instance2);

    /* Try to discard retained card */
    g_assert_false (lrg_hand_discard (fixture->hand, instance1, fixture->discard_pile));
    g_assert_cmpuint (lrg_hand_get_count (fixture->hand), ==, 2);
    g_assert_true (lrg_hand_contains (fixture->hand, instance1));

    /* Non-retained card should discard normally */
    g_assert_true (lrg_hand_discard (fixture->hand, instance2, fixture->discard_pile));
}

static void
test_hand_discard_all (DeckbuilderFixture *fixture,
                       gconstpointer       user_data)
{
    LrgCardInstance *instance1;
    LrgCardInstance *instance2;
    LrgCardInstance *instance3;
    guint discarded;

    (void)user_data;

    instance1 = lrg_card_instance_new (fixture->strike);
    instance2 = lrg_card_instance_new (fixture->defend);
    instance3 = lrg_card_instance_new (fixture->power_card);

    /* Give instance2 Retain */
    lrg_card_instance_add_temporary_keyword (instance2, LRG_CARD_KEYWORD_RETAIN);

    lrg_hand_add (fixture->hand, instance1);
    lrg_hand_add (fixture->hand, instance2);
    lrg_hand_add (fixture->hand, instance3);

    discarded = lrg_hand_discard_all (fixture->hand, fixture->discard_pile);

    /* 2 discarded, 1 retained */
    g_assert_cmpuint (discarded, ==, 2);
    g_assert_cmpuint (lrg_hand_get_count (fixture->hand), ==, 1);
    g_assert_cmpuint (lrg_card_pile_get_count (fixture->discard_pile), ==, 2);
    g_assert_true (lrg_hand_contains (fixture->hand, instance2));  /* Retained */
}

static void
test_hand_find_by_id (DeckbuilderFixture *fixture,
                      gconstpointer       user_data)
{
    LrgCardInstance *strike1;
    LrgCardInstance *defend1;
    LrgCardInstance *found;
    g_autoptr(GPtrArray) all_strikes = NULL;

    (void)user_data;

    strike1 = lrg_card_instance_new (fixture->strike);
    defend1 = lrg_card_instance_new (fixture->defend);

    lrg_hand_add (fixture->hand, strike1);
    lrg_hand_add (fixture->hand, defend1);

    found = lrg_hand_find_by_id (fixture->hand, "strike");
    g_assert_nonnull (found);
    g_assert_true (found == strike1);

    found = lrg_hand_find_by_id (fixture->hand, "nonexistent");
    g_assert_null (found);

    all_strikes = lrg_hand_find_all_by_id (fixture->hand, "strike");
    g_assert_cmpuint (all_strikes->len, ==, 1);
}

static void
test_hand_find_playable (DeckbuilderFixture *fixture,
                         gconstpointer       user_data)
{
    LrgCardInstance *strike1;
    LrgCardInstance *curse1;
    LrgCardInstance *power1;
    g_autoptr(GPtrArray) playable = NULL;

    (void)user_data;

    strike1 = lrg_card_instance_new (fixture->strike);  /* Cost 1 */
    curse1 = lrg_card_instance_new (fixture->curse);    /* Unplayable */
    power1 = lrg_card_instance_new (fixture->power_card);  /* Cost 3 */

    lrg_hand_add (fixture->hand, strike1);
    lrg_hand_add (fixture->hand, curse1);
    lrg_hand_add (fixture->hand, power1);

    /* With 2 energy: only strike is playable */
    playable = lrg_hand_find_playable (fixture->hand, 2);
    g_assert_cmpuint (playable->len, ==, 1);
    g_assert_true (g_ptr_array_index (playable, 0) == strike1);

    g_ptr_array_unref (playable);

    /* With 3 energy: strike and power are playable */
    playable = lrg_hand_find_playable (fixture->hand, 3);
    g_assert_cmpuint (playable->len, ==, 2);
}

static void
test_hand_selection (DeckbuilderFixture *fixture,
                     gconstpointer       user_data)
{
    LrgCardInstance *instance1;
    LrgCardInstance *instance2;
    GPtrArray *selected;

    (void)user_data;

    instance1 = lrg_card_instance_new (fixture->strike);
    instance2 = lrg_card_instance_new (fixture->defend);

    lrg_hand_add (fixture->hand, instance1);
    lrg_hand_add (fixture->hand, instance2);

    /* Initially nothing selected */
    selected = lrg_hand_get_selected (fixture->hand);
    g_assert_cmpuint (selected->len, ==, 0);

    /* Select cards */
    g_assert_true (lrg_hand_select (fixture->hand, instance1));
    g_assert_true (lrg_hand_is_selected (fixture->hand, instance1));
    g_assert_cmpuint (selected->len, ==, 1);

    g_assert_true (lrg_hand_select (fixture->hand, instance2));
    g_assert_cmpuint (selected->len, ==, 2);

    /* Deselect */
    g_assert_true (lrg_hand_deselect (fixture->hand, instance1));
    g_assert_false (lrg_hand_is_selected (fixture->hand, instance1));
    g_assert_cmpuint (selected->len, ==, 1);

    /* Clear selection */
    lrg_hand_clear_selection (fixture->hand);
    g_assert_cmpuint (selected->len, ==, 0);
}

static void
test_hand_sort_by_cost (DeckbuilderFixture *fixture,
                        gconstpointer       user_data)
{
    LrgCardInstance *strike1;  /* Cost 1 */
    LrgCardInstance *power1;   /* Cost 3 */
    LrgCardInstance *defend1;  /* Cost 1 */
    LrgCardInstance *first;
    LrgCardInstance *last;
    LrgCardDef *first_def;
    LrgCardDef *last_def;

    (void)user_data;

    strike1 = lrg_card_instance_new (fixture->strike);
    power1 = lrg_card_instance_new (fixture->power_card);
    defend1 = lrg_card_instance_new (fixture->defend);

    /* Add in random order */
    lrg_hand_add (fixture->hand, power1);
    lrg_hand_add (fixture->hand, strike1);
    lrg_hand_add (fixture->hand, defend1);

    /* Sort ascending */
    lrg_hand_sort_by_cost (fixture->hand, TRUE);

    /* Cost 1 cards first, then cost 3 */
    first = lrg_hand_get_card_at (fixture->hand, 0);
    last = lrg_hand_get_card_at (fixture->hand, 2);
    first_def = lrg_card_instance_get_def (first);
    last_def = lrg_card_instance_get_def (last);

    g_assert_cmpint (lrg_card_def_get_base_cost (first_def), ==, 1);
    g_assert_cmpint (lrg_card_def_get_base_cost (last_def), ==, 3);
}

static void
test_hand_sort_by_type (DeckbuilderFixture *fixture,
                        gconstpointer       user_data)
{
    LrgCardInstance *strike1;  /* Attack */
    LrgCardInstance *power1;   /* Power */
    LrgCardInstance *defend1;  /* Skill */
    LrgCardInstance *first;
    LrgCardDef *first_def;

    (void)user_data;

    strike1 = lrg_card_instance_new (fixture->strike);
    power1 = lrg_card_instance_new (fixture->power_card);
    defend1 = lrg_card_instance_new (fixture->defend);

    /* Add in random order */
    lrg_hand_add (fixture->hand, power1);
    lrg_hand_add (fixture->hand, strike1);
    lrg_hand_add (fixture->hand, defend1);

    /* Sort by type */
    lrg_hand_sort_by_type (fixture->hand);

    /* Attack (0), Skill (1), Power (2) */
    first = lrg_hand_get_card_at (fixture->hand, 0);
    first_def = lrg_card_instance_get_def (first);
    g_assert_cmpint (lrg_card_def_get_card_type (first_def), ==, LRG_CARD_TYPE_ATTACK);
}

static void
test_hand_get_index_of (DeckbuilderFixture *fixture,
                        gconstpointer       user_data)
{
    LrgCardInstance *instance1;
    LrgCardInstance *instance2;
    LrgCardInstance *instance3;

    (void)user_data;

    instance1 = lrg_card_instance_new (fixture->strike);
    instance2 = lrg_card_instance_new (fixture->defend);
    instance3 = lrg_card_instance_new (fixture->power_card);

    lrg_hand_add (fixture->hand, instance1);
    lrg_hand_add (fixture->hand, instance2);

    g_assert_cmpint (lrg_hand_get_index_of (fixture->hand, instance1), ==, 0);
    g_assert_cmpint (lrg_hand_get_index_of (fixture->hand, instance2), ==, 1);
    g_assert_cmpint (lrg_hand_get_index_of (fixture->hand, instance3), ==, -1);

    g_object_unref (instance3);
}

/* ==========================================================================
 * LrgDeckCardEntry Tests (Phase 2)
 * ========================================================================== */

static void
test_deck_card_entry_new (DeckbuilderFixture *fixture,
                          gconstpointer       user_data)
{
    g_autoptr(LrgDeckCardEntry) entry = NULL;

    (void)user_data;

    entry = lrg_deck_card_entry_new (fixture->strike, 3);

    g_assert_nonnull (entry);
    g_assert_true (lrg_deck_card_entry_get_card_def (entry) == fixture->strike);
    g_assert_cmpuint (lrg_deck_card_entry_get_count (entry), ==, 3);
}

static void
test_deck_card_entry_copy (DeckbuilderFixture *fixture,
                           gconstpointer       user_data)
{
    g_autoptr(LrgDeckCardEntry) entry = NULL;
    g_autoptr(LrgDeckCardEntry) copy = NULL;

    (void)user_data;

    entry = lrg_deck_card_entry_new (fixture->strike, 5);
    copy = lrg_deck_card_entry_copy (entry);

    g_assert_nonnull (copy);
    g_assert_true (lrg_deck_card_entry_get_card_def (copy) == fixture->strike);
    g_assert_cmpuint (lrg_deck_card_entry_get_count (copy), ==, 5);
}

static void
test_deck_card_entry_set_count (DeckbuilderFixture *fixture,
                                gconstpointer       user_data)
{
    g_autoptr(LrgDeckCardEntry) entry = NULL;

    (void)user_data;

    entry = lrg_deck_card_entry_new (fixture->strike, 1);
    g_assert_cmpuint (lrg_deck_card_entry_get_count (entry), ==, 1);

    lrg_deck_card_entry_set_count (entry, 4);
    g_assert_cmpuint (lrg_deck_card_entry_get_count (entry), ==, 4);
}

/* ==========================================================================
 * LrgDeckDef Tests (Phase 2)
 * ========================================================================== */

static void
test_deck_def_new (void)
{
    g_autoptr(LrgDeckDef) def = NULL;

    def = lrg_deck_def_new ("ironclad_starter");

    g_assert_nonnull (def);
    g_assert_cmpstr (lrg_deck_def_get_id (def), ==, "ironclad_starter");
    g_assert_null (lrg_deck_def_get_name (def));
    g_assert_null (lrg_deck_def_get_description (def));
    g_assert_cmpuint (lrg_deck_def_get_min_size (def), ==, 0);
    g_assert_cmpuint (lrg_deck_def_get_max_size (def), ==, 0);  /* 0 = unlimited */
}

static void
test_deck_def_properties (void)
{
    g_autoptr(LrgDeckDef) def = NULL;

    def = lrg_deck_def_new ("test_deck");
    lrg_deck_def_set_name (def, "Test Deck");
    lrg_deck_def_set_description (def, "A deck for testing.");
    lrg_deck_def_set_min_size (def, 10);
    lrg_deck_def_set_max_size (def, 50);

    g_assert_cmpstr (lrg_deck_def_get_name (def), ==, "Test Deck");
    g_assert_cmpstr (lrg_deck_def_get_description (def), ==, "A deck for testing.");
    g_assert_cmpuint (lrg_deck_def_get_min_size (def), ==, 10);
    g_assert_cmpuint (lrg_deck_def_get_max_size (def), ==, 50);
}

static void
test_deck_def_starting_cards (DeckbuilderFixture *fixture,
                              gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    GPtrArray *starting_cards;
    LrgDeckCardEntry *entry;

    (void)user_data;

    def = lrg_deck_def_new ("starter");

    /* Add starting cards */
    lrg_deck_def_add_starting_card (def, fixture->strike, 5);
    lrg_deck_def_add_starting_card (def, fixture->defend, 4);

    /* Verify count */
    g_assert_cmpuint (lrg_deck_def_get_starting_card_count (def), ==, 2);

    /* Get starting cards */
    starting_cards = lrg_deck_def_get_starting_cards (def);
    g_assert_nonnull (starting_cards);
    g_assert_cmpuint (starting_cards->len, ==, 2);

    entry = g_ptr_array_index (starting_cards, 0);
    g_assert_true (lrg_deck_card_entry_get_card_def (entry) == fixture->strike);
    g_assert_cmpuint (lrg_deck_card_entry_get_count (entry), ==, 5);

    entry = g_ptr_array_index (starting_cards, 1);
    g_assert_true (lrg_deck_card_entry_get_card_def (entry) == fixture->defend);
    g_assert_cmpuint (lrg_deck_card_entry_get_count (entry), ==, 4);

    /* Verify total starting cards */
    g_assert_cmpuint (lrg_deck_def_get_total_starting_cards (def), ==, 9);
}

static void
test_deck_def_remove_starting_card (DeckbuilderFixture *fixture,
                                    gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;

    (void)user_data;

    def = lrg_deck_def_new ("starter");

    lrg_deck_def_add_starting_card (def, fixture->strike, 5);
    lrg_deck_def_add_starting_card (def, fixture->defend, 4);
    g_assert_cmpuint (lrg_deck_def_get_starting_card_count (def), ==, 2);

    g_assert_true (lrg_deck_def_remove_starting_card (def, fixture->strike));
    g_assert_cmpuint (lrg_deck_def_get_starting_card_count (def), ==, 1);

    /* Can't remove what's not there */
    g_assert_false (lrg_deck_def_remove_starting_card (def, fixture->strike));
}

static void
test_deck_def_allowed_types (DeckbuilderFixture *fixture,
                             gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;

    (void)user_data;
    (void)fixture;

    def = lrg_deck_def_new ("attack_only");

    /* Initially all types allowed */
    g_assert_true (lrg_deck_def_is_card_type_allowed (def, LRG_CARD_TYPE_ATTACK));
    g_assert_true (lrg_deck_def_is_card_type_allowed (def, LRG_CARD_TYPE_SKILL));

    /* Set specific allowed types */
    lrg_deck_def_set_allowed_types (def, LRG_CARD_TYPE_ATTACK);
    g_assert_true (lrg_deck_def_is_card_type_allowed (def, LRG_CARD_TYPE_ATTACK));
    g_assert_false (lrg_deck_def_is_card_type_allowed (def, LRG_CARD_TYPE_SKILL));
    g_assert_false (lrg_deck_def_is_card_type_allowed (def, LRG_CARD_TYPE_POWER));

    /* Add another type */
    lrg_deck_def_add_allowed_type (def, LRG_CARD_TYPE_SKILL);
    g_assert_true (lrg_deck_def_is_card_type_allowed (def, LRG_CARD_TYPE_SKILL));
}

static void
test_deck_def_banned_cards (DeckbuilderFixture *fixture,
                            gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;

    (void)user_data;

    def = lrg_deck_def_new ("no_curses");

    /* Initially nothing banned */
    g_assert_false (lrg_deck_def_is_card_banned (def, fixture->curse));

    /* Ban the curse */
    lrg_deck_def_ban_card (def, fixture->curse);
    g_assert_true (lrg_deck_def_is_card_banned (def, fixture->curse));
    g_assert_false (lrg_deck_def_is_card_banned (def, fixture->strike));

    /* Unban */
    lrg_deck_def_unban_card (def, fixture->curse);
    g_assert_false (lrg_deck_def_is_card_banned (def, fixture->curse));
}

static void
test_deck_def_can_add_card (DeckbuilderFixture *fixture,
                            gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;

    (void)user_data;

    def = lrg_deck_def_new ("restricted");

    /* Set only attacks allowed */
    lrg_deck_def_set_allowed_types (def, LRG_CARD_TYPE_ATTACK);

    /* Ban curse */
    lrg_deck_def_ban_card (def, fixture->curse);

    /* Strike (attack) should be allowed */
    g_assert_true (lrg_deck_def_can_add_card (def, fixture->strike));

    /* Defend (skill) should not be allowed */
    g_assert_false (lrg_deck_def_can_add_card (def, fixture->defend));

    /* Curse is banned */
    g_assert_false (lrg_deck_def_can_add_card (def, fixture->curse));
}

/* ==========================================================================
 * LrgDeckInstance Tests (Phase 2)
 * ========================================================================== */

static void
test_deck_instance_new (DeckbuilderFixture *fixture,
                        gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckInstance) instance = NULL;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    lrg_deck_def_add_starting_card (def, fixture->strike, 5);
    lrg_deck_def_add_starting_card (def, fixture->defend, 4);

    instance = lrg_deck_instance_new (def);

    g_assert_nonnull (instance);
    g_assert_true (lrg_deck_instance_get_def (instance) == def);
    g_assert_cmpuint (lrg_deck_instance_get_seed (instance), !=, 0);
    g_assert_nonnull (lrg_deck_instance_get_rng (instance));
}

static void
test_deck_instance_new_with_seed (DeckbuilderFixture *fixture,
                                  gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckInstance) instance = NULL;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    instance = lrg_deck_instance_new_with_seed (def, 12345);

    g_assert_cmpuint (lrg_deck_instance_get_seed (instance), ==, 12345);
}

static void
test_deck_instance_piles (DeckbuilderFixture *fixture,
                          gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckInstance) instance = NULL;
    LrgCardPile *draw_pile;
    LrgCardPile *discard_pile;
    LrgCardPile *exhaust_pile;
    LrgHand *hand;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    instance = lrg_deck_instance_new (def);

    draw_pile = lrg_deck_instance_get_draw_pile (instance);
    discard_pile = lrg_deck_instance_get_discard_pile (instance);
    exhaust_pile = lrg_deck_instance_get_exhaust_pile (instance);
    hand = lrg_deck_instance_get_hand (instance);

    g_assert_nonnull (draw_pile);
    g_assert_nonnull (discard_pile);
    g_assert_nonnull (exhaust_pile);
    g_assert_nonnull (hand);

    g_assert_cmpint (lrg_card_pile_get_zone (draw_pile), ==, LRG_ZONE_DRAW);
    g_assert_cmpint (lrg_card_pile_get_zone (discard_pile), ==, LRG_ZONE_DISCARD);
    g_assert_cmpint (lrg_card_pile_get_zone (exhaust_pile), ==, LRG_ZONE_EXHAUST);
}

static void
test_deck_instance_setup (DeckbuilderFixture *fixture,
                          gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckInstance) instance = NULL;
    LrgCardPile *draw_pile;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    lrg_deck_def_add_starting_card (def, fixture->strike, 5);
    lrg_deck_def_add_starting_card (def, fixture->defend, 4);

    instance = lrg_deck_instance_new (def);

    /* Before setup, piles are empty */
    draw_pile = lrg_deck_instance_get_draw_pile (instance);
    g_assert_cmpuint (lrg_card_pile_get_count (draw_pile), ==, 0);

    /* Setup */
    lrg_deck_instance_setup (instance);

    /* After setup, draw pile has all starting cards */
    g_assert_cmpuint (lrg_card_pile_get_count (draw_pile), ==, 9);
    g_assert_cmpuint (lrg_deck_instance_get_total_cards (instance), ==, 9);
}

static void
test_deck_instance_shuffle (DeckbuilderFixture *fixture,
                            gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckInstance) instance1 = NULL;
    g_autoptr(LrgDeckInstance) instance2 = NULL;
    LrgCardPile *pile1;
    LrgCardPile *pile2;
    guint i;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    lrg_deck_def_add_starting_card (def, fixture->strike, 10);

    /* Two instances with same seed should shuffle identically */
    instance1 = lrg_deck_instance_new_with_seed (def, 54321);
    instance2 = lrg_deck_instance_new_with_seed (def, 54321);

    lrg_deck_instance_setup (instance1);
    lrg_deck_instance_setup (instance2);

    pile1 = lrg_deck_instance_get_draw_pile (instance1);
    pile2 = lrg_deck_instance_get_draw_pile (instance2);

    /*
     * With same seed, the deck order should be deterministic.
     * We verify both decks have the same count and the same card
     * definitions in the same positions.
     */
    g_assert_cmpuint (lrg_card_pile_get_count (pile1), ==, lrg_card_pile_get_count (pile2));

    for (i = 0; i < lrg_card_pile_get_count (pile1); i++)
    {
        LrgCardInstance *card1 = lrg_card_pile_get_card_at (pile1, i);
        LrgCardInstance *card2 = lrg_card_pile_get_card_at (pile2, i);
        LrgCardDef *def1 = lrg_card_instance_get_def (card1);
        LrgCardDef *def2 = lrg_card_instance_get_def (card2);

        /* Same card definition in same position means deterministic shuffle */
        g_assert_true (def1 == def2);
    }
}

static void
test_deck_instance_draw_card (DeckbuilderFixture *fixture,
                              gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckInstance) instance = NULL;
    LrgCardPile *draw_pile;
    LrgHand *hand;
    LrgCardInstance *drawn;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    lrg_deck_def_add_starting_card (def, fixture->strike, 5);

    instance = lrg_deck_instance_new (def);
    lrg_deck_instance_setup (instance);

    draw_pile = lrg_deck_instance_get_draw_pile (instance);
    hand = lrg_deck_instance_get_hand (instance);

    g_assert_cmpuint (lrg_card_pile_get_count (draw_pile), ==, 5);
    g_assert_cmpuint (lrg_hand_get_count (hand), ==, 0);

    /* Draw a card */
    drawn = lrg_deck_instance_draw_card (instance);
    g_assert_nonnull (drawn);
    g_assert_cmpuint (lrg_card_pile_get_count (draw_pile), ==, 4);
    g_assert_cmpuint (lrg_hand_get_count (hand), ==, 1);
    g_assert_true (lrg_hand_contains (hand, drawn));
}

static void
test_deck_instance_draw_cards (DeckbuilderFixture *fixture,
                               gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckInstance) instance = NULL;
    LrgHand *hand;
    guint drawn_count;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    lrg_deck_def_add_starting_card (def, fixture->strike, 10);

    instance = lrg_deck_instance_new (def);
    lrg_deck_instance_setup (instance);

    hand = lrg_deck_instance_get_hand (instance);

    /* Draw 5 cards */
    drawn_count = lrg_deck_instance_draw_cards (instance, 5);
    g_assert_cmpuint (drawn_count, ==, 5);
    g_assert_cmpuint (lrg_hand_get_count (hand), ==, 5);
}

static void
test_deck_instance_shuffle_discard_into_draw (DeckbuilderFixture *fixture,
                                              gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckInstance) instance = NULL;
    LrgCardPile *draw_pile;
    LrgCardPile *discard_pile;
    LrgHand *hand;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    lrg_deck_def_add_starting_card (def, fixture->strike, 5);

    instance = lrg_deck_instance_new (def);
    lrg_deck_instance_setup (instance);

    draw_pile = lrg_deck_instance_get_draw_pile (instance);
    discard_pile = lrg_deck_instance_get_discard_pile (instance);
    hand = lrg_deck_instance_get_hand (instance);

    /* Draw all cards to hand */
    lrg_deck_instance_draw_cards (instance, 5);
    g_assert_cmpuint (lrg_card_pile_get_count (draw_pile), ==, 0);
    g_assert_cmpuint (lrg_hand_get_count (hand), ==, 5);

    /* Discard hand */
    lrg_deck_instance_discard_hand (instance);
    g_assert_cmpuint (lrg_hand_get_count (hand), ==, 0);
    g_assert_cmpuint (lrg_card_pile_get_count (discard_pile), ==, 5);

    /* Shuffle discard into draw */
    lrg_deck_instance_shuffle_discard_into_draw (instance);
    g_assert_cmpuint (lrg_card_pile_get_count (draw_pile), ==, 5);
    g_assert_cmpuint (lrg_card_pile_get_count (discard_pile), ==, 0);
}

static void
test_deck_instance_add_card (DeckbuilderFixture *fixture,
                             gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckInstance) instance = NULL;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    lrg_deck_def_add_starting_card (def, fixture->strike, 5);

    instance = lrg_deck_instance_new (def);
    lrg_deck_instance_setup (instance);

    g_assert_cmpuint (lrg_deck_instance_get_total_cards (instance), ==, 5);

    /* Add a card to deck */
    lrg_deck_instance_add_card (instance, fixture->power_card);
    g_assert_cmpuint (lrg_deck_instance_get_total_cards (instance), ==, 6);
    g_assert_cmpuint (lrg_deck_instance_get_master_deck_size (instance), ==, 6);
}

static void
test_deck_instance_remove_card (DeckbuilderFixture *fixture,
                                gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckInstance) instance = NULL;
    LrgCardPile *draw_pile;
    LrgCardInstance *card;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    lrg_deck_def_add_starting_card (def, fixture->strike, 5);

    instance = lrg_deck_instance_new (def);
    lrg_deck_instance_setup (instance);

    draw_pile = lrg_deck_instance_get_draw_pile (instance);
    card = lrg_card_pile_peek (draw_pile);

    g_assert_cmpuint (lrg_deck_instance_get_total_cards (instance), ==, 5);

    /* Remove a card */
    g_assert_true (lrg_deck_instance_remove_card (instance, card));
    g_assert_cmpuint (lrg_deck_instance_get_total_cards (instance), ==, 4);
}

static void
test_deck_instance_count_card_def (DeckbuilderFixture *fixture,
                                   gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckInstance) instance = NULL;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    lrg_deck_def_add_starting_card (def, fixture->strike, 5);
    lrg_deck_def_add_starting_card (def, fixture->defend, 4);

    instance = lrg_deck_instance_new (def);
    lrg_deck_instance_setup (instance);

    g_assert_cmpuint (lrg_deck_instance_count_card_def (instance, fixture->strike), ==, 5);
    g_assert_cmpuint (lrg_deck_instance_count_card_def (instance, fixture->defend), ==, 4);
    g_assert_cmpuint (lrg_deck_instance_count_card_def (instance, fixture->power_card), ==, 0);
}

static void
test_deck_instance_master_deck (DeckbuilderFixture *fixture,
                                gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckInstance) instance = NULL;
    GPtrArray *master_deck;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    lrg_deck_def_add_starting_card (def, fixture->strike, 5);
    lrg_deck_def_add_starting_card (def, fixture->defend, 4);

    instance = lrg_deck_instance_new (def);
    lrg_deck_instance_setup (instance);

    master_deck = lrg_deck_instance_get_master_deck (instance);
    g_assert_nonnull (master_deck);
    g_assert_cmpuint (master_deck->len, ==, 9);
    g_assert_cmpuint (lrg_deck_instance_get_master_deck_size (instance), ==, 9);
}

static void
test_deck_instance_end_combat (DeckbuilderFixture *fixture,
                               gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckInstance) instance = NULL;
    LrgCardPile *draw_pile;
    LrgCardPile *discard_pile;
    LrgCardPile *exhaust_pile;
    LrgHand *hand;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    lrg_deck_def_add_starting_card (def, fixture->strike, 5);

    instance = lrg_deck_instance_new (def);
    lrg_deck_instance_setup (instance);

    draw_pile = lrg_deck_instance_get_draw_pile (instance);
    discard_pile = lrg_deck_instance_get_discard_pile (instance);
    exhaust_pile = lrg_deck_instance_get_exhaust_pile (instance);
    hand = lrg_deck_instance_get_hand (instance);

    /* Simulate combat - draw some cards */
    lrg_deck_instance_draw_cards (instance, 3);

    /* End combat should reset everything to draw pile (except exhaust) */
    lrg_deck_instance_end_combat (instance);

    g_assert_cmpuint (lrg_hand_get_count (hand), ==, 0);
    g_assert_cmpuint (lrg_card_pile_get_count (discard_pile), ==, 0);
    g_assert_cmpuint (lrg_card_pile_get_count (exhaust_pile), ==, 0);
    g_assert_cmpuint (lrg_card_pile_get_count (draw_pile), ==, 5);
}

/* ==========================================================================
 * LrgDeckBuilder Tests (Phase 2)
 * ========================================================================== */

static void
test_deck_builder_new (void)
{
    g_autoptr(LrgDeckBuilder) builder = NULL;

    builder = lrg_deck_builder_new ();

    g_assert_nonnull (builder);
    g_assert_null (lrg_deck_builder_get_deck_def (builder));
    g_assert_cmpuint (lrg_deck_builder_get_max_copies (builder), ==, 0);  /* 0 = unlimited */
}

static void
test_deck_builder_new_with_def (DeckbuilderFixture *fixture,
                                gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckBuilder) builder = NULL;

    (void)user_data;
    (void)fixture;

    def = lrg_deck_def_new ("starter");
    builder = lrg_deck_builder_new_with_def (def);

    g_assert_true (lrg_deck_builder_get_deck_def (builder) == def);
}

static void
test_deck_builder_set_max_copies (DeckbuilderFixture *fixture,
                                  gconstpointer       user_data)
{
    g_autoptr(LrgDeckBuilder) builder = NULL;

    (void)user_data;
    (void)fixture;

    builder = lrg_deck_builder_new ();

    lrg_deck_builder_set_max_copies (builder, 5);
    g_assert_cmpuint (lrg_deck_builder_get_max_copies (builder), ==, 5);
}

static void
test_deck_builder_can_add_card (DeckbuilderFixture *fixture,
                                gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckBuilder) builder = NULL;
    g_autoptr(LrgDeckInstance) deck = NULL;
    g_autoptr(GError) error = NULL;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    lrg_deck_def_add_starting_card (def, fixture->strike, 2);

    builder = lrg_deck_builder_new_with_def (def);
    lrg_deck_builder_set_max_copies (builder, 3);

    deck = lrg_deck_instance_new (def);
    lrg_deck_instance_setup (deck);

    /* Should be able to add (have 2, max is 3) */
    g_assert_true (lrg_deck_builder_can_add_card (builder, deck, fixture->strike, &error));
    g_assert_no_error (error);

    /* Add one more */
    lrg_deck_instance_add_card (deck, fixture->strike);

    /* Now at limit (3 copies) - shouldn't be able to add */
    g_assert_false (lrg_deck_builder_can_add_card (builder, deck, fixture->strike, &error));
    g_assert_error (error, LRG_DECKBUILDER_ERROR, LRG_DECKBUILDER_ERROR_CARD_LIMIT_EXCEEDED);
}

static void
test_deck_builder_can_add_banned_card (DeckbuilderFixture *fixture,
                                       gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckBuilder) builder = NULL;
    g_autoptr(LrgDeckInstance) deck = NULL;
    g_autoptr(GError) error = NULL;

    (void)user_data;

    def = lrg_deck_def_new ("no_curses");
    lrg_deck_def_ban_card (def, fixture->curse);

    builder = lrg_deck_builder_new_with_def (def);
    deck = lrg_deck_instance_new (def);
    lrg_deck_instance_setup (deck);

    g_assert_false (lrg_deck_builder_can_add_card (builder, deck, fixture->curse, &error));
    g_assert_error (error, LRG_DECKBUILDER_ERROR, LRG_DECKBUILDER_ERROR_CARD_BANNED);
}

static void
test_deck_builder_can_add_wrong_type (DeckbuilderFixture *fixture,
                                      gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckBuilder) builder = NULL;
    g_autoptr(LrgDeckInstance) deck = NULL;
    g_autoptr(GError) error = NULL;

    (void)user_data;

    def = lrg_deck_def_new ("attacks_only");
    lrg_deck_def_set_allowed_types (def, LRG_CARD_TYPE_ATTACK);

    builder = lrg_deck_builder_new_with_def (def);
    deck = lrg_deck_instance_new (def);
    lrg_deck_instance_setup (deck);

    /* Attack should be allowed */
    g_assert_true (lrg_deck_builder_can_add_card (builder, deck, fixture->strike, &error));
    g_assert_no_error (error);

    /* Skill should not be allowed */
    g_assert_false (lrg_deck_builder_can_add_card (builder, deck, fixture->defend, &error));
    g_assert_error (error, LRG_DECKBUILDER_ERROR, LRG_DECKBUILDER_ERROR_CARD_NOT_ALLOWED);
}

static void
test_deck_builder_add_card (DeckbuilderFixture *fixture,
                            gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckBuilder) builder = NULL;
    g_autoptr(LrgDeckInstance) deck = NULL;
    g_autoptr(GError) error = NULL;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    builder = lrg_deck_builder_new_with_def (def);
    deck = lrg_deck_instance_new (def);
    lrg_deck_instance_setup (deck);

    g_assert_cmpuint (lrg_deck_instance_get_total_cards (deck), ==, 0);

    g_assert_true (lrg_deck_builder_add_card (builder, deck, fixture->strike, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_deck_instance_get_total_cards (deck), ==, 1);
}

static void
test_deck_builder_remove_card (DeckbuilderFixture *fixture,
                               gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckBuilder) builder = NULL;
    g_autoptr(LrgDeckInstance) deck = NULL;
    g_autoptr(GError) error = NULL;
    LrgCardPile *draw_pile;
    LrgCardInstance *card;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    lrg_deck_def_add_starting_card (def, fixture->strike, 5);

    builder = lrg_deck_builder_new_with_def (def);
    deck = lrg_deck_instance_new (def);
    lrg_deck_instance_setup (deck);

    draw_pile = lrg_deck_instance_get_draw_pile (deck);
    card = lrg_card_pile_peek (draw_pile);

    g_assert_true (lrg_deck_builder_remove_card (builder, deck, card, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_deck_instance_get_total_cards (deck), ==, 4);
}

static void
test_deck_builder_validate_deck (DeckbuilderFixture *fixture,
                                 gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckBuilder) builder = NULL;
    g_autoptr(LrgDeckInstance) deck = NULL;
    g_autoptr(GError) error = NULL;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    lrg_deck_def_set_min_size (def, 5);
    lrg_deck_def_set_max_size (def, 10);
    lrg_deck_def_add_starting_card (def, fixture->strike, 5);

    builder = lrg_deck_builder_new_with_def (def);
    deck = lrg_deck_instance_new (def);
    lrg_deck_instance_setup (deck);

    /* 5 cards, min 5, max 10 - should be valid */
    g_assert_true (lrg_deck_builder_validate_deck (builder, deck, &error));
    g_assert_no_error (error);
}

static void
test_deck_builder_validate_deck_too_small (DeckbuilderFixture *fixture,
                                           gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckBuilder) builder = NULL;
    g_autoptr(LrgDeckInstance) deck = NULL;
    g_autoptr(GError) error = NULL;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    lrg_deck_def_set_min_size (def, 10);  /* Min 10 */
    lrg_deck_def_add_starting_card (def, fixture->strike, 3);  /* Only 3 */

    builder = lrg_deck_builder_new_with_def (def);
    deck = lrg_deck_instance_new (def);
    lrg_deck_instance_setup (deck);

    g_assert_false (lrg_deck_builder_validate_deck (builder, deck, &error));
    g_assert_error (error, LRG_DECKBUILDER_ERROR, LRG_DECKBUILDER_ERROR_DECK_TOO_SMALL);
}

static void
test_deck_builder_validate_deck_too_large (DeckbuilderFixture *fixture,
                                           gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckBuilder) builder = NULL;
    g_autoptr(LrgDeckInstance) deck = NULL;
    g_autoptr(GError) error = NULL;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    lrg_deck_def_set_max_size (def, 5);  /* Max 5 */
    lrg_deck_def_add_starting_card (def, fixture->strike, 10);  /* 10 cards */

    builder = lrg_deck_builder_new_with_def (def);
    deck = lrg_deck_instance_new (def);
    lrg_deck_instance_setup (deck);

    g_assert_false (lrg_deck_builder_validate_deck (builder, deck, &error));
    g_assert_error (error, LRG_DECKBUILDER_ERROR, LRG_DECKBUILDER_ERROR_DECK_TOO_LARGE);
}

static void
test_deck_builder_build (DeckbuilderFixture *fixture,
                         gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckBuilder) builder = NULL;
    g_autoptr(LrgDeckInstance) deck = NULL;
    g_autoptr(GError) error = NULL;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    lrg_deck_def_add_starting_card (def, fixture->strike, 5);
    lrg_deck_def_add_starting_card (def, fixture->defend, 4);

    builder = lrg_deck_builder_new_with_def (def);

    deck = lrg_deck_builder_build (builder, &error);

    g_assert_nonnull (deck);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_deck_instance_get_total_cards (deck), ==, 9);
}

static void
test_deck_builder_build_with_seed (DeckbuilderFixture *fixture,
                                   gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckBuilder) builder = NULL;
    g_autoptr(LrgDeckInstance) deck = NULL;
    g_autoptr(GError) error = NULL;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    lrg_deck_def_add_starting_card (def, fixture->strike, 5);

    builder = lrg_deck_builder_new_with_def (def);

    deck = lrg_deck_builder_build_with_seed (builder, 99999, &error);

    g_assert_nonnull (deck);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_deck_instance_get_seed (deck), ==, 99999);
}

static void
test_deck_builder_upgrade_card (DeckbuilderFixture *fixture,
                                gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckBuilder) builder = NULL;
    g_autoptr(LrgDeckInstance) deck = NULL;
    g_autoptr(GError) error = NULL;
    LrgCardPile *draw_pile;
    LrgCardInstance *card;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    lrg_deck_def_add_starting_card (def, fixture->strike, 5);

    builder = lrg_deck_builder_new_with_def (def);
    deck = lrg_deck_instance_new (def);
    lrg_deck_instance_setup (deck);

    draw_pile = lrg_deck_instance_get_draw_pile (deck);
    card = lrg_card_pile_peek (draw_pile);

    g_assert_cmpint (lrg_card_instance_get_upgrade_tier (card), ==, LRG_CARD_UPGRADE_TIER_BASE);

    g_assert_true (lrg_deck_builder_upgrade_card (builder, deck, card, &error));
    g_assert_no_error (error);
    g_assert_cmpint (lrg_card_instance_get_upgrade_tier (card), ==, LRG_CARD_UPGRADE_TIER_PLUS);
}

static void
test_deck_builder_transform_card (DeckbuilderFixture *fixture,
                                  gconstpointer       user_data)
{
    g_autoptr(LrgDeckDef) def = NULL;
    g_autoptr(LrgDeckBuilder) builder = NULL;
    g_autoptr(LrgDeckInstance) deck = NULL;
    g_autoptr(GError) error = NULL;
    LrgCardPile *draw_pile;
    LrgCardInstance *card;

    (void)user_data;

    def = lrg_deck_def_new ("starter");
    lrg_deck_def_add_starting_card (def, fixture->strike, 5);

    builder = lrg_deck_builder_new_with_def (def);
    deck = lrg_deck_instance_new (def);
    lrg_deck_instance_setup (deck);

    draw_pile = lrg_deck_instance_get_draw_pile (deck);
    card = lrg_card_pile_peek (draw_pile);

    g_assert_cmpstr (lrg_card_instance_get_id (card), ==, "strike");

    g_assert_true (lrg_deck_builder_transform_card (builder, deck, card, fixture->power_card, &error));
    g_assert_no_error (error);

    /* The old card is removed, get the new card from the pile */
    card = lrg_card_pile_peek (draw_pile);
    g_assert_nonnull (card);
    g_assert_cmpstr (lrg_card_instance_get_id (card), ==, "demon_form");
}

/* ==========================================================================
 * Phase 3: Card Effect Tests
 * ========================================================================== */

static void
test_card_effect_new (void)
{
    g_autoptr(LrgCardEffect) effect = NULL;

    effect = lrg_card_effect_new ("damage");
    g_assert_nonnull (effect);
    g_assert_cmpstr (lrg_card_effect_get_effect_type (effect), ==, "damage");
    g_assert_cmpint (lrg_card_effect_get_target_type (effect), ==, LRG_CARD_TARGET_NONE);
    g_assert_cmpint (lrg_card_effect_get_flags (effect), ==, LRG_EFFECT_FLAG_NONE);
    g_assert_cmpint (lrg_card_effect_get_priority (effect), ==, 0);
}

static void
test_card_effect_params (void)
{
    g_autoptr(LrgCardEffect) effect = NULL;

    effect = lrg_card_effect_new ("damage");

    /* Test integer params */
    g_assert_false (lrg_card_effect_has_param (effect, "amount"));
    lrg_card_effect_set_param_int (effect, "amount", 6);
    g_assert_true (lrg_card_effect_has_param (effect, "amount"));
    g_assert_cmpint (lrg_card_effect_get_param_int (effect, "amount", 0), ==, 6);
    g_assert_cmpint (lrg_card_effect_get_param_int (effect, "missing", 99), ==, 99);

    /* Test float params */
    lrg_card_effect_set_param_float (effect, "multiplier", 1.5f);
    g_assert_true (lrg_card_effect_has_param (effect, "multiplier"));
    g_assert_cmpfloat_with_epsilon (lrg_card_effect_get_param_float (effect, "multiplier", 0.0f), 1.5f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_card_effect_get_param_float (effect, "missing", 2.0f), 2.0f, 0.001f);

    /* Test string params */
    lrg_card_effect_set_param_string (effect, "status", "vulnerable");
    g_assert_true (lrg_card_effect_has_param (effect, "status"));
    g_assert_cmpstr (lrg_card_effect_get_param_string (effect, "status", NULL), ==, "vulnerable");
    g_assert_null (lrg_card_effect_get_param_string (effect, "missing", NULL));
}

static void
test_card_effect_flags (void)
{
    g_autoptr(LrgCardEffect) effect = NULL;

    effect = lrg_card_effect_new ("damage");

    g_assert_false (lrg_card_effect_has_flag (effect, LRG_EFFECT_FLAG_UNBLOCKABLE));
    g_assert_false (lrg_card_effect_has_flag (effect, LRG_EFFECT_FLAG_PIERCING));

    lrg_card_effect_add_flag (effect, LRG_EFFECT_FLAG_UNBLOCKABLE);
    g_assert_true (lrg_card_effect_has_flag (effect, LRG_EFFECT_FLAG_UNBLOCKABLE));
    g_assert_false (lrg_card_effect_has_flag (effect, LRG_EFFECT_FLAG_PIERCING));

    lrg_card_effect_add_flag (effect, LRG_EFFECT_FLAG_LIFESTEAL);
    g_assert_true (lrg_card_effect_has_flag (effect, LRG_EFFECT_FLAG_UNBLOCKABLE));
    g_assert_true (lrg_card_effect_has_flag (effect, LRG_EFFECT_FLAG_LIFESTEAL));

    lrg_card_effect_set_flags (effect, LRG_EFFECT_FLAG_PIERCING);
    g_assert_false (lrg_card_effect_has_flag (effect, LRG_EFFECT_FLAG_UNBLOCKABLE));
    g_assert_true (lrg_card_effect_has_flag (effect, LRG_EFFECT_FLAG_PIERCING));
}

static void
test_card_effect_priority (void)
{
    g_autoptr(LrgCardEffect) effect = NULL;

    effect = lrg_card_effect_new ("damage");
    g_assert_cmpint (lrg_card_effect_get_priority (effect), ==, 0);

    lrg_card_effect_set_priority (effect, 100);
    g_assert_cmpint (lrg_card_effect_get_priority (effect), ==, 100);

    lrg_card_effect_set_priority (effect, -50);
    g_assert_cmpint (lrg_card_effect_get_priority (effect), ==, -50);
}

static void
test_card_effect_copy (void)
{
    g_autoptr(LrgCardEffect) effect = NULL;
    g_autoptr(LrgCardEffect) copy = NULL;

    effect = lrg_card_effect_new ("damage");
    lrg_card_effect_set_target_type (effect, LRG_CARD_TARGET_SINGLE_ENEMY);
    lrg_card_effect_add_flag (effect, LRG_EFFECT_FLAG_UNBLOCKABLE);
    lrg_card_effect_set_priority (effect, 50);
    lrg_card_effect_set_param_int (effect, "amount", 10);
    lrg_card_effect_set_param_float (effect, "mult", 2.0f);
    lrg_card_effect_set_param_string (effect, "note", "test");

    copy = lrg_card_effect_copy (effect);
    g_assert_nonnull (copy);

    /* Verify copy has same values */
    g_assert_cmpstr (lrg_card_effect_get_effect_type (copy), ==, "damage");
    g_assert_cmpint (lrg_card_effect_get_target_type (copy), ==, LRG_CARD_TARGET_SINGLE_ENEMY);
    g_assert_true (lrg_card_effect_has_flag (copy, LRG_EFFECT_FLAG_UNBLOCKABLE));
    g_assert_cmpint (lrg_card_effect_get_priority (copy), ==, 50);
    g_assert_cmpint (lrg_card_effect_get_param_int (copy, "amount", 0), ==, 10);
    g_assert_cmpfloat_with_epsilon (lrg_card_effect_get_param_float (copy, "mult", 0.0f), 2.0f, 0.001f);
    g_assert_cmpstr (lrg_card_effect_get_param_string (copy, "note", NULL), ==, "test");

    /* Modify original - should not affect copy */
    lrg_card_effect_set_param_int (effect, "amount", 20);
    g_assert_cmpint (lrg_card_effect_get_param_int (copy, "amount", 0), ==, 10);
}

/* ==========================================================================
 * Phase 3: Effect Registry Tests
 * ========================================================================== */

static void
test_effect_registry_singleton (void)
{
    LrgCardEffectRegistry *registry1;
    LrgCardEffectRegistry *registry2;

    registry1 = lrg_card_effect_registry_get_default ();
    g_assert_nonnull (registry1);

    registry2 = lrg_card_effect_registry_get_default ();
    g_assert_true (registry1 == registry2);
}

/* ==========================================================================
 * Phase 3: Effect Stack Tests
 * ========================================================================== */

static void
test_effect_stack_new (void)
{
    LrgCardEffectRegistry *registry;
    g_autoptr(LrgEffectStack) stack = NULL;

    registry = lrg_card_effect_registry_get_default ();
    stack = lrg_effect_stack_new (registry);
    g_assert_nonnull (stack);
    g_assert_true (lrg_effect_stack_is_empty (stack));
    g_assert_cmpuint (lrg_effect_stack_get_count (stack), ==, 0);
}

static void
test_effect_stack_push_pop (void)
{
    LrgCardEffectRegistry *registry;
    g_autoptr(LrgEffectStack) stack = NULL;
    g_autoptr(LrgCardEffect) effect1 = NULL;
    g_autoptr(LrgCardEffect) effect2 = NULL;
    LrgEffectStackEntry *entry = NULL;

    registry = lrg_card_effect_registry_get_default ();
    stack = lrg_effect_stack_new (registry);

    effect1 = lrg_card_effect_new ("damage");
    lrg_card_effect_set_param_int (effect1, "amount", 6);

    effect2 = lrg_card_effect_new ("block");
    lrg_card_effect_set_param_int (effect2, "amount", 5);

    /* Push effects */
    lrg_effect_stack_push_effect (stack, effect1, NULL, NULL);
    g_assert_false (lrg_effect_stack_is_empty (stack));
    g_assert_cmpuint (lrg_effect_stack_get_count (stack), ==, 1);

    lrg_effect_stack_push_effect (stack, effect2, NULL, NULL);
    g_assert_cmpuint (lrg_effect_stack_get_count (stack), ==, 2);

    /* Pop effects - default priority (0), should come back in order pushed */
    entry = lrg_effect_stack_pop (stack);
    g_assert_nonnull (entry);
    g_assert_cmpstr (lrg_card_effect_get_effect_type (lrg_effect_stack_entry_get_effect (entry)),
                     ==, "damage");
    lrg_effect_stack_entry_free (entry);

    entry = lrg_effect_stack_pop (stack);
    g_assert_nonnull (entry);
    g_assert_cmpstr (lrg_card_effect_get_effect_type (lrg_effect_stack_entry_get_effect (entry)),
                     ==, "block");
    lrg_effect_stack_entry_free (entry);

    g_assert_true (lrg_effect_stack_is_empty (stack));
    g_assert_null (lrg_effect_stack_pop (stack));
}

static void
test_effect_stack_priority_order (void)
{
    LrgCardEffectRegistry *registry;
    g_autoptr(LrgEffectStack) stack = NULL;
    g_autoptr(LrgCardEffect) low_priority = NULL;
    g_autoptr(LrgCardEffect) high_priority = NULL;
    g_autoptr(LrgCardEffect) mid_priority = NULL;
    LrgEffectStackEntry *entry = NULL;

    registry = lrg_card_effect_registry_get_default ();
    stack = lrg_effect_stack_new (registry);

    /* Create effects with different priorities */
    low_priority = lrg_card_effect_new ("low");
    lrg_card_effect_set_priority (low_priority, 10);

    mid_priority = lrg_card_effect_new ("mid");
    lrg_card_effect_set_priority (mid_priority, 50);

    high_priority = lrg_card_effect_new ("high");
    lrg_card_effect_set_priority (high_priority, 100);

    /* Push in wrong order */
    lrg_effect_stack_push_effect (stack, low_priority, NULL, NULL);
    lrg_effect_stack_push_effect (stack, high_priority, NULL, NULL);
    lrg_effect_stack_push_effect (stack, mid_priority, NULL, NULL);

    /* Pop should return highest priority first */
    entry = lrg_effect_stack_pop (stack);
    g_assert_cmpstr (lrg_card_effect_get_effect_type (lrg_effect_stack_entry_get_effect (entry)),
                     ==, "high");
    lrg_effect_stack_entry_free (entry);

    entry = lrg_effect_stack_pop (stack);
    g_assert_cmpstr (lrg_card_effect_get_effect_type (lrg_effect_stack_entry_get_effect (entry)),
                     ==, "mid");
    lrg_effect_stack_entry_free (entry);

    entry = lrg_effect_stack_pop (stack);
    g_assert_cmpstr (lrg_card_effect_get_effect_type (lrg_effect_stack_entry_get_effect (entry)),
                     ==, "low");
    lrg_effect_stack_entry_free (entry);

    g_assert_true (lrg_effect_stack_is_empty (stack));
}

/* ==========================================================================
 * Phase 3.5: Trigger/Event System Tests
 * ========================================================================== */

static void
test_card_event_new (void)
{
    g_autoptr(LrgCardEvent) event = NULL;

    event = lrg_card_event_new (LRG_CARD_EVENT_DAMAGE_DEALT);
    g_assert_nonnull (event);
    g_assert_cmpint (lrg_card_event_get_event_type (event), ==, LRG_CARD_EVENT_DAMAGE_DEALT);
    g_assert_null (lrg_card_event_get_source (event));
    g_assert_null (lrg_card_event_get_target (event));
    g_assert_cmpint (lrg_card_event_get_amount (event), ==, 0);
    g_assert_false (lrg_card_event_is_cancelled (event));
}

static void
test_card_event_damage (void)
{
    g_autoptr(LrgCardEvent) event = NULL;
    gint dummy_source = 1;
    gint dummy_target = 2;

    event = lrg_card_event_new_damage (&dummy_source, &dummy_target, 10, LRG_EFFECT_FLAG_PIERCING);
    g_assert_nonnull (event);
    g_assert_cmpint (lrg_card_event_get_event_type (event), ==, LRG_CARD_EVENT_DAMAGE_DEALT);
    g_assert_true (lrg_card_event_get_source (event) == &dummy_source);
    g_assert_true (lrg_card_event_get_target (event) == &dummy_target);
    g_assert_cmpint (lrg_card_event_get_amount (event), ==, 10);
    g_assert_cmpint (lrg_card_event_get_flags (event), ==, LRG_EFFECT_FLAG_PIERCING);
}

static void
test_card_event_block (void)
{
    g_autoptr(LrgCardEvent) event = NULL;
    gint dummy_target = 1;

    event = lrg_card_event_new_block (&dummy_target, 5);
    g_assert_nonnull (event);
    g_assert_cmpint (lrg_card_event_get_event_type (event), ==, LRG_CARD_EVENT_BLOCK_GAINED);
    g_assert_true (lrg_card_event_get_target (event) == &dummy_target);
    g_assert_cmpint (lrg_card_event_get_amount (event), ==, 5);
}

static void
test_card_event_status (void)
{
    g_autoptr(LrgCardEvent) event = NULL;
    gint dummy_target = 1;

    event = lrg_card_event_new_status (LRG_CARD_EVENT_STATUS_APPLIED, &dummy_target, "poison", 3);
    g_assert_nonnull (event);
    g_assert_cmpint (lrg_card_event_get_event_type (event), ==, LRG_CARD_EVENT_STATUS_APPLIED);
    g_assert_true (lrg_card_event_get_target (event) == &dummy_target);
    g_assert_cmpstr (lrg_card_event_get_status_id (event), ==, "poison");
    g_assert_cmpint (lrg_card_event_get_amount (event), ==, 3);
}

static void
test_card_event_cancel (void)
{
    g_autoptr(LrgCardEvent) event = NULL;

    event = lrg_card_event_new (LRG_CARD_EVENT_CARD_PLAYED);
    g_assert_false (lrg_card_event_is_cancelled (event));

    lrg_card_event_cancel (event);
    g_assert_true (lrg_card_event_is_cancelled (event));
}

static void
test_card_event_copy (void)
{
    g_autoptr(LrgCardEvent) original = NULL;
    g_autoptr(LrgCardEvent) copy = NULL;
    gint dummy_target = 1;

    original = lrg_card_event_new_status (LRG_CARD_EVENT_STATUS_APPLIED, &dummy_target, "strength", 2);
    lrg_card_event_set_turn (original, 5);

    copy = lrg_card_event_copy (original);
    g_assert_nonnull (copy);
    g_assert_cmpint (lrg_card_event_get_event_type (copy), ==, LRG_CARD_EVENT_STATUS_APPLIED);
    g_assert_cmpstr (lrg_card_event_get_status_id (copy), ==, "strength");
    g_assert_cmpint (lrg_card_event_get_amount (copy), ==, 2);
    g_assert_cmpuint (lrg_card_event_get_turn (copy), ==, 5);
}

static void
test_event_bus_new (void)
{
    g_autoptr(LrgEventBus) bus = NULL;

    bus = lrg_event_bus_new ();
    g_assert_nonnull (bus);
    g_assert_cmpuint (lrg_event_bus_get_listener_count (bus), ==, 0);
}

static void
test_event_bus_singleton (void)
{
    LrgEventBus *bus1;
    LrgEventBus *bus2;

    bus1 = lrg_event_bus_get_default ();
    bus2 = lrg_event_bus_get_default ();

    g_assert_nonnull (bus1);
    g_assert_true (bus1 == bus2);
}

static void
test_event_bus_emit_no_listeners (void)
{
    g_autoptr(LrgEventBus) bus = NULL;
    LrgCardEvent *event = NULL;
    gboolean result;

    bus = lrg_event_bus_new ();
    event = lrg_card_event_new (LRG_CARD_EVENT_TURN_START);

    /* Emit with no listeners should succeed */
    result = lrg_event_bus_emit (bus, event, NULL);
    g_assert_true (result);
}

static void
test_trigger_listener_mask (void)
{
    guint64 mask;

    mask = lrg_trigger_listener_event_type_to_mask (LRG_CARD_EVENT_DAMAGE_DEALT);
    g_assert_cmpuint (mask, ==, ((guint64)1) << LRG_CARD_EVENT_DAMAGE_DEALT);

    mask = lrg_trigger_listener_event_type_to_mask (LRG_CARD_EVENT_TURN_START);
    g_assert_cmpuint (mask, ==, ((guint64)1) << LRG_CARD_EVENT_TURN_START);
}

/* ==========================================================================
 * Phase 4: Keyword System Tests
 * ========================================================================== */

static void
test_card_keyword_get_name (void)
{
    const gchar *name;

    name = lrg_card_keyword_get_name (LRG_CARD_KEYWORD_INNATE);
    g_assert_nonnull (name);
    g_assert_cmpstr (name, ==, "Innate");

    name = lrg_card_keyword_get_name (LRG_CARD_KEYWORD_EXHAUST);
    g_assert_nonnull (name);
    g_assert_cmpstr (name, ==, "Exhaust");

    name = lrg_card_keyword_get_name (LRG_CARD_KEYWORD_NONE);
    g_assert_null (name);
}

static void
test_card_keyword_get_description (void)
{
    const gchar *desc;

    desc = lrg_card_keyword_get_description (LRG_CARD_KEYWORD_RETAIN);
    g_assert_nonnull (desc);
    g_assert_true (g_str_has_prefix (desc, "This card is not discarded"));

    desc = lrg_card_keyword_get_description (LRG_CARD_KEYWORD_NONE);
    g_assert_null (desc);
}

static void
test_card_keyword_is_positive (void)
{
    g_assert_true (lrg_card_keyword_is_positive (LRG_CARD_KEYWORD_INNATE));
    g_assert_true (lrg_card_keyword_is_positive (LRG_CARD_KEYWORD_RETAIN));
    g_assert_false (lrg_card_keyword_is_positive (LRG_CARD_KEYWORD_UNPLAYABLE));
    g_assert_false (lrg_card_keyword_is_positive (LRG_CARD_KEYWORD_ETHEREAL));
}

static void
test_card_keyword_is_negative (void)
{
    g_assert_true (lrg_card_keyword_is_negative (LRG_CARD_KEYWORD_UNPLAYABLE));
    g_assert_true (lrg_card_keyword_is_negative (LRG_CARD_KEYWORD_ETHEREAL));
    g_assert_false (lrg_card_keyword_is_negative (LRG_CARD_KEYWORD_INNATE));
    g_assert_false (lrg_card_keyword_is_negative (LRG_CARD_KEYWORD_RETAIN));
}

static void
test_card_keyword_from_string (void)
{
    LrgCardKeyword keyword;

    keyword = lrg_card_keyword_from_string ("Innate");
    g_assert_cmpint (keyword, ==, LRG_CARD_KEYWORD_INNATE);

    keyword = lrg_card_keyword_from_string ("innate");
    g_assert_cmpint (keyword, ==, LRG_CARD_KEYWORD_INNATE);

    keyword = lrg_card_keyword_from_string ("EXHAUST");
    g_assert_cmpint (keyword, ==, LRG_CARD_KEYWORD_EXHAUST);

    keyword = lrg_card_keyword_from_string ("invalid");
    g_assert_cmpint (keyword, ==, LRG_CARD_KEYWORD_NONE);
}

static void
test_card_keyword_to_string (void)
{
    const gchar *str;

    str = lrg_card_keyword_to_string (LRG_CARD_KEYWORD_INNATE);
    g_assert_cmpstr (str, ==, "Innate");

    str = lrg_card_keyword_to_string (LRG_CARD_KEYWORD_EXHAUST);
    g_assert_cmpstr (str, ==, "Exhaust");
}

static void
test_card_keywords_from_string (void)
{
    LrgCardKeyword keywords;

    keywords = lrg_card_keywords_from_string ("Innate,Exhaust");
    g_assert_true (keywords & LRG_CARD_KEYWORD_INNATE);
    g_assert_true (keywords & LRG_CARD_KEYWORD_EXHAUST);

    keywords = lrg_card_keywords_from_string ("innate, exhaust, retain");
    g_assert_true (keywords & LRG_CARD_KEYWORD_INNATE);
    g_assert_true (keywords & LRG_CARD_KEYWORD_EXHAUST);
    g_assert_true (keywords & LRG_CARD_KEYWORD_RETAIN);
}

static void
test_card_keywords_to_string (void)
{
    g_autofree gchar *str = NULL;
    LrgCardKeyword keywords;

    keywords = LRG_CARD_KEYWORD_INNATE | LRG_CARD_KEYWORD_EXHAUST;
    str = lrg_card_keywords_to_string (keywords);
    g_assert_nonnull (str);
    /* Should contain both keywords */
    g_assert_true (g_strstr_len (str, -1, "Innate") != NULL);
    g_assert_true (g_strstr_len (str, -1, "Exhaust") != NULL);
}

static void
test_card_keyword_count (void)
{
    guint count;

    count = lrg_card_keyword_count (LRG_CARD_KEYWORD_NONE);
    g_assert_cmpuint (count, ==, 0);

    count = lrg_card_keyword_count (LRG_CARD_KEYWORD_INNATE);
    g_assert_cmpuint (count, ==, 1);

    count = lrg_card_keyword_count (LRG_CARD_KEYWORD_INNATE | LRG_CARD_KEYWORD_EXHAUST);
    g_assert_cmpuint (count, ==, 2);

    count = lrg_card_keyword_count (LRG_CARD_KEYWORD_INNATE | LRG_CARD_KEYWORD_EXHAUST | LRG_CARD_KEYWORD_RETAIN);
    g_assert_cmpuint (count, ==, 3);
}

static void
test_card_keyword_def_new (void)
{
    g_autoptr(LrgCardKeywordDef) def = NULL;

    def = lrg_card_keyword_def_new ("custom-keyword", "Custom", "A custom keyword");
    g_assert_nonnull (def);
    g_assert_cmpstr (lrg_card_keyword_def_get_id (def), ==, "custom-keyword");
    g_assert_cmpstr (lrg_card_keyword_def_get_name (def), ==, "Custom");
    g_assert_cmpstr (lrg_card_keyword_def_get_description (def), ==, "A custom keyword");
}

static void
test_card_keyword_def_properties (void)
{
    g_autoptr(LrgCardKeywordDef) def = NULL;

    def = lrg_card_keyword_def_new ("test-keyword", "Test", "Test description");

    /* Test is_positive/is_negative default to FALSE */
    g_assert_false (lrg_card_keyword_def_is_positive (def));
    g_assert_false (lrg_card_keyword_def_is_negative (def));

    /* Set positive */
    lrg_card_keyword_def_set_positive (def, TRUE);
    g_assert_true (lrg_card_keyword_def_is_positive (def));

    /* Set negative */
    lrg_card_keyword_def_set_negative (def, TRUE);
    g_assert_true (lrg_card_keyword_def_is_negative (def));

    /* Set icon */
    g_assert_null (lrg_card_keyword_def_get_icon (def));
    lrg_card_keyword_def_set_icon (def, "icon-test");
    g_assert_cmpstr (lrg_card_keyword_def_get_icon (def), ==, "icon-test");
}

static void
test_card_keyword_registry_singleton (void)
{
    LrgCardKeywordRegistry *registry1;
    LrgCardKeywordRegistry *registry2;

    registry1 = lrg_card_keyword_registry_get_default ();
    g_assert_nonnull (registry1);

    registry2 = lrg_card_keyword_registry_get_default ();
    g_assert_true (registry1 == registry2);
}

static void
test_card_keyword_registry_register (void)
{
    LrgCardKeywordRegistry *registry;
    g_autoptr(LrgCardKeywordDef) def = NULL;
    gboolean success;

    registry = lrg_card_keyword_registry_get_default ();
    lrg_card_keyword_registry_clear (registry);

    def = lrg_card_keyword_def_new ("test-reg-keyword", "TestReg", "Test registration");

    /* Register should succeed */
    success = lrg_card_keyword_registry_register (registry, def);
    g_assert_true (success);
    g_assert_true (lrg_card_keyword_registry_is_registered (registry, "test-reg-keyword"));
    g_assert_cmpuint (lrg_card_keyword_registry_get_count (registry), ==, 1);

    /* Duplicate registration should fail */
    success = lrg_card_keyword_registry_register (registry, def);
    g_assert_false (success);

    lrg_card_keyword_registry_clear (registry);
}

static void
test_card_keyword_registry_lookup (void)
{
    LrgCardKeywordRegistry *registry;
    g_autoptr(LrgCardKeywordDef) def = NULL;
    LrgCardKeywordDef *found;

    registry = lrg_card_keyword_registry_get_default ();
    lrg_card_keyword_registry_clear (registry);

    def = lrg_card_keyword_def_new ("lookup-test", "Lookup", "Lookup test");
    lrg_card_keyword_registry_register (registry, def);

    found = lrg_card_keyword_registry_lookup (registry, "lookup-test");
    g_assert_nonnull (found);
    g_assert_cmpstr (lrg_card_keyword_def_get_id (found), ==, "lookup-test");

    found = lrg_card_keyword_registry_lookup (registry, "nonexistent");
    g_assert_null (found);

    lrg_card_keyword_registry_clear (registry);
}

static void
test_card_keyword_registry_unregister (void)
{
    LrgCardKeywordRegistry *registry;
    g_autoptr(LrgCardKeywordDef) def = NULL;
    gboolean success;

    registry = lrg_card_keyword_registry_get_default ();
    lrg_card_keyword_registry_clear (registry);

    def = lrg_card_keyword_def_new ("unreg-test", "Unreg", "Unregister test");
    lrg_card_keyword_registry_register (registry, def);

    g_assert_true (lrg_card_keyword_registry_is_registered (registry, "unreg-test"));

    success = lrg_card_keyword_registry_unregister (registry, "unreg-test");
    g_assert_true (success);
    g_assert_false (lrg_card_keyword_registry_is_registered (registry, "unreg-test"));

    /* Unregistering again should fail */
    success = lrg_card_keyword_registry_unregister (registry, "unreg-test");
    g_assert_false (success);

    lrg_card_keyword_registry_clear (registry);
}

static void
test_synergy_new (void)
{
    g_autoptr(LrgSynergy) synergy = NULL;

    synergy = lrg_synergy_new ("test-synergy", "Test Synergy", LRG_SYNERGY_TYPE_KEYWORD);
    g_assert_nonnull (synergy);
    g_assert_cmpstr (lrg_synergy_get_id (synergy), ==, "test-synergy");
    g_assert_cmpstr (lrg_synergy_get_name (synergy), ==, "Test Synergy");
    g_assert_cmpint (lrg_synergy_get_synergy_type (synergy), ==, LRG_SYNERGY_TYPE_KEYWORD);
}

static void
test_synergy_new_keyword (void)
{
    g_autoptr(LrgSynergy) synergy = NULL;

    synergy = lrg_synergy_new_keyword ("exhaust-synergy", "Exhaust Synergy",
                                        LRG_CARD_KEYWORD_EXHAUST, 3);
    g_assert_nonnull (synergy);
    g_assert_cmpint (lrg_synergy_get_synergy_type (synergy), ==, LRG_SYNERGY_TYPE_KEYWORD);
    g_assert_cmpuint (lrg_synergy_get_min_count (synergy), ==, 3);
}

static void
test_synergy_new_card_type (void)
{
    g_autoptr(LrgSynergy) synergy = NULL;

    synergy = lrg_synergy_new_card_type ("attack-synergy", "Attack Synergy",
                                          LRG_CARD_TYPE_ATTACK, 5);
    g_assert_nonnull (synergy);
    g_assert_cmpint (lrg_synergy_get_synergy_type (synergy), ==, LRG_SYNERGY_TYPE_CARD_TYPE);
    g_assert_cmpuint (lrg_synergy_get_min_count (synergy), ==, 5);
}

static void
test_synergy_new_tag (void)
{
    g_autoptr(LrgSynergy) synergy = NULL;

    synergy = lrg_synergy_new_tag ("fire-synergy", "Fire Synergy", "fire", 2);
    g_assert_nonnull (synergy);
    g_assert_cmpint (lrg_synergy_get_synergy_type (synergy), ==, LRG_SYNERGY_TYPE_TAG);
    g_assert_cmpuint (lrg_synergy_get_min_count (synergy), ==, 2);
}

static void
test_synergy_properties (void)
{
    g_autoptr(LrgSynergy) synergy = NULL;

    synergy = lrg_synergy_new ("prop-test", "Property Test", LRG_SYNERGY_TYPE_CUSTOM);

    /* Test min_count */
    g_assert_cmpuint (lrg_synergy_get_min_count (synergy), ==, 2);  /* default */
    lrg_synergy_set_min_count (synergy, 4);
    g_assert_cmpuint (lrg_synergy_get_min_count (synergy), ==, 4);

    /* Test bonus_per_card */
    g_assert_cmpint (lrg_synergy_get_bonus_per_card (synergy), ==, 1);  /* default */
    lrg_synergy_set_bonus_per_card (synergy, 5);
    g_assert_cmpint (lrg_synergy_get_bonus_per_card (synergy), ==, 5);

    /* Test description */
    g_assert_null (lrg_synergy_get_description (synergy));
    lrg_synergy_set_description (synergy, "Test description");
    g_assert_cmpstr (lrg_synergy_get_description (synergy), ==, "Test description");
}

static void
test_synergy_check_cards_empty (void)
{
    g_autoptr(LrgSynergy) synergy = NULL;
    g_autoptr(GPtrArray) cards = NULL;
    gboolean active;

    synergy = lrg_synergy_new ("empty-test", "Empty Test", LRG_SYNERGY_TYPE_CUSTOM);
    cards = g_ptr_array_new ();

    active = lrg_synergy_check_cards (synergy, cards);
    g_assert_false (active);

    active = lrg_synergy_check_cards (synergy, NULL);
    g_assert_false (active);
}

static void
test_synergy_get_synergy_cards_empty (void)
{
    g_autoptr(LrgSynergy) synergy = NULL;
    g_autoptr(GPtrArray) cards = NULL;
    GPtrArray *result;

    synergy = lrg_synergy_new ("empty-cards", "Empty Cards", LRG_SYNERGY_TYPE_CUSTOM);
    cards = g_ptr_array_new ();

    result = lrg_synergy_get_synergy_cards (synergy, cards);
    g_assert_nonnull (result);
    g_assert_cmpuint (result->len, ==, 0);
    g_ptr_array_unref (result);
}

/* ==========================================================================
 * Phase 5: Status Effect System Tests
 * ========================================================================== */

static void
test_status_effect_def_new (void)
{
    g_autoptr(LrgStatusEffectDef) def = NULL;

    def = lrg_status_effect_def_new ("strength", "Strength", LRG_STATUS_EFFECT_TYPE_BUFF);
    g_assert_nonnull (def);
    g_assert_cmpstr (lrg_status_effect_def_get_id (def), ==, "strength");
    g_assert_cmpstr (lrg_status_effect_def_get_name (def), ==, "Strength");
    g_assert_cmpint (lrg_status_effect_def_get_effect_type (def), ==, LRG_STATUS_EFFECT_TYPE_BUFF);
}

static void
test_status_effect_def_properties (void)
{
    g_autoptr(LrgStatusEffectDef) def = NULL;

    def = lrg_status_effect_def_new ("vulnerable", "Vulnerable", LRG_STATUS_EFFECT_TYPE_DEBUFF);

    /* Test description */
    g_assert_null (lrg_status_effect_def_get_description (def));
    lrg_status_effect_def_set_description (def, "Take 50% more damage");
    g_assert_cmpstr (lrg_status_effect_def_get_description (def), ==, "Take 50% more damage");

    /* Test icon */
    g_assert_null (lrg_status_effect_def_get_icon (def));
    lrg_status_effect_def_set_icon (def, "status-vulnerable");
    g_assert_cmpstr (lrg_status_effect_def_get_icon (def), ==, "status-vulnerable");

    /* Test stack behavior */
    g_assert_cmpint (lrg_status_effect_def_get_stack_behavior (def), ==, LRG_STATUS_STACK_INTENSITY);
    lrg_status_effect_def_set_stack_behavior (def, LRG_STATUS_STACK_DURATION);
    g_assert_cmpint (lrg_status_effect_def_get_stack_behavior (def), ==, LRG_STATUS_STACK_DURATION);

    /* Test max stacks */
    g_assert_cmpint (lrg_status_effect_def_get_max_stacks (def), ==, 0);  /* unlimited */
    lrg_status_effect_def_set_max_stacks (def, 999);
    g_assert_cmpint (lrg_status_effect_def_get_max_stacks (def), ==, 999);
}

static void
test_status_effect_def_flags (void)
{
    g_autoptr(LrgStatusEffectDef) buff = NULL;
    g_autoptr(LrgStatusEffectDef) debuff = NULL;

    buff = lrg_status_effect_def_new ("str", "Str", LRG_STATUS_EFFECT_TYPE_BUFF);
    debuff = lrg_status_effect_def_new ("weak", "Weak", LRG_STATUS_EFFECT_TYPE_DEBUFF);

    g_assert_true (lrg_status_effect_def_is_buff (buff));
    g_assert_false (lrg_status_effect_def_is_debuff (buff));

    g_assert_false (lrg_status_effect_def_is_buff (debuff));
    g_assert_true (lrg_status_effect_def_is_debuff (debuff));

    /* Test turn-end flags */
    g_assert_false (lrg_status_effect_def_clears_at_turn_end (buff));
    lrg_status_effect_def_set_clears_at_turn_end (buff, TRUE);
    g_assert_true (lrg_status_effect_def_clears_at_turn_end (buff));

    g_assert_false (lrg_status_effect_def_decrements_at_turn_end (debuff));
    lrg_status_effect_def_set_decrements_at_turn_end (debuff, TRUE);
    g_assert_true (lrg_status_effect_def_decrements_at_turn_end (debuff));

    /* Test permanent flag */
    g_assert_false (lrg_status_effect_def_is_permanent (buff));
    lrg_status_effect_def_set_permanent (buff, TRUE);
    g_assert_true (lrg_status_effect_def_is_permanent (buff));
}

static void
test_status_effect_def_tooltip (void)
{
    g_autoptr(LrgStatusEffectDef) def = NULL;
    g_autofree gchar *tooltip = NULL;

    def = lrg_status_effect_def_new ("poison", "Poison", LRG_STATUS_EFFECT_TYPE_DEBUFF);
    lrg_status_effect_def_set_description (def, "Take damage at end of turn");

    tooltip = lrg_status_effect_def_get_tooltip (def, 5);
    g_assert_nonnull (tooltip);
    /* Default implementation includes stacks */
    g_assert_true (g_str_has_suffix (tooltip, "(5)") || g_strstr_len (tooltip, -1, "5") != NULL);
}

static void
test_status_effect_instance_new (void)
{
    g_autoptr(LrgStatusEffectDef) def = NULL;
    LrgStatusEffectInstance *instance;

    def = lrg_status_effect_def_new ("strength", "Strength", LRG_STATUS_EFFECT_TYPE_BUFF);
    instance = lrg_status_effect_instance_new (def, 3);

    g_assert_nonnull (instance);
    g_assert_cmpint (lrg_status_effect_instance_get_stacks (instance), ==, 3);
    g_assert_true (lrg_status_effect_instance_get_def (instance) == def);
    g_assert_cmpstr (lrg_status_effect_instance_get_id (instance), ==, "strength");
    g_assert_cmpstr (lrg_status_effect_instance_get_name (instance), ==, "Strength");

    lrg_status_effect_instance_free (instance);
}

static void
test_status_effect_instance_stacks (void)
{
    g_autoptr(LrgStatusEffectDef) def = NULL;
    LrgStatusEffectInstance *instance;
    gint new_stacks;

    def = lrg_status_effect_def_new ("strength", "Strength", LRG_STATUS_EFFECT_TYPE_BUFF);
    instance = lrg_status_effect_instance_new (def, 5);

    /* Add stacks */
    new_stacks = lrg_status_effect_instance_add_stacks (instance, 3);
    g_assert_cmpint (new_stacks, ==, 8);
    g_assert_cmpint (lrg_status_effect_instance_get_stacks (instance), ==, 8);

    /* Remove stacks */
    new_stacks = lrg_status_effect_instance_remove_stacks (instance, 2);
    g_assert_cmpint (new_stacks, ==, 6);

    /* Set stacks directly */
    lrg_status_effect_instance_set_stacks (instance, 10);
    g_assert_cmpint (lrg_status_effect_instance_get_stacks (instance), ==, 10);

    lrg_status_effect_instance_free (instance);
}

static void
test_status_effect_instance_max_stacks (void)
{
    g_autoptr(LrgStatusEffectDef) def = NULL;
    LrgStatusEffectInstance *instance;

    def = lrg_status_effect_def_new ("artifact", "Artifact", LRG_STATUS_EFFECT_TYPE_BUFF);
    lrg_status_effect_def_set_max_stacks (def, 5);

    /* Creating with stacks over max should clamp */
    instance = lrg_status_effect_instance_new (def, 10);
    g_assert_cmpint (lrg_status_effect_instance_get_stacks (instance), ==, 5);

    /* Adding should respect max */
    lrg_status_effect_instance_add_stacks (instance, 10);
    g_assert_cmpint (lrg_status_effect_instance_get_stacks (instance), ==, 5);

    lrg_status_effect_instance_free (instance);
}

static void
test_status_effect_instance_expired (void)
{
    g_autoptr(LrgStatusEffectDef) def = NULL;
    LrgStatusEffectInstance *instance;

    def = lrg_status_effect_def_new ("vulnerable", "Vulnerable", LRG_STATUS_EFFECT_TYPE_DEBUFF);
    instance = lrg_status_effect_instance_new (def, 2);

    g_assert_false (lrg_status_effect_instance_is_expired (instance));

    lrg_status_effect_instance_remove_stacks (instance, 1);
    g_assert_false (lrg_status_effect_instance_is_expired (instance));

    lrg_status_effect_instance_remove_stacks (instance, 1);
    g_assert_true (lrg_status_effect_instance_is_expired (instance));

    lrg_status_effect_instance_free (instance);
}

static void
test_status_effect_instance_copy (void)
{
    g_autoptr(LrgStatusEffectDef) def = NULL;
    LrgStatusEffectInstance *original;
    LrgStatusEffectInstance *copy;

    def = lrg_status_effect_def_new ("strength", "Strength", LRG_STATUS_EFFECT_TYPE_BUFF);
    original = lrg_status_effect_instance_new (def, 5);
    copy = lrg_status_effect_instance_copy (original);

    g_assert_nonnull (copy);
    g_assert_cmpint (lrg_status_effect_instance_get_stacks (copy), ==, 5);
    g_assert_cmpstr (lrg_status_effect_instance_get_id (copy), ==, "strength");

    /* Modifying copy shouldn't affect original */
    lrg_status_effect_instance_add_stacks (copy, 3);
    g_assert_cmpint (lrg_status_effect_instance_get_stacks (copy), ==, 8);
    g_assert_cmpint (lrg_status_effect_instance_get_stacks (original), ==, 5);

    lrg_status_effect_instance_free (original);
    lrg_status_effect_instance_free (copy);
}

static void
test_status_effect_instance_convenience (void)
{
    g_autoptr(LrgStatusEffectDef) buff = NULL;
    g_autoptr(LrgStatusEffectDef) debuff = NULL;
    LrgStatusEffectInstance *buff_inst;
    LrgStatusEffectInstance *debuff_inst;
    g_autofree gchar *tooltip = NULL;

    buff = lrg_status_effect_def_new ("dex", "Dexterity", LRG_STATUS_EFFECT_TYPE_BUFF);
    debuff = lrg_status_effect_def_new ("frail", "Frail", LRG_STATUS_EFFECT_TYPE_DEBUFF);

    buff_inst = lrg_status_effect_instance_new (buff, 2);
    debuff_inst = lrg_status_effect_instance_new (debuff, 3);

    /* Test convenience accessors */
    g_assert_true (lrg_status_effect_instance_is_buff (buff_inst));
    g_assert_false (lrg_status_effect_instance_is_debuff (buff_inst));

    g_assert_false (lrg_status_effect_instance_is_buff (debuff_inst));
    g_assert_true (lrg_status_effect_instance_is_debuff (debuff_inst));

    g_assert_cmpint (lrg_status_effect_instance_get_effect_type (buff_inst), ==, LRG_STATUS_EFFECT_TYPE_BUFF);
    g_assert_cmpint (lrg_status_effect_instance_get_effect_type (debuff_inst), ==, LRG_STATUS_EFFECT_TYPE_DEBUFF);

    tooltip = lrg_status_effect_instance_get_tooltip (buff_inst);
    g_assert_nonnull (tooltip);

    lrg_status_effect_instance_free (buff_inst);
    lrg_status_effect_instance_free (debuff_inst);
}

static void
test_status_effect_registry_singleton (void)
{
    LrgStatusEffectRegistry *reg1;
    LrgStatusEffectRegistry *reg2;

    reg1 = lrg_status_effect_registry_get_default ();
    reg2 = lrg_status_effect_registry_get_default ();

    g_assert_nonnull (reg1);
    g_assert_true (reg1 == reg2);

    /* Clear for other tests */
    lrg_status_effect_registry_clear (reg1);
}

static void
test_status_effect_registry_register (void)
{
    LrgStatusEffectRegistry *registry;
    g_autoptr(LrgStatusEffectDef) def = NULL;
    gboolean success;

    registry = lrg_status_effect_registry_get_default ();
    lrg_status_effect_registry_clear (registry);

    def = lrg_status_effect_def_new ("test-status", "TestStatus", LRG_STATUS_EFFECT_TYPE_BUFF);

    /* Register should succeed */
    success = lrg_status_effect_registry_register (registry, def);
    g_assert_true (success);
    g_assert_true (lrg_status_effect_registry_is_registered (registry, "test-status"));
    g_assert_cmpuint (lrg_status_effect_registry_get_count (registry), ==, 1);

    /* Duplicate registration should fail */
    success = lrg_status_effect_registry_register (registry, def);
    g_assert_false (success);

    lrg_status_effect_registry_clear (registry);
}

static void
test_status_effect_registry_lookup (void)
{
    LrgStatusEffectRegistry *registry;
    g_autoptr(LrgStatusEffectDef) def = NULL;
    LrgStatusEffectDef *found;

    registry = lrg_status_effect_registry_get_default ();
    lrg_status_effect_registry_clear (registry);

    def = lrg_status_effect_def_new ("lookup-status", "LookupStatus", LRG_STATUS_EFFECT_TYPE_DEBUFF);
    lrg_status_effect_registry_register (registry, def);

    found = lrg_status_effect_registry_lookup (registry, "lookup-status");
    g_assert_nonnull (found);
    g_assert_cmpstr (lrg_status_effect_def_get_id (found), ==, "lookup-status");

    /* Not found */
    found = lrg_status_effect_registry_lookup (registry, "nonexistent");
    g_assert_null (found);

    lrg_status_effect_registry_clear (registry);
}

static void
test_status_effect_registry_unregister (void)
{
    LrgStatusEffectRegistry *registry;
    g_autoptr(LrgStatusEffectDef) def = NULL;
    gboolean success;

    registry = lrg_status_effect_registry_get_default ();
    lrg_status_effect_registry_clear (registry);

    def = lrg_status_effect_def_new ("unreg-status", "UnregStatus", LRG_STATUS_EFFECT_TYPE_NEUTRAL);
    lrg_status_effect_registry_register (registry, def);
    g_assert_true (lrg_status_effect_registry_is_registered (registry, "unreg-status"));

    success = lrg_status_effect_registry_unregister (registry, "unreg-status");
    g_assert_true (success);
    g_assert_false (lrg_status_effect_registry_is_registered (registry, "unreg-status"));

    /* Unregister again should fail */
    success = lrg_status_effect_registry_unregister (registry, "unreg-status");
    g_assert_false (success);

    lrg_status_effect_registry_clear (registry);
}

static void
test_status_effect_registry_create_instance (void)
{
    LrgStatusEffectRegistry *registry;
    g_autoptr(LrgStatusEffectDef) def = NULL;
    LrgStatusEffectInstance *instance;

    registry = lrg_status_effect_registry_get_default ();
    lrg_status_effect_registry_clear (registry);

    def = lrg_status_effect_def_new ("create-test", "CreateTest", LRG_STATUS_EFFECT_TYPE_BUFF);
    lrg_status_effect_registry_register (registry, def);

    instance = lrg_status_effect_registry_create_instance (registry, "create-test", 5);
    g_assert_nonnull (instance);
    g_assert_cmpint (lrg_status_effect_instance_get_stacks (instance), ==, 5);
    g_assert_cmpstr (lrg_status_effect_instance_get_id (instance), ==, "create-test");

    lrg_status_effect_instance_free (instance);

    /* Create from unknown ID should fail */
    instance = lrg_status_effect_registry_create_instance (registry, "unknown", 1);
    g_assert_null (instance);

    lrg_status_effect_registry_clear (registry);
}

static void
test_status_effect_registry_get_buffs_debuffs (void)
{
    LrgStatusEffectRegistry *registry;
    g_autoptr(LrgStatusEffectDef) buff1 = NULL;
    g_autoptr(LrgStatusEffectDef) buff2 = NULL;
    g_autoptr(LrgStatusEffectDef) debuff1 = NULL;
    GList *buffs;
    GList *debuffs;

    registry = lrg_status_effect_registry_get_default ();
    lrg_status_effect_registry_clear (registry);

    buff1 = lrg_status_effect_def_new ("strength", "Strength", LRG_STATUS_EFFECT_TYPE_BUFF);
    buff2 = lrg_status_effect_def_new ("dexterity", "Dexterity", LRG_STATUS_EFFECT_TYPE_BUFF);
    debuff1 = lrg_status_effect_def_new ("vulnerable", "Vulnerable", LRG_STATUS_EFFECT_TYPE_DEBUFF);

    lrg_status_effect_registry_register (registry, buff1);
    lrg_status_effect_registry_register (registry, buff2);
    lrg_status_effect_registry_register (registry, debuff1);

    buffs = lrg_status_effect_registry_get_buffs (registry);
    g_assert_cmpuint (g_list_length (buffs), ==, 2);
    g_list_free (buffs);

    debuffs = lrg_status_effect_registry_get_debuffs (registry);
    g_assert_cmpuint (g_list_length (debuffs), ==, 1);
    g_list_free (debuffs);

    lrg_status_effect_registry_clear (registry);
}

/* ==========================================================================
 * Phase 5.5: Relic & Potion System Tests
 * ========================================================================== */

static void
test_relic_def_new (void)
{
    g_autoptr(LrgRelicDef) def = NULL;

    def = lrg_relic_def_new ("burning-blood", "Burning Blood");

    g_assert_nonnull (def);
    g_assert_cmpstr (lrg_relic_def_get_id (def), ==, "burning-blood");
    g_assert_cmpstr (lrg_relic_def_get_name (def), ==, "Burning Blood");
    g_assert_cmpint (lrg_relic_def_get_rarity (def), ==, LRG_RELIC_RARITY_COMMON);
}

static void
test_relic_def_properties (void)
{
    g_autoptr(LrgRelicDef) def = NULL;

    def = lrg_relic_def_new ("vajra", "Vajra");

    lrg_relic_def_set_description (def, "Start each combat with 1 Strength.");
    lrg_relic_def_set_flavor_text (def, "A symbol of power.");
    lrg_relic_def_set_rarity (def, LRG_RELIC_RARITY_STARTER);
    lrg_relic_def_set_triggers (def, LRG_RELIC_TRIGGER_COMBAT_START);
    lrg_relic_def_set_counter_max (def, 3);
    lrg_relic_def_set_unique (def, TRUE);
    lrg_relic_def_set_price (def, 250);

    g_assert_cmpstr (lrg_relic_def_get_description (def), ==, "Start each combat with 1 Strength.");
    g_assert_cmpstr (lrg_relic_def_get_flavor_text (def), ==, "A symbol of power.");
    g_assert_cmpint (lrg_relic_def_get_rarity (def), ==, LRG_RELIC_RARITY_STARTER);
    g_assert_cmpint (lrg_relic_def_get_triggers (def), ==, LRG_RELIC_TRIGGER_COMBAT_START);
    g_assert_cmpint (lrg_relic_def_get_counter_max (def), ==, 3);
    g_assert_true (lrg_relic_def_get_unique (def));
    g_assert_cmpint (lrg_relic_def_get_price (def), ==, 250);
}

static void
test_relic_def_trigger_flags (void)
{
    g_autoptr(LrgRelicDef) def = NULL;
    LrgRelicTrigger triggers;

    def = lrg_relic_def_new ("test-relic", "Test Relic");

    triggers = LRG_RELIC_TRIGGER_COMBAT_START | LRG_RELIC_TRIGGER_TURN_START;
    lrg_relic_def_set_triggers (def, triggers);

    g_assert_true (lrg_relic_def_has_trigger (def, LRG_RELIC_TRIGGER_COMBAT_START));
    g_assert_true (lrg_relic_def_has_trigger (def, LRG_RELIC_TRIGGER_TURN_START));
    g_assert_false (lrg_relic_def_has_trigger (def, LRG_RELIC_TRIGGER_COMBAT_END));
    g_assert_false (lrg_relic_def_has_trigger (def, LRG_RELIC_TRIGGER_ON_CARD_PLAYED));
}

static void
test_relic_instance_new (void)
{
    g_autoptr(LrgRelicDef) def = NULL;
    g_autoptr(LrgRelicInstance) instance = NULL;

    def = lrg_relic_def_new ("burning-blood", "Burning Blood");
    instance = lrg_relic_instance_new (def);

    g_assert_nonnull (instance);
    g_assert_true (LRG_IS_RELIC_INSTANCE (instance));
    g_assert_true (lrg_relic_instance_get_def (instance) == def);
    g_assert_true (lrg_relic_instance_get_enabled (instance));
    g_assert_cmpint (lrg_relic_instance_get_counter (instance), ==, 0);
}

static void
test_relic_instance_counter (void)
{
    g_autoptr(LrgRelicDef) def = NULL;
    g_autoptr(LrgRelicInstance) instance = NULL;

    def = lrg_relic_def_new ("ink-bottle", "Ink Bottle");
    lrg_relic_def_set_counter_max (def, 10);

    instance = lrg_relic_instance_new (def);

    g_assert_cmpint (lrg_relic_instance_get_counter (instance), ==, 0);

    lrg_relic_instance_set_counter (instance, 5);
    g_assert_cmpint (lrg_relic_instance_get_counter (instance), ==, 5);

    lrg_relic_instance_increment_counter (instance);
    g_assert_cmpint (lrg_relic_instance_get_counter (instance), ==, 6);

    /* Counter should reset after reaching max */
    lrg_relic_instance_set_counter (instance, 9);
    lrg_relic_instance_increment_counter (instance);
    g_assert_cmpint (lrg_relic_instance_get_counter (instance), ==, 0);
}

static void
test_relic_instance_enabled (void)
{
    g_autoptr(LrgRelicDef) def = NULL;
    g_autoptr(LrgRelicInstance) instance = NULL;

    def = lrg_relic_def_new ("test-relic", "Test Relic");
    instance = lrg_relic_instance_new (def);

    g_assert_true (lrg_relic_instance_get_enabled (instance));

    lrg_relic_instance_set_enabled (instance, FALSE);
    g_assert_false (lrg_relic_instance_get_enabled (instance));

    lrg_relic_instance_set_enabled (instance, TRUE);
    g_assert_true (lrg_relic_instance_get_enabled (instance));
}

static void
test_relic_instance_data (void)
{
    g_autoptr(LrgRelicDef) def = NULL;
    g_autoptr(LrgRelicInstance) instance = NULL;
    gint *value;
    gint *retrieved;

    def = lrg_relic_def_new ("test-relic", "Test Relic");
    instance = lrg_relic_instance_new (def);

    value = g_new0 (gint, 1);
    *value = 42;

    lrg_relic_instance_set_data (instance, "test-key", value, g_free);

    retrieved = lrg_relic_instance_get_data (instance, "test-key");
    g_assert_nonnull (retrieved);
    g_assert_cmpint (*retrieved, ==, 42);

    g_assert_null (lrg_relic_instance_get_data (instance, "nonexistent"));
}

static void
test_relic_registry_singleton (void)
{
    LrgRelicRegistry *reg1;
    LrgRelicRegistry *reg2;

    reg1 = lrg_relic_registry_get_default ();
    reg2 = lrg_relic_registry_get_default ();

    g_assert_nonnull (reg1);
    g_assert_true (reg1 == reg2);
}

static void
test_relic_registry_register (void)
{
    LrgRelicRegistry *registry;
    g_autoptr(LrgRelicDef) def = NULL;
    gboolean result;

    registry = lrg_relic_registry_get_default ();
    lrg_relic_registry_clear (registry);

    def = lrg_relic_def_new ("burning-blood", "Burning Blood");
    result = lrg_relic_registry_register (registry, def);

    g_assert_true (result);
    g_assert_true (lrg_relic_registry_is_registered (registry, "burning-blood"));

    /* Duplicate registration should fail */
    result = lrg_relic_registry_register (registry, def);
    g_assert_false (result);

    lrg_relic_registry_clear (registry);
}

static void
test_relic_registry_lookup (void)
{
    LrgRelicRegistry *registry;
    g_autoptr(LrgRelicDef) def = NULL;
    LrgRelicDef *found;

    registry = lrg_relic_registry_get_default ();
    lrg_relic_registry_clear (registry);

    def = lrg_relic_def_new ("vajra", "Vajra");
    lrg_relic_registry_register (registry, def);

    found = lrg_relic_registry_lookup (registry, "vajra");
    g_assert_nonnull (found);
    g_assert_true (found == def);

    found = lrg_relic_registry_lookup (registry, "nonexistent");
    g_assert_null (found);

    lrg_relic_registry_clear (registry);
}

static void
test_relic_registry_create_instance (void)
{
    LrgRelicRegistry *registry;
    g_autoptr(LrgRelicDef) def = NULL;
    g_autoptr(LrgRelicInstance) instance = NULL;
    g_autoptr(LrgRelicInstance) invalid = NULL;

    registry = lrg_relic_registry_get_default ();
    lrg_relic_registry_clear (registry);

    def = lrg_relic_def_new ("burning-blood", "Burning Blood");
    lrg_relic_registry_register (registry, def);

    instance = lrg_relic_registry_create_instance (registry, "burning-blood");
    g_assert_nonnull (instance);
    g_assert_true (lrg_relic_instance_get_def (instance) == def);

    invalid = lrg_relic_registry_create_instance (registry, "nonexistent");
    g_assert_null (invalid);

    lrg_relic_registry_clear (registry);
}

static void
test_relic_registry_get_by_rarity (void)
{
    LrgRelicRegistry *registry;
    g_autoptr(LrgRelicDef) common1 = NULL;
    g_autoptr(LrgRelicDef) common2 = NULL;
    g_autoptr(LrgRelicDef) rare1 = NULL;
    GList *common_relics;

    registry = lrg_relic_registry_get_default ();
    lrg_relic_registry_clear (registry);

    common1 = lrg_relic_def_new ("relic1", "Relic 1");
    lrg_relic_def_set_rarity (common1, LRG_RELIC_RARITY_COMMON);

    common2 = lrg_relic_def_new ("relic2", "Relic 2");
    lrg_relic_def_set_rarity (common2, LRG_RELIC_RARITY_COMMON);

    rare1 = lrg_relic_def_new ("relic3", "Relic 3");
    lrg_relic_def_set_rarity (rare1, LRG_RELIC_RARITY_RARE);

    lrg_relic_registry_register (registry, common1);
    lrg_relic_registry_register (registry, common2);
    lrg_relic_registry_register (registry, rare1);

    common_relics = lrg_relic_registry_get_by_rarity (registry, LRG_RELIC_RARITY_COMMON);
    g_assert_cmpuint (g_list_length (common_relics), ==, 2);
    g_list_free (common_relics);

    lrg_relic_registry_clear (registry);
}

static void
test_potion_def_new (void)
{
    g_autoptr(LrgPotionDef) def = NULL;

    def = lrg_potion_def_new ("health-potion", "Health Potion");

    g_assert_nonnull (def);
    g_assert_cmpstr (lrg_potion_def_get_id (def), ==, "health-potion");
    g_assert_cmpstr (lrg_potion_def_get_name (def), ==, "Health Potion");
    g_assert_cmpint (lrg_potion_def_get_rarity (def), ==, LRG_POTION_RARITY_COMMON);
}

static void
test_potion_def_properties (void)
{
    g_autoptr(LrgPotionDef) def = NULL;

    def = lrg_potion_def_new ("fire-potion", "Fire Potion");

    lrg_potion_def_set_description (def, "Deal 20 damage to ALL enemies.");
    lrg_potion_def_set_rarity (def, LRG_POTION_RARITY_UNCOMMON);
    lrg_potion_def_set_target_type (def, LRG_POTION_TARGET_ALL_ENEMIES);
    lrg_potion_def_set_potency (def, 20);
    lrg_potion_def_set_combat_only (def, TRUE);
    lrg_potion_def_set_price (def, 50);

    g_assert_cmpstr (lrg_potion_def_get_description (def), ==, "Deal 20 damage to ALL enemies.");
    g_assert_cmpint (lrg_potion_def_get_rarity (def), ==, LRG_POTION_RARITY_UNCOMMON);
    g_assert_cmpint (lrg_potion_def_get_target_type (def), ==, LRG_POTION_TARGET_ALL_ENEMIES);
    g_assert_cmpint (lrg_potion_def_get_potency (def), ==, 20);
    g_assert_true (lrg_potion_def_get_combat_only (def));
    g_assert_cmpint (lrg_potion_def_get_price (def), ==, 50);
}

static void
test_potion_instance_new (void)
{
    g_autoptr(LrgPotionDef) def = NULL;
    g_autoptr(LrgPotionInstance) instance = NULL;

    def = lrg_potion_def_new ("health-potion", "Health Potion");
    instance = lrg_potion_instance_new (def);

    g_assert_nonnull (instance);
    g_assert_true (LRG_IS_POTION_INSTANCE (instance));
    g_assert_true (lrg_potion_instance_get_def (instance) == def);
    g_assert_false (lrg_potion_instance_is_consumed (instance));
}

static void
test_potion_instance_use (void)
{
    g_autoptr(LrgPotionDef) def = NULL;
    g_autoptr(LrgPotionInstance) instance = NULL;
    gboolean result;

    def = lrg_potion_def_new ("health-potion", "Health Potion");
    instance = lrg_potion_instance_new (def);

    g_assert_false (lrg_potion_instance_is_consumed (instance));
    g_assert_true (lrg_potion_instance_can_use (instance, NULL));

    result = lrg_potion_instance_use (instance, NULL, NULL);
    g_assert_true (result);
    g_assert_true (lrg_potion_instance_is_consumed (instance));

    /* Cannot use again after consumed */
    g_assert_false (lrg_potion_instance_can_use (instance, NULL));
    result = lrg_potion_instance_use (instance, NULL, NULL);
    g_assert_false (result);
}

static void
test_potion_instance_discard (void)
{
    g_autoptr(LrgPotionDef) def = NULL;
    g_autoptr(LrgPotionInstance) instance = NULL;

    def = lrg_potion_def_new ("block-potion", "Block Potion");
    instance = lrg_potion_instance_new (def);

    g_assert_false (lrg_potion_instance_is_consumed (instance));

    lrg_potion_instance_discard (instance);
    g_assert_true (lrg_potion_instance_is_consumed (instance));

    /* Discarding again does nothing (already consumed) */
    lrg_potion_instance_discard (instance);
    g_assert_true (lrg_potion_instance_is_consumed (instance));
}

static void
test_potion_instance_convenience (void)
{
    g_autoptr(LrgPotionDef) def = NULL;
    g_autoptr(LrgPotionInstance) instance = NULL;

    def = lrg_potion_def_new ("fire-potion", "Fire Potion");
    instance = lrg_potion_instance_new (def);

    g_assert_cmpstr (lrg_potion_instance_get_id (instance), ==, "fire-potion");
    g_assert_cmpstr (lrg_potion_instance_get_name (instance), ==, "Fire Potion");
}

/* ==========================================================================
 * Phase 6: Combat System Tests
 * ========================================================================== */

/* --------------------------------------------------------------------------
 * LrgEnemyIntent Tests
 * -------------------------------------------------------------------------- */

static void
test_enemy_intent_new_attack (void)
{
    g_autoptr(LrgEnemyIntent) intent = NULL;

    intent = lrg_enemy_intent_new_attack (12, 1);

    g_assert_nonnull (intent);
    g_assert_cmpint (lrg_enemy_intent_get_intent_type (intent), ==, LRG_INTENT_ATTACK);
    g_assert_cmpint (lrg_enemy_intent_get_damage (intent), ==, 12);
    g_assert_cmpint (lrg_enemy_intent_get_times (intent), ==, 1);
    g_assert_cmpint (lrg_enemy_intent_get_block (intent), ==, 0);
}

static void
test_enemy_intent_new_attack_multi (void)
{
    g_autoptr(LrgEnemyIntent) intent = NULL;

    intent = lrg_enemy_intent_new_attack (5, 3);

    g_assert_nonnull (intent);
    g_assert_cmpint (lrg_enemy_intent_get_intent_type (intent), ==, LRG_INTENT_ATTACK);
    g_assert_cmpint (lrg_enemy_intent_get_damage (intent), ==, 5);
    g_assert_cmpint (lrg_enemy_intent_get_times (intent), ==, 3);
}

static void
test_enemy_intent_new_defend (void)
{
    g_autoptr(LrgEnemyIntent) intent = NULL;

    intent = lrg_enemy_intent_new_defend (8);

    g_assert_nonnull (intent);
    g_assert_cmpint (lrg_enemy_intent_get_intent_type (intent), ==, LRG_INTENT_DEFEND);
    g_assert_cmpint (lrg_enemy_intent_get_block (intent), ==, 8);
    g_assert_cmpint (lrg_enemy_intent_get_damage (intent), ==, 0);
}

static void
test_enemy_intent_new_buff (void)
{
    g_autoptr(LrgEnemyIntent) intent = NULL;

    intent = lrg_enemy_intent_new_buff ("strength", 2);

    g_assert_nonnull (intent);
    g_assert_cmpint (lrg_enemy_intent_get_intent_type (intent), ==, LRG_INTENT_BUFF);
    g_assert_cmpstr (lrg_enemy_intent_get_status_id (intent), ==, "strength");
    g_assert_cmpint (lrg_enemy_intent_get_stacks (intent), ==, 2);
}

static void
test_enemy_intent_new_debuff (void)
{
    g_autoptr(LrgEnemyIntent) intent = NULL;

    intent = lrg_enemy_intent_new_debuff ("vulnerable", 2);

    g_assert_nonnull (intent);
    g_assert_cmpint (lrg_enemy_intent_get_intent_type (intent), ==, LRG_INTENT_DEBUFF);
    g_assert_cmpstr (lrg_enemy_intent_get_status_id (intent), ==, "vulnerable");
    g_assert_cmpint (lrg_enemy_intent_get_stacks (intent), ==, 2);
}

static void
test_enemy_intent_copy (void)
{
    g_autoptr(LrgEnemyIntent) original = NULL;
    g_autoptr(LrgEnemyIntent) copy = NULL;

    original = lrg_enemy_intent_new_attack (10, 2);
    copy = lrg_enemy_intent_copy (original);

    g_assert_nonnull (copy);
    g_assert_true (original != copy);
    g_assert_cmpint (lrg_enemy_intent_get_intent_type (copy), ==, LRG_INTENT_ATTACK);
    g_assert_cmpint (lrg_enemy_intent_get_damage (copy), ==, 10);
    g_assert_cmpint (lrg_enemy_intent_get_times (copy), ==, 2);
}

/* --------------------------------------------------------------------------
 * LrgEnemyDef Tests
 * -------------------------------------------------------------------------- */

static void
test_enemy_def_new (void)
{
    g_autoptr(LrgEnemyDef) def = NULL;

    def = lrg_enemy_def_new ("slime", "Acid Slime");

    g_assert_nonnull (def);
    g_assert_true (LRG_IS_ENEMY_DEF (def));
    g_assert_cmpstr (lrg_enemy_def_get_id (def), ==, "slime");
    g_assert_cmpstr (lrg_enemy_def_get_name (def), ==, "Acid Slime");
    g_assert_cmpint (lrg_enemy_def_get_enemy_type (def), ==, LRG_ENEMY_TYPE_NORMAL);
}

static void
test_enemy_def_properties (void)
{
    g_autoptr(LrgEnemyDef) def = NULL;

    def = lrg_enemy_def_new ("gremlin-nob", "Gremlin Nob");

    lrg_enemy_def_set_description (def, "A large, angry gremlin.");
    lrg_enemy_def_set_enemy_type (def, LRG_ENEMY_TYPE_ELITE);
    lrg_enemy_def_set_base_health (def, 82);
    lrg_enemy_def_set_health_variance (def, 4);

    g_assert_cmpstr (lrg_enemy_def_get_description (def), ==, "A large, angry gremlin.");
    g_assert_cmpint (lrg_enemy_def_get_enemy_type (def), ==, LRG_ENEMY_TYPE_ELITE);
    g_assert_cmpint (lrg_enemy_def_get_base_health (def), ==, 82);
    g_assert_cmpint (lrg_enemy_def_get_health_variance (def), ==, 4);
}

static void
test_enemy_def_intent_patterns (void)
{
    g_autoptr(LrgEnemyDef) def = NULL;
    LrgEnemyIntent *attack;
    LrgEnemyIntent *defend;

    def = lrg_enemy_def_new ("slime", "Slime");

    attack = lrg_enemy_intent_new_attack (8, 1);
    defend = lrg_enemy_intent_new_defend (5);

    /* Note: add_intent_pattern takes ownership (transfer full) */
    lrg_enemy_def_add_intent_pattern (def, attack, 60);
    lrg_enemy_def_add_intent_pattern (def, defend, 40);

    /* Patterns added successfully - tested via decide_intent in instance tests */
}

/* --------------------------------------------------------------------------
 * LrgEnemyInstance Tests
 * -------------------------------------------------------------------------- */

static void
test_enemy_instance_new (void)
{
    g_autoptr(LrgEnemyDef) def = NULL;
    g_autoptr(LrgEnemyInstance) instance = NULL;

    def = lrg_enemy_def_new ("slime", "Acid Slime");
    lrg_enemy_def_set_base_health (def, 50);

    instance = lrg_enemy_instance_new (def);

    g_assert_nonnull (instance);
    g_assert_true (LRG_IS_ENEMY_INSTANCE (instance));
    g_assert_true (lrg_enemy_instance_get_def (instance) == def);
    g_assert_cmpint (lrg_combatant_get_max_health (LRG_COMBATANT (instance)), ==, 50);
    g_assert_cmpint (lrg_combatant_get_current_health (LRG_COMBATANT (instance)), ==, 50);
}

static void
test_enemy_instance_combatant_interface (void)
{
    g_autoptr(LrgEnemyDef) def = NULL;
    g_autoptr(LrgEnemyInstance) instance = NULL;
    LrgCombatant *combatant;

    def = lrg_enemy_def_new ("cultist", "Cultist");
    lrg_enemy_def_set_base_health (def, 48);

    instance = lrg_enemy_instance_new (def);
    combatant = LRG_COMBATANT (instance);

    g_assert_true (LRG_IS_COMBATANT (instance));
    g_assert_cmpstr (lrg_combatant_get_name (combatant), ==, "Cultist");
    g_assert_cmpint (lrg_combatant_get_max_health (combatant), ==, 48);
    g_assert_cmpint (lrg_combatant_get_current_health (combatant), ==, 48);
    g_assert_cmpint (lrg_combatant_get_block (combatant), ==, 0);
    g_assert_true (lrg_combatant_is_alive (combatant));
}

static void
test_enemy_instance_take_damage (void)
{
    g_autoptr(LrgEnemyDef) def = NULL;
    g_autoptr(LrgEnemyInstance) instance = NULL;
    LrgCombatant *combatant;
    gint actual;

    def = lrg_enemy_def_new ("slime", "Slime");
    lrg_enemy_def_set_base_health (def, 30);

    instance = lrg_enemy_instance_new (def);
    combatant = LRG_COMBATANT (instance);

    actual = lrg_combatant_take_damage (combatant, 10, LRG_EFFECT_FLAG_NONE);

    g_assert_cmpint (actual, ==, 10);
    g_assert_cmpint (lrg_combatant_get_current_health (combatant), ==, 20);
    g_assert_true (lrg_combatant_is_alive (combatant));
}

static void
test_enemy_instance_take_damage_with_block (void)
{
    g_autoptr(LrgEnemyDef) def = NULL;
    g_autoptr(LrgEnemyInstance) instance = NULL;
    LrgCombatant *combatant;
    gint actual;

    def = lrg_enemy_def_new ("slime", "Slime");
    lrg_enemy_def_set_base_health (def, 30);

    instance = lrg_enemy_instance_new (def);
    combatant = LRG_COMBATANT (instance);

    lrg_combatant_add_block (combatant, 15);
    g_assert_cmpint (lrg_combatant_get_block (combatant), ==, 15);

    /* 20 damage, 15 block -> 15 absorbed, 5 hp damage */
    actual = lrg_combatant_take_damage (combatant, 20, LRG_EFFECT_FLAG_NONE);

    g_assert_cmpint (actual, ==, 5);
    g_assert_cmpint (lrg_combatant_get_block (combatant), ==, 0);
    g_assert_cmpint (lrg_combatant_get_current_health (combatant), ==, 25);
}

static void
test_enemy_instance_heal (void)
{
    g_autoptr(LrgEnemyDef) def = NULL;
    g_autoptr(LrgEnemyInstance) instance = NULL;
    LrgCombatant *combatant;

    def = lrg_enemy_def_new ("slime", "Slime");
    lrg_enemy_def_set_base_health (def, 50);

    instance = lrg_enemy_instance_new (def);
    combatant = LRG_COMBATANT (instance);

    /* Take some damage first */
    lrg_combatant_take_damage (combatant, 30, LRG_EFFECT_FLAG_NONE);
    g_assert_cmpint (lrg_combatant_get_current_health (combatant), ==, 20);

    /* Heal 15 */
    lrg_combatant_heal (combatant, 15);
    g_assert_cmpint (lrg_combatant_get_current_health (combatant), ==, 35);

    /* Heal more than max - capped to max */
    lrg_combatant_heal (combatant, 100);
    g_assert_cmpint (lrg_combatant_get_current_health (combatant), ==, 50);
}

static void
test_enemy_instance_death (void)
{
    g_autoptr(LrgEnemyDef) def = NULL;
    g_autoptr(LrgEnemyInstance) instance = NULL;
    LrgCombatant *combatant;

    def = lrg_enemy_def_new ("slime", "Slime");
    lrg_enemy_def_set_base_health (def, 20);

    instance = lrg_enemy_instance_new (def);
    combatant = LRG_COMBATANT (instance);

    g_assert_true (lrg_combatant_is_alive (combatant));

    lrg_combatant_take_damage (combatant, 25, LRG_EFFECT_FLAG_NONE);

    g_assert_cmpint (lrg_combatant_get_current_health (combatant), ==, 0);
    g_assert_false (lrg_combatant_is_alive (combatant));
}

static void
test_enemy_instance_intent (void)
{
    g_autoptr(LrgEnemyDef) def = NULL;
    g_autoptr(LrgEnemyInstance) instance = NULL;
    LrgEnemyIntent *intent;
    const LrgEnemyIntent *current;

    def = lrg_enemy_def_new ("slime", "Slime");
    instance = lrg_enemy_instance_new (def);

    intent = lrg_enemy_intent_new_attack (10, 1);
    /* Note: set_intent takes ownership (transfer full) */
    lrg_enemy_instance_set_intent (instance, intent);

    current = lrg_enemy_instance_get_intent (instance);
    g_assert_nonnull (current);
    g_assert_cmpint (lrg_enemy_intent_get_intent_type (current), ==, LRG_INTENT_ATTACK);
    g_assert_cmpint (lrg_enemy_intent_get_damage (current), ==, 10);
}

/* --------------------------------------------------------------------------
 * LrgPlayerCombatant Tests
 * -------------------------------------------------------------------------- */

static void
test_player_combatant_new (void)
{
    g_autoptr(LrgPlayerCombatant) player = NULL;

    player = lrg_player_combatant_new ("ironclad", "The Ironclad", 80);

    g_assert_nonnull (player);
    g_assert_true (LRG_IS_PLAYER_COMBATANT (player));
    g_assert_true (LRG_IS_COMBATANT (player));
    g_assert_cmpstr (lrg_combatant_get_id (LRG_COMBATANT (player)), ==, "ironclad");
    g_assert_cmpstr (lrg_combatant_get_name (LRG_COMBATANT (player)), ==, "The Ironclad");
    g_assert_cmpint (lrg_combatant_get_max_health (LRG_COMBATANT (player)), ==, 80);
    g_assert_cmpint (lrg_combatant_get_current_health (LRG_COMBATANT (player)), ==, 80);
}

static void
test_player_combatant_interface (void)
{
    g_autoptr(LrgPlayerCombatant) player = NULL;
    LrgCombatant *combatant;

    player = lrg_player_combatant_new ("silent", "The Silent", 70);
    combatant = LRG_COMBATANT (player);

    g_assert_cmpstr (lrg_combatant_get_name (combatant), ==, "The Silent");
    g_assert_cmpint (lrg_combatant_get_max_health (combatant), ==, 70);
    g_assert_cmpint (lrg_combatant_get_current_health (combatant), ==, 70);
    g_assert_cmpint (lrg_combatant_get_block (combatant), ==, 0);
    g_assert_true (lrg_combatant_is_alive (combatant));
}

static void
test_player_combatant_damage_and_block (void)
{
    g_autoptr(LrgPlayerCombatant) player = NULL;
    LrgCombatant *combatant;
    gint actual;

    player = lrg_player_combatant_new ("ironclad", "Ironclad", 80);
    combatant = LRG_COMBATANT (player);

    /* Add block */
    lrg_combatant_add_block (combatant, 20);
    g_assert_cmpint (lrg_combatant_get_block (combatant), ==, 20);

    /* Take damage (absorbed by block) */
    actual = lrg_combatant_take_damage (combatant, 15, LRG_EFFECT_FLAG_NONE);
    g_assert_cmpint (actual, ==, 0);
    g_assert_cmpint (lrg_combatant_get_block (combatant), ==, 5);
    g_assert_cmpint (lrg_combatant_get_current_health (combatant), ==, 80);

    /* Take more damage (exceeds remaining block) */
    actual = lrg_combatant_take_damage (combatant, 25, LRG_EFFECT_FLAG_NONE);
    g_assert_cmpint (actual, ==, 20);
    g_assert_cmpint (lrg_combatant_get_block (combatant), ==, 0);
    g_assert_cmpint (lrg_combatant_get_current_health (combatant), ==, 60);
}

static void
test_player_combatant_gold (void)
{
    g_autoptr(LrgPlayerCombatant) player = NULL;

    player = lrg_player_combatant_new ("ironclad", "Ironclad", 80);

    g_assert_cmpint (lrg_player_combatant_get_gold (player), ==, 0);

    lrg_player_combatant_set_gold (player, 100);
    g_assert_cmpint (lrg_player_combatant_get_gold (player), ==, 100);

    lrg_player_combatant_add_gold (player, 50);
    g_assert_cmpint (lrg_player_combatant_get_gold (player), ==, 150);

    g_assert_true (lrg_player_combatant_remove_gold (player, 75));
    g_assert_cmpint (lrg_player_combatant_get_gold (player), ==, 75);

    /* Cannot remove more than available */
    g_assert_false (lrg_player_combatant_remove_gold (player, 100));
    g_assert_cmpint (lrg_player_combatant_get_gold (player), ==, 75);
}

/* --------------------------------------------------------------------------
 * LrgCombatContext Tests
 * -------------------------------------------------------------------------- */

static void
test_combat_context_new (void)
{
    g_autoptr(LrgPlayerCombatant) player = NULL;
    g_autoptr(LrgCombatContext) ctx = NULL;

    player = lrg_player_combatant_new ("ironclad", "Ironclad", 80);
    ctx = lrg_combat_context_new (player, NULL);

    g_assert_nonnull (ctx);
    g_assert_true (LRG_IS_COMBAT_CONTEXT (ctx));
    g_assert_true (lrg_combat_context_get_player (ctx) == player);
    g_assert_cmpint (lrg_combat_context_get_turn (ctx), ==, 0);
    g_assert_cmpint (lrg_combat_context_get_phase (ctx), ==, LRG_COMBAT_PHASE_SETUP);
}

static void
test_combat_context_energy (void)
{
    g_autoptr(LrgPlayerCombatant) player = NULL;
    g_autoptr(LrgCombatContext) ctx = NULL;

    player = lrg_player_combatant_new ("ironclad", "Ironclad", 80);
    ctx = lrg_combat_context_new (player, NULL);

    g_assert_cmpint (lrg_combat_context_get_energy (ctx), ==, 0);

    lrg_combat_context_set_energy (ctx, 3);
    g_assert_cmpint (lrg_combat_context_get_energy (ctx), ==, 3);

    lrg_combat_context_add_energy (ctx, 2);
    g_assert_cmpint (lrg_combat_context_get_energy (ctx), ==, 5);

    g_assert_true (lrg_combat_context_spend_energy (ctx, 3));
    g_assert_cmpint (lrg_combat_context_get_energy (ctx), ==, 2);

    g_assert_false (lrg_combat_context_spend_energy (ctx, 5));
    g_assert_cmpint (lrg_combat_context_get_energy (ctx), ==, 2);
}

static void
test_combat_context_enemies (void)
{
    g_autoptr(LrgPlayerCombatant) player = NULL;
    g_autoptr(LrgCombatContext) ctx = NULL;
    g_autoptr(LrgEnemyDef) def1 = NULL;
    g_autoptr(LrgEnemyDef) def2 = NULL;
    LrgEnemyInstance *enemy1;
    LrgEnemyInstance *enemy2;
    GPtrArray *enemies;

    player = lrg_player_combatant_new ("ironclad", "Ironclad", 80);
    ctx = lrg_combat_context_new (player, NULL);

    g_assert_cmpuint (lrg_combat_context_get_enemy_count (ctx), ==, 0);

    def1 = lrg_enemy_def_new ("slime", "Slime");
    lrg_enemy_def_set_base_health (def1, 20);
    def2 = lrg_enemy_def_new ("cultist", "Cultist");
    lrg_enemy_def_set_base_health (def2, 48);

    enemy1 = lrg_enemy_instance_new (def1);
    enemy2 = lrg_enemy_instance_new (def2);

    lrg_combat_context_add_enemy (ctx, enemy1);
    lrg_combat_context_add_enemy (ctx, enemy2);

    g_assert_cmpuint (lrg_combat_context_get_enemy_count (ctx), ==, 2);

    enemies = lrg_combat_context_get_enemies (ctx);
    g_assert_nonnull (enemies);
    g_assert_cmpuint (enemies->len, ==, 2);

    g_assert_true (lrg_combat_context_get_enemy_at (ctx, 0) == enemy1);
    g_assert_true (lrg_combat_context_get_enemy_at (ctx, 1) == enemy2);

    lrg_combat_context_remove_enemy (ctx, enemy1);
    g_assert_cmpuint (lrg_combat_context_get_enemy_count (ctx), ==, 1);
    g_assert_true (lrg_combat_context_get_enemy_at (ctx, 0) == enemy2);
}

static void
test_combat_context_card_piles (void)
{
    g_autoptr(LrgPlayerCombatant) player = NULL;
    g_autoptr(LrgCombatContext) ctx = NULL;
    LrgCardPile *draw_pile;
    LrgCardPile *discard_pile;
    LrgCardPile *exhaust_pile;
    LrgHand *hand;

    player = lrg_player_combatant_new ("ironclad", "Ironclad", 80);
    ctx = lrg_combat_context_new (player, NULL);

    draw_pile = lrg_combat_context_get_draw_pile (ctx);
    discard_pile = lrg_combat_context_get_discard_pile (ctx);
    exhaust_pile = lrg_combat_context_get_exhaust_pile (ctx);
    hand = lrg_combat_context_get_hand (ctx);

    g_assert_nonnull (draw_pile);
    g_assert_nonnull (discard_pile);
    g_assert_nonnull (exhaust_pile);
    g_assert_nonnull (hand);

    g_assert_true (LRG_IS_CARD_PILE (draw_pile));
    g_assert_true (LRG_IS_CARD_PILE (discard_pile));
    g_assert_true (LRG_IS_CARD_PILE (exhaust_pile));
    g_assert_true (LRG_IS_HAND (hand));
}

static void
test_combat_context_turn (void)
{
    g_autoptr(LrgPlayerCombatant) player = NULL;
    g_autoptr(LrgCombatContext) ctx = NULL;

    player = lrg_player_combatant_new ("ironclad", "Ironclad", 80);
    ctx = lrg_combat_context_new (player, NULL);

    g_assert_cmpint (lrg_combat_context_get_turn (ctx), ==, 0);

    lrg_combat_context_increment_turn (ctx);
    g_assert_cmpint (lrg_combat_context_get_turn (ctx), ==, 1);

    lrg_combat_context_increment_turn (ctx);
    g_assert_cmpint (lrg_combat_context_get_turn (ctx), ==, 2);
}

static void
test_combat_context_cards_played (void)
{
    g_autoptr(LrgPlayerCombatant) player = NULL;
    g_autoptr(LrgCombatContext) ctx = NULL;

    player = lrg_player_combatant_new ("ironclad", "Ironclad", 80);
    ctx = lrg_combat_context_new (player, NULL);

    g_assert_cmpint (lrg_combat_context_get_cards_played_this_turn (ctx), ==, 0);

    lrg_combat_context_increment_cards_played (ctx);
    g_assert_cmpint (lrg_combat_context_get_cards_played_this_turn (ctx), ==, 1);

    lrg_combat_context_increment_cards_played (ctx);
    lrg_combat_context_increment_cards_played (ctx);
    g_assert_cmpint (lrg_combat_context_get_cards_played_this_turn (ctx), ==, 3);

    lrg_combat_context_reset_turn_counters (ctx);
    g_assert_cmpint (lrg_combat_context_get_cards_played_this_turn (ctx), ==, 0);
}

static void
test_combat_context_variables (void)
{
    g_autoptr(LrgPlayerCombatant) player = NULL;
    g_autoptr(LrgCombatContext) ctx = NULL;

    player = lrg_player_combatant_new ("ironclad", "Ironclad", 80);
    ctx = lrg_combat_context_new (player, NULL);

    /* Variables default to 0 */
    g_assert_cmpint (lrg_combat_context_get_variable (ctx, "X"), ==, 0);

    lrg_combat_context_set_variable (ctx, "X", 5);
    g_assert_cmpint (lrg_combat_context_get_variable (ctx, "X"), ==, 5);

    lrg_combat_context_set_variable (ctx, "combo", 3);
    g_assert_cmpint (lrg_combat_context_get_variable (ctx, "combo"), ==, 3);
}

static void
test_combat_context_rng (void)
{
    g_autoptr(LrgPlayerCombatant) player = NULL;
    g_autoptr(LrgCombatContext) ctx = NULL;
    GRand *rng;
    gint val1, val2;

    player = lrg_player_combatant_new ("ironclad", "Ironclad", 80);
    ctx = lrg_combat_context_new (player, NULL);

    rng = lrg_combat_context_get_rng (ctx);
    g_assert_nonnull (rng);

    /* Set seed for reproducibility */
    lrg_combat_context_set_seed (ctx, 12345);
    val1 = g_rand_int_range (lrg_combat_context_get_rng (ctx), 0, 100);

    /* Reset seed - should get same value */
    lrg_combat_context_set_seed (ctx, 12345);
    val2 = g_rand_int_range (lrg_combat_context_get_rng (ctx), 0, 100);

    g_assert_cmpint (val1, ==, val2);
}

/* --------------------------------------------------------------------------
 * LrgCombatManager Tests
 * -------------------------------------------------------------------------- */

static void
test_combat_manager_new (void)
{
    g_autoptr(LrgCombatManager) manager = NULL;

    manager = lrg_combat_manager_new ();

    g_assert_nonnull (manager);
    g_assert_true (LRG_IS_COMBAT_MANAGER (manager));
    g_assert_false (lrg_combat_manager_is_active (manager));
    g_assert_null (lrg_combat_manager_get_context (manager));
}

static void
test_combat_manager_start_combat (void)
{
    g_autoptr(LrgCombatManager) manager = NULL;
    g_autoptr(LrgPlayerCombatant) player = NULL;
    g_autoptr(LrgCombatContext) ctx = NULL;

    manager = lrg_combat_manager_new ();
    player = lrg_player_combatant_new ("ironclad", "Ironclad", 80);
    ctx = lrg_combat_context_new (player, NULL);

    g_assert_false (lrg_combat_manager_is_active (manager));

    lrg_combat_manager_start_combat (manager, ctx);

    g_assert_true (lrg_combat_manager_is_active (manager));
    g_assert_true (lrg_combat_manager_get_context (manager) == ctx);
}

static void
test_combat_manager_end_combat (void)
{
    g_autoptr(LrgCombatManager) manager = NULL;
    g_autoptr(LrgPlayerCombatant) player = NULL;
    g_autoptr(LrgCombatContext) ctx = NULL;

    manager = lrg_combat_manager_new ();
    player = lrg_player_combatant_new ("ironclad", "Ironclad", 80);
    ctx = lrg_combat_context_new (player, NULL);

    lrg_combat_manager_start_combat (manager, ctx);
    g_assert_true (lrg_combat_manager_is_active (manager));

    lrg_combat_manager_end_combat (manager, LRG_COMBAT_RESULT_VICTORY);

    g_assert_false (lrg_combat_manager_is_active (manager));
}

static void
test_combat_manager_victory_check (void)
{
    g_autoptr(LrgCombatManager) manager = NULL;
    g_autoptr(LrgPlayerCombatant) player = NULL;
    g_autoptr(LrgCombatContext) ctx = NULL;
    g_autoptr(LrgEnemyDef) def = NULL;
    LrgEnemyInstance *enemy;

    manager = lrg_combat_manager_new ();
    player = lrg_player_combatant_new ("ironclad", "Ironclad", 80);
    ctx = lrg_combat_context_new (player, NULL);

    def = lrg_enemy_def_new ("slime", "Slime");
    lrg_enemy_def_set_base_health (def, 20);
    enemy = lrg_enemy_instance_new (def);
    lrg_combat_context_add_enemy (ctx, enemy);

    lrg_combat_manager_start_combat (manager, ctx);

    /* With enemy alive, no victory */
    g_assert_false (lrg_combat_manager_check_victory (manager));

    /* Kill the enemy */
    lrg_combatant_take_damage (LRG_COMBATANT (enemy), 20, LRG_EFFECT_FLAG_NONE);
    g_assert_false (lrg_combatant_is_alive (LRG_COMBATANT (enemy)));

    /* Now we have victory */
    g_assert_true (lrg_combat_manager_check_victory (manager));
}

static void
test_combat_manager_defeat_check (void)
{
    g_autoptr(LrgCombatManager) manager = NULL;
    g_autoptr(LrgPlayerCombatant) player = NULL;
    g_autoptr(LrgCombatContext) ctx = NULL;
    LrgCombatant *player_combatant;

    manager = lrg_combat_manager_new ();
    player = lrg_player_combatant_new ("ironclad", "Ironclad", 80);
    ctx = lrg_combat_context_new (player, NULL);

    lrg_combat_manager_start_combat (manager, ctx);
    player_combatant = LRG_COMBATANT (player);

    /* With player alive, no defeat */
    g_assert_false (lrg_combat_manager_check_defeat (manager));

    /* Kill the player */
    lrg_combatant_take_damage (player_combatant, 80, LRG_EFFECT_FLAG_NONE);
    g_assert_false (lrg_combatant_is_alive (player_combatant));

    /* Now we have defeat */
    g_assert_true (lrg_combat_manager_check_defeat (manager));
}

/* ==========================================================================
 * Phase 6.5: Run/Map System Tests
 * ========================================================================== */

static void
test_map_node_new (void)
{
    g_autoptr(LrgMapNode) node = NULL;

    node = lrg_map_node_new ("node_1_0_0", LRG_MAP_NODE_COMBAT, 0, 0);
    g_assert_nonnull (node);

    g_assert_cmpstr (lrg_map_node_get_id (node), ==, "node_1_0_0");
    g_assert_cmpint (lrg_map_node_get_node_type (node), ==, LRG_MAP_NODE_COMBAT);
    g_assert_cmpint (lrg_map_node_get_row (node), ==, 0);
    g_assert_cmpint (lrg_map_node_get_column (node), ==, 0);
    g_assert_false (lrg_map_node_get_visited (node));
}

static void
test_map_node_types (void)
{
    g_autoptr(LrgMapNode) combat = NULL;
    g_autoptr(LrgMapNode) elite = NULL;
    g_autoptr(LrgMapNode) boss = NULL;
    g_autoptr(LrgMapNode) event = NULL;
    g_autoptr(LrgMapNode) shop = NULL;
    g_autoptr(LrgMapNode) rest = NULL;

    combat = lrg_map_node_new ("n1", LRG_MAP_NODE_COMBAT, 0, 0);
    elite = lrg_map_node_new ("n2", LRG_MAP_NODE_ELITE, 1, 0);
    boss = lrg_map_node_new ("n3", LRG_MAP_NODE_BOSS, 2, 0);
    event = lrg_map_node_new ("n4", LRG_MAP_NODE_EVENT, 3, 0);
    shop = lrg_map_node_new ("n5", LRG_MAP_NODE_SHOP, 4, 0);
    rest = lrg_map_node_new ("n6", LRG_MAP_NODE_REST, 5, 0);

    g_assert_cmpint (lrg_map_node_get_node_type (combat), ==, LRG_MAP_NODE_COMBAT);
    g_assert_cmpint (lrg_map_node_get_node_type (elite), ==, LRG_MAP_NODE_ELITE);
    g_assert_cmpint (lrg_map_node_get_node_type (boss), ==, LRG_MAP_NODE_BOSS);
    g_assert_cmpint (lrg_map_node_get_node_type (event), ==, LRG_MAP_NODE_EVENT);
    g_assert_cmpint (lrg_map_node_get_node_type (shop), ==, LRG_MAP_NODE_SHOP);
    g_assert_cmpint (lrg_map_node_get_node_type (rest), ==, LRG_MAP_NODE_REST);
}

static void
test_map_node_connections (void)
{
    g_autoptr(LrgMapNode) node1 = NULL;
    g_autoptr(LrgMapNode) node2 = NULL;
    g_autoptr(LrgMapNode) node3 = NULL;
    GPtrArray *connections;

    node1 = lrg_map_node_new ("n1", LRG_MAP_NODE_COMBAT, 0, 0);
    node2 = lrg_map_node_new ("n2", LRG_MAP_NODE_COMBAT, 1, 0);
    node3 = lrg_map_node_new ("n3", LRG_MAP_NODE_COMBAT, 1, 1);

    /* Initially no connections */
    g_assert_cmpuint (lrg_map_node_get_connection_count (node1), ==, 0);
    g_assert_false (lrg_map_node_is_connected_to (node1, node2));

    /* Add connections */
    lrg_map_node_add_connection (node1, node2);
    lrg_map_node_add_connection (node1, node3);

    g_assert_cmpuint (lrg_map_node_get_connection_count (node1), ==, 2);
    g_assert_true (lrg_map_node_is_connected_to (node1, node2));
    g_assert_true (lrg_map_node_is_connected_to (node1, node3));

    /* Get connections */
    connections = lrg_map_node_get_connections (node1);
    g_assert_nonnull (connections);
    g_assert_cmpuint (connections->len, ==, 2);

    /* Remove connection */
    g_assert_true (lrg_map_node_remove_connection (node1, node2));
    g_assert_cmpuint (lrg_map_node_get_connection_count (node1), ==, 1);
    g_assert_false (lrg_map_node_is_connected_to (node1, node2));
    g_assert_true (lrg_map_node_is_connected_to (node1, node3));
}

static void
test_map_node_visited (void)
{
    g_autoptr(LrgMapNode) node = NULL;

    node = lrg_map_node_new ("n1", LRG_MAP_NODE_COMBAT, 0, 0);

    g_assert_false (lrg_map_node_get_visited (node));

    lrg_map_node_set_visited (node, TRUE);
    g_assert_true (lrg_map_node_get_visited (node));

    lrg_map_node_set_visited (node, FALSE);
    g_assert_false (lrg_map_node_get_visited (node));
}

static void
test_map_node_encounter (void)
{
    g_autoptr(LrgMapNode) node = NULL;

    node = lrg_map_node_new ("n1", LRG_MAP_NODE_COMBAT, 0, 0);

    g_assert_null (lrg_map_node_get_encounter_id (node));

    lrg_map_node_set_encounter_id (node, "slime_fight");
    g_assert_cmpstr (lrg_map_node_get_encounter_id (node), ==, "slime_fight");

    lrg_map_node_set_encounter_id (node, NULL);
    g_assert_null (lrg_map_node_get_encounter_id (node));
}

static void
test_map_node_position (void)
{
    g_autoptr(LrgMapNode) node = NULL;

    node = lrg_map_node_new ("n1", LRG_MAP_NODE_COMBAT, 0, 0);

    /* Default position is 0, 0 */
    g_assert_cmpfloat (lrg_map_node_get_x (node), ==, 0.0f);
    g_assert_cmpfloat (lrg_map_node_get_y (node), ==, 0.0f);

    /* Set position */
    lrg_map_node_set_x (node, 100.0f);
    lrg_map_node_set_y (node, 200.0f);

    g_assert_cmpfloat (lrg_map_node_get_x (node), ==, 100.0f);
    g_assert_cmpfloat (lrg_map_node_get_y (node), ==, 200.0f);
}

static void
test_run_map_new (void)
{
    g_autoptr(LrgRunMap) map = NULL;

    map = lrg_run_map_new (1, 12345);
    g_assert_nonnull (map);

    g_assert_cmpint (lrg_run_map_get_act (map), ==, 1);
    g_assert_cmpuint (lrg_run_map_get_seed (map), ==, 12345);
    g_assert_false (lrg_run_map_is_generated (map));
}

static void
test_run_map_generate (void)
{
    g_autoptr(LrgRunMap) map = NULL;

    map = lrg_run_map_new (1, 42);
    g_assert_false (lrg_run_map_is_generated (map));

    lrg_run_map_generate (map, 15, 2, 4);

    g_assert_true (lrg_run_map_is_generated (map));
    g_assert_cmpint (lrg_run_map_get_row_count (map), ==, 15);
    g_assert_cmpuint (lrg_run_map_get_node_count (map), >, 0);
}

static void
test_run_map_starting_nodes (void)
{
    g_autoptr(LrgRunMap) map = NULL;
    GPtrArray *starting;

    map = lrg_run_map_new (1, 42);
    lrg_run_map_generate (map, 10, 3, 4);

    starting = lrg_run_map_get_starting_nodes (map);
    g_assert_nonnull (starting);
    g_assert_cmpuint (starting->len, >=, 3);
    g_assert_cmpuint (starting->len, <=, 4);

    /* All starting nodes should be in row 0 */
    {
        guint i;
        for (i = 0; i < starting->len; i++)
        {
            LrgMapNode *node = g_ptr_array_index (starting, i);
            g_assert_cmpint (lrg_map_node_get_row (node), ==, 0);
        }
    }
}

static void
test_run_map_boss_node (void)
{
    g_autoptr(LrgRunMap) map = NULL;
    LrgMapNode *boss;

    map = lrg_run_map_new (1, 42);
    lrg_run_map_generate (map, 10, 2, 4);

    boss = lrg_run_map_get_boss_node (map);
    g_assert_nonnull (boss);
    g_assert_cmpint (lrg_map_node_get_node_type (boss), ==, LRG_MAP_NODE_BOSS);
    g_assert_cmpint (lrg_map_node_get_row (boss), ==, 9);  /* Last row */
}

static void
test_run_map_connections (void)
{
    g_autoptr(LrgRunMap) map = NULL;
    GPtrArray *row0, *row1;
    guint i;
    gboolean any_connections = FALSE;

    map = lrg_run_map_new (1, 42);
    lrg_run_map_generate (map, 10, 3, 4);

    row0 = lrg_run_map_get_nodes_in_row (map, 0);
    row1 = lrg_run_map_get_nodes_in_row (map, 1);

    g_assert_nonnull (row0);
    g_assert_nonnull (row1);

    /* Check that row 0 nodes have connections to row 1 */
    for (i = 0; i < row0->len; i++)
    {
        LrgMapNode *node = g_ptr_array_index (row0, i);
        if (lrg_map_node_get_connection_count (node) > 0)
        {
            any_connections = TRUE;
            break;
        }
    }

    g_assert_true (any_connections);
}

static void
test_run_map_lookup (void)
{
    g_autoptr(LrgRunMap) map = NULL;
    LrgMapNode *found;
    GPtrArray *starting;
    LrgMapNode *first_node;
    const gchar *node_id;

    map = lrg_run_map_new (1, 42);
    lrg_run_map_generate (map, 10, 2, 4);

    /* Get a node ID to search for */
    starting = lrg_run_map_get_starting_nodes (map);
    first_node = g_ptr_array_index (starting, 0);
    node_id = lrg_map_node_get_id (first_node);

    /* Look it up */
    found = lrg_run_map_get_node_by_id (map, node_id);
    g_assert_nonnull (found);
    g_assert_true (found == first_node);

    /* Non-existent node */
    found = lrg_run_map_get_node_by_id (map, "nonexistent_node");
    g_assert_null (found);
}

static void
test_run_new (void)
{
    g_autoptr(LrgRun) run = NULL;

    run = lrg_run_new ("ironclad", 12345);
    g_assert_nonnull (run);

    g_assert_cmpstr (lrg_run_get_character_id (run), ==, "ironclad");
    g_assert_cmpuint (lrg_run_get_seed (run), ==, 12345);
    g_assert_cmpint (lrg_run_get_state (run), ==, LRG_RUN_STATE_NOT_STARTED);
    g_assert_cmpint (lrg_run_get_current_act (run), ==, 1);
    g_assert_cmpint (lrg_run_get_gold (run), ==, 0);
}

static void
test_run_gold (void)
{
    g_autoptr(LrgRun) run = NULL;

    run = lrg_run_new ("ironclad", 42);

    g_assert_cmpint (lrg_run_get_gold (run), ==, 0);

    lrg_run_set_gold (run, 100);
    g_assert_cmpint (lrg_run_get_gold (run), ==, 100);

    lrg_run_add_gold (run, 50);
    g_assert_cmpint (lrg_run_get_gold (run), ==, 150);

    g_assert_true (lrg_run_spend_gold (run, 75));
    g_assert_cmpint (lrg_run_get_gold (run), ==, 75);

    g_assert_false (lrg_run_spend_gold (run, 100));  /* Not enough */
    g_assert_cmpint (lrg_run_get_gold (run), ==, 75);
}

static void
test_run_state (void)
{
    g_autoptr(LrgRun) run = NULL;

    run = lrg_run_new ("ironclad", 42);

    g_assert_cmpint (lrg_run_get_state (run), ==, LRG_RUN_STATE_NOT_STARTED);

    lrg_run_set_state (run, LRG_RUN_STATE_MAP);
    g_assert_cmpint (lrg_run_get_state (run), ==, LRG_RUN_STATE_MAP);

    lrg_run_set_state (run, LRG_RUN_STATE_COMBAT);
    g_assert_cmpint (lrg_run_get_state (run), ==, LRG_RUN_STATE_COMBAT);

    lrg_run_set_state (run, LRG_RUN_STATE_VICTORY);
    g_assert_cmpint (lrg_run_get_state (run), ==, LRG_RUN_STATE_VICTORY);
}

static void
test_run_relics (void)
{
    g_autoptr(LrgRun) run = NULL;
    g_autoptr(LrgRelicDef) def = NULL;
    LrgRelicInstance *relic;
    GPtrArray *relics;

    run = lrg_run_new ("ironclad", 42);
    def = lrg_relic_def_new ("burning_blood", "Burning Blood");

    relics = lrg_run_get_relics (run);
    g_assert_nonnull (relics);
    g_assert_cmpuint (relics->len, ==, 0);

    /* Add relic (takes ownership) */
    relic = lrg_relic_instance_new (def);
    lrg_run_add_relic (run, relic);

    g_assert_cmpuint (lrg_run_get_relics (run)->len, ==, 1);
    g_assert_true (lrg_run_has_relic (run, "burning_blood"));
    g_assert_nonnull (lrg_run_get_relic (run, "burning_blood"));
}

static void
test_run_potions (void)
{
    g_autoptr(LrgRun) run = NULL;
    g_autoptr(LrgPotionDef) def = NULL;
    LrgPotionInstance *potion;
    GPtrArray *potions;

    run = lrg_run_new ("ironclad", 42);
    def = lrg_potion_def_new ("health_potion", "Health Potion");

    /* Default max potions is 3 */
    g_assert_cmpint (lrg_run_get_max_potions (run), ==, 3);

    potions = lrg_run_get_potions (run);
    g_assert_nonnull (potions);
    g_assert_cmpuint (potions->len, ==, 0);

    /* Add potion (takes ownership) */
    potion = lrg_potion_instance_new (def);
    g_assert_true (lrg_run_add_potion (run, potion));

    g_assert_cmpuint (lrg_run_get_potions (run)->len, ==, 1);

    /* Remove potion */
    g_assert_true (lrg_run_remove_potion (run, 0));
    g_assert_cmpuint (lrg_run_get_potions (run)->len, ==, 0);
}

static void
test_run_potions_max (void)
{
    g_autoptr(LrgRun) run = NULL;
    g_autoptr(LrgPotionDef) def = NULL;
    LrgPotionInstance *p1, *p2, *p3, *p4;

    run = lrg_run_new ("ironclad", 42);
    def = lrg_potion_def_new ("health_potion", "Health Potion");

    lrg_run_set_max_potions (run, 2);
    g_assert_cmpint (lrg_run_get_max_potions (run), ==, 2);

    p1 = lrg_potion_instance_new (def);
    p2 = lrg_potion_instance_new (def);
    p3 = lrg_potion_instance_new (def);

    g_assert_true (lrg_run_add_potion (run, p1));
    g_assert_true (lrg_run_add_potion (run, p2));
    g_assert_false (lrg_run_add_potion (run, p3));  /* Full */

    g_assert_cmpuint (lrg_run_get_potions (run)->len, ==, 2);

    /* Expand capacity */
    lrg_run_set_max_potions (run, 3);
    p4 = lrg_potion_instance_new (def);
    g_assert_true (lrg_run_add_potion (run, p4));
    g_assert_cmpuint (lrg_run_get_potions (run)->len, ==, 3);
}

static void
test_run_statistics (void)
{
    g_autoptr(LrgRun) run = NULL;

    run = lrg_run_new ("ironclad", 42);

    g_assert_cmpint (lrg_run_get_enemies_killed (run), ==, 0);
    g_assert_cmpfloat (lrg_run_get_elapsed_time (run), ==, 0.0);

    lrg_run_add_enemy_killed (run);
    lrg_run_add_enemy_killed (run);
    g_assert_cmpint (lrg_run_get_enemies_killed (run), ==, 2);

    lrg_run_add_elapsed_time (run, 60.0);
    lrg_run_add_elapsed_time (run, 30.0);
    g_assert_cmpfloat (lrg_run_get_elapsed_time (run), ==, 90.0);
}

static void
test_run_manager_singleton (void)
{
    LrgRunManager *manager1;
    LrgRunManager *manager2;

    manager1 = lrg_run_manager_get_default ();
    g_assert_nonnull (manager1);

    manager2 = lrg_run_manager_get_default ();
    g_assert_true (manager1 == manager2);
}

static void
test_run_manager_start_run (void)
{
    g_autoptr(LrgRunManager) manager = NULL;
    LrgRun *run;

    manager = lrg_run_manager_new ();

    g_assert_false (lrg_run_manager_has_active_run (manager));
    g_assert_null (lrg_run_manager_get_current_run (manager));

    run = lrg_run_manager_start_run (manager, "ironclad", 42);
    g_assert_nonnull (run);

    g_assert_true (lrg_run_manager_has_active_run (manager));
    g_assert_true (run == lrg_run_manager_get_current_run (manager));

    /* State should be MAP after starting */
    g_assert_cmpint (lrg_run_get_state (run), ==, LRG_RUN_STATE_MAP);
}

static void
test_run_manager_end_run (void)
{
    g_autoptr(LrgRunManager) manager = NULL;

    manager = lrg_run_manager_new ();

    (void)lrg_run_manager_start_run (manager, "ironclad", 42);
    g_assert_true (lrg_run_manager_has_active_run (manager));

    lrg_run_manager_end_run (manager, TRUE);

    g_assert_false (lrg_run_manager_has_active_run (manager));
    g_assert_null (lrg_run_manager_get_current_run (manager));
}

static void
test_run_manager_map_generation (void)
{
    g_autoptr(LrgRunManager) manager = NULL;
    LrgRun *run;
    LrgRunMap *map;

    manager = lrg_run_manager_new ();
    lrg_run_manager_set_map_rows (manager, 10);
    lrg_run_manager_set_map_width (manager, 2, 4);

    run = lrg_run_manager_start_run (manager, "ironclad", 42);

    map = lrg_run_get_map (run);
    g_assert_nonnull (map);
    g_assert_true (lrg_run_map_is_generated (map));
    g_assert_cmpint (lrg_run_map_get_row_count (map), ==, 10);
}

static void
test_run_manager_valid_moves (void)
{
    g_autoptr(LrgRunManager) manager = NULL;
    LrgRun *run;
    GPtrArray *moves;
    GPtrArray *starting;

    manager = lrg_run_manager_new ();
    lrg_run_manager_set_map_rows (manager, 10);
    lrg_run_manager_set_map_width (manager, 2, 3);

    run = lrg_run_manager_start_run (manager, "ironclad", 42);

    /* At start, valid moves are the starting nodes */
    moves = lrg_run_manager_get_valid_moves (manager);
    g_assert_nonnull (moves);

    starting = lrg_run_map_get_starting_nodes (lrg_run_get_map (run));
    g_assert_cmpuint (moves->len, ==, starting->len);
}

static void
test_run_manager_select_node (void)
{
    g_autoptr(LrgRunManager) manager = NULL;
    LrgRun *run;
    LrgRunMap *map;
    GPtrArray *starting;
    LrgMapNode *first_node;

    manager = lrg_run_manager_new ();
    lrg_run_manager_set_map_rows (manager, 10);
    lrg_run_manager_set_map_width (manager, 2, 3);

    run = lrg_run_manager_start_run (manager, "ironclad", 42);
    map = lrg_run_get_map (run);

    /* Select the first starting node */
    starting = lrg_run_map_get_starting_nodes (map);
    first_node = g_ptr_array_index (starting, 0);

    g_assert_true (lrg_run_manager_select_node (manager, first_node));

    /* Current node should be set */
    g_assert_true (lrg_run_get_current_node (run) == first_node);

    /* Node should be marked visited */
    g_assert_true (lrg_map_node_get_visited (first_node));
}

/* ==========================================================================
 * Phase 6.6: Scoring System Tests
 * ========================================================================== */

/* --- LrgScoringRules tests --- */
/* Note: LrgScoringRules is an interface. Tests use LrgScoringManager which
 * provides a default implementation internally. */

static void
test_scoring_rules_interface (void)
{
    /* Just verify the interface type is registered */
    GType type = LRG_TYPE_SCORING_RULES;
    g_assert_true (g_type_is_a (type, G_TYPE_INTERFACE));
}

/* --- LrgScoringHand tests --- */

static LrgCardInstance *
create_poker_card (LrgCardRank rank, LrgCardSuit suit)
{
    g_autoptr(LrgCardDef) def = lrg_card_def_new ("poker-card");
    LrgCardInstance *instance;
    gint chip_value;

    lrg_card_def_set_rank (def, rank);
    lrg_card_def_set_suit (def, suit);

    /* Set chip value based on rank (Balatro-style):
     * Ace = 11, Face cards = 10, others = face value */
    chip_value = lrg_scoring_hand_get_chip_value (rank);
    lrg_card_def_set_chip_value (def, chip_value);

    instance = lrg_card_instance_new (def);
    return instance;
}

static void
test_scoring_hand_new (void)
{
    g_autoptr(LrgScoringHand) hand = NULL;

    hand = lrg_scoring_hand_new ();
    g_assert_nonnull (hand);
    g_assert_true (LRG_IS_SCORING_HAND (hand));
}

static void
test_scoring_hand_high_card (void)
{
    g_autoptr(LrgScoringHand) hand = NULL;
    g_autoptr(GPtrArray) cards = NULL;
    LrgHandType type;

    hand = lrg_scoring_hand_new ();
    cards = g_ptr_array_new_with_free_func (g_object_unref);

    /* High card: Ace, 7, 5, 3, 2 (different suits) */
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_ACE, LRG_CARD_SUIT_SPADES));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_SEVEN, LRG_CARD_SUIT_HEARTS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_FIVE, LRG_CARD_SUIT_DIAMONDS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_THREE, LRG_CARD_SUIT_CLUBS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_TWO, LRG_CARD_SUIT_SPADES));

    lrg_scoring_hand_set_cards (hand, cards);
    type = lrg_scoring_hand_evaluate (hand);

    g_assert_cmpint (type, ==, LRG_HAND_TYPE_HIGH_CARD);
}

static void
test_scoring_hand_pair (void)
{
    g_autoptr(LrgScoringHand) hand = NULL;
    g_autoptr(GPtrArray) cards = NULL;
    LrgHandType type;
    GPtrArray *scoring;

    hand = lrg_scoring_hand_new ();
    cards = g_ptr_array_new_with_free_func (g_object_unref);

    /* Pair of Kings */
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_KING, LRG_CARD_SUIT_SPADES));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_KING, LRG_CARD_SUIT_HEARTS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_SEVEN, LRG_CARD_SUIT_DIAMONDS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_FIVE, LRG_CARD_SUIT_CLUBS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_TWO, LRG_CARD_SUIT_SPADES));

    lrg_scoring_hand_set_cards (hand, cards);
    type = lrg_scoring_hand_evaluate (hand);

    g_assert_cmpint (type, ==, LRG_HAND_TYPE_PAIR);

    /* In Balatro-style scoring, all played cards contribute chip values */
    scoring = lrg_scoring_hand_get_scoring_cards (hand);
    g_assert_cmpuint (scoring->len, ==, 5);
}

static void
test_scoring_hand_two_pair (void)
{
    g_autoptr(LrgScoringHand) hand = NULL;
    g_autoptr(GPtrArray) cards = NULL;
    LrgHandType type;

    hand = lrg_scoring_hand_new ();
    cards = g_ptr_array_new_with_free_func (g_object_unref);

    /* Two pair: Kings and 7s */
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_KING, LRG_CARD_SUIT_SPADES));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_KING, LRG_CARD_SUIT_HEARTS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_SEVEN, LRG_CARD_SUIT_DIAMONDS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_SEVEN, LRG_CARD_SUIT_CLUBS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_TWO, LRG_CARD_SUIT_SPADES));

    lrg_scoring_hand_set_cards (hand, cards);
    type = lrg_scoring_hand_evaluate (hand);

    g_assert_cmpint (type, ==, LRG_HAND_TYPE_TWO_PAIR);
}

static void
test_scoring_hand_three_of_a_kind (void)
{
    g_autoptr(LrgScoringHand) hand = NULL;
    g_autoptr(GPtrArray) cards = NULL;
    LrgHandType type;
    GPtrArray *scoring;

    hand = lrg_scoring_hand_new ();
    cards = g_ptr_array_new_with_free_func (g_object_unref);

    /* Three Jacks */
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_JACK, LRG_CARD_SUIT_SPADES));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_JACK, LRG_CARD_SUIT_HEARTS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_JACK, LRG_CARD_SUIT_DIAMONDS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_FIVE, LRG_CARD_SUIT_CLUBS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_TWO, LRG_CARD_SUIT_SPADES));

    lrg_scoring_hand_set_cards (hand, cards);
    type = lrg_scoring_hand_evaluate (hand);

    g_assert_cmpint (type, ==, LRG_HAND_TYPE_THREE_OF_A_KIND);

    /* In Balatro-style scoring, all played cards contribute chip values */
    scoring = lrg_scoring_hand_get_scoring_cards (hand);
    g_assert_cmpuint (scoring->len, ==, 5);
}

static void
test_scoring_hand_straight (void)
{
    g_autoptr(LrgScoringHand) hand = NULL;
    g_autoptr(GPtrArray) cards = NULL;
    LrgHandType type;

    hand = lrg_scoring_hand_new ();
    cards = g_ptr_array_new_with_free_func (g_object_unref);

    /* Straight: 5-6-7-8-9 */
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_FIVE, LRG_CARD_SUIT_SPADES));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_SIX, LRG_CARD_SUIT_HEARTS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_SEVEN, LRG_CARD_SUIT_DIAMONDS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_EIGHT, LRG_CARD_SUIT_CLUBS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_NINE, LRG_CARD_SUIT_SPADES));

    lrg_scoring_hand_set_cards (hand, cards);
    type = lrg_scoring_hand_evaluate (hand);

    g_assert_cmpint (type, ==, LRG_HAND_TYPE_STRAIGHT);
}

static void
test_scoring_hand_flush (void)
{
    g_autoptr(LrgScoringHand) hand = NULL;
    g_autoptr(GPtrArray) cards = NULL;
    LrgHandType type;

    hand = lrg_scoring_hand_new ();
    cards = g_ptr_array_new_with_free_func (g_object_unref);

    /* Flush: all hearts */
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_TWO, LRG_CARD_SUIT_HEARTS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_FIVE, LRG_CARD_SUIT_HEARTS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_EIGHT, LRG_CARD_SUIT_HEARTS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_JACK, LRG_CARD_SUIT_HEARTS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_ACE, LRG_CARD_SUIT_HEARTS));

    lrg_scoring_hand_set_cards (hand, cards);
    type = lrg_scoring_hand_evaluate (hand);

    g_assert_cmpint (type, ==, LRG_HAND_TYPE_FLUSH);
}

static void
test_scoring_hand_full_house (void)
{
    g_autoptr(LrgScoringHand) hand = NULL;
    g_autoptr(GPtrArray) cards = NULL;
    LrgHandType type;

    hand = lrg_scoring_hand_new ();
    cards = g_ptr_array_new_with_free_func (g_object_unref);

    /* Full House: 3 Queens + 2 Tens */
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_QUEEN, LRG_CARD_SUIT_SPADES));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_QUEEN, LRG_CARD_SUIT_HEARTS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_QUEEN, LRG_CARD_SUIT_DIAMONDS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_TEN, LRG_CARD_SUIT_CLUBS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_TEN, LRG_CARD_SUIT_SPADES));

    lrg_scoring_hand_set_cards (hand, cards);
    type = lrg_scoring_hand_evaluate (hand);

    g_assert_cmpint (type, ==, LRG_HAND_TYPE_FULL_HOUSE);
}

static void
test_scoring_hand_four_of_a_kind (void)
{
    g_autoptr(LrgScoringHand) hand = NULL;
    g_autoptr(GPtrArray) cards = NULL;
    LrgHandType type;
    GPtrArray *scoring;

    hand = lrg_scoring_hand_new ();
    cards = g_ptr_array_new_with_free_func (g_object_unref);

    /* Four Aces */
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_ACE, LRG_CARD_SUIT_SPADES));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_ACE, LRG_CARD_SUIT_HEARTS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_ACE, LRG_CARD_SUIT_DIAMONDS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_ACE, LRG_CARD_SUIT_CLUBS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_TWO, LRG_CARD_SUIT_SPADES));

    lrg_scoring_hand_set_cards (hand, cards);
    type = lrg_scoring_hand_evaluate (hand);

    g_assert_cmpint (type, ==, LRG_HAND_TYPE_FOUR_OF_A_KIND);

    /* In Balatro-style scoring, all played cards contribute chip values */
    scoring = lrg_scoring_hand_get_scoring_cards (hand);
    g_assert_cmpuint (scoring->len, ==, 5);
}

static void
test_scoring_hand_straight_flush (void)
{
    g_autoptr(LrgScoringHand) hand = NULL;
    g_autoptr(GPtrArray) cards = NULL;
    LrgHandType type;

    hand = lrg_scoring_hand_new ();
    cards = g_ptr_array_new_with_free_func (g_object_unref);

    /* Straight Flush: 5-6-7-8-9 of spades */
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_FIVE, LRG_CARD_SUIT_SPADES));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_SIX, LRG_CARD_SUIT_SPADES));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_SEVEN, LRG_CARD_SUIT_SPADES));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_EIGHT, LRG_CARD_SUIT_SPADES));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_NINE, LRG_CARD_SUIT_SPADES));

    lrg_scoring_hand_set_cards (hand, cards);
    type = lrg_scoring_hand_evaluate (hand);

    g_assert_cmpint (type, ==, LRG_HAND_TYPE_STRAIGHT_FLUSH);
}

static void
test_scoring_hand_chip_values (void)
{
    /* Ace = 11 chips */
    g_assert_cmpint (lrg_scoring_hand_get_chip_value (LRG_CARD_RANK_ACE), ==, 11);

    /* Face cards = 10 chips */
    g_assert_cmpint (lrg_scoring_hand_get_chip_value (LRG_CARD_RANK_KING), ==, 10);
    g_assert_cmpint (lrg_scoring_hand_get_chip_value (LRG_CARD_RANK_QUEEN), ==, 10);
    g_assert_cmpint (lrg_scoring_hand_get_chip_value (LRG_CARD_RANK_JACK), ==, 10);

    /* Number cards = face value */
    g_assert_cmpint (lrg_scoring_hand_get_chip_value (LRG_CARD_RANK_TEN), ==, 10);
    g_assert_cmpint (lrg_scoring_hand_get_chip_value (LRG_CARD_RANK_FIVE), ==, 5);
    g_assert_cmpint (lrg_scoring_hand_get_chip_value (LRG_CARD_RANK_TWO), ==, 2);
}

/* --- LrgScoringContext tests --- */

static void
test_scoring_context_new (void)
{
    g_autoptr(LrgScoringContext) ctx = NULL;

    ctx = lrg_scoring_context_new ();
    g_assert_nonnull (ctx);
    g_assert_true (LRG_IS_SCORING_CONTEXT (ctx));
}

static void
test_scoring_context_chips (void)
{
    g_autoptr(LrgScoringContext) ctx = NULL;

    ctx = lrg_scoring_context_new ();

    lrg_scoring_context_set_base_chips (ctx, 30);
    g_assert_cmpint (lrg_scoring_context_get_base_chips (ctx), ==, 30);

    lrg_scoring_context_add_chips (ctx, 10);
    lrg_scoring_context_add_chips (ctx, 5);
    g_assert_cmpint (lrg_scoring_context_get_total_chips (ctx), ==, 45);
}

static void
test_scoring_context_mult (void)
{
    g_autoptr(LrgScoringContext) ctx = NULL;

    ctx = lrg_scoring_context_new ();

    lrg_scoring_context_set_base_mult (ctx, 4);
    g_assert_cmpint (lrg_scoring_context_get_base_mult (ctx), ==, 4);

    lrg_scoring_context_add_mult (ctx, 2);
    lrg_scoring_context_add_mult (ctx, 3);
    g_assert_cmpint (lrg_scoring_context_get_total_mult (ctx), ==, 9);
}

static void
test_scoring_context_x_mult (void)
{
    g_autoptr(LrgScoringContext) ctx = NULL;

    ctx = lrg_scoring_context_new ();

    g_assert_cmpfloat (lrg_scoring_context_get_x_mult (ctx), ==, 1.0);

    lrg_scoring_context_apply_x_mult (ctx, 1.5);
    g_assert_cmpfloat (lrg_scoring_context_get_x_mult (ctx), ==, 1.5);

    lrg_scoring_context_apply_x_mult (ctx, 2.0);
    g_assert_cmpfloat (lrg_scoring_context_get_x_mult (ctx), ==, 3.0);
}

static void
test_scoring_context_score_calculation (void)
{
    g_autoptr(LrgScoringContext) ctx = NULL;
    gint64 score;

    ctx = lrg_scoring_context_new ();

    /* Set up: 30 chips, 4 mult, x2 */
    lrg_scoring_context_set_base_chips (ctx, 30);
    lrg_scoring_context_set_base_mult (ctx, 4);
    lrg_scoring_context_apply_x_mult (ctx, 2.0);

    /* Score = 30 × 4 × 2.0 = 240 */
    score = lrg_scoring_context_calculate_score (ctx);
    g_assert_cmpint (score, ==, 240);
}

static void
test_scoring_context_reset (void)
{
    g_autoptr(LrgScoringContext) ctx = NULL;

    ctx = lrg_scoring_context_new ();

    lrg_scoring_context_set_base_chips (ctx, 50);
    lrg_scoring_context_add_chips (ctx, 20);
    lrg_scoring_context_set_base_mult (ctx, 6);
    lrg_scoring_context_apply_x_mult (ctx, 1.5);

    lrg_scoring_context_reset (ctx);

    g_assert_cmpint (lrg_scoring_context_get_base_chips (ctx), ==, 0);
    g_assert_cmpint (lrg_scoring_context_get_total_chips (ctx), ==, 0);
    g_assert_cmpint (lrg_scoring_context_get_base_mult (ctx), ==, 0);
    g_assert_cmpfloat (lrg_scoring_context_get_x_mult (ctx), ==, 1.0);
}

/* --- LrgJokerDef tests --- */

static void
test_joker_def_new (void)
{
    g_autoptr(LrgJokerDef) def = NULL;

    def = lrg_joker_def_new ("test-joker", "Test Joker");
    g_assert_nonnull (def);
    g_assert_true (LRG_IS_JOKER_DEF (def));
    g_assert_cmpstr (lrg_joker_def_get_id (def), ==, "test-joker");
}

static void
test_joker_def_properties (void)
{
    g_autoptr(LrgJokerDef) def = NULL;

    def = lrg_joker_def_new ("jolly", "Jolly Joker");
    lrg_joker_def_set_description (def, "+8 Mult if played hand contains a Pair");
    lrg_joker_def_set_rarity (def, LRG_JOKER_RARITY_COMMON);
    lrg_joker_def_set_cost (def, 5);
    lrg_joker_def_set_sell_value (def, 2);

    g_assert_cmpstr (lrg_joker_def_get_name (def), ==, "Jolly Joker");
    g_assert_cmpstr (lrg_joker_def_get_description (def, NULL), ==, "+8 Mult if played hand contains a Pair");
    g_assert_cmpint (lrg_joker_def_get_rarity (def), ==, LRG_JOKER_RARITY_COMMON);
    g_assert_cmpint (lrg_joker_def_get_cost (def), ==, 5);
    g_assert_cmpint (lrg_joker_def_get_sell_value (def), ==, 2);
}

static void
test_joker_def_bonuses (void)
{
    g_autoptr(LrgJokerDef) def = NULL;

    def = lrg_joker_def_new ("bonus-joker", "Bonus Joker");

    lrg_joker_def_set_plus_chips (def, 30);
    g_assert_cmpint (lrg_joker_def_get_plus_chips (def), ==, 30);

    lrg_joker_def_set_plus_mult (def, 4);
    g_assert_cmpint (lrg_joker_def_get_plus_mult (def), ==, 4);

    lrg_joker_def_set_x_mult (def, 1.5);
    g_assert_cmpfloat (lrg_joker_def_get_x_mult (def), ==, 1.5);
}

static void
test_joker_def_conditions (void)
{
    g_autoptr(LrgJokerDef) def = NULL;

    def = lrg_joker_def_new ("zany", "Zany Joker");
    lrg_joker_def_set_required_hand (def, LRG_HAND_TYPE_THREE_OF_A_KIND);
    lrg_joker_def_set_required_suit (def, LRG_CARD_SUIT_HEARTS);

    g_assert_cmpint (lrg_joker_def_get_required_hand (def), ==, LRG_HAND_TYPE_THREE_OF_A_KIND);
    g_assert_cmpint (lrg_joker_def_get_required_suit (def), ==, LRG_CARD_SUIT_HEARTS);
}

/* --- LrgJokerInstance tests --- */

static void
test_joker_instance_new (void)
{
    g_autoptr(LrgJokerDef) def = NULL;
    g_autoptr(LrgJokerInstance) instance = NULL;

    def = lrg_joker_def_new ("joker", "Joker");
    lrg_joker_def_set_sell_value (def, 3);

    instance = lrg_joker_instance_new (def);
    g_assert_nonnull (instance);
    g_assert_true (LRG_IS_JOKER_INSTANCE (instance));
    g_assert_cmpstr (lrg_joker_instance_get_name (instance), ==, "Joker");
    g_assert_cmpint (lrg_joker_instance_get_sell_value (instance), ==, 3);
}

static void
test_joker_instance_edition (void)
{
    g_autoptr(LrgJokerDef) def = NULL;
    g_autoptr(LrgJokerInstance) instance = NULL;

    def = lrg_joker_def_new ("foil-joker", "Foil Joker");
    instance = lrg_joker_instance_new_with_edition (def, LRG_JOKER_EDITION_FOIL);

    g_assert_cmpint (lrg_joker_instance_get_edition (instance), ==, LRG_JOKER_EDITION_FOIL);
    g_assert_cmpint (lrg_joker_instance_get_edition_chips (instance), ==, 50);
    g_assert_cmpint (lrg_joker_instance_get_edition_mult (instance), ==, 0);
    g_assert_cmpfloat (lrg_joker_instance_get_edition_x_mult (instance), ==, 1.0);
}

static void
test_joker_instance_edition_holographic (void)
{
    g_autoptr(LrgJokerDef) def = NULL;
    g_autoptr(LrgJokerInstance) instance = NULL;

    def = lrg_joker_def_new ("holo-joker", "Holographic Joker");
    instance = lrg_joker_instance_new_with_edition (def, LRG_JOKER_EDITION_HOLOGRAPHIC);

    g_assert_cmpint (lrg_joker_instance_get_edition_chips (instance), ==, 0);
    g_assert_cmpint (lrg_joker_instance_get_edition_mult (instance), ==, 10);
    g_assert_cmpfloat (lrg_joker_instance_get_edition_x_mult (instance), ==, 1.0);
}

static void
test_joker_instance_edition_polychrome (void)
{
    g_autoptr(LrgJokerDef) def = NULL;
    g_autoptr(LrgJokerInstance) instance = NULL;

    def = lrg_joker_def_new ("poly-joker", "Polychrome Joker");
    instance = lrg_joker_instance_new_with_edition (def, LRG_JOKER_EDITION_POLYCHROME);

    g_assert_cmpint (lrg_joker_instance_get_edition_chips (instance), ==, 0);
    g_assert_cmpint (lrg_joker_instance_get_edition_mult (instance), ==, 0);
    g_assert_cmpfloat (lrg_joker_instance_get_edition_x_mult (instance), ==, 1.5);
}

static void
test_joker_instance_counter (void)
{
    g_autoptr(LrgJokerDef) def = NULL;
    g_autoptr(LrgJokerInstance) instance = NULL;

    def = lrg_joker_def_new ("ice-cream", "Ice Cream");
    instance = lrg_joker_instance_new (def);

    /* Initial value is 0 */
    g_assert_cmpint (lrg_joker_instance_get_counter (instance), ==, 0);

    lrg_joker_instance_set_counter (instance, 100);
    g_assert_cmpint (lrg_joker_instance_get_counter (instance), ==, 100);

    lrg_joker_instance_add_counter (instance, -5);
    g_assert_cmpint (lrg_joker_instance_get_counter (instance), ==, 95);
}

static void
test_joker_instance_trigger_count (void)
{
    g_autoptr(LrgJokerDef) def = NULL;
    g_autoptr(LrgJokerInstance) instance = NULL;

    def = lrg_joker_def_new ("trigger-test", "Trigger Test Joker");
    instance = lrg_joker_instance_new (def);

    g_assert_cmpuint (lrg_joker_instance_get_times_triggered (instance), ==, 0);

    lrg_joker_instance_increment_trigger_count (instance);
    lrg_joker_instance_increment_trigger_count (instance);
    lrg_joker_instance_increment_trigger_count (instance);
    g_assert_cmpuint (lrg_joker_instance_get_times_triggered (instance), ==, 3);

    lrg_joker_instance_reset_trigger_count (instance);
    g_assert_cmpuint (lrg_joker_instance_get_times_triggered (instance), ==, 0);
}

static void
test_joker_instance_sell_value (void)
{
    g_autoptr(LrgJokerDef) def = NULL;
    g_autoptr(LrgJokerInstance) instance = NULL;

    def = lrg_joker_def_new ("egg", "Egg");
    lrg_joker_def_set_sell_value (def, 3);
    instance = lrg_joker_instance_new (def);

    g_assert_cmpint (lrg_joker_instance_get_sell_value (instance), ==, 3);

    /* Egg gains value at end of round */
    lrg_joker_instance_add_sell_value (instance, 3);
    g_assert_cmpint (lrg_joker_instance_get_sell_value (instance), ==, 6);
}

/* --- LrgScoringManager tests --- */

static void
test_scoring_manager_new (void)
{
    g_autoptr(LrgScoringManager) manager = NULL;

    manager = lrg_scoring_manager_new ();
    g_assert_nonnull (manager);
    g_assert_true (LRG_IS_SCORING_MANAGER (manager));
}

static void
test_scoring_manager_singleton (void)
{
    LrgScoringManager *manager1;
    LrgScoringManager *manager2;

    manager1 = lrg_scoring_manager_get_default ();
    manager2 = lrg_scoring_manager_get_default ();

    g_assert_nonnull (manager1);
    g_assert_true (manager1 == manager2);
}

static void
test_scoring_manager_round (void)
{
    g_autoptr(LrgScoringManager) manager = NULL;

    manager = lrg_scoring_manager_new ();

    g_assert_false (lrg_scoring_manager_is_round_active (manager));

    lrg_scoring_manager_start_round (manager, 300, 4, 3);

    g_assert_true (lrg_scoring_manager_is_round_active (manager));
    g_assert_cmpint (lrg_scoring_manager_get_target_score (manager), ==, 300);
    g_assert_cmpint (lrg_scoring_manager_get_hands_remaining (manager), ==, 4);
    g_assert_cmpint (lrg_scoring_manager_get_discards_remaining (manager), ==, 3);
    g_assert_cmpint (lrg_scoring_manager_get_current_score (manager), ==, 0);

    lrg_scoring_manager_end_round (manager);

    g_assert_false (lrg_scoring_manager_is_round_active (manager));
}

static void
test_scoring_manager_jokers (void)
{
    g_autoptr(LrgScoringManager) manager = NULL;
    g_autoptr(LrgJokerDef) def = NULL;
    LrgJokerInstance *joker;
    GPtrArray *jokers;

    manager = lrg_scoring_manager_new ();
    def = lrg_joker_def_new ("test", "Test Joker");

    /* Set max jokers to 5 */
    lrg_scoring_manager_set_max_jokers (manager, 5);
    g_assert_cmpint (lrg_scoring_manager_get_max_jokers (manager), ==, 5);

    /* Add a joker */
    joker = lrg_joker_instance_new (def);
    lrg_scoring_manager_add_joker (manager, joker);

    jokers = lrg_scoring_manager_get_jokers (manager);
    g_assert_cmpuint (jokers->len, ==, 1);

    /* Remove joker */
    g_assert_true (lrg_scoring_manager_remove_joker (manager, joker));
    g_assert_cmpuint (jokers->len, ==, 0);
}

static void
test_scoring_manager_phase (void)
{
    g_autoptr(LrgScoringManager) manager = NULL;

    manager = lrg_scoring_manager_new ();

    /* Initial phase is SETUP */
    g_assert_cmpint (lrg_scoring_manager_get_phase (manager), ==, LRG_SCORING_PHASE_SETUP);

    /* After starting a round, phase changes */
    lrg_scoring_manager_start_round (manager, 300, 4, 3);
    g_assert_cmpint (lrg_scoring_manager_get_phase (manager), ==, LRG_SCORING_PHASE_SELECT);

    lrg_scoring_manager_end_round (manager);
}

static void
test_scoring_manager_evaluate_hand (void)
{
    g_autoptr(LrgScoringManager) manager = NULL;
    g_autoptr(GPtrArray) cards = NULL;
    LrgHandType type;

    manager = lrg_scoring_manager_new ();
    cards = g_ptr_array_new_with_free_func (g_object_unref);

    /* Create a pair */
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_ACE, LRG_CARD_SUIT_SPADES));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_ACE, LRG_CARD_SUIT_HEARTS));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_FIVE, LRG_CARD_SUIT_DIAMONDS));

    type = lrg_scoring_manager_evaluate_hand (manager, cards);
    g_assert_cmpint (type, ==, LRG_HAND_TYPE_PAIR);
}

static void
test_scoring_manager_preview_score (void)
{
    g_autoptr(LrgScoringManager) manager = NULL;
    g_autoptr(GPtrArray) cards = NULL;
    gint64 preview;

    manager = lrg_scoring_manager_new ();
    cards = g_ptr_array_new_with_free_func (g_object_unref);

    /* Create a pair of aces (each ace = 11 chips) */
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_ACE, LRG_CARD_SUIT_SPADES));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_ACE, LRG_CARD_SUIT_HEARTS));

    /* Pair base: 10 chips, 2 mult
     * Card chips: 11 + 11 = 22
     * Total: (10 + 22) × 2 = 64 */
    preview = lrg_scoring_manager_preview_score (manager, cards);
    g_assert_cmpint (preview, ==, 64);
}

static void
test_scoring_manager_play_hand (void)
{
    g_autoptr(LrgScoringManager) manager = NULL;
    g_autoptr(GPtrArray) cards = NULL;
    gint64 score;

    manager = lrg_scoring_manager_new ();
    cards = g_ptr_array_new_with_free_func (g_object_unref);

    /* Start a round */
    lrg_scoring_manager_start_round (manager, 300, 4, 3);

    /* Create a pair */
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_KING, LRG_CARD_SUIT_SPADES));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_KING, LRG_CARD_SUIT_HEARTS));

    /* Play the hand */
    score = lrg_scoring_manager_play_hand (manager, cards);

    /* Pair base: 10 chips, 2 mult
     * Card chips: 10 + 10 = 20
     * Total: (10 + 20) × 2 = 60 */
    g_assert_cmpint (score, ==, 60);

    /* Hands remaining should decrease */
    g_assert_cmpint (lrg_scoring_manager_get_hands_remaining (manager), ==, 3);

    /* Current score should update */
    g_assert_cmpint (lrg_scoring_manager_get_current_score (manager), ==, 60);

    lrg_scoring_manager_end_round (manager);
}

static void
test_scoring_manager_discard (void)
{
    g_autoptr(LrgScoringManager) manager = NULL;
    g_autoptr(GPtrArray) cards = NULL;
    gboolean result;

    manager = lrg_scoring_manager_new ();
    cards = g_ptr_array_new_with_free_func (g_object_unref);

    lrg_scoring_manager_start_round (manager, 300, 4, 3);

    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_TWO, LRG_CARD_SUIT_SPADES));
    g_ptr_array_add (cards, create_poker_card (LRG_CARD_RANK_THREE, LRG_CARD_SUIT_HEARTS));

    result = lrg_scoring_manager_discard (manager, cards);
    g_assert_true (result);

    /* Discards remaining should decrease */
    g_assert_cmpint (lrg_scoring_manager_get_discards_remaining (manager), ==, 2);

    lrg_scoring_manager_end_round (manager);
}

static void
test_scoring_manager_rules (void)
{
    g_autoptr(LrgScoringManager) manager = NULL;
    LrgScoringRules *rules;

    manager = lrg_scoring_manager_new ();

    /* By default, no custom rules are set (uses internal defaults) */
    rules = lrg_scoring_manager_get_rules (manager);
    g_assert_null (rules);

    /* Can set custom rules if desired */
    /* (would require implementing an LrgScoringRules) */
}

/* ==========================================================================
 * Phase 7: Meta-Progression Tests
 * ========================================================================== */

/* --- LrgCharacterDef tests --- */

static void
test_character_def_new (void)
{
    g_autoptr(LrgCharacterDef) def = NULL;

    def = lrg_character_def_new ("ironclad", "The Ironclad");
    g_assert_nonnull (def);
    g_assert_true (LRG_IS_CHARACTER_DEF (def));
    g_assert_cmpstr (lrg_character_def_get_id (def), ==, "ironclad");
    g_assert_cmpstr (lrg_character_def_get_name (def), ==, "The Ironclad");
}

static void
test_character_def_properties (void)
{
    g_autoptr(LrgCharacterDef) def = NULL;

    def = lrg_character_def_new ("silent", "The Silent");

    /* Default stats (StS-style) */
    g_assert_cmpint (lrg_character_def_get_base_hp (def), ==, 80);
    g_assert_cmpint (lrg_character_def_get_base_energy (def), ==, 3);
    g_assert_cmpint (lrg_character_def_get_base_draw (def), ==, 5);
    g_assert_cmpint (lrg_character_def_get_starting_gold (def), ==, 99);

    /* Set custom stats */
    lrg_character_def_set_base_hp (def, 70);
    lrg_character_def_set_base_energy (def, 3);
    lrg_character_def_set_base_draw (def, 5);
    lrg_character_def_set_starting_gold (def, 99);

    g_assert_cmpint (lrg_character_def_get_base_hp (def), ==, 70);

    /* Description and icon */
    lrg_character_def_set_description (def, "A deadly ninja with a deck of shivs");
    g_assert_cmpstr (lrg_character_def_get_description (def), ==, "A deadly ninja with a deck of shivs");

    lrg_character_def_set_icon (def, "characters/silent.png");
    g_assert_cmpstr (lrg_character_def_get_icon (def), ==, "characters/silent.png");
}

static void
test_character_def_starting_deck (void)
{
    g_autoptr(LrgCharacterDef) def = NULL;
    g_autoptr(GPtrArray) deck = NULL;

    def = lrg_character_def_new ("defect", "The Defect");

    /* Add starting cards */
    lrg_character_def_add_starting_card (def, "strike", 4);
    lrg_character_def_add_starting_card (def, "defend", 4);
    lrg_character_def_add_starting_card (def, "zap", 1);
    lrg_character_def_add_starting_card (def, "dualcast", 1);

    deck = lrg_character_def_get_starting_deck (def);
    g_assert_nonnull (deck);
    /* 4 strikes + 4 defends + 1 zap + 1 dualcast = 10 cards */
    g_assert_cmpuint (deck->len, ==, 10);
}

static void
test_character_def_starting_relic (void)
{
    g_autoptr(LrgCharacterDef) def = NULL;

    def = lrg_character_def_new ("ironclad", "The Ironclad");

    lrg_character_def_set_starting_relic (def, "burning-blood");
    g_assert_cmpstr (lrg_character_def_get_starting_relic (def), ==, "burning-blood");
}

static void
test_character_def_unlock (void)
{
    g_autoptr(LrgCharacterDef) def = NULL;

    def = lrg_character_def_new ("watcher", "The Watcher");

    /* Not unlocked by default */
    g_assert_false (lrg_character_def_get_unlocked_by_default (def));

    lrg_character_def_set_unlocked_by_default (def, TRUE);
    g_assert_true (lrg_character_def_get_unlocked_by_default (def));

    lrg_character_def_set_unlock_requirement (def, "Complete a run with any character");
    g_assert_cmpstr (lrg_character_def_get_unlock_requirement (def), ==, "Complete a run with any character");
}

/* --- LrgPlayerProfile tests --- */

static void
test_player_profile_new (void)
{
    g_autoptr(LrgPlayerProfile) profile = NULL;

    profile = lrg_player_profile_new ("TestPlayer");
    g_assert_nonnull (profile);
    g_assert_true (LRG_IS_PLAYER_PROFILE (profile));
    g_assert_cmpstr (lrg_player_profile_get_name (profile), ==, "TestPlayer");
}

static void
test_player_profile_singleton (void)
{
    LrgPlayerProfile *profile1;
    LrgPlayerProfile *profile2;

    profile1 = lrg_player_profile_get_default ();
    profile2 = lrg_player_profile_get_default ();

    g_assert_nonnull (profile1);
    g_assert_true (profile1 == profile2);
}

static void
test_player_profile_unlocks (void)
{
    g_autoptr(LrgPlayerProfile) profile = NULL;
    g_autoptr(GPtrArray) unlocked = NULL;
    gboolean newly_unlocked;

    profile = lrg_player_profile_new ("UnlockTest");

    /* Initially nothing is unlocked */
    g_assert_false (lrg_player_profile_is_unlocked (profile, LRG_UNLOCK_TYPE_CHARACTER, "silent"));

    /* Unlock a character */
    newly_unlocked = lrg_player_profile_unlock (profile, LRG_UNLOCK_TYPE_CHARACTER, "silent");
    g_assert_true (newly_unlocked);
    g_assert_true (lrg_player_profile_is_unlocked (profile, LRG_UNLOCK_TYPE_CHARACTER, "silent"));

    /* Unlocking again returns FALSE */
    newly_unlocked = lrg_player_profile_unlock (profile, LRG_UNLOCK_TYPE_CHARACTER, "silent");
    g_assert_false (newly_unlocked);

    /* Get all unlocked characters */
    unlocked = lrg_player_profile_get_unlocked_ids (profile, LRG_UNLOCK_TYPE_CHARACTER);
    g_assert_cmpuint (unlocked->len, ==, 1);
}

static void
test_player_profile_unlock_status (void)
{
    g_autoptr(LrgPlayerProfile) profile = NULL;
    LrgUnlockStatus status;

    profile = lrg_player_profile_new ("StatusTest");

    /* Initially locked */
    status = lrg_player_profile_get_unlock_status (profile, LRG_UNLOCK_TYPE_CARD, "bash");
    g_assert_cmpint (status, ==, LRG_UNLOCK_STATUS_LOCKED);

    /* Unlock - becomes NEW */
    lrg_player_profile_unlock (profile, LRG_UNLOCK_TYPE_CARD, "bash");
    status = lrg_player_profile_get_unlock_status (profile, LRG_UNLOCK_TYPE_CARD, "bash");
    g_assert_cmpint (status, ==, LRG_UNLOCK_STATUS_NEW);

    /* Mark seen */
    lrg_player_profile_mark_seen (profile, LRG_UNLOCK_TYPE_CARD, "bash");
    status = lrg_player_profile_get_unlock_status (profile, LRG_UNLOCK_TYPE_CARD, "bash");
    g_assert_cmpint (status, ==, LRG_UNLOCK_STATUS_UNLOCKED);
}

static void
test_player_profile_character_progress (void)
{
    g_autoptr(LrgPlayerProfile) profile = NULL;

    profile = lrg_player_profile_new ("ProgressTest");

    /* Initial values are 0 */
    g_assert_cmpint (lrg_player_profile_get_character_wins (profile, "ironclad"), ==, 0);
    g_assert_cmpint (lrg_player_profile_get_character_runs (profile, "ironclad"), ==, 0);

    /* Add runs and wins */
    lrg_player_profile_add_character_run (profile, "ironclad");
    lrg_player_profile_add_character_run (profile, "ironclad");
    lrg_player_profile_add_character_run (profile, "ironclad");
    lrg_player_profile_add_character_win (profile, "ironclad");

    g_assert_cmpint (lrg_player_profile_get_character_runs (profile, "ironclad"), ==, 3);
    g_assert_cmpint (lrg_player_profile_get_character_wins (profile, "ironclad"), ==, 1);
}

static void
test_player_profile_ascension (void)
{
    g_autoptr(LrgPlayerProfile) profile = NULL;
    gint new_level;

    profile = lrg_player_profile_new ("AscensionTest");

    /* Initial max ascension is 0 */
    g_assert_cmpint (lrg_player_profile_get_max_ascension (profile, "silent"), ==, 0);

    /* Set max ascension */
    lrg_player_profile_set_max_ascension (profile, "silent", 5);
    g_assert_cmpint (lrg_player_profile_get_max_ascension (profile, "silent"), ==, 5);

    /* Unlock next */
    new_level = lrg_player_profile_unlock_next_ascension (profile, "silent");
    g_assert_cmpint (new_level, ==, 6);
    g_assert_cmpint (lrg_player_profile_get_max_ascension (profile, "silent"), ==, 6);
}

static void
test_player_profile_statistics (void)
{
    g_autoptr(LrgPlayerProfile) profile = NULL;

    profile = lrg_player_profile_new ("StatTest");

    /* Initial stats are 0 */
    g_assert_cmpint (lrg_player_profile_get_stat (profile, "cards_played"), ==, 0);

    lrg_player_profile_set_stat (profile, "cards_played", 100);
    g_assert_cmpint (lrg_player_profile_get_stat (profile, "cards_played"), ==, 100);

    lrg_player_profile_increment_stat (profile, "cards_played", 50);
    g_assert_cmpint (lrg_player_profile_get_stat (profile, "cards_played"), ==, 150);
}

static void
test_player_profile_high_score (void)
{
    g_autoptr(LrgPlayerProfile) profile = NULL;
    gboolean new_high;

    profile = lrg_player_profile_new ("ScoreTest");

    /* Initial high score is 0 */
    g_assert_cmpint (lrg_player_profile_get_high_score (profile, "defect"), ==, 0);

    /* Submit a score */
    new_high = lrg_player_profile_submit_score (profile, "defect", 500);
    g_assert_true (new_high);
    g_assert_cmpint (lrg_player_profile_get_high_score (profile, "defect"), ==, 500);

    /* Lower score is not a new high */
    new_high = lrg_player_profile_submit_score (profile, "defect", 300);
    g_assert_false (new_high);
    g_assert_cmpint (lrg_player_profile_get_high_score (profile, "defect"), ==, 500);

    /* Higher score is a new high */
    new_high = lrg_player_profile_submit_score (profile, "defect", 750);
    g_assert_true (new_high);
    g_assert_cmpint (lrg_player_profile_get_high_score (profile, "defect"), ==, 750);
}

static void
test_player_profile_dirty (void)
{
    g_autoptr(LrgPlayerProfile) profile = NULL;

    profile = lrg_player_profile_new ("DirtyTest");

    /* Changes should mark profile dirty */
    lrg_player_profile_mark_clean (profile);
    g_assert_false (lrg_player_profile_is_dirty (profile));

    lrg_player_profile_add_character_run (profile, "ironclad");
    g_assert_true (lrg_player_profile_is_dirty (profile));

    lrg_player_profile_mark_clean (profile);
    g_assert_false (lrg_player_profile_is_dirty (profile));
}

/* --- LrgUnlockDef tests --- */

static void
test_unlock_def_new (void)
{
    g_autoptr(LrgUnlockDef) def = NULL;

    def = lrg_unlock_def_new ("unlock-silent", LRG_UNLOCK_TYPE_CHARACTER, "silent");
    g_assert_nonnull (def);
    g_assert_true (LRG_IS_UNLOCK_DEF (def));
    g_assert_cmpstr (lrg_unlock_def_get_id (def), ==, "unlock-silent");
    g_assert_cmpint (lrg_unlock_def_get_unlock_type (def), ==, LRG_UNLOCK_TYPE_CHARACTER);
    g_assert_cmpstr (lrg_unlock_def_get_target_id (def), ==, "silent");
}

static void
test_unlock_def_properties (void)
{
    g_autoptr(LrgUnlockDef) def = NULL;

    def = lrg_unlock_def_new ("unlock-bash", LRG_UNLOCK_TYPE_CARD, "bash");

    lrg_unlock_def_set_name (def, "Unlock Bash");
    g_assert_cmpstr (lrg_unlock_def_get_name (def), ==, "Unlock Bash");

    lrg_unlock_def_set_description (def, "A powerful attack card");
    g_assert_cmpstr (lrg_unlock_def_get_description (def), ==, "A powerful attack card");

    /* Hidden unlocks */
    g_assert_false (lrg_unlock_def_get_hidden (def));
    lrg_unlock_def_set_hidden (def, TRUE);
    g_assert_true (lrg_unlock_def_get_hidden (def));
}

static void
test_unlock_def_win_condition (void)
{
    g_autoptr(LrgUnlockDef) def = NULL;
    g_autoptr(LrgPlayerProfile) profile = NULL;
    g_autofree gchar *req_text = NULL;

    def = lrg_unlock_def_new ("unlock-watcher", LRG_UNLOCK_TYPE_CHARACTER, "watcher");
    lrg_unlock_def_set_win_count (def, NULL, 1);  /* Win 1 run with any character */

    profile = lrg_player_profile_new ("ConditionTest");

    /* Not yet met */
    g_assert_false (lrg_unlock_def_check_condition (def, profile));
    g_assert_cmpfloat (lrg_unlock_def_get_progress (def, profile), ==, 0.0);

    /* Add a win */
    lrg_player_profile_add_character_win (profile, "ironclad");

    /* Now met */
    g_assert_true (lrg_unlock_def_check_condition (def, profile));
    g_assert_cmpfloat (lrg_unlock_def_get_progress (def, profile), ==, 1.0);

    /* Get requirement text */
    req_text = lrg_unlock_def_get_requirement_text (def);
    g_assert_nonnull (req_text);
}

static void
test_unlock_def_grant (void)
{
    g_autoptr(LrgUnlockDef) def = NULL;
    g_autoptr(LrgPlayerProfile) profile = NULL;
    gboolean granted;

    def = lrg_unlock_def_new ("unlock-defect", LRG_UNLOCK_TYPE_CHARACTER, "defect");
    lrg_unlock_def_set_win_count (def, NULL, 1);

    profile = lrg_player_profile_new ("GrantTest");

    /* Can't grant - conditions not met */
    granted = lrg_unlock_def_grant (def, profile);
    g_assert_false (granted);
    g_assert_false (lrg_player_profile_is_unlocked (profile, LRG_UNLOCK_TYPE_CHARACTER, "defect"));

    /* Meet condition */
    lrg_player_profile_add_character_win (profile, "silent");

    /* Now can grant */
    granted = lrg_unlock_def_grant (def, profile);
    g_assert_true (granted);
    g_assert_true (lrg_player_profile_is_unlocked (profile, LRG_UNLOCK_TYPE_CHARACTER, "defect"));

    /* Granting again returns FALSE (already unlocked) */
    granted = lrg_unlock_def_grant (def, profile);
    g_assert_false (granted);
}

/* --- LrgAscension tests --- */

static void
test_ascension_new (void)
{
    g_autoptr(LrgAscension) asc = NULL;

    asc = lrg_ascension_new (5);
    g_assert_nonnull (asc);
    g_assert_true (LRG_IS_ASCENSION (asc));
    g_assert_cmpint (lrg_ascension_get_level (asc), ==, 5);
}

static void
test_ascension_defaults (void)
{
    g_autoptr(LrgAscension) asc0 = NULL;
    g_autoptr(LrgAscension) asc5 = NULL;
    g_autoptr(LrgAscension) asc20 = NULL;

    /* Level 0 = no modifiers */
    asc0 = lrg_ascension_new_default (0);
    g_assert_cmpint (lrg_ascension_get_hp_reduction (asc0), ==, 0);
    g_assert_cmpint (lrg_ascension_get_modifiers (asc0), ==, LRG_ASCENSION_MODIFIER_NONE);

    /* Level 5 has some modifiers */
    asc5 = lrg_ascension_new_default (5);
    g_assert_cmpint (lrg_ascension_get_level (asc5), ==, 5);
    /* A5 typically adds heal reduction */
    g_assert_cmpint (lrg_ascension_get_heal_reduction_percent (asc5), >, 0);

    /* Level 20 = maximum difficulty */
    asc20 = lrg_ascension_new_default (20);
    g_assert_cmpint (lrg_ascension_get_level (asc20), ==, 20);
    /* Should have significant HP reduction */
    g_assert_cmpint (lrg_ascension_get_hp_reduction (asc20), >, 0);
}

static void
test_ascension_modifiers (void)
{
    g_autoptr(LrgAscension) asc = NULL;

    asc = lrg_ascension_new (10);

    /* Add modifiers */
    lrg_ascension_add_modifier (asc, LRG_ASCENSION_MODIFIER_HARDER_ELITES);
    lrg_ascension_add_modifier (asc, LRG_ASCENSION_MODIFIER_HARDER_BOSSES);

    g_assert_true (lrg_ascension_has_modifier (asc, LRG_ASCENSION_MODIFIER_HARDER_ELITES));
    g_assert_true (lrg_ascension_has_modifier (asc, LRG_ASCENSION_MODIFIER_HARDER_BOSSES));
    g_assert_false (lrg_ascension_has_modifier (asc, LRG_ASCENSION_MODIFIER_CURSES));
}

static void
test_ascension_numeric_modifiers (void)
{
    g_autoptr(LrgAscension) asc = NULL;

    asc = lrg_ascension_new (15);

    lrg_ascension_set_hp_reduction (asc, 10);
    g_assert_cmpint (lrg_ascension_get_hp_reduction (asc), ==, 10);

    lrg_ascension_set_gold_reduction (asc, 20);
    g_assert_cmpint (lrg_ascension_get_gold_reduction (asc), ==, 20);

    lrg_ascension_set_heal_reduction_percent (asc, 25);
    g_assert_cmpint (lrg_ascension_get_heal_reduction_percent (asc), ==, 25);

    lrg_ascension_set_enemy_hp_increase_percent (asc, 15);
    g_assert_cmpint (lrg_ascension_get_enemy_hp_increase_percent (asc), ==, 15);

    lrg_ascension_set_enemy_damage_increase_percent (asc, 10);
    g_assert_cmpint (lrg_ascension_get_enemy_damage_increase_percent (asc), ==, 10);
}

static void
test_ascension_apply_hp (void)
{
    g_autoptr(LrgAscension) asc = NULL;
    gint modified;

    asc = lrg_ascension_new (5);
    lrg_ascension_set_hp_reduction (asc, 10);

    modified = lrg_ascension_apply_hp (asc, 80);
    g_assert_cmpint (modified, ==, 70);  /* 80 - 10 = 70 */
}

static void
test_ascension_apply_gold (void)
{
    g_autoptr(LrgAscension) asc = NULL;
    gint modified;

    asc = lrg_ascension_new (10);
    lrg_ascension_set_gold_reduction (asc, 20);

    modified = lrg_ascension_apply_gold (asc, 99);
    g_assert_cmpint (modified, ==, 79);  /* 99 - 20 = 79 */
}

static void
test_ascension_apply_heal (void)
{
    g_autoptr(LrgAscension) asc = NULL;
    gint modified;

    asc = lrg_ascension_new (5);
    lrg_ascension_set_heal_reduction_percent (asc, 25);

    modified = lrg_ascension_apply_heal (asc, 40);
    g_assert_cmpint (modified, ==, 30);  /* 40 - 25% = 30 */
}

static void
test_ascension_apply_enemy_hp (void)
{
    g_autoptr(LrgAscension) asc = NULL;
    gint modified;

    asc = lrg_ascension_new (17);
    lrg_ascension_set_enemy_hp_increase_percent (asc, 50);

    modified = lrg_ascension_apply_enemy_hp (asc, 100);
    g_assert_cmpint (modified, ==, 150);  /* 100 + 50% = 150 */
}

static void
test_ascension_name (void)
{
    g_autoptr(LrgAscension) asc0 = NULL;
    g_autoptr(LrgAscension) asc10 = NULL;

    asc0 = lrg_ascension_new (0);
    g_assert_cmpstr (lrg_ascension_get_name (asc0), ==, "Normal");

    asc10 = lrg_ascension_new (10);
    g_assert_cmpstr (lrg_ascension_get_name (asc10), ==, "Ascension 10");
}

/* --- LrgDeckbuilderManager tests --- */

static void
test_deckbuilder_manager_singleton (void)
{
    LrgDeckbuilderManager *mgr1;
    LrgDeckbuilderManager *mgr2;

    mgr1 = lrg_deckbuilder_manager_get_default ();
    mgr2 = lrg_deckbuilder_manager_get_default ();

    g_assert_nonnull (mgr1);
    g_assert_true (mgr1 == mgr2);
}

static void
test_deckbuilder_manager_profile (void)
{
    LrgDeckbuilderManager *mgr;
    g_autoptr(LrgPlayerProfile) profile = NULL;
    LrgPlayerProfile *current;

    mgr = lrg_deckbuilder_manager_get_default ();

    /* Create and set a profile */
    profile = lrg_player_profile_new ("ManagerTest");
    lrg_deckbuilder_manager_set_profile (mgr, profile);

    current = lrg_deckbuilder_manager_get_profile (mgr);
    g_assert_true (current == profile);
}

static void
test_deckbuilder_manager_characters (void)
{
    LrgDeckbuilderManager *mgr;
    g_autoptr(LrgCharacterDef) ironclad = NULL;
    g_autoptr(LrgCharacterDef) silent = NULL;
    LrgCharacterDef *found;
    GPtrArray *all;

    mgr = lrg_deckbuilder_manager_get_default ();

    /* Register characters */
    ironclad = lrg_character_def_new ("test-ironclad", "Test Ironclad");
    silent = lrg_character_def_new ("test-silent", "Test Silent");

    lrg_deckbuilder_manager_register_character (mgr, ironclad);
    lrg_deckbuilder_manager_register_character (mgr, silent);

    /* Lookup */
    found = lrg_deckbuilder_manager_get_character (mgr, "test-ironclad");
    g_assert_true (found == ironclad);

    found = lrg_deckbuilder_manager_get_character (mgr, "nonexistent");
    g_assert_null (found);

    /* Get all */
    all = lrg_deckbuilder_manager_get_characters (mgr);
    g_assert_cmpuint (all->len, >=, 2);
}

static void
test_deckbuilder_manager_unlocks (void)
{
    LrgDeckbuilderManager *mgr;
    g_autoptr(LrgUnlockDef) unlock = NULL;
    LrgUnlockDef *found;

    mgr = lrg_deckbuilder_manager_get_default ();

    /* Register an unlock */
    unlock = lrg_unlock_def_new ("test-unlock-watcher", LRG_UNLOCK_TYPE_CHARACTER, "test-watcher");
    lrg_unlock_def_set_win_count (unlock, NULL, 1);

    lrg_deckbuilder_manager_register_unlock (mgr, unlock);

    /* Lookup */
    found = lrg_deckbuilder_manager_get_unlock (mgr, "test-unlock-watcher");
    g_assert_true (found == unlock);
}

static void
test_deckbuilder_manager_ascension (void)
{
    LrgDeckbuilderManager *mgr;
    LrgAscension *asc5;
    LrgAscension *asc10;

    mgr = lrg_deckbuilder_manager_get_default ();

    asc5 = lrg_deckbuilder_manager_get_ascension (mgr, 5);
    g_assert_nonnull (asc5);
    g_assert_cmpint (lrg_ascension_get_level (asc5), ==, 5);

    asc10 = lrg_deckbuilder_manager_get_ascension (mgr, 10);
    g_assert_nonnull (asc10);
    g_assert_cmpint (lrg_ascension_get_level (asc10), ==, 10);

    /* Same level returns same object */
    g_assert_true (lrg_deckbuilder_manager_get_ascension (mgr, 5) == asc5);
}

static void
test_deckbuilder_manager_run (void)
{
    LrgDeckbuilderManager *mgr;
    g_autoptr(LrgCharacterDef) character = NULL;
    g_autoptr(LrgPlayerProfile) profile = NULL;
    LrgRun *run;

    mgr = lrg_deckbuilder_manager_get_default ();

    /* Need a character and profile to start a run */
    character = lrg_character_def_new ("run-test-char", "Run Test Character");
    lrg_character_def_set_unlocked_by_default (character, TRUE);
    lrg_deckbuilder_manager_register_character (mgr, character);

    profile = lrg_player_profile_new ("RunTest");
    lrg_player_profile_unlock (profile, LRG_UNLOCK_TYPE_CHARACTER, "run-test-char");
    lrg_deckbuilder_manager_set_profile (mgr, profile);

    /* Initially no run */
    g_assert_null (lrg_deckbuilder_manager_get_current_run (mgr));

    /* Start a run */
    run = lrg_deckbuilder_manager_start_run (mgr, "run-test-char", 0, NULL);
    g_assert_nonnull (run);
    g_assert_true (lrg_deckbuilder_manager_get_current_run (mgr) == run);

    /* End the run */
    lrg_deckbuilder_manager_end_run (mgr, TRUE);
    g_assert_null (lrg_deckbuilder_manager_get_current_run (mgr));
}

static void
test_deckbuilder_manager_statistics (void)
{
    LrgDeckbuilderManager *mgr;
    g_autoptr(LrgPlayerProfile) profile = NULL;
    gfloat rate;

    mgr = lrg_deckbuilder_manager_get_default ();

    profile = lrg_player_profile_new ("StatsTest");
    lrg_player_profile_add_character_run (profile, "ironclad");
    lrg_player_profile_add_character_run (profile, "ironclad");
    lrg_player_profile_add_character_run (profile, "ironclad");
    lrg_player_profile_add_character_win (profile, "ironclad");
    lrg_deckbuilder_manager_set_profile (mgr, profile);

    /* Get stats through manager */
    g_assert_cmpint (lrg_deckbuilder_manager_get_run_count (mgr, "ironclad"), ==, 3);
    g_assert_cmpint (lrg_deckbuilder_manager_get_win_count (mgr, "ironclad"), ==, 1);

    /* Win rate = 1/3 ≈ 33.3% */
    rate = lrg_deckbuilder_manager_get_win_rate (mgr, "ironclad");
    g_assert_cmpfloat (rate, >, 33.0);
    g_assert_cmpfloat (rate, <, 34.0);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* LrgCardDef tests */
    g_test_add_func ("/deckbuilder/card-def/new", test_card_def_new);
    g_test_add ("/deckbuilder/card-def/properties", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_def_properties, deckbuilder_fixture_tear_down);
    g_test_add_func ("/deckbuilder/card-def/keywords", test_card_def_keywords);
    g_test_add_func ("/deckbuilder/card-def/upgrade", test_card_def_upgrade);
    g_test_add_func ("/deckbuilder/card-def/scoring", test_card_def_scoring);

    /* LrgCardInstance tests */
    g_test_add ("/deckbuilder/card-instance/new", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_instance_new, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/card-instance/upgrade", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_instance_upgrade, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/card-instance/zone", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_instance_zone, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/card-instance/cost-modifier", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_instance_cost_modifier, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/card-instance/temporary-keywords", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_instance_temporary_keywords, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/card-instance/combined-keywords", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_instance_combined_keywords, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/card-instance/play-count", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_instance_play_count, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/card-instance/bonus-chips", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_instance_bonus_chips, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/card-instance/unique-ids", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_instance_unique_ids, deckbuilder_fixture_tear_down);

    /* LrgCardPile tests */
    g_test_add_func ("/deckbuilder/card-pile/new", test_card_pile_new);
    g_test_add_func ("/deckbuilder/card-pile/new-with-zone", test_card_pile_new_with_zone);
    g_test_add ("/deckbuilder/card-pile/add-draw", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_pile_add_draw, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/card-pile/add-bottom", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_pile_add_bottom, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/card-pile/draw-bottom", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_pile_draw_bottom, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/card-pile/peek", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_pile_peek, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/card-pile/peek-n", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_pile_peek_n, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/card-pile/shuffle", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_pile_shuffle, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/card-pile/contains", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_pile_contains, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/card-pile/remove", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_pile_remove, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/card-pile/find-by-id", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_pile_find_by_id, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/card-pile/find-by-type", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_pile_find_by_type, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/card-pile/transfer-all", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_pile_transfer_all, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/card-pile/clear", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_card_pile_clear, deckbuilder_fixture_tear_down);

    /* LrgHand tests */
    g_test_add_func ("/deckbuilder/hand/new", test_hand_new);
    g_test_add_func ("/deckbuilder/hand/new-with-size", test_hand_new_with_size);
    g_test_add ("/deckbuilder/hand/add", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_hand_add, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/hand/add-full", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_hand_add_full, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/hand/remove", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_hand_remove, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/hand/discard", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_hand_discard, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/hand/discard-retain", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_hand_discard_retain, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/hand/discard-all", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_hand_discard_all, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/hand/find-by-id", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_hand_find_by_id, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/hand/find-playable", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_hand_find_playable, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/hand/selection", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_hand_selection, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/hand/sort-by-cost", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_hand_sort_by_cost, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/hand/sort-by-type", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_hand_sort_by_type, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/hand/get-index-of", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_hand_get_index_of, deckbuilder_fixture_tear_down);

    /* LrgDeckCardEntry tests (Phase 2) */
    g_test_add ("/deckbuilder/deck-card-entry/new", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_card_entry_new, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-card-entry/copy", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_card_entry_copy, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-card-entry/set-count", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_card_entry_set_count, deckbuilder_fixture_tear_down);

    /* LrgDeckDef tests (Phase 2) */
    g_test_add_func ("/deckbuilder/deck-def/new", test_deck_def_new);
    g_test_add_func ("/deckbuilder/deck-def/properties", test_deck_def_properties);
    g_test_add ("/deckbuilder/deck-def/starting-cards", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_def_starting_cards, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-def/remove-starting-card", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_def_remove_starting_card, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-def/allowed-types", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_def_allowed_types, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-def/banned-cards", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_def_banned_cards, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-def/can-add-card", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_def_can_add_card, deckbuilder_fixture_tear_down);

    /* LrgDeckInstance tests (Phase 2) */
    g_test_add ("/deckbuilder/deck-instance/new", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_instance_new, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-instance/new-with-seed", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_instance_new_with_seed, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-instance/piles", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_instance_piles, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-instance/setup", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_instance_setup, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-instance/shuffle", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_instance_shuffle, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-instance/draw-card", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_instance_draw_card, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-instance/draw-cards", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_instance_draw_cards, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-instance/shuffle-discard-into-draw", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_instance_shuffle_discard_into_draw, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-instance/add-card", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_instance_add_card, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-instance/remove-card", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_instance_remove_card, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-instance/count-card-def", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_instance_count_card_def, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-instance/master-deck", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_instance_master_deck, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-instance/end-combat", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_instance_end_combat, deckbuilder_fixture_tear_down);

    /* LrgDeckBuilder tests (Phase 2) */
    g_test_add_func ("/deckbuilder/deck-builder/new", test_deck_builder_new);
    g_test_add ("/deckbuilder/deck-builder/new-with-def", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_builder_new_with_def, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-builder/set-max-copies", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_builder_set_max_copies, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-builder/can-add-card", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_builder_can_add_card, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-builder/can-add-banned-card", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_builder_can_add_banned_card, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-builder/can-add-wrong-type", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_builder_can_add_wrong_type, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-builder/add-card", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_builder_add_card, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-builder/remove-card", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_builder_remove_card, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-builder/validate-deck", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_builder_validate_deck, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-builder/validate-deck-too-small", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_builder_validate_deck_too_small, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-builder/validate-deck-too-large", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_builder_validate_deck_too_large, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-builder/build", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_builder_build, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-builder/build-with-seed", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_builder_build_with_seed, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-builder/upgrade-card", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_builder_upgrade_card, deckbuilder_fixture_tear_down);
    g_test_add ("/deckbuilder/deck-builder/transform-card", DeckbuilderFixture, NULL,
                deckbuilder_fixture_set_up, test_deck_builder_transform_card, deckbuilder_fixture_tear_down);

    /* Phase 3: Effect System Tests */
    g_test_add_func ("/deckbuilder/card-effect/new", test_card_effect_new);
    g_test_add_func ("/deckbuilder/card-effect/params", test_card_effect_params);
    g_test_add_func ("/deckbuilder/card-effect/flags", test_card_effect_flags);
    g_test_add_func ("/deckbuilder/card-effect/priority", test_card_effect_priority);
    g_test_add_func ("/deckbuilder/card-effect/copy", test_card_effect_copy);
    g_test_add_func ("/deckbuilder/effect-registry/singleton", test_effect_registry_singleton);
    g_test_add_func ("/deckbuilder/effect-stack/new", test_effect_stack_new);
    g_test_add_func ("/deckbuilder/effect-stack/push-pop", test_effect_stack_push_pop);
    g_test_add_func ("/deckbuilder/effect-stack/priority-order", test_effect_stack_priority_order);

    /* Phase 3.5: Trigger/Event System Tests */
    g_test_add_func ("/deckbuilder/card-event/new", test_card_event_new);
    g_test_add_func ("/deckbuilder/card-event/damage", test_card_event_damage);
    g_test_add_func ("/deckbuilder/card-event/block", test_card_event_block);
    g_test_add_func ("/deckbuilder/card-event/status", test_card_event_status);
    g_test_add_func ("/deckbuilder/card-event/cancel", test_card_event_cancel);
    g_test_add_func ("/deckbuilder/card-event/copy", test_card_event_copy);
    g_test_add_func ("/deckbuilder/event-bus/new", test_event_bus_new);
    g_test_add_func ("/deckbuilder/event-bus/singleton", test_event_bus_singleton);
    g_test_add_func ("/deckbuilder/event-bus/emit-no-listeners", test_event_bus_emit_no_listeners);
    g_test_add_func ("/deckbuilder/trigger-listener/mask", test_trigger_listener_mask);

    /* Phase 4: Keyword System Tests */
    g_test_add_func ("/deckbuilder/card-keyword/get-name", test_card_keyword_get_name);
    g_test_add_func ("/deckbuilder/card-keyword/get-description", test_card_keyword_get_description);
    g_test_add_func ("/deckbuilder/card-keyword/is-positive", test_card_keyword_is_positive);
    g_test_add_func ("/deckbuilder/card-keyword/is-negative", test_card_keyword_is_negative);
    g_test_add_func ("/deckbuilder/card-keyword/from-string", test_card_keyword_from_string);
    g_test_add_func ("/deckbuilder/card-keyword/to-string", test_card_keyword_to_string);
    g_test_add_func ("/deckbuilder/card-keywords/from-string", test_card_keywords_from_string);
    g_test_add_func ("/deckbuilder/card-keywords/to-string", test_card_keywords_to_string);
    g_test_add_func ("/deckbuilder/card-keyword/count", test_card_keyword_count);
    g_test_add_func ("/deckbuilder/card-keyword-def/new", test_card_keyword_def_new);
    g_test_add_func ("/deckbuilder/card-keyword-def/properties", test_card_keyword_def_properties);
    g_test_add_func ("/deckbuilder/card-keyword-registry/singleton", test_card_keyword_registry_singleton);
    g_test_add_func ("/deckbuilder/card-keyword-registry/register", test_card_keyword_registry_register);
    g_test_add_func ("/deckbuilder/card-keyword-registry/lookup", test_card_keyword_registry_lookup);
    g_test_add_func ("/deckbuilder/card-keyword-registry/unregister", test_card_keyword_registry_unregister);
    g_test_add_func ("/deckbuilder/synergy/new", test_synergy_new);
    g_test_add_func ("/deckbuilder/synergy/new-keyword", test_synergy_new_keyword);
    g_test_add_func ("/deckbuilder/synergy/new-card-type", test_synergy_new_card_type);
    g_test_add_func ("/deckbuilder/synergy/new-tag", test_synergy_new_tag);
    g_test_add_func ("/deckbuilder/synergy/properties", test_synergy_properties);
    g_test_add_func ("/deckbuilder/synergy/check-cards-empty", test_synergy_check_cards_empty);
    g_test_add_func ("/deckbuilder/synergy/get-synergy-cards-empty", test_synergy_get_synergy_cards_empty);

    /* Phase 5: Status Effect System Tests */
    g_test_add_func ("/deckbuilder/status-effect-def/new", test_status_effect_def_new);
    g_test_add_func ("/deckbuilder/status-effect-def/properties", test_status_effect_def_properties);
    g_test_add_func ("/deckbuilder/status-effect-def/flags", test_status_effect_def_flags);
    g_test_add_func ("/deckbuilder/status-effect-def/tooltip", test_status_effect_def_tooltip);
    g_test_add_func ("/deckbuilder/status-effect-instance/new", test_status_effect_instance_new);
    g_test_add_func ("/deckbuilder/status-effect-instance/stacks", test_status_effect_instance_stacks);
    g_test_add_func ("/deckbuilder/status-effect-instance/max-stacks", test_status_effect_instance_max_stacks);
    g_test_add_func ("/deckbuilder/status-effect-instance/expired", test_status_effect_instance_expired);
    g_test_add_func ("/deckbuilder/status-effect-instance/copy", test_status_effect_instance_copy);
    g_test_add_func ("/deckbuilder/status-effect-instance/convenience", test_status_effect_instance_convenience);
    g_test_add_func ("/deckbuilder/status-effect-registry/singleton", test_status_effect_registry_singleton);
    g_test_add_func ("/deckbuilder/status-effect-registry/register", test_status_effect_registry_register);
    g_test_add_func ("/deckbuilder/status-effect-registry/lookup", test_status_effect_registry_lookup);
    g_test_add_func ("/deckbuilder/status-effect-registry/unregister", test_status_effect_registry_unregister);
    g_test_add_func ("/deckbuilder/status-effect-registry/create-instance", test_status_effect_registry_create_instance);
    g_test_add_func ("/deckbuilder/status-effect-registry/get-buffs-debuffs", test_status_effect_registry_get_buffs_debuffs);

    /* Phase 5.5: Relic & Potion System Tests */
    g_test_add_func ("/deckbuilder/relic-def/new", test_relic_def_new);
    g_test_add_func ("/deckbuilder/relic-def/properties", test_relic_def_properties);
    g_test_add_func ("/deckbuilder/relic-def/trigger-flags", test_relic_def_trigger_flags);
    g_test_add_func ("/deckbuilder/relic-instance/new", test_relic_instance_new);
    g_test_add_func ("/deckbuilder/relic-instance/counter", test_relic_instance_counter);
    g_test_add_func ("/deckbuilder/relic-instance/enabled", test_relic_instance_enabled);
    g_test_add_func ("/deckbuilder/relic-instance/data", test_relic_instance_data);
    g_test_add_func ("/deckbuilder/relic-registry/singleton", test_relic_registry_singleton);
    g_test_add_func ("/deckbuilder/relic-registry/register", test_relic_registry_register);
    g_test_add_func ("/deckbuilder/relic-registry/lookup", test_relic_registry_lookup);
    g_test_add_func ("/deckbuilder/relic-registry/create-instance", test_relic_registry_create_instance);
    g_test_add_func ("/deckbuilder/relic-registry/get-by-rarity", test_relic_registry_get_by_rarity);
    g_test_add_func ("/deckbuilder/potion-def/new", test_potion_def_new);
    g_test_add_func ("/deckbuilder/potion-def/properties", test_potion_def_properties);
    g_test_add_func ("/deckbuilder/potion-instance/new", test_potion_instance_new);
    g_test_add_func ("/deckbuilder/potion-instance/use", test_potion_instance_use);
    g_test_add_func ("/deckbuilder/potion-instance/discard", test_potion_instance_discard);
    g_test_add_func ("/deckbuilder/potion-instance/convenience", test_potion_instance_convenience);

    /* LrgEnemyIntent tests */
    g_test_add_func ("/deckbuilder/enemy-intent/new-attack", test_enemy_intent_new_attack);
    g_test_add_func ("/deckbuilder/enemy-intent/new-attack-multi", test_enemy_intent_new_attack_multi);
    g_test_add_func ("/deckbuilder/enemy-intent/new-defend", test_enemy_intent_new_defend);
    g_test_add_func ("/deckbuilder/enemy-intent/new-buff", test_enemy_intent_new_buff);
    g_test_add_func ("/deckbuilder/enemy-intent/new-debuff", test_enemy_intent_new_debuff);
    g_test_add_func ("/deckbuilder/enemy-intent/copy", test_enemy_intent_copy);

    /* LrgEnemyDef tests */
    g_test_add_func ("/deckbuilder/enemy-def/new", test_enemy_def_new);
    g_test_add_func ("/deckbuilder/enemy-def/properties", test_enemy_def_properties);
    g_test_add_func ("/deckbuilder/enemy-def/intent-patterns", test_enemy_def_intent_patterns);

    /* LrgEnemyInstance tests */
    g_test_add_func ("/deckbuilder/enemy-instance/new", test_enemy_instance_new);
    g_test_add_func ("/deckbuilder/enemy-instance/combatant-interface", test_enemy_instance_combatant_interface);
    g_test_add_func ("/deckbuilder/enemy-instance/take-damage", test_enemy_instance_take_damage);
    g_test_add_func ("/deckbuilder/enemy-instance/take-damage-with-block", test_enemy_instance_take_damage_with_block);
    g_test_add_func ("/deckbuilder/enemy-instance/heal", test_enemy_instance_heal);
    g_test_add_func ("/deckbuilder/enemy-instance/death", test_enemy_instance_death);
    g_test_add_func ("/deckbuilder/enemy-instance/intent", test_enemy_instance_intent);

    /* LrgPlayerCombatant tests */
    g_test_add_func ("/deckbuilder/player-combatant/new", test_player_combatant_new);
    g_test_add_func ("/deckbuilder/player-combatant/interface", test_player_combatant_interface);
    g_test_add_func ("/deckbuilder/player-combatant/damage-and-block", test_player_combatant_damage_and_block);
    g_test_add_func ("/deckbuilder/player-combatant/gold", test_player_combatant_gold);

    /* LrgCombatContext tests */
    g_test_add_func ("/deckbuilder/combat-context/new", test_combat_context_new);
    g_test_add_func ("/deckbuilder/combat-context/energy", test_combat_context_energy);
    g_test_add_func ("/deckbuilder/combat-context/enemies", test_combat_context_enemies);
    g_test_add_func ("/deckbuilder/combat-context/card-piles", test_combat_context_card_piles);
    g_test_add_func ("/deckbuilder/combat-context/turn", test_combat_context_turn);
    g_test_add_func ("/deckbuilder/combat-context/cards-played", test_combat_context_cards_played);
    g_test_add_func ("/deckbuilder/combat-context/variables", test_combat_context_variables);
    g_test_add_func ("/deckbuilder/combat-context/rng", test_combat_context_rng);

    /* LrgCombatManager tests */
    g_test_add_func ("/deckbuilder/combat-manager/new", test_combat_manager_new);
    g_test_add_func ("/deckbuilder/combat-manager/start-combat", test_combat_manager_start_combat);
    g_test_add_func ("/deckbuilder/combat-manager/end-combat", test_combat_manager_end_combat);
    g_test_add_func ("/deckbuilder/combat-manager/victory-check", test_combat_manager_victory_check);
    g_test_add_func ("/deckbuilder/combat-manager/defeat-check", test_combat_manager_defeat_check);

    /* Phase 6.5: Run/Map System Tests */
    g_test_add_func ("/deckbuilder/map-node/new", test_map_node_new);
    g_test_add_func ("/deckbuilder/map-node/types", test_map_node_types);
    g_test_add_func ("/deckbuilder/map-node/connections", test_map_node_connections);
    g_test_add_func ("/deckbuilder/map-node/visited", test_map_node_visited);
    g_test_add_func ("/deckbuilder/map-node/encounter", test_map_node_encounter);
    g_test_add_func ("/deckbuilder/map-node/position", test_map_node_position);
    g_test_add_func ("/deckbuilder/run-map/new", test_run_map_new);
    g_test_add_func ("/deckbuilder/run-map/generate", test_run_map_generate);
    g_test_add_func ("/deckbuilder/run-map/starting-nodes", test_run_map_starting_nodes);
    g_test_add_func ("/deckbuilder/run-map/boss-node", test_run_map_boss_node);
    g_test_add_func ("/deckbuilder/run-map/connections", test_run_map_connections);
    g_test_add_func ("/deckbuilder/run-map/lookup", test_run_map_lookup);
    g_test_add_func ("/deckbuilder/run/new", test_run_new);
    g_test_add_func ("/deckbuilder/run/gold", test_run_gold);
    g_test_add_func ("/deckbuilder/run/state", test_run_state);
    g_test_add_func ("/deckbuilder/run/relics", test_run_relics);
    g_test_add_func ("/deckbuilder/run/potions", test_run_potions);
    g_test_add_func ("/deckbuilder/run/potions-max", test_run_potions_max);
    g_test_add_func ("/deckbuilder/run/statistics", test_run_statistics);
    g_test_add_func ("/deckbuilder/run-manager/singleton", test_run_manager_singleton);
    g_test_add_func ("/deckbuilder/run-manager/start-run", test_run_manager_start_run);
    g_test_add_func ("/deckbuilder/run-manager/end-run", test_run_manager_end_run);
    g_test_add_func ("/deckbuilder/run-manager/map-generation", test_run_manager_map_generation);
    g_test_add_func ("/deckbuilder/run-manager/valid-moves", test_run_manager_valid_moves);
    g_test_add_func ("/deckbuilder/run-manager/select-node", test_run_manager_select_node);

    /* Phase 6.6: Scoring System Tests */
    g_test_add_func ("/deckbuilder/scoring-rules/interface", test_scoring_rules_interface);
    g_test_add_func ("/deckbuilder/scoring-hand/new", test_scoring_hand_new);
    g_test_add_func ("/deckbuilder/scoring-hand/high-card", test_scoring_hand_high_card);
    g_test_add_func ("/deckbuilder/scoring-hand/pair", test_scoring_hand_pair);
    g_test_add_func ("/deckbuilder/scoring-hand/two-pair", test_scoring_hand_two_pair);
    g_test_add_func ("/deckbuilder/scoring-hand/three-of-a-kind", test_scoring_hand_three_of_a_kind);
    g_test_add_func ("/deckbuilder/scoring-hand/straight", test_scoring_hand_straight);
    g_test_add_func ("/deckbuilder/scoring-hand/flush", test_scoring_hand_flush);
    g_test_add_func ("/deckbuilder/scoring-hand/full-house", test_scoring_hand_full_house);
    g_test_add_func ("/deckbuilder/scoring-hand/four-of-a-kind", test_scoring_hand_four_of_a_kind);
    g_test_add_func ("/deckbuilder/scoring-hand/straight-flush", test_scoring_hand_straight_flush);
    g_test_add_func ("/deckbuilder/scoring-hand/chip-values", test_scoring_hand_chip_values);
    g_test_add_func ("/deckbuilder/scoring-context/new", test_scoring_context_new);
    g_test_add_func ("/deckbuilder/scoring-context/chips", test_scoring_context_chips);
    g_test_add_func ("/deckbuilder/scoring-context/mult", test_scoring_context_mult);
    g_test_add_func ("/deckbuilder/scoring-context/x-mult", test_scoring_context_x_mult);
    g_test_add_func ("/deckbuilder/scoring-context/score-calculation", test_scoring_context_score_calculation);
    g_test_add_func ("/deckbuilder/scoring-context/reset", test_scoring_context_reset);
    g_test_add_func ("/deckbuilder/joker-def/new", test_joker_def_new);
    g_test_add_func ("/deckbuilder/joker-def/properties", test_joker_def_properties);
    g_test_add_func ("/deckbuilder/joker-def/bonuses", test_joker_def_bonuses);
    g_test_add_func ("/deckbuilder/joker-def/conditions", test_joker_def_conditions);
    g_test_add_func ("/deckbuilder/joker-instance/new", test_joker_instance_new);
    g_test_add_func ("/deckbuilder/joker-instance/edition", test_joker_instance_edition);
    g_test_add_func ("/deckbuilder/joker-instance/edition-holographic", test_joker_instance_edition_holographic);
    g_test_add_func ("/deckbuilder/joker-instance/edition-polychrome", test_joker_instance_edition_polychrome);
    g_test_add_func ("/deckbuilder/joker-instance/counter", test_joker_instance_counter);
    g_test_add_func ("/deckbuilder/joker-instance/trigger-count", test_joker_instance_trigger_count);
    g_test_add_func ("/deckbuilder/joker-instance/sell-value", test_joker_instance_sell_value);
    g_test_add_func ("/deckbuilder/scoring-manager/new", test_scoring_manager_new);
    g_test_add_func ("/deckbuilder/scoring-manager/singleton", test_scoring_manager_singleton);
    g_test_add_func ("/deckbuilder/scoring-manager/round", test_scoring_manager_round);
    g_test_add_func ("/deckbuilder/scoring-manager/jokers", test_scoring_manager_jokers);
    g_test_add_func ("/deckbuilder/scoring-manager/phase", test_scoring_manager_phase);
    g_test_add_func ("/deckbuilder/scoring-manager/evaluate-hand", test_scoring_manager_evaluate_hand);
    g_test_add_func ("/deckbuilder/scoring-manager/preview-score", test_scoring_manager_preview_score);
    g_test_add_func ("/deckbuilder/scoring-manager/play-hand", test_scoring_manager_play_hand);
    g_test_add_func ("/deckbuilder/scoring-manager/discard", test_scoring_manager_discard);
    g_test_add_func ("/deckbuilder/scoring-manager/rules", test_scoring_manager_rules);

    /* Phase 7: Meta-Progression Tests */

    /* LrgCharacterDef tests */
    g_test_add_func ("/deckbuilder/character-def/new", test_character_def_new);
    g_test_add_func ("/deckbuilder/character-def/properties", test_character_def_properties);
    g_test_add_func ("/deckbuilder/character-def/starting-deck", test_character_def_starting_deck);
    g_test_add_func ("/deckbuilder/character-def/starting-relic", test_character_def_starting_relic);
    g_test_add_func ("/deckbuilder/character-def/unlock", test_character_def_unlock);

    /* LrgPlayerProfile tests */
    g_test_add_func ("/deckbuilder/player-profile/new", test_player_profile_new);
    g_test_add_func ("/deckbuilder/player-profile/singleton", test_player_profile_singleton);
    g_test_add_func ("/deckbuilder/player-profile/unlocks", test_player_profile_unlocks);
    g_test_add_func ("/deckbuilder/player-profile/unlock-status", test_player_profile_unlock_status);
    g_test_add_func ("/deckbuilder/player-profile/character-progress", test_player_profile_character_progress);
    g_test_add_func ("/deckbuilder/player-profile/ascension", test_player_profile_ascension);
    g_test_add_func ("/deckbuilder/player-profile/statistics", test_player_profile_statistics);
    g_test_add_func ("/deckbuilder/player-profile/high-score", test_player_profile_high_score);
    g_test_add_func ("/deckbuilder/player-profile/dirty", test_player_profile_dirty);

    /* LrgUnlockDef tests */
    g_test_add_func ("/deckbuilder/unlock-def/new", test_unlock_def_new);
    g_test_add_func ("/deckbuilder/unlock-def/properties", test_unlock_def_properties);
    g_test_add_func ("/deckbuilder/unlock-def/win-condition", test_unlock_def_win_condition);
    g_test_add_func ("/deckbuilder/unlock-def/grant", test_unlock_def_grant);

    /* LrgAscension tests */
    g_test_add_func ("/deckbuilder/ascension/new", test_ascension_new);
    g_test_add_func ("/deckbuilder/ascension/defaults", test_ascension_defaults);
    g_test_add_func ("/deckbuilder/ascension/modifiers", test_ascension_modifiers);
    g_test_add_func ("/deckbuilder/ascension/numeric-modifiers", test_ascension_numeric_modifiers);
    g_test_add_func ("/deckbuilder/ascension/apply-hp", test_ascension_apply_hp);
    g_test_add_func ("/deckbuilder/ascension/apply-gold", test_ascension_apply_gold);
    g_test_add_func ("/deckbuilder/ascension/apply-heal", test_ascension_apply_heal);
    g_test_add_func ("/deckbuilder/ascension/apply-enemy-hp", test_ascension_apply_enemy_hp);
    g_test_add_func ("/deckbuilder/ascension/name", test_ascension_name);

    /* LrgDeckbuilderManager tests */
    g_test_add_func ("/deckbuilder/deckbuilder-manager/singleton", test_deckbuilder_manager_singleton);
    g_test_add_func ("/deckbuilder/deckbuilder-manager/profile", test_deckbuilder_manager_profile);
    g_test_add_func ("/deckbuilder/deckbuilder-manager/characters", test_deckbuilder_manager_characters);
    g_test_add_func ("/deckbuilder/deckbuilder-manager/unlocks", test_deckbuilder_manager_unlocks);
    g_test_add_func ("/deckbuilder/deckbuilder-manager/ascension", test_deckbuilder_manager_ascension);
    g_test_add_func ("/deckbuilder/deckbuilder-manager/run", test_deckbuilder_manager_run);
    g_test_add_func ("/deckbuilder/deckbuilder-manager/statistics", test_deckbuilder_manager_statistics);

    return g_test_run ();
}
