/* test-idle.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the idle game module.
 */

#include <glib.h>
#include <math.h>

#include "idle/lrg-big-number.h"
#include "idle/lrg-milestone.h"
#include "idle/lrg-idle-calculator.h"
#include "idle/lrg-prestige.h"
#include "idle/lrg-unlock-tree.h"
#include "idle/lrg-automation.h"

/* ========================================================================= */
/* LrgBigNumber Tests                                                        */
/* ========================================================================= */

static void
test_big_number_new (void)
{
    g_autoptr(LrgBigNumber) bn = NULL;

    bn = lrg_big_number_new (1234.5);

    g_assert_nonnull (bn);
    g_assert_cmpfloat_with_epsilon (lrg_big_number_get_mantissa (bn), 1.2345, 0.0001);
    g_assert_cmpint (lrg_big_number_get_exponent (bn), ==, 3);
}

static void
test_big_number_new_from_parts (void)
{
    g_autoptr(LrgBigNumber) bn = NULL;

    bn = lrg_big_number_new_from_parts (5.0, 10);

    g_assert_cmpfloat_with_epsilon (lrg_big_number_get_mantissa (bn), 5.0, 0.001);
    g_assert_cmpint (lrg_big_number_get_exponent (bn), ==, 10);
}

static void
test_big_number_zero (void)
{
    g_autoptr(LrgBigNumber) bn = NULL;

    bn = lrg_big_number_new_zero ();

    g_assert_true (lrg_big_number_is_zero (bn));
}

static void
test_big_number_add (void)
{
    g_autoptr(LrgBigNumber) a = NULL;
    g_autoptr(LrgBigNumber) b = NULL;
    g_autoptr(LrgBigNumber) result = NULL;

    a = lrg_big_number_new (1000.0);
    b = lrg_big_number_new (500.0);
    result = lrg_big_number_add (a, b);

    g_assert_cmpfloat_with_epsilon (lrg_big_number_to_double (result), 1500.0, 0.1);
}

static void
test_big_number_add_different_magnitudes (void)
{
    g_autoptr(LrgBigNumber) a = NULL;
    g_autoptr(LrgBigNumber) b = NULL;
    g_autoptr(LrgBigNumber) result = NULL;

    a = lrg_big_number_new_from_parts (1.0, 6);  /* 1,000,000 */
    b = lrg_big_number_new (1.0);
    result = lrg_big_number_add (a, b);

    /* Should still be essentially 1,000,000 */
    g_assert_cmpint (lrg_big_number_get_exponent (result), ==, 6);
}

static void
test_big_number_subtract (void)
{
    g_autoptr(LrgBigNumber) a = NULL;
    g_autoptr(LrgBigNumber) b = NULL;
    g_autoptr(LrgBigNumber) result = NULL;

    a = lrg_big_number_new (1000.0);
    b = lrg_big_number_new (400.0);
    result = lrg_big_number_subtract (a, b);

    g_assert_cmpfloat_with_epsilon (lrg_big_number_to_double (result), 600.0, 0.1);
}

static void
test_big_number_multiply (void)
{
    g_autoptr(LrgBigNumber) a = NULL;
    g_autoptr(LrgBigNumber) b = NULL;
    g_autoptr(LrgBigNumber) result = NULL;

    a = lrg_big_number_new (1000.0);
    b = lrg_big_number_new (5.0);
    result = lrg_big_number_multiply (a, b);

    g_assert_cmpfloat_with_epsilon (lrg_big_number_to_double (result), 5000.0, 0.1);
}

static void
test_big_number_divide (void)
{
    g_autoptr(LrgBigNumber) a = NULL;
    g_autoptr(LrgBigNumber) b = NULL;
    g_autoptr(LrgBigNumber) result = NULL;

    a = lrg_big_number_new (1000.0);
    b = lrg_big_number_new (4.0);
    result = lrg_big_number_divide (a, b);

    g_assert_cmpfloat_with_epsilon (lrg_big_number_to_double (result), 250.0, 0.1);
}

static void
test_big_number_pow (void)
{
    g_autoptr(LrgBigNumber) base = NULL;
    g_autoptr(LrgBigNumber) result = NULL;

    base = lrg_big_number_new (10.0);
    result = lrg_big_number_pow (base, 3.0);

    g_assert_cmpfloat_with_epsilon (lrg_big_number_to_double (result), 1000.0, 0.1);
}

