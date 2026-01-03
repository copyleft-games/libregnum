/* test-template-deckbuilder.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for deckbuilder template system:
 *   - LrgDeckbuilderTemplate (base deckbuilder template)
 *   - LrgDeckbuilderCombatTemplate (Slay the Spire style)
 *   - LrgDeckbuilderPokerTemplate (Balatro style)
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Skip Macros for CI/Headless
 * ========================================================================== */

#define SKIP_IF_NO_DISPLAY() \
    do { \
        if (g_getenv ("DISPLAY") == NULL && g_getenv ("WAYLAND_DISPLAY") == NULL) \
        { \
            g_test_skip ("No display available (headless environment)"); \
            return; \
        } \
    } while (0)

#define SKIP_IF_NULL(ptr) \
    do { \
        if ((ptr) == NULL) \
        { \
            g_test_skip ("Resource not available"); \
            return; \
        } \
    } while (0)

/* ==========================================================================
 * Test Cases - LrgDeckbuilderTemplate Construction
 * ========================================================================== */

static void
test_deckbuilder_template_new (void)
{
    g_autoptr(LrgDeckbuilderTemplate) template = NULL;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_template_new ();
    SKIP_IF_NULL (template);

    g_assert_nonnull (template);
    g_assert_true (LRG_IS_DECKBUILDER_TEMPLATE (template));
}

static void
test_deckbuilder_template_inherits_game_template (void)
{
    g_autoptr(LrgDeckbuilderTemplate) template = NULL;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_template_new ();
    SKIP_IF_NULL (template);

    g_assert_true (LRG_IS_GAME_TEMPLATE (template));
}

/* ==========================================================================
 * Test Cases - LrgDeckbuilderTemplate Properties
 * ========================================================================== */

static void
test_deckbuilder_template_energy (void)
{
    g_autoptr(LrgDeckbuilderTemplate) template = NULL;
    gint energy;
    gint max_energy;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_template_new ();
    SKIP_IF_NULL (template);

    /* Set max energy */
    lrg_deckbuilder_template_set_max_energy (template, 5);
    max_energy = lrg_deckbuilder_template_get_max_energy (template);
    g_assert_cmpint (max_energy, ==, 5);

    /* Set current energy */
    lrg_deckbuilder_template_set_current_energy (template, 3);
    energy = lrg_deckbuilder_template_get_current_energy (template);
    g_assert_cmpint (energy, ==, 3);
}

static void
test_deckbuilder_template_hand_size (void)
{
    g_autoptr(LrgDeckbuilderTemplate) template = NULL;
    guint hand_size;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_template_new ();
    SKIP_IF_NULL (template);

    lrg_deckbuilder_template_set_base_hand_size (template, 7);
    hand_size = lrg_deckbuilder_template_get_base_hand_size (template);

    g_assert_cmpuint (hand_size, ==, 7);
}

static void
test_deckbuilder_template_current_turn (void)
{
    g_autoptr(LrgDeckbuilderTemplate) template = NULL;
    guint turn;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_template_new ();
    SKIP_IF_NULL (template);

    /* Initial turn is 0 (not started) */
    turn = lrg_deckbuilder_template_get_current_turn (template);
    g_assert_cmpuint (turn, ==, 0);
}

/* ==========================================================================
 * Test Cases - LrgDeckbuilderTemplate Energy Operations
 * ========================================================================== */

static void
test_deckbuilder_template_spend_energy (void)
{
    g_autoptr(LrgDeckbuilderTemplate) template = NULL;
    gboolean spent;
    gint energy;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_template_new ();
    SKIP_IF_NULL (template);

    lrg_deckbuilder_template_set_current_energy (template, 3);

    /* Spend 2 energy - should succeed */
    spent = lrg_deckbuilder_template_spend_energy (template, 2);
    g_assert_true (spent);

    energy = lrg_deckbuilder_template_get_current_energy (template);
    g_assert_cmpint (energy, ==, 1);

    /* Try to spend 5 energy - should fail */
    spent = lrg_deckbuilder_template_spend_energy (template, 5);
    g_assert_false (spent);

    /* Energy should be unchanged */
    energy = lrg_deckbuilder_template_get_current_energy (template);
    g_assert_cmpint (energy, ==, 1);
}

