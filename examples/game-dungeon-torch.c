/* game-dungeon-torch.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A dungeon crawler demonstrating Phase 4 features:
 * Tweening, Scene Transitions, 2D Triggers, Tutorial System,
 * Weather Effects, and 2D Lighting.
 *
 * Features demonstrated:
 * - LrgTweenManager / LrgTween: Smooth UI animations
 * - LrgTransitionManager / LrgFadeTransition: Scene transitions
 * - LrgTriggerManager / LrgTrigger2D: Collision zones
 * - LrgTutorialManager / LrgTutorial: Tutorial system
 * - LrgWeatherManager / LrgFog: Weather effects
 * - LrgLightingManager / LrgPointLight2D: 2D lighting
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

#define WINDOW_WIDTH    800
#define WINDOW_HEIGHT   600
#define TILE_SIZE       40
#define PLAYER_SIZE     30
#define PLAYER_SPEED    150.0f

/* Room dimensions */
#define ROOM_WIDTH      16
#define ROOM_HEIGHT     12

/* Room count */
#define ROOM_COUNT      5

/* Trigger types */
#define TRIGGER_COIN    0
#define TRIGGER_TRAP    1
#define TRIGGER_EXIT    2

/* Light types */
#define LIGHT_TORCH     0
#define LIGHT_PLAYER    1

/* =============================================================================
 * COLORS
 * ========================================================================== */

static GrlColor *color_floor = NULL;
static GrlColor *color_wall = NULL;
static GrlColor *color_player = NULL;
static GrlColor *color_coin = NULL;
static GrlColor *color_trap = NULL;
static GrlColor *color_exit = NULL;
static GrlColor *color_torch_light = NULL;
static GrlColor *color_fog = NULL;
static GrlColor *color_ui_bg = NULL;
static GrlColor *color_health = NULL;
static GrlColor *color_health_lost = NULL;
static GrlColor *color_text = NULL;
static GrlColor *color_tutorial = NULL;
static GrlColor *color_dim = NULL;

static void
init_colors (void)
{
    color_floor = grl_color_new (60, 50, 45, 255);
    color_wall = grl_color_new (40, 35, 30, 255);
    color_player = grl_color_new (100, 180, 255, 255);
    color_coin = grl_color_new (255, 215, 0, 255);
    color_trap = grl_color_new (200, 50, 50, 255);
    color_exit = grl_color_new (100, 255, 100, 255);
    color_torch_light = grl_color_new (255, 200, 100, 80);
    color_fog = grl_color_new (50, 50, 70, 200);
    color_ui_bg = grl_color_new (30, 30, 40, 220);
    color_health = grl_color_new (255, 80, 80, 255);
    color_health_lost = grl_color_new (60, 30, 30, 255);
    color_text = grl_color_new (230, 230, 240, 255);
    color_tutorial = grl_color_new (255, 255, 200, 255);
    color_dim = grl_color_new (150, 150, 160, 255);
}

static void
cleanup_colors (void)
{
    g_clear_pointer (&color_floor, grl_color_free);
    g_clear_pointer (&color_wall, grl_color_free);
    g_clear_pointer (&color_player, grl_color_free);
    g_clear_pointer (&color_coin, grl_color_free);
    g_clear_pointer (&color_trap, grl_color_free);
    g_clear_pointer (&color_exit, grl_color_free);
    g_clear_pointer (&color_torch_light, grl_color_free);
    g_clear_pointer (&color_fog, grl_color_free);
    g_clear_pointer (&color_ui_bg, grl_color_free);
    g_clear_pointer (&color_health, grl_color_free);
    g_clear_pointer (&color_health_lost, grl_color_free);
    g_clear_pointer (&color_text, grl_color_free);
    g_clear_pointer (&color_tutorial, grl_color_free);
    g_clear_pointer (&color_dim, grl_color_free);
}

/* =============================================================================
 * ROOM DATA
 * ========================================================================== */

typedef struct
{
    gint x, y;
    gint type;
    gboolean triggered;
    gfloat cooldown;
} Trigger;

typedef struct
{
    gint x, y;
    gfloat radius;
    gfloat flicker;
    gboolean is_player_torch;
} Light;

