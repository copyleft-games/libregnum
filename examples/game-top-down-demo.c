/* game-top-down-demo.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A top-down action RPG demo demonstrating LrgTopDownTemplate.
 *
 * Features demonstrated:
 * - Subclassing LrgTopDownTemplate for custom top-down game logic
 * - 8-directional movement with facing direction
 * - Interaction system for NPCs and objects
 * - Inventory system with item pickups
 * - Basic melee and magic combat
 * - Health and mana system with regeneration
 * - Room transitions and multi-area world
 * - Simple quest tracking
 * - Dodge roll with invincibility frames
 * - Enemy AI with patrol and chase
 *
 * Controls:
 *   WASD/Arrows  - Move character (8-directional)
 *   Space        - Dodge roll
 *   E            - Interact / Talk
 *   LMB/Z        - Melee attack
 *   RMB/X        - Magic attack (costs mana)
 *   I            - Toggle inventory
 *   Tab          - Toggle map
 *   1-4          - Use consumable items
 *   ESC          - Exit
 */

/* =============================================================================
 * INCLUDES
 * ========================================================================== */

#include <libregnum.h>
#include <graylib.h>
#include <math.h>

/* =============================================================================
 * CONSTANTS
 * ========================================================================== */

#define WINDOW_WIDTH        1280
#define WINDOW_HEIGHT       720
#define TILE_SIZE           32
#define ROOM_WIDTH          20
#define ROOM_HEIGHT         15
#define ROOM_COUNT          4
#define MAX_ENEMIES         8
#define MAX_ITEMS           16
#define MAX_NPCS            4
#define INVENTORY_SIZE      12
#define ATTACK_RANGE        48.0f
#define ATTACK_DAMAGE       15.0f
#define ATTACK_COOLDOWN     0.35f
#define MAGIC_DAMAGE        25.0f
#define MAGIC_COST          20.0f
#define MAGIC_COOLDOWN      0.6f
#define DODGE_DISTANCE      80.0f
#define DODGE_DURATION      0.25f
#define DODGE_COOLDOWN      0.5f
#define ENEMY_HEALTH        40.0f
#define ENEMY_DAMAGE        8.0f
#define MANA_REGEN_RATE     5.0f
#define HEALTH_REGEN_RATE   1.0f

/* =============================================================================
 * COLORS
 * ========================================================================== */

static GrlColor *color_floor = NULL;
static GrlColor *color_wall = NULL;
static GrlColor *color_door = NULL;
static GrlColor *color_player = NULL;
static GrlColor *color_player_dodge = NULL;
static GrlColor *color_enemy = NULL;
static GrlColor *color_enemy_alert = NULL;
static GrlColor *color_npc = NULL;
static GrlColor *color_item_health = NULL;
static GrlColor *color_item_mana = NULL;
static GrlColor *color_item_key = NULL;
static GrlColor *color_item_coin = NULL;
static GrlColor *color_health_bar = NULL;
static GrlColor *color_mana_bar = NULL;
static GrlColor *color_bar_bg = NULL;
static GrlColor *color_hud_text = NULL;
static GrlColor *color_interact = NULL;
static GrlColor *color_attack = NULL;
static GrlColor *color_magic = NULL;
static GrlColor *color_shadow = NULL;
static GrlColor *color_dialog_bg = NULL;

/* =============================================================================
 * DATA STRUCTURES
 * ========================================================================== */

typedef enum
{
    TILE_FLOOR = 0,
    TILE_WALL,
    TILE_DOOR_N,
    TILE_DOOR_S,
    TILE_DOOR_E,
    TILE_DOOR_W
} TileType;

typedef struct
{
    TileType tiles[ROOM_HEIGHT][ROOM_WIDTH];
    gint connection_n;  /* -1 = no connection */
    gint connection_s;
    gint connection_e;
    gint connection_w;
} Room;

typedef enum
{
    ITEM_TYPE_NONE = 0,
    ITEM_TYPE_HEALTH_POTION,
    ITEM_TYPE_MANA_POTION,
    ITEM_TYPE_KEY,
    ITEM_TYPE_COIN
} ItemType;

typedef struct
{
    gfloat x, y;
    ItemType type;
    gboolean active;
    gfloat bob_timer;
} WorldItem;

typedef enum
{
    ENEMY_STATE_IDLE,
    ENEMY_STATE_PATROL,
    ENEMY_STATE_CHASE,
    ENEMY_STATE_ATTACK,
    ENEMY_STATE_HURT,
    ENEMY_STATE_DEAD
} EnemyState;

typedef struct
{
    gfloat x, y;
    gfloat health;
    EnemyState state;
    gfloat state_timer;
    gfloat attack_cooldown;
    gfloat facing_angle;
    gfloat patrol_target_x;
    gfloat patrol_target_y;
    gint room;
} Enemy;

typedef struct
{
    gfloat x, y;
    gint room;
    const gchar *name;
    const gchar *dialog;
    gboolean has_quest;
    gboolean quest_complete;
} NPC;

/* =============================================================================
 * GAME STATE
 * ========================================================================== */

static Room rooms[ROOM_COUNT];
static Enemy enemies[MAX_ENEMIES];
static WorldItem world_items[MAX_ITEMS];
static NPC npcs[MAX_NPCS];
static ItemType inventory[INVENTORY_SIZE];
static gint current_room = 0;

/* =============================================================================
 * CUSTOM TOP DOWN TYPE
 * ========================================================================== */

#define DEMO_TYPE_TOP_DOWN (demo_top_down_get_type ())
G_DECLARE_FINAL_TYPE (DemoTopDown, demo_top_down, DEMO, TOP_DOWN, LrgTopDownTemplate)

struct _DemoTopDown
{
    LrgTopDownTemplate parent_instance;

    /* Player stats */
    gfloat health;
    gfloat max_health;
    gfloat mana;
    gfloat max_mana;

    /* Combat */
    gfloat attack_cooldown;
    gfloat magic_cooldown;
    gboolean is_attacking;
    gfloat attack_timer;
    gfloat magic_effect_timer;

    /* Dodge */
    gboolean is_dodging;
    gfloat dodge_timer;
    gfloat dodge_cooldown;
    gfloat dodge_dir_x;
    gfloat dodge_dir_y;

    /* Damage feedback */
    gfloat damage_flash;
    gfloat invincibility;

    /* Stats */
    gint score;
    gint coins;
    gint keys;
    gint enemies_defeated;
    gfloat play_time;

    /* UI State */
    gboolean inventory_open;
    gboolean map_open;
    gboolean dialog_active;
    const gchar *current_dialog;
    gfloat dialog_timer;

    /* Quest */
    gboolean quest_active;
    gint quest_enemies_needed;
    gint quest_enemies_killed;
};

G_DEFINE_FINAL_TYPE (DemoTopDown, demo_top_down, LRG_TYPE_TOP_DOWN_TEMPLATE)

/* =============================================================================
 * WORLD GENERATION
 * ========================================================================== */

