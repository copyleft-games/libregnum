# Minimal Game Example

This example demonstrates the simplest possible game using `LrgGameTemplate`. It shows the basic structure required to create a game with the template system.

## Complete Code

```c
/* minimal-game.c - The simplest game template example */

#include <libregnum.h>

/* ==========================================================================
 * Type Declaration
 * ========================================================================== */

#define MY_TYPE_MINIMAL_GAME (my_minimal_game_get_type ())
G_DECLARE_FINAL_TYPE (MyMinimalGame, my_minimal_game, MY, MINIMAL_GAME, LrgGameTemplate)

struct _MyMinimalGame
{
    LrgGameTemplate parent_instance;

    gfloat player_x;
    gfloat player_y;
    gfloat player_speed;
};

G_DEFINE_TYPE (MyMinimalGame, my_minimal_game, LRG_TYPE_GAME_TEMPLATE)

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

/*
 * Configure is called once during initialization.
 * Set window properties, load assets, and configure game settings.
 */
static void
my_minimal_game_configure (LrgGameTemplate *template)
{
    MyMinimalGame *self = MY_MINIMAL_GAME (template);

    /* Set window properties */
    g_object_set (template,
                  "title", "Minimal Game",
                  "window-width", 800,
                  "window-height", 600,
                  NULL);

    /* Initialize player position (center of screen) */
    self->player_x = 400.0f;
    self->player_y = 300.0f;
    self->player_speed = 200.0f;
}

/*
 * Update is called every frame with the delta time.
 * Handle input and update game logic here.
 */
static void
my_minimal_game_update (LrgGameTemplate *template,
                        gdouble          delta)
{
    MyMinimalGame *self = MY_MINIMAL_GAME (template);

    /* Get keyboard input */
    gfloat move_x = 0.0f;
    gfloat move_y = 0.0f;

    if (grl_is_key_down (GRL_KEY_W) || grl_is_key_down (GRL_KEY_UP))
        move_y -= 1.0f;
    if (grl_is_key_down (GRL_KEY_S) || grl_is_key_down (GRL_KEY_DOWN))
        move_y += 1.0f;
    if (grl_is_key_down (GRL_KEY_A) || grl_is_key_down (GRL_KEY_LEFT))
        move_x -= 1.0f;
    if (grl_is_key_down (GRL_KEY_D) || grl_is_key_down (GRL_KEY_RIGHT))
        move_x += 1.0f;

    /* Normalize diagonal movement */
    if (move_x != 0.0f && move_y != 0.0f)
    {
        gfloat len = sqrtf (move_x * move_x + move_y * move_y);
        move_x /= len;
        move_y /= len;
    }

    /* Update player position */
    self->player_x += move_x * self->player_speed * delta;
    self->player_y += move_y * self->player_speed * delta;

    /* Keep player on screen */
    self->player_x = CLAMP (self->player_x, 16.0f, 784.0f);
    self->player_y = CLAMP (self->player_y, 16.0f, 584.0f);

    /* Exit on Escape */
    if (grl_is_key_pressed (GRL_KEY_ESCAPE))
        lrg_game_template_quit (template);
}

/*
 * Draw is called every frame after update.
 * Render the game world here.
 */
static void
my_minimal_game_draw (LrgGameTemplate *template)
{
    MyMinimalGame *self = MY_MINIMAL_GAME (template);

    /* Clear screen */
    g_autoptr(GrlColor) bg = grl_color_new (40, 44, 52, 255);
    grl_clear_background (bg);

    /* Draw player as a circle */
    g_autoptr(GrlColor) player_color = grl_color_new (97, 175, 239, 255);
    grl_draw_circle (self->player_x, self->player_y, 16.0f, player_color);

    /* Draw instructions */
    g_autoptr(GrlColor) text_color = grl_color_new (255, 255, 255, 255);
    grl_draw_text ("WASD or Arrow Keys to move", 10, 10, 20, text_color);
    grl_draw_text ("ESC to quit", 10, 35, 20, text_color);
}

/* ==========================================================================
 * Class Initialization
 * ========================================================================== */

static void
my_minimal_game_class_init (MyMinimalGameClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);

    template_class->configure = my_minimal_game_configure;
    template_class->update = my_minimal_game_update;
    template_class->draw = my_minimal_game_draw;
}

static void
my_minimal_game_init (MyMinimalGame *self)
{
    /* Instance initialization (called before configure) */
}

/* ==========================================================================
 * Main Entry Point
 * ========================================================================== */

int
main (int    argc,
      char **argv)
{
    g_autoptr(MyMinimalGame) game = g_object_new (MY_TYPE_MINIMAL_GAME, NULL);

    return lrg_game_template_run (LRG_GAME_TEMPLATE (game), argc, argv);
}
```