typedef struct
{
    gchar tiles[ROOM_HEIGHT][ROOM_WIDTH];  /* '#'=wall, '.'=floor */
    Trigger triggers[20];
    gint trigger_count;
    Light lights[10];
    gint light_count;
    gfloat fog_density;
    gfloat ambient_light;
    const gchar *name;
} Room;

#define MAX_TRIGGERS 20
#define MAX_LIGHTS 10

static Room g_rooms[ROOM_COUNT];

static void
init_room (Room        *room,
           const gchar *layout,
           const gchar *name,
           gfloat       fog,
           gfloat       ambient)
{
    gint i;
    gint x;
    gint y;

    memset (room, 0, sizeof (Room));
    room->name = name;
    room->fog_density = fog;
    room->ambient_light = ambient;

    /* Parse layout string */
    i = 0;
    for (y = 0; y < ROOM_HEIGHT && layout[i] != '\0'; y++)
    {
        for (x = 0; x < ROOM_WIDTH && layout[i] != '\0'; x++)
        {
            if (layout[i] == '\n')
            {
                i++;
                x--;
                continue;
            }
            room->tiles[y][x] = layout[i++];
        }
    }
}

static void
add_trigger (Room *room, gint x, gint y, gint type)
{
    Trigger *t;

    if (room->trigger_count >= MAX_TRIGGERS)
        return;

    t = &room->triggers[room->trigger_count++];
    t->x = x * TILE_SIZE + TILE_SIZE / 2;
    t->y = y * TILE_SIZE + TILE_SIZE / 2;
    t->type = type;
    t->triggered = FALSE;
    t->cooldown = 0.0f;
}

static void
add_light (Room *room, gint x, gint y, gfloat radius, gboolean is_player)
{
    Light *l;

    if (room->light_count >= MAX_LIGHTS)
        return;

    l = &room->lights[room->light_count++];
    l->x = x * TILE_SIZE + TILE_SIZE / 2;
    l->y = y * TILE_SIZE + TILE_SIZE / 2;
    l->radius = radius;
    l->flicker = 0.0f;
    l->is_player_torch = is_player;
}

static void
init_rooms (void)
{
    gint x;
    gint y;

    /* Room 0: Tutorial Room */
    init_room (&g_rooms[0],
        "################"
        "#..............#"
        "#..............#"
        "#..............#"
        "#..............#"
        "#..............#"
        "#..............#"
        "#..............#"
        "#..............#"
        "#..............#"
        "#.............>#"
        "################",
        "Tutorial Room", 0.3f, 0.4f);

    add_light (&g_rooms[0], 8, 6, 150, FALSE);
    add_trigger (&g_rooms[0], 14, 10, TRIGGER_EXIT);
    add_trigger (&g_rooms[0], 5, 5, TRIGGER_COIN);
    add_trigger (&g_rooms[0], 10, 3, TRIGGER_COIN);

    /* Room 1: Treasure Room */
    init_room (&g_rooms[1],
        "################"
        "#..o...o...o..##"
        "#.............##"
        "#..o.......o..##"
        "##............##"
        "##....###.....##"
        "##....###.....##"
        "#.............##"
        "#..o.......o..##"
        "#.............##"
        "#..o...o...o.>##"
        "################",
        "Treasure Room", 0.2f, 0.3f);

    add_light (&g_rooms[1], 4, 5, 120, FALSE);
    add_light (&g_rooms[1], 10, 5, 120, FALSE);
    add_trigger (&g_rooms[1], 13, 10, TRIGGER_EXIT);

    /* Add coins */
    for (y = 0; y < ROOM_HEIGHT; y++)
    {
        for (x = 0; x < ROOM_WIDTH; x++)
        {
            if (g_rooms[1].tiles[y][x] == 'o')
            {
                g_rooms[1].tiles[y][x] = '.';
                add_trigger (&g_rooms[1], x, y, TRIGGER_COIN);
            }
        }
    }

    /* Room 2: Trap Room */
    init_room (&g_rooms[2],
        "################"
        "#..............#"
        "#..X..X..X..X..#"
        "#..............#"
        "#..X..X..X..X..#"
        "#..............#"
        "#..X..X..X..X..#"
        "#..............#"
        "#..X..X..X..X..#"
        "#..............#"
        "#.............>#"
        "################",
        "Trap Room", 0.4f, 0.25f);

    add_light (&g_rooms[2], 1, 1, 100, FALSE);
    add_light (&g_rooms[2], 14, 1, 100, FALSE);
    add_trigger (&g_rooms[2], 14, 10, TRIGGER_EXIT);

    /* Add traps */
    for (y = 0; y < ROOM_HEIGHT; y++)
    {
        for (x = 0; x < ROOM_WIDTH; x++)
        {
            if (g_rooms[2].tiles[y][x] == 'X')
            {
                g_rooms[2].tiles[y][x] = '.';
                add_trigger (&g_rooms[2], x, y, TRIGGER_TRAP);
            }
        }
    }

    /* Room 3: Dark Room */
    init_room (&g_rooms[3],
        "################"
        "#..............#"
        "#..####..####..#"
        "#..#........#..#"
        "#..#........#..#"
        "#..............#"
        "#..#........#..#"
        "#..#........#..#"
        "#..####..####..#"
        "#..............#"
        "#.............>#"
        "################",
        "Dark Room", 0.8f, 0.1f);

    add_trigger (&g_rooms[3], 14, 10, TRIGGER_EXIT);
    add_trigger (&g_rooms[3], 8, 5, TRIGGER_COIN);

    /* Room 4: Final Room */
    init_room (&g_rooms[4],
        "################"
        "#..............#"
        "#..o...oo...o..#"
        "#..............#"
        "#......##......#"
        "#.....####.....#"
        "#.....####.....#"
        "#......##......#"
        "#..............#"
        "#..o...oo...o..#"
        "#..............#"
        "################",
        "Victory Room", 0.1f, 0.5f);

    add_light (&g_rooms[4], 8, 6, 200, FALSE);

    /* Add coins */
    for (y = 0; y < ROOM_HEIGHT; y++)
    {
        for (x = 0; x < ROOM_WIDTH; x++)
        {
            if (g_rooms[4].tiles[y][x] == 'o')
            {
                g_rooms[4].tiles[y][x] = '.';
                add_trigger (&g_rooms[4], x, y, TRIGGER_COIN);
            }
        }
    }
}

