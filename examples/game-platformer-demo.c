/* game-platformer-demo.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A platformer game demonstrating LrgPlatformerTemplate.
 *
 * Features demonstrated:
 * - Subclassing LrgPlatformerTemplate for custom game logic
 * - Gravity and physics-based movement
 * - Jumping with coyote time and jump buffering
 * - Wall slide and wall jump mechanics
 * - Virtual resolution with pixel-perfect scaling
 * - Camera following with deadzone
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

#define VIRTUAL_WIDTH    320
#define VIRTUAL_HEIGHT   180
#define PLAYER_WIDTH     12
#define PLAYER_HEIGHT    20
#define TILE_SIZE        16

#define NUM_PLATFORMS    8
#define NUM_COINS        5

/* =============================================================================
 * COLORS
 * ========================================================================== */

static GrlColor *color_bg = NULL;
static GrlColor *color_ground = NULL;
static GrlColor *color_platform = NULL;
static GrlColor *color_player = NULL;
static GrlColor *color_player_wall = NULL;
static GrlColor *color_coin = NULL;
static GrlColor *color_text = NULL;
static GrlColor *color_text_dim = NULL;

/* =============================================================================
 * LEVEL DATA
 * ========================================================================== */

typedef struct
{
    gint x, y, w, h;
} Platform;

static Platform platforms[NUM_PLATFORMS] = {
    { 0,   160, 320, 20 },   /* Ground */
    { 40,  130, 60,  10 },   /* Platform 1 */
    { 140, 100, 60,  10 },   /* Platform 2 */
    { 220, 70,  60,  10 },   /* Platform 3 */
    { 0,   40,  30,  120 },  /* Left wall */
    { 290, 40,  30,  120 },  /* Right wall */
    { 100, 50,  40,  10 },   /* High platform */
    { 180, 130, 50,  10 },   /* Middle platform */
};

typedef struct
{
    gfloat x, y;
    gboolean collected;
} Coin;

static Coin coins[NUM_COINS] = {
    { 60.0f,  115.0f, FALSE },
    { 160.0f, 85.0f,  FALSE },
    { 240.0f, 55.0f,  FALSE },
    { 110.0f, 35.0f,  FALSE },
    { 195.0f, 115.0f, FALSE },
};

/* =============================================================================
 * CUSTOM PLATFORMER TYPE
 * ========================================================================== */

#define DEMO_TYPE_PLATFORMER (demo_platformer_get_type ())
G_DECLARE_FINAL_TYPE (DemoPlatformer, demo_platformer, DEMO, PLATFORMER, LrgPlatformerTemplate)

struct _DemoPlatformer
{
    LrgPlatformerTemplate parent_instance;

    gint coins_collected;
    gint total_jumps;
    gint wall_jumps;
    gfloat play_time;
    gboolean facing_right;
};

G_DEFINE_FINAL_TYPE (DemoPlatformer, demo_platformer, LRG_TYPE_PLATFORMER_TEMPLATE)

/* =============================================================================
 * COLLISION DETECTION
 * ========================================================================== */

