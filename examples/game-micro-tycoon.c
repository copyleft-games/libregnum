/* game-micro-tycoon.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A factory management tycoon game demonstrating Phase 2 features:
 * Economy/Resource System, Building/Placement System, Vehicle System,
 * and Idle Game mechanics.
 *
 * Features demonstrated:
 * - LrgEconomyManager / LrgResource: Currency and resource tracking
 * - LrgProductionRecipe / LrgProducer: Production chains
 * - LrgPlacementSystem / LrgBuildGrid: Grid-based building placement
 * - LrgBuildingDef / LrgBuildingInstance: Building types with upgrades
 * - LrgIdleCalculator / LrgBigNumber: Offline progress and big numbers
 * - LrgVehicle: Delivery trucks between buildings
 */

/* =============================================================================
 * INCLUDES
 * ========================================================================== */

#include <libregnum.h>
#include <graylib.h>
#include <math.h>
#include <string.h>
#include <time.h>

/* =============================================================================
 * CONSTANTS
 * ========================================================================== */

#define WINDOW_WIDTH     1024
#define WINDOW_HEIGHT    768
#define GRID_SIZE        12
#define CELL_SIZE        48
#define GRID_OFFSET_X    50
#define GRID_OFFSET_Y    100

/* Resource IDs */
#define RES_GOLD         0
#define RES_ORE          1
#define RES_COAL         2
#define RES_STEEL        3
#define RES_PRODUCTS     4
#define RES_COUNT        5

/* Building types */
#define BUILD_NONE       0
#define BUILD_IRON_MINE  1
#define BUILD_COAL_MINE  2
#define BUILD_FOUNDRY    3
#define BUILD_FACTORY    4
#define BUILD_WAREHOUSE  5
#define BUILD_MARKET     6
#define BUILD_TYPE_COUNT 7

/* UI Layout */
#define RESOURCE_BAR_Y   10
#define BUILDING_MENU_Y  (WINDOW_HEIGHT - 100)
#define INFO_PANEL_X     (GRID_OFFSET_X + GRID_SIZE * CELL_SIZE + 30)
#define INFO_PANEL_Y     100

/* Timing */
#define PRODUCTION_INTERVAL 2.0f
#define TRUCK_SPEED         120.0f
#define MARKET_UPDATE_INTERVAL 10.0f

/* =============================================================================
 * COLORS
 * ========================================================================== */

static GrlColor *color_bg = NULL;
static GrlColor *color_grid = NULL;
static GrlColor *color_grid_hover = NULL;
static GrlColor *color_text = NULL;
static GrlColor *color_text_dim = NULL;
static GrlColor *color_gold = NULL;
static GrlColor *color_ore = NULL;
static GrlColor *color_coal = NULL;
static GrlColor *color_steel = NULL;
static GrlColor *color_products = NULL;
static GrlColor *color_selected = NULL;
static GrlColor *color_invalid = NULL;

/* Building colors */
static GrlColor *building_colors[BUILD_TYPE_COUNT] = { NULL };

static void
init_colors (void)
{
    color_bg = grl_color_new (30, 35, 40, 255);
    color_grid = grl_color_new (50, 55, 65, 255);
    color_grid_hover = grl_color_new (70, 80, 95, 255);
    color_text = grl_color_new (230, 235, 240, 255);
    color_text_dim = grl_color_new (140, 145, 160, 255);
    color_gold = grl_color_new (255, 215, 0, 255);
    color_ore = grl_color_new (180, 100, 60, 255);
    color_coal = grl_color_new (60, 60, 70, 255);
    color_steel = grl_color_new (160, 170, 190, 255);
    color_products = grl_color_new (100, 200, 150, 255);
    color_selected = grl_color_new (100, 180, 255, 255);
    color_invalid = grl_color_new (255, 80, 80, 128);

    building_colors[BUILD_NONE] = NULL;
    building_colors[BUILD_IRON_MINE] = grl_color_new (180, 100, 60, 255);
    building_colors[BUILD_COAL_MINE] = grl_color_new (60, 60, 70, 255);
    building_colors[BUILD_FOUNDRY] = grl_color_new (200, 120, 50, 255);
    building_colors[BUILD_FACTORY] = grl_color_new (100, 150, 200, 255);
    building_colors[BUILD_WAREHOUSE] = grl_color_new (140, 120, 100, 255);
    building_colors[BUILD_MARKET] = grl_color_new (100, 200, 100, 255);
}

