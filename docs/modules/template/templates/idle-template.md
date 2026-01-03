# LrgIdleTemplate

`LrgIdleTemplate` is a game template specialized for idle/clicker games. It provides automatic integration with offline progress calculation, prestige mechanics, big number display, and auto-save.

## Inheritance Hierarchy

```
LrgGameTemplate
└── LrgIdleTemplate (derivable)
```

## Features

- Offline progress calculation with configurable efficiency
- Prestige system with configurable rewards
- Big number formatting (scientific, engineering, letter notation)
- Automatic generator management via `LrgIdleCalculator`
- Frequent auto-save with timestamps for offline tracking
- Maximum offline hours cap
- Welcome-back popup support

## Quick Start

```c
#define MY_TYPE_IDLE_GAME (my_idle_game_get_type ())
G_DECLARE_FINAL_TYPE (MyIdleGame, my_idle_game, MY, IDLE_GAME, LrgIdleTemplate)

struct _MyIdleGame
{
    LrgIdleTemplate parent_instance;
    LrgBigNumber   *currency;
};

G_DEFINE_TYPE (MyIdleGame, my_idle_game, LRG_TYPE_IDLE_TEMPLATE)

static void
my_idle_game_configure (LrgGameTemplate *template)
{
    LrgIdleTemplate *idle = LRG_IDLE_TEMPLATE (template);

    g_object_set (template,
                  "title", "Cookie Clicker Clone",
                  "window-width", 1280,
                  "window-height", 720,
                  NULL);

    /* Configure offline progress */
    lrg_idle_template_set_offline_efficiency (idle, 0.5);  /* 50% of online rate */
    lrg_idle_template_set_max_offline_hours (idle, 24.0);  /* Cap at 24 hours */
    lrg_idle_template_set_show_offline_popup (idle, TRUE);

    /* Configure prestige */
    lrg_idle_template_set_prestige_enabled (idle, TRUE);
}

static LrgIdleCalculator *
my_idle_game_create_idle_calculator (LrgIdleTemplate *template)
{
    LrgIdleCalculator *calc = lrg_idle_calculator_new ();

    /* Add generators */
    lrg_idle_calculator_add_generator (calc, "cursor", 0.1);   /* 0.1 per second */
    lrg_idle_calculator_add_generator (calc, "grandma", 1.0);  /* 1 per second */
    lrg_idle_calculator_add_generator (calc, "farm", 8.0);     /* 8 per second */
    lrg_idle_calculator_add_generator (calc, "mine", 47.0);    /* 47 per second */

    return calc;
}

static void
my_idle_game_class_init (MyIdleGameClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    LrgIdleTemplateClass *idle_class = LRG_IDLE_TEMPLATE_CLASS (klass);

    template_class->configure = my_idle_game_configure;
    idle_class->create_idle_calculator = my_idle_game_create_idle_calculator;
}
```

## Virtual Methods

```c
/* Creates the idle calculator instance */
LrgIdleCalculator * (*create_idle_calculator) (LrgIdleTemplate *self);

/* Creates the optional prestige layer */
LrgPrestige * (*create_prestige) (LrgIdleTemplate *self);

/* Called after offline progress is calculated */
void (*on_offline_progress_calculated) (LrgIdleTemplate    *self,
                                         const LrgBigNumber *progress,
                                         gdouble             seconds_offline);

/* Format a big number for display */
gchar * (*format_big_number) (LrgIdleTemplate    *self,
                              const LrgBigNumber *number);

/* Get offline production efficiency (0.0 to 1.0) */
gdouble (*get_offline_efficiency) (LrgIdleTemplate *self);

/* Get maximum offline hours to calculate */
gdouble (*get_max_offline_hours) (LrgIdleTemplate *self);
```

## Offline Progress

### Configuration

```c
/* Set offline efficiency (50% = half the online production rate) */
lrg_idle_template_set_offline_efficiency (template, 0.5);
gdouble eff = lrg_idle_template_get_offline_efficiency (template);

/* Set maximum offline hours (0 = unlimited) */
lrg_idle_template_set_max_offline_hours (template, 24.0);
gdouble max = lrg_idle_template_get_max_offline_hours (template);

/* Show welcome-back popup */
lrg_idle_template_set_show_offline_popup (template, TRUE);
gboolean show = lrg_idle_template_get_show_offline_popup (template);
```

### Processing Offline Progress

