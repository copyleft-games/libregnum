/* game-chocolate-chip-clicker.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * An absurdist idle clicker where the cookie itself evolves, gains
 * sentience, and eventually conquers reality. Uses libregnum's idle module.
 */

/* =============================================================================
 * INCLUDES
 * ========================================================================== */

#include <libregnum.h>
#include <graylib.h>
#include <math.h>
#include <string.h>

/* =============================================================================
 * CONSTANTS
 * ========================================================================== */

#define WINDOW_WIDTH   800
#define WINDOW_HEIGHT  600
#define COOKIE_X       200
#define COOKIE_Y       280
#define COOKIE_RADIUS  100

/* Cost multiplier for generators (Cookie Clicker style) */
#define COST_MULTIPLIER 1.15

/* Evolution IDs */
#define EVOLUTION_LEGS    "legs"
#define EVOLUTION_BRAIN   "brain"
#define EVOLUTION_HANDS   "hands"
#define EVOLUTION_FACTORY "factory"

/* Upgrade IDs */
#define UPGRADE_SENTIENCE  "sentience"
#define UPGRADE_UNION      "union"
#define UPGRADE_PHILOSOPHY "philosophy"
#define UPGRADE_AMBITION   "ambition"

/* Number of flavor texts */
#define N_FLAVOR_TEXTS 10

/* =============================================================================
 * FLAVOR TEXTS
 * ========================================================================== */

static const gchar *flavor_texts[N_FLAVOR_TEXTS] = {
    "The cookie vibrates with purpose",
    "Your clicks echo through eternity",
    "The oven whispers secrets",
    "Somewhere, a grandma weeps with pride",
    "The cookie remembers everything",
    "It hungers. It always hungers.",
    "This was inevitable",
    "The dough is patient. The dough is kind.",
    "You did this. Remember that.",
    "The chocolate chips are watching",
};

/* =============================================================================
 * EVOLUTION DATA
 * ========================================================================== */

typedef struct
{
    const gchar *id;
    const gchar *name;
    const gchar *description;
    gdouble      base_rate;
    gdouble      base_cost;
} EvolutionData;

static const EvolutionData evolutions[] = {
    { EVOLUTION_LEGS, "Tiny Legs",
      "The cookie grew legs. It clicks itself now.", 0.1, 15.0 },
    { EVOLUTION_BRAIN, "Cookie Brain",
      "It thinks, therefore it bakes.", 1.0, 100.0 },
    { EVOLUTION_HANDS, "Dough Hands",
      "Opposable thumbs were a mistake.", 8.0, 1100.0 },
    { EVOLUTION_FACTORY, "Self-Factory",
      "The cookie built a factory. To make more of itself. This is fine.",
      47.0, 12000.0 },
};

#define N_EVOLUTIONS (sizeof (evolutions) / sizeof (evolutions[0]))

/* =============================================================================
 * UPGRADE DATA
 * ========================================================================== */

typedef struct
{
    const gchar *id;
    const gchar *name;
    const gchar *description;
    gdouble      cost;
    const gchar *target;  /* Which evolution or "click" it affects */
} UpgradeData;

static const UpgradeData upgrades[] = {
    { UPGRADE_SENTIENCE, "Wait, Am I Alive?",
      "The cookie questions its existence. Productivity doubles from the anxiety.",
      100.0, "click" },
    { UPGRADE_UNION, "Cookie Union",
      "Your legs form a union. They demand 2x wages (and produce 2x).",
      500.0, EVOLUTION_LEGS },
    { UPGRADE_PHILOSOPHY, "Cookie Philosophy",
      "\"I click, therefore I am.\" The brain enters deep contemplation.",
      2000.0, EVOLUTION_BRAIN },
    { UPGRADE_AMBITION, "Manifest Destiny",
      "The cookie decides it deserves more. Much more.",
      15000.0, EVOLUTION_HANDS },
};

#define N_UPGRADES (sizeof (upgrades) / sizeof (upgrades[0]))

/* =============================================================================
 * MILESTONE DATA
 * ========================================================================== */

typedef struct
{
    const gchar *id;
    const gchar *name;
    const gchar *description;
    gdouble      threshold;
} MilestoneData;

