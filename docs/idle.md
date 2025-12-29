# Idle Game Support Module

## Overview

The idle module provides essential components for building idle/incremental games, including arbitrary precision numbers, offline progress calculation, prestige systems, unlock trees, milestones, and automation.

## Core Concepts

### Big Numbers

Idle games quickly exceed normal numeric ranges. `LrgBigNumber` represents values using scientific notation (mantissa × 10^exponent), allowing representation of values up to 10^308 and beyond.

### Offline Progress

When players return after being away, `LrgIdleCalculator` simulates production that occurred during the offline period, with configurable efficiency multipliers.

### Prestige Systems

Prestige allows players to reset progress in exchange for permanent bonuses, creating compelling progression loops. `LrgPrestige` handles reward calculation and multiplier stacking.

### Unlock Trees

`LrgUnlockTree` manages dependency graphs of unlockable content, ensuring prerequisites are met before allowing purchases.

### Milestones

`LrgMilestone` tracks achievement checkpoints that can trigger rewards or unlock content.

### Automation

`LrgAutomation` provides auto-click and auto-buy functionality through configurable rules with interval or threshold triggers.

## Classes

### LrgBigNumber (Boxed Type)

Arbitrary precision number using mantissa × 10^exponent representation.

```c
/* Creation */
LrgBigNumber *bn = lrg_big_number_new (1234567.89);
LrgBigNumber *bn2 = lrg_big_number_new_from_parts (1.5, 12);  /* 1.5e12 */
LrgBigNumber *zero = lrg_big_number_new_zero ();

/* Access */
gdouble mantissa = lrg_big_number_get_mantissa (bn);  /* 1.0 <= m < 10.0 */
gint64 exponent = lrg_big_number_get_exponent (bn);
gdouble value = lrg_big_number_to_double (bn);  /* May overflow */

/* Arithmetic */
LrgBigNumber *sum = lrg_big_number_add (a, b);
LrgBigNumber *diff = lrg_big_number_subtract (a, b);
LrgBigNumber *prod = lrg_big_number_multiply (a, b);
LrgBigNumber *quot = lrg_big_number_divide (a, b);
LrgBigNumber *power = lrg_big_number_pow (base, 2.5);
LrgBigNumber *scaled = lrg_big_number_multiply_scalar (bn, 1.5);

/* In-place operations (modifies first argument) */
lrg_big_number_add_in_place (accumulator, delta);
lrg_big_number_multiply_in_place (value, 2.0);

/* Comparison */
gint cmp = lrg_big_number_compare (a, b);  /* -1, 0, 1 */
gboolean eq = lrg_big_number_equals (a, b);
gboolean lt = lrg_big_number_less_than (a, b);
gboolean gt = lrg_big_number_greater_than (a, b);

/* Formatting */
gchar *short_fmt = lrg_big_number_format_short (bn);       /* "1.50M", "2.30B" */
gchar *sci_fmt = lrg_big_number_format_scientific (bn);    /* "1.50e6" */
gchar *eng_fmt = lrg_big_number_format_engineering (bn);   /* "1.50e6" */
```

**Suffix Table:**

| Exponent | Suffix | Name |
|----------|--------|------|
| 3 | K | Thousand |
| 6 | M | Million |
| 9 | B | Billion |
| 12 | T | Trillion |
| 15 | Qa | Quadrillion |
| 18 | Qi | Quintillion |
| 21 | Sx | Sextillion |
| 24 | Sp | Septillion |
| 27 | Oc | Octillion |
| 30 | No | Nonillion |
| 33 | Dc | Decillion |
| 36+ | e## | Scientific |

### LrgMilestone (Boxed Type)

Achievement/milestone checkpoint for tracking progress.