static void
generate_room (Room *room, gint room_index)
{
    gint x, y;

    /* Clear room */
    for (y = 0; y < ROOM_HEIGHT; y++)
    {
        for (x = 0; x < ROOM_WIDTH; x++)
        {
            /* Walls on edges */
            if (x == 0 || x == ROOM_WIDTH - 1 || y == 0 || y == ROOM_HEIGHT - 1)
                room->tiles[y][x] = TILE_WALL;
            else
                room->tiles[y][x] = TILE_FLOOR;
        }
    }

    /* Add some interior walls for variety */
    switch (room_index)
    {
    case 0: /* Start room - mostly open */
        room->tiles[5][5] = TILE_WALL;
        room->tiles[5][6] = TILE_WALL;
        room->tiles[9][12] = TILE_WALL;
        room->tiles[10][12] = TILE_WALL;
        break;

    case 1: /* Corridor-like */
        for (x = 3; x < 8; x++)
            room->tiles[6][x] = TILE_WALL;
        for (x = 12; x < 17; x++)
            room->tiles[8][x] = TILE_WALL;
        break;

    case 2: /* Pillars */
        room->tiles[4][4] = TILE_WALL;
        room->tiles[4][10] = TILE_WALL;
        room->tiles[4][15] = TILE_WALL;
        room->tiles[10][4] = TILE_WALL;
        room->tiles[10][10] = TILE_WALL;
        room->tiles[10][15] = TILE_WALL;
        break;

    case 3: /* Treasure room */
        for (x = 6; x < 14; x++)
        {
            room->tiles[4][x] = TILE_WALL;
            room->tiles[10][x] = TILE_WALL;
        }
        room->tiles[7][7] = TILE_WALL;
        room->tiles[7][12] = TILE_WALL;
        break;
    }

    /* Initialize connections */
    room->connection_n = -1;
    room->connection_s = -1;
    room->connection_e = -1;
    room->connection_w = -1;
}

static void
setup_room_connections (void)
{
    /* Room 0 connects to 1 (east) and 2 (south) */
    rooms[0].connection_e = 1;
    rooms[0].connection_s = 2;
    rooms[0].tiles[ROOM_HEIGHT / 2][ROOM_WIDTH - 1] = TILE_DOOR_E;
    rooms[0].tiles[ROOM_HEIGHT - 1][ROOM_WIDTH / 2] = TILE_DOOR_S;

    /* Room 1 connects to 0 (west) and 3 (south) */
    rooms[1].connection_w = 0;
    rooms[1].connection_s = 3;
    rooms[1].tiles[ROOM_HEIGHT / 2][0] = TILE_DOOR_W;
    rooms[1].tiles[ROOM_HEIGHT - 1][ROOM_WIDTH / 2] = TILE_DOOR_S;

    /* Room 2 connects to 0 (north) and 3 (east) */
    rooms[2].connection_n = 0;
    rooms[2].connection_e = 3;
    rooms[2].tiles[0][ROOM_WIDTH / 2] = TILE_DOOR_N;
    rooms[2].tiles[ROOM_HEIGHT / 2][ROOM_WIDTH - 1] = TILE_DOOR_E;

    /* Room 3 connects to 1 (north) and 2 (west) */
    rooms[3].connection_n = 1;
    rooms[3].connection_w = 2;
    rooms[3].tiles[0][ROOM_WIDTH / 2] = TILE_DOOR_N;
    rooms[3].tiles[ROOM_HEIGHT / 2][0] = TILE_DOOR_W;
}

static void
init_world (void)
{
    gint i;

    /* Generate rooms */
    for (i = 0; i < ROOM_COUNT; i++)
    {
        generate_room (&rooms[i], i);
    }
    setup_room_connections ();

    /* Initialize enemies */
    for (i = 0; i < MAX_ENEMIES; i++)
    {
        enemies[i].x = (3 + g_random_int_range (0, ROOM_WIDTH - 6)) * TILE_SIZE;
        enemies[i].y = (3 + g_random_int_range (0, ROOM_HEIGHT - 6)) * TILE_SIZE;
        enemies[i].health = ENEMY_HEALTH;
        enemies[i].state = ENEMY_STATE_PATROL;
        enemies[i].state_timer = g_random_double () * 2.0f;
        enemies[i].attack_cooldown = 0.0f;
        enemies[i].facing_angle = g_random_double () * G_PI * 2.0f;
        enemies[i].patrol_target_x = enemies[i].x;
        enemies[i].patrol_target_y = enemies[i].y;
        enemies[i].room = i % ROOM_COUNT;
    }

    /* Initialize items */
    for (i = 0; i < MAX_ITEMS; i++)
    {
        world_items[i].x = (2 + g_random_int_range (0, ROOM_WIDTH - 4)) * TILE_SIZE;
        world_items[i].y = (2 + g_random_int_range (0, ROOM_HEIGHT - 4)) * TILE_SIZE;
        world_items[i].active = TRUE;
        world_items[i].bob_timer = g_random_double () * G_PI * 2.0f;

        /* Distribute item types */
        if (i < 5)
            world_items[i].type = ITEM_TYPE_COIN;
        else if (i < 8)
            world_items[i].type = ITEM_TYPE_HEALTH_POTION;
        else if (i < 10)
            world_items[i].type = ITEM_TYPE_MANA_POTION;
        else
            world_items[i].type = ITEM_TYPE_KEY;
    }

    /* Initialize NPCs */
    npcs[0].x = 5 * TILE_SIZE;
    npcs[0].y = 5 * TILE_SIZE;
    npcs[0].room = 0;
    npcs[0].name = "Old Sage";
    npcs[0].dialog = "Welcome, adventurer! Defeat 5 enemies\nto prove your worth.";
    npcs[0].has_quest = TRUE;
    npcs[0].quest_complete = FALSE;

    npcs[1].x = 14 * TILE_SIZE;
    npcs[1].y = 10 * TILE_SIZE;
    npcs[1].room = 1;
    npcs[1].name = "Merchant";
    npcs[1].dialog = "I have nothing to sell today.\nCome back later!";
    npcs[1].has_quest = FALSE;
    npcs[1].quest_complete = FALSE;

    npcs[2].x = 10 * TILE_SIZE;
    npcs[2].y = 7 * TILE_SIZE;
    npcs[2].room = 2;
    npcs[2].name = "Guard";
    npcs[2].dialog = "The treasure room is to the east.\nBe careful of the enemies!";
    npcs[2].has_quest = FALSE;
    npcs[2].quest_complete = FALSE;

    npcs[3].x = 10 * TILE_SIZE;
    npcs[3].y = 7 * TILE_SIZE;
    npcs[3].room = 3;
    npcs[3].name = "Treasure Keeper";
    npcs[3].dialog = "You found the treasure room!\nTake what you need.";
    npcs[3].has_quest = FALSE;
    npcs[3].quest_complete = FALSE;

    /* Clear inventory */
    for (i = 0; i < INVENTORY_SIZE; i++)
    {
        inventory[i] = ITEM_TYPE_NONE;
    }
}