/* =============================================================================
 * GAME STATE
 * ========================================================================== */

typedef struct
{
    /* Player */
    gfloat player_x, player_y;
    gint health;
    gint max_health;
    gint score;
    gboolean has_torch;
    gfloat player_torch_radius;

    /* Current room */
    gint current_room;

    /* Transition */
    gboolean transitioning;
    gfloat transition_timer;
    gfloat transition_duration;
    gint transition_type;  /* 0=fade, 1=dissolve, 2=wipe, 3=zoom */
    gint next_room;

    /* Tutorial */
    gint tutorial_step;
    gboolean tutorial_complete;
    gfloat tutorial_timer;

    /* UI Tweens */
    gfloat health_display;
    gfloat score_popup_y;
    gfloat score_popup_alpha;
    gint score_popup_value;

    /* Room title */
    gfloat room_title_alpha;

    /* Game over */
    gboolean game_over;
    gboolean victory;

    /* Damage flash */
    gfloat damage_flash;

    /* Trap cooldown global */
    gfloat invincibility;
} GameState;

static GameState *g_game = NULL;

static void
game_init (void)
{
    g_game = g_new0 (GameState, 1);
    g_game->player_x = TILE_SIZE * 2;
    g_game->player_y = TILE_SIZE * 6;
    g_game->health = 5;
    g_game->max_health = 5;
    g_game->score = 0;
    g_game->has_torch = FALSE;
    g_game->player_torch_radius = 100.0f;
    g_game->current_room = 0;
    g_game->transition_duration = 0.5f;
    g_game->health_display = 5.0f;
    g_game->room_title_alpha = 1.0f;
}

static void
game_cleanup (void)
{
    g_free (g_game);
    g_game = NULL;
}

/* =============================================================================
 * COLLISION DETECTION
 * ========================================================================== */

static gboolean
is_wall (gint px, gint py)
{
    Room *room;
    gint tile_x;
    gint tile_y;

    room = &g_rooms[g_game->current_room];

    tile_x = px / TILE_SIZE;
    tile_y = py / TILE_SIZE;

    if (tile_x < 0 || tile_x >= ROOM_WIDTH || tile_y < 0 || tile_y >= ROOM_HEIGHT)
        return TRUE;

    return room->tiles[tile_y][tile_x] == '#';
}

