# Platformer Example

This example demonstrates creating a 2D platformer using `LrgPlatformerTemplate`. It includes gravity, jumping with coyote time and input buffering, wall sliding, and basic level collision.

## Complete Code

```c
/* platformer.c - 2D platformer with wall mechanics */

#include <libregnum.h>
#include <math.h>

/* ==========================================================================
 * Level Definition
 * ========================================================================== */

/* Simple tile-based level (1 = solid, 0 = empty) */
static const gint LEVEL_WIDTH = 40;
static const gint LEVEL_HEIGHT = 22;
static const gint TILE_SIZE = 32;

static const gchar LEVEL_DATA[] =
    "1111111111111111111111111111111111111111"
    "1000000000000000000000000000000000000001"
    "1000000000000000000000000000000000000001"
    "1000000000000000000000000000000000000001"
    "1000000000000000000000000000000000000001"
    "1000000000000000000111110000000000000001"
    "1000000000000000000000000000000000000001"
    "1000000000000000000000000000000000000001"
    "100000000011111100000000000000111100001"
    "1000000000000000000000000000000000000001"
    "1000000000000000000000000000000000000001"
    "1001111000000000000000000000011111000001"
    "1000000000000000011100000000000000000001"
    "1000000000000000000000000000000000000001"
    "1000000000000000000000001111110000000001"
    "1000000111110000000000000000000000000001"
    "1000000000000000000000000000000000000001"
    "1000000000000001111111111000000000111111"
    "1000000000000000000000000000000000000001"
    "1000000000000000000000000000000011110001"
    "1000000000000000000000000000000000000001"
    "1111111111111111111111111111111111111111";

/* ==========================================================================
 * Type Declaration
 * ========================================================================== */

#define MY_TYPE_PLATFORMER (my_platformer_get_type ())
G_DECLARE_FINAL_TYPE (MyPlatformer, my_platformer, MY, PLATFORMER, LrgPlatformerTemplate)

struct _MyPlatformer
{
    LrgPlatformerTemplate parent_instance;

    /* Player state */
    gfloat player_x;
    gfloat player_y;
    gfloat velocity_x;
    gfloat velocity_y;

    /* Movement parameters */
    gfloat move_speed;
    gfloat jump_force;
    gfloat gravity;
    gfloat max_fall_speed;

    /* Wall mechanics */
    gfloat wall_slide_speed;
    gfloat wall_jump_force_x;
    gfloat wall_jump_force_y;

    /* State flags */
    gboolean on_ground;
    gboolean on_wall_left;
    gboolean on_wall_right;
    gboolean facing_right;

    /* Timers (in frames) */
    gint coyote_timer;
    gint wall_coyote_timer;
    gint jump_buffer_timer;
};

G_DEFINE_TYPE (MyPlatformer, my_platformer, LRG_TYPE_PLATFORMER_TEMPLATE)

/* ==========================================================================
 * Collision Detection
 * ========================================================================== */

static gboolean
is_solid (gint tile_x, gint tile_y)
{
    if (tile_x < 0 || tile_x >= LEVEL_WIDTH ||
        tile_y < 0 || tile_y >= LEVEL_HEIGHT)
        return TRUE;  /* Out of bounds = solid */

    return LEVEL_DATA[tile_y * LEVEL_WIDTH + tile_x] == '1';
}

static gboolean
check_collision_at (gfloat x, gfloat y, gfloat width, gfloat height)
{
    /* Check all tiles the player hitbox overlaps */
    gint left = (gint)(x / TILE_SIZE);
    gint right = (gint)((x + width - 1) / TILE_SIZE);
    gint top = (gint)(y / TILE_SIZE);
    gint bottom = (gint)((y + height - 1) / TILE_SIZE);

    for (gint ty = top; ty <= bottom; ty++)
    {
        for (gint tx = left; tx <= right; tx++)
        {
            if (is_solid (tx, ty))
                return TRUE;
        }
    }
    return FALSE;
}

/* ==========================================================================
 * Movement and Physics
 * ========================================================================== */

static void
move_x (MyPlatformer *self, gfloat amount)
{
    gfloat new_x = self->player_x + amount;

    if (!check_collision_at (new_x, self->player_y, 24, 32))
    {
        self->player_x = new_x;
    }
    else
    {
        /* Slide against wall */
        gfloat step = (amount > 0) ? 1.0f : -1.0f;
        while (fabsf (amount) > 0)
        {
            if (!check_collision_at (self->player_x + step, self->player_y, 24, 32))
            {
                self->player_x += step;
                amount -= step;
            }
            else
            {
                self->velocity_x = 0;
                break;
            }
        }
    }
}

static void
move_y (MyPlatformer *self, gfloat amount)
{
    gfloat new_y = self->player_y + amount;

    if (!check_collision_at (self->player_x, new_y, 24, 32))
    {
        self->player_y = new_y;
    }
    else
    {
        /* Slide against floor/ceiling */
        gfloat step = (amount > 0) ? 1.0f : -1.0f;
        while (fabsf (amount) > 0)
        {
            if (!check_collision_at (self->player_x, self->player_y + step, 24, 32))
            {
                self->player_y += step;
                amount -= step;
            }
            else
            {
                if (amount > 0)  /* Landed on ground */
                    self->on_ground = TRUE;
                self->velocity_y = 0;
                break;
            }
        }
    }
}

static void
check_wall_contact (MyPlatformer *self)
{
    /* Check for wall on left */
    self->on_wall_left = check_collision_at (self->player_x - 1, self->player_y + 4, 1, 24);

    /* Check for wall on right */
    self->on_wall_right = check_collision_at (self->player_x + 24, self->player_y + 4, 1, 24);
}

static void
do_jump (MyPlatformer *self)
{
    self->velocity_y = -self->jump_force;
    self->on_ground = FALSE;
    self->coyote_timer = 0;

    /* Screen shake for impact */
    lrg_game_template_shake (LRG_GAME_TEMPLATE (self), 0.1f);
}

static void
do_wall_jump (MyPlatformer *self, gboolean from_left)
{
    self->velocity_y = -self->wall_jump_force_y;
    self->velocity_x = from_left ? self->wall_jump_force_x : -self->wall_jump_force_x;
    self->wall_coyote_timer = 0;
    self->facing_right = from_left;

    /* Screen shake */
    lrg_game_template_shake (LRG_GAME_TEMPLATE (self), 0.15f);
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

static void
my_platformer_configure (LrgGameTemplate *template)
{
    MyPlatformer *self = MY_PLATFORMER (template);
    LrgPlatformerTemplate *platformer = LRG_PLATFORMER_TEMPLATE (template);

    g_object_set (template,
                  "title", "Platformer Demo",
                  "window-width", LEVEL_WIDTH * TILE_SIZE,
                  "window-height", LEVEL_HEIGHT * TILE_SIZE,
                  NULL);

    /* Configure 2D settings */
    LrgGame2DTemplate *template_2d = LRG_GAME_2D_TEMPLATE (template);
    lrg_game_2d_template_set_virtual_resolution (template_2d,
        LEVEL_WIDTH * TILE_SIZE, LEVEL_HEIGHT * TILE_SIZE);

    /* Configure platformer template settings */
    lrg_platformer_template_set_gravity (platformer, 1800.0f);
    lrg_platformer_template_set_max_fall_speed (platformer, 600.0f);
    lrg_platformer_template_set_jump_buffer_frames (platformer, 6);
    lrg_platformer_template_set_coyote_frames (platformer, 6);

    /* Player starting position */
    self->player_x = 100.0f;
    self->player_y = 500.0f;
    self->facing_right = TRUE;

    /* Movement parameters */
    self->move_speed = 300.0f;
    self->jump_force = 500.0f;
    self->gravity = 1800.0f;
    self->max_fall_speed = 600.0f;

    /* Wall mechanics */
    self->wall_slide_speed = 100.0f;
    self->wall_jump_force_x = 350.0f;
    self->wall_jump_force_y = 450.0f;
}

static void
my_platformer_update (LrgGameTemplate *template, gdouble delta)
{
    MyPlatformer *self = MY_PLATFORMER (template);

    /* Check ground status before movement */
    gboolean was_on_ground = self->on_ground;
    self->on_ground = check_collision_at (self->player_x, self->player_y + 32, 24, 1);

    /* Coyote time - brief window to jump after leaving ground */
    if (was_on_ground && !self->on_ground)
        self->coyote_timer = 6;  /* 6 frames */
    else if (self->on_ground)
        self->coyote_timer = 6;
    else if (self->coyote_timer > 0)
        self->coyote_timer--;

    /* Wall contact */
    check_wall_contact (self);

    /* Wall coyote time */
    if (self->on_wall_left || self->on_wall_right)
        self->wall_coyote_timer = 6;
    else if (self->wall_coyote_timer > 0)
        self->wall_coyote_timer--;

    /* Horizontal input */
    gfloat input_x = 0.0f;
    if (grl_is_key_down (GRL_KEY_A) || grl_is_key_down (GRL_KEY_LEFT))
        input_x -= 1.0f;
    if (grl_is_key_down (GRL_KEY_D) || grl_is_key_down (GRL_KEY_RIGHT))
        input_x += 1.0f;

    /* Apply horizontal movement */
    if (input_x != 0.0f)
    {
        self->velocity_x = input_x * self->move_speed;
        self->facing_right = input_x > 0;
    }
    else
    {
        self->velocity_x *= 0.85f;  /* Friction */
    }

    /* Jump input buffering */
    gboolean jump_pressed = grl_is_key_pressed (GRL_KEY_SPACE) ||
                            grl_is_key_pressed (GRL_KEY_W) ||
                            grl_is_key_pressed (GRL_KEY_UP);

    if (jump_pressed)
        self->jump_buffer_timer = 6;  /* Buffer for 6 frames */
    else if (self->jump_buffer_timer > 0)
        self->jump_buffer_timer--;

    /* Process jump */
    if (self->jump_buffer_timer > 0)
    {
        /* Regular jump (with coyote time) */
        if (self->coyote_timer > 0)
        {
            do_jump (self);
            self->jump_buffer_timer = 0;
        }
        /* Wall jump */
        else if (self->wall_coyote_timer > 0 && !self->on_ground)
        {
            if (self->on_wall_left)
            {
                do_wall_jump (self, TRUE);
                self->jump_buffer_timer = 0;
            }
            else if (self->on_wall_right)
            {
                do_wall_jump (self, FALSE);
                self->jump_buffer_timer = 0;
            }
        }
    }

    /* Variable jump height (cut jump short on release) */
    gboolean jump_held = grl_is_key_down (GRL_KEY_SPACE) ||
                         grl_is_key_down (GRL_KEY_W) ||
                         grl_is_key_down (GRL_KEY_UP);

    if (!jump_held && self->velocity_y < 0)
        self->velocity_y *= 0.5f;  /* Cut upward velocity */

    /* Apply gravity */
    self->velocity_y += self->gravity * delta;

    /* Wall slide (slow fall when touching wall) */
    if (!self->on_ground && (self->on_wall_left || self->on_wall_right))
    {
        if (self->velocity_y > self->wall_slide_speed)
            self->velocity_y = self->wall_slide_speed;
    }

    /* Clamp fall speed */
    if (self->velocity_y > self->max_fall_speed)
        self->velocity_y = self->max_fall_speed;

    /* Apply movement */
    move_x (self, self->velocity_x * delta);
    move_y (self, self->velocity_y * delta);

    /* Reset if fallen off bottom */
    if (self->player_y > LEVEL_HEIGHT * TILE_SIZE)
    {
        self->player_x = 100.0f;
        self->player_y = 500.0f;
        self->velocity_x = 0;
        self->velocity_y = 0;

        /* Hit stop on death */
        lrg_game_template_hit_stop (template, 0.1);
        lrg_game_template_shake (template, 0.5f);
    }

    /* Escape to quit */
    if (grl_is_key_pressed (GRL_KEY_ESCAPE))
        lrg_game_template_quit (template);
}

static void
my_platformer_draw (LrgGameTemplate *template)
{
    MyPlatformer *self = MY_PLATFORMER (template);

    /* Colors */
    g_autoptr(GrlColor) bg = grl_color_new (30, 35, 45, 255);
    g_autoptr(GrlColor) tile_color = grl_color_new (80, 90, 100, 255);
    g_autoptr(GrlColor) player_color = grl_color_new (100, 180, 240, 255);
    g_autoptr(GrlColor) wall_slide_color = grl_color_new (120, 200, 255, 255);

    grl_clear_background (bg);

    /* Draw level tiles */
    for (gint y = 0; y < LEVEL_HEIGHT; y++)
    {
        for (gint x = 0; x < LEVEL_WIDTH; x++)
        {
            if (LEVEL_DATA[y * LEVEL_WIDTH + x] == '1')
            {
                grl_draw_rectangle (x * TILE_SIZE, y * TILE_SIZE,
                                     TILE_SIZE, TILE_SIZE, tile_color);
            }
        }
    }

    /* Draw player */
    GrlColor *p_color = (!self->on_ground && (self->on_wall_left || self->on_wall_right))
                         ? wall_slide_color : player_color;

    grl_draw_rectangle (self->player_x, self->player_y, 24, 32, p_color);

    /* Draw player direction indicator */
    gfloat eye_x = self->facing_right ? self->player_x + 16 : self->player_x + 4;
    g_autoptr(GrlColor) eye_color = grl_color_new (255, 255, 255, 255);
    grl_draw_circle (eye_x, self->player_y + 8, 4, eye_color);

    /* Draw debug info */
    g_autoptr(GrlColor) text_color = grl_color_new (200, 200, 200, 255);

    g_autofree gchar *debug = g_strdup_printf (
        "Ground: %s | Wall L: %s R: %s | Coyote: %d | Buffer: %d",
        self->on_ground ? "Y" : "N",
        self->on_wall_left ? "Y" : "N",
        self->on_wall_right ? "Y" : "N",
        self->coyote_timer,
        self->jump_buffer_timer);
    grl_draw_text (debug, 10, 10, 14, text_color);

    grl_draw_text ("WASD/Arrows: Move | Space: Jump | Wall jump available",
                   10, LEVEL_HEIGHT * TILE_SIZE - 25, 14, text_color);
}

/* ==========================================================================
 * Class Initialization
 * ========================================================================== */

static void
my_platformer_class_init (MyPlatformerClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);

    template_class->configure = my_platformer_configure;
    template_class->update = my_platformer_update;
    template_class->draw = my_platformer_draw;
}

static void
my_platformer_init (MyPlatformer *self)
{
}

/* ==========================================================================
 * Main Entry Point
 * ========================================================================== */

int
main (int    argc,
      char **argv)
{
    g_autoptr(MyPlatformer) game = g_object_new (MY_TYPE_PLATFORMER, NULL);
    return lrg_game_template_run (LRG_GAME_TEMPLATE (game), argc, argv);
}
```