```c
/* Creation */
LrgMilestone *milestone = lrg_milestone_new_simple ("first_1000", "First 1000", 1000.0);

/* With LrgBigNumber threshold */
LrgBigNumber *thresh = lrg_big_number_new_from_parts (1.0, 9);
LrgMilestone *big_milestone = lrg_milestone_new ("billion", "Billionaire", thresh);

/* Configuration */
lrg_milestone_set_description (milestone, "Reach 1000 coins");
lrg_milestone_set_icon (milestone, "icons/trophy.png");
lrg_milestone_set_reward_multiplier (milestone, 1.5);  /* 50% bonus on achieve */

/* Checking */
LrgBigNumber *current = lrg_big_number_new (1500.0);
if (lrg_milestone_check (milestone, current))
{
    /* Newly achieved! */
    gint64 when = lrg_milestone_get_achieved_time (milestone);
    gdouble bonus = lrg_milestone_get_reward_multiplier (milestone);
}

/* Progress tracking */
gdouble progress = lrg_milestone_get_progress (milestone, current);  /* 0.0 - 1.0 */

/* Reset for prestige */
lrg_milestone_reset (milestone);
```

### LrgIdleCalculator (Final GObject)

Manages generators and calculates production rates.

```c
/* Creation */
LrgIdleCalculator *calc = lrg_idle_calculator_new ();

/* Add generators */
LrgIdleGenerator *coins = lrg_idle_generator_new_simple ("coins", 1.0);  /* 1/sec base */
lrg_idle_calculator_add_generator (calc, coins);

/* Configure generator */
LrgIdleGenerator *gen = lrg_idle_calculator_get_generator (calc, "coins");
lrg_idle_generator_set_count (gen, 10);       /* Own 10 of them */
lrg_idle_generator_set_multiplier (gen, 2.0); /* 2x boost */
lrg_idle_generator_set_enabled (gen, TRUE);

/* Get effective rate: 1.0 * 10 * 2.0 = 20/sec */
LrgBigNumber *rate = lrg_idle_generator_get_effective_rate (gen);

/* Global multiplier (from prestige, etc.) */
lrg_idle_calculator_set_global_multiplier (calc, 1.5);

/* Total rate across all generators */
LrgBigNumber *total = lrg_idle_calculator_get_total_rate (calc);

/* Simulate time passage */
LrgBigNumber *production = lrg_idle_calculator_simulate (calc, 60.0);  /* 1 minute */

/* Offline calculation */
lrg_idle_calculator_take_snapshot (calc);  /* On app close */

/* Later, on app open */
gint64 last_time = lrg_idle_calculator_get_snapshot_time (calc);
LrgBigNumber *offline = lrg_idle_calculator_simulate_offline (
    calc,
    last_time,
    0.5,   /* 50% offline efficiency */
    8.0    /* Max 8 hours */
);
```

### LrgPrestige (Derivable GObject)

Prestige layer reset mechanics with reward calculation.

```c
/* Creation */
LrgPrestige *prestige = lrg_prestige_new ();

/* Configuration */
lrg_prestige_set_id (prestige, "prestige1");
lrg_prestige_set_name (prestige, "Prestige");
lrg_prestige_set_threshold_simple (prestige, 1000000.0);  /* Need 1M to prestige */
lrg_prestige_set_scaling_exponent (prestige, 0.5);        /* reward = (value/threshold)^0.5 */

/* Check availability */
LrgBigNumber *current_coins = get_player_coins ();
if (lrg_prestige_can_prestige (prestige, current_coins))
{
    /* Preview reward */
    LrgBigNumber *pending = lrg_prestige_calculate_reward (prestige, current_coins);

    /* Perform prestige */
    LrgBigNumber *gained = lrg_prestige_perform (prestige, current_coins);
    /* Reset player coins here */
}

/* Get bonus multiplier from accumulated prestige points */
gdouble mult = lrg_prestige_get_bonus_multiplier (prestige);
/* Apply to production: default formula is 1.0 + sqrt(points) * 0.1 */

/* Stats */
gint64 count = lrg_prestige_get_times_prestiged (prestige);
const LrgBigNumber *points = lrg_prestige_get_points (prestige);

/* Signal handling */
g_signal_connect (prestige, "prestige-performed",
                  G_CALLBACK (on_prestige_performed), NULL);
```