static void
test_big_number_compare (void)
{
    g_autoptr(LrgBigNumber) a = NULL;
    g_autoptr(LrgBigNumber) b = NULL;
    g_autoptr(LrgBigNumber) c = NULL;

    a = lrg_big_number_new (1000.0);
    b = lrg_big_number_new (500.0);
    c = lrg_big_number_new (1000.0);

    g_assert_cmpint (lrg_big_number_compare (a, b), >, 0);
    g_assert_cmpint (lrg_big_number_compare (b, a), <, 0);
    g_assert_true (lrg_big_number_equals (a, c));
    g_assert_true (lrg_big_number_greater_than (a, b));
    g_assert_true (lrg_big_number_less_than (b, a));
}

static void
test_big_number_format_short (void)
{
    g_autoptr(LrgBigNumber) million = NULL;
    g_autoptr(LrgBigNumber) billion = NULL;
    g_autofree gchar *million_str = NULL;
    g_autofree gchar *billion_str = NULL;

    million = lrg_big_number_new (1500000.0);
    billion = lrg_big_number_new (2300000000.0);

    million_str = lrg_big_number_format_short (million);
    billion_str = lrg_big_number_format_short (billion);

    g_assert_cmpstr (million_str, ==, "1.50M");
    g_assert_cmpstr (billion_str, ==, "2.30B");
}

static void
test_big_number_format_scientific (void)
{
    g_autoptr(LrgBigNumber) bn = NULL;
    g_autofree gchar *str = NULL;

    bn = lrg_big_number_new_from_parts (1.5, 6);
    str = lrg_big_number_format_scientific (bn);

    g_assert_cmpstr (str, ==, "1.50e6");
}

static void
test_big_number_in_place (void)
{
    g_autoptr(LrgBigNumber) bn = NULL;
    g_autoptr(LrgBigNumber) add = NULL;

    bn = lrg_big_number_new (1000.0);
    add = lrg_big_number_new (500.0);

    lrg_big_number_add_in_place (bn, add);
    g_assert_cmpfloat_with_epsilon (lrg_big_number_to_double (bn), 1500.0, 0.1);

    lrg_big_number_multiply_in_place (bn, 2.0);
    g_assert_cmpfloat_with_epsilon (lrg_big_number_to_double (bn), 3000.0, 0.1);
}

/* ========================================================================= */
/* LrgMilestone Tests                                                        */
/* ========================================================================= */

static void
test_milestone_new (void)
{
    g_autoptr(LrgMilestone) milestone = NULL;

    milestone = lrg_milestone_new_simple ("test", "Test Milestone", 1000.0);

    g_assert_cmpstr (lrg_milestone_get_id (milestone), ==, "test");
    g_assert_cmpstr (lrg_milestone_get_name (milestone), ==, "Test Milestone");
    g_assert_false (lrg_milestone_is_achieved (milestone));
}

static void
test_milestone_check (void)
{
    g_autoptr(LrgMilestone) milestone = NULL;
    g_autoptr(LrgBigNumber) below = NULL;
    g_autoptr(LrgBigNumber) above = NULL;

    milestone = lrg_milestone_new_simple ("test", "Test", 1000.0);
    below = lrg_big_number_new (500.0);
    above = lrg_big_number_new (1500.0);

    g_assert_false (lrg_milestone_check (milestone, below));
    g_assert_false (lrg_milestone_is_achieved (milestone));

    g_assert_true (lrg_milestone_check (milestone, above));
    g_assert_true (lrg_milestone_is_achieved (milestone));

    /* Should not trigger again once achieved */
    g_assert_false (lrg_milestone_check (milestone, above));
}

static void
test_milestone_progress (void)
{
    g_autoptr(LrgMilestone) milestone = NULL;
    g_autoptr(LrgBigNumber) half = NULL;
    gdouble progress;

    milestone = lrg_milestone_new_simple ("test", "Test", 1000.0);
    half = lrg_big_number_new (500.0);

    progress = lrg_milestone_get_progress (milestone, half);
    g_assert_cmpfloat_with_epsilon (progress, 0.5, 0.01);
}

static void
test_milestone_reset (void)
{
    g_autoptr(LrgMilestone) milestone = NULL;
    g_autoptr(LrgBigNumber) above = NULL;

    milestone = lrg_milestone_new_simple ("test", "Test", 1000.0);
    above = lrg_big_number_new (2000.0);

    lrg_milestone_check (milestone, above);
    g_assert_true (lrg_milestone_is_achieved (milestone));

    lrg_milestone_reset (milestone);
    g_assert_false (lrg_milestone_is_achieved (milestone));
}

