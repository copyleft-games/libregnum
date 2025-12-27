# Scripting Examples

This guide provides comprehensive examples for using the Lua and Python scripting backends in Libregnum.

## Hello World

### Lua

```c
#include <libregnum.h>

int
main (int argc, char *argv[])
{
    g_autoptr(LrgScriptingLua) scripting = lrg_scripting_lua_new ();
    g_autoptr(GError) error = NULL;

    if (!lrg_scripting_load_string (LRG_SCRIPTING (scripting),
                                    "hello",
                                    "Log:info('Hello from Lua!')",
                                    &error))
    {
        g_printerr ("Error: %s\n", error->message);
        return 1;
    }

    return 0;
}
```

### Python

```c
#include <libregnum.h>

int
main (int argc, char *argv[])
{
    g_autoptr(LrgScriptingPython) scripting = lrg_scripting_python_new ();
    g_autoptr(GError) error = NULL;

    if (!lrg_scripting_load_string (LRG_SCRIPTING (scripting),
                                    "hello",
                                    "Log.info('Hello from Python!')",
                                    &error))
    {
        g_printerr ("Error: %s\n", error->message);
        return 1;
    }

    return 0;
}
```

## Loading Scripts from Files

### C Code

```c
#include <libregnum.h>

int
main (int argc, char *argv[])
{
    g_autoptr(LrgScriptingPython) scripting = lrg_scripting_python_new ();
    g_autoptr(GError) error = NULL;

    /* Add search path for imports */
    lrg_scripting_python_add_search_path (scripting, "scripts");
    lrg_scripting_python_add_search_path (scripting, "scripts/lib");

    /* Load the main script */
    if (!lrg_scripting_load_file (LRG_SCRIPTING (scripting),
                                  "scripts/main.py",
                                  &error))
    {
        g_printerr ("Failed to load script: %s\n", error->message);
        return 1;
    }

    return 0;
}
```

### scripts/main.py

```python
# Can import from search paths
import utils
import config

Log.info("Main script loaded")
utils.initialize()
```

## Calling Script Functions from C

### Lua Script

```lua
-- game.lua
function calculate_damage(base_damage, armor, multiplier)
    local reduced = base_damage - armor
    if reduced < 0 then reduced = 0 end
    return reduced * multiplier
end
```

### C Code

```c
#include <libregnum.h>

gdouble
call_damage_calculator (LrgScripting *scripting,
                        gdouble       base_damage,
                        gdouble       armor,
                        gdouble       multiplier)
{
    g_autoptr(GError) error = NULL;
    GValue args[3] = { G_VALUE_INIT, G_VALUE_INIT, G_VALUE_INIT };
    GValue result = G_VALUE_INIT;
    gdouble damage = 0.0;

    /* Set up arguments */
    g_value_init (&args[0], G_TYPE_DOUBLE);
    g_value_init (&args[1], G_TYPE_DOUBLE);
    g_value_init (&args[2], G_TYPE_DOUBLE);
    g_value_set_double (&args[0], base_damage);
    g_value_set_double (&args[1], armor);
    g_value_set_double (&args[2], multiplier);

    /* Call the function */
    if (lrg_scripting_call_function (scripting,
                                     "calculate_damage",
                                     &result,
                                     3, args,
                                     &error))
    {
        if (G_VALUE_HOLDS_DOUBLE (&result))
            damage = g_value_get_double (&result);
        else if (G_VALUE_HOLDS_INT64 (&result))
            damage = (gdouble)g_value_get_int64 (&result);
        g_value_unset (&result);
    }
    else
    {
        g_warning ("Failed to call calculate_damage: %s", error->message);
    }

    g_value_unset (&args[0]);
    g_value_unset (&args[1]);
    g_value_unset (&args[2]);

    return damage;
}
```

## Registering C Functions for Scripts

### C Code