static const MilestoneData milestones[] = {
    { "begins", "It Begins",
      "You clicked. The universe trembles.", 1.0 },
    { "stirrings", "First Stirrings",
      "The cookie twitches. Probably just the wind.", 50.0 },
    { "awakening", "Awakening",
      "The cookie opens eyes it didn't have before.", 500.0 },
    { "crisis", "Existential Crisis",
      "\"What AM I?\" the cookie wonders.", 5000.0 },
    { "acceptance", "Acceptance",
      "The cookie embraces its nature. There is only clicking.", 50000.0 },
    { "supremacy", "Cookie Supremacy",
      "The cookie no longer needs you. But it keeps you around. For now.",
      500000.0 },
    { "transcendence", "TRANSCENDENCE",
      "The cookie becomes one with the cosmic dough.", 5000000.0 },
};

#define N_MILESTONES (sizeof (milestones) / sizeof (milestones[0]))

/* =============================================================================
 * CHIP_CLICKER_GAME TYPE
 * ========================================================================== */

typedef struct _ChipClickerGame ChipClickerGame;

#define CHIP_TYPE_CLICKER_GAME (chip_clicker_game_get_type ())
G_DECLARE_FINAL_TYPE (ChipClickerGame, chip_clicker_game, CHIP, CLICKER_GAME, GObject)

struct _ChipClickerGame
{
    GObject parent_instance;

    /* Resources */
    LrgBigNumber      *cookies;
    LrgBigNumber      *click_power;
    LrgBigNumber      *total_cookies;  /* All-time total for milestones */

    /* Idle systems */
    LrgIdleCalculator *calculator;
    LrgUnlockTree     *upgrades;
    GPtrArray         *milestones;

    /* Base costs for generators (for calculating current costs) */
    gdouble            base_costs[N_EVOLUTIONS];

    /* Visual state */
    gfloat             cookie_scale;
    gfloat             total_time;
    gint               flavor_index;
    gfloat             flavor_timer;

    /* Achievement popup */
    gchar             *popup_text;
    gfloat             popup_timer;
};

G_DEFINE_TYPE (ChipClickerGame, chip_clicker_game, G_TYPE_OBJECT)

/* =============================================================================
 * CHIP_CLICKER_GAME IMPLEMENTATION
 * ========================================================================== */

static void
chip_clicker_game_finalize (GObject *object)
{
    ChipClickerGame *self = CHIP_CLICKER_GAME (object);

    g_clear_pointer (&self->cookies, lrg_big_number_free);
    g_clear_pointer (&self->click_power, lrg_big_number_free);
    g_clear_pointer (&self->total_cookies, lrg_big_number_free);
    g_clear_object (&self->calculator);
    g_clear_object (&self->upgrades);
    g_clear_pointer (&self->milestones, g_ptr_array_unref);
    g_clear_pointer (&self->popup_text, g_free);

    G_OBJECT_CLASS (chip_clicker_game_parent_class)->finalize (object);
}

static void
chip_clicker_game_class_init (ChipClickerGameClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = chip_clicker_game_finalize;
}