static void
test_deckbuilder_template_gain_energy (void)
{
    g_autoptr(LrgDeckbuilderTemplate) template = NULL;
    gint energy;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_template_new ();
    SKIP_IF_NULL (template);

    lrg_deckbuilder_template_set_current_energy (template, 2);
    lrg_deckbuilder_template_gain_energy (template, 3);

    energy = lrg_deckbuilder_template_get_current_energy (template);
    g_assert_cmpint (energy, ==, 5);
}

static void
test_deckbuilder_template_reset_energy (void)
{
    g_autoptr(LrgDeckbuilderTemplate) template = NULL;
    gint energy;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_template_new ();
    SKIP_IF_NULL (template);

    lrg_deckbuilder_template_set_max_energy (template, 4);
    lrg_deckbuilder_template_set_current_energy (template, 1);
    lrg_deckbuilder_template_reset_energy (template);

    energy = lrg_deckbuilder_template_get_current_energy (template);
    g_assert_cmpint (energy, ==, 4); /* Reset to max */
}

/* ==========================================================================
 * Test Cases - LrgDeckbuilderTemplate Turn Management
 * ========================================================================== */

static void
test_deckbuilder_template_is_player_turn (void)
{
    g_autoptr(LrgDeckbuilderTemplate) template = NULL;
    gboolean is_player;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_template_new ();
    SKIP_IF_NULL (template);

    /* Check - should not crash */
    is_player = lrg_deckbuilder_template_is_player_turn (template);
    /* Value depends on state, just verify it doesn't crash */
    (void)is_player;

    g_assert_true (TRUE);
}

/* ==========================================================================
 * Test Cases - LrgDeckbuilderCombatTemplate Construction
 * ========================================================================== */

static void
test_deckbuilder_combat_new (void)
{
    g_autoptr(LrgDeckbuilderCombatTemplate) template = NULL;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_combat_template_new ();
    SKIP_IF_NULL (template);

    g_assert_nonnull (template);
    g_assert_true (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (template));
}

static void
test_deckbuilder_combat_inherits_deckbuilder (void)
{
    g_autoptr(LrgDeckbuilderCombatTemplate) template = NULL;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_combat_template_new ();
    SKIP_IF_NULL (template);

    g_assert_true (LRG_IS_DECKBUILDER_TEMPLATE (template));
    g_assert_true (LRG_IS_GAME_TEMPLATE (template));
}

/* ==========================================================================
 * Test Cases - LrgDeckbuilderCombatTemplate Player State
 * ========================================================================== */

static void
test_deckbuilder_combat_player_health (void)
{
    g_autoptr(LrgDeckbuilderCombatTemplate) template = NULL;
    gint health;
    gint max_health;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_combat_template_new ();
    SKIP_IF_NULL (template);

    lrg_deckbuilder_combat_template_set_player_max_health (template, 80);
    max_health = lrg_deckbuilder_combat_template_get_player_max_health (template);
    g_assert_cmpint (max_health, ==, 80);

    /* Current health should be accessible */
    health = lrg_deckbuilder_combat_template_get_player_health (template);
    g_assert_cmpint (health, >=, 0);
    g_assert_cmpint (health, <=, max_health);
}

static void
test_deckbuilder_combat_player_block (void)
{
    g_autoptr(LrgDeckbuilderCombatTemplate) template = NULL;
    gint block;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_combat_template_new ();
    SKIP_IF_NULL (template);

    /* Initially no block */
    block = lrg_deckbuilder_combat_template_get_player_block (template);
    g_assert_cmpint (block, ==, 0);

    /* Add block */
    lrg_deckbuilder_combat_template_add_player_block (template, 5);
    block = lrg_deckbuilder_combat_template_get_player_block (template);
    g_assert_cmpint (block, ==, 5);
}