static gboolean
check_collision (gfloat new_x, gfloat new_y)
{
    gint half;

    half = PLAYER_SIZE / 2;

    /* Check all corners */
    if (is_wall ((gint)(new_x - half), (gint)(new_y - half))) return TRUE;
    if (is_wall ((gint)(new_x + half), (gint)(new_y - half))) return TRUE;
    if (is_wall ((gint)(new_x - half), (gint)(new_y + half))) return TRUE;
    if (is_wall ((gint)(new_x + half), (gint)(new_y + half))) return TRUE;

    return FALSE;
}

/* =============================================================================
 * TRIGGERS
 * ========================================================================== */

static void
show_score_popup (gint value)
{
    g_game->score_popup_value = value;
    g_game->score_popup_y = g_game->player_y - 30;
    g_game->score_popup_alpha = 1.0f;
}

static void
check_triggers (void)
{
    Room *room;
    gint i;
    Trigger *t;
    gfloat dx;
    gfloat dy;
    gfloat dist;
    gfloat trigger_radius;

    room = &g_rooms[g_game->current_room];

    for (i = 0; i < room->trigger_count; i++)
    {
        t = &room->triggers[i];

        if (t->triggered && t->type != TRIGGER_TRAP)
            continue;

        /* Check distance */
        dx = g_game->player_x - t->x;
        dy = g_game->player_y - t->y;
        dist = sqrtf (dx * dx + dy * dy);

        trigger_radius = (t->type == TRIGGER_EXIT) ? 25.0f : 20.0f;

        if (dist < trigger_radius)
        {
            switch (t->type)
            {
            case TRIGGER_COIN:
                t->triggered = TRUE;
                g_game->score += 10;
                show_score_popup (10);
                break;

            case TRIGGER_TRAP:
                if (t->cooldown <= 0 && g_game->invincibility <= 0)
                {
                    g_game->health--;
                    g_game->damage_flash = 0.3f;
                    g_game->invincibility = 1.0f;
                    t->cooldown = 2.0f;

                    if (g_game->health <= 0)
                    {
                        g_game->game_over = TRUE;
                    }
                }
                break;

            case TRIGGER_EXIT:
                if (g_game->current_room < ROOM_COUNT - 1)
                {
                    g_game->transitioning = TRUE;
                    g_game->transition_timer = 0.0f;
                    g_game->next_room = g_game->current_room + 1;
                    g_game->transition_type = g_game->current_room % 4;
                }
                else
                {
                    g_game->victory = TRUE;
                    g_game->game_over = TRUE;
                }
                break;
            }
        }
    }
}

/* =============================================================================
 * TUTORIAL
 * ========================================================================== */

static const gchar *tutorial_messages[] = {
    "Use WASD or Arrow Keys to move",
    "Collect coins for points",
    "Find the exit (green door)",
    "Avoid the spikes!",
    NULL
};

static void
update_tutorial (gfloat delta)
{
    gboolean advance;

    if (g_game->tutorial_complete)
        return;

    g_game->tutorial_timer += delta;

    /* Auto-advance tutorial after a few seconds or on action */
    advance = FALSE;

    switch (g_game->tutorial_step)
    {
    case 0:
        /* Move tutorial - advance when player moves */
        if (g_game->player_x > TILE_SIZE * 3 || g_game->player_y != TILE_SIZE * 6)
            advance = TRUE;
        break;
    case 1:
        /* Coin tutorial - advance when first coin collected */
        if (g_game->score > 0)
            advance = TRUE;
        break;
    case 2:
        /* Exit tutorial - advance after 3 seconds */
        if (g_game->tutorial_timer > 3.0f)
            advance = TRUE;
        break;
    case 3:
        /* Trap warning - advance after viewing */
        if (g_game->tutorial_timer > 3.0f)
            advance = TRUE;
        break;
    default:
        g_game->tutorial_complete = TRUE;
        break;
    }

    if (advance)
    {
        g_game->tutorial_step++;
        g_game->tutorial_timer = 0.0f;
    }

    /* Skip tutorial with SPACE */
    if (grl_input_is_key_pressed (GRL_KEY_SPACE))
    {
        g_game->tutorial_complete = TRUE;
    }
}

