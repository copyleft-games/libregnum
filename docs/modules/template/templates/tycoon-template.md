# LrgTycoonTemplate

`LrgTycoonTemplate` is a game template specialized for tycoon, management, and city-builder games. It provides time control, economy simulation, data overlays, building placement, and grid-based camera controls.

## Inheritance Hierarchy

```
LrgGameTemplate
└── LrgGame2DTemplate
    └── LrgTycoonTemplate (derivable)
```

## Features

- Time control system (pause, 1x-4x speeds)
- Economy tick simulation
- In-game day/time tracking
- Data visualization overlays
- Building/placement mode
- Grid-based world coordinates
- Camera panning and zooming
- Edge panning support
- Basic resource (money) tracking

## Quick Start

```c
#define MY_TYPE_TYCOON (my_tycoon_get_type ())
G_DECLARE_FINAL_TYPE (MyTycoon, my_tycoon, MY, TYCOON, LrgTycoonTemplate)

struct _MyTycoon
{
    LrgTycoonTemplate parent_instance;
    GPtrArray *buildings;
};

G_DEFINE_TYPE (MyTycoon, my_tycoon, LRG_TYPE_TYCOON_TEMPLATE)

static void
my_tycoon_configure (LrgGameTemplate *template)
{
    LrgTycoonTemplate *tycoon = LRG_TYCOON_TEMPLATE (template);

    g_object_set (template,
                  "title", "My Tycoon Game",
                  "window-width", 1920,
                  "window-height", 1080,
                  NULL);

    /* Time settings */
    lrg_tycoon_template_set_day_length (tycoon, 60.0f);  /* 60 sec = 1 day */
    lrg_tycoon_template_set_tick_interval (tycoon, 1.0f);  /* Economy tick every second */

    /* Grid settings */
    lrg_tycoon_template_set_grid_size (tycoon, 32.0f);

    /* Camera settings */
    lrg_tycoon_template_set_pan_speed (tycoon, 500.0f);
    lrg_tycoon_template_set_zoom_speed (tycoon, 0.1f);
    lrg_tycoon_template_set_zoom_limits (tycoon, 0.5f, 2.0f);
    lrg_tycoon_template_set_edge_pan_margin (tycoon, 20);

    /* Starting money */
    lrg_tycoon_template_set_money (tycoon, 10000);
}

static void
my_tycoon_on_economy_tick (LrgTycoonTemplate *tycoon)
{
    MyTycoon *self = MY_TYCOON (tycoon);

    /* Calculate income from buildings */
    gint64 income = 0;
    for (guint i = 0; i < self->buildings->len; i++)
    {
        Building *b = g_ptr_array_index (self->buildings, i);
        income += building_get_income (b);
    }

    lrg_tycoon_template_add_money (tycoon, income);
}

static void
my_tycoon_class_init (MyTycoonClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    LrgTycoonTemplateClass *tycoon_class = LRG_TYCOON_TEMPLATE_CLASS (klass);

    template_class->configure = my_tycoon_configure;
    tycoon_class->on_economy_tick = my_tycoon_on_economy_tick;
}
```

## Virtual Methods

```c
/* Called when time speed changes */
void (*on_time_speed_changed) (LrgTycoonTemplate *self,
                               LrgTimeSpeed       old_speed,
                               LrgTimeSpeed       new_speed);

/* Called when data overlay changes */
void (*on_overlay_changed) (LrgTycoonTemplate *self,
                            LrgTycoonOverlay   old_overlay,
                            LrgTycoonOverlay   new_overlay);

/* Called on each economy simulation tick */
void (*on_economy_tick) (LrgTycoonTemplate *self);

/* Called when in-game day advances */
void (*on_day_changed) (LrgTycoonTemplate *self,
                        guint              day);

/* Called when entering build/placement mode */
void (*on_build_mode_enter) (LrgTycoonTemplate *self);

/* Called when exiting build/placement mode */
void (*on_build_mode_exit) (LrgTycoonTemplate *self);

/* Updates economy simulation (delta scaled by time speed) */
void (*update_economy) (LrgTycoonTemplate *self,
                        gdouble            delta);

/* Renders the current data overlay */
void (*draw_overlay) (LrgTycoonTemplate *self);

/* Renders the placement grid */
void (*draw_grid) (LrgTycoonTemplate *self);

/* Draws resource counters and status indicators */
void (*draw_resources_hud) (LrgTycoonTemplate *self);
```

