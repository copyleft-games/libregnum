/* scripted-python-gobject-game.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Example demonstrating the PyGObject scripting system in libregnum.
 *
 * This example shows:
 * - Creating a PyGObject scripting context
 * - Loading GI typelibs for script access
 * - Loading Python scripts with GI bindings
 * - Registering C functions callable from Python
 * - Using update hooks for per-frame game logic
 * - Passing data between C and Python
 *
 * Unlike scripted-python-game.c which uses direct Python C API wrappers,
 * this example uses PyGObject for native GObject Introspection bindings.
 * Scripts can use `from gi.repository import GLib` and similar imports.
 *
 * Controls:
 *   SPACE/ENTER - Spawn a new ball from Python
 *   R           - Reset all balls
 *   ESC         - Quit
 */

#include <libregnum.h>
#include <graylib.h>
#include <math.h>

/* =============================================================================
 * Constants
 * ========================================================================== */

#define WINDOW_WIDTH   800
#define WINDOW_HEIGHT  600
#define MAX_BALLS      100

/* =============================================================================
 * Ball Structure
 *
 * Simple bouncing ball managed by Python/PyGObject.
 * ========================================================================== */

typedef struct
{
    gfloat x, y;
    gfloat vx, vy;
    gfloat radius;
    guint8 r, g, b;
    gboolean active;
} Ball;

static Ball balls[MAX_BALLS];
static gint  ball_count = 0;

/* =============================================================================
 * C Functions Exposed to Python
 * ========================================================================== */

/**
 * spawn_ball:
 *
 * C function callable from Python to spawn a new ball.
 * Python signature: spawn_ball(x, y, vx, vy, radius, r, g, b) -> ball_index
 */
static gboolean
spawn_ball (LrgScripting  *scripting,
            guint          n_args,
            const GValue  *args,
            GValue        *return_value,
            gpointer       user_data,
            GError       **error)
{
    gfloat x, y, vx, vy, radius;
    gint   r, g, b;
    gint   i;

    if (n_args < 8)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "spawn_ball requires 8 arguments: x, y, vx, vy, radius, r, g, b");
        return FALSE;
    }

    /* Extract arguments - Python numbers come as INT64 or DOUBLE */
    x = (gfloat)(G_VALUE_HOLDS_DOUBLE (&args[0]) ?
                 g_value_get_double (&args[0]) :
                 g_value_get_int64 (&args[0]));
    y = (gfloat)(G_VALUE_HOLDS_DOUBLE (&args[1]) ?
                 g_value_get_double (&args[1]) :
                 g_value_get_int64 (&args[1]));
    vx = (gfloat)(G_VALUE_HOLDS_DOUBLE (&args[2]) ?
                  g_value_get_double (&args[2]) :
                  g_value_get_int64 (&args[2]));
    vy = (gfloat)(G_VALUE_HOLDS_DOUBLE (&args[3]) ?
                  g_value_get_double (&args[3]) :
                  g_value_get_int64 (&args[3]));
    radius = (gfloat)(G_VALUE_HOLDS_DOUBLE (&args[4]) ?
                      g_value_get_double (&args[4]) :
                      g_value_get_int64 (&args[4]));
    r = (gint)(G_VALUE_HOLDS_DOUBLE (&args[5]) ?
               g_value_get_double (&args[5]) :
               g_value_get_int64 (&args[5]));
    g = (gint)(G_VALUE_HOLDS_DOUBLE (&args[6]) ?
               g_value_get_double (&args[6]) :
               g_value_get_int64 (&args[6]));
    b = (gint)(G_VALUE_HOLDS_DOUBLE (&args[7]) ?
               g_value_get_double (&args[7]) :
               g_value_get_int64 (&args[7]));

    /* Find an inactive slot */
    for (i = 0; i < MAX_BALLS; i++)
    {
        if (!balls[i].active)
        {
            balls[i].x = x;
            balls[i].y = y;
            balls[i].vx = vx;
            balls[i].vy = vy;
            balls[i].radius = radius;
            balls[i].r = (guint8)CLAMP (r, 0, 255);
            balls[i].g = (guint8)CLAMP (g, 0, 255);
            balls[i].b = (guint8)CLAMP (b, 0, 255);
            balls[i].active = TRUE;
            ball_count++;

            g_value_init (return_value, G_TYPE_INT);
            g_value_set_int (return_value, i);
            return TRUE;
        }
    }

    /* No slots available */
    g_value_init (return_value, G_TYPE_INT);
    g_value_set_int (return_value, -1);
    return TRUE;
}

