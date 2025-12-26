# Hello World - Getting Started with Libregnum

A minimal example to get started with the Libregnum game engine. This example creates a simple game with a moving square, physics simulation, and debug overlay.

## Minimal Example

The simplest possible Libregnum application:

```c
#include <libregnum.h>
#include <stdio.h>

int main (int argc, char *argv[])
{
    g_autoptr(LrgEngine) engine = lrg_engine_get_default ();
    g_autoptr(GError) error = NULL;

    /* Initialize engine */
    if (!lrg_engine_startup (engine, &error))
    {
        g_print ("Engine startup failed: %s\n", error->message);
        return 1;
    }

    g_print ("Engine started successfully\n");

    /* Game loop (10 frames) */
    for (gint frame = 0; frame < 10; frame++)
    {
        lrg_engine_update (engine, 1.0f / 60.0f);
        g_print ("Frame %d\n", frame);
    }

    /* Shutdown */
    lrg_engine_shutdown (engine);

    return 0;
}
```

## Adding Physics

Create a game object with a physics body:

```c
#include <libregnum.h>

typedef struct
{
    LrgWorld         *world;
    LrgPhysicsWorld  *physics;
    LrgGameObject    *player;
    LrgRigidBody     *player_body;
} GameState;

static void
create_player (GameState *state)
{
    /* Create game object */
    state->player = g_object_new (LRG_TYPE_GAME_OBJECT,
                                   "tag", "player",
                                   NULL);

    /* Create physics body */
    state->player_body = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);
    lrg_rigid_body_set_position (state->player_body, 100.0f, 200.0f);
    lrg_rigid_body_set_box_shape (state->player_body, 32.0f, 32.0f);
    lrg_rigid_body_set_mass (state->player_body, 1.0f);

    /* Add to world */
    lrg_world_add_object (state->world, state->player);
    lrg_physics_world_add_body (state->physics, state->player_body);
}

static void
update_game (GameState *state, gfloat delta_time)
{
    /* Update physics */
    lrg_physics_world_step (state->physics, delta_time);

    /* Get player position */
    gfloat x, y;
    lrg_rigid_body_get_position (state->player_body, &x, &y);

    g_print ("Player at (%.1f, %.1f)\n", x, y);
}

int main (void)
{
    GameState state = { 0 };
    g_autoptr(GError) error = NULL;

    /* Create engine */
    g_autoptr(LrgEngine) engine = lrg_engine_get_default ();
    if (!lrg_engine_startup (engine, &error))
    {
        g_print ("Startup failed: %s\n", error->message);
        return 1;
    }

    /* Setup game state */
    state.world = lrg_world_new ();
    state.physics = lrg_physics_world_new ();
    lrg_physics_world_set_gravity (state.physics, 0.0f, -9.8f);

    create_player (&state);

    /* Game loop */
    for (gint frame = 0; frame < 600; frame++)  /* 10 seconds at 60 FPS */
    {
        update_game (&state, 1.0f / 60.0f);
    }

    /* Cleanup */
    g_object_unref (state.physics);
    g_object_unref (state.world);
    lrg_engine_shutdown (engine);

    return 0;
}
```

## Adding Debug Overlay

Display real-time performance metrics:

```c
#include <libregnum.h>

typedef struct
{
    LrgWorld         *world;
    LrgPhysicsWorld  *physics;
    LrgProfiler      *profiler;
    LrgDebugOverlay  *overlay;
    gint             score;
} GameState;

static void
update_game (GameState *state, gfloat delta_time)
{
    LrgProfiler *p = state->profiler;

    lrg_profiler_begin_frame (p);

    /* Profile physics update */
    lrg_profiler_begin_section (p, "physics");
    lrg_physics_world_step (state->physics, delta_time);
    lrg_profiler_end_section (p, "physics");

    /* Update score */
    state->score += 10;

    /* Update overlay with custom data */
    lrg_profiler_begin_section (p, "overlay");
    lrg_debug_overlay_set_custom_line (state->overlay, "score",
                                        "Score: %d", state->score);
    lrg_debug_overlay_set_custom_line (state->overlay, "bodies",
                                        "Bodies: %u",
                                        lrg_physics_world_get_body_count (state->physics));
    lrg_profiler_end_section (p, "overlay");

    lrg_profiler_end_frame (p);
}

int main (void)
{
    GameState state = { 0 };
    g_autoptr(GError) error = NULL;

    /* Setup engine */
    g_autoptr(LrgEngine) engine = lrg_engine_get_default ();
    if (!lrg_engine_startup (engine, &error))
    {
        g_print ("Startup failed: %s\n", error->message);
        return 1;
    }

    /* Setup game state */
    state.world = lrg_world_new ();
    state.physics = lrg_physics_world_new ();
    state.profiler = lrg_profiler_get_default ();
    state.overlay = lrg_debug_overlay_get_default ();

    /* Configure profiler and overlay */
    lrg_profiler_set_enabled (state.profiler, TRUE);
    lrg_debug_overlay_set_visible (state.overlay, TRUE);
    lrg_debug_overlay_set_flags (state.overlay,
        LRG_DEBUG_OVERLAY_FPS |
        LRG_DEBUG_OVERLAY_FRAME_TIME |
        LRG_DEBUG_OVERLAY_CUSTOM);

    lrg_physics_world_set_gravity (state.physics, 0.0f, -9.8f);

    /* Add some bodies */
    for (gint i = 0; i < 5; i++)
    {
        g_autoptr(LrgRigidBody) body = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);
        lrg_rigid_body_set_position (body, 50.0f + i * 30.0f, 100.0f + i * 20.0f);
        lrg_rigid_body_set_box_shape (body, 20.0f, 20.0f);
        lrg_physics_world_add_body (state.physics, body);
    }

    /* Game loop */
    for (gint frame = 0; frame < 600; frame++)
    {
        update_game (&state, 1.0f / 60.0f);

        if (frame % 60 == 0)  /* Every second */
        {
            g_autofree gchar *text = lrg_debug_overlay_get_text (state.overlay);
            g_print ("%s\n\n", text);
        }
    }

    /* Cleanup */
    g_object_unref (state.physics);
    g_object_unref (state.world);
    lrg_engine_shutdown (engine);

    return 0;
}
```