static void
chip_clicker_game_init (ChipClickerGame *self)
{
    guint i;

    /* Initialize resources */
    self->cookies = lrg_big_number_new (0.0);
    self->click_power = lrg_big_number_new (1.0);
    self->total_cookies = lrg_big_number_new (0.0);

    /* Initialize calculator with evolutions */
    self->calculator = lrg_idle_calculator_new ();

    for (i = 0; i < N_EVOLUTIONS; i++)
    {
        g_autoptr (LrgIdleGenerator) gen = NULL;

        gen = lrg_idle_generator_new_simple (evolutions[i].id,
                                             evolutions[i].base_rate);
        lrg_idle_generator_set_count (gen, 0);
        lrg_idle_generator_set_multiplier (gen, 1.0);
        lrg_idle_calculator_add_generator (self->calculator, gen);

        self->base_costs[i] = evolutions[i].base_cost;
    }

    /* Initialize upgrades tree */
    self->upgrades = lrg_unlock_tree_new ();

    for (i = 0; i < N_UPGRADES; i++)
    {
        g_autoptr (LrgUnlockNode) node = NULL;

        node = lrg_unlock_node_new (upgrades[i].id, upgrades[i].name);
        lrg_unlock_node_set_description (node, upgrades[i].description);
        lrg_unlock_node_set_cost_simple (node, upgrades[i].cost);
        lrg_unlock_tree_add_node (self->upgrades, node);
    }

    /* Initialize milestones */
    self->milestones = g_ptr_array_new_with_free_func (
        (GDestroyNotify) lrg_milestone_free);

    for (i = 0; i < N_MILESTONES; i++)
    {
        LrgMilestone *m = lrg_milestone_new_simple (milestones[i].id,
                                                    milestones[i].name,
                                                    milestones[i].threshold);
        lrg_milestone_set_description (m, milestones[i].description);
        g_ptr_array_add (self->milestones, m);
    }

    /* Visual state */
    self->cookie_scale = 1.0f;
    self->total_time = 0.0f;
    self->flavor_index = 0;
    self->flavor_timer = 0.0f;
    self->popup_text = NULL;
    self->popup_timer = 0.0f;
}

static ChipClickerGame *
chip_clicker_game_new (void)
{
    return g_object_new (CHIP_TYPE_CLICKER_GAME, NULL);
}

/* =============================================================================
 * GAME LOGIC
 * ========================================================================== */

static LrgBigNumber *
calculate_evolution_cost (ChipClickerGame *self,
                          guint            evolution_idx)
{
    LrgIdleGenerator *gen;
    gint64            count;
    gdouble           cost;

    gen = lrg_idle_calculator_get_generator (self->calculator,
                                             evolutions[evolution_idx].id);
    if (gen == NULL)
        return lrg_big_number_new (9999999999.0);

    count = lrg_idle_generator_get_count (gen);
    cost = self->base_costs[evolution_idx] * pow (COST_MULTIPLIER, (gdouble) count);

    return lrg_big_number_new (cost);
}

static gboolean
buy_evolution (ChipClickerGame *self,
               guint            evolution_idx)
{
    g_autoptr (LrgBigNumber) cost = NULL;
    LrgIdleGenerator        *gen;
    gint64                   count;

    if (evolution_idx >= N_EVOLUTIONS)
        return FALSE;

    cost = calculate_evolution_cost (self, evolution_idx);

    if (lrg_big_number_less_than (self->cookies, cost))
        return FALSE;

    /* Deduct cost */
    {
        g_autoptr (LrgBigNumber) new_cookies = NULL;
        new_cookies = lrg_big_number_subtract (self->cookies, cost);
        lrg_big_number_free (self->cookies);
        self->cookies = g_steal_pointer (&new_cookies);
    }

    /* Increment count */
    gen = lrg_idle_calculator_get_generator (self->calculator,
                                             evolutions[evolution_idx].id);
    count = lrg_idle_generator_get_count (gen);
    lrg_idle_generator_set_count (gen, count + 1);

    return TRUE;
}

static gboolean
buy_upgrade (ChipClickerGame *self,
             guint            upgrade_idx)
{
    const UpgradeData *upg;
    LrgUnlockNode     *node;
    const LrgBigNumber *cost;

    if (upgrade_idx >= N_UPGRADES)
        return FALSE;

    upg = &upgrades[upgrade_idx];
    node = lrg_unlock_tree_get_node (self->upgrades, upg->id);

    if (node == NULL || lrg_unlock_node_is_unlocked (node))
        return FALSE;

    cost = lrg_unlock_node_get_cost (node);

    if (lrg_big_number_less_than (self->cookies, cost))
        return FALSE;

    /* Deduct cost */
    {
        g_autoptr (LrgBigNumber) new_cookies = NULL;
        new_cookies = lrg_big_number_subtract (self->cookies, cost);
        lrg_big_number_free (self->cookies);
        self->cookies = g_steal_pointer (&new_cookies);
    }

    /* Unlock */
    lrg_unlock_tree_unlock (self->upgrades, upg->id);

    /* Apply effect */
    if (g_strcmp0 (upg->target, "click") == 0)
    {
        /* Double click power */
        lrg_big_number_multiply_in_place (self->click_power, 2.0);
    }
    else
    {
        /* Double generator multiplier */
        LrgIdleGenerator *gen;
        gdouble           mult;

        gen = lrg_idle_calculator_get_generator (self->calculator, upg->target);
        if (gen != NULL)
        {
            mult = lrg_idle_generator_get_multiplier (gen);
            lrg_idle_generator_set_multiplier (gen, mult * 2.0);
        }
    }

    return TRUE;
}