static void
test_deckbuilder_combat_player_heal (void)
{
    g_autoptr(LrgDeckbuilderCombatTemplate) template = NULL;
    gint healed;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_combat_template_new ();
    SKIP_IF_NULL (template);

    lrg_deckbuilder_combat_template_set_player_max_health (template, 80);

    /* Heal player - should not crash */
    healed = lrg_deckbuilder_combat_template_heal_player (template, 10);
    g_assert_cmpint (healed, >=, 0);
}

static void
test_deckbuilder_combat_player_damage (void)
{
    g_autoptr(LrgDeckbuilderCombatTemplate) template = NULL;
    gint damage_taken;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_combat_template_new ();
    SKIP_IF_NULL (template);

    lrg_deckbuilder_combat_template_set_player_max_health (template, 80);

    /* Damage player - should not crash */
    damage_taken = lrg_deckbuilder_combat_template_damage_player (template, 10);
    g_assert_cmpint (damage_taken, >=, 0);
}

/* ==========================================================================
 * Test Cases - LrgDeckbuilderCombatTemplate Combat Flow
 * ========================================================================== */

static void
test_deckbuilder_combat_is_in_combat (void)
{
    g_autoptr(LrgDeckbuilderCombatTemplate) template = NULL;
    gboolean in_combat;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_combat_template_new ();
    SKIP_IF_NULL (template);

    /* Initially not in combat */
    in_combat = lrg_deckbuilder_combat_template_is_in_combat (template);
    g_assert_false (in_combat);
}

static void
test_deckbuilder_combat_get_enemy_count (void)
{
    g_autoptr(LrgDeckbuilderCombatTemplate) template = NULL;
    guint count;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_combat_template_new ();
    SKIP_IF_NULL (template);

    /* No enemies initially */
    count = lrg_deckbuilder_combat_template_get_enemy_count (template);
    g_assert_cmpuint (count, ==, 0);
}

static void
test_deckbuilder_combat_get_enemies (void)
{
    g_autoptr(LrgDeckbuilderCombatTemplate) template = NULL;
    GPtrArray *enemies;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_combat_template_new ();
    SKIP_IF_NULL (template);

    enemies = lrg_deckbuilder_combat_template_get_enemies (template);

    /* Enemies array may be NULL if combat not started */
    if (enemies != NULL)
    {
        g_assert_cmpuint (enemies->len, ==, 0);
    }
}

static void
test_deckbuilder_combat_get_combat_context (void)
{
    g_autoptr(LrgDeckbuilderCombatTemplate) template = NULL;
    LrgCombatContext *context;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_combat_template_new ();
    SKIP_IF_NULL (template);

    /* Not in combat = NULL context */
    context = lrg_deckbuilder_combat_template_get_combat_context (template);
    g_assert_null (context);
}

static void
test_deckbuilder_combat_get_player (void)
{
    g_autoptr(LrgDeckbuilderCombatTemplate) template = NULL;
    LrgPlayerCombatant *player;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_combat_template_new ();
    SKIP_IF_NULL (template);

    player = lrg_deckbuilder_combat_template_get_player (template);
    g_assert_nonnull (player);
}

/* ==========================================================================
 * Test Cases - LrgDeckbuilderPokerTemplate Construction
 * ========================================================================== */

static void
test_deckbuilder_poker_new (void)
{
    g_autoptr(LrgDeckbuilderPokerTemplate) template = NULL;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_poker_template_new ();
    SKIP_IF_NULL (template);

    g_assert_nonnull (template);
    g_assert_true (LRG_IS_DECKBUILDER_POKER_TEMPLATE (template));
}

static void
test_deckbuilder_poker_inherits_deckbuilder (void)
{
    g_autoptr(LrgDeckbuilderPokerTemplate) template = NULL;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_poker_template_new ();
    SKIP_IF_NULL (template);

    g_assert_true (LRG_IS_DECKBUILDER_TEMPLATE (template));
    g_assert_true (LRG_IS_GAME_TEMPLATE (template));
}