/* ========================================================================= */
/* LrgIdleCalculator Tests                                                   */
/* ========================================================================= */

static void
test_idle_calculator_new (void)
{
    g_autoptr(LrgIdleCalculator) calc = NULL;

    calc = lrg_idle_calculator_new ();

    g_assert_nonnull (calc);
    g_assert_cmpfloat (lrg_idle_calculator_get_global_multiplier (calc), ==, 1.0);
}

static void
test_idle_generator_new (void)
{
    g_autoptr(LrgIdleGenerator) gen = NULL;

    gen = lrg_idle_generator_new_simple ("coins", 10.0);

    g_assert_cmpstr (lrg_idle_generator_get_id (gen), ==, "coins");
    g_assert_true (lrg_idle_generator_is_enabled (gen));
    g_assert_cmpint (lrg_idle_generator_get_count (gen), ==, 0);
}

static void
test_idle_calculator_add_generator (void)
{
    g_autoptr(LrgIdleCalculator) calc = NULL;
    g_autoptr(LrgIdleGenerator) gen = NULL;
    LrgIdleGenerator *found;

    calc = lrg_idle_calculator_new ();
    gen = lrg_idle_generator_new_simple ("gold", 5.0);

    lrg_idle_calculator_add_generator (calc, gen);

    found = lrg_idle_calculator_get_generator (calc, "gold");
    g_assert_nonnull (found);
    g_assert_cmpstr (lrg_idle_generator_get_id (found), ==, "gold");
}

static void
test_idle_calculator_total_rate (void)
{
    g_autoptr(LrgIdleCalculator) calc = NULL;
    g_autoptr(LrgIdleGenerator) gen = NULL;
    g_autoptr(LrgBigNumber) rate = NULL;
    LrgIdleGenerator *found;

    calc = lrg_idle_calculator_new ();
    gen = lrg_idle_generator_new_simple ("coins", 10.0);

    lrg_idle_calculator_add_generator (calc, gen);

    /* Set count to 5 */
    found = lrg_idle_calculator_get_generator (calc, "coins");
    lrg_idle_generator_set_count (found, 5);

    rate = lrg_idle_calculator_get_total_rate (calc);

    /* 10.0 * 5 = 50.0 per second */
    g_assert_cmpfloat_with_epsilon (lrg_big_number_to_double (rate), 50.0, 0.1);
}

static void
test_idle_calculator_simulate (void)
{
    g_autoptr(LrgIdleCalculator) calc = NULL;
    g_autoptr(LrgIdleGenerator) gen = NULL;
    g_autoptr(LrgBigNumber) production = NULL;
    LrgIdleGenerator *found;

    calc = lrg_idle_calculator_new ();
    gen = lrg_idle_generator_new_simple ("coins", 10.0);

    lrg_idle_calculator_add_generator (calc, gen);

    found = lrg_idle_calculator_get_generator (calc, "coins");
    lrg_idle_generator_set_count (found, 5);

    /* Simulate 10 seconds: 50 * 10 = 500 */
    production = lrg_idle_calculator_simulate (calc, 10.0);

    g_assert_cmpfloat_with_epsilon (lrg_big_number_to_double (production), 500.0, 0.1);
}

static void
test_idle_calculator_global_multiplier (void)
{
    g_autoptr(LrgIdleCalculator) calc = NULL;
    g_autoptr(LrgIdleGenerator) gen = NULL;
    g_autoptr(LrgBigNumber) rate = NULL;
    LrgIdleGenerator *found;

    calc = lrg_idle_calculator_new ();
    gen = lrg_idle_generator_new_simple ("coins", 10.0);

    lrg_idle_calculator_add_generator (calc, gen);

    found = lrg_idle_calculator_get_generator (calc, "coins");
    lrg_idle_generator_set_count (found, 1);

    lrg_idle_calculator_set_global_multiplier (calc, 2.0);

    rate = lrg_idle_calculator_get_total_rate (calc);

    /* 10.0 * 1 * 2.0 = 20.0 */
    g_assert_cmpfloat_with_epsilon (lrg_big_number_to_double (rate), 20.0, 0.1);
}