static gboolean
check_platform_collision (gfloat px, gfloat py, gfloat pw, gfloat ph)
{
    gint i;
    Platform *p;

    for (i = 0; i < NUM_PLATFORMS; i++)
    {
        p = &platforms[i];
        if (px < p->x + p->w &&
            px + pw > p->x &&
            py < p->y + p->h &&
            py + ph > p->y)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean
demo_platformer_check_ground (LrgPlatformerTemplate *template)
{
    gfloat px, py;

    lrg_platformer_template_get_player_position (template, &px, &py);

    /* Check if standing on any platform */
    return check_platform_collision (px, py + 1.0f, PLAYER_WIDTH, PLAYER_HEIGHT);
}

static gboolean
demo_platformer_check_wall (LrgPlatformerTemplate *template,
                            gint                   direction)
{
    gfloat px, py;
    gfloat check_x;

    lrg_platformer_template_get_player_position (template, &px, &py);

    check_x = (direction > 0) ? px + 1.0f : px - 1.0f;
    return check_platform_collision (check_x, py, PLAYER_WIDTH, PLAYER_HEIGHT - 4);
}

/* =============================================================================
 * GAME EVENTS
 * ========================================================================== */

static void
demo_platformer_on_jump (LrgPlatformerTemplate *template)
{
    DemoPlatformer *self = DEMO_PLATFORMER (template);
    self->total_jumps++;
}

static void
demo_platformer_on_wall_jump (LrgPlatformerTemplate *template,
                              gint                   direction)
{
    DemoPlatformer *self = DEMO_PLATFORMER (template);
    self->wall_jumps++;
    self->facing_right = (direction > 0);
}

static void
demo_platformer_on_landed (LrgPlatformerTemplate *template)
{
    /* Could play landing sound here */
    (void)template;
}

/* =============================================================================
 * PHYSICS UPDATE
 * ========================================================================== */

static void
demo_platformer_update_physics (LrgPlatformerTemplate *template,
                                gdouble                delta)
{
    DemoPlatformer *self = DEMO_PLATFORMER (template);
    gfloat px, py, vx, vy;
    gfloat new_x, new_y;
    gint i;
    Coin *coin;

    /* Update play time */
    self->play_time += (gfloat)delta;

    /* Get current state */
    lrg_platformer_template_get_player_position (template, &px, &py);
    lrg_platformer_template_get_velocity (template, &vx, &vy);

    /* Update facing direction based on velocity */
    if (vx > 0.1f)
        self->facing_right = TRUE;
    else if (vx < -0.1f)
        self->facing_right = FALSE;

    /* Apply gravity */
    vy += 600.0f * (gfloat)delta;
    if (vy > 400.0f)
        vy = 400.0f;

    /* Calculate new position */
    new_x = px + vx * (gfloat)delta;
    new_y = py + vy * (gfloat)delta;

    /* X collision */
    if (check_platform_collision (new_x, py, PLAYER_WIDTH, PLAYER_HEIGHT))
    {
        vx = 0.0f;
        new_x = px;
    }

    /* Y collision */
    if (check_platform_collision (new_x, new_y, PLAYER_WIDTH, PLAYER_HEIGHT))
    {
        if (vy > 0)
        {
            /* Landing */
            while (!check_platform_collision (new_x, new_y, PLAYER_WIDTH, PLAYER_HEIGHT))
                new_y += 1.0f;
            new_y -= 1.0f;
        }
        vy = 0.0f;
    }

    /* Clamp to world bounds */
    if (new_x < 0) new_x = 0;
    if (new_x > VIRTUAL_WIDTH - PLAYER_WIDTH) new_x = VIRTUAL_WIDTH - PLAYER_WIDTH;
    if (new_y > VIRTUAL_HEIGHT) new_y = 0; /* Respawn at top if fall off */

    /* Apply friction */
    vx *= 0.85f;

    lrg_platformer_template_set_player_position (template, new_x, new_y);
    lrg_platformer_template_set_velocity (template, vx, vy);

    /* Check coin collection */
    for (i = 0; i < NUM_COINS; i++)
    {
        coin = &coins[i];
        if (!coin->collected)
        {
            if (fabsf (new_x + PLAYER_WIDTH / 2 - coin->x) < 10.0f &&
                fabsf (new_y + PLAYER_HEIGHT / 2 - coin->y) < 10.0f)
            {
                coin->collected = TRUE;
                self->coins_collected++;
            }
        }
    }

    /* Update camera to follow player */
    lrg_game_2d_template_set_camera_target (LRG_GAME_2D_TEMPLATE (template),
                                            new_x + PLAYER_WIDTH / 2,
                                            new_y + PLAYER_HEIGHT / 2);
}

/* =============================================================================
 * RENDERING
 * ========================================================================== */

static void
demo_platformer_draw_background (LrgGame2DTemplate *template)
{
    (void)template;

    /* Simple gradient background would go here */
    grl_draw_clear_background (color_bg);
}

static void
demo_platformer_draw_world (LrgGame2DTemplate *template)
{
    DemoPlatformer *self = DEMO_PLATFORMER (template);
    gint i;
    Platform *p;
    Coin *coin;
    gfloat px, py;
    GrlColor *player_color;
    gboolean is_wall_sliding;
    gfloat coin_anim;

    /* Draw platforms */
    for (i = 0; i < NUM_PLATFORMS; i++)
    {
        p = &platforms[i];
        if (i == 0)
            grl_draw_rectangle (p->x, p->y, p->w, p->h, color_ground);
        else
            grl_draw_rectangle (p->x, p->y, p->w, p->h, color_platform);
    }

    /* Draw coins */
    coin_anim = sinf (self->play_time * 4.0f) * 2.0f;
    for (i = 0; i < NUM_COINS; i++)
    {
        coin = &coins[i];
        if (!coin->collected)
        {
            grl_draw_circle ((gint)coin->x, (gint)(coin->y + coin_anim), 6, color_coin);
        }
    }

    /* Draw player */
    lrg_platformer_template_get_player_position (LRG_PLATFORMER_TEMPLATE (self), &px, &py);
    is_wall_sliding = lrg_platformer_template_is_wall_sliding (LRG_PLATFORMER_TEMPLATE (self));
    player_color = is_wall_sliding ? color_player_wall : color_player;

    grl_draw_rectangle ((gint)px, (gint)py, PLAYER_WIDTH, PLAYER_HEIGHT, player_color);

    /* Draw simple face to show direction */
    if (self->facing_right)
    {
        grl_draw_rectangle ((gint)px + 7, (gint)py + 4, 3, 3, color_bg);
    }
    else
    {
        grl_draw_rectangle ((gint)px + 2, (gint)py + 4, 3, 3, color_bg);
    }
}

static void
demo_platformer_draw_ui (LrgGame2DTemplate *template)
{
    DemoPlatformer *self = DEMO_PLATFORMER (template);
    g_autofree gchar *coins_str = NULL;
    g_autofree gchar *time_str = NULL;
    g_autofree gchar *jumps_str = NULL;

    /* Draw HUD */
    coins_str = g_strdup_printf ("Coins: %d/%d", self->coins_collected, NUM_COINS);
    grl_draw_text (coins_str, 8, 4, 8, color_coin);

    time_str = g_strdup_printf ("Time: %.1fs", self->play_time);
    grl_draw_text (time_str, 8, 14, 8, color_text);

    jumps_str = g_strdup_printf ("Jumps: %d (Wall: %d)", self->total_jumps, self->wall_jumps);
    grl_draw_text (jumps_str, 8, 24, 8, color_text_dim);

    /* Win message */
    if (self->coins_collected == NUM_COINS)
    {
        grl_draw_text ("ALL COINS COLLECTED!", VIRTUAL_WIDTH / 2 - 50, VIRTUAL_HEIGHT / 2, 10, color_coin);
    }

    /* Controls */
    grl_draw_text ("Arrow Keys/WASD: Move  Space: Jump  R: Reset", 8, VIRTUAL_HEIGHT - 12, 6, color_text_dim);
}

/* =============================================================================
 * INPUT HANDLING
 * ========================================================================== */

static gboolean
demo_platformer_handle_global_input (LrgGameTemplate *template)
{
    DemoPlatformer *self = DEMO_PLATFORMER (template);
    LrgInputManager *input;
    gint i;

    input = lrg_input_manager_get_default ();

    /* Check for reset with R key */
    if (lrg_input_manager_is_key_pressed (input, GRL_KEY_R))
    {
        /* Reset player */
        lrg_platformer_template_set_player_position (LRG_PLATFORMER_TEMPLATE (self),
                                                      50.0f, 140.0f);
        lrg_platformer_template_set_velocity (LRG_PLATFORMER_TEMPLATE (self), 0.0f, 0.0f);

        /* Reset coins */
        for (i = 0; i < NUM_COINS; i++)
        {
            coins[i].collected = FALSE;
        }

        /* Reset stats */
        self->coins_collected = 0;
        self->total_jumps = 0;
        self->wall_jumps = 0;
        self->play_time = 0.0f;
    }

    return LRG_GAME_TEMPLATE_CLASS (demo_platformer_parent_class)->handle_global_input (template);
}

/* =============================================================================
 * CONFIGURATION
 * ========================================================================== */

static void
demo_platformer_configure (LrgGameTemplate *template)
{
    LrgGame2DTemplate *template_2d = LRG_GAME_2D_TEMPLATE (template);
    LrgPlatformerTemplate *platformer = LRG_PLATFORMER_TEMPLATE (template);

    /* Call parent configure */
    LRG_GAME_TEMPLATE_CLASS (demo_platformer_parent_class)->configure (template);

    /* Window settings */
    lrg_game_template_set_title (template, "Platformer Demo - Template System");

    /* Virtual resolution for pixel art */
    lrg_game_2d_template_set_virtual_resolution (template_2d, VIRTUAL_WIDTH, VIRTUAL_HEIGHT);
    lrg_game_2d_template_set_scaling_mode (template_2d, LRG_SCALING_MODE_LETTERBOX);
    lrg_game_2d_template_set_pixel_perfect (template_2d, TRUE);

    /* Camera setup */
    lrg_game_2d_template_set_camera_smoothing (template_2d, 0.15f);
    lrg_game_2d_template_set_camera_deadzone (template_2d, 40.0f, 30.0f);
    lrg_game_2d_template_set_camera_bounds (template_2d, 0, 0, VIRTUAL_WIDTH, VIRTUAL_HEIGHT);

    /* Platformer physics */
    lrg_platformer_template_set_gravity (platformer, 600.0f);
    lrg_platformer_template_set_jump_height (platformer, 48.0f);
    lrg_platformer_template_set_move_speed (platformer, 100.0f);
    lrg_platformer_template_set_acceleration (platformer, 800.0f);

    /* Jump feel */
    lrg_platformer_template_set_coyote_time (platformer, 0.1f);
    lrg_platformer_template_set_jump_buffer_time (platformer, 0.15f);

    /* Wall mechanics */
    lrg_platformer_template_set_wall_slide_enabled (platformer, TRUE);
    lrg_platformer_template_set_wall_slide_speed (platformer, 50.0f);
    lrg_platformer_template_set_wall_jump_enabled (platformer, TRUE);
    lrg_platformer_template_set_wall_jump_force (platformer, 200.0f, 180.0f);

    /* Starting position */
    lrg_platformer_template_set_player_position (platformer, 50.0f, 140.0f);
}

/* =============================================================================
 * STARTUP
 * ========================================================================== */

static void
demo_platformer_post_startup (LrgGameTemplate *template)
{
    LRG_GAME_TEMPLATE_CLASS (demo_platformer_parent_class)->post_startup (template);

    /* Initialize colors */
    color_bg = grl_color_new (40, 44, 52, 255);
    color_ground = grl_color_new (100, 80, 60, 255);
    color_platform = grl_color_new (80, 120, 80, 255);
    color_player = grl_color_new (100, 150, 220, 255);
    color_player_wall = grl_color_new (150, 100, 220, 255);
    color_coin = grl_color_new (255, 215, 0, 255);
    color_text = grl_color_new (230, 230, 230, 255);
    color_text_dim = grl_color_new (140, 140, 140, 255);
}

static void
demo_platformer_shutdown (LrgGameTemplate *template)
{
    /* Cleanup colors */
    g_clear_pointer (&color_bg, grl_color_free);
    g_clear_pointer (&color_ground, grl_color_free);
    g_clear_pointer (&color_platform, grl_color_free);
    g_clear_pointer (&color_player, grl_color_free);
    g_clear_pointer (&color_player_wall, grl_color_free);
    g_clear_pointer (&color_coin, grl_color_free);
    g_clear_pointer (&color_text, grl_color_free);
    g_clear_pointer (&color_text_dim, grl_color_free);

    LRG_GAME_TEMPLATE_CLASS (demo_platformer_parent_class)->shutdown (template);
}

/* =============================================================================
 * CLASS INITIALIZATION
 * ========================================================================== */

static void
demo_platformer_class_init (DemoPlatformerClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    LrgGame2DTemplateClass *template_2d_class = LRG_GAME_2D_TEMPLATE_CLASS (klass);
    LrgPlatformerTemplateClass *platformer_class = LRG_PLATFORMER_TEMPLATE_CLASS (klass);

    /* Game template overrides */
    template_class->configure = demo_platformer_configure;
    template_class->post_startup = demo_platformer_post_startup;
    template_class->shutdown = demo_platformer_shutdown;
    template_class->handle_global_input = demo_platformer_handle_global_input;

    /* 2D template overrides */
    template_2d_class->draw_background = demo_platformer_draw_background;
    template_2d_class->draw_world = demo_platformer_draw_world;
    template_2d_class->draw_ui = demo_platformer_draw_ui;

    /* Platformer template overrides */
    platformer_class->on_jump = demo_platformer_on_jump;
    platformer_class->on_landed = demo_platformer_on_landed;
    platformer_class->on_wall_jump = demo_platformer_on_wall_jump;
    platformer_class->update_physics = demo_platformer_update_physics;
    platformer_class->check_ground = demo_platformer_check_ground;
    platformer_class->check_wall = demo_platformer_check_wall;
}

static void
demo_platformer_init (DemoPlatformer *self)
{
    self->coins_collected = 0;
    self->total_jumps = 0;
    self->wall_jumps = 0;
    self->play_time = 0.0f;
    self->facing_right = TRUE;
}

/* =============================================================================
 * MAIN
 * ========================================================================== */

int
main (int    argc,
      char **argv)
{
    g_autoptr(DemoPlatformer) game = NULL;

    game = g_object_new (DEMO_TYPE_PLATFORMER, NULL);

    return lrg_game_template_run (LRG_GAME_TEMPLATE (game), argc, argv);
}