/* =============================================================================
 * UTILITY FUNCTIONS
 * ========================================================================== */

static gfloat
distance_2d (gfloat x1, gfloat y1, gfloat x2, gfloat y2)
{
    gfloat dx = x2 - x1;
    gfloat dy = y2 - y1;
    return sqrtf (dx * dx + dy * dy);
}

static gboolean
is_tile_solid (gint tile_x, gint tile_y)
{
    TileType tile;

    if (tile_x < 0 || tile_x >= ROOM_WIDTH || tile_y < 0 || tile_y >= ROOM_HEIGHT)
        return TRUE;

    tile = rooms[current_room].tiles[tile_y][tile_x];
    return tile == TILE_WALL;
}

static gboolean
add_to_inventory (ItemType type)
{
    gint i;
    for (i = 0; i < INVENTORY_SIZE; i++)
    {
        if (inventory[i] == ITEM_TYPE_NONE)
        {
            inventory[i] = type;
            return TRUE;
        }
    }
    return FALSE;
}

/* =============================================================================
 * COLLISION
 * ========================================================================== */

static gboolean
demo_top_down_check_collision (LrgTopDownTemplate *template,
                               gfloat              new_x,
                               gfloat              new_y,
                               gfloat             *resolved_x,
                               gfloat             *resolved_y)
{
    gfloat player_half_w = 12.0f;
    gfloat player_half_h = 12.0f;
    gboolean collision = FALSE;
    gint tile_x, tile_y;
    gfloat corners[4][2];
    gint i;
    gfloat old_x, old_y;
    gboolean x_ok, y_ok;

    (void)template;

    *resolved_x = new_x;
    *resolved_y = new_y;

    /* Check corners */
    corners[0][0] = new_x - player_half_w;
    corners[0][1] = new_y - player_half_h;
    corners[1][0] = new_x + player_half_w;
    corners[1][1] = new_y - player_half_h;
    corners[2][0] = new_x - player_half_w;
    corners[2][1] = new_y + player_half_h;
    corners[3][0] = new_x + player_half_w;
    corners[3][1] = new_y + player_half_h;

    for (i = 0; i < 4; i++)
    {
        tile_x = (gint)(corners[i][0] / TILE_SIZE);
        tile_y = (gint)(corners[i][1] / TILE_SIZE);

        if (is_tile_solid (tile_x, tile_y))
        {
            collision = TRUE;
        }
    }

    if (collision)
    {
        /* Simple resolution - stop movement */
        old_x = lrg_top_down_template_get_player_x (template);
        old_y = lrg_top_down_template_get_player_y (template);

        /* Try X movement only */
        x_ok = TRUE;
        for (i = 0; i < 4; i++)
        {
            tile_x = (gint)((new_x + (i % 2 == 0 ? -player_half_w : player_half_w)) / TILE_SIZE);
            tile_y = (gint)((old_y + (i < 2 ? -player_half_h : player_half_h)) / TILE_SIZE);
            if (is_tile_solid (tile_x, tile_y))
                x_ok = FALSE;
        }

        /* Try Y movement only */
        y_ok = TRUE;
        for (i = 0; i < 4; i++)
        {
            tile_x = (gint)((old_x + (i % 2 == 0 ? -player_half_w : player_half_w)) / TILE_SIZE);
            tile_y = (gint)((new_y + (i < 2 ? -player_half_h : player_half_h)) / TILE_SIZE);
            if (is_tile_solid (tile_x, tile_y))
                y_ok = FALSE;
        }

        if (x_ok)
            *resolved_x = new_x;
        else
            *resolved_x = old_x;

        if (y_ok)
            *resolved_y = new_y;
        else
            *resolved_y = old_y;
    }

    return collision;
}

/* =============================================================================
 * INTERACTION
 * ========================================================================== */

static gboolean
demo_top_down_on_interact (LrgTopDownTemplate *template)
{
    DemoTopDown *self = DEMO_TOP_DOWN (template);
    gfloat px, py;
    gfloat dist;
    gint i;
    NPC *npc;
    WorldItem *item;

    if (self->dialog_active)
    {
        /* Close dialog */
        self->dialog_active = FALSE;
        self->current_dialog = NULL;
        return TRUE;
    }

    px = lrg_top_down_template_get_player_x (template);
    py = lrg_top_down_template_get_player_y (template);

    /* Check NPCs first */
    for (i = 0; i < MAX_NPCS; i++)
    {
        npc = &npcs[i];
        if (npc->room != current_room)
            continue;

        dist = distance_2d (px, py, npc->x, npc->y);
        if (dist < 50.0f)
        {
            /* Start dialog */
            self->dialog_active = TRUE;

            if (npc->has_quest && !self->quest_active && !npc->quest_complete)
            {
                self->quest_active = TRUE;
                self->quest_enemies_needed = 5;
                self->quest_enemies_killed = 0;
                self->current_dialog = npc->dialog;
            }
            else if (npc->has_quest && self->quest_active &&
                     self->quest_enemies_killed >= self->quest_enemies_needed)
            {
                npc->quest_complete = TRUE;
                self->quest_active = FALSE;
                self->score += 500;
                self->current_dialog = "Well done! You have proven yourself.\nHere is your reward!";
            }
            else if (npc->has_quest && self->quest_active)
            {
                g_autofree gchar *progress = g_strdup_printf (
                    "Keep going! You've defeated %d of %d enemies.",
                    self->quest_enemies_killed, self->quest_enemies_needed);
                self->current_dialog = progress;
            }
            else
            {
                self->current_dialog = npc->dialog;
            }
            return TRUE;
        }
    }

    /* Check items */
    for (i = 0; i < MAX_ITEMS; i++)
    {
        item = &world_items[i];
        if (!item->active)
            continue;

        dist = distance_2d (px, py, item->x, item->y);
        if (dist < 40.0f)
        {
            /* Pick up item */
            switch (item->type)
            {
            case ITEM_TYPE_COIN:
                self->coins++;
                self->score += 10;
                item->active = FALSE;
                break;

            case ITEM_TYPE_KEY:
                self->keys++;
                item->active = FALSE;
                break;

            case ITEM_TYPE_HEALTH_POTION:
            case ITEM_TYPE_MANA_POTION:
                if (add_to_inventory (item->type))
                    item->active = FALSE;
                break;

            default:
                break;
            }
            return TRUE;
        }
    }

    return FALSE;
}

/* =============================================================================
 * ROOM TRANSITIONS
 * ========================================================================== */