static void
test_idle_calculator_snapshot (void)
{
    g_autoptr(LrgIdleCalculator) calc = NULL;
    gint64 time1, time2;

    calc = lrg_idle_calculator_new ();

    g_assert_cmpint (lrg_idle_calculator_get_snapshot_time (calc), ==, 0);

    lrg_idle_calculator_take_snapshot (calc);
    time1 = lrg_idle_calculator_get_snapshot_time (calc);

    g_assert_cmpint (time1, >, 0);

    /* Set a specific time */
    lrg_idle_calculator_set_snapshot_time (calc, 12345);
    time2 = lrg_idle_calculator_get_snapshot_time (calc);

    g_assert_cmpint (time2, ==, 12345);
}

/* ========================================================================= */
/* LrgPrestige Tests                                                         */
/* ========================================================================= */

static void
test_prestige_new (void)
{
    g_autoptr(LrgPrestige) prestige = NULL;

    prestige = lrg_prestige_new ();

    g_assert_nonnull (prestige);
    g_assert_cmpint (lrg_prestige_get_times_prestiged (prestige), ==, 0);
}

static void
test_prestige_can_prestige (void)
{
    g_autoptr(LrgPrestige) prestige = NULL;
    g_autoptr(LrgBigNumber) below = NULL;
    g_autoptr(LrgBigNumber) above = NULL;

    prestige = lrg_prestige_new ();
    lrg_prestige_set_threshold_simple (prestige, 1000.0);

    below = lrg_big_number_new (500.0);
    above = lrg_big_number_new (2000.0);

    g_assert_false (lrg_prestige_can_prestige (prestige, below));
    g_assert_true (lrg_prestige_can_prestige (prestige, above));
}

static void
test_prestige_calculate_reward (void)
{
    g_autoptr(LrgPrestige) prestige = NULL;
    g_autoptr(LrgBigNumber) value = NULL;
    g_autoptr(LrgBigNumber) reward = NULL;

    prestige = lrg_prestige_new ();
    lrg_prestige_set_threshold_simple (prestige, 1000.0);
    lrg_prestige_set_scaling_exponent (prestige, 0.5);

    /* value = 4000, threshold = 1000, ratio = 4, reward = 4^0.5 = 2 */
    value = lrg_big_number_new (4000.0);
    reward = lrg_prestige_calculate_reward (prestige, value);

    g_assert_cmpfloat_with_epsilon (lrg_big_number_to_double (reward), 2.0, 0.1);
}

static void
test_prestige_perform (void)
{
    g_autoptr(LrgPrestige) prestige = NULL;
    g_autoptr(LrgBigNumber) value = NULL;
    g_autoptr(LrgBigNumber) reward = NULL;

    prestige = lrg_prestige_new ();
    lrg_prestige_set_threshold_simple (prestige, 1000.0);

    value = lrg_big_number_new (4000.0);
    reward = lrg_prestige_perform (prestige, value);

    g_assert_cmpint (lrg_prestige_get_times_prestiged (prestige), ==, 1);
    g_assert_false (lrg_big_number_is_zero (reward));

    /* Points should be added */
    g_assert_false (lrg_big_number_is_zero (lrg_prestige_get_points (prestige)));
}

static void
test_prestige_multiplier (void)
{
    g_autoptr(LrgPrestige) prestige = NULL;
    g_autoptr(LrgBigNumber) points = NULL;
    gdouble mult;

    prestige = lrg_prestige_new ();

    /* Default formula: 1.0 + sqrt(points) * 0.1 */
    /* With 100 points: 1.0 + sqrt(100) * 0.1 = 1.0 + 10 * 0.1 = 2.0 */
    points = lrg_big_number_new (100.0);
    lrg_prestige_set_points (prestige, points);

    mult = lrg_prestige_get_bonus_multiplier (prestige);
    g_assert_cmpfloat_with_epsilon (mult, 2.0, 0.01);
}

static void
test_prestige_reset (void)
{
    g_autoptr(LrgPrestige) prestige = NULL;
    g_autoptr(LrgBigNumber) value = NULL;

    prestige = lrg_prestige_new ();
    lrg_prestige_set_threshold_simple (prestige, 1000.0);

    value = lrg_big_number_new (4000.0);
    lrg_prestige_perform (prestige, value);

    g_assert_cmpint (lrg_prestige_get_times_prestiged (prestige), ==, 1);

    lrg_prestige_reset (prestige);

    g_assert_cmpint (lrg_prestige_get_times_prestiged (prestige), ==, 0);
    g_assert_true (lrg_big_number_is_zero (lrg_prestige_get_points (prestige)));
}