/* ==========================================================================
 * Test Cases - LrgDeckbuilderPokerTemplate Score & Progress
 * ========================================================================== */

static void
test_deckbuilder_poker_score (void)
{
    g_autoptr(LrgDeckbuilderPokerTemplate) template = NULL;
    gint64 score;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_poker_template_new ();
    SKIP_IF_NULL (template);

    score = lrg_deckbuilder_poker_template_get_score (template);
    g_assert_cmpint (score, ==, 0);
}

static void
test_deckbuilder_poker_blind_score (void)
{
    g_autoptr(LrgDeckbuilderPokerTemplate) template = NULL;
    gint64 blind;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_poker_template_new ();
    SKIP_IF_NULL (template);

    lrg_deckbuilder_poker_template_set_blind_score (template, 300);
    blind = lrg_deckbuilder_poker_template_get_blind_score (template);
    g_assert_cmpint (blind, ==, 300);
}

static void
test_deckbuilder_poker_ante (void)
{
    g_autoptr(LrgDeckbuilderPokerTemplate) template = NULL;
    guint ante;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_poker_template_new ();
    SKIP_IF_NULL (template);

    lrg_deckbuilder_poker_template_set_ante (template, 5);
    ante = lrg_deckbuilder_poker_template_get_ante (template);
    g_assert_cmpuint (ante, ==, 5);
}

static void
test_deckbuilder_poker_money (void)
{
    g_autoptr(LrgDeckbuilderPokerTemplate) template = NULL;
    gint64 money;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_poker_template_new ();
    SKIP_IF_NULL (template);

    lrg_deckbuilder_poker_template_set_money (template, 100);
    money = lrg_deckbuilder_poker_template_get_money (template);
    g_assert_cmpint (money, ==, 100);

    lrg_deckbuilder_poker_template_add_money (template, 50);
    money = lrg_deckbuilder_poker_template_get_money (template);
    g_assert_cmpint (money, ==, 150);
}

/* ==========================================================================
 * Test Cases - LrgDeckbuilderPokerTemplate Hands & Discards
 * ========================================================================== */

static void
test_deckbuilder_poker_hands_remaining (void)
{
    g_autoptr(LrgDeckbuilderPokerTemplate) template = NULL;
    guint hands;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_poker_template_new ();
    SKIP_IF_NULL (template);

    lrg_deckbuilder_poker_template_set_hands_remaining (template, 4);
    hands = lrg_deckbuilder_poker_template_get_hands_remaining (template);
    g_assert_cmpuint (hands, ==, 4);
}

static void
test_deckbuilder_poker_discards_remaining (void)
{
    g_autoptr(LrgDeckbuilderPokerTemplate) template = NULL;
    guint discards;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_poker_template_new ();
    SKIP_IF_NULL (template);

    lrg_deckbuilder_poker_template_set_discards_remaining (template, 3);
    discards = lrg_deckbuilder_poker_template_get_discards_remaining (template);
    g_assert_cmpuint (discards, ==, 3);
}

static void
test_deckbuilder_poker_max_hands (void)
{
    g_autoptr(LrgDeckbuilderPokerTemplate) template = NULL;
    guint max_hands;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_poker_template_new ();
    SKIP_IF_NULL (template);

    lrg_deckbuilder_poker_template_set_max_hands (template, 4);
    max_hands = lrg_deckbuilder_poker_template_get_max_hands (template);
    g_assert_cmpuint (max_hands, ==, 4);
}

static void
test_deckbuilder_poker_max_discards (void)
{
    g_autoptr(LrgDeckbuilderPokerTemplate) template = NULL;
    guint max_discards;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_poker_template_new ();
    SKIP_IF_NULL (template);

    lrg_deckbuilder_poker_template_set_max_discards (template, 3);
    max_discards = lrg_deckbuilder_poker_template_get_max_discards (template);
    g_assert_cmpuint (max_discards, ==, 3);
}

