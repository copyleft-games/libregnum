/* scripted-gjs-game.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Example demonstrating the Gjs (GNOME JavaScript) scripting system in libregnum.
 *
 * This example shows:
 * - Creating a Gjs scripting context
 * - Loading GI typelibs for script access
 * - Loading JavaScript scripts with GI bindings
 * - Using update hooks for per-frame game logic
 * - Using globals to pass data between C and JavaScript
 *
 * Note: This example uses globals for C/JS communication rather than
 * registered C functions, as Gjs's high-level API doesn't easily support
 * C function callbacks without using the SpiderMonkey native API.
 *
 * Controls:
 *   SPACE/ENTER - Spawn a new ball
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
 * Simple bouncing ball with physics handled in C.
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
 * Ball Management Functions (C-side)
 * ========================================================================== */

static gint
spawn_ball_c (gfloat x, gfloat y, gfloat vx, gfloat vy,
              gfloat radius, gint r, gint g, gint b)
{
    gint i;

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
            return i;
        }
    }

    return -1;
}

static void
spawn_random_ball_c (void)
{
    gfloat x, y, vx, vy, radius;
    gint   r, g, b;

    x = (gfloat)(g_random_int_range (50, WINDOW_WIDTH - 50));
    y = (gfloat)(g_random_int_range (50, WINDOW_HEIGHT / 2));
    vx = (gfloat)(g_random_int_range (-300, 300));
    vy = (gfloat)(g_random_int_range (-100, 100));
    radius = (gfloat)(g_random_int_range (10, 30));
    r = g_random_int_range (50, 255);
    g = g_random_int_range (50, 255);
    b = g_random_int_range (50, 255);

    spawn_ball_c (x, y, vx, vy, radius, r, g, b);
}

static void
clear_balls_c (void)
{
    gint i;

    for (i = 0; i < MAX_BALLS; i++)
    {
        balls[i].active = FALSE;
    }
    ball_count = 0;
}

/* =============================================================================
 * JavaScript Script (Gjs)
 *
 * This script demonstrates:
 * - Using GI imports (GLib)
 * - Defining update hooks called each frame
 * - Reading globals set by C
 * - Printing status messages
 *
 * The script receives delta time via update hooks and has access to
 * globals set by the C host (ball_count, screen_width, screen_height).
 * ========================================================================== */