/* ========================================================================= */
/* LrgUnlockTree Tests                                                       */
/* ========================================================================= */

static void
test_unlock_tree_new (void)
{
    g_autoptr(LrgUnlockTree) tree = NULL;

    tree = lrg_unlock_tree_new ();

    g_assert_nonnull (tree);
    g_assert_cmpfloat (lrg_unlock_tree_get_progress (tree), ==, 1.0);  /* Empty tree = complete */
}

static void
test_unlock_node_new (void)
{
    g_autoptr(LrgUnlockNode) node = NULL;

    node = lrg_unlock_node_new ("upgrade1", "First Upgrade");

    g_assert_cmpstr (lrg_unlock_node_get_id (node), ==, "upgrade1");
    g_assert_cmpstr (lrg_unlock_node_get_name (node), ==, "First Upgrade");
    g_assert_false (lrg_unlock_node_is_unlocked (node));
}

static void
test_unlock_tree_add_node (void)
{
    g_autoptr(LrgUnlockTree) tree = NULL;
    g_autoptr(LrgUnlockNode) node = NULL;
    LrgUnlockNode *found;

    tree = lrg_unlock_tree_new ();
    node = lrg_unlock_node_new ("node1", "Node 1");

    g_assert_true (lrg_unlock_tree_add_node (tree, node));

    found = lrg_unlock_tree_get_node (tree, "node1");
    g_assert_nonnull (found);

    /* Duplicate should fail */
    g_assert_false (lrg_unlock_tree_add_node (tree, node));
}

static void
test_unlock_tree_requirements (void)
{
    g_autoptr(LrgUnlockTree) tree = NULL;
    g_autoptr(LrgUnlockNode) node1 = NULL;
    g_autoptr(LrgUnlockNode) node2 = NULL;
    g_autoptr(GPtrArray) reqs = NULL;

    tree = lrg_unlock_tree_new ();
    node1 = lrg_unlock_node_new ("base", "Base");
    node2 = lrg_unlock_node_new ("advanced", "Advanced");

    lrg_unlock_tree_add_node (tree, node1);
    lrg_unlock_tree_add_node (tree, node2);

    g_assert_true (lrg_unlock_tree_add_requirement (tree, "advanced", "base"));

    reqs = lrg_unlock_tree_get_requirements (tree, "advanced");
    g_assert_cmpuint (reqs->len, ==, 1);
    g_assert_cmpstr (g_ptr_array_index (reqs, 0), ==, "base");
}

static void
test_unlock_tree_cycle_prevention (void)
{
    g_autoptr(LrgUnlockTree) tree = NULL;
    g_autoptr(LrgUnlockNode) a = NULL;
    g_autoptr(LrgUnlockNode) b = NULL;
    g_autoptr(LrgUnlockNode) c = NULL;

    tree = lrg_unlock_tree_new ();
    a = lrg_unlock_node_new ("a", "A");
    b = lrg_unlock_node_new ("b", "B");
    c = lrg_unlock_node_new ("c", "C");

    lrg_unlock_tree_add_node (tree, a);
    lrg_unlock_tree_add_node (tree, b);
    lrg_unlock_tree_add_node (tree, c);

    /* a -> b -> c */
    g_assert_true (lrg_unlock_tree_add_requirement (tree, "b", "a"));
    g_assert_true (lrg_unlock_tree_add_requirement (tree, "c", "b"));

    /* c -> a would create cycle */
    g_assert_false (lrg_unlock_tree_add_requirement (tree, "a", "c"));
}

static void
test_unlock_tree_can_unlock (void)
{
    g_autoptr(LrgUnlockTree) tree = NULL;
    g_autoptr(LrgUnlockNode) base = NULL;
    g_autoptr(LrgUnlockNode) adv = NULL;
    g_autoptr(LrgBigNumber) points = NULL;

    tree = lrg_unlock_tree_new ();
    base = lrg_unlock_node_new ("base", "Base");
    lrg_unlock_node_set_cost_simple (base, 100.0);

    adv = lrg_unlock_node_new ("adv", "Advanced");
    lrg_unlock_node_set_cost_simple (adv, 200.0);

    lrg_unlock_tree_add_node (tree, base);
    lrg_unlock_tree_add_node (tree, adv);
    lrg_unlock_tree_add_requirement (tree, "adv", "base");

    points = lrg_big_number_new (500.0);

    /* Can unlock base (no requirements) */
    g_assert_true (lrg_unlock_tree_can_unlock (tree, "base", points));

    /* Cannot unlock adv (requires base) */
    g_assert_false (lrg_unlock_tree_can_unlock (tree, "adv", points));

    /* Unlock base */
    lrg_unlock_tree_unlock (tree, "base");

    /* Now can unlock adv */
    g_assert_true (lrg_unlock_tree_can_unlock (tree, "adv", points));
}