## Adding Debug Console

Interactive debugging commands:

```c
#include <libregnum.h>

typedef struct
{
    LrgWorld         *world;
    LrgPhysicsWorld  *physics;
    LrgDebugConsole  *console;
    gint             score;
} GameState;

static gchar *
cmd_score (LrgDebugConsole  *console,
           guint             argc,
           const gchar     **argv,
           gpointer          user_data)
{
    GameState *state = (GameState *)user_data;
    return g_strdup_printf ("Score: %d", state->score);
}

static gchar *
cmd_add_score (LrgDebugConsole  *console,
               guint             argc,
               const gchar     **argv,
               gpointer          user_data)
{
    GameState *state = (GameState *)user_data;

    if (argc < 2)
        return g_strdup ("Usage: addscore <amount>");

    gint amount = g_ascii_strtoll (argv[1], NULL, 10);
    state->score += amount;

    return g_strdup_printf ("Score now: %d", state->score);
}

int main (void)
{
    GameState state = { 0 };
    g_autoptr(GError) error = NULL;

    /* Setup engine */
    g_autoptr(LrgEngine) engine = lrg_engine_get_default ();
    if (!lrg_engine_startup (engine, &error))
    {
        g_print ("Startup failed: %s\n", error->message);
        return 1;
    }

    /* Setup game state */
    state.world = lrg_world_new ();
    state.physics = lrg_physics_world_new ();
    state.console = lrg_debug_console_get_default ();

    /* Register console commands */
    lrg_debug_console_register_command (state.console, "score",
        "Show current score", cmd_score, &state, NULL);
    lrg_debug_console_register_command (state.console, "addscore",
        "Add to score", cmd_add_score, &state, NULL);

    /* Execute commands */
    g_autofree gchar *result = lrg_debug_console_execute (state.console,
                                                           "score", &error);
    if (result != NULL)
        g_print ("Command result: %s\n", result);

    result = lrg_debug_console_execute (state.console, "addscore 100", NULL);
    g_print ("Command result: %s\n", result);

    result = lrg_debug_console_execute (state.console, "score", NULL);
    g_print ("Command result: %s\n", result);

    /* Cleanup */
    g_object_unref (state.physics);
    g_object_unref (state.world);
    lrg_engine_shutdown (engine);

    return 0;
}
```

## Building the Example

Create a `Makefile`:

```makefile
CFLAGS = $(shell pkg-config --cflags libregnum glib-2.0 gobject-2.0)
LDFLAGS = $(shell pkg-config --libs libregnum glib-2.0 gobject-2.0)

SOURCES = hello-world.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = hello-world

all: $(TARGET)

$(TARGET): $(OBJECTS)
	gcc $(OBJECTS) -o $@ $(LDFLAGS)

%.o: %.c
	gcc -c $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
```

Build and run:

```bash
make clean && make && make run
```

## Next Steps

1. **Read Module Documentation**: Start with Physics, Debug, or Networking modules based on your needs
2. **Explore Examples**: Check `/var/home/zach/Source/Projects/libregnum/tests/` for more complete examples
3. **Build Your Game**: Use the patterns shown here to create your game logic
4. **Profile Performance**: Use the profiler to optimize hot spots
5. **Enable Multiplayer**: Integrate networking when ready for online features

## Key Concepts

- **Engine**: Central singleton managing all subsystems
- **World**: Container for game objects
- **Physics World**: Separate physics simulation system
- **Debug Tools**: Console, overlay, profiler, and inspector for development
- **Messages**: Network communication unit in multiplayer

## Common Pitfalls

1. **Forgetting `g_autoptr()`**: Always use automatic cleanup for GObject instances
2. **Not calling `poll()`**: Remember to call server/client poll in game loop
3. **Leaking memory**: Use `g_object_unref()` or `g_autoptr()` for proper cleanup
4. **Ignoring return values**: Check return values and error pointers from API calls