/**
 * get_ball_count:
 *
 * Returns the current number of active balls.
 * Python signature: get_ball_count() -> count
 */
static gboolean
get_ball_count (LrgScripting  *scripting,
                guint          n_args,
                const GValue  *args,
                GValue        *return_value,
                gpointer       user_data,
                GError       **error)
{
    g_value_init (return_value, G_TYPE_INT);
    g_value_set_int (return_value, ball_count);
    return TRUE;
}

/**
 * clear_balls:
 *
 * Removes all balls.
 * Python signature: clear_balls()
 */
static gboolean
clear_balls (LrgScripting  *scripting,
             guint          n_args,
             const GValue  *args,
             GValue        *return_value,
             gpointer       user_data,
             GError       **error)
{
    gint i;

    for (i = 0; i < MAX_BALLS; i++)
    {
        balls[i].active = FALSE;
    }
    ball_count = 0;

    return TRUE;
}

/**
 * get_screen_size:
 *
 * Returns the screen width.
 * Python signature: get_screen_size() -> width
 */
static gboolean
get_screen_size (LrgScripting  *scripting,
                 guint          n_args,
                 const GValue  *args,
                 GValue        *return_value,
                 gpointer       user_data,
                 GError       **error)
{
    g_value_init (return_value, G_TYPE_INT);
    g_value_set_int (return_value, WINDOW_WIDTH);
    return TRUE;
}

/**
 * get_screen_height:
 *
 * Returns the screen height.
 */
static gboolean
get_screen_height (LrgScripting  *scripting,
                   guint          n_args,
                   const GValue  *args,
                   GValue        *return_value,
                   gpointer       user_data,
                   GError       **error)
{
    g_value_init (return_value, G_TYPE_INT);
    g_value_set_int (return_value, WINDOW_HEIGHT);
    return TRUE;
}

/* =============================================================================
 * Python Script (PyGObject)
 *
 * This script is embedded for simplicity. In a real game, you'd load
 * this from a file using lrg_scripting_load_file().
 *
 * Note: This script uses GLib from gi.repository to demonstrate
 * PyGObject's GI bindings capability.
 * ========================================================================== */

static const gchar *PYTHON_GAME_SCRIPT =
    "# Scripted Game Logic (PyGObject)\n"
    "# This Python code controls the bouncing balls\n"
    "# Using PyGObject for GObject Introspection bindings\n"
    "import random\n"
    "from gi.repository import GLib\n"
    "\n"
    "# Configuration\n"
    "GRAVITY = 500\n"
    "BOUNCE_DAMPENING = 0.8\n"
    "SPAWN_SPEED = 300\n"
    "\n"
    "# Ball state (mirrors C state for physics)\n"
    "ball_velocities = {}\n"
    "\n"
    "def game_init():\n"
    "    \"\"\"Initialize the game.\"\"\"\n"
    "    print('PyGObject game script initialized!')\n"
    "    print(f'Using GLib version: {GLib.MAJOR_VERSION}.{GLib.MINOR_VERSION}.{GLib.MICRO_VERSION}')\n"
    "    print('Press SPACE to spawn balls, R to reset')\n"
    "    \n"
    "    # Spawn a few initial balls\n"
    "    for i in range(5):\n"
    "        spawn_random_ball()\n"
    "\n"
    "def spawn_random_ball():\n"
    "    \"\"\"Spawn a ball at a random position with random color.\"\"\"\n"
    "    global ball_velocities\n"
    "    \n"
    "    width = get_screen_size()\n"
    "    height = get_screen_height()\n"
    "    \n"
    "    x = random.randint(50, width - 50)\n"
    "    y = random.randint(50, height // 2)\n"
    "    vx = random.randint(-SPAWN_SPEED, SPAWN_SPEED)\n"
    "    vy = random.randint(-100, 100)\n"
    "    radius = random.randint(10, 30)\n"
    "    r = random.randint(50, 255)\n"
    "    g = random.randint(50, 255)\n"
    "    b = random.randint(50, 255)\n"
    "    \n"
    "    idx = spawn_ball(x, y, vx, vy, radius, r, g, b)\n"
    "    if idx >= 0:\n"
    "        ball_velocities[idx] = {'vx': vx, 'vy': vy}\n"
    "    else:\n"
    "        print('Warning: Could not spawn ball - max reached!')\n"
    "    \n"
    "    return idx\n"
    "\n"
    "def on_spawn_key():\n"
    "    \"\"\"Called when user presses SPACE.\"\"\"\n"
    "    spawn_random_ball()\n"
    "    count = get_ball_count()\n"
    "    print(f'Ball count: {count}')\n"
    "\n"
    "def on_reset_key():\n"
    "    \"\"\"Called when user presses R.\"\"\"\n"
    "    global ball_velocities\n"
    "    \n"
    "    clear_balls()\n"
    "    ball_velocities = {}\n"
    "    print('All balls cleared!')\n"
    "    \n"
    "    # Spawn initial balls again\n"
    "    for i in range(5):\n"
    "        spawn_random_ball()\n"
    "\n"
    "def game_update(delta):\n"
    "    \"\"\"Per-frame update (registered as update hook).\"\"\"\n"
    "    # Physics is handled in C for this example\n"
    "    # But Python could do additional game logic here\n"
    "    # With PyGObject, we could also use GLib.timeout_add, etc.\n"
    "    pass\n"
    "\n"
    "# Call init on load\n"
    "game_init()\n";