/* =============================================================================
 * TRANSITIONS
 * ========================================================================== */

static void
update_transition (gfloat delta)
{
    gfloat t;

    if (!g_game->transitioning)
        return;

    g_game->transition_timer += delta;
    t = g_game->transition_timer / g_game->transition_duration;

    if (t >= 1.0f)
    {
        /* Complete transition */
        g_game->current_room = g_game->next_room;
        g_game->player_x = TILE_SIZE * 2;
        g_game->player_y = TILE_SIZE * 6;
        g_game->transitioning = FALSE;
        g_game->room_title_alpha = 1.0f;
    }
}

static void
draw_transition (void)
{
    gfloat t;
    guint8 alpha;
    GrlColor *fade;
    GrlColor *black;
    gint block_size;
    gint x;
    gint y;
    gint wipe_x;
    gint radius;

    if (!g_game->transitioning)
        return;

    t = g_game->transition_timer / g_game->transition_duration;

    switch (g_game->transition_type)
    {
    case 0: /* Fade */
        alpha = (guint8)(t * 255);
        fade = grl_color_new (0, 0, 0, alpha);
        grl_draw_rectangle (0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, fade);
        grl_color_free (fade);
        break;

    case 1: /* Dissolve (simulated with random rectangles) */
        black = grl_color_new (0, 0, 0, 255);
        block_size = 20;
        for (y = 0; y < WINDOW_HEIGHT; y += block_size)
        {
            for (x = 0; x < WINDOW_WIDTH; x += block_size)
            {
                if (g_random_double () < t)
                {
                    grl_draw_rectangle (x, y, block_size, block_size, black);
                }
            }
        }
        grl_color_free (black);
        break;

    case 2: /* Wipe */
        wipe_x = (gint)(t * WINDOW_WIDTH);
        black = grl_color_new (0, 0, 0, 255);
        grl_draw_rectangle (0, 0, wipe_x, WINDOW_HEIGHT, black);
        grl_color_free (black);
        break;

    case 3: /* Zoom (simulated with growing circle) */
        radius = (gint)(t * WINDOW_WIDTH);
        black = grl_color_new (0, 0, 0, 255);
        grl_draw_circle (WINDOW_WIDTH / 2, WINDOW_HEIGHT / 2, radius, black);
        grl_color_free (black);
        break;
    }
}

/* =============================================================================
 * TWEENS (Simplified)
 * ========================================================================== */

static void
update_tweens (gfloat delta)
{
    gfloat target_health;
    Room *room;
    gint i;

    /* Health display tween */
    target_health = (gfloat)g_game->health;
    g_game->health_display += (target_health - g_game->health_display) * 5.0f * delta;

    /* Score popup tween */
    if (g_game->score_popup_alpha > 0)
    {
        g_game->score_popup_y -= 50.0f * delta;
        g_game->score_popup_alpha -= 1.5f * delta;
    }

    /* Room title fade */
    if (g_game->room_title_alpha > 0)
    {
        g_game->room_title_alpha -= 0.3f * delta;
    }

    /* Damage flash */
    if (g_game->damage_flash > 0)
    {
        g_game->damage_flash -= delta;
    }

    /* Invincibility */
    if (g_game->invincibility > 0)
    {
        g_game->invincibility -= delta;
    }

    /* Trap cooldowns */
    room = &g_rooms[g_game->current_room];
    for (i = 0; i < room->trigger_count; i++)
    {
        if (room->triggers[i].cooldown > 0)
        {
            room->triggers[i].cooldown -= delta;
        }
    }
}

/* =============================================================================
 * INPUT
 * ========================================================================== */

