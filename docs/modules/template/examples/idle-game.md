# Idle Game Example

This example demonstrates creating an idle/incremental game using `LrgIdleTemplate`. It includes resource generation, upgrades, offline progress, and prestige mechanics.

## Complete Code

```c
/* idle-game.c - Cookie clicker style idle game */

#include <libregnum.h>

/* ==========================================================================
 * Type Declaration
 * ========================================================================== */

#define MY_TYPE_IDLE_GAME (my_idle_game_get_type ())
G_DECLARE_FINAL_TYPE (MyIdleGame, my_idle_game, MY, IDLE_GAME, LrgIdleTemplate)

/* Upgrade data */
typedef struct {
    const gchar *name;
    const gchar *description;
    LrgBigNumber *base_cost;
    LrgBigNumber *cost_multiplier;
    gint64 owned;
    gdouble production_per;  /* Cookies per second per unit */
} Upgrade;

struct _MyIdleGame
{
    LrgIdleTemplate parent_instance;

    /* Resources */
    LrgBigNumber *cookies;
    LrgBigNumber *cookies_per_second;

    /* Upgrades */
    Upgrade upgrades[6];
    gint selected_upgrade;

    /* Prestige */
    guint prestige_level;
    gdouble prestige_multiplier;

    /* UI */
    gboolean show_prestige_menu;
};

G_DEFINE_TYPE (MyIdleGame, my_idle_game, LRG_TYPE_IDLE_TEMPLATE)

/* ==========================================================================
 * Upgrade Definitions
 * ========================================================================== */

static void
init_upgrades (MyIdleGame *self)
{
    /* Cursor - 0.1 CPS */
    self->upgrades[0].name = "Cursor";
    self->upgrades[0].description = "Auto-clicks once every 10 seconds";
    self->upgrades[0].base_cost = lrg_big_number_new_from_double (15.0);
    self->upgrades[0].cost_multiplier = lrg_big_number_new_from_double (1.15);
    self->upgrades[0].production_per = 0.1;

    /* Grandma - 1 CPS */
    self->upgrades[1].name = "Grandma";
    self->upgrades[1].description = "A nice grandma to bake cookies";
    self->upgrades[1].base_cost = lrg_big_number_new_from_double (100.0);
    self->upgrades[1].cost_multiplier = lrg_big_number_new_from_double (1.15);
    self->upgrades[1].production_per = 1.0;

    /* Farm - 8 CPS */
    self->upgrades[2].name = "Farm";
    self->upgrades[2].description = "Grows cookie plants";
    self->upgrades[2].base_cost = lrg_big_number_new_from_double (1100.0);
    self->upgrades[2].cost_multiplier = lrg_big_number_new_from_double (1.15);
    self->upgrades[2].production_per = 8.0;

    /* Factory - 47 CPS */
    self->upgrades[3].name = "Factory";
    self->upgrades[3].description = "Produces large quantities of cookies";
    self->upgrades[3].base_cost = lrg_big_number_new_from_double (12000.0);
    self->upgrades[3].cost_multiplier = lrg_big_number_new_from_double (1.15);
    self->upgrades[3].production_per = 47.0;

    /* Bank - 260 CPS */
    self->upgrades[4].name = "Bank";
    self->upgrades[4].description = "Generates cookies from interest";
    self->upgrades[4].base_cost = lrg_big_number_new_from_double (130000.0);
    self->upgrades[4].cost_multiplier = lrg_big_number_new_from_double (1.15);
    self->upgrades[4].production_per = 260.0;

    /* Portal - 1400 CPS */
    self->upgrades[5].name = "Portal";
    self->upgrades[5].description = "Opens a portal to the cookieverse";
    self->upgrades[5].base_cost = lrg_big_number_new_from_double (1400000.0);
    self->upgrades[5].cost_multiplier = lrg_big_number_new_from_double (1.15);
    self->upgrades[5].production_per = 1400.0;
}

/* ==========================================================================
 * Game Logic
 * ========================================================================== */

static LrgBigNumber *
get_upgrade_cost (MyIdleGame *self, gint index)
{
    Upgrade *u = &self->upgrades[index];

    /* Cost = base_cost * multiplier^owned */
    g_autoptr(LrgBigNumber) power = lrg_big_number_new_from_int64 (u->owned);
    g_autoptr(LrgBigNumber) mult = lrg_big_number_pow (u->cost_multiplier, power);

    return lrg_big_number_multiply (u->base_cost, mult);
}

static void
recalculate_cps (MyIdleGame *self)
{
    lrg_big_number_set_double (self->cookies_per_second, 0.0);

    for (gint i = 0; i < 6; i++)
    {
        Upgrade *u = &self->upgrades[i];
        if (u->owned > 0)
        {
            gdouble production = u->production_per * u->owned * self->prestige_multiplier;
            lrg_big_number_add_double (self->cookies_per_second, production);
        }
    }
}

static gboolean
try_buy_upgrade (MyIdleGame *self, gint index)
{
    g_autoptr(LrgBigNumber) cost = get_upgrade_cost (self, index);

    if (lrg_big_number_compare (self->cookies, cost) >= 0)
    {
        lrg_big_number_subtract (self->cookies, cost);
        self->upgrades[index].owned++;
        recalculate_cps (self);
        return TRUE;
    }
    return FALSE;
}

static void
do_prestige (MyIdleGame *self)
{
    /* Calculate prestige bonus based on current cookies */
    /* Each order of magnitude = +10% bonus */
    gdouble cookies_log = log10 (lrg_big_number_to_double (self->cookies));
    if (cookies_log > 6)  /* Minimum 1 million cookies */
    {
        self->prestige_level++;
        self->prestige_multiplier = 1.0 + (self->prestige_level * 0.1);

        /* Reset resources */
        lrg_big_number_set_double (self->cookies, 0.0);

        /* Reset upgrades */
        for (gint i = 0; i < 6; i++)
            self->upgrades[i].owned = 0;

        recalculate_cps (self);
    }

    self->show_prestige_menu = FALSE;
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

static void
my_idle_game_configure (LrgGameTemplate *template)
{
    MyIdleGame *self = MY_IDLE_GAME (template);
    LrgIdleTemplate *idle = LRG_IDLE_TEMPLATE (template);

    g_object_set (template,
                  "title", "Cookie Clicker",
                  "window-width", 1024,
                  "window-height", 768,
                  NULL);

    /* Initialize big numbers */
    self->cookies = lrg_big_number_new ();
    self->cookies_per_second = lrg_big_number_new ();

    /* Initialize upgrades */
    init_upgrades (self);

    /* Prestige */
    self->prestige_level = 0;
    self->prestige_multiplier = 1.0;

    /* Configure idle template settings */
    lrg_idle_template_set_auto_save_interval (idle, 30.0);  /* Save every 30 sec */
    lrg_idle_template_set_offline_progress_enabled (idle, TRUE);
    lrg_idle_template_set_offline_efficiency (idle, 0.5);  /* 50% offline */
}

static void
my_idle_game_update (LrgGameTemplate *template, gdouble delta)
{
    MyIdleGame *self = MY_IDLE_GAME (template);

    /* Generate cookies based on CPS */
    g_autoptr(LrgBigNumber) gained = lrg_big_number_multiply_double (
        self->cookies_per_second, delta);
    lrg_big_number_add (self->cookies, gained);

    /* Handle click */
    if (grl_is_mouse_button_pressed (GRL_MOUSE_BUTTON_LEFT))
    {
        gint mx = grl_get_mouse_x ();
        gint my = grl_get_mouse_y ();

        /* Check if clicking the big cookie (centered at 200, 400) */
        if (mx > 50 && mx < 350 && my > 250 && my < 550)
        {
            /* Click value = 1 + CPS * 0.1, with prestige bonus */
            gdouble click_value = 1.0 * self->prestige_multiplier;
            gdouble cps_bonus = lrg_big_number_to_double (self->cookies_per_second) * 0.1;
            lrg_big_number_add_double (self->cookies, click_value + cps_bonus);
        }
    }

    /* Handle keyboard for upgrades */
    for (gint i = 0; i < 6; i++)
    {
        if (grl_is_key_pressed (GRL_KEY_1 + i))
            try_buy_upgrade (self, i);
    }

    /* Toggle prestige menu */
    if (grl_is_key_pressed (GRL_KEY_P))
        self->show_prestige_menu = !self->show_prestige_menu;

    /* Confirm prestige */
    if (self->show_prestige_menu && grl_is_key_pressed (GRL_KEY_Y))
        do_prestige (self);

    /* Escape closes prestige menu or exits */
    if (grl_is_key_pressed (GRL_KEY_ESCAPE))
    {
        if (self->show_prestige_menu)
            self->show_prestige_menu = FALSE;
        else
            lrg_game_template_quit (template);
    }
}

static void
my_idle_game_draw (LrgGameTemplate *template)
{
    MyIdleGame *self = MY_IDLE_GAME (template);

    /* Colors */
    g_autoptr(GrlColor) bg = grl_color_new (245, 235, 220, 255);
    g_autoptr(GrlColor) brown = grl_color_new (139, 90, 43, 255);
    g_autoptr(GrlColor) text = grl_color_new (60, 40, 20, 255);
    g_autoptr(GrlColor) green = grl_color_new (50, 150, 50, 255);
    g_autoptr(GrlColor) gray = grl_color_new (128, 128, 128, 255);

    grl_clear_background (bg);

    /* Draw big cookie */
    grl_draw_circle (200, 400, 120, brown);

    /* Draw cookie count */
    g_autofree gchar *cookie_str = lrg_big_number_to_formatted_string (self->cookies);
    g_autofree gchar *cps_str = lrg_big_number_to_formatted_string (self->cookies_per_second);
    g_autofree gchar *header = g_strdup_printf ("%s cookies", cookie_str);
    g_autofree gchar *subheader = g_strdup_printf ("per second: %s", cps_str);

    grl_draw_text (header, 50, 30, 32, text);
    grl_draw_text (subheader, 50, 70, 20, text);

    /* Draw prestige info */
    if (self->prestige_level > 0)
    {
        g_autofree gchar *prestige_str = g_strdup_printf (
            "Prestige: %u (+%.0f%% bonus)", self->prestige_level,
            (self->prestige_multiplier - 1.0) * 100);
        grl_draw_text (prestige_str, 50, 100, 16, text);
    }

    /* Draw upgrades panel */
    grl_draw_rectangle (450, 50, 520, 650, text);
    grl_draw_rectangle (455, 55, 510, 640, bg);

    grl_draw_text ("Upgrades (press 1-6 to buy)", 470, 70, 20, text);

    for (gint i = 0; i < 6; i++)
    {
        Upgrade *u = &self->upgrades[i];
        gint y = 110 + i * 100;

        g_autoptr(LrgBigNumber) cost = get_upgrade_cost (self, i);
        g_autofree gchar *cost_str = lrg_big_number_to_formatted_string (cost);

        gboolean can_afford = lrg_big_number_compare (self->cookies, cost) >= 0;

        /* Upgrade name and count */
        g_autofree gchar *name_str = g_strdup_printf ("%d. %s (%ld)",
            i + 1, u->name, u->owned);
        grl_draw_text (name_str, 470, y, 18, can_afford ? text : gray);

        /* Description */
        grl_draw_text (u->description, 470, y + 22, 14, gray);

        /* Cost and production */
        g_autofree gchar *cost_line = g_strdup_printf ("Cost: %s", cost_str);
        g_autofree gchar *prod_line = g_strdup_printf ("Produces %.1f/s each", u->production_per);
        grl_draw_text (cost_line, 470, y + 44, 14, can_afford ? green : gray);
        grl_draw_text (prod_line, 470, y + 62, 14, gray);
    }

    /* Instructions */
    grl_draw_text ("Click cookie | 1-6 Buy upgrades | P Prestige | ESC Quit",
                   50, 740, 14, gray);

    /* Prestige menu overlay */
    if (self->show_prestige_menu)
    {
        g_autoptr(GrlColor) overlay = grl_color_new (0, 0, 0, 180);
        g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);

        grl_draw_rectangle (0, 0, 1024, 768, overlay);
        grl_draw_text ("PRESTIGE", 400, 300, 48, white);
        grl_draw_text ("Reset all progress for a permanent bonus?", 280, 380, 20, white);

        g_autofree gchar *bonus_str = g_strdup_printf (
            "Current bonus: +%.0f%% -> +%.0f%%",
            (self->prestige_multiplier - 1.0) * 100,
            self->prestige_level * 10.0 + 10.0);
        grl_draw_text (bonus_str, 350, 420, 18, white);

        grl_draw_text ("Y - Yes, prestige now", 380, 480, 20, green);
        grl_draw_text ("ESC - Cancel", 420, 520, 20, gray);
    }
}

/* Offline progress calculation (from LrgIdleTemplate) */
static void
my_idle_game_on_offline_progress (LrgIdleTemplate *idle,
                                   gdouble          seconds_offline,
                                   gdouble          efficiency)
{
    MyIdleGame *self = MY_IDLE_GAME (idle);

    /* Calculate cookies earned while offline */
    gdouble cps = lrg_big_number_to_double (self->cookies_per_second);
    gdouble earned = cps * seconds_offline * efficiency;

    if (earned > 0)
    {
        lrg_big_number_add_double (self->cookies, earned);

        /* Show offline earnings (could display popup) */
        g_print ("Welcome back! You earned %.2f cookies while away.\n", earned);
    }
}

/* ==========================================================================
 * Class Initialization
 * ========================================================================== */

static void
my_idle_game_class_init (MyIdleGameClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    LrgIdleTemplateClass *idle_class = LRG_IDLE_TEMPLATE_CLASS (klass);

    template_class->configure = my_idle_game_configure;
    template_class->update = my_idle_game_update;
    template_class->draw = my_idle_game_draw;

    idle_class->on_offline_progress = my_idle_game_on_offline_progress;
}

static void
my_idle_game_init (MyIdleGame *self)
{
}

/* ==========================================================================
 * Main Entry Point
 * ========================================================================== */

int
main (int    argc,
      char **argv)
{
    g_autoptr(MyIdleGame) game = g_object_new (MY_TYPE_IDLE_GAME, NULL);
    return lrg_game_template_run (LRG_GAME_TEMPLATE (game), argc, argv);
}
```