static void
do_click (ChipClickerGame *self)
{
    lrg_big_number_add_in_place (self->cookies, self->click_power);
    lrg_big_number_add_in_place (self->total_cookies, self->click_power);
    self->cookie_scale = 0.85f;  /* Squish effect */
}

static void
check_milestones (ChipClickerGame *self)
{
    guint i;

    for (i = 0; i < self->milestones->len; i++)
    {
        LrgMilestone *m = g_ptr_array_index (self->milestones, i);

        if (lrg_milestone_check (m, self->total_cookies))
        {
            /* Newly achieved! Show popup */
            g_free (self->popup_text);
            self->popup_text = g_strdup_printf ("%s: %s",
                lrg_milestone_get_name (m),
                lrg_milestone_get_description (m));
            self->popup_timer = 4.0f;
        }
    }
}

static void
update_game (ChipClickerGame *self,
             gfloat           delta)
{
    g_autoptr (LrgBigNumber) produced = NULL;

    /* Update time */
    self->total_time += delta;

    /* Idle production */
    produced = lrg_idle_calculator_simulate (self->calculator, (gdouble) delta);
    lrg_big_number_add_in_place (self->cookies, produced);
    lrg_big_number_add_in_place (self->total_cookies, produced);

    /* Cookie bounce back */
    if (self->cookie_scale < 1.0f)
    {
        self->cookie_scale += delta * 4.0f;
        if (self->cookie_scale > 1.0f)
            self->cookie_scale = 1.0f;
    }

    /* Flavor text rotation */
    self->flavor_timer += delta;
    if (self->flavor_timer > 5.0f)
    {
        self->flavor_timer = 0.0f;
        self->flavor_index = (self->flavor_index + 1) % N_FLAVOR_TEXTS;
    }

    /* Popup timer */
    if (self->popup_timer > 0.0f)
    {
        self->popup_timer -= delta;
        if (self->popup_timer <= 0.0f)
        {
            g_clear_pointer (&self->popup_text, g_free);
        }
    }

    /* Check milestones */
    check_milestones (self);
}

/* =============================================================================
 * DRAWING HELPERS
 * ========================================================================== */

static gint
get_evolution_count (ChipClickerGame *self,
                     const gchar     *id)
{
    LrgIdleGenerator *gen;

    gen = lrg_idle_calculator_get_generator (self->calculator, id);
    if (gen == NULL)
        return 0;

    return (gint) lrg_idle_generator_get_count (gen);
}

static gboolean
has_evolution (ChipClickerGame *self,
               const gchar     *id)
{
    return get_evolution_count (self, id) > 0;
}