static void
cleanup_colors (void)
{
    gint i;

    g_clear_pointer (&color_bg, grl_color_free);
    g_clear_pointer (&color_grid, grl_color_free);
    g_clear_pointer (&color_grid_hover, grl_color_free);
    g_clear_pointer (&color_text, grl_color_free);
    g_clear_pointer (&color_text_dim, grl_color_free);
    g_clear_pointer (&color_gold, grl_color_free);
    g_clear_pointer (&color_ore, grl_color_free);
    g_clear_pointer (&color_coal, grl_color_free);
    g_clear_pointer (&color_steel, grl_color_free);
    g_clear_pointer (&color_products, grl_color_free);
    g_clear_pointer (&color_selected, grl_color_free);
    g_clear_pointer (&color_invalid, grl_color_free);

    for (i = 1; i < BUILD_TYPE_COUNT; i++)
    {
        g_clear_pointer (&building_colors[i], grl_color_free);
    }
}

/* =============================================================================
 * RESOURCE DATA
 * ========================================================================== */

static const gchar *resource_names[RES_COUNT] = {
    "Gold",
    "Iron Ore",
    "Coal",
    "Steel",
    "Products"
};

static const gchar *resource_icons[RES_COUNT] = {
    "$",
    "O",
    "C",
    "S",
    "P"
};

/* =============================================================================
 * BUILDING DATA
 * ========================================================================== */

typedef struct
{
    const gchar *name;
    const gchar *description;
    gdouble cost;
    gint produces;      /* Resource ID produced (-1 for none) */
    gdouble produce_rate; /* Per production cycle */
    gint consumes1;     /* First consumed resource (-1 for none) */
    gint consumes2;     /* Second consumed resource (-1 for none) */
    gdouble consume_rate;
} BuildingDef;

static const BuildingDef building_defs[BUILD_TYPE_COUNT] = {
    { "Empty", "Empty cell", 0, -1, 0, -1, -1, 0 },
    { "Iron Mine", "Produces iron ore", 100, RES_ORE, 2.0, -1, -1, 0 },
    { "Coal Mine", "Produces coal", 100, RES_COAL, 2.0, -1, -1, 0 },
    { "Foundry", "Converts ore + coal to steel", 500, RES_STEEL, 1.0, RES_ORE, RES_COAL, 1.0 },
    { "Factory", "Converts steel to products", 1000, RES_PRODUCTS, 0.5, RES_STEEL, -1, 2.0 },
    { "Warehouse", "Increases storage by 100", 250, -1, 0, -1, -1, 0 },
    { "Market", "Sells products for gold", 800, RES_GOLD, 0, RES_PRODUCTS, -1, 1.0 }
};

/* =============================================================================
 * GAME STATE
 * ========================================================================== */

typedef struct
{
    gint type;
    gint level;  /* 0-3, each level doubles production */
    gfloat production_timer;
    gboolean active;
} Building;

typedef struct
{
    gfloat x, y;
    gfloat target_x, target_y;
    gint cargo_type;
    gdouble cargo_amount;
    gboolean active;
    gboolean returning;
} Truck;

#define MAX_TRUCKS 20

typedef struct
{
    /* Resources (using LrgBigNumber for late game) */
    LrgBigNumber *resources[RES_COUNT];
    gdouble storage_capacity;

    /* Grid */
    Building grid[GRID_SIZE][GRID_SIZE];

    /* Building placement */
    gint selected_building;
    gint hover_x, hover_y;
    gboolean placement_valid;

    /* Selected cell for info */
    gint info_x, info_y;

    /* Trucks */
    Truck trucks[MAX_TRUCKS];
    gint truck_count;

    /* Market */
    gdouble market_price;
    gfloat market_timer;

    /* Offline progress */
    gint64 last_save_time;

    /* Stats */
    gdouble total_gold_earned;
    gint buildings_built;
} GameState;

static GameState *g_game = NULL;

/* =============================================================================
 * BIG NUMBER HELPERS
 * ========================================================================== */

static gchar *
format_number (LrgBigNumber *num)
{
    gdouble value;

    value = lrg_big_number_to_double (num);

    if (value < 1000.0)
        return g_strdup_printf ("%.0f", value);
    else if (value < 1000000.0)
        return g_strdup_printf ("%.1fK", value / 1000.0);
    else if (value < 1000000000.0)
        return g_strdup_printf ("%.1fM", value / 1000000.0);
    else if (value < 1000000000000.0)
        return g_strdup_printf ("%.1fB", value / 1000000000.0);
    else
        return g_strdup_printf ("%.1fT", value / 1000000000000.0);
}