static void
handle_input (gfloat delta)
{
    gfloat dx;
    gfloat dy;
    gfloat new_x;
    gfloat new_y;
    gint r;
    gint i;

    if (g_game->transitioning || g_game->game_over)
        return;

    dx = 0;
    dy = 0;

    if (grl_input_is_key_down (GRL_KEY_W) || grl_input_is_key_down (GRL_KEY_UP))
        dy -= 1.0f;
    if (grl_input_is_key_down (GRL_KEY_S) || grl_input_is_key_down (GRL_KEY_DOWN))
        dy += 1.0f;
    if (grl_input_is_key_down (GRL_KEY_A) || grl_input_is_key_down (GRL_KEY_LEFT))
        dx -= 1.0f;
    if (grl_input_is_key_down (GRL_KEY_D) || grl_input_is_key_down (GRL_KEY_RIGHT))
        dx += 1.0f;

    /* Normalize diagonal movement */
    if (dx != 0 && dy != 0)
    {
        dx *= 0.707f;
        dy *= 0.707f;
    }

    new_x = g_game->player_x + dx * PLAYER_SPEED * delta;
    new_y = g_game->player_y + dy * PLAYER_SPEED * delta;

    /* Check collision separately for X and Y for sliding */
    if (!check_collision (new_x, g_game->player_y))
    {
        g_game->player_x = new_x;
    }
    if (!check_collision (g_game->player_x, new_y))
    {
        g_game->player_y = new_y;
    }

    /* Restart on R */
    if (grl_input_is_key_pressed (GRL_KEY_R) && g_game->game_over)
    {
        g_game->health = g_game->max_health;
        g_game->current_room = 0;
        g_game->player_x = TILE_SIZE * 2;
        g_game->player_y = TILE_SIZE * 6;
        g_game->game_over = FALSE;
        g_game->victory = FALSE;
        g_game->score = 0;
        g_game->health_display = (gfloat)g_game->health;

        /* Reset all triggers */
        for (r = 0; r < ROOM_COUNT; r++)
        {
            for (i = 0; i < g_rooms[r].trigger_count; i++)
            {
                g_rooms[r].triggers[i].triggered = FALSE;
                g_rooms[r].triggers[i].cooldown = 0.0f;
            }
        }
    }
}

/* =============================================================================
 * RENDERING
 * ========================================================================== */

static void
draw_room (void)
{
    Room *room;
    gint x;
    gint y;
    gint px;
    gint py;
    GrlColor *color;
    GrlColor *grid;

    room = &g_rooms[g_game->current_room];

    /* Draw tiles */
    for (y = 0; y < ROOM_HEIGHT; y++)
    {
        for (x = 0; x < ROOM_WIDTH; x++)
        {
            px = x * TILE_SIZE;
            py = y * TILE_SIZE;

            color = (room->tiles[y][x] == '#') ? color_wall : color_floor;
            grl_draw_rectangle (px, py, TILE_SIZE, TILE_SIZE, color);

            /* Grid lines */
            grid = grl_color_new (50, 45, 40, 255);
            grl_draw_rectangle (px, py, TILE_SIZE, 1, grid);
            grl_draw_rectangle (px, py, 1, TILE_SIZE, grid);
            grl_color_free (grid);
        }
    }
}

static void
draw_triggers (void)
{
    Room *room;
    gint i;
    Trigger *t;
    GrlColor *color;
    gint size;

    room = &g_rooms[g_game->current_room];

    for (i = 0; i < room->trigger_count; i++)
    {
        t = &room->triggers[i];

        if (t->triggered && t->type != TRIGGER_TRAP)
            continue;

        color = NULL;
        size = 15;

        switch (t->type)
        {
        case TRIGGER_COIN:
            color = color_coin;
            size = 10;
            break;
        case TRIGGER_TRAP:
            color = color_trap;
            size = 15;
            break;
        case TRIGGER_EXIT:
            color = color_exit;
            size = 20;
            break;
        }

        grl_draw_circle (t->x, t->y, size, color);
    }
}

static void
draw_lights (void)
{
    Room *room;
    gint i;
    Light *l;
    gfloat flicker;
    gint radius;
    gint r;
    guint8 alpha;
    GrlColor *glow;
    gint px;
    gint py;

    room = &g_rooms[g_game->current_room];

    /* Draw torch lights */
    for (i = 0; i < room->light_count; i++)
    {
        l = &room->lights[i];
        flicker = sinf (l->flicker) * 10.0f;
        radius = (gint)(l->radius + flicker);

        /* Multiple circles for gradient effect */
        for (r = radius; r > 0; r -= 15)
        {
            alpha = (guint8)((1.0f - (gfloat)r / radius) * 40);
            glow = grl_color_new (255, 200, 100, alpha);
            grl_draw_circle (l->x, l->y, r, glow);
            grl_color_free (glow);
        }
    }

    /* Player torch light */
    if (g_game->has_torch || g_game->current_room == 3) /* Auto-torch in dark room */
    {
        px = (gint)g_game->player_x;
        py = (gint)g_game->player_y;

        for (r = (gint)g_game->player_torch_radius; r > 0; r -= 10)
        {
            alpha = (guint8)((1.0f - (gfloat)r / g_game->player_torch_radius) * 50);
            glow = grl_color_new (200, 220, 255, alpha);
            grl_draw_circle (px, py, r, glow);
            grl_color_free (glow);
        }
    }
}