static void
check_room_transition (DemoTopDown *self)
{
    LrgTopDownTemplate *template = LRG_TOP_DOWN_TEMPLATE (self);
    gfloat px = lrg_top_down_template_get_player_x (template);
    gfloat py = lrg_top_down_template_get_player_y (template);
    gint tile_x = (gint)(px / TILE_SIZE);
    gint tile_y = (gint)(py / TILE_SIZE);
    Room *room = &rooms[current_room];
    TileType tile = room->tiles[tile_y][tile_x];
    gint new_room = -1;
    gfloat new_x = px, new_y = py;

    switch (tile)
    {
    case TILE_DOOR_N:
        if (room->connection_n >= 0)
        {
            new_room = room->connection_n;
            new_y = (ROOM_HEIGHT - 2) * TILE_SIZE;
        }
        break;

    case TILE_DOOR_S:
        if (room->connection_s >= 0)
        {
            new_room = room->connection_s;
            new_y = 2 * TILE_SIZE;
        }
        break;

    case TILE_DOOR_E:
        if (room->connection_e >= 0)
        {
            new_room = room->connection_e;
            new_x = 2 * TILE_SIZE;
        }
        break;

    case TILE_DOOR_W:
        if (room->connection_w >= 0)
        {
            new_room = room->connection_w;
            new_x = (ROOM_WIDTH - 2) * TILE_SIZE;
        }
        break;

    default:
        break;
    }

    if (new_room >= 0)
    {
        current_room = new_room;
        lrg_top_down_template_set_player_position (template, new_x, new_y);
    }
}

/* =============================================================================
 * ENEMY AI
 * ========================================================================== */

static void
update_enemy (Enemy      *enemy,
              gfloat      player_x,
              gfloat      player_y,
              gfloat      delta,
              DemoTopDown *game)
{
    gfloat dist;
    gfloat angle;
    gfloat speed;

    if (enemy->room != current_room)
        return;

    if (enemy->state == ENEMY_STATE_DEAD)
        return;

    if (enemy->attack_cooldown > 0.0f)
        enemy->attack_cooldown -= delta;

    dist = distance_2d (enemy->x, enemy->y, player_x, player_y);
    angle = atan2f (player_y - enemy->y, player_x - enemy->x);

    switch (enemy->state)
    {
    case ENEMY_STATE_IDLE:
        enemy->state_timer -= delta;
        if (enemy->state_timer <= 0.0f)
        {
            enemy->state = ENEMY_STATE_PATROL;
            enemy->patrol_target_x = enemy->x + (g_random_double () - 0.5) * 100.0f;
            enemy->patrol_target_y = enemy->y + (g_random_double () - 0.5) * 100.0f;
        }
        if (dist < 150.0f)
            enemy->state = ENEMY_STATE_CHASE;
        break;

    case ENEMY_STATE_PATROL:
        {
            gfloat patrol_dist = distance_2d (enemy->x, enemy->y,
                                              enemy->patrol_target_x,
                                              enemy->patrol_target_y);
            if (patrol_dist < 10.0f)
            {
                enemy->state = ENEMY_STATE_IDLE;
                enemy->state_timer = 1.0f + g_random_double ();
            }
            else
            {
                gfloat patrol_angle = atan2f (enemy->patrol_target_y - enemy->y,
                                              enemy->patrol_target_x - enemy->x);
                enemy->facing_angle = patrol_angle;
                speed = 30.0f * delta;
                enemy->x += cosf (patrol_angle) * speed;
                enemy->y += sinf (patrol_angle) * speed;
            }
        }
        if (dist < 150.0f)
            enemy->state = ENEMY_STATE_CHASE;
        break;

    case ENEMY_STATE_CHASE:
        if (dist > 200.0f)
        {
            enemy->state = ENEMY_STATE_IDLE;
            enemy->state_timer = 1.0f;
        }
        else if (dist < 35.0f && enemy->attack_cooldown <= 0.0f)
        {
            enemy->state = ENEMY_STATE_ATTACK;
            enemy->state_timer = 0.3f;
        }
        else
        {
            enemy->facing_angle = angle;
            speed = 60.0f * delta;
            enemy->x += cosf (angle) * speed;
            enemy->y += sinf (angle) * speed;
        }
        break;

    case ENEMY_STATE_ATTACK:
        enemy->state_timer -= delta;
        if (enemy->state_timer <= 0.0f)
        {
            /* Deal damage */
            if (dist < 45.0f && game->invincibility <= 0.0f)
            {
                game->health -= ENEMY_DAMAGE;
                game->damage_flash = 0.2f;
                game->invincibility = 0.5f;
                lrg_game_template_shake (LRG_GAME_TEMPLATE (game), 0.2f);

                if (game->health <= 0.0f)
                {
                    game->health = game->max_health;
                    lrg_top_down_template_set_player_position (
                        LRG_TOP_DOWN_TEMPLATE (game),
                        ROOM_WIDTH * TILE_SIZE / 2,
                        ROOM_HEIGHT * TILE_SIZE / 2);
                    current_room = 0;
                    game->score = MAX (0, game->score - 100);
                }
            }
            enemy->attack_cooldown = 1.0f;
            enemy->state = ENEMY_STATE_CHASE;
        }
        break;

    case ENEMY_STATE_HURT:
        enemy->state_timer -= delta;
        if (enemy->state_timer <= 0.0f)
            enemy->state = ENEMY_STATE_CHASE;
        break;

    default:
        break;
    }

    /* Keep in bounds */
    enemy->x = CLAMP (enemy->x, TILE_SIZE * 2, (ROOM_WIDTH - 2) * TILE_SIZE);
    enemy->y = CLAMP (enemy->y, TILE_SIZE * 2, (ROOM_HEIGHT - 2) * TILE_SIZE);
}

/* =============================================================================
 * COMBAT
 * ========================================================================== */

static void
perform_attack (DemoTopDown *self)
{
    LrgTopDownTemplate *template = LRG_TOP_DOWN_TEMPLATE (self);
    gfloat px, py;
    gfloat facing_angle;
    gfloat dist, angle_to_enemy, angle_diff;
    gint i;
    Enemy *enemy;

    px = lrg_top_down_template_get_player_x (template);
    py = lrg_top_down_template_get_player_y (template);
    facing_angle = lrg_top_down_template_get_facing_angle (template);

    for (i = 0; i < MAX_ENEMIES; i++)
    {
        enemy = &enemies[i];
        if (enemy->room != current_room || enemy->state == ENEMY_STATE_DEAD)
            continue;

        dist = distance_2d (px, py, enemy->x, enemy->y);
        if (dist > ATTACK_RANGE)
            continue;

        /* Check if enemy is in front */
        angle_to_enemy = atan2f (enemy->y - py, enemy->x - px);
        angle_diff = fabsf (fmodf (angle_to_enemy - facing_angle + (gfloat)G_PI * 3.0f, (gfloat)G_PI * 2.0f) - (gfloat)G_PI);

        if (angle_diff < (gfloat)G_PI / 2.0f)
        {
            enemy->health -= ATTACK_DAMAGE;
            enemy->state = ENEMY_STATE_HURT;
            enemy->state_timer = 0.2f;

            /* Knockback */
            enemy->x += cosf (facing_angle) * 15.0f;
            enemy->y += sinf (facing_angle) * 15.0f;

            if (enemy->health <= 0.0f)
            {
                enemy->state = ENEMY_STATE_DEAD;
                self->enemies_defeated++;
                self->score += 50;
                if (self->quest_active)
                    self->quest_enemies_killed++;
            }
        }
    }
}