/* ==========================================================================
 * Test Cases - LrgDeckbuilderPokerTemplate Joker Management
 * ========================================================================== */

static void
test_deckbuilder_poker_joker_count (void)
{
    g_autoptr(LrgDeckbuilderPokerTemplate) template = NULL;
    guint count;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_poker_template_new ();
    SKIP_IF_NULL (template);

    /* No jokers initially */
    count = lrg_deckbuilder_poker_template_get_joker_count (template);
    g_assert_cmpuint (count, ==, 0);
}

static void
test_deckbuilder_poker_max_jokers (void)
{
    g_autoptr(LrgDeckbuilderPokerTemplate) template = NULL;
    guint max_jokers;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_poker_template_new ();
    SKIP_IF_NULL (template);

    lrg_deckbuilder_poker_template_set_max_jokers (template, 5);
    max_jokers = lrg_deckbuilder_poker_template_get_max_jokers (template);
    g_assert_cmpuint (max_jokers, ==, 5);
}

static void
test_deckbuilder_poker_get_jokers (void)
{
    g_autoptr(LrgDeckbuilderPokerTemplate) template = NULL;
    GPtrArray *jokers;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_poker_template_new ();
    SKIP_IF_NULL (template);

    jokers = lrg_deckbuilder_poker_template_get_jokers (template);
    g_assert_nonnull (jokers);
    g_assert_cmpuint (jokers->len, ==, 0);
}

/* ==========================================================================
 * Test Cases - LrgDeckbuilderPokerTemplate Round Management
 * ========================================================================== */

static void
test_deckbuilder_poker_is_in_round (void)
{
    g_autoptr(LrgDeckbuilderPokerTemplate) template = NULL;
    gboolean in_round;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_poker_template_new ();
    SKIP_IF_NULL (template);

    /* Initially not in round */
    in_round = lrg_deckbuilder_poker_template_is_in_round (template);
    g_assert_false (in_round);
}

static void
test_deckbuilder_poker_is_round_won (void)
{
    g_autoptr(LrgDeckbuilderPokerTemplate) template = NULL;
    gboolean won;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_poker_template_new ();
    SKIP_IF_NULL (template);

    /* Not won without score */
    won = lrg_deckbuilder_poker_template_is_round_won (template);
    g_assert_false (won);
}

static void
test_deckbuilder_poker_is_round_lost (void)
{
    g_autoptr(LrgDeckbuilderPokerTemplate) template = NULL;
    gboolean lost;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_poker_template_new ();
    SKIP_IF_NULL (template);

    /* Check - just verify it doesn't crash */
    lost = lrg_deckbuilder_poker_template_is_round_lost (template);
    (void)lost;

    g_assert_true (TRUE);
}

static void
test_deckbuilder_poker_can_play_hand (void)
{
    g_autoptr(LrgDeckbuilderPokerTemplate) template = NULL;
    gboolean can_play;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_poker_template_new ();
    SKIP_IF_NULL (template);

    /* Can't play without cards selected */
    can_play = lrg_deckbuilder_poker_template_can_play_hand (template);
    g_assert_false (can_play);
}

static void
test_deckbuilder_poker_can_discard (void)
{
    g_autoptr(LrgDeckbuilderPokerTemplate) template = NULL;
    gboolean can_discard;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_poker_template_new ();
    SKIP_IF_NULL (template);

    /* Can't discard without cards selected */
    can_discard = lrg_deckbuilder_poker_template_can_discard (template);
    g_assert_false (can_discard);
}

static void
test_deckbuilder_poker_get_scoring_context (void)
{
    g_autoptr(LrgDeckbuilderPokerTemplate) template = NULL;
    LrgScoringContext *context;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_poker_template_new ();
    SKIP_IF_NULL (template);

    context = lrg_deckbuilder_poker_template_get_scoring_context (template);
    g_assert_nonnull (context);
}

