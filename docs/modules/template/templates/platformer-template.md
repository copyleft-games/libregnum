# LrgPlatformerTemplate

`LrgPlatformerTemplate` is a 2D game template specialized for platformer games. It provides gravity physics, jumping mechanics, coyote time, wall sliding/jumping, and other platformer essentials.

## Inheritance Hierarchy

```
LrgGameTemplate
└── LrgGame2DTemplate
    └── LrgPlatformerTemplate (derivable)
```

## Features

- Gravity and ground detection
- Configurable jump with height control
- Coyote time (grace period after leaving platform)
- Jump buffering (input recorded before landing)
- Wall slide with configurable speed
- Wall jump with customizable angle
- Variable jump height (hold jump for higher)
- Multiple jumps (double jump, triple jump)

## Quick Start

```c
#define MY_TYPE_PLATFORMER (my_platformer_get_type ())
G_DECLARE_FINAL_TYPE (MyPlatformer, my_platformer, MY, PLATFORMER, LrgPlatformerTemplate)

struct _MyPlatformer
{
    LrgPlatformerTemplate parent_instance;
    gfloat player_x, player_y;
    gfloat velocity_x, velocity_y;
};

G_DEFINE_TYPE (MyPlatformer, my_platformer, LRG_TYPE_PLATFORMER_TEMPLATE)

static void
my_platformer_configure (LrgGameTemplate *template)
{
    LrgPlatformerTemplate *plat = LRG_PLATFORMER_TEMPLATE (template);
    LrgGame2DTemplate *template_2d = LRG_GAME_2D_TEMPLATE (template);

    g_object_set (template,
                  "title", "Super Platformer",
                  "window-width", 1280,
                  "window-height", 720,
                  NULL);

    /* Virtual resolution for pixel art */
    lrg_game_2d_template_set_virtual_resolution (template_2d, 320, 180);
    lrg_game_2d_template_set_pixel_perfect (template_2d, TRUE);

    /* Configure platformer physics */
    lrg_platformer_template_set_gravity (plat, 600.0f);
    lrg_platformer_template_set_jump_height (plat, 48.0f);
    lrg_platformer_template_set_coyote_time (plat, 0.1f);
    lrg_platformer_template_set_jump_buffer_time (plat, 0.1f);

    /* Wall mechanics */
    lrg_platformer_template_set_wall_slide_enabled (plat, TRUE);
    lrg_platformer_template_set_wall_slide_speed (plat, 50.0f);
    lrg_platformer_template_set_wall_jump_enabled (plat, TRUE);
}

static void
my_platformer_class_init (MyPlatformerClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    template_class->configure = my_platformer_configure;
}
```

## Physics Configuration

### Gravity

```c
/* Set gravity (pixels per second squared) */
lrg_platformer_template_set_gravity (template, 600.0f);
gfloat gravity = lrg_platformer_template_get_gravity (template);

/* Set terminal velocity (max fall speed) */
lrg_platformer_template_set_terminal_velocity (template, 400.0f);
gfloat terminal = lrg_platformer_template_get_terminal_velocity (template);
```

### Movement Speed

```c
/* Horizontal movement */
lrg_platformer_template_set_move_speed (template, 120.0f);
gfloat speed = lrg_platformer_template_get_move_speed (template);

/* Acceleration and deceleration */
lrg_platformer_template_set_acceleration (template, 800.0f);
lrg_platformer_template_set_deceleration (template, 1200.0f);
lrg_platformer_template_set_air_acceleration (template, 400.0f);  /* Reduced in air */
```

## Jump Mechanics

### Basic Jump

```c
/* Set jump height (in pixels) */
lrg_platformer_template_set_jump_height (template, 48.0f);
gfloat height = lrg_platformer_template_get_jump_height (template);

/* Alternative: set jump velocity directly */
lrg_platformer_template_set_jump_velocity (template, 280.0f);
```

### Variable Jump Height

When the player releases jump early, the jump is cut short:

```c
/* Enable/disable variable jump */
lrg_platformer_template_set_variable_jump_enabled (template, TRUE);

/* Minimum jump height (fraction of full jump) */
lrg_platformer_template_set_min_jump_fraction (template, 0.4f);  /* 40% minimum */
```

### Coyote Time

Grace period after leaving a platform where jump is still valid:

```c
/* Set coyote time (seconds) */
lrg_platformer_template_set_coyote_time (template, 0.1f);  /* 100ms */
gfloat coyote = lrg_platformer_template_get_coyote_time (template);
```

### Jump Buffering

Record jump input before landing, execute when grounded:

```c
/* Set jump buffer time (seconds) */
lrg_platformer_template_set_jump_buffer_time (template, 0.1f);
gfloat buffer = lrg_platformer_template_get_jump_buffer_time (template);
```

### Multiple Jumps