static void
perform_magic (DemoTopDown *self)
{
    LrgTopDownTemplate *template = LRG_TOP_DOWN_TEMPLATE (self);
    gfloat px, py;
    gfloat facing_angle;
    gfloat range, dist, angle_to_enemy, angle_diff;
    gint i;
    Enemy *enemy;

    if (self->mana < MAGIC_COST)
        return;

    self->mana -= MAGIC_COST;

    px = lrg_top_down_template_get_player_x (template);
    py = lrg_top_down_template_get_player_y (template);
    facing_angle = lrg_top_down_template_get_facing_angle (template);

    /* Magic has longer range and hits in a cone */
    range = ATTACK_RANGE * 2.0f;

    for (i = 0; i < MAX_ENEMIES; i++)
    {
        enemy = &enemies[i];
        if (enemy->room != current_room || enemy->state == ENEMY_STATE_DEAD)
            continue;

        dist = distance_2d (px, py, enemy->x, enemy->y);
        if (dist > range)
            continue;

        angle_to_enemy = atan2f (enemy->y - py, enemy->x - px);
        angle_diff = fabsf (fmodf (angle_to_enemy - facing_angle + (gfloat)G_PI * 3.0f, (gfloat)G_PI * 2.0f) - (gfloat)G_PI);

        if (angle_diff < (gfloat)G_PI / 3.0f)
        {
            enemy->health -= MAGIC_DAMAGE;
            enemy->state = ENEMY_STATE_HURT;
            enemy->state_timer = 0.3f;

            /* Magic knockback */
            enemy->x += cosf (facing_angle) * 25.0f;
            enemy->y += sinf (facing_angle) * 25.0f;

            if (enemy->health <= 0.0f)
            {
                enemy->state = ENEMY_STATE_DEAD;
                self->enemies_defeated++;
                self->score += 50;
                if (self->quest_active)
                    self->quest_enemies_killed++;
            }
        }
    }

    self->magic_effect_timer = 0.3f;
}

/* =============================================================================
 * UPDATE
 * ========================================================================== */

static void
demo_top_down_pre_update (LrgGameTemplate *template,
                          gdouble          delta)
{
    DemoTopDown *self = DEMO_TOP_DOWN (template);
    LrgTopDownTemplate *td = LRG_TOP_DOWN_TEMPLATE (template);
    gfloat px, py;
    gint i;

    /* Skip normal update if dialog is active */
    if (self->dialog_active)
    {
        if (grl_input_is_key_pressed (GRL_KEY_E) || grl_input_is_key_pressed (GRL_KEY_SPACE))
        {
            self->dialog_active = FALSE;
            self->current_dialog = NULL;
        }
        return;
    }

    /* Update timers */
    self->play_time += (gfloat)delta;

    if (self->attack_cooldown > 0.0f)
        self->attack_cooldown -= (gfloat)delta;

    if (self->magic_cooldown > 0.0f)
        self->magic_cooldown -= (gfloat)delta;

    if (self->attack_timer > 0.0f)
    {
        self->attack_timer -= (gfloat)delta;
        if (self->attack_timer <= 0.0f)
            self->is_attacking = FALSE;
    }

    if (self->magic_effect_timer > 0.0f)
        self->magic_effect_timer -= (gfloat)delta;

    if (self->damage_flash > 0.0f)
        self->damage_flash -= (gfloat)delta;

    if (self->invincibility > 0.0f)
        self->invincibility -= (gfloat)delta;

    if (self->dodge_cooldown > 0.0f)
        self->dodge_cooldown -= (gfloat)delta;

    /* Regeneration */
    if (self->mana < self->max_mana)
        self->mana = MIN (self->mana + MANA_REGEN_RATE * (gfloat)delta, self->max_mana);

    /* Handle dodge */
    if (self->is_dodging)
    {
        self->dodge_timer -= (gfloat)delta;
        if (self->dodge_timer <= 0.0f)
        {
            self->is_dodging = FALSE;
        }
        else
        {
            /* Move in dodge direction */
            gfloat dodge_speed = DODGE_DISTANCE / DODGE_DURATION;
            px = lrg_top_down_template_get_player_x (td);
            py = lrg_top_down_template_get_player_y (td);
            lrg_top_down_template_set_player_position (
                td,
                px + self->dodge_dir_x * dodge_speed * (gfloat)delta,
                py + self->dodge_dir_y * dodge_speed * (gfloat)delta);
        }
    }

    /* Handle attack input */
    if ((grl_input_is_mouse_button_pressed (GRL_MOUSE_BUTTON_LEFT) || grl_input_is_key_pressed (GRL_KEY_Z)) &&
        self->attack_cooldown <= 0.0f && !self->is_dodging)
    {
        self->is_attacking = TRUE;
        self->attack_timer = 0.15f;
        self->attack_cooldown = ATTACK_COOLDOWN;
        perform_attack (self);
    }

    /* Handle magic input */
    if ((grl_input_is_mouse_button_pressed (GRL_MOUSE_BUTTON_RIGHT) || grl_input_is_key_pressed (GRL_KEY_X)) &&
        self->magic_cooldown <= 0.0f && !self->is_dodging && self->mana >= MAGIC_COST)
    {
        self->magic_cooldown = MAGIC_COOLDOWN;
        perform_magic (self);
    }

    /* Handle dodge input */
    if (grl_input_is_key_pressed (GRL_KEY_SPACE) && self->dodge_cooldown <= 0.0f && !self->is_dodging)
    {
        gfloat vx, vy;
        lrg_top_down_template_get_player_velocity (td, &vx, &vy);

        if (fabsf (vx) > 0.1f || fabsf (vy) > 0.1f)
        {
            gfloat len = sqrtf (vx * vx + vy * vy);
            self->dodge_dir_x = vx / len;
            self->dodge_dir_y = vy / len;
        }
        else
        {
            gfloat facing = lrg_top_down_template_get_facing_angle (td);
            self->dodge_dir_x = cosf (facing);
            self->dodge_dir_y = sinf (facing);
        }

        self->is_dodging = TRUE;
        self->dodge_timer = DODGE_DURATION;
        self->dodge_cooldown = DODGE_COOLDOWN + DODGE_DURATION;
        self->invincibility = DODGE_DURATION;
    }

    /* Toggle UI */
    if (grl_input_is_key_pressed (GRL_KEY_I))
        self->inventory_open = !self->inventory_open;

    if (grl_input_is_key_pressed (GRL_KEY_TAB))
        self->map_open = !self->map_open;

    /* Use consumables */
    for (i = 0; i < 4; i++)
    {
        if (grl_input_is_key_pressed (GRL_KEY_ONE + i) && inventory[i] != ITEM_TYPE_NONE)
        {
            if (inventory[i] == ITEM_TYPE_HEALTH_POTION)
            {
                self->health = MIN (self->health + 50.0f, self->max_health);
                inventory[i] = ITEM_TYPE_NONE;
            }
            else if (inventory[i] == ITEM_TYPE_MANA_POTION)
            {
                self->mana = MIN (self->mana + 50.0f, self->max_mana);
                inventory[i] = ITEM_TYPE_NONE;
            }
        }
    }

    /* Update enemies */
    px = lrg_top_down_template_get_player_x (td);
    py = lrg_top_down_template_get_player_y (td);

    for (i = 0; i < MAX_ENEMIES; i++)
    {
        update_enemy (&enemies[i], px, py, (gfloat)delta, self);
    }

    /* Update item animations */
    for (i = 0; i < MAX_ITEMS; i++)
    {
        if (world_items[i].active)
            world_items[i].bob_timer += (gfloat)delta * 3.0f;
    }

    /* Check room transitions */
    check_room_transition (self);

    LRG_GAME_TEMPLATE_CLASS (demo_top_down_parent_class)->pre_update (template, delta);
}