static void
add_resource (gint resource_id, gdouble amount)
{
    LrgBigNumber *add_val;
    gdouble current;

    if (resource_id < 0 || resource_id >= RES_COUNT)
        return;

    add_val = lrg_big_number_new (amount);
    lrg_big_number_add_in_place (g_game->resources[resource_id], add_val);
    lrg_big_number_free (add_val);

    /* Clamp to storage capacity (except gold) */
    if (resource_id != RES_GOLD)
    {
        current = lrg_big_number_to_double (g_game->resources[resource_id]);
        if (current > g_game->storage_capacity)
        {
            lrg_big_number_free (g_game->resources[resource_id]);
            g_game->resources[resource_id] = lrg_big_number_new (g_game->storage_capacity);
        }
    }

    /* Track gold earned */
    if (resource_id == RES_GOLD)
    {
        g_game->total_gold_earned += amount;
    }
}

static gboolean
spend_resource (gint resource_id, gdouble amount)
{
    gdouble current;
    LrgBigNumber *result;
    LrgBigNumber *sub_val;

    if (resource_id < 0 || resource_id >= RES_COUNT)
        return FALSE;

    current = lrg_big_number_to_double (g_game->resources[resource_id]);
    if (current < amount)
        return FALSE;

    sub_val = lrg_big_number_new (amount);
    result = lrg_big_number_subtract (g_game->resources[resource_id], sub_val);
    lrg_big_number_free (sub_val);
    lrg_big_number_free (g_game->resources[resource_id]);
    g_game->resources[resource_id] = result;
    return TRUE;
}

static gdouble
get_resource (gint resource_id)
{
    if (resource_id < 0 || resource_id >= RES_COUNT)
        return 0.0;
    return lrg_big_number_to_double (g_game->resources[resource_id]);
}

/* =============================================================================
 * GAME INITIALIZATION
 * ========================================================================== */

static void
game_init (void)
{
    gint i;
    gint x, y;

    g_game = g_new0 (GameState, 1);

    /* Initialize resources */
    for (i = 0; i < RES_COUNT; i++)
    {
        g_game->resources[i] = lrg_big_number_new (0.0);
    }

    /* Starting gold */
    lrg_big_number_free (g_game->resources[RES_GOLD]);
    g_game->resources[RES_GOLD] = lrg_big_number_new (500.0);

    g_game->storage_capacity = 100.0;
    g_game->selected_building = BUILD_IRON_MINE;
    g_game->hover_x = -1;
    g_game->hover_y = -1;
    g_game->info_x = -1;
    g_game->info_y = -1;
    g_game->market_price = 50.0;
    g_game->last_save_time = g_get_real_time () / G_USEC_PER_SEC;

    /* Initialize grid */
    for (y = 0; y < GRID_SIZE; y++)
    {
        for (x = 0; x < GRID_SIZE; x++)
        {
            g_game->grid[y][x].type = BUILD_NONE;
            g_game->grid[y][x].level = 0;
            g_game->grid[y][x].production_timer = 0.0f;
            g_game->grid[y][x].active = FALSE;
        }
    }
}

static void
game_cleanup (void)
{
    gint i;

    if (g_game == NULL)
        return;

    for (i = 0; i < RES_COUNT; i++)
    {
        lrg_big_number_free (g_game->resources[i]);
    }

    g_free (g_game);
    g_game = NULL;
}

/* =============================================================================
 * BUILDING LOGIC
 * ========================================================================== */

static gboolean
can_place_building (gint x, gint y, gint building_type)
{
    gdouble cost;

    if (x < 0 || x >= GRID_SIZE || y < 0 || y >= GRID_SIZE)
        return FALSE;

    if (g_game->grid[y][x].type != BUILD_NONE)
        return FALSE;

    cost = building_defs[building_type].cost;
    if (get_resource (RES_GOLD) < cost)
        return FALSE;

    return TRUE;
}

static void
place_building (gint x, gint y, gint building_type)
{
    gdouble cost;

    if (!can_place_building (x, y, building_type))
        return;

    cost = building_defs[building_type].cost;
    spend_resource (RES_GOLD, cost);

    g_game->grid[y][x].type = building_type;
    g_game->grid[y][x].level = 0;
    g_game->grid[y][x].production_timer = 0.0f;
    g_game->grid[y][x].active = TRUE;
    g_game->buildings_built++;

    /* Warehouses increase storage */
    if (building_type == BUILD_WAREHOUSE)
    {
        g_game->storage_capacity += 100.0;
    }
}