static void
test_unlock_tree_progress (void)
{
    g_autoptr(LrgUnlockTree) tree = NULL;
    g_autoptr(LrgUnlockNode) n1 = NULL;
    g_autoptr(LrgUnlockNode) n2 = NULL;

    tree = lrg_unlock_tree_new ();
    n1 = lrg_unlock_node_new ("n1", "N1");
    n2 = lrg_unlock_node_new ("n2", "N2");

    lrg_unlock_tree_add_node (tree, n1);
    lrg_unlock_tree_add_node (tree, n2);

    g_assert_cmpfloat (lrg_unlock_tree_get_progress (tree), ==, 0.0);

    lrg_unlock_tree_unlock (tree, "n1");
    g_assert_cmpfloat (lrg_unlock_tree_get_progress (tree), ==, 0.5);

    lrg_unlock_tree_unlock (tree, "n2");
    g_assert_cmpfloat (lrg_unlock_tree_get_progress (tree), ==, 1.0);
}

static void
test_unlock_tree_reset (void)
{
    g_autoptr(LrgUnlockTree) tree = NULL;
    g_autoptr(LrgUnlockNode) node = NULL;

    tree = lrg_unlock_tree_new ();
    node = lrg_unlock_node_new ("node", "Node");

    lrg_unlock_tree_add_node (tree, node);
    lrg_unlock_tree_unlock (tree, "node");

    g_assert_true (lrg_unlock_tree_is_unlocked (tree, "node"));

    lrg_unlock_tree_reset (tree);

    g_assert_false (lrg_unlock_tree_is_unlocked (tree, "node"));
}

/* ========================================================================= */
/* LrgAutomation Tests                                                       */
/* ========================================================================= */

static int automation_trigger_count = 0;

static gboolean
test_automation_callback (LrgAutomationRule *rule,
                          gpointer           user_data)
{
    (void)rule;
    (void)user_data;
    automation_trigger_count++;
    return TRUE;
}

static void
test_automation_new (void)
{
    g_autoptr(LrgAutomation) automation = NULL;

    automation = lrg_automation_new ();

    g_assert_nonnull (automation);
    g_assert_true (lrg_automation_is_enabled (automation));
}

static void
test_automation_rule_new (void)
{
    g_autoptr(LrgAutomationRule) rule = NULL;

    rule = lrg_automation_rule_new ("auto_click", LRG_AUTOMATION_TRIGGER_INTERVAL);

    g_assert_cmpstr (lrg_automation_rule_get_id (rule), ==, "auto_click");
    g_assert_cmpint (lrg_automation_rule_get_trigger (rule), ==, LRG_AUTOMATION_TRIGGER_INTERVAL);
    g_assert_true (lrg_automation_rule_is_enabled (rule));
}

static void
test_automation_add_rule (void)
{
    g_autoptr(LrgAutomation) automation = NULL;
    g_autoptr(LrgAutomationRule) rule = NULL;
    LrgAutomationRule *found;

    automation = lrg_automation_new ();
    rule = lrg_automation_rule_new ("rule1", LRG_AUTOMATION_TRIGGER_INTERVAL);

    g_assert_true (lrg_automation_add_rule (automation, rule));

    found = lrg_automation_get_rule (automation, "rule1");
    g_assert_nonnull (found);

    /* Duplicate should fail */
    g_assert_false (lrg_automation_add_rule (automation, rule));
}

static void
test_automation_interval_trigger (void)
{
    g_autoptr(LrgAutomation) automation = NULL;
    g_autoptr(LrgAutomationRule) rule = NULL;
    LrgAutomationRule *found;

    automation_trigger_count = 0;

    automation = lrg_automation_new ();
    rule = lrg_automation_rule_new ("ticker", LRG_AUTOMATION_TRIGGER_INTERVAL);
    lrg_automation_rule_set_interval (rule, 1.0);
    lrg_automation_rule_set_callback (rule, test_automation_callback, NULL, NULL);

    lrg_automation_add_rule (automation, rule);

    found = lrg_automation_get_rule (automation, "ticker");
    lrg_automation_rule_set_callback (found, test_automation_callback, NULL, NULL);

    /* Update for 0.5 seconds - should not trigger */
    lrg_automation_update (automation, 0.5, NULL);
    g_assert_cmpint (automation_trigger_count, ==, 0);

    /* Update for another 0.6 seconds - should trigger */
    lrg_automation_update (automation, 0.6, NULL);
    g_assert_cmpint (automation_trigger_count, ==, 1);

    /* Update for 2 seconds - should trigger twice */
    lrg_automation_update (automation, 2.0, NULL);
    g_assert_cmpint (automation_trigger_count, ==, 3);
}