## Time Control

```c
typedef enum {
    LRG_TIME_PAUSED,   /* 0x speed */
    LRG_TIME_NORMAL,   /* 1x speed */
    LRG_TIME_FAST,     /* 2x speed */
    LRG_TIME_FASTER,   /* 3x speed */
    LRG_TIME_FASTEST   /* 4x speed */
} LrgTimeSpeed;

/* Get/set time speed */
LrgTimeSpeed speed = lrg_tycoon_template_get_time_speed (template);
lrg_tycoon_template_set_time_speed (template, LRG_TIME_FAST);

/* Toggle pause */
lrg_tycoon_template_toggle_pause (template);
gboolean paused = lrg_tycoon_template_is_paused (template);

/* Get multiplier as float (0.0, 1.0, 2.0, 3.0, 4.0) */
gfloat multiplier = lrg_tycoon_template_get_time_multiplier (template);
```

## In-Game Time

```c
/* Current day (starts at 1) */
guint day = lrg_tycoon_template_get_day (template);
lrg_tycoon_template_set_day (template, 1);

/* Day progress (0.0 = midnight, 1.0 = end of day) */
gfloat progress = lrg_tycoon_template_get_day_progress (template);

/* Day length in real seconds (at 1x speed) */
lrg_tycoon_template_set_day_length (template, 60.0f);
gfloat length = lrg_tycoon_template_get_day_length (template);
```

## Economy Tick

```c
/* Economy tick interval in real seconds (at 1x speed) */
lrg_tycoon_template_set_tick_interval (template, 1.0f);
gfloat interval = lrg_tycoon_template_get_tick_interval (template);
```

## Data Overlays

```c
typedef enum {
    LRG_TYCOON_OVERLAY_NONE,       /* Normal view */
    LRG_TYCOON_OVERLAY_ZONE,       /* Zone/land use */
    LRG_TYCOON_OVERLAY_VALUE,      /* Property/resource value */
    LRG_TYCOON_OVERLAY_TRAFFIC,    /* Traffic/flow */
    LRG_TYCOON_OVERLAY_POWER,      /* Power/utility */
    LRG_TYCOON_OVERLAY_WATER,      /* Water/plumbing */
    LRG_TYCOON_OVERLAY_POLLUTION,  /* Pollution/environment */
    LRG_TYCOON_OVERLAY_HAPPINESS,  /* Happiness/satisfaction */
    LRG_TYCOON_OVERLAY_CUSTOM      /* Game-specific overlay */
} LrgTycoonOverlay;

/* Get/set current overlay */
LrgTycoonOverlay overlay = lrg_tycoon_template_get_overlay (template);
lrg_tycoon_template_set_overlay (template, LRG_TYCOON_OVERLAY_TRAFFIC);
```

## Build Mode

```c
/* Enter/exit build mode */
lrg_tycoon_template_enter_build_mode (template);
lrg_tycoon_template_exit_build_mode (template);
gboolean building = lrg_tycoon_template_is_build_mode (template);

/* Grid visibility (auto-shown in build mode) */
lrg_tycoon_template_set_show_grid (template, TRUE);
gboolean grid_visible = lrg_tycoon_template_get_show_grid (template);
```

## Grid System

```c
/* Grid cell size in world units */
lrg_tycoon_template_set_grid_size (template, 32.0f);
gfloat size = lrg_tycoon_template_get_grid_size (template);

/* Snap world coordinates to grid */
gfloat grid_x, grid_y;
lrg_tycoon_template_snap_to_grid (template, 45.0f, 78.0f, &grid_x, &grid_y);
/* grid_x = 32.0, grid_y = 64.0 (snapped to nearest cell) */

/* Convert world to grid indices */
gint cell_x, cell_y;
lrg_tycoon_template_world_to_grid (template, 100.0f, 200.0f, &cell_x, &cell_y);

/* Convert grid indices to world (cell center) */
gfloat world_x, world_y;
lrg_tycoon_template_grid_to_world (template, 3, 5, &world_x, &world_y);
```