```c
#include <libregnum.h>

/* C function to expose to scripts */
static gboolean
get_random_int (LrgScripting  *scripting,
                guint          n_args,
                const GValue  *args,
                GValue        *return_value,
                gpointer       user_data,
                GError       **error)
{
    gint min = 0;
    gint max = 100;

    /* Parse arguments */
    if (n_args >= 1 && G_VALUE_HOLDS_INT64 (&args[0]))
        min = (gint)g_value_get_int64 (&args[0]);
    if (n_args >= 2 && G_VALUE_HOLDS_INT64 (&args[1]))
        max = (gint)g_value_get_int64 (&args[1]);

    /* Generate random number */
    gint result = g_random_int_range (min, max + 1);

    /* Return result */
    g_value_init (return_value, G_TYPE_INT64);
    g_value_set_int64 (return_value, result);

    return TRUE;
}

int
main (int argc, char *argv[])
{
    g_autoptr(LrgScriptingPython) scripting = lrg_scripting_python_new ();
    g_autoptr(GError) error = NULL;

    /* Register the C function */
    lrg_scripting_register_function (LRG_SCRIPTING (scripting),
                                     "get_random_int",
                                     get_random_int,
                                     NULL,
                                     &error);

    /* Now use it from Python */
    lrg_scripting_load_string (LRG_SCRIPTING (scripting), "test",
        "damage = get_random_int(10, 20)\n"
        "Log.info(f'Random damage: {damage}')",
        &error);

    return 0;
}
```

### Usage in Scripts

```lua
-- Lua
local damage = get_random_int(10, 20)
Log:info("Random damage: " .. damage)
```

```python
# Python
damage = get_random_int(10, 20)
Log.info(f"Random damage: {damage}")
```

## Global Variable Exchange

### C Code

```c
#include <libregnum.h>

void
setup_game_globals (LrgScripting *scripting)
{
    GValue val = G_VALUE_INIT;

    /* Set player name */
    g_value_init (&val, G_TYPE_STRING);
    g_value_set_string (&val, "Hero");
    lrg_scripting_set_global (scripting, "player_name", &val, NULL);
    g_value_unset (&val);

    /* Set starting gold */
    g_value_init (&val, G_TYPE_INT64);
    g_value_set_int64 (&val, 100);
    lrg_scripting_set_global (scripting, "player_gold", &val, NULL);
    g_value_unset (&val);

    /* Set difficulty multiplier */
    g_value_init (&val, G_TYPE_DOUBLE);
    g_value_set_double (&val, 1.5);
    lrg_scripting_set_global (scripting, "difficulty", &val, NULL);
    g_value_unset (&val);
}

gint64
get_player_score (LrgScripting *scripting)
{
    GValue val = G_VALUE_INIT;
    gint64 score = 0;

    if (lrg_scripting_get_global (scripting, "player_score", &val, NULL))
    {
        if (G_VALUE_HOLDS_INT64 (&val))
            score = g_value_get_int64 (&val);
        else if (G_VALUE_HOLDS_DOUBLE (&val))
            score = (gint64)g_value_get_double (&val);
        g_value_unset (&val);
    }

    return score;
}
```

### Usage in Scripts

```python
# Python - globals set from C are available
Log.info(f"Welcome, {player_name}!")
Log.info(f"Starting gold: {player_gold}")

# Scripts can set globals for C to read
player_score = 0

def add_score(points):
    global player_score
    player_score += int(points * difficulty)
```

## Update Hooks in Game Loop

### C Code

```c
#include <libregnum.h>

int
main (int argc, char *argv[])
{
    g_autoptr(LrgScriptingPython) scripting = lrg_scripting_python_new ();
    g_autoptr(GError) error = NULL;

    /* Load game script */
    lrg_scripting_load_string (LRG_SCRIPTING (scripting), "game",
        "import time\n"
        "\n"
        "player_x = 0.0\n"
        "player_y = 0.0\n"
        "speed = 200.0\n"
        "\n"
        "def update(delta):\n"
        "    global player_x\n"
        "    player_x += speed * delta\n"
        "\n"
        "def physics_update(delta):\n"
        "    # Fixed timestep physics\n"
        "    pass\n",
        &error);

    /* Register multiple update hooks */
    lrg_scripting_python_register_update_hook (scripting, "update");
    lrg_scripting_python_register_update_hook (scripting, "physics_update");

    /* Game loop */
    gint64 last_time = g_get_monotonic_time ();
    gboolean running = TRUE;

    while (running)
    {
        gint64 current_time = g_get_monotonic_time ();
        gfloat delta = (gfloat)(current_time - last_time) / 1000000.0f;
        last_time = current_time;

        /* Call all update hooks */
        lrg_scripting_python_update (scripting, delta);

        /* Limit frame rate */
        g_usleep (16000);  /* ~60 FPS */
    }

    return 0;
}
```

## Complete Game Loop Integration

### C Code