/* =============================================================================
 * Physics Update (C-side)
 *
 * Updates ball positions with gravity and bouncing.
 * ========================================================================== */

static void
update_physics (gfloat delta)
{
    gint   i;
    gfloat gravity = 500.0f;
    gfloat dampening = 0.8f;

    for (i = 0; i < MAX_BALLS; i++)
    {
        if (!balls[i].active)
            continue;

        /* Apply gravity */
        balls[i].vy += gravity * delta;

        /* Update position */
        balls[i].x += balls[i].vx * delta;
        balls[i].y += balls[i].vy * delta;

        /* Bounce off walls */
        if (balls[i].x - balls[i].radius < 0)
        {
            balls[i].x = balls[i].radius;
            balls[i].vx = -balls[i].vx * dampening;
        }
        else if (balls[i].x + balls[i].radius > WINDOW_WIDTH)
        {
            balls[i].x = WINDOW_WIDTH - balls[i].radius;
            balls[i].vx = -balls[i].vx * dampening;
        }

        /* Bounce off floor/ceiling */
        if (balls[i].y - balls[i].radius < 0)
        {
            balls[i].y = balls[i].radius;
            balls[i].vy = -balls[i].vy * dampening;
        }
        else if (balls[i].y + balls[i].radius > WINDOW_HEIGHT)
        {
            balls[i].y = WINDOW_HEIGHT - balls[i].radius;
            balls[i].vy = -balls[i].vy * dampening;
        }
    }
}