## Camera Controls

```c
/* Pan speed (world units per second) */
lrg_tycoon_template_set_pan_speed (template, 500.0f);
gfloat pan = lrg_tycoon_template_get_pan_speed (template);

/* Zoom speed (per scroll tick) */
lrg_tycoon_template_set_zoom_speed (template, 0.1f);
gfloat zoom_speed = lrg_tycoon_template_get_zoom_speed (template);

/* Zoom limits */
lrg_tycoon_template_set_zoom_limits (template, 0.5f, 2.0f);
gfloat min_zoom = lrg_tycoon_template_get_min_zoom (template);
gfloat max_zoom = lrg_tycoon_template_get_max_zoom (template);

/* Edge panning (pan when mouse near screen edge) */
lrg_tycoon_template_set_edge_pan_margin (template, 20);  /* 20 pixels */
gint margin = lrg_tycoon_template_get_edge_pan_margin (template);
/* Set to 0 to disable edge panning */
```

## Money/Resources

```c
/* Get/set money */
gint64 money = lrg_tycoon_template_get_money (template);
lrg_tycoon_template_set_money (template, 50000);

/* Add/subtract money */
gint64 new_balance = lrg_tycoon_template_add_money (template, 1000);
gint64 after_expense = lrg_tycoon_template_add_money (template, -500);

/* Check if affordable */
gboolean can_buy = lrg_tycoon_template_can_afford (template, 2500);
```

## Overlay Implementation

```c
static void
my_tycoon_draw_overlay (LrgTycoonTemplate *tycoon)
{
    MyTycoon *self = MY_TYCOON (tycoon);
    LrgTycoonOverlay overlay = lrg_tycoon_template_get_overlay (tycoon);

    if (overlay == LRG_TYCOON_OVERLAY_NONE)
        return;

    gfloat grid_size = lrg_tycoon_template_get_grid_size (tycoon);

    for (gint y = 0; y < self->map_height; y++)
    {
        for (gint x = 0; x < self->map_width; x++)
        {
            gfloat world_x, world_y;
            lrg_tycoon_template_grid_to_world (tycoon, x, y, &world_x, &world_y);

            g_autoptr(GrlColor) color = NULL;

            switch (overlay)
            {
                case LRG_TYCOON_OVERLAY_VALUE:
                    {
                        gfloat value = get_land_value (self, x, y);
                        guint8 intensity = (guint8)(value * 255);
                        color = grl_color_new (0, intensity, 0, 128);
                    }
                    break;

                case LRG_TYCOON_OVERLAY_POLLUTION:
                    {
                        gfloat pollution = get_pollution (self, x, y);
                        guint8 intensity = (guint8)(pollution * 255);
                        color = grl_color_new (intensity, 0, 0, 128);
                    }
                    break;

                default:
                    continue;
            }

            grl_draw_rectangle (world_x - grid_size / 2,
                                world_y - grid_size / 2,
                                grid_size, grid_size, color);
        }
    }
}
```

## Day Change Handling

```c
static void
my_tycoon_on_day_changed (LrgTycoonTemplate *tycoon, guint day)
{
    MyTycoon *self = MY_TYCOON (tycoon);

    /* Daily expenses */
    gint64 expenses = calculate_daily_expenses (self);
    lrg_tycoon_template_add_money (tycoon, -expenses);

    /* Daily events */
    if (day % 7 == 0)
    {
        /* Weekly report */
        show_weekly_report (self);
    }

    if (day % 30 == 0)
    {
        /* Monthly taxes */
        gint64 tax = calculate_monthly_tax (self);
        lrg_tycoon_template_add_money (tycoon, -tax);
    }

    /* Update satisfaction, population, etc. */
    update_city_stats (self);
}
```

## Related Documentation

- [LrgGame2DTemplate](game-2d-template.md) - 2D template features
- [LrgGameTemplate](game-template.md) - Base template features
- [LrgIdleTemplate](idle-template.md) - Idle game mechanics