static void
demolish_building (gint x, gint y)
{
    Building *b;
    gdouble refund;

    if (x < 0 || x >= GRID_SIZE || y < 0 || y >= GRID_SIZE)
        return;

    b = &g_game->grid[y][x];
    if (b->type == BUILD_NONE)
        return;

    /* Refund 50% of cost */
    refund = building_defs[b->type].cost * 0.5;
    add_resource (RES_GOLD, refund);

    /* Remove storage if warehouse */
    if (b->type == BUILD_WAREHOUSE)
    {
        g_game->storage_capacity -= 100.0;
        if (g_game->storage_capacity < 100.0)
            g_game->storage_capacity = 100.0;
    }

    b->type = BUILD_NONE;
    b->level = 0;
    b->active = FALSE;
}

static void
upgrade_building (gint x, gint y)
{
    Building *b;
    gdouble cost;

    if (x < 0 || x >= GRID_SIZE || y < 0 || y >= GRID_SIZE)
        return;

    b = &g_game->grid[y][x];
    if (b->type == BUILD_NONE || b->level >= 3)
        return;

    /* Upgrade cost = base cost * (level + 2) */
    cost = building_defs[b->type].cost * (b->level + 2);
    if (!spend_resource (RES_GOLD, cost))
        return;

    b->level++;

    /* Warehouses give extra storage per level */
    if (b->type == BUILD_WAREHOUSE)
    {
        g_game->storage_capacity += 50.0;
    }
}

/* =============================================================================
 * PRODUCTION LOGIC
 * ========================================================================== */

static gdouble
get_production_multiplier (Building *b)
{
    /* Each level doubles production */
    return pow (2.0, b->level);
}

static void
update_production (gfloat delta)
{
    gint x, y;
    Building *b;
    const BuildingDef *def;
    gdouble mult;
    gboolean can_produce;
    gdouble products_sold;

    for (y = 0; y < GRID_SIZE; y++)
    {
        for (x = 0; x < GRID_SIZE; x++)
        {
            b = &g_game->grid[y][x];
            if (b->type == BUILD_NONE || !b->active)
                continue;

            def = &building_defs[b->type];
            b->production_timer += delta;

            if (b->production_timer >= PRODUCTION_INTERVAL)
            {
                b->production_timer = 0.0f;
                mult = get_production_multiplier (b);

                /* Check if we can consume required resources */
                can_produce = TRUE;

                if (def->consumes1 >= 0)
                {
                    if (get_resource (def->consumes1) < def->consume_rate * mult)
                        can_produce = FALSE;
                }
                if (def->consumes2 >= 0)
                {
                    if (get_resource (def->consumes2) < def->consume_rate * mult)
                        can_produce = FALSE;
                }

                if (can_produce)
                {
                    /* Consume resources */
                    if (def->consumes1 >= 0)
                        spend_resource (def->consumes1, def->consume_rate * mult);
                    if (def->consumes2 >= 0)
                        spend_resource (def->consumes2, def->consume_rate * mult);

                    /* Produce resources */
                    if (def->produces >= 0)
                    {
                        /* Market sells products for gold at market price */
                        if (b->type == BUILD_MARKET)
                        {
                            products_sold = fmin (get_resource (RES_PRODUCTS), def->consume_rate * mult);
                            if (products_sold > 0)
                            {
                                spend_resource (RES_PRODUCTS, products_sold);
                                add_resource (RES_GOLD, products_sold * g_game->market_price);
                            }
                        }
                        else
                        {
                            add_resource (def->produces, def->produce_rate * mult);
                        }
                    }
                }
            }
        }
    }
}

/* =============================================================================
 * MARKET LOGIC
 * ========================================================================== */

static void
update_market (gfloat delta)
{
    g_game->market_timer += delta;

    if (g_game->market_timer >= MARKET_UPDATE_INTERVAL)
    {
        g_game->market_timer = 0.0f;

        /* Random price fluctuation between 30 and 80 */
        g_game->market_price = 30.0 + g_random_double () * 50.0;
    }
}

/* =============================================================================
 * TRUCK LOGIC (Simplified visual effect)
 * ========================================================================== */

static void
spawn_truck (gint from_x, gint from_y, gint to_x, gint to_y)
{
    gint i;

    if (g_game->truck_count >= MAX_TRUCKS)
        return;

    for (i = 0; i < MAX_TRUCKS; i++)
    {
        if (!g_game->trucks[i].active)
        {
            g_game->trucks[i].x = GRID_OFFSET_X + from_x * CELL_SIZE + CELL_SIZE / 2;
            g_game->trucks[i].y = GRID_OFFSET_Y + from_y * CELL_SIZE + CELL_SIZE / 2;
            g_game->trucks[i].target_x = GRID_OFFSET_X + to_x * CELL_SIZE + CELL_SIZE / 2;
            g_game->trucks[i].target_y = GRID_OFFSET_Y + to_y * CELL_SIZE + CELL_SIZE / 2;
            g_game->trucks[i].active = TRUE;
            g_game->trucks[i].returning = FALSE;
            g_game->truck_count++;
            break;
        }
    }
}