**Subclassing for Custom Formulas:**

```c
/* Custom prestige class */
G_DECLARE_FINAL_TYPE (MyPrestige, my_prestige, MY, PRESTIGE, LrgPrestige)

struct _MyPrestige { LrgPrestige parent; };

G_DEFINE_TYPE (MyPrestige, my_prestige, LRG_TYPE_PRESTIGE)

static LrgBigNumber *
my_prestige_calculate_reward (LrgPrestige *self, const LrgBigNumber *value)
{
    /* Custom formula: log2(value / threshold) */
    /* ... */
}

static gdouble
my_prestige_get_bonus_multiplier (LrgPrestige *self, const LrgBigNumber *points)
{
    /* Custom multiplier formula */
    return 1.0 + lrg_big_number_to_double (points) * 0.01;
}

static void
my_prestige_class_init (MyPrestigeClass *klass)
{
    LrgPrestigeClass *prestige_class = LRG_PRESTIGE_CLASS (klass);
    prestige_class->calculate_reward = my_prestige_calculate_reward;
    prestige_class->get_bonus_multiplier = my_prestige_get_bonus_multiplier;
}
```

### LrgUnlockTree (Final GObject)

Dependency graph for unlockable content.

```c
/* Creation */
LrgUnlockTree *tree = lrg_unlock_tree_new ();

/* Add nodes */
LrgUnlockNode *basic = lrg_unlock_node_new ("basic", "Basic Upgrade");
lrg_unlock_node_set_cost_simple (basic, 100.0);
lrg_unlock_node_set_description (basic, "Doubles production");
lrg_unlock_node_set_tier (basic, 0);
lrg_unlock_tree_add_node (tree, basic);

LrgUnlockNode *advanced = lrg_unlock_node_new ("advanced", "Advanced Upgrade");
lrg_unlock_node_set_cost_simple (advanced, 500.0);
lrg_unlock_node_set_tier (advanced, 1);
lrg_unlock_tree_add_node (tree, advanced);

/* Set requirements (advanced requires basic) */
lrg_unlock_tree_add_requirement (tree, "advanced", "basic");

/* Check if can unlock */
LrgBigNumber *points = get_player_prestige_points ();
if (lrg_unlock_tree_can_unlock (tree, "basic", points))
{
    /* Deduct cost manually */
    LrgUnlockNode *node = lrg_unlock_tree_get_node (tree, "basic");
    deduct_points (lrg_unlock_node_get_cost (node));

    /* Mark as unlocked */
    lrg_unlock_tree_unlock (tree, "basic");
}

/* Query state */
gboolean is_unlocked = lrg_unlock_tree_is_unlocked (tree, "basic");
GPtrArray *available = lrg_unlock_tree_get_available (tree, points);
GPtrArray *unlocked = lrg_unlock_tree_get_unlocked (tree);
gdouble progress = lrg_unlock_tree_get_progress (tree);

/* Get requirements and dependents */
GPtrArray *reqs = lrg_unlock_tree_get_requirements (tree, "advanced");
GPtrArray *deps = lrg_unlock_tree_get_dependents (tree, "basic");

/* Reset for prestige */
lrg_unlock_tree_reset (tree);

/* Signals */
g_signal_connect (tree, "node-unlocked", G_CALLBACK (on_node_unlocked), NULL);
```

### LrgAutomation (Final GObject)

Auto-click and auto-buy automation system.