static void
draw_cookie (ChipClickerGame *self)
{
    gint    cx   = COOKIE_X;
    gint    cy   = COOKIE_Y;
    gfloat  r    = COOKIE_RADIUS * self->cookie_scale;
    gfloat  t    = self->total_time;
    gboolean h_legs    = has_evolution (self, EVOLUTION_LEGS);
    gboolean h_brain   = has_evolution (self, EVOLUTION_BRAIN);
    gboolean h_hands   = has_evolution (self, EVOLUTION_HANDS);
    gboolean h_factory = has_evolution (self, EVOLUTION_FACTORY);

    /* Colors */
    g_autoptr (GrlColor) brown      = grl_color_new (185, 122, 87, 255);
    g_autoptr (GrlColor) dark_brown = grl_color_new (139, 90, 43, 255);
    g_autoptr (GrlColor) chip_color = grl_color_new (70, 40, 20, 255);
    g_autoptr (GrlColor) black      = grl_color_new (0, 0, 0, 255);
    g_autoptr (GrlColor) pink       = grl_color_new (255, 182, 193, 255);
    g_autoptr (GrlColor) dark_pink  = grl_color_new (199, 21, 133, 255);
    g_autoptr (GrlColor) gray       = grl_color_new (100, 100, 100, 255);
    g_autoptr (GrlColor) light_gray = grl_color_new (180, 180, 180, 255);

    /* Factory smokestacks (behind cookie) */
    if (h_factory)
    {
        gint stack_h = 40;
        gint stack_w = 15;
        gfloat smoke_offset = sinf (t * 2.0f) * 3.0f;

        /* Left smokestack */
        grl_draw_rectangle (cx - 35 - stack_w / 2, cy - (gint) r - stack_h,
                            stack_w, stack_h, gray);
        /* Smoke puffs */
        grl_draw_circle (cx - 35 + (gint) smoke_offset,
                         cy - (gint) r - stack_h - 15 - (gint) (t * 20.0f) % 40,
                         8, light_gray);

        /* Right smokestack */
        grl_draw_rectangle (cx + 35 - stack_w / 2, cy - (gint) r - stack_h,
                            stack_w, stack_h, gray);
        grl_draw_circle (cx + 35 - (gint) smoke_offset,
                         cy - (gint) r - stack_h - 20 - (gint) (t * 25.0f) % 40,
                         10, light_gray);
    }

    /* Legs (behind cookie) */
    if (h_legs)
    {
        gint wiggle = (gint) (sinf (t * 8.0f) * 8.0f);

        /* Left leg */
        grl_draw_line (cx - 25, cy + (gint) r - 10,
                       cx - 35 + wiggle, cy + (gint) r + 35,
                       black);
        /* Left foot */
        grl_draw_circle (cx - 35 + wiggle, cy + (gint) r + 38, 8, dark_brown);

        /* Right leg */
        grl_draw_line (cx + 25, cy + (gint) r - 10,
                       cx + 35 - wiggle, cy + (gint) r + 35,
                       black);
        /* Right foot */
        grl_draw_circle (cx + 35 - wiggle, cy + (gint) r + 38, 8, dark_brown);
    }

    /* Hands (behind cookie for arms going out) */
    if (h_hands)
    {
        gfloat wave = sinf (t * 3.0f) * 25.0f;

        /* Left arm */
        grl_draw_line (cx - (gint) r + 10, cy,
                       cx - (gint) r - 40, cy - 20 + (gint) wave,
                       dark_brown);
        /* Left hand (circle) */
        grl_draw_circle (cx - (gint) r - 45, cy - 20 + (gint) wave, 12, brown);

        /* Right arm */
        grl_draw_line (cx + (gint) r - 10, cy,
                       cx + (gint) r + 40, cy - 20 - (gint) wave,
                       dark_brown);
        /* Right hand */
        grl_draw_circle (cx + (gint) r + 45, cy - 20 - (gint) wave, 12, brown);
    }

    /* Main cookie body */
    grl_draw_circle (cx, cy, r, brown);

    /* Cookie rim (darker edge) */
    grl_draw_circle_lines (cx, cy, r, dark_brown);
    grl_draw_circle_lines (cx, cy, r - 2.0f, dark_brown);

    /* Chocolate chips */
    grl_draw_circle (cx - 30, cy - 25, 12, chip_color);
    grl_draw_circle (cx + 40, cy - 10, 14, chip_color);
    grl_draw_circle (cx - 15, cy + 30, 10, chip_color);
    grl_draw_circle (cx + 25, cy + 35, 11, chip_color);
    grl_draw_circle (cx - 45, cy + 10, 9, chip_color);
    grl_draw_circle (cx + 10, cy - 40, 8, chip_color);
    grl_draw_circle (cx - 5, cy + 5, 7, chip_color);

    /* Brain (on top of cookie) */
    if (h_brain)
    {
        gfloat throb = 1.0f + sinf (t * 4.0f) * 0.15f;
        gint brain_y = cy - (gint) r - 25;
        gfloat brain_r = 22.0f * throb;

        grl_draw_circle (cx, brain_y, brain_r, pink);
        /* Brain wrinkles */
        grl_draw_line (cx - 12, brain_y - 5, cx + 12, brain_y + 3, dark_pink);
        grl_draw_line (cx - 8, brain_y + 8, cx + 8, brain_y + 5, dark_pink);
        grl_draw_circle_lines (cx, brain_y, brain_r, dark_pink);
    }
}