static const gchar *JAVASCRIPT_GAME_SCRIPT =
    "// Scripted Game Logic (Gjs)\n"
    "// Demonstrates Gjs GObject Introspection bindings\n"
    "const GLib = imports.gi.GLib;\n"
    "\n"
    "// Track accumulated time for periodic logging\n"
    "let accumulated_time = 0;\n"
    "let log_interval = 2.0;  // Log every 2 seconds\n"
    "let last_ball_count = 0;\n"
    "\n"
    "function game_init() {\n"
    "    print('=== Gjs Bouncing Balls Demo ===');\n"
    "    print('Using GLib version: ' + GLib.MAJOR_VERSION + '.' +\n"
    "          GLib.MINOR_VERSION + '.' + GLib.MICRO_VERSION);\n"
    "    print('');\n"
    "    print('Controls:');\n"
    "    print('  SPACE/ENTER - Spawn a new ball');\n"
    "    print('  R           - Reset all balls');\n"
    "    print('  ESC         - Quit');\n"
    "    print('');\n"
    "}\n"
    "\n"
    "function game_update(delta) {\n"
    "    // Accumulate time\n"
    "    accumulated_time += delta;\n"
    "    \n"
    "    // Check if ball count changed (set by C as global)\n"
    "    if (typeof globalThis.ball_count !== 'undefined' &&\n"
    "        globalThis.ball_count !== last_ball_count) {\n"
    "        if (globalThis.ball_count > last_ball_count) {\n"
    "            print('Ball spawned! Count: ' + globalThis.ball_count);\n"
    "        } else if (globalThis.ball_count === 0) {\n"
    "            print('All balls cleared!');\n"
    "        }\n"
    "        last_ball_count = globalThis.ball_count;\n"
    "    }\n"
    "    \n"
    "    // Periodic status update\n"
    "    if (accumulated_time >= log_interval) {\n"
    "        accumulated_time = 0;\n"
    "        // This shows the script is running and has access to GLib\n"
    "        let now = GLib.DateTime.new_now_local();\n"
    "        let timeStr = now.format('%H:%M:%S');\n"
    "        print('[' + timeStr + '] Balls active: ' + \n"
    "              (globalThis.ball_count || 0));\n"
    "    }\n"
    "}\n"
    "\n"
    "// Initialize on load\n"
    "game_init();\n";

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
    g_autoptr(LrgScriptingGjs) scripting = NULL;
    g_autoptr(LrgGrlWindow) window = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GrlColor) bg_color = NULL;
    g_autoptr(GrlColor) white_color = NULL;
    g_autoptr(GrlColor) gray_color = NULL;
    LrgScriptingGI *gi_self = NULL;
    LrgEngine *engine = NULL;
    LrgInputManager *input_manager = NULL;
    GrlWindow *grl_window = NULL;
    GValue ball_count_value = G_VALUE_INIT;
    gint i;

    /* Create window first */
    window = lrg_grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT,
                                 "Scripted Game (Gjs) - Bouncing Balls");
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

    /* Create Gjs scripting context */
    scripting = lrg_scripting_gjs_new ();
    gi_self = LRG_SCRIPTING_GI (scripting);

    /* Load GLib typelib so scripts can use imports.gi.GLib */
    if (!lrg_scripting_gi_require_typelib (gi_self, "GLib", "2.0", &error))
    {
        g_printerr ("Failed to load GLib typelib: %s\n", error->message);
        return 1;
    }

    /* Attach scripting to engine */
    lrg_engine_set_scripting (engine, LRG_SCRIPTING (scripting));

    /* Set initial globals for the script to read */
    g_value_init (&ball_count_value, G_TYPE_INT);
    g_value_set_int (&ball_count_value, ball_count);
    lrg_scripting_set_global (LRG_SCRIPTING (scripting), "ball_count",
                               &ball_count_value, NULL);
    g_value_unset (&ball_count_value);

    /* Load the JavaScript game script */
    if (!lrg_scripting_load_string (LRG_SCRIPTING (scripting),
                                    "game.js",
                                    JAVASCRIPT_GAME_SCRIPT,
                                    &error))
    {
        g_printerr ("Failed to load JavaScript script: %s\n", error->message);
        return 1;
    }

    /* Register the update hook (using GI base class method) */
    lrg_scripting_gi_register_update_hook (gi_self, "game_update");

    /* Initialize balls array */
    for (i = 0; i < MAX_BALLS; i++)
    {
        balls[i].active = FALSE;
    }

    /* Spawn some initial balls */
    for (i = 0; i < 5; i++)
    {
        spawn_random_ball_c ();
    }

    /* Update the ball count global */
    g_value_init (&ball_count_value, G_TYPE_INT);
    g_value_set_int (&ball_count_value, ball_count);
    lrg_scripting_set_global (LRG_SCRIPTING (scripting), "ball_count",
                               &ball_count_value, NULL);
    g_value_unset (&ball_count_value);

    /* Create reusable colors */
    bg_color = grl_color_new (30, 30, 40, 255);
    white_color = grl_color_new (255, 255, 255, 255);
    gray_color = grl_color_new (150, 150, 150, 255);

    /* Main loop */
    while (!lrg_window_should_close (LRG_WINDOW (window)))
    {
        gfloat delta;
        gint old_ball_count;

        delta = lrg_window_get_frame_time (LRG_WINDOW (window));
        old_ball_count = ball_count;

        /* Poll input */
        lrg_input_manager_poll (input_manager);

        /* Handle input */
        if (lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_SPACE) ||
            lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_ENTER))
        {
            spawn_random_ball_c ();
        }

        if (lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_R))
        {
            clear_balls_c ();
            /* Spawn initial balls again */
            for (i = 0; i < 5; i++)
            {
                spawn_random_ball_c ();
            }
        }

        if (lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_ESCAPE))
        {
            break;
        }

        /* Update ball count global if it changed */
        if (ball_count != old_ball_count)
        {
            g_value_init (&ball_count_value, G_TYPE_INT);
            g_value_set_int (&ball_count_value, ball_count);
            lrg_scripting_set_global (LRG_SCRIPTING (scripting), "ball_count",
                                       &ball_count_value, NULL);
            g_value_unset (&ball_count_value);
        }

        /* Update physics (C-side) */
        update_physics (delta);

        /* Update engine (calls JavaScript update hooks via GI) */
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

                ball_color = grl_color_new (balls[i].r, balls[i].g,
                                            balls[i].b, 255);
                grl_draw_circle ((gint)balls[i].x, (gint)balls[i].y,
                                 balls[i].radius, ball_color);
            }
        }

        /* Draw ball count */
        {
            g_autofree gchar *text = NULL;

            text = g_strdup_printf ("Balls: %d", ball_count);
            grl_draw_text (text, 10, 10, 20, white_color);
        }

        /* Draw instructions */
        grl_draw_text ("SPACE/ENTER: spawn | R: reset | ESC: quit",
                       10, WINDOW_HEIGHT - 30, 16, gray_color);

        grl_window_end_drawing (grl_window);
    }

    /* Shutdown */
    lrg_engine_shutdown (engine);

    return 0;
}