## Key Features Demonstrated

### Big Numbers

The `LrgBigNumber` type handles arbitrarily large numbers common in idle games:

```c
self->cookies = lrg_big_number_new ();
lrg_big_number_add_double (self->cookies, 1.5e15);
g_autofree gchar *str = lrg_big_number_to_formatted_string (self->cookies);
/* Output: "1.50 quadrillion" */
```

### Offline Progress

Override `on_offline_progress` to handle time spent away:

```c
static void
my_game_on_offline_progress (LrgIdleTemplate *idle,
                              gdouble          seconds_offline,
                              gdouble          efficiency)
{
    /* efficiency is typically 0.5 (50%) for offline */
    gdouble earned = production_rate * seconds_offline * efficiency;
    add_resources (self, earned);
}
```

### Auto-Save

The template handles auto-saving automatically:

```c
lrg_idle_template_set_auto_save_interval (idle, 30.0);  /* Every 30 seconds */
```

### Prestige System

Reset progress for permanent bonuses:

```c
static void
do_prestige (MyIdleGame *self)
{
    self->prestige_level++;
    self->prestige_multiplier = 1.0 + (self->prestige_level * 0.1);

    /* Reset resources */
    lrg_big_number_set_double (self->cookies, 0.0);

    /* Reset buildings */
    for (gint i = 0; i < 6; i++)
        self->upgrades[i].owned = 0;
}
```

## Related Documentation

- [LrgIdleTemplate](../templates/idle-template.md) - Full idle template reference
- [LrgBigNumber](../../idle/big-number.md) - Arbitrary precision numbers
- [Engagement Systems](../systems/engagement.md) - Statistics and daily rewards