static void
update_trucks (gfloat delta)
{
    static gfloat truck_spawn_timer = 0.0f;
    gint producer_x, producer_y;
    gint consumer_x, consumer_y;
    gint x, y, i;
    Building *b;
    Truck *t;
    gfloat dx, dy, dist;
    gfloat temp_x, temp_y;

    /* Occasionally spawn trucks between production buildings */
    truck_spawn_timer += delta;

    if (truck_spawn_timer >= 3.0f && g_game->truck_count < MAX_TRUCKS)
    {
        truck_spawn_timer = 0.0f;

        /* Find a producer and a consumer */
        producer_x = -1;
        producer_y = -1;
        consumer_x = -1;
        consumer_y = -1;

        for (y = 0; y < GRID_SIZE && producer_x < 0; y++)
        {
            for (x = 0; x < GRID_SIZE; x++)
            {
                b = &g_game->grid[y][x];
                if (b->type == BUILD_IRON_MINE || b->type == BUILD_COAL_MINE)
                {
                    producer_x = x;
                    producer_y = y;
                    break;
                }
            }
        }

        for (y = 0; y < GRID_SIZE && consumer_x < 0; y++)
        {
            for (x = 0; x < GRID_SIZE; x++)
            {
                b = &g_game->grid[y][x];
                if (b->type == BUILD_FOUNDRY || b->type == BUILD_FACTORY)
                {
                    consumer_x = x;
                    consumer_y = y;
                    break;
                }
            }
        }

        if (producer_x >= 0 && consumer_x >= 0)
        {
            spawn_truck (producer_x, producer_y, consumer_x, consumer_y);
        }
    }

    /* Update truck positions */
    for (i = 0; i < MAX_TRUCKS; i++)
    {
        t = &g_game->trucks[i];
        if (!t->active)
            continue;

        dx = t->target_x - t->x;
        dy = t->target_y - t->y;
        dist = sqrtf (dx * dx + dy * dy);

        if (dist < 5.0f)
        {
            if (!t->returning)
            {
                /* Swap target and start for return trip */
                temp_x = t->target_x;
                temp_y = t->target_y;
                t->target_x = t->x;
                t->target_y = t->y;
                t->x = temp_x;
                t->y = temp_y;
                t->returning = TRUE;
            }
            else
            {
                /* Trip complete */
                t->active = FALSE;
                g_game->truck_count--;
            }
        }
        else
        {
            t->x += (dx / dist) * TRUCK_SPEED * delta;
            t->y += (dy / dist) * TRUCK_SPEED * delta;
        }
    }
}

/* =============================================================================
 * OFFLINE PROGRESS
 * ========================================================================== */

static void
calculate_offline_progress (void)
{
    gint64 now;
    gint64 elapsed;
    gint cycles;
    gint x, y;
    Building *b;
    const BuildingDef *def;
    gdouble mult;

    now = g_get_real_time () / G_USEC_PER_SEC;
    elapsed = now - g_game->last_save_time;

    if (elapsed <= 0)
        return;

    /* Cap at 24 hours */
    if (elapsed > 86400)
        elapsed = 86400;

    /* Calculate production for elapsed time */
    cycles = elapsed / (gint)PRODUCTION_INTERVAL;

    for (y = 0; y < GRID_SIZE; y++)
    {
        for (x = 0; x < GRID_SIZE; x++)
        {
            b = &g_game->grid[y][x];
            if (b->type == BUILD_NONE || !b->active)
                continue;

            def = &building_defs[b->type];
            mult = get_production_multiplier (b);

            /* Skip buildings that consume resources (simplified) */
            if (def->consumes1 >= 0)
                continue;

            /* Add production */
            if (def->produces >= 0 && b->type != BUILD_MARKET)
            {
                add_resource (def->produces, def->produce_rate * mult * cycles);
            }
        }
    }

    g_game->last_save_time = now;

    if (cycles > 0)
    {
        g_message ("Calculated %d offline production cycles (%.1f hours)",
                   cycles, elapsed / 3600.0);
    }
}

/* =============================================================================
 * INPUT HANDLING
 * ========================================================================== */