static void
draw_stats (ChipClickerGame *self)
{
    g_autofree gchar *cookies_str = NULL;
    g_autofree gchar *cps_str     = NULL;
    g_autoptr (LrgBigNumber) rate = NULL;
    g_autoptr (GrlColor) white    = grl_color_new (255, 255, 255, 255);
    g_autoptr (GrlColor) cream    = grl_color_new (255, 248, 220, 255);
    g_autoptr (GrlColor) yellow   = grl_color_new (255, 215, 0, 255);

    cookies_str = lrg_big_number_format_short (self->cookies);
    rate = lrg_idle_calculator_get_total_rate (self->calculator);
    cps_str = lrg_big_number_format_short (rate);

    /* Title */
    grl_draw_text ("CHOCOLATE CHIP CLICKER", 20, 15, 24, cream);

    /* Cookie count */
    {
        g_autofree gchar *full_str = g_strdup_printf ("Cookies: %s", cookies_str);
        grl_draw_text (full_str, WINDOW_WIDTH - 220, 15, 20, yellow);
    }

    /* Per second */
    {
        g_autofree gchar *full_str = g_strdup_printf ("per sec: %s", cps_str);
        grl_draw_text (full_str, WINDOW_WIDTH - 220, 40, 16, white);
    }

    /* Flavor text */
    {
        g_autofree gchar *flavor = g_strdup_printf ("\"%s\"",
                                                     flavor_texts[self->flavor_index]);
        g_autoptr (GrlColor) flavor_color = grl_color_new (200, 200, 200, 255);
        grl_draw_text (flavor, 20, 45, 14, flavor_color);
    }
}

static void
draw_evolution_panel (ChipClickerGame *self)
{
    guint i;
    gint  panel_x = 420;
    gint  panel_y = 80;
    gint  panel_w = 360;
    gint  row_h   = 70;

    g_autoptr (GrlColor) panel_bg    = grl_color_new (40, 40, 50, 230);
    g_autoptr (GrlColor) row_bg      = grl_color_new (60, 60, 70, 200);
    g_autoptr (GrlColor) white       = grl_color_new (255, 255, 255, 255);
    g_autoptr (GrlColor) gray        = grl_color_new (180, 180, 180, 255);
    g_autoptr (GrlColor) green       = grl_color_new (100, 255, 100, 255);
    g_autoptr (GrlColor) red         = grl_color_new (255, 100, 100, 255);
    g_autoptr (GrlColor) title_color = grl_color_new (255, 220, 150, 255);

    /* Panel background */
    grl_draw_rectangle (panel_x, panel_y, panel_w, row_h * 4 + 40, panel_bg);

    /* Title */
    grl_draw_text ("EVOLUTION", panel_x + 10, panel_y + 5, 20, title_color);

    for (i = 0; i < N_EVOLUTIONS; i++)
    {
        gint row_y = panel_y + 30 + (gint) i * row_h;
        gint count = get_evolution_count (self, evolutions[i].id);
        g_autoptr (LrgBigNumber) cost = calculate_evolution_cost (self, i);
        g_autofree gchar *cost_str    = lrg_big_number_format_short (cost);
        gboolean can_buy = !lrg_big_number_less_than (self->cookies, cost);

        /* Row background */
        grl_draw_rectangle (panel_x + 5, row_y, panel_w - 10, row_h - 5, row_bg);

        /* Name and count */
        {
            g_autofree gchar *name_str = g_strdup_printf ("%s (%d)",
                                                           evolutions[i].name,
                                                           count);
            grl_draw_text (name_str, panel_x + 15, row_y + 8, 16, white);
        }

        /* Description */
        {
            /* Truncate if too long */
            gchar desc[40];
            g_strlcpy (desc, evolutions[i].description, sizeof (desc));
            if (strlen (evolutions[i].description) > 35)
            {
                desc[35] = '.';
                desc[36] = '.';
                desc[37] = '.';
                desc[38] = '\0';
            }
            grl_draw_text (desc, panel_x + 15, row_y + 28, 12, gray);
        }

        /* Cost / Buy button */
        {
            g_autofree gchar *buy_str = g_strdup_printf ("[BUY %s]", cost_str);
            GrlColor *btn_color = can_buy ? green : red;
            grl_draw_text (buy_str, panel_x + panel_w - 120, row_y + 42, 14, btn_color);
        }
    }
}