```c
#include <libregnum.h>

typedef struct {
    LrgEngine          *engine;
    LrgScriptingPython *scripting;
    gboolean            running;
} GameState;

static void
game_init (GameState *state)
{
    g_autoptr(GError) error = NULL;

    state->engine = lrg_engine_get_default ();
    state->scripting = lrg_scripting_python_new ();
    state->running = TRUE;

    /* Connect scripting to engine */
    lrg_scripting_python_set_engine (state->scripting, state->engine);
    lrg_scripting_python_set_registry (state->scripting,
                                       lrg_engine_get_registry (state->engine));
    lrg_engine_set_scripting (state->engine, LRG_SCRIPTING (state->scripting));

    /* Add search paths */
    lrg_scripting_python_add_search_path (state->scripting, "scripts");
    lrg_scripting_python_add_search_path (state->scripting, "mods");

    /* Load main script */
    if (!lrg_scripting_load_file (LRG_SCRIPTING (state->scripting),
                                  "scripts/main.py",
                                  &error))
    {
        g_printerr ("Script error: %s\n", error->message);
        state->running = FALSE;
        return;
    }

    /* Register hooks */
    lrg_scripting_python_register_update_hook (state->scripting, "game_update");
    lrg_scripting_python_register_update_hook (state->scripting, "late_update");

    /* Call init function if defined */
    lrg_scripting_call_function (LRG_SCRIPTING (state->scripting),
                                 "game_init",
                                 NULL, 0, NULL,
                                 NULL);  /* Ignore if not found */

    lrg_engine_startup (state->engine, NULL);
}

static void
game_update (GameState *state, gfloat delta)
{
    /* Update scripts */
    lrg_scripting_python_update (state->scripting, delta);

    /* Update engine */
    lrg_engine_update (state->engine, delta);

    /* Check if should quit */
    GValue quit_val = G_VALUE_INIT;
    if (lrg_scripting_get_global (LRG_SCRIPTING (state->scripting),
                                  "should_quit",
                                  &quit_val, NULL))
    {
        if (G_VALUE_HOLDS_BOOLEAN (&quit_val) && g_value_get_boolean (&quit_val))
            state->running = FALSE;
        g_value_unset (&quit_val);
    }
}

static void
game_shutdown (GameState *state)
{
    /* Call cleanup function if defined */
    lrg_scripting_call_function (LRG_SCRIPTING (state->scripting),
                                 "game_shutdown",
                                 NULL, 0, NULL,
                                 NULL);

    lrg_engine_shutdown (state->engine);
    g_clear_object (&state->scripting);
    g_clear_object (&state->engine);
}

int
main (int argc, char *argv[])
{
    GameState state = { 0 };
    gint64 last_time;
    gfloat delta;

    game_init (&state);

    last_time = g_get_monotonic_time ();
    while (state.running && lrg_engine_is_running (state.engine))
    {
        gint64 current_time = g_get_monotonic_time ();
        delta = (gfloat)(current_time - last_time) / 1000000.0f;
        last_time = current_time;

        game_update (&state, delta);

        g_usleep (16000);  /* ~60 FPS */
    }

    game_shutdown (&state);

    return 0;
}
```

### scripts/main.py

```python
"""
Main game script.
"""

# Game state
should_quit = False
player = None
enemies = []
score = 0

def game_init():
    """Called once at startup."""
    global player

    Log.info("Initializing game...")

    # Create player using registry
    if Registry.is_registered("player"):
        player = Registry.create("player", name="Hero", health=100)
        Log.info(f"Created player: {player.name}")
    else:
        Log.warning("Player type not registered")

def game_update(delta):
    """Called every frame."""
    global score

    # Update player
    if player:
        # Movement would go here
        pass

    # Update enemies
    for enemy in enemies:
        # Enemy AI would go here
        pass

def late_update(delta):
    """Called after main update."""
    # UI updates, camera following, etc.
    pass

def game_shutdown():
    """Called when game exits."""
    Log.info(f"Final score: {score}")
    Log.info("Game shutting down...")
```

## Building the Examples

```makefile
# Example Makefile
CC := gcc
PKG_CONFIG := pkg-config

CFLAGS := $(shell $(PKG_CONFIG) --cflags glib-2.0 gobject-2.0 libregnum-1)
LDFLAGS := $(shell $(PKG_CONFIG) --libs glib-2.0 gobject-2.0 libregnum-1)

game: main.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f game
```

## See Also

- [Scripting Module Overview](../modules/scripting/index.md)
- [LrgScripting API](../modules/scripting/scripting.md)
- [LrgScriptingLua](../modules/scripting/scripting-lua.md)
- [LrgScriptingPython](../modules/scripting/scripting-python.md)