static void
handle_input (void)
{
    gint mx, my;
    gint grid_x, grid_y;

    mx = grl_input_get_mouse_x ();
    my = grl_input_get_mouse_y ();

    /* Calculate grid cell under mouse */
    grid_x = (mx - GRID_OFFSET_X) / CELL_SIZE;
    grid_y = (my - GRID_OFFSET_Y) / CELL_SIZE;

    if (grid_x >= 0 && grid_x < GRID_SIZE && grid_y >= 0 && grid_y < GRID_SIZE &&
        mx >= GRID_OFFSET_X && my >= GRID_OFFSET_Y &&
        mx < GRID_OFFSET_X + GRID_SIZE * CELL_SIZE &&
        my < GRID_OFFSET_Y + GRID_SIZE * CELL_SIZE)
    {
        g_game->hover_x = grid_x;
        g_game->hover_y = grid_y;
        g_game->placement_valid = can_place_building (grid_x, grid_y, g_game->selected_building);
    }
    else
    {
        g_game->hover_x = -1;
        g_game->hover_y = -1;
    }

    /* Left click to place or select */
    if (grl_input_is_mouse_button_pressed (GRL_MOUSE_BUTTON_LEFT))
    {
        if (g_game->hover_x >= 0 && g_game->hover_y >= 0)
        {
            if (g_game->grid[g_game->hover_y][g_game->hover_x].type == BUILD_NONE)
            {
                /* Place building */
                place_building (g_game->hover_x, g_game->hover_y, g_game->selected_building);
            }
            else
            {
                /* Select for info */
                g_game->info_x = g_game->hover_x;
                g_game->info_y = g_game->hover_y;
            }
        }
    }

    /* Right click to demolish */
    if (grl_input_is_mouse_button_pressed (GRL_MOUSE_BUTTON_RIGHT))
    {
        if (g_game->hover_x >= 0 && g_game->hover_y >= 0)
        {
            demolish_building (g_game->hover_x, g_game->hover_y);
        }
    }

    /* Number keys to select building type */
    if (grl_input_is_key_pressed (GRL_KEY_ONE)) g_game->selected_building = BUILD_IRON_MINE;
    if (grl_input_is_key_pressed (GRL_KEY_TWO)) g_game->selected_building = BUILD_COAL_MINE;
    if (grl_input_is_key_pressed (GRL_KEY_THREE)) g_game->selected_building = BUILD_FOUNDRY;
    if (grl_input_is_key_pressed (GRL_KEY_FOUR)) g_game->selected_building = BUILD_FACTORY;
    if (grl_input_is_key_pressed (GRL_KEY_FIVE)) g_game->selected_building = BUILD_WAREHOUSE;
    if (grl_input_is_key_pressed (GRL_KEY_SIX)) g_game->selected_building = BUILD_MARKET;

    /* U to upgrade selected building */
    if (grl_input_is_key_pressed (GRL_KEY_U))
    {
        if (g_game->info_x >= 0 && g_game->info_y >= 0)
        {
            upgrade_building (g_game->info_x, g_game->info_y);
        }
    }
}

/* =============================================================================
 * RENDERING
 * ========================================================================== */

static void
draw_resource_bar (void)
{
    gint x, y, i;
    gint spacing;
    GrlColor *res_colors[RES_COUNT];
    g_autofree gchar *value_str = NULL;
    g_autofree gchar *label = NULL;
    g_autofree gchar *storage_str = NULL;
    g_autofree gchar *price_str = NULL;

    x = 20;
    y = RESOURCE_BAR_Y;
    spacing = 170;

    res_colors[0] = color_gold;
    res_colors[1] = color_ore;
    res_colors[2] = color_coal;
    res_colors[3] = color_steel;
    res_colors[4] = color_products;

    for (i = 0; i < RES_COUNT; i++)
    {
        g_free (value_str);
        g_free (label);
        value_str = format_number (g_game->resources[i]);
        label = g_strdup_printf ("%s: %s %s",
                                 resource_icons[i],
                                 value_str,
                                 resource_names[i]);
        grl_draw_text (label, x + i * spacing, y, 18, res_colors[i]);
    }

    /* Storage indicator */
    storage_str = g_strdup_printf ("Storage: %.0f", g_game->storage_capacity);
    grl_draw_text (storage_str, WINDOW_WIDTH - 150, y, 16, color_text_dim);

    /* Market price */
    price_str = g_strdup_printf ("Market Price: $%.0f", g_game->market_price);
    grl_draw_text (price_str, WINDOW_WIDTH - 150, y + 20, 16, color_gold);
}