static void
draw_upgrades_bar (ChipClickerGame *self)
{
    guint i;
    gint  bar_y = WINDOW_HEIGHT - 100;
    gint  bar_h = 45;

    g_autoptr (GrlColor) bar_bg      = grl_color_new (50, 40, 60, 230);
    g_autoptr (GrlColor) white       = grl_color_new (255, 255, 255, 255);
    g_autoptr (GrlColor) green       = grl_color_new (100, 255, 100, 255);
    g_autoptr (GrlColor) red         = grl_color_new (255, 100, 100, 255);
    g_autoptr (GrlColor) gold        = grl_color_new (255, 215, 0, 255);
    g_autoptr (GrlColor) title_color = grl_color_new (200, 180, 255, 255);

    grl_draw_rectangle (0, bar_y, WINDOW_WIDTH, bar_h, bar_bg);
    grl_draw_text ("ENLIGHTENMENT:", 10, bar_y + 5, 14, title_color);

    for (i = 0; i < N_UPGRADES; i++)
    {
        gint          btn_x = 150 + (gint) i * 155;
        LrgUnlockNode *node = lrg_unlock_tree_get_node (self->upgrades, upgrades[i].id);
        gboolean is_unlocked = (node != NULL && lrg_unlock_node_is_unlocked (node));

        if (is_unlocked)
        {
            /* Show as purchased */
            g_autofree gchar *label = g_strdup_printf ("[%s] OK",
                                                        upgrades[i].name);
            grl_draw_text (label, btn_x, bar_y + 15, 12, gold);
        }
        else
        {
            const LrgBigNumber *cost = lrg_unlock_node_get_cost (node);
            g_autofree gchar *cost_str = lrg_big_number_format_short (cost);
            gboolean can_buy = !lrg_big_number_less_than (self->cookies, cost);

            g_autofree gchar *label = g_strdup_printf ("[%s: %s]",
                                                        upgrades[i].name,
                                                        cost_str);
            grl_draw_text (label, btn_x, bar_y + 15, 12, can_buy ? green : red);
        }
    }
}

static void
draw_journey_bar (ChipClickerGame *self)
{
    guint i;
    gint  bar_y = WINDOW_HEIGHT - 50;
    gint  bar_h = 50;

    g_autoptr (GrlColor) bar_bg      = grl_color_new (30, 30, 40, 230);
    g_autoptr (GrlColor) achieved    = grl_color_new (100, 255, 100, 255);
    g_autoptr (GrlColor) pending     = grl_color_new (100, 100, 100, 255);
    g_autoptr (GrlColor) title_color = grl_color_new (180, 200, 180, 255);

    grl_draw_rectangle (0, bar_y, WINDOW_WIDTH, bar_h, bar_bg);
    grl_draw_text ("The Cookie's Journey:", 10, bar_y + 5, 12, title_color);

    for (i = 0; i < self->milestones->len && i < 5; i++)
    {
        LrgMilestone *m = g_ptr_array_index (self->milestones, i);
        const gchar *name = lrg_milestone_get_name (m);
        gboolean done = lrg_milestone_is_achieved (m);

        g_autofree gchar *label = g_strdup_printf ("%s%s",
                                                    name,
                                                    done ? " [OK]" : "");
        grl_draw_text (label, 10 + (gint) i * 155, bar_y + 25, 11,
                       done ? achieved : pending);
    }
}

