/* scripted-game.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Example demonstrating the Lua scripting system in libregnum with 3D rendering.
 *
 * This example shows:
 * - Creating a scripting context
 * - Loading Lua scripts
 * - Registering C functions callable from Lua
 * - Using update hooks for per-frame game logic
 * - Passing data between C and Lua
 * - 3D rendering with LrgSphere3D and LrgText2D
 * - Using LrgRenderer with layer system (WORLD/UI)
 * - Isometric camera setup
 *
 * Controls:
 *   SPACE/ENTER - Spawn a new 3D sphere from Lua
 *   R           - Reset all spheres
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
#define SCENE_DEPTH    400
#define SCENE_WIDTH    400
#define SCENE_HEIGHT   300

/* =============================================================================
 * Ball Structure
 *
 * Simple bouncing ball managed by Lua (3D).
 * ========================================================================== */

typedef struct
{
    gfloat x, y, z;      /* 3D position */
    gfloat vx, vy, vz;   /* 3D velocity */
    gfloat radius;
    guint8 r, g, b;
    gboolean active;
} Ball;

static Ball balls[MAX_BALLS];
static gint  ball_count = 0;

/* =============================================================================
 * C Functions Exposed to Lua
 * ========================================================================== */

/**
 * spawn_ball:
 *
 * C function callable from Lua to spawn a new ball.
 * Lua signature: spawn_ball(x, y, z, vx, vy, vz, radius, r, g, b) -> ball_index
 */