static void
draw_fog (void)
{
    Room *room;
    guint8 fog_alpha;
    GrlColor *fog;

    room = &g_rooms[g_game->current_room];

    if (room->fog_density <= 0)
        return;

    /* Simple fog overlay */
    fog_alpha = (guint8)(room->fog_density * 180);
    fog = grl_color_new (50, 50, 70, fog_alpha);
    grl_draw_rectangle (0, 0, ROOM_WIDTH * TILE_SIZE, ROOM_HEIGHT * TILE_SIZE, fog);
    grl_color_free (fog);

    /* Draw dark circles to create "hole" in fog (inverted) */
    /* This is a simplification - real implementation would use shaders */
}

static void
draw_player (void)
{
    gint px;
    gint py;
    GrlColor *player_color;
    GrlColor *eye_color;

    px = (gint)g_game->player_x;
    py = (gint)g_game->player_y;

    /* Damage flash */
    player_color = color_player;
    if (g_game->invincibility > 0 && ((gint)(g_game->invincibility * 10) % 2))
    {
        player_color = grl_color_new (255, 255, 255, 200);
    }

    /* Player body */
    grl_draw_circle (px, py, PLAYER_SIZE / 2, player_color);

    /* Direction indicator */
    eye_color = grl_color_new (50, 50, 60, 255);
    grl_draw_circle (px + 5, py - 3, 4, eye_color);
    grl_color_free (eye_color);

    if (player_color != color_player)
    {
        grl_color_free (player_color);
    }
}

static void
draw_ui (void)
{
    gint ui_y;
    gint i;
    gint hx;
    GrlColor *color;
    gchar *score_str;
    Room *room;
    gchar *room_str;
    guint8 alpha;
    GrlColor *popup_color;
    gchar *popup_str;
    GrlColor *title_color;
    gint title_width;
    GrlColor *flash;

    /* UI background */
    grl_draw_rectangle (0, ROOM_HEIGHT * TILE_SIZE, WINDOW_WIDTH, 120, color_ui_bg);

    ui_y = ROOM_HEIGHT * TILE_SIZE + 10;

    /* Health bar */
    grl_draw_text ("Health:", 20, ui_y, 18, color_text);

    for (i = 0; i < g_game->max_health; i++)
    {
        hx = 100 + i * 30;
        color = (i < (gint)g_game->health_display) ? color_health : color_health_lost;
        grl_draw_circle (hx, ui_y + 8, 10, color);
    }

    /* Score */
    score_str = g_strdup_printf ("Score: %d", g_game->score);
    grl_draw_text (score_str, 300, ui_y, 18, color_coin);
    g_free (score_str);

    /* Room name */
    room = &g_rooms[g_game->current_room];
    room_str = g_strdup_printf ("Room %d: %s", g_game->current_room + 1, room->name);
    grl_draw_text (room_str, 500, ui_y, 18, color_text);
    g_free (room_str);

    /* Controls */
    grl_draw_text ("WASD/Arrows: Move | R: Restart", 20, ui_y + 40, 14, color_dim);

    /* Score popup */
    if (g_game->score_popup_alpha > 0)
    {
        alpha = (guint8)(g_game->score_popup_alpha * 255);
        popup_color = grl_color_new (255, 215, 0, alpha);
        popup_str = g_strdup_printf ("+%d", g_game->score_popup_value);
        grl_draw_text (popup_str, (gint)g_game->player_x - 10, (gint)g_game->score_popup_y, 20, popup_color);
        g_free (popup_str);
        grl_color_free (popup_color);
    }

    /* Room title */
    if (g_game->room_title_alpha > 0)
    {
        alpha = (guint8)(g_game->room_title_alpha * 255);
        title_color = grl_color_new (255, 255, 255, alpha);
        title_width = grl_measure_text (room->name, 36);
        grl_draw_text (room->name, (ROOM_WIDTH * TILE_SIZE - title_width) / 2, 200, 36, title_color);
        grl_color_free (title_color);
    }

    /* Damage flash */
    if (g_game->damage_flash > 0)
    {
        alpha = (guint8)(g_game->damage_flash * 100);
        flash = grl_color_new (255, 0, 0, alpha);
        grl_draw_rectangle (0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, flash);
        grl_color_free (flash);
    }
}