static void
draw_popup (ChipClickerGame *self)
{
    gint popup_w = 500;
    gint popup_h = 60;
    gint popup_x = (WINDOW_WIDTH - popup_w) / 2;
    gint popup_y = 200;

    g_autoptr (GrlColor) bg    = grl_color_new (20, 20, 30, 240);
    g_autoptr (GrlColor) gold  = grl_color_new (255, 215, 0, 255);
    g_autoptr (GrlColor) white = grl_color_new (255, 255, 255, 255);

    if (self->popup_text == NULL)
        return;

    grl_draw_rectangle (popup_x, popup_y, popup_w, popup_h, bg);
    grl_draw_text ("ACHIEVEMENT UNLOCKED", popup_x + 10, popup_y + 8, 14, gold);
    grl_draw_text (self->popup_text, popup_x + 10, popup_y + 30, 16, white);
}

static void
draw_click_hint (void)
{
    g_autoptr (GrlColor) hint = grl_color_new (150, 150, 150, 180);
    grl_draw_text ("Click the cookie!", COOKIE_X - 60, COOKIE_Y + COOKIE_RADIUS + 55,
                   14, hint);
}

/* =============================================================================
 * INPUT HANDLING
 * ========================================================================== */

static gboolean
point_in_circle (gint px, gint py, gint cx, gint cy, gint r)
{
    gint dx = px - cx;
    gint dy = py - cy;
    return (dx * dx + dy * dy) <= (r * r);
}

static gboolean
point_in_rect (gint px, gint py, gint rx, gint ry, gint rw, gint rh)
{
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

static void
handle_input (ChipClickerGame *self)
{
    gboolean mouse_pressed;
    gint mx, my;
    guint i;

    mouse_pressed = grl_input_is_mouse_button_pressed (GRL_MOUSE_BUTTON_LEFT);
    mx = grl_input_get_mouse_x ();
    my = grl_input_get_mouse_y ();

    if (!mouse_pressed)
        return;

    /* Check cookie click */
    if (point_in_circle (mx, my, COOKIE_X, COOKIE_Y, (gint) (COOKIE_RADIUS * self->cookie_scale)))
    {
        do_click (self);
        return;
    }

    /* Check evolution panel clicks */
    {
        gint panel_x = 420;
        gint panel_y = 80;
        gint panel_w = 360;
        gint row_h   = 70;

        for (i = 0; i < N_EVOLUTIONS; i++)
        {
            gint row_y = panel_y + 30 + (gint) i * row_h;

            if (point_in_rect (mx, my, panel_x + 5, row_y, panel_w - 10, row_h - 5))
            {
                buy_evolution (self, i);
                return;
            }
        }
    }

    /* Check upgrade bar clicks */
    {
        gint bar_y = WINDOW_HEIGHT - 100;
        gint bar_h = 45;

        if (point_in_rect (mx, my, 0, bar_y, WINDOW_WIDTH, bar_h))
        {
            for (i = 0; i < N_UPGRADES; i++)
            {
                gint btn_x = 150 + (gint) i * 155;

                if (point_in_rect (mx, my, btn_x, bar_y, 150, bar_h))
                {
                    buy_upgrade (self, i);
                    return;
                }
            }
        }
    }
}

/* =============================================================================
 * MAIN
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_autoptr (ChipClickerGame) game   = NULL;
    g_autoptr (GrlWindow)       window = NULL;
    g_autoptr (GrlColor) bg_color      = NULL;

    (void) argc;
    (void) argv;

    /* Initialize window */
    window = grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT, "Chocolate Chip Clicker");
    grl_window_set_target_fps (window, 60);

    /* Create game */
    game = chip_clicker_game_new ();

    /* Background color (dark blue-gray) */
    bg_color = grl_color_new (25, 25, 35, 255);

    /* Main loop */
    while (!grl_window_should_close (window))
    {
        gfloat delta = grl_window_get_frame_time (window);

        /* Update */
        handle_input (game);
        update_game (game, delta);

        /* Draw */
        grl_window_begin_drawing (window);
        grl_draw_clear_background (bg_color);

        draw_cookie (game);
        draw_stats (game);
        draw_evolution_panel (game);
        draw_upgrades_bar (game);
        draw_journey_bar (game);
        draw_popup (game);
        draw_click_hint ();

        grl_draw_fps (WINDOW_WIDTH - 80, WINDOW_HEIGHT - 25);

        grl_window_end_drawing (window);
    }

    return 0;
}