static gboolean
spawn_ball (LrgScripting  *scripting,
            guint          n_args,
            const GValue  *args,
            GValue        *return_value,
            gpointer       user_data,
            GError       **error)
{
    gfloat x, y, z, vx, vy, vz, radius;
    gint   r, g, b;
    gint   i;

    if (n_args < 10)
    {
        g_set_error (error,
                     LRG_SCRIPTING_ERROR,
                     LRG_SCRIPTING_ERROR_FAILED,
                     "spawn_ball requires 10 arguments: x, y, z, vx, vy, vz, radius, r, g, b");
        return FALSE;
    }

    /* Extract arguments - Lua numbers come as INT64 or DOUBLE */
    x = (gfloat)(G_VALUE_HOLDS_DOUBLE (&args[0]) ?
                 g_value_get_double (&args[0]) :
                 g_value_get_int64 (&args[0]));
    y = (gfloat)(G_VALUE_HOLDS_DOUBLE (&args[1]) ?
                 g_value_get_double (&args[1]) :
                 g_value_get_int64 (&args[1]));
    z = (gfloat)(G_VALUE_HOLDS_DOUBLE (&args[2]) ?
                 g_value_get_double (&args[2]) :
                 g_value_get_int64 (&args[2]));
    vx = (gfloat)(G_VALUE_HOLDS_DOUBLE (&args[3]) ?
                  g_value_get_double (&args[3]) :
                  g_value_get_int64 (&args[3]));
    vy = (gfloat)(G_VALUE_HOLDS_DOUBLE (&args[4]) ?
                  g_value_get_double (&args[4]) :
                  g_value_get_int64 (&args[4]));
    vz = (gfloat)(G_VALUE_HOLDS_DOUBLE (&args[5]) ?
                  g_value_get_double (&args[5]) :
                  g_value_get_int64 (&args[5]));
    radius = (gfloat)(G_VALUE_HOLDS_DOUBLE (&args[6]) ?
                      g_value_get_double (&args[6]) :
                      g_value_get_int64 (&args[6]));
    r = (gint)(G_VALUE_HOLDS_DOUBLE (&args[7]) ?
               g_value_get_double (&args[7]) :
               g_value_get_int64 (&args[7]));
    g = (gint)(G_VALUE_HOLDS_DOUBLE (&args[8]) ?
               g_value_get_double (&args[8]) :
               g_value_get_int64 (&args[8]));
    b = (gint)(G_VALUE_HOLDS_DOUBLE (&args[9]) ?
               g_value_get_double (&args[9]) :
               g_value_get_int64 (&args[9]));

    /* Find an inactive slot */
    for (i = 0; i < MAX_BALLS; i++)
    {
        if (!balls[i].active)
        {
            balls[i].x = x;
            balls[i].y = y;
            balls[i].z = z;
            balls[i].vx = vx;
            balls[i].vy = vy;
            balls[i].vz = vz;
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
 * Lua signature: get_ball_count() -> count
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
 * Lua signature: clear_balls()
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
 * Returns the scene width (3D bounds).
 * Lua signature: get_screen_size() -> width
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
    g_value_set_int (return_value, SCENE_WIDTH);
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
    g_value_set_int (return_value, SCENE_HEIGHT);
    return TRUE;
}

/**
 * get_scene_depth:
 *
 * Returns the scene depth (z-axis bounds).
 */
static gboolean
get_scene_depth (LrgScripting  *scripting,
                 guint          n_args,
                 const GValue  *args,
                 GValue        *return_value,
                 gpointer       user_data,
                 GError       **error)
{
    g_value_init (return_value, G_TYPE_INT);
    g_value_set_int (return_value, SCENE_DEPTH);
    return TRUE;
}

/* =============================================================================
 * Lua Script
 *
 * This script is embedded for simplicity. In a real game, you'd load
 * this from a file using lrg_scripting_load_file().
 * ========================================================================== */

static const gchar *LUA_GAME_SCRIPT =
    "-- Scripted Game Logic\n"
    "-- This Lua code controls the bouncing 3D spheres\n"
    "\n"
    "-- Configuration\n"
    "local GRAVITY = 200\n"
    "local BOUNCE_DAMPENING = 0.8\n"
    "local SPAWN_SPEED = 150\n"
    "\n"
    "-- Ball state (mirrors C state for physics)\n"
    "local ball_velocities = {}\n"
    "\n"
    "-- Initialize\n"
    "function game_init()\n"
    "    Log.info('Lua 3D game script initialized!')\n"
    "    Log.info('Press SPACE to spawn spheres, R to reset')\n"
    "    \n"
    "    -- Spawn a few initial balls\n"
    "    for i = 1, 5 do\n"
    "        spawn_random_ball()\n"
    "    end\n"
    "end\n"
    "\n"
    "-- Spawn a ball at a random 3D position with random color\n"
    "function spawn_random_ball()\n"
    "    local width = get_screen_size()\n"
    "    local height = get_screen_height()\n"
    "    local depth = get_scene_depth()\n"
    "    \n"
    "    local x = math.random(20, width - 20)\n"
    "    local y = math.random(20, height - 20)\n"
    "    local z = math.random(20, depth - 20)\n"
    "    local vx = math.random(-SPAWN_SPEED, SPAWN_SPEED)\n"
    "    local vy = math.random(-SPAWN_SPEED, SPAWN_SPEED)\n"
    "    local vz = math.random(-SPAWN_SPEED, SPAWN_SPEED)\n"
    "    local radius = math.random(5, 15)\n"
    "    local r = math.random(50, 255)\n"
    "    local g = math.random(50, 255)\n"
    "    local b = math.random(50, 255)\n"
    "    \n"
    "    local idx = spawn_ball(x, y, z, vx, vy, vz, radius, r, g, b)\n"
    "    if idx >= 0 then\n"
    "        ball_velocities[idx] = {vx = vx, vy = vy, vz = vz}\n"
    "        Log.debug('Spawned sphere ' .. idx .. ' at (' .. x .. ', ' .. y .. ', ' .. z .. ')')\n"
    "    else\n"
    "        Log.warning('Could not spawn sphere - max reached!')\n"
    "    end\n"
    "    \n"
    "    return idx\n"
    "end\n"
    "\n"
    "-- Called when user presses SPACE\n"
    "function on_spawn_key()\n"
    "    spawn_random_ball()\n"
    "    local count = get_ball_count()\n"
    "    Log.info('Sphere count: ' .. count)\n"
    "end\n"
    "\n"
    "-- Called when user presses R\n"
    "function on_reset_key()\n"
    "    clear_balls()\n"
    "    ball_velocities = {}\n"
    "    Log.info('All spheres cleared!')\n"
    "    \n"
    "    -- Spawn initial balls again\n"
    "    for i = 1, 5 do\n"
    "        spawn_random_ball()\n"
    "    end\n"
    "end\n"
    "\n"
    "-- Per-frame update (registered as update hook)\n"
    "function game_update(delta)\n"
    "    -- Physics is handled in C for this example\n"
    "    -- But Lua could do additional game logic here\n"
    "end\n"
    "\n"
    "-- Call init on load\n"
    "game_init()\n";

/* =============================================================================
 * Physics Update (C-side)
 *
 * Updates ball positions with gravity and 3D bouncing.
 * ========================================================================== */

static void
update_physics (gfloat delta)
{
    gint   i;
    gfloat gravity = 200.0f;
    gfloat dampening = 0.8f;

    for (i = 0; i < MAX_BALLS; i++)
    {
        if (!balls[i].active)
            continue;

        /* Apply gravity (downward in Y) */
        balls[i].vy += gravity * delta;

        /* Update 3D position */
        balls[i].x += balls[i].vx * delta;
        balls[i].y += balls[i].vy * delta;
        balls[i].z += balls[i].vz * delta;

        /* Bounce off left/right walls (X axis) */
        if (balls[i].x - balls[i].radius < 0)
        {
            balls[i].x = balls[i].radius;
            balls[i].vx = -balls[i].vx * dampening;
        }
        else if (balls[i].x + balls[i].radius > SCENE_WIDTH)
        {
            balls[i].x = SCENE_WIDTH - balls[i].radius;
            balls[i].vx = -balls[i].vx * dampening;
        }

        /* Bounce off floor/ceiling (Y axis) */
        if (balls[i].y - balls[i].radius < 0)
        {
            balls[i].y = balls[i].radius;
            balls[i].vy = -balls[i].vy * dampening;
        }
        else if (balls[i].y + balls[i].radius > SCENE_HEIGHT)
        {
            balls[i].y = SCENE_HEIGHT - balls[i].radius;
            balls[i].vy = -balls[i].vy * dampening;
        }

        /* Bounce off front/back walls (Z axis) */
        if (balls[i].z - balls[i].radius < 0)
        {
            balls[i].z = balls[i].radius;
            balls[i].vz = -balls[i].vz * dampening;
        }
        else if (balls[i].z + balls[i].radius > SCENE_DEPTH)
        {
            balls[i].z = SCENE_DEPTH - balls[i].radius;
            balls[i].vz = -balls[i].vz * dampening;
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
    g_autoptr(LrgScriptingLua) scripting = NULL;
    g_autoptr(LrgGrlWindow) window = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GrlColor) bg_color = NULL;
    g_autoptr(GrlColor) white_color = NULL;
    g_autoptr(GrlColor) gray_color = NULL;
    g_autoptr(LrgCameraIsometric) camera = NULL;
    LrgEngine *engine = NULL;
    LrgInputManager *input_manager = NULL;
    LrgRenderer *renderer = NULL;
    gint i;

    /* Create window first */
    window = lrg_grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT,
                                 "Scripted Game - 3D Bouncing Spheres");
    lrg_window_set_target_fps (LRG_WINDOW (window), 60);

    /* Initialize engine with window */
    engine = lrg_engine_get_default ();
    lrg_engine_set_window (engine, LRG_WINDOW (window));

    if (!lrg_engine_startup (engine, &error))
    {
        g_printerr ("Failed to start engine: %s\n", error->message);
        return 1;
    }

    /* Get renderer and setup camera */
    renderer = lrg_engine_get_renderer (engine);
    camera = lrg_camera_isometric_new ();
    lrg_camera_isometric_set_zoom (camera, 0.008f);
    lrg_renderer_set_camera (renderer, LRG_CAMERA (camera));

    /* Focus camera on center of scene */
    lrg_camera_isometric_focus_on (camera,
                                   SCENE_WIDTH / 2.0f,
                                   SCENE_HEIGHT / 2.0f,
                                   SCENE_DEPTH / 2.0f);

    /* Get input manager */
    input_manager = lrg_input_manager_get_default ();

    /* Create scripting context */
    scripting = lrg_scripting_lua_new ();

    /* Attach scripting to engine */
    lrg_engine_set_scripting (engine, LRG_SCRIPTING (scripting));

    /* Register C functions that Lua can call */
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

    lrg_scripting_register_function (LRG_SCRIPTING (scripting),
                                     "get_scene_depth",
                                     get_scene_depth,
                                     NULL,
                                     &error);

    /* Load the Lua game script */
    if (!lrg_scripting_load_string (LRG_SCRIPTING (scripting),
                                    "game.lua",
                                    LUA_GAME_SCRIPT,
                                    &error))
    {
        g_printerr ("Failed to load Lua script: %s\n", error->message);
        return 1;
    }

    /* Register the update hook */
    lrg_scripting_lua_register_update_hook (scripting, "game_update");

    /* Initialize balls array */
    for (i = 0; i < MAX_BALLS; i++)
    {
        balls[i].active = FALSE;
    }

    /* Create reusable colors */
    bg_color = grl_color_new (30, 30, 40, 255);
    white_color = grl_color_new (255, 255, 255, 255);
    gray_color = grl_color_new (150, 150, 150, 255);

    g_print ("Scripted Game Example (3D)\n");
    g_print ("===========================\n");
    g_print ("Controls:\n");
    g_print ("  SPACE/ENTER - Spawn a new sphere\n");
    g_print ("  R           - Reset all spheres\n");
    g_print ("  SCROLL      - Zoom in/out\n");
    g_print ("  ESC         - Quit\n\n");

    /* Main loop */
    while (!lrg_window_should_close (LRG_WINDOW (window)))
    {
        gfloat delta;

        delta = lrg_window_get_frame_time (LRG_WINDOW (window));

        /* Poll input */
        lrg_input_manager_poll (input_manager);

        /* Handle scroll wheel zoom */
        {
            gfloat wheel = grl_input_get_mouse_wheel_move ();
            if (wheel != 0.0f)
            {
                gfloat current_zoom = lrg_camera_isometric_get_zoom (camera);
                gfloat new_zoom = current_zoom + wheel * 0.002f;

                /* Clamp zoom to reasonable range */
                if (new_zoom < 0.001f)
                    new_zoom = 0.001f;
                if (new_zoom > 0.1f)
                    new_zoom = 0.1f;

                lrg_camera_isometric_set_zoom (camera, new_zoom);
            }
        }

        /* Handle input */
        if (lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_SPACE) ||
            lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_ENTER))
        {
            /* Call Lua function to spawn ball */
            lrg_scripting_call_function (LRG_SCRIPTING (scripting),
                                         "on_spawn_key",
                                         NULL,
                                         0,
                                         NULL,
                                         NULL);
        }

        if (lrg_input_manager_is_key_pressed (input_manager, GRL_KEY_R))
        {
            /* Call Lua function to reset */
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

        /* Update engine (calls Lua update hooks) */
        lrg_engine_update (engine, delta);

        /* Render using LrgRenderer with layers */
        lrg_renderer_begin_frame (renderer);
        lrg_renderer_clear (renderer, bg_color);

        /* World layer - 3D spheres */
        lrg_renderer_begin_layer (renderer, LRG_RENDER_LAYER_WORLD);
        for (i = 0; i < MAX_BALLS; i++)
        {
            if (balls[i].active)
            {
                g_autoptr(GrlColor) ball_color = NULL;
                g_autoptr(LrgSphere3D) sphere = NULL;

                ball_color = grl_color_new (balls[i].r, balls[i].g,
                                            balls[i].b, 255);
                sphere = lrg_sphere3d_new_full (balls[i].x, balls[i].y,
                                                balls[i].z, balls[i].radius,
                                                ball_color);
                lrg_drawable_draw (LRG_DRAWABLE (sphere), delta);
            }
        }
        lrg_renderer_end_layer (renderer);

        /* UI layer - 2D text */
        lrg_renderer_begin_layer (renderer, LRG_RENDER_LAYER_UI);
        {
            g_autofree gchar *text = NULL;
            g_autoptr(LrgText2D) label = NULL;

            text = g_strdup_printf ("Spheres: %d", ball_count);
            label = lrg_text2d_new_full (10.0f, 10.0f, text, 20.0f, white_color);
            lrg_drawable_draw (LRG_DRAWABLE (label), delta);
        }
        {
            g_autoptr(LrgText2D) help_label = NULL;

            help_label = lrg_text2d_new_full (10.0f, (gfloat)(WINDOW_HEIGHT - 30),
                                              "SPACE/ENTER: spawn | R: reset | SCROLL: zoom | ESC: quit",
                                              16.0f, gray_color);
            lrg_drawable_draw (LRG_DRAWABLE (help_label), delta);
        }
        lrg_renderer_end_layer (renderer);

        lrg_renderer_end_frame (renderer);
    }

    /* Shutdown */
    lrg_engine_shutdown (engine);

    return 0;
}