## Key Features Demonstrated

### Coyote Time

A brief window after leaving a platform where jumps are still allowed:

```c
/* When leaving ground, start coyote timer */
if (was_on_ground && !self->on_ground)
    self->coyote_timer = 6;  /* 6 frames ≈ 100ms at 60fps */

/* Can still jump during coyote time */
if (self->coyote_timer > 0 && jump_pressed)
{
    do_jump (self);
    self->coyote_timer = 0;
}
```

### Jump Buffering

Inputs pressed slightly before landing are remembered:

```c
/* Record jump input */
if (jump_pressed)
    self->jump_buffer_timer = 6;

/* Consume buffer when landing */
if (self->on_ground && self->jump_buffer_timer > 0)
{
    do_jump (self);
    self->jump_buffer_timer = 0;
}
```

### Variable Jump Height

Releasing jump early cuts the arc:

```c
if (!jump_held && self->velocity_y < 0)
    self->velocity_y *= 0.5f;
```

### Wall Sliding

Slow the fall when touching a wall:

```c
if (!self->on_ground && (self->on_wall_left || self->on_wall_right))
{
    if (self->velocity_y > self->wall_slide_speed)
        self->velocity_y = self->wall_slide_speed;
}
```

### Wall Jumping

Jump away from walls:

```c
static void
do_wall_jump (MyPlatformer *self, gboolean from_left)
{
    self->velocity_y = -self->wall_jump_force_y;
    self->velocity_x = from_left ? self->wall_jump_force_x : -self->wall_jump_force_x;
    self->facing_right = from_left;
}
```