```c
/* Called automatically on game load, or call manually */
LrgBigNumber *progress = lrg_idle_template_process_offline_progress (template);

if (progress != NULL)
{
    /* Player earned something while away */
    gchar *formatted = lrg_idle_template_format_big_number (template, progress);
    show_welcome_back_popup (formatted);
    g_free (formatted);
    lrg_big_number_free (progress);
}
```

### Custom Offline Handler

```c
static void
my_game_on_offline_progress_calculated (LrgIdleTemplate    *template,
                                          const LrgBigNumber *progress,
                                          gdouble             seconds_offline)
{
    MyIdleGame *self = MY_IDLE_GAME (template);

    /* Add progress to player's currency */
    lrg_big_number_add (self->currency, progress);

    /* Show notification */
    gchar *formatted = lrg_idle_template_format_big_number (template, progress);
    gint hours = (gint)(seconds_offline / 3600.0);
    gint minutes = (gint)((seconds_offline - hours * 3600) / 60.0);

    show_notification ("Welcome back! You earned %s in %dh %dm.",
                       formatted, hours, minutes);
    g_free (formatted);
}
```

## Generator Management

### Access Calculator

```c
LrgIdleCalculator *calc = lrg_idle_template_get_idle_calculator (template);
```

### Convenience Methods

```c
/* Add a generator */
lrg_idle_template_add_generator (template, "factory", 260.0);

/* Set/get generator count */
lrg_idle_template_set_generator_count (template, "grandma", 50);
gint64 count = lrg_idle_template_get_generator_count (template, "grandma");

/* Get total production rate */
LrgBigNumber *rate = lrg_idle_template_get_total_production_rate (template);
gchar *rate_str = lrg_idle_template_format_big_number (template, rate);
/* "1.5M per second" */
```

## Prestige System

### Configuration

```c
/* Enable/disable prestige */
lrg_idle_template_set_prestige_enabled (template, TRUE);
gboolean enabled = lrg_idle_template_get_prestige_enabled (template);

/* Access prestige layer */
LrgPrestige *prestige = lrg_idle_template_get_prestige (template);
```

### Performing Prestige

```c
/* Try to prestige with current accumulated value */
LrgBigNumber *reward = lrg_idle_template_try_prestige (template, self->currency);

if (reward != NULL)
{
    /* Prestige successful - reset game state but keep reward */
    self->prestige_currency = lrg_big_number_copy (reward);
    reset_generators ();
    lrg_big_number_free (reward);
}
else
{
    /* Requirements not met */
    show_message ("Need more cookies to prestige!");
}
```

### Custom Prestige Layer

```c
static LrgPrestige *
my_game_create_prestige (LrgIdleTemplate *template)
{
    LrgPrestige *prestige = lrg_prestige_new ();

    /* Require 1 trillion to prestige */
    LrgBigNumber *threshold = lrg_big_number_new_from_string ("1e12");
    lrg_prestige_set_threshold (prestige, threshold);
    lrg_big_number_free (threshold);

    /* Formula: prestige_points = floor(sqrt(currency / threshold)) */
    lrg_prestige_set_formula (prestige, LRG_PRESTIGE_FORMULA_SQRT);

    return prestige;
}
```

## Big Number Formatting

```c
/* Format using template's default settings */
gchar *str = lrg_idle_template_format_big_number (template, number);
/* Examples: "1.23M", "456.7B", "1.00e15" */

/* Custom format in subclass */
static gchar *
my_game_format_big_number (LrgIdleTemplate    *template,
                            const LrgBigNumber *number)
{
    /* Use letter notation: K, M, B, T, Qa, Qi, Sx, Sp, Oc, No, Dc */
    return lrg_big_number_to_letter_notation (number, 2);  /* 2 decimal places */
}
```

## LrgIdleMixin Interface

For composing idle mechanics into non-idle templates:

```c
static void
my_state_apply_offline_progress (LrgIdleMixin       *mixin,
                                   const LrgBigNumber *progress)
{
    MyState *self = MY_STATE (mixin);
    lrg_big_number_add (self->resources, progress);
}

static void
my_state_idle_mixin_init (LrgIdleMixinInterface *iface)
{
    iface->apply_offline_progress = my_state_apply_offline_progress;
}

G_DEFINE_TYPE_WITH_CODE (MyState, my_state, LRG_TYPE_GAME_STATE,
    G_IMPLEMENT_INTERFACE (LRG_TYPE_IDLE_MIXIN, my_state_idle_mixin_init))
```

## Related Documentation

- [LrgGameTemplate](game-template.md) - Base template features
- [Engagement Systems](../systems/engagement.md) - Statistics and daily rewards
- [Idle Game Example](../examples/idle-game.md) - Complete example