/* =============================================================================
 * RENDERING
 * ========================================================================== */

static void
demo_top_down_draw_world (LrgGame2DTemplate *template)
{
    DemoTopDown *self = DEMO_TOP_DOWN (template);
    Room *room = &rooms[current_room];
    gint x, y, i;
    GrlColor *tile_color;
    GrlColor *grid_color;
    GrlColor *item_color;
    GrlColor *enemy_color;
    TileType tile;
    WorldItem *item;
    NPC *npc;
    Enemy *enemy;
    gfloat bob;
    gint fx, fy;

    /* Draw tiles */
    for (y = 0; y < ROOM_HEIGHT; y++)
    {
        for (x = 0; x < ROOM_WIDTH; x++)
        {
            tile = room->tiles[y][x];

            switch (tile)
            {
            case TILE_FLOOR:
                tile_color = color_floor;
                break;
            case TILE_WALL:
                tile_color = color_wall;
                break;
            default:
                tile_color = color_door;
                break;
            }

            grl_draw_rectangle (x * TILE_SIZE, y * TILE_SIZE, TILE_SIZE, TILE_SIZE, tile_color);
        }
    }

    /* Draw grid lines (subtle) */
    grid_color = grl_color_new (60, 60, 60, 50);
    for (x = 0; x <= ROOM_WIDTH; x++)
        grl_draw_line (x * TILE_SIZE, 0, x * TILE_SIZE, ROOM_HEIGHT * TILE_SIZE, grid_color);
    for (y = 0; y <= ROOM_HEIGHT; y++)
        grl_draw_line (0, y * TILE_SIZE, ROOM_WIDTH * TILE_SIZE, y * TILE_SIZE, grid_color);
    grl_color_free (grid_color);

    /* Draw items */
    for (i = 0; i < MAX_ITEMS; i++)
    {
        item = &world_items[i];
        if (!item->active)
            continue;

        bob = sinf (item->bob_timer) * 3.0f;

        switch (item->type)
        {
        case ITEM_TYPE_COIN:
            item_color = color_item_coin;
            break;
        case ITEM_TYPE_HEALTH_POTION:
            item_color = color_item_health;
            break;
        case ITEM_TYPE_MANA_POTION:
            item_color = color_item_mana;
            break;
        case ITEM_TYPE_KEY:
            item_color = color_item_key;
            break;
        default:
            continue;
        }

        grl_draw_circle ((gint)item->x, (gint)(item->y + bob), 8, item_color);
    }

    /* Draw NPCs */
    for (i = 0; i < MAX_NPCS; i++)
    {
        npc = &npcs[i];
        if (npc->room != current_room)
            continue;

        /* Shadow */
        grl_draw_ellipse ((gint)npc->x, (gint)npc->y + 10, 12, 4, color_shadow);

        /* Body */
        grl_draw_circle ((gint)npc->x, (gint)npc->y, 14, color_npc);

        /* Quest indicator */
        if (npc->has_quest && !npc->quest_complete)
        {
            grl_draw_text ("!", (gint)npc->x - 4, (gint)npc->y - 30, 20, color_item_coin);
        }
    }

    /* Draw enemies */
    for (i = 0; i < MAX_ENEMIES; i++)
    {
        enemy = &enemies[i];
        if (enemy->room != current_room || enemy->state == ENEMY_STATE_DEAD)
            continue;

        enemy_color = (enemy->state == ENEMY_STATE_CHASE ||
                       enemy->state == ENEMY_STATE_ATTACK)
                      ? color_enemy_alert : color_enemy;

        /* Shadow */
        grl_draw_ellipse ((gint)enemy->x, (gint)enemy->y + 10, 10, 3, color_shadow);

        /* Body */
        grl_draw_circle ((gint)enemy->x, (gint)enemy->y, 12, enemy_color);

        /* Facing indicator */
        fx = (gint)(enemy->x + cosf (enemy->facing_angle) * 16);
        fy = (gint)(enemy->y + sinf (enemy->facing_angle) * 16);
        grl_draw_circle (fx, fy, 4, enemy_color);

        /* Health bar if damaged */
        if (enemy->health < ENEMY_HEALTH)
        {
            gfloat health_pct = enemy->health / ENEMY_HEALTH;
            grl_draw_rectangle ((gint)enemy->x - 15, (gint)enemy->y - 22, 30, 4, color_bar_bg);
            grl_draw_rectangle ((gint)enemy->x - 15, (gint)enemy->y - 22,
                               (gint)(30 * health_pct), 4, color_health_bar);
        }
    }

    /* Draw magic effect */
    if (self->magic_effect_timer > 0.0f)
    {
        gfloat px = lrg_top_down_template_get_player_x (LRG_TOP_DOWN_TEMPLATE (template));
        gfloat py = lrg_top_down_template_get_player_y (LRG_TOP_DOWN_TEMPLATE (template));
        gfloat facing = lrg_top_down_template_get_facing_angle (LRG_TOP_DOWN_TEMPLATE (template));
        gfloat alpha = self->magic_effect_timer / 0.3f * 150.0f;

        GrlColor *magic_col = grl_color_new (100, 100, 255, (guint8)alpha);

        /* Draw magic cone */
        for (gfloat r = 20.0f; r < ATTACK_RANGE * 2.0f; r += 20.0f)
        {
            gfloat spread = G_PI / 3.0f;
            for (gfloat a = -spread; a <= spread; a += 0.1f)
            {
                gint mx = (gint)(px + cosf (facing + a) * r);
                gint my = (gint)(py + sinf (facing + a) * r);
                grl_draw_circle (mx, my, 5, magic_col);
            }
        }
        grl_color_free (magic_col);
    }
}