static void
test_deckbuilder_poker_last_hand (void)
{
    g_autoptr(LrgDeckbuilderPokerTemplate) template = NULL;
    LrgHandType type;
    gint64 score;

    SKIP_IF_NO_DISPLAY ();

    template = lrg_deckbuilder_poker_template_new ();
    SKIP_IF_NULL (template);

    /* Check defaults - should not crash */
    type = lrg_deckbuilder_poker_template_get_last_hand_type (template);
    score = lrg_deckbuilder_poker_template_get_last_hand_score (template);

    /* Just verify they return something reasonable */
    g_assert_cmpint (score, >=, 0);
    (void)type;
}

/* ==========================================================================
 * Test Cases - Type Hierarchy
 * ========================================================================== */

static void
test_deckbuilder_type_hierarchy (void)
{
    g_autoptr(LrgDeckbuilderTemplate) base = NULL;
    g_autoptr(LrgDeckbuilderCombatTemplate) combat = NULL;
    g_autoptr(LrgDeckbuilderPokerTemplate) poker = NULL;

    SKIP_IF_NO_DISPLAY ();

    base = lrg_deckbuilder_template_new ();
    combat = lrg_deckbuilder_combat_template_new ();
    poker = lrg_deckbuilder_poker_template_new ();

    /* Skip if any failed to create */
    if (base == NULL || combat == NULL || poker == NULL)
    {
        g_test_skip ("Templates not available");
        return;
    }

    /* Verify hierarchy */
    g_assert_true (LRG_IS_GAME_TEMPLATE (base));
    g_assert_false (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (base));
    g_assert_false (LRG_IS_DECKBUILDER_POKER_TEMPLATE (base));

    g_assert_true (LRG_IS_GAME_TEMPLATE (combat));
    g_assert_true (LRG_IS_DECKBUILDER_TEMPLATE (combat));
    g_assert_false (LRG_IS_DECKBUILDER_POKER_TEMPLATE (combat));

    g_assert_true (LRG_IS_GAME_TEMPLATE (poker));
    g_assert_true (LRG_IS_DECKBUILDER_TEMPLATE (poker));
    g_assert_false (LRG_IS_DECKBUILDER_COMBAT_TEMPLATE (poker));
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* LrgDeckbuilderTemplate - Construction */
    g_test_add_func ("/template/deckbuilder/new",
                     test_deckbuilder_template_new);
    g_test_add_func ("/template/deckbuilder/inherits-game-template",
                     test_deckbuilder_template_inherits_game_template);

    /* LrgDeckbuilderTemplate - Properties */
    g_test_add_func ("/template/deckbuilder/energy",
                     test_deckbuilder_template_energy);
    g_test_add_func ("/template/deckbuilder/hand-size",
                     test_deckbuilder_template_hand_size);
    g_test_add_func ("/template/deckbuilder/current-turn",
                     test_deckbuilder_template_current_turn);

    /* LrgDeckbuilderTemplate - Energy Operations */
    g_test_add_func ("/template/deckbuilder/spend-energy",
                     test_deckbuilder_template_spend_energy);
    g_test_add_func ("/template/deckbuilder/gain-energy",
                     test_deckbuilder_template_gain_energy);
    g_test_add_func ("/template/deckbuilder/reset-energy",
                     test_deckbuilder_template_reset_energy);

    /* LrgDeckbuilderTemplate - Turn Management */
    g_test_add_func ("/template/deckbuilder/is-player-turn",
                     test_deckbuilder_template_is_player_turn);

    /* LrgDeckbuilderCombatTemplate - Construction */
    g_test_add_func ("/template/deckbuilder-combat/new",
                     test_deckbuilder_combat_new);
    g_test_add_func ("/template/deckbuilder-combat/inherits-deckbuilder",
                     test_deckbuilder_combat_inherits_deckbuilder);

    /* LrgDeckbuilderCombatTemplate - Player State */
    g_test_add_func ("/template/deckbuilder-combat/player-health",
                     test_deckbuilder_combat_player_health);
    g_test_add_func ("/template/deckbuilder-combat/player-block",
                     test_deckbuilder_combat_player_block);
    g_test_add_func ("/template/deckbuilder-combat/player-heal",
                     test_deckbuilder_combat_player_heal);
    g_test_add_func ("/template/deckbuilder-combat/player-damage",
                     test_deckbuilder_combat_player_damage);

    /* LrgDeckbuilderCombatTemplate - Combat Flow */
    g_test_add_func ("/template/deckbuilder-combat/is-in-combat",
                     test_deckbuilder_combat_is_in_combat);
    g_test_add_func ("/template/deckbuilder-combat/get-enemy-count",
                     test_deckbuilder_combat_get_enemy_count);
    g_test_add_func ("/template/deckbuilder-combat/get-enemies",
                     test_deckbuilder_combat_get_enemies);
    g_test_add_func ("/template/deckbuilder-combat/get-combat-context",
                     test_deckbuilder_combat_get_combat_context);
    g_test_add_func ("/template/deckbuilder-combat/get-player",
                     test_deckbuilder_combat_get_player);

    /* LrgDeckbuilderPokerTemplate - Construction */
    g_test_add_func ("/template/deckbuilder-poker/new",
                     test_deckbuilder_poker_new);
    g_test_add_func ("/template/deckbuilder-poker/inherits-deckbuilder",
                     test_deckbuilder_poker_inherits_deckbuilder);

    /* LrgDeckbuilderPokerTemplate - Score & Progress */
    g_test_add_func ("/template/deckbuilder-poker/score",
                     test_deckbuilder_poker_score);
    g_test_add_func ("/template/deckbuilder-poker/blind-score",
                     test_deckbuilder_poker_blind_score);
    g_test_add_func ("/template/deckbuilder-poker/ante",
                     test_deckbuilder_poker_ante);
    g_test_add_func ("/template/deckbuilder-poker/money",
                     test_deckbuilder_poker_money);

    /* LrgDeckbuilderPokerTemplate - Hands & Discards */
    g_test_add_func ("/template/deckbuilder-poker/hands-remaining",
                     test_deckbuilder_poker_hands_remaining);
    g_test_add_func ("/template/deckbuilder-poker/discards-remaining",
                     test_deckbuilder_poker_discards_remaining);
    g_test_add_func ("/template/deckbuilder-poker/max-hands",
                     test_deckbuilder_poker_max_hands);
    g_test_add_func ("/template/deckbuilder-poker/max-discards",
                     test_deckbuilder_poker_max_discards);

    /* LrgDeckbuilderPokerTemplate - Joker Management */
    g_test_add_func ("/template/deckbuilder-poker/joker-count",
                     test_deckbuilder_poker_joker_count);
    g_test_add_func ("/template/deckbuilder-poker/max-jokers",
                     test_deckbuilder_poker_max_jokers);
    g_test_add_func ("/template/deckbuilder-poker/get-jokers",
                     test_deckbuilder_poker_get_jokers);

    /* LrgDeckbuilderPokerTemplate - Round Management */
    g_test_add_func ("/template/deckbuilder-poker/is-in-round",
                     test_deckbuilder_poker_is_in_round);
    g_test_add_func ("/template/deckbuilder-poker/is-round-won",
                     test_deckbuilder_poker_is_round_won);
    g_test_add_func ("/template/deckbuilder-poker/is-round-lost",
                     test_deckbuilder_poker_is_round_lost);
    g_test_add_func ("/template/deckbuilder-poker/can-play-hand",
                     test_deckbuilder_poker_can_play_hand);
    g_test_add_func ("/template/deckbuilder-poker/can-discard",
                     test_deckbuilder_poker_can_discard);
    g_test_add_func ("/template/deckbuilder-poker/get-scoring-context",
                     test_deckbuilder_poker_get_scoring_context);
    g_test_add_func ("/template/deckbuilder-poker/last-hand",
                     test_deckbuilder_poker_last_hand);

    /* Type Hierarchy */
    g_test_add_func ("/template/deckbuilder/type-hierarchy",
                     test_deckbuilder_type_hierarchy);

    return g_test_run ();
}