```c
/* Creation */
LrgAutomation *automation = lrg_automation_new ();

/* Interval-based rule (auto-clicker) */
LrgAutomationRule *clicker = lrg_automation_rule_new ("auto_click",
                                                       LRG_AUTOMATION_TRIGGER_INTERVAL);
lrg_automation_rule_set_name (clicker, "Auto Clicker");
lrg_automation_rule_set_interval (clicker, 0.1);  /* Every 100ms */
lrg_automation_rule_set_callback (clicker, on_auto_click, game, NULL);
lrg_automation_add_rule (automation, clicker);

/* Threshold-based rule (auto-buy) */
LrgAutomationRule *buyer = lrg_automation_rule_new ("auto_buy",
                                                     LRG_AUTOMATION_TRIGGER_THRESHOLD);
lrg_automation_rule_set_threshold_simple (buyer, 1000.0);  /* When coins >= 1000 */
lrg_automation_rule_set_callback (buyer, on_auto_buy, game, NULL);
lrg_automation_add_rule (automation, buyer);

/* Manual trigger only */
LrgAutomationRule *boost = lrg_automation_rule_new ("boost",
                                                     LRG_AUTOMATION_TRIGGER_MANUAL);
lrg_automation_rule_set_max_triggers (boost, 10);  /* Limited uses */
lrg_automation_add_rule (automation, boost);

/* In game loop */
void game_update (gdouble delta_time)
{
    LrgBigNumber *coins = get_coins ();
    lrg_automation_update (automation, delta_time, coins);
}

/* Manual trigger */
lrg_automation_trigger (automation, "boost");

/* Global enable/disable */
lrg_automation_set_enabled (automation, FALSE);  /* Pause all automation */

/* Per-rule enable/disable */
LrgAutomationRule *rule = lrg_automation_get_rule (automation, "auto_click");
lrg_automation_rule_set_enabled (rule, FALSE);

/* Stats */
gint64 triggers = lrg_automation_rule_get_trigger_count (rule);

/* Reset all timers and counts */
lrg_automation_reset (automation);

/* Callback signature */
static gboolean
on_auto_click (LrgAutomationRule *rule, gpointer user_data)
{
    Game *game = user_data;
    add_coins (game, 1);
    return TRUE;  /* Continue automation */
}
```

## Integration Example

Complete idle game setup:

```c
typedef struct {
    LrgBigNumber      *coins;
    LrgIdleCalculator *calculator;
    LrgPrestige       *prestige;
    LrgUnlockTree     *upgrades;
    LrgAutomation     *automation;
    GPtrArray         *milestones;
} IdleGame;

IdleGame *
idle_game_new (void)
{
    IdleGame *game = g_new0 (IdleGame, 1);

    game->coins = lrg_big_number_new_zero ();
    game->calculator = lrg_idle_calculator_new ();
    game->prestige = lrg_prestige_new ();
    game->upgrades = lrg_unlock_tree_new ();
    game->automation = lrg_automation_new ();
    game->milestones = g_ptr_array_new_with_free_func (
        (GDestroyNotify)lrg_milestone_free);

    /* Setup generators */
    LrgIdleGenerator *clicker = lrg_idle_generator_new_simple ("click", 0.0);
    lrg_idle_generator_set_count (clicker, 1);
    lrg_idle_calculator_add_generator (game->calculator, clicker);

    LrgIdleGenerator *worker = lrg_idle_generator_new_simple ("worker", 1.0);
    lrg_idle_calculator_add_generator (game->calculator, worker);

    /* Setup prestige */
    lrg_prestige_set_threshold_simple (game->prestige, 1000000.0);

    /* Setup milestones */
    g_ptr_array_add (game->milestones,
        lrg_milestone_new_simple ("m1", "First Thousand", 1000.0));
    g_ptr_array_add (game->milestones,
        lrg_milestone_new_simple ("m2", "First Million", 1000000.0));

    return game;
}

void
idle_game_update (IdleGame *game, gdouble delta)
{
    /* Accumulate production */
    g_autoptr(LrgBigNumber) production = lrg_idle_calculator_simulate (
        game->calculator, delta);
    lrg_big_number_add_in_place (game->coins, production);

    /* Check milestones */
    for (guint i = 0; i < game->milestones->len; i++)
    {
        LrgMilestone *m = g_ptr_array_index (game->milestones, i);
        if (lrg_milestone_check (m, game->coins))
        {
            /* Milestone achieved! */
            show_achievement (lrg_milestone_get_name (m));
        }
    }

    /* Update automation */
    lrg_automation_update (game->automation, delta, game->coins);
}

void
idle_game_click (IdleGame *game)
{
    LrgIdleGenerator *clicker = lrg_idle_calculator_get_generator (
        game->calculator, "click");
    g_autoptr(LrgBigNumber) click_value = lrg_idle_generator_get_effective_rate (clicker);

    /* Apply prestige multiplier */
    gdouble mult = lrg_prestige_get_bonus_multiplier (game->prestige);
    lrg_big_number_multiply_in_place (click_value, mult);

    lrg_big_number_add_in_place (game->coins, click_value);
}

void
idle_game_prestige (IdleGame *game)
{
    if (!lrg_prestige_can_prestige (game->prestige, game->coins))
        return;

    LrgBigNumber *reward = lrg_prestige_perform (game->prestige, game->coins);

    /* Reset game state */
    lrg_big_number_free (game->coins);
    game->coins = lrg_big_number_new_zero ();

    /* Reset generators */
    GPtrArray *gens = lrg_idle_calculator_get_generators (game->calculator);
    for (guint i = 0; i < gens->len; i++)
    {
        LrgIdleGenerator *gen = g_ptr_array_index (gens, i);
        lrg_idle_generator_set_count (gen, 0);
    }

    /* Reset milestones */
    for (guint i = 0; i < game->milestones->len; i++)
    {
        LrgMilestone *m = g_ptr_array_index (game->milestones, i);
        lrg_milestone_reset (m);
    }

    /* Apply new multiplier */
    gdouble mult = lrg_prestige_get_bonus_multiplier (game->prestige);
    lrg_idle_calculator_set_global_multiplier (game->calculator, mult);

    lrg_big_number_free (reward);
}
```

## Save/Load Support

All idle types support serialization for save games:

```c
/* Save */
void save_game (IdleGame *game)
{
    /* Big numbers can be serialized as mantissa + exponent */
    save_double ("coins_mantissa", lrg_big_number_get_mantissa (game->coins));
    save_int64 ("coins_exponent", lrg_big_number_get_exponent (game->coins));

    /* Calculator snapshot */
    lrg_idle_calculator_take_snapshot (game->calculator);
    save_int64 ("last_active", lrg_idle_calculator_get_snapshot_time (game->calculator));

    /* Prestige points */
    const LrgBigNumber *points = lrg_prestige_get_points (game->prestige);
    save_double ("prestige_mantissa", lrg_big_number_get_mantissa (points));
    save_int64 ("prestige_exponent", lrg_big_number_get_exponent (points));
}

/* Load */
void load_game (IdleGame *game)
{
    /* Restore coins */
    gdouble mantissa = load_double ("coins_mantissa");
    gint64 exponent = load_int64 ("coins_exponent");
    lrg_big_number_free (game->coins);
    game->coins = lrg_big_number_new_from_parts (mantissa, exponent);

    /* Calculate offline progress */
    gint64 last_time = load_int64 ("last_active");
    lrg_idle_calculator_set_snapshot_time (game->calculator, last_time);

    LrgBigNumber *offline = lrg_idle_calculator_simulate_offline (
        game->calculator, last_time, 0.5, 24.0);

    show_offline_earnings (offline);
    lrg_big_number_add_in_place (game->coins, offline);
}
```

## Performance Notes

- `LrgBigNumber` operations are O(1) since they use mantissa/exponent representation
- Avoid creating temporary big numbers in tight loops; use in-place operations
- `lrg_big_number_to_double()` may lose precision for very large numbers
- `LrgIdleCalculator` is optimized for many generators; use `set_enabled(FALSE)` for inactive ones