static void
demo_top_down_draw_player (LrgTopDownTemplate *template)
{
    DemoTopDown *self = DEMO_TOP_DOWN (template);
    gfloat px, py, facing;
    gfloat trail_x, trail_y;
    GrlColor *player_color;
    GrlColor *trail;
    gint fx, fy;

    px = lrg_top_down_template_get_player_x (template);
    py = lrg_top_down_template_get_player_y (template);
    facing = lrg_top_down_template_get_facing_angle (template);

    /* Dodge visual */
    if (self->is_dodging)
    {
        player_color = color_player_dodge;
        /* Afterimage effect */
        trail_x = px - self->dodge_dir_x * 20.0f;
        trail_y = py - self->dodge_dir_y * 20.0f;
        trail = grl_color_new (100, 150, 200, 100);
        grl_draw_circle ((gint)trail_x, (gint)trail_y, 14, trail);
        grl_color_free (trail);
    }
    else
    {
        player_color = color_player;
    }

    /* Shadow */
    grl_draw_ellipse ((gint)px, (gint)py + 12, 14, 5, color_shadow);

    /* Body */
    grl_draw_circle ((gint)px, (gint)py, 16, player_color);

    /* Facing indicator */
    fx = (gint)(px + cosf (facing) * 20);
    fy = (gint)(py + sinf (facing) * 20);
    grl_draw_circle (fx, fy, 5, color_player);

    /* Attack visual */
    if (self->is_attacking)
    {
        gint ax = (gint)(px + cosf (facing) * 30);
        gint ay = (gint)(py + sinf (facing) * 30);
        grl_draw_circle (ax, ay, 10, color_attack);
    }

    /* Invincibility flash */
    if (self->invincibility > 0.0f && !self->is_dodging)
    {
        gint flash = (gint)(self->invincibility * 20) % 2;
        if (flash)
        {
            GrlColor *flash_color = grl_color_new (255, 255, 255, 150);
            grl_draw_circle ((gint)px, (gint)py, 18, flash_color);
            grl_color_free (flash_color);
        }
    }
}

static void
demo_top_down_draw_hud (LrgGame2DTemplate *template)
{
    DemoTopDown *self = DEMO_TOP_DOWN (template);
    gint health_width, mana_width;
    gint i, slot_x, slot_y;
    gint dialog_x, dialog_y;
    gint map_x, map_y, map_scale, tx, ty;
    gfloat mpx, mpy;
    gchar *stats_str = NULL;
    gchar *quest_str = NULL;
    gchar *room_str = NULL;
    gchar *key_str = NULL;
    GrlColor *item_col;
    GrlColor *flash;
    GrlColor *tile_col;
    Room *room;

    /* Health bar */
    health_width = (gint)((self->health / self->max_health) * 200.0f);
    grl_draw_rectangle (20, 20, 200, 20, color_bar_bg);
    grl_draw_rectangle (20, 20, health_width, 20, color_health_bar);
    grl_draw_text ("HP", 24, 22, 14, color_hud_text);

    /* Mana bar */
    mana_width = (gint)((self->mana / self->max_mana) * 200.0f);
    grl_draw_rectangle (20, 45, 200, 16, color_bar_bg);
    grl_draw_rectangle (20, 45, mana_width, 16, color_mana_bar);
    grl_draw_text ("MP", 24, 47, 12, color_hud_text);

    /* Stats */
    stats_str = g_strdup_printf ("Coins: %d  Keys: %d  Score: %d",
                                 self->coins, self->keys, self->score);
    grl_draw_text (stats_str, 20, 70, 16, color_hud_text);
    g_free (stats_str);

    /* Room indicator */
    room_str = g_strdup_printf ("Room %d", current_room + 1);
    grl_draw_text (room_str, WINDOW_WIDTH - 100, 20, 16, color_hud_text);
    g_free (room_str);

    /* Quest tracker */
    if (self->quest_active)
    {
        quest_str = g_strdup_printf ("Quest: Defeat enemies %d/%d",
                                     self->quest_enemies_killed,
                                     self->quest_enemies_needed);
        grl_draw_rectangle (WINDOW_WIDTH - 220, 50, 200, 25, color_bar_bg);
        grl_draw_text (quest_str, WINDOW_WIDTH - 215, 55, 14, color_hud_text);
        g_free (quest_str);
    }

    /* Inventory slots (1-4) */
    grl_draw_text ("Items:", 20, WINDOW_HEIGHT - 50, 14, color_hud_text);
    for (i = 0; i < 4; i++)
    {
        slot_x = 80 + i * 40;
        slot_y = WINDOW_HEIGHT - 55;

        grl_draw_rectangle (slot_x, slot_y, 32, 32, color_bar_bg);

        if (inventory[i] != ITEM_TYPE_NONE)
        {
            item_col = (inventory[i] == ITEM_TYPE_HEALTH_POTION)
                       ? color_item_health : color_item_mana;
            grl_draw_circle (slot_x + 16, slot_y + 16, 10, item_col);
        }

        key_str = g_strdup_printf ("%d", i + 1);
        grl_draw_text (key_str, slot_x + 12, slot_y - 15, 12, color_hud_text);
        g_free (key_str);
    }

    /* Damage flash */
    if (self->damage_flash > 0.0f)
    {
        flash = grl_color_new (255, 0, 0, (guint8)(self->damage_flash * 150.0f));
        grl_draw_rectangle (0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, flash);
        grl_color_free (flash);
    }

    /* Dialog box */
    if (self->dialog_active && self->current_dialog)
    {
        dialog_x = WINDOW_WIDTH / 2 - 200;
        dialog_y = WINDOW_HEIGHT - 150;

        grl_draw_rectangle (dialog_x, dialog_y, 400, 100, color_dialog_bg);
        grl_draw_rectangle (dialog_x + 2, dialog_y + 2, 396, 96, color_bar_bg);
        grl_draw_text (self->current_dialog, dialog_x + 15, dialog_y + 15, 16, color_hud_text);
        grl_draw_text ("[E] Continue", dialog_x + 150, dialog_y + 75, 14, color_interact);
    }

    /* Minimap */
    if (self->map_open)
    {
        map_x = WINDOW_WIDTH - 150;
        map_y = 100;
        map_scale = 6;

        grl_draw_rectangle (map_x - 5, map_y - 5, ROOM_WIDTH * map_scale + 10,
                           ROOM_HEIGHT * map_scale + 10, color_bar_bg);

        room = &rooms[current_room];
        for (ty = 0; ty < ROOM_HEIGHT; ty++)
        {
            for (tx = 0; tx < ROOM_WIDTH; tx++)
            {
                tile_col = (room->tiles[ty][tx] == TILE_WALL) ? color_wall : color_floor;
                grl_draw_rectangle (map_x + tx * map_scale, map_y + ty * map_scale,
                                   map_scale, map_scale, tile_col);
            }
        }

        /* Player on map */
        mpx = lrg_top_down_template_get_player_x (LRG_TOP_DOWN_TEMPLATE (template));
        mpy = lrg_top_down_template_get_player_y (LRG_TOP_DOWN_TEMPLATE (template));
        grl_draw_circle (map_x + (gint)(mpx / TILE_SIZE * map_scale),
                         map_y + (gint)(mpy / TILE_SIZE * map_scale), 3, color_player);
    }

    /* Controls */
    grl_draw_text ("WASD: Move  E: Interact  LMB/Z: Attack  RMB/X: Magic  Space: Dodge  I: Inventory  Tab: Map",
                   20, WINDOW_HEIGHT - 20, 12, color_hud_text);
}

/* =============================================================================
 * CONFIGURATION
 * ========================================================================== */