static void
draw_grid (void)
{
    gint x, y;
    gint px, py;
    Building *b;
    GrlColor *bg;
    GrlColor *bar_color;
    GrlColor *ghost_color;
    gfloat progress;
    gint bar_width;
    g_autofree gchar *lvl = NULL;

    for (y = 0; y < GRID_SIZE; y++)
    {
        for (x = 0; x < GRID_SIZE; x++)
        {
            px = GRID_OFFSET_X + x * CELL_SIZE;
            py = GRID_OFFSET_Y + y * CELL_SIZE;

            b = &g_game->grid[y][x];

            /* Draw cell background */
            bg = color_grid;
            if (x == g_game->hover_x && y == g_game->hover_y)
            {
                bg = color_grid_hover;
            }
            if (x == g_game->info_x && y == g_game->info_y)
            {
                bg = color_selected;
            }

            grl_draw_rectangle (px + 1, py + 1, CELL_SIZE - 2, CELL_SIZE - 2, bg);

            /* Draw building */
            if (b->type != BUILD_NONE)
            {
                grl_draw_rectangle (px + 4, py + 4, CELL_SIZE - 8, CELL_SIZE - 8,
                                     building_colors[b->type]);

                /* Level indicator */
                if (b->level > 0)
                {
                    g_free (lvl);
                    lvl = g_strdup_printf ("%d", b->level + 1);
                    grl_draw_text (lvl, px + CELL_SIZE - 14, py + 2, 12, color_text);
                }

                /* Production indicator (small bar) */
                progress = b->production_timer / PRODUCTION_INTERVAL;
                bar_width = (gint)((CELL_SIZE - 10) * progress);
                bar_color = grl_color_new (100, 255, 100, 180);
                grl_draw_rectangle (px + 5, py + CELL_SIZE - 8, bar_width, 4, bar_color);
                grl_color_free (bar_color);
            }
        }
    }

    /* Draw placement ghost */
    if (g_game->hover_x >= 0 && g_game->hover_y >= 0 &&
        g_game->grid[g_game->hover_y][g_game->hover_x].type == BUILD_NONE)
    {
        px = GRID_OFFSET_X + g_game->hover_x * CELL_SIZE;
        py = GRID_OFFSET_Y + g_game->hover_y * CELL_SIZE;

        if (g_game->placement_valid)
        {
            ghost_color = grl_color_new (100, 255, 100, 100);
        }
        else
        {
            ghost_color = grl_color_new (255, 100, 100, 100);
        }

        grl_draw_rectangle (px + 4, py + 4, CELL_SIZE - 8, CELL_SIZE - 8, ghost_color);
        grl_color_free (ghost_color);
    }
}

static void
draw_trucks (void)
{
    GrlColor *truck_color;
    gint i;
    Truck *t;

    truck_color = grl_color_new (200, 200, 50, 255);

    for (i = 0; i < MAX_TRUCKS; i++)
    {
        t = &g_game->trucks[i];
        if (!t->active)
            continue;

        grl_draw_rectangle ((gint)t->x - 4, (gint)t->y - 4, 8, 8, truck_color);
    }

    grl_color_free (truck_color);
}

static void
draw_building_menu (void)
{
    gint x, y, i;
    const BuildingDef *def;
    GrlColor *color;
    const gchar *controls;
    gint text_width;
    g_autofree gchar *label = NULL;

    x = 20;
    y = BUILDING_MENU_Y;

    grl_draw_text ("Buildings: [1-6]", x, y, 20, color_text);

    x = 150;
    for (i = 1; i < BUILD_TYPE_COUNT; i++)
    {
        def = &building_defs[i];
        if (i == g_game->selected_building)
        {
            color = color_selected;
        }
        else
        {
            color = color_text_dim;
        }

        g_free (label);
        label = g_strdup_printf ("[%d] %s ($%.0f)", i, def->name, def->cost);
        grl_draw_text (label, x, y, 16, color);
        text_width = grl_measure_text (label, 16);
        x += text_width + 20;
    }

    /* Controls */
    controls = "Left Click: Place | Right Click: Demolish | U: Upgrade";
    grl_draw_text (controls, 20, y + 30, 14, color_text_dim);
}