static void
test_automation_threshold_trigger (void)
{
    g_autoptr(LrgAutomation) automation = NULL;
    g_autoptr(LrgAutomationRule) rule = NULL;
    g_autoptr(LrgBigNumber) below = NULL;
    g_autoptr(LrgBigNumber) above = NULL;
    LrgAutomationRule *found;

    automation_trigger_count = 0;

    automation = lrg_automation_new ();
    rule = lrg_automation_rule_new ("threshold", LRG_AUTOMATION_TRIGGER_THRESHOLD);
    lrg_automation_rule_set_threshold_simple (rule, 1000.0);

    lrg_automation_add_rule (automation, rule);

    found = lrg_automation_get_rule (automation, "threshold");
    lrg_automation_rule_set_callback (found, test_automation_callback, NULL, NULL);

    below = lrg_big_number_new (500.0);
    above = lrg_big_number_new (1500.0);

    lrg_automation_update (automation, 0.1, below);
    g_assert_cmpint (automation_trigger_count, ==, 0);

    lrg_automation_update (automation, 0.1, above);
    g_assert_cmpint (automation_trigger_count, ==, 1);
}

static void
test_automation_manual_trigger (void)
{
    g_autoptr(LrgAutomation) automation = NULL;
    g_autoptr(LrgAutomationRule) rule = NULL;
    LrgAutomationRule *found;

    automation_trigger_count = 0;

    automation = lrg_automation_new ();
    rule = lrg_automation_rule_new ("manual", LRG_AUTOMATION_TRIGGER_MANUAL);

    lrg_automation_add_rule (automation, rule);

    found = lrg_automation_get_rule (automation, "manual");
    lrg_automation_rule_set_callback (found, test_automation_callback, NULL, NULL);

    /* Update should not trigger manual rules */
    lrg_automation_update (automation, 1.0, NULL);
    g_assert_cmpint (automation_trigger_count, ==, 0);

    /* Explicit trigger */
    g_assert_true (lrg_automation_trigger (automation, "manual"));
    g_assert_cmpint (automation_trigger_count, ==, 1);
}

static void
test_automation_max_triggers (void)
{
    g_autoptr(LrgAutomation) automation = NULL;
    g_autoptr(LrgAutomationRule) rule = NULL;
    LrgAutomationRule *found;

    automation_trigger_count = 0;

    automation = lrg_automation_new ();
    rule = lrg_automation_rule_new ("limited", LRG_AUTOMATION_TRIGGER_INTERVAL);
    lrg_automation_rule_set_interval (rule, 0.1);
    lrg_automation_rule_set_max_triggers (rule, 3);

    lrg_automation_add_rule (automation, rule);

    found = lrg_automation_get_rule (automation, "limited");
    lrg_automation_rule_set_callback (found, test_automation_callback, NULL, NULL);

    /* Should trigger 3 times then stop */
    lrg_automation_update (automation, 1.0, NULL);

    g_assert_cmpint (automation_trigger_count, ==, 3);
    g_assert_false (lrg_automation_rule_is_enabled (found));
}

static void
test_automation_reset (void)
{
    g_autoptr(LrgAutomation) automation = NULL;
    g_autoptr(LrgAutomationRule) rule = NULL;
    LrgAutomationRule *found;

    automation = lrg_automation_new ();
    rule = lrg_automation_rule_new ("rule", LRG_AUTOMATION_TRIGGER_INTERVAL);
    lrg_automation_rule_set_interval (rule, 1.0);

    lrg_automation_add_rule (automation, rule);

    found = lrg_automation_get_rule (automation, "rule");
    lrg_automation_rule_set_callback (found, test_automation_callback, NULL, NULL);

    lrg_automation_update (automation, 2.5, NULL);

    g_assert_cmpint (lrg_automation_rule_get_trigger_count (found), ==, 2);

    lrg_automation_reset (automation);

    g_assert_cmpint (lrg_automation_rule_get_trigger_count (found), ==, 0);
}