```c
/* Set number of extra jumps (1 = double jump, 2 = triple jump) */
lrg_platformer_template_set_extra_jumps (template, 1);
guint extra = lrg_platformer_template_get_extra_jumps (template);

/* Query remaining jumps */
guint remaining = lrg_platformer_template_get_remaining_jumps (template);

/* Reset jumps (call when landing) */
lrg_platformer_template_reset_jumps (template);
```

## Wall Mechanics

### Wall Slide

```c
/* Enable wall sliding */
lrg_platformer_template_set_wall_slide_enabled (template, TRUE);
gboolean enabled = lrg_platformer_template_get_wall_slide_enabled (template);

/* Set slide speed (slower than normal falling) */
lrg_platformer_template_set_wall_slide_speed (template, 50.0f);
gfloat slide_speed = lrg_platformer_template_get_wall_slide_speed (template);
```

### Wall Jump

```c
/* Enable wall jumping */
lrg_platformer_template_set_wall_jump_enabled (template, TRUE);
gboolean enabled = lrg_platformer_template_get_wall_jump_enabled (template);

/* Set wall jump force */
lrg_platformer_template_set_wall_jump_force_x (template, 150.0f);  /* Horizontal */
lrg_platformer_template_set_wall_jump_force_y (template, 250.0f);  /* Vertical */
```

## State Queries

```c
/* Check if player is on ground */
gboolean grounded = lrg_platformer_template_is_grounded (template);

/* Check if player is touching a wall */
gboolean on_wall = lrg_platformer_template_is_touching_wall (template);

/* Check wall side (-1 = left, 0 = none, 1 = right) */
gint wall_side = lrg_platformer_template_get_wall_side (template);

/* Check if currently wall sliding */
gboolean sliding = lrg_platformer_template_is_wall_sliding (template);

/* Check if in coyote time */
gboolean in_coyote = lrg_platformer_template_is_in_coyote_time (template);

/* Check if falling */
gboolean falling = lrg_platformer_template_is_falling (template);
```

## Action Methods

```c
/* Trigger a jump (checks if allowed) */
gboolean jumped = lrg_platformer_template_jump (template);

/* Force a jump (ignores ground state - for bouncing, springs, etc.) */
lrg_platformer_template_force_jump (template, 350.0f);  /* Custom velocity */

/* Cut jump short (variable jump) */
lrg_platformer_template_cut_jump (template);

/* Apply horizontal movement input (-1 to 1) */
lrg_platformer_template_set_move_input (template, input_x);
```

## Collision Callbacks

Override these to provide collision detection:

```c
/* Check if a point collides with ground */
gboolean (*check_ground) (LrgPlatformerTemplate *self,
                           gfloat                 x,
                           gfloat                 y);

/* Check if a point collides with wall */
gboolean (*check_wall) (LrgPlatformerTemplate *self,
                         gfloat                 x,
                         gfloat                 y);

/* Check if a point collides with ceiling */
gboolean (*check_ceiling) (LrgPlatformerTemplate *self,
                            gfloat                 x,
                            gfloat                 y);
```

### Example Implementation

```c
static gboolean
my_platformer_check_ground (LrgPlatformerTemplate *template,
                              gfloat                 x,
                              gfloat                 y)
{
    MyPlatformer *self = MY_PLATFORMER (template);

    /* Check tilemap collision */
    return tilemap_is_solid (self->tilemap, x, y + 1);
}

static gboolean
my_platformer_check_wall (LrgPlatformerTemplate *template,
                            gfloat                 x,
                            gfloat                 y)
{
    MyPlatformer *self = MY_PLATFORMER (template);

    /* Check tilemap collision with small offset */
    return tilemap_is_solid (self->tilemap, x, y);
}

static void
my_platformer_class_init (MyPlatformerClass *klass)
{
    LrgPlatformerTemplateClass *plat_class = LRG_PLATFORMER_TEMPLATE_CLASS (klass);
    plat_class->check_ground = my_platformer_check_ground;
    plat_class->check_wall = my_platformer_check_wall;
}
```

## Virtual Methods

```c
/* Called when player lands on ground */
void (*on_landed) (LrgPlatformerTemplate *self);

/* Called when player leaves ground */
void (*on_left_ground) (LrgPlatformerTemplate *self);

/* Called when player starts wall slide */
void (*on_wall_slide_start) (LrgPlatformerTemplate *self);

/* Called when player jumps */
void (*on_jumped) (LrgPlatformerTemplate *self, gboolean from_wall);
```

## Related Documentation

- [LrgGame2DTemplate](game-2d-template.md) - 2D template features
- [LrgGameTemplate](game-template.md) - Base template features
- [Platformer Example](../examples/platformer.md) - Complete example
- [Input Buffer](../systems/input-buffer.md) - Frame-perfect input