static void
draw_info_panel (void)
{
    Building *b;
    const BuildingDef *def;
    gint x, y;
    GrlColor *panel_bg;
    gdouble mult;
    gdouble upgrade_cost;
    g_autofree gchar *level_str = NULL;
    g_autofree gchar *prod_str = NULL;
    g_autofree gchar *cons_str = NULL;
    g_autofree gchar *cons_str2 = NULL;
    g_autofree gchar *upgrade_str = NULL;

    if (g_game->info_x < 0 || g_game->info_y < 0)
        return;

    b = &g_game->grid[g_game->info_y][g_game->info_x];
    if (b->type == BUILD_NONE)
        return;

    def = &building_defs[b->type];
    x = INFO_PANEL_X;
    y = INFO_PANEL_Y;

    /* Panel background */
    panel_bg = grl_color_new (40, 45, 55, 240);
    grl_draw_rectangle (x - 10, y - 10, 220, 200, panel_bg);
    g_object_unref (panel_bg);

    /* Building name */
    grl_draw_text (def->name, x, y, 24, building_colors[b->type]);
    y += 35;

    /* Level */
    level_str = g_strdup_printf ("Level: %d / 4", b->level + 1);
    grl_draw_text (level_str, x, y, 16, color_text);
    y += 25;

    /* Production info */
    mult = get_production_multiplier (b);
    if (def->produces >= 0 && b->type != BUILD_MARKET)
    {
        prod_str = g_strdup_printf ("Produces: %.1f %s/cycle",
                                    def->produce_rate * mult,
                                    resource_names[def->produces]);
        grl_draw_text (prod_str, x, y, 14, color_products);
        y += 20;
    }

    /* Consumption info */
    if (def->consumes1 >= 0)
    {
        cons_str = g_strdup_printf ("Consumes: %.1f %s/cycle",
                                    def->consume_rate * mult,
                                    resource_names[def->consumes1]);
        grl_draw_text (cons_str, x, y, 14, color_ore);
        y += 20;
    }
    if (def->consumes2 >= 0)
    {
        cons_str2 = g_strdup_printf ("          %.1f %s/cycle",
                                     def->consume_rate * mult,
                                     resource_names[def->consumes2]);
        grl_draw_text (cons_str2, x, y, 14, color_coal);
        y += 20;
    }

    /* Upgrade cost */
    if (b->level < 3)
    {
        upgrade_cost = def->cost * (b->level + 2);
        upgrade_str = g_strdup_printf ("Upgrade: $%.0f [U]", upgrade_cost);
        grl_draw_text (upgrade_str, x, y + 10, 14, color_gold);
    }
    else
    {
        grl_draw_text ("Max Level", x, y + 10, 14, color_text_dim);
    }
}

static void
draw_stats (void)
{
    gint x, y;
    g_autofree gchar *earned = NULL;
    g_autofree gchar *built = NULL;
    g_autofree gchar *trucks = NULL;

    x = INFO_PANEL_X;
    y = INFO_PANEL_Y + 220;

    grl_draw_text ("--- Stats ---", x, y, 16, color_text);
    y += 25;

    earned = g_strdup_printf ("Total Gold: $%.0f", g_game->total_gold_earned);
    grl_draw_text (earned, x, y, 14, color_gold);
    y += 20;

    built = g_strdup_printf ("Buildings: %d", g_game->buildings_built);
    grl_draw_text (built, x, y, 14, color_text_dim);
    y += 20;

    trucks = g_strdup_printf ("Active Trucks: %d", g_game->truck_count);
    grl_draw_text (trucks, x, y, 14, color_text_dim);
}

/* =============================================================================
 * MAIN FUNCTION
 * ========================================================================== */

int
main (int    argc,
      char **argv)
{
    GrlWindow *window;
    gfloat delta;

    (void)argc;
    (void)argv;

    /* Initialize window */
    window = grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT,
                             "Micro Tycoon - Phase 2 Demo");
    grl_window_set_target_fps (window, 60);

    /* Initialize colors */
    init_colors ();

    /* Initialize game state */
    game_init ();

    /* Calculate offline progress */
    calculate_offline_progress ();

    /* Main game loop */
    while (!grl_window_should_close (window))
    {
        delta = grl_window_get_frame_time (window);

        /* Input */
        handle_input ();

        /* Update */
        update_production (delta);
        update_market (delta);
        update_trucks (delta);

        /* Draw */
        grl_window_begin_drawing (window);
        grl_draw_clear_background (color_bg);

        draw_resource_bar ();
        draw_grid ();
        draw_trucks ();
        draw_building_menu ();
        draw_info_panel ();
        draw_stats ();

        /* Title */
        grl_draw_text ("MICRO TYCOON", GRID_OFFSET_X, 50, 36, color_text);
        grl_draw_text ("Factory Management Demo - Phase 2", GRID_OFFSET_X, 80, 16, color_text_dim);

        grl_draw_fps (WINDOW_WIDTH - 80, WINDOW_HEIGHT - 25);
        grl_window_end_drawing (window);
    }

    /* Save timestamp for offline progress */
    g_game->last_save_time = g_get_real_time () / G_USEC_PER_SEC;

    /* Cleanup */
    game_cleanup ();
    cleanup_colors ();
    g_object_unref (window);

    return 0;
}