## Building

Create a simple `Makefile`:

```makefile
CC = gcc
CFLAGS = $(shell pkg-config --cflags libregnum)
LDFLAGS = $(shell pkg-config --libs libregnum)

minimal-game: minimal-game.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f minimal-game
```

Build and run:

```bash
make
./minimal-game
```

## Code Breakdown

### 1. Type Declaration

```c
G_DECLARE_FINAL_TYPE (MyMinimalGame, my_minimal_game, MY, MINIMAL_GAME, LrgGameTemplate)
```

This macro declares a new GObject type that inherits from `LrgGameTemplate`. The struct contains game-specific state (player position, speed).

### 2. configure() Virtual Method

Called once at startup. Use it to:
- Set window properties (title, size, vsync)
- Initialize game state
- Load assets

### 3. update() Virtual Method

Called every frame with delta time. Use it to:
- Read input
- Update game logic
- Handle physics
- Check collisions

### 4. draw() Virtual Method

Called every frame after update. Use it to:
- Clear the screen
- Draw game objects
- Draw UI

### 5. Running the Game

```c
return lrg_game_template_run (LRG_GAME_TEMPLATE (game), argc, argv);
```

This starts the game loop and doesn't return until the game exits.

## Adding Features

### Add a Texture

```c
struct _MyMinimalGame
{
    LrgGameTemplate parent_instance;
    GrlTexture *player_texture;
    /* ... */
};

static void
my_minimal_game_configure (LrgGameTemplate *template)
{
    MyMinimalGame *self = MY_MINIMAL_GAME (template);

    /* Load texture */
    self->player_texture = grl_texture_new ("player.png");

    /* ... */
}

static void
my_minimal_game_draw (LrgGameTemplate *template)
{
    MyMinimalGame *self = MY_MINIMAL_GAME (template);

    /* Draw textured player */
    g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);
    grl_draw_texture (self->player_texture,
                       self->player_x - 16, self->player_y - 16,
                       white);
}
```

### Add Sound

```c
static void
my_minimal_game_configure (LrgGameTemplate *template)
{
    MyMinimalGame *self = MY_MINIMAL_GAME (template);

    /* Load sound */
    self->jump_sound = grl_sound_new ("jump.wav");

    /* ... */
}

static void
my_minimal_game_update (LrgGameTemplate *template, gdouble delta)
{
    MyMinimalGame *self = MY_MINIMAL_GAME (template);

    if (grl_is_key_pressed (GRL_KEY_SPACE))
    {
        grl_sound_play (self->jump_sound);
    }

    /* ... */
}
```

### Add Game States

```c
static void
my_minimal_game_configure (LrgGameTemplate *template)
{
    MyMinimalGame *self = MY_MINIMAL_GAME (template);

    /* Create main menu state */
    LrgTemplateMainMenuState *menu = lrg_template_main_menu_state_new ();
    g_signal_connect (menu, "new-game", G_CALLBACK (on_new_game), self);

    /* Push main menu as starting state */
    LrgGameStateManager *manager = lrg_game_template_get_state_manager (template);
    lrg_game_state_manager_push (manager, LRG_GAME_STATE (menu));
}
```

## Next Steps

- See [Idle Game Example](idle-game.md) for idle/incremental mechanics
- See [Deckbuilder Example](deckbuilder.md) for card game mechanics
- See [Platformer Example](platformer.md) for 2D platformer mechanics

## Related Documentation

- [LrgGameTemplate](../templates/game-template.md) - Full template reference
- [Getting Started](../README.md) - Template system overview