/* ========================================================================= */
/* Main                                                                      */
/* ========================================================================= */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* LrgBigNumber tests */
    g_test_add_func ("/idle/big-number/new", test_big_number_new);
    g_test_add_func ("/idle/big-number/new-from-parts", test_big_number_new_from_parts);
    g_test_add_func ("/idle/big-number/zero", test_big_number_zero);
    g_test_add_func ("/idle/big-number/add", test_big_number_add);
    g_test_add_func ("/idle/big-number/add-different-magnitudes", test_big_number_add_different_magnitudes);
    g_test_add_func ("/idle/big-number/subtract", test_big_number_subtract);
    g_test_add_func ("/idle/big-number/multiply", test_big_number_multiply);
    g_test_add_func ("/idle/big-number/divide", test_big_number_divide);
    g_test_add_func ("/idle/big-number/pow", test_big_number_pow);
    g_test_add_func ("/idle/big-number/compare", test_big_number_compare);
    g_test_add_func ("/idle/big-number/format-short", test_big_number_format_short);
    g_test_add_func ("/idle/big-number/format-scientific", test_big_number_format_scientific);
    g_test_add_func ("/idle/big-number/in-place", test_big_number_in_place);

    /* LrgMilestone tests */
    g_test_add_func ("/idle/milestone/new", test_milestone_new);
    g_test_add_func ("/idle/milestone/check", test_milestone_check);
    g_test_add_func ("/idle/milestone/progress", test_milestone_progress);
    g_test_add_func ("/idle/milestone/reset", test_milestone_reset);

    /* LrgIdleCalculator tests */
    g_test_add_func ("/idle/calculator/new", test_idle_calculator_new);
    g_test_add_func ("/idle/generator/new", test_idle_generator_new);
    g_test_add_func ("/idle/calculator/add-generator", test_idle_calculator_add_generator);
    g_test_add_func ("/idle/calculator/total-rate", test_idle_calculator_total_rate);
    g_test_add_func ("/idle/calculator/simulate", test_idle_calculator_simulate);
    g_test_add_func ("/idle/calculator/global-multiplier", test_idle_calculator_global_multiplier);
    g_test_add_func ("/idle/calculator/snapshot", test_idle_calculator_snapshot);

    /* LrgPrestige tests */
    g_test_add_func ("/idle/prestige/new", test_prestige_new);
    g_test_add_func ("/idle/prestige/can-prestige", test_prestige_can_prestige);
    g_test_add_func ("/idle/prestige/calculate-reward", test_prestige_calculate_reward);
    g_test_add_func ("/idle/prestige/perform", test_prestige_perform);
    g_test_add_func ("/idle/prestige/multiplier", test_prestige_multiplier);
    g_test_add_func ("/idle/prestige/reset", test_prestige_reset);

    /* LrgUnlockTree tests */
    g_test_add_func ("/idle/unlock-tree/new", test_unlock_tree_new);
    g_test_add_func ("/idle/unlock-node/new", test_unlock_node_new);
    g_test_add_func ("/idle/unlock-tree/add-node", test_unlock_tree_add_node);
    g_test_add_func ("/idle/unlock-tree/requirements", test_unlock_tree_requirements);
    g_test_add_func ("/idle/unlock-tree/cycle-prevention", test_unlock_tree_cycle_prevention);
    g_test_add_func ("/idle/unlock-tree/can-unlock", test_unlock_tree_can_unlock);
    g_test_add_func ("/idle/unlock-tree/progress", test_unlock_tree_progress);
    g_test_add_func ("/idle/unlock-tree/reset", test_unlock_tree_reset);

    /* LrgAutomation tests */
    g_test_add_func ("/idle/automation/new", test_automation_new);
    g_test_add_func ("/idle/automation-rule/new", test_automation_rule_new);
    g_test_add_func ("/idle/automation/add-rule", test_automation_add_rule);
    g_test_add_func ("/idle/automation/interval-trigger", test_automation_interval_trigger);
    g_test_add_func ("/idle/automation/threshold-trigger", test_automation_threshold_trigger);
    g_test_add_func ("/idle/automation/manual-trigger", test_automation_manual_trigger);
    g_test_add_func ("/idle/automation/max-triggers", test_automation_max_triggers);
    g_test_add_func ("/idle/automation/reset", test_automation_reset);

    return g_test_run ();
}