/* =============================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_autoptr(LrgScriptingPyGObject) scripting = NULL;
    g_autoptr(LrgGrlWindow) window = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GrlColor) bg_color = NULL;
    g_autoptr(GrlColor) white_color = NULL;
    g_autoptr(GrlColor) gray_color = NULL;
    LrgScriptingGI *gi_self = NULL;
    LrgEngine *engine = NULL;
    LrgInputManager *input_manager = NULL;
    GrlWindow *grl_window = NULL;
    gint i;

    /* Create window first */
    window = lrg_grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT,
                                 "Scripted Game (PyGObject) - Bouncing Balls");
    lrg_window_set_target_fps (LRG_WINDOW (window), 60);

    /* Get the underlying GrlWindow for drawing */
    grl_window = lrg_grl_window_get_grl_window (window);

    /* Initialize engine with window */
    engine = lrg_engine_get_default ();
    lrg_engine_set_window (engine, LRG_WINDOW (window));

    if (!lrg_engine_startup (engine, &error))
    {
        g_printerr ("Failed to start engine: %s\n", error->message);
        return 1;
    }

    /* Get input manager */
    input_manager = lrg_input_manager_get_default ();

    /* Create PyGObject scripting context */
    scripting = lrg_scripting_pygobject_new ();
    gi_self = LRG_SCRIPTING_GI (scripting);

    /* Load GLib typelib so scripts can use gi.repository.GLib */
    if (!lrg_scripting_gi_require_typelib (gi_self, "GLib", "2.0", &error))
    {
        g_printerr ("Failed to load GLib typelib: %s\n", error->message);
        return 1;
    }

    /* Attach scripting to engine */
    lrg_engine_set_scripting (engine, LRG_SCRIPTING (scripting));

    /* Register C functions that Python can call */
    lrg_scripting_register_function (LRG_SCRIPTING (scripting),
                                     "spawn_ball",
                                     spawn_ball,
                                     NULL,
                                     &error);
    if (error != NULL)
    {
        g_printerr ("Failed to register spawn_ball: %s\n", error->message);
        return 1;
    }

    lrg_scripting_register_function (LRG_SCRIPTING (scripting),
                                     "get_ball_count",
                                     get_ball_count,
                                     NULL,
                                     &error);

    lrg_scripting_register_function (LRG_SCRIPTING (scripting),
                                     "clear_balls",
                                     clear_balls,
                                     NULL,
                                     &error);

    lrg_scripting_register_function (LRG_SCRIPTING (scripting),
                                     "get_screen_size",
                                     get_screen_size,
                                     NULL,
                                     &error);

    lrg_scripting_register_function (LRG_SCRIPTING (scripting),
                                     "get_screen_height",
                                     get_screen_height,
                                     NULL,
                                     &error);

    /* Load the Python game script */
    if (!lrg_scripting_load_string (LRG_SCRIPTING (scripting),
                                    "game.py",
                                    PYTHON_GAME_SCRIPT,
                                    &error))
    {
        g_printerr ("Failed to load Python script: %s\n", error->message);
        return 1;
    }

    /* Register the update hook (using GI base class method) */
    lrg_scripting_gi_register_update_hook (gi_self, "game_update");

    /* Initialize balls array */
    for (i = 0; i < MAX_BALLS; i++)
    {
        balls[i].active = FALSE;
    }

    /* Create reusable colors */
    bg_color = grl_color_new (30, 30, 40, 255);
    white_color = grl_color_new (255, 255, 255, 255);
    gray_color = grl_color_new (150, 150, 150, 255);

    g_print ("Scripted Game Example (PyGObject)\n");
    g_print ("==================================\n");
    g_print ("Controls:\n");
    g_print ("  SPACE/ENTER - Spawn a new ball\n");
    g_print ("  R           - Reset all balls\n");
    g_print ("  ESC         - Quit\n\n");

    /* Main loop */
    while (!lrg_window_should_close (LRG_WINDOW (window)))
    {
        gfloat delta;

        delta = lrg_window_get_frame_time (LRG_WINDOW (window));

        /* Poll input */
        lrg_input_manager_poll (input_manager);

        /* Handle input */
        if (lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_SPACE) ||
            lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_ENTER))
        {
            /* Call Python function to spawn ball */
            lrg_scripting_call_function (LRG_SCRIPTING (scripting),
                                         "on_spawn_key",
                                         NULL,
                                         0,
                                         NULL,
                                         NULL);
        }

        if (lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_R))
        {
            /* Call Python function to reset */
            lrg_scripting_call_function (LRG_SCRIPTING (scripting),
                                         "on_reset_key",
                                         NULL,
                                         0,
                                         NULL,
                                         NULL);
        }

        if (lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_ESCAPE))
        {
            break;
        }

        /* Update physics (C-side) */
        update_physics (delta);

        /* Update engine (calls Python update hooks via GI) */
        lrg_engine_update (engine, delta);

        /* Render using graylib direct drawing */
        grl_window_begin_drawing (grl_window);
        grl_draw_clear_background (bg_color);

        /* Draw all active balls */
        for (i = 0; i < MAX_BALLS; i++)
        {
            if (balls[i].active)
            {
                g_autoptr(GrlColor) ball_color = NULL;
                g_autoptr(LrgCircle2D) circle = NULL;

                ball_color = grl_color_new (balls[i].r, balls[i].g,
                                            balls[i].b, 255);
                circle = lrg_circle2d_new_full (balls[i].x, balls[i].y,
                                                balls[i].radius, ball_color);
                lrg_drawable_draw (LRG_DRAWABLE (circle), delta);
            }
        }

        /* Draw ball count */
        {
            g_autofree gchar *text = NULL;
            g_autoptr(LrgText2D) count_label = NULL;

            text = g_strdup_printf ("Balls: %d", ball_count);
            count_label = lrg_text2d_new_full (10.0f, 10.0f, text, 20.0f, white_color);
            lrg_drawable_draw (LRG_DRAWABLE (count_label), delta);
        }

        /* Draw instructions */
        {
            g_autoptr(LrgText2D) hint_label = NULL;

            hint_label = lrg_text2d_new_full (10.0f, (gfloat)(WINDOW_HEIGHT - 30),
                                              "SPACE/ENTER: spawn | R: reset | ESC: quit",
                                              16.0f, gray_color);
            lrg_drawable_draw (LRG_DRAWABLE (hint_label), delta);
        }

        grl_window_end_drawing (grl_window);
    }

    /* Shutdown */
    lrg_engine_shutdown (engine);

    return 0;
}