### Collision Resolution

Slide along walls instead of stopping:

```c
static void
move_x (MyPlatformer *self, gfloat amount)
{
    if (!check_collision_at (new_x, self->player_y, 24, 32))
    {
        self->player_x = new_x;
    }
    else
    {
        /* Pixel-by-pixel slide */
        gfloat step = (amount > 0) ? 1.0f : -1.0f;
        while (fabsf (amount) > 0)
        {
            if (!check_collision_at (self->player_x + step, self->player_y, 24, 32))
            {
                self->player_x += step;
                amount -= step;
            }
            else
            {
                break;
            }
        }
    }
}
```

## Extending the Example

### Add Animations

```c
typedef enum {
    ANIM_IDLE,
    ANIM_RUN,
    ANIM_JUMP,
    ANIM_FALL,
    ANIM_WALL_SLIDE
} AnimState;

static AnimState
get_anim_state (MyPlatformer *self)
{
    if (!self->on_ground)
    {
        if (self->on_wall_left || self->on_wall_right)
            return ANIM_WALL_SLIDE;
        return (self->velocity_y < 0) ? ANIM_JUMP : ANIM_FALL;
    }
    return (fabsf (self->velocity_x) > 10) ? ANIM_RUN : ANIM_IDLE;
}
```

### Add Dash Ability

```c
if (grl_is_key_pressed (GRL_KEY_LEFT_SHIFT) && self->can_dash)
{
    self->velocity_x = self->facing_right ? 800.0f : -800.0f;
    self->velocity_y = 0;  /* Horizontal dash */
    self->can_dash = FALSE;

    lrg_game_template_hit_stop (template, 0.05);
    lrg_game_template_shake (template, 0.2f);
}

/* Reset dash on ground */
if (self->on_ground)
    self->can_dash = TRUE;
```

## Related Documentation

- [LrgPlatformerTemplate](../templates/platformer-template.md) - Full template reference
- [Input Buffering](../systems/input-buffer.md) - Input buffer system
- [Game Feel](../systems/game-feel.md) - Juice effects