static void
demo_top_down_configure (LrgGameTemplate *template)
{
    LrgTopDownTemplate *td = LRG_TOP_DOWN_TEMPLATE (template);

    LRG_GAME_TEMPLATE_CLASS (demo_top_down_parent_class)->configure (template);

    lrg_game_template_set_title (template, "Top-Down RPG Demo - Template System");

    /* Movement settings */
    lrg_top_down_template_set_movement_mode (td, LRG_TOP_DOWN_MOVEMENT_8_DIR);
    lrg_top_down_template_set_move_speed (td, 150.0f);
    lrg_top_down_template_set_acceleration (td, 1500.0f);
    lrg_top_down_template_set_friction (td, 1200.0f);

    /* Player size */
    lrg_top_down_template_set_player_width (td, 24.0f);
    lrg_top_down_template_set_player_height (td, 24.0f);

    /* Interaction */
    lrg_top_down_template_set_interact_radius (td, 50.0f);

    /* Camera */
    lrg_top_down_template_set_look_ahead (td, 40.0f);
    lrg_top_down_template_set_look_ahead_speed (td, 0.1f);

    /* Starting position */
    lrg_top_down_template_set_player_position (td,
        ROOM_WIDTH * TILE_SIZE / 2,
        ROOM_HEIGHT * TILE_SIZE / 2);
}

static void
demo_top_down_post_startup (LrgGameTemplate *template)
{
    LRG_GAME_TEMPLATE_CLASS (demo_top_down_parent_class)->post_startup (template);

    /* Initialize colors */
    color_floor = grl_color_new (50, 50, 55, 255);
    color_wall = grl_color_new (80, 75, 70, 255);
    color_door = grl_color_new (100, 80, 60, 255);
    color_player = grl_color_new (60, 120, 180, 255);
    color_player_dodge = grl_color_new (100, 180, 220, 200);
    color_enemy = grl_color_new (180, 60, 60, 255);
    color_enemy_alert = grl_color_new (220, 80, 40, 255);
    color_npc = grl_color_new (80, 180, 80, 255);
    color_item_health = grl_color_new (220, 60, 60, 255);
    color_item_mana = grl_color_new (60, 100, 220, 255);
    color_item_key = grl_color_new (220, 180, 50, 255);
    color_item_coin = grl_color_new (255, 220, 50, 255);
    color_health_bar = grl_color_new (200, 50, 50, 255);
    color_mana_bar = grl_color_new (50, 100, 200, 255);
    color_bar_bg = grl_color_new (40, 40, 40, 200);
    color_hud_text = grl_color_new (240, 240, 240, 255);
    color_interact = grl_color_new (100, 200, 100, 255);
    color_attack = grl_color_new (255, 200, 100, 200);
    color_magic = grl_color_new (100, 150, 255, 200);
    color_shadow = grl_color_new (0, 0, 0, 80);
    color_dialog_bg = grl_color_new (60, 60, 70, 240);

    /* Initialize world */
    init_world ();
}

static void
demo_top_down_shutdown (LrgGameTemplate *template)
{
    g_clear_pointer (&color_floor, grl_color_free);
    g_clear_pointer (&color_wall, grl_color_free);
    g_clear_pointer (&color_door, grl_color_free);
    g_clear_pointer (&color_player, grl_color_free);
    g_clear_pointer (&color_player_dodge, grl_color_free);
    g_clear_pointer (&color_enemy, grl_color_free);
    g_clear_pointer (&color_enemy_alert, grl_color_free);
    g_clear_pointer (&color_npc, grl_color_free);
    g_clear_pointer (&color_item_health, grl_color_free);
    g_clear_pointer (&color_item_mana, grl_color_free);
    g_clear_pointer (&color_item_key, grl_color_free);
    g_clear_pointer (&color_item_coin, grl_color_free);
    g_clear_pointer (&color_health_bar, grl_color_free);
    g_clear_pointer (&color_mana_bar, grl_color_free);
    g_clear_pointer (&color_bar_bg, grl_color_free);
    g_clear_pointer (&color_hud_text, grl_color_free);
    g_clear_pointer (&color_interact, grl_color_free);
    g_clear_pointer (&color_attack, grl_color_free);
    g_clear_pointer (&color_magic, grl_color_free);
    g_clear_pointer (&color_shadow, grl_color_free);
    g_clear_pointer (&color_dialog_bg, grl_color_free);

    LRG_GAME_TEMPLATE_CLASS (demo_top_down_parent_class)->shutdown (template);
}

/* =============================================================================
 * CLASS INITIALIZATION
 * ========================================================================== */

static void
demo_top_down_class_init (DemoTopDownClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    LrgGame2DTemplateClass *template_2d_class = LRG_GAME_2D_TEMPLATE_CLASS (klass);
    LrgTopDownTemplateClass *td_class = LRG_TOP_DOWN_TEMPLATE_CLASS (klass);

    /* Game template overrides */
    template_class->configure = demo_top_down_configure;
    template_class->post_startup = demo_top_down_post_startup;
    template_class->shutdown = demo_top_down_shutdown;
    template_class->pre_update = demo_top_down_pre_update;

    /* 2D template overrides */
    template_2d_class->draw_world = demo_top_down_draw_world;
    template_2d_class->draw_ui = demo_top_down_draw_hud;

    /* Top-down template overrides */
    td_class->check_collision = demo_top_down_check_collision;
    td_class->on_interact = demo_top_down_on_interact;
    td_class->draw_player = demo_top_down_draw_player;
}

static void
demo_top_down_init (DemoTopDown *self)
{
    self->health = 100.0f;
    self->max_health = 100.0f;
    self->mana = 100.0f;
    self->max_mana = 100.0f;
    self->attack_cooldown = 0.0f;
    self->magic_cooldown = 0.0f;
    self->is_attacking = FALSE;
    self->attack_timer = 0.0f;
    self->magic_effect_timer = 0.0f;
    self->is_dodging = FALSE;
    self->dodge_timer = 0.0f;
    self->dodge_cooldown = 0.0f;
    self->dodge_dir_x = 0.0f;
    self->dodge_dir_y = 0.0f;
    self->damage_flash = 0.0f;
    self->invincibility = 0.0f;
    self->score = 0;
    self->coins = 0;
    self->keys = 0;
    self->enemies_defeated = 0;
    self->play_time = 0.0f;
    self->inventory_open = FALSE;
    self->map_open = FALSE;
    self->dialog_active = FALSE;
    self->current_dialog = NULL;
    self->dialog_timer = 0.0f;
    self->quest_active = FALSE;
    self->quest_enemies_needed = 0;
    self->quest_enemies_killed = 0;
}

/* =============================================================================
 * MAIN
 * ========================================================================== */

int
main (int    argc,
      char **argv)
{
    g_autoptr(DemoTopDown) game = NULL;

    game = g_object_new (DEMO_TYPE_TOP_DOWN, NULL);

    return lrg_game_template_run (LRG_GAME_TEMPLATE (game), argc, argv);
}