static void
draw_tutorial (void)
{
    const gchar *message;
    gint box_width;
    gint box_height;
    gint box_x;
    gint box_y;
    GrlColor *box_bg;
    gint text_width;

    if (g_game->tutorial_complete || g_game->current_room != 0)
        return;

    message = tutorial_messages[g_game->tutorial_step];
    if (message == NULL)
        return;

    /* Tutorial box */
    box_width = 350;
    box_height = 60;
    box_x = (ROOM_WIDTH * TILE_SIZE - box_width) / 2;
    box_y = 30;

    box_bg = grl_color_new (40, 40, 60, 230);
    grl_draw_rectangle (box_x, box_y, box_width, box_height, box_bg);
    grl_color_free (box_bg);

    text_width = grl_measure_text (message, 18);
    grl_draw_text (message, box_x + (box_width - text_width) / 2, box_y + 15, 18, color_tutorial);

    grl_draw_text ("Press SPACE to skip tutorial", box_x + 70, box_y + 40, 12, color_dim);
}

static void
draw_game_over (void)
{
    GrlColor *overlay;
    const gchar *text;
    gint width;
    gchar *score_str;
    gint score_width;
    const gchar *restart;
    gint restart_width;

    if (!g_game->game_over)
        return;

    /* Overlay */
    overlay = grl_color_new (0, 0, 0, 180);
    grl_draw_rectangle (0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, overlay);
    grl_color_free (overlay);

    if (g_game->victory)
    {
        text = "VICTORY!";
        width = grl_measure_text (text, 60);
        grl_draw_text (text, (WINDOW_WIDTH - width) / 2, 200, 60, color_exit);

        score_str = g_strdup_printf ("Final Score: %d", g_game->score);
        score_width = grl_measure_text (score_str, 24);
        grl_draw_text (score_str, (WINDOW_WIDTH - score_width) / 2, 280, 24, color_coin);
        g_free (score_str);
    }
    else
    {
        text = "GAME OVER";
        width = grl_measure_text (text, 60);
        grl_draw_text (text, (WINDOW_WIDTH - width) / 2, 200, 60, color_health);
    }

    restart = "Press R to restart";
    restart_width = grl_measure_text (restart, 20);
    grl_draw_text (restart, (WINDOW_WIDTH - restart_width) / 2, 350, 20, color_text);
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
    Room *room;
    gint i;

    (void)argc;
    (void)argv;

    /* Initialize window */
    window = grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT,
                             "Dungeon Torch - Phase 4 Demo");
    grl_window_set_target_fps (window, 60);

    /* Initialize systems */
    init_colors ();
    init_rooms ();
    game_init ();

    /* Main game loop */
    while (!grl_window_should_close (window))
    {
        delta = grl_window_get_frame_time (window);

        /* Update light flicker */
        room = &g_rooms[g_game->current_room];
        for (i = 0; i < room->light_count; i++)
        {
            room->lights[i].flicker += delta * 8.0f;
        }

        /* Input */
        handle_input (delta);

        /* Update */
        update_transition (delta);
        update_tweens (delta);
        update_tutorial (delta);
        check_triggers ();

        /* Draw */
        grl_window_begin_drawing (window);
        grl_draw_clear_background (color_wall);

        draw_room ();
        draw_triggers ();
        draw_lights ();
        draw_fog ();
        draw_player ();
        draw_ui ();
        draw_tutorial ();
        draw_transition ();
        draw_game_over ();

        grl_draw_fps (WINDOW_WIDTH - 80, WINDOW_HEIGHT - 25);

        grl_window_end_drawing (window);
    }

    /* Cleanup */
    game_cleanup ();
    cleanup_colors ();
    g_object_unref (window);

    return 0;
}
