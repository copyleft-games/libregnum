/* game-vr-demo.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A VR demonstration showcasing libregnum's VR support.
 *
 * Features demonstrated:
 * - LrgVRService: VR backend abstraction interface
 * - LrgVRStub: Fallback implementation when no VR runtime
 * - LrgVRComfortSettings: Motion sickness mitigation options
 * - Stereo rendering with eye projection matrices
 * - HMD and controller pose tracking
 * - Controller input (buttons, thumbsticks, triggers)
 * - Haptic feedback
 * - Graceful fallback to keyboard/mouse when VR unavailable
 *
 * Controls (Keyboard fallback):
 *   WASD/Arrows - Move around
 *   Mouse       - Look around
 *   Space       - Grab nearest cube
 *   Tab         - Toggle comfort settings menu
 *   1-4         - Change comfort settings
 *   Escape      - Exit
 *
 * Controls (VR):
 *   Thumbsticks - Move/turn (based on comfort settings)
 *   Grip        - Grab cubes
 *   Trigger     - Haptic test
 *   B Button    - Toggle comfort menu
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

#define WINDOW_WIDTH    1280
#define WINDOW_HEIGHT   720
#define CUBE_COUNT      8
#define CUBE_SIZE       0.3f
#define GRAB_DISTANCE   0.5f
#define PLAYER_HEIGHT   1.7f
#define MOVE_SPEED      2.0f
#define TURN_SPEED      90.0f
#define SNAP_COOLDOWN   0.3f

/* =============================================================================
 * COLORS
 * ========================================================================== */

static GrlColor *color_background = NULL;
static GrlColor *color_floor = NULL;
static GrlColor *color_grid = NULL;
static GrlColor *color_cube_default = NULL;
static GrlColor *color_cube_highlight = NULL;
static GrlColor *color_cube_grabbed = NULL;
static GrlColor *color_left_hand = NULL;
static GrlColor *color_right_hand = NULL;
static GrlColor *color_text = NULL;
static GrlColor *color_text_dim = NULL;
static GrlColor *color_ui_bg = NULL;
static GrlColor *color_ui_highlight = NULL;
static GrlColor *color_warning = NULL;
static GrlColor *color_success = NULL;

static void
init_colors (void)
{
    color_background = grl_color_new (30, 30, 40, 255);
    color_floor = grl_color_new (60, 60, 70, 255);
    color_grid = grl_color_new (80, 80, 100, 255);
    color_cube_default = grl_color_new (100, 150, 200, 255);
    color_cube_highlight = grl_color_new (150, 200, 255, 255);
    color_cube_grabbed = grl_color_new (255, 200, 100, 255);
    color_left_hand = grl_color_new (100, 150, 255, 255);
    color_right_hand = grl_color_new (255, 100, 100, 255);
    color_text = grl_color_new (230, 230, 240, 255);
    color_text_dim = grl_color_new (150, 150, 170, 255);
    color_ui_bg = grl_color_new (30, 30, 50, 220);
    color_ui_highlight = grl_color_new (80, 80, 120, 255);
    color_warning = grl_color_new (255, 180, 80, 255);
    color_success = grl_color_new (100, 255, 150, 255);
}

static void
cleanup_colors (void)
{
    g_clear_pointer (&color_background, grl_color_free);
    g_clear_pointer (&color_floor, grl_color_free);
    g_clear_pointer (&color_grid, grl_color_free);
    g_clear_pointer (&color_cube_default, grl_color_free);
    g_clear_pointer (&color_cube_highlight, grl_color_free);
    g_clear_pointer (&color_cube_grabbed, grl_color_free);
    g_clear_pointer (&color_left_hand, grl_color_free);
    g_clear_pointer (&color_right_hand, grl_color_free);
    g_clear_pointer (&color_text, grl_color_free);
    g_clear_pointer (&color_text_dim, grl_color_free);
    g_clear_pointer (&color_ui_bg, grl_color_free);
    g_clear_pointer (&color_ui_highlight, grl_color_free);
    g_clear_pointer (&color_warning, grl_color_free);
    g_clear_pointer (&color_success, grl_color_free);
}

/* =============================================================================
 * CUBE DATA
 * ========================================================================== */

typedef struct
{
    gfloat x, y, z;
    gfloat rotation;
    gboolean grabbed;
    LrgVRHand grabbed_by;
} Cube;

/* =============================================================================
 * GAME STATE
 * ========================================================================== */

typedef struct
{
    /* VR system */
    LrgVRService         *vr_service;
    LrgVRComfortSettings *comfort;
    gboolean              vr_available;
    gboolean              hmd_present;
    guint                 render_width;
    guint                 render_height;

    /* Player state */
    gfloat player_x;
    gfloat player_y;
    gfloat player_z;
    gfloat player_yaw;
    gfloat player_pitch;

    /* Controller poses (from VR or simulated) */
    gfloat left_hand_x, left_hand_y, left_hand_z;
    gfloat right_hand_x, right_hand_y, right_hand_z;

    /* Controller button states */
    guint left_buttons;
    guint right_buttons;
    gfloat left_thumbstick_x, left_thumbstick_y;
    gfloat right_thumbstick_x, right_thumbstick_y;
    gfloat left_trigger;
    gfloat right_trigger;

    /* Input state tracking */
    gboolean left_grip_was_pressed;
    gboolean right_grip_was_pressed;
    gfloat snap_turn_cooldown;

    /* Scene objects */
    Cube cubes[CUBE_COUNT];
    gint cubes_grabbed_total;

    /* UI state */
    gboolean show_comfort_menu;
    gint comfort_menu_selection;

    /* Mouse look (fallback mode) */
    gboolean mouse_captured;
    gfloat last_mouse_x;
    gfloat last_mouse_y;
} GameState;

static GameState *g_game = NULL;

/* =============================================================================
 * INITIALIZATION
 * ========================================================================== */

static void
init_cubes (void)
{
    gint i;
    gfloat angle;

    /* Arrange cubes in a circle around the player */
    for (i = 0; i < CUBE_COUNT; i++)
    {
        angle = (gfloat)i / CUBE_COUNT * 2.0f * G_PI;
        g_game->cubes[i].x = cosf (angle) * 2.0f;
        g_game->cubes[i].y = 1.0f + (i % 3) * 0.5f; /* Varying heights */
        g_game->cubes[i].z = sinf (angle) * 2.0f;
        g_game->cubes[i].rotation = 0.0f;
        g_game->cubes[i].grabbed = FALSE;
    }
}

static void
init_vr (void)
{
    g_autoptr(GError) error = NULL;

    /*
     * Create VR service using the stub implementation.
     * In a real application, you would use an OpenXR or OpenVR backend.
     * The stub gracefully handles the case when no VR hardware is present.
     */
    g_game->vr_service = LRG_VR_SERVICE (lrg_vr_stub_new ());

    /* Attempt to initialize VR */
    if (lrg_vr_service_initialize (g_game->vr_service, &error))
    {
        g_game->vr_available = TRUE;
        g_game->hmd_present = lrg_vr_service_is_hmd_present (g_game->vr_service);

        /* Get recommended render target size */
        lrg_vr_service_get_recommended_render_size (g_game->vr_service,
                                                     &g_game->render_width,
                                                     &g_game->render_height);
    }
    else
    {
        /* VR not available - this is expected with the stub on systems without VR */
        g_game->vr_available = FALSE;
        g_game->hmd_present = FALSE;
        g_game->render_width = WINDOW_WIDTH / 2;
        g_game->render_height = WINDOW_HEIGHT;

        /* Log the reason (not an error, just informational) */
        g_message ("VR not available: %s. Using keyboard/mouse fallback.",
                   error ? error->message : "Unknown reason");
    }

    /* Create comfort settings with sensible defaults */
    g_game->comfort = lrg_vr_comfort_settings_new ();
    lrg_vr_comfort_settings_set_turn_mode (g_game->comfort, LRG_VR_TURN_MODE_SNAP);
    lrg_vr_comfort_settings_set_snap_turn_angle (g_game->comfort, 45.0f);
    lrg_vr_comfort_settings_set_locomotion_mode (g_game->comfort, LRG_VR_LOCOMOTION_SMOOTH);
    lrg_vr_comfort_settings_set_vignette_enabled (g_game->comfort, TRUE);
    lrg_vr_comfort_settings_set_vignette_intensity (g_game->comfort, 0.3f);
}

static void
game_init (void)
{
    g_game = g_new0 (GameState, 1);

    /* Initialize player position */
    g_game->player_x = 0.0f;
    g_game->player_y = 0.0f;
    g_game->player_z = 0.0f;
    g_game->player_yaw = 0.0f;
    g_game->player_pitch = 0.0f;

    /* Initialize VR */
    init_vr ();

    /* Initialize scene */
    init_cubes ();

    /* Initialize simulated hand positions (relative to player) */
    g_game->left_hand_x = -0.3f;
    g_game->left_hand_y = 1.0f;
    g_game->left_hand_z = -0.4f;
    g_game->right_hand_x = 0.3f;
    g_game->right_hand_y = 1.0f;
    g_game->right_hand_z = -0.4f;
}

static void
game_cleanup (void)
{
    if (g_game != NULL)
    {
        if (g_game->vr_service != NULL)
        {
            lrg_vr_service_shutdown (g_game->vr_service);
            g_clear_object (&g_game->vr_service);
        }
        g_clear_object (&g_game->comfort);
        g_free (g_game);
        g_game = NULL;
    }
}

/* =============================================================================
 * INPUT HANDLING
 * ========================================================================== */

static void
handle_vr_input (void)
{
    gfloat matrix[16];

    /* Poll VR events */
    lrg_vr_service_poll_events (g_game->vr_service);

    /* Get controller poses */
    lrg_vr_service_get_controller_pose (g_game->vr_service, LRG_VR_HAND_LEFT, matrix);
    /* Extract position from 4x4 matrix (column 3 = translation) */
    g_game->left_hand_x = matrix[12];
    g_game->left_hand_y = matrix[13];
    g_game->left_hand_z = matrix[14];

    lrg_vr_service_get_controller_pose (g_game->vr_service, LRG_VR_HAND_RIGHT, matrix);
    g_game->right_hand_x = matrix[12];
    g_game->right_hand_y = matrix[13];
    g_game->right_hand_z = matrix[14];

    /* Get button states */
    g_game->left_buttons = lrg_vr_service_get_controller_buttons (g_game->vr_service,
                                                                   LRG_VR_HAND_LEFT);
    g_game->right_buttons = lrg_vr_service_get_controller_buttons (g_game->vr_service,
                                                                    LRG_VR_HAND_RIGHT);

    /* Get axis values */
    g_game->left_thumbstick_x = lrg_vr_service_get_controller_axis (g_game->vr_service,
                                                                     LRG_VR_HAND_LEFT, 0);
    g_game->left_thumbstick_y = lrg_vr_service_get_controller_axis (g_game->vr_service,
                                                                     LRG_VR_HAND_LEFT, 1);
    g_game->left_trigger = lrg_vr_service_get_controller_axis (g_game->vr_service,
                                                                LRG_VR_HAND_LEFT, 2);

    g_game->right_thumbstick_x = lrg_vr_service_get_controller_axis (g_game->vr_service,
                                                                      LRG_VR_HAND_RIGHT, 0);
    g_game->right_thumbstick_y = lrg_vr_service_get_controller_axis (g_game->vr_service,
                                                                      LRG_VR_HAND_RIGHT, 1);
    g_game->right_trigger = lrg_vr_service_get_controller_axis (g_game->vr_service,
                                                                 LRG_VR_HAND_RIGHT, 2);
}

static void
handle_keyboard_input (gfloat delta)
{
    gfloat dx;
    gfloat dz;
    gfloat move_x;
    gfloat move_z;
    gfloat sin_yaw;
    gfloat cos_yaw;
    gint mouse_dx;
    gint mouse_dy;
    gfloat mouse_x;
    gfloat mouse_y;

    dx = 0.0f;
    dz = 0.0f;

    /* Movement */
    if (grl_input_is_key_down (GRL_KEY_W) || grl_input_is_key_down (GRL_KEY_UP))
        dz -= 1.0f;
    if (grl_input_is_key_down (GRL_KEY_S) || grl_input_is_key_down (GRL_KEY_DOWN))
        dz += 1.0f;
    if (grl_input_is_key_down (GRL_KEY_A) || grl_input_is_key_down (GRL_KEY_LEFT))
        dx -= 1.0f;
    if (grl_input_is_key_down (GRL_KEY_D) || grl_input_is_key_down (GRL_KEY_RIGHT))
        dx += 1.0f;

    /* Normalize diagonal movement */
    if (dx != 0 && dz != 0)
    {
        dx *= 0.707f;
        dz *= 0.707f;
    }

    /* Apply movement relative to player facing */
    sin_yaw = sinf (g_game->player_yaw * G_PI / 180.0f);
    cos_yaw = cosf (g_game->player_yaw * G_PI / 180.0f);

    move_x = dx * cos_yaw - dz * sin_yaw;
    move_z = dx * sin_yaw + dz * cos_yaw;

    g_game->player_x += move_x * MOVE_SPEED * delta;
    g_game->player_z += move_z * MOVE_SPEED * delta;

    /* Mouse look (hold right button) */
    if (grl_input_is_mouse_button_down (GRL_MOUSE_BUTTON_RIGHT))
    {
        mouse_x = grl_input_get_mouse_x ();
        mouse_y = grl_input_get_mouse_y ();

        if (g_game->mouse_captured)
        {
            mouse_dx = (gint)(mouse_x - g_game->last_mouse_x);
            mouse_dy = (gint)(mouse_y - g_game->last_mouse_y);

            g_game->player_yaw += mouse_dx * 0.2f;
            g_game->player_pitch -= mouse_dy * 0.2f;

            /* Clamp pitch */
            if (g_game->player_pitch > 89.0f)
                g_game->player_pitch = 89.0f;
            if (g_game->player_pitch < -89.0f)
                g_game->player_pitch = -89.0f;
        }

        g_game->last_mouse_x = mouse_x;
        g_game->last_mouse_y = mouse_y;
        g_game->mouse_captured = TRUE;
    }
    else
    {
        g_game->mouse_captured = FALSE;
    }

    /* Simulate thumbstick values from keyboard */
    g_game->left_thumbstick_x = dx;
    g_game->left_thumbstick_y = -dz;
    g_game->right_thumbstick_x = 0.0f;
    g_game->right_thumbstick_y = 0.0f;

    /* Simulate grab with space */
    if (grl_input_is_key_pressed (GRL_KEY_SPACE))
    {
        g_game->right_buttons |= LRG_VR_CONTROLLER_BUTTON_GRIP;
    }
    else if (grl_input_is_key_down (GRL_KEY_SPACE))
    {
        g_game->right_buttons |= LRG_VR_CONTROLLER_BUTTON_GRIP;
    }
    else
    {
        g_game->right_buttons &= ~LRG_VR_CONTROLLER_BUTTON_GRIP;
    }

    /* Update simulated hand positions */
    sin_yaw = sinf (g_game->player_yaw * G_PI / 180.0f);
    cos_yaw = cosf (g_game->player_yaw * G_PI / 180.0f);

    g_game->left_hand_x = g_game->player_x + (-0.3f * cos_yaw - (-0.4f) * sin_yaw);
    g_game->left_hand_y = PLAYER_HEIGHT * 0.6f;
    g_game->left_hand_z = g_game->player_z + (-0.3f * sin_yaw + (-0.4f) * cos_yaw);

    g_game->right_hand_x = g_game->player_x + (0.3f * cos_yaw - (-0.4f) * sin_yaw);
    g_game->right_hand_y = PLAYER_HEIGHT * 0.6f;
    g_game->right_hand_z = g_game->player_z + (0.3f * sin_yaw + (-0.4f) * cos_yaw);
}

static void
handle_comfort_menu_input (void)
{
    LrgVRTurnMode turn_mode;
    LrgVRLocomotionMode loco_mode;
    gfloat snap_angle;
    gboolean vignette;
    gfloat intensity;

    /* Toggle comfort menu */
    if (grl_input_is_key_pressed (GRL_KEY_TAB) ||
        (g_game->right_buttons & LRG_VR_CONTROLLER_BUTTON_B))
    {
        g_game->show_comfort_menu = !g_game->show_comfort_menu;
    }

    if (!g_game->show_comfort_menu)
        return;

    /* Navigate menu */
    if (grl_input_is_key_pressed (GRL_KEY_ONE))
    {
        /* Toggle turn mode */
        turn_mode = lrg_vr_comfort_settings_get_turn_mode (g_game->comfort);
        if (turn_mode == LRG_VR_TURN_MODE_SMOOTH)
            lrg_vr_comfort_settings_set_turn_mode (g_game->comfort, LRG_VR_TURN_MODE_SNAP);
        else
            lrg_vr_comfort_settings_set_turn_mode (g_game->comfort, LRG_VR_TURN_MODE_SMOOTH);
    }

    if (grl_input_is_key_pressed (GRL_KEY_TWO))
    {
        /* Cycle snap angle */
        snap_angle = lrg_vr_comfort_settings_get_snap_turn_angle (g_game->comfort);
        if (snap_angle >= 90.0f)
            snap_angle = 15.0f;
        else
            snap_angle += 15.0f;
        lrg_vr_comfort_settings_set_snap_turn_angle (g_game->comfort, snap_angle);
    }

    if (grl_input_is_key_pressed (GRL_KEY_THREE))
    {
        /* Toggle locomotion mode */
        loco_mode = lrg_vr_comfort_settings_get_locomotion_mode (g_game->comfort);
        if (loco_mode == LRG_VR_LOCOMOTION_SMOOTH)
            lrg_vr_comfort_settings_set_locomotion_mode (g_game->comfort, LRG_VR_LOCOMOTION_TELEPORT);
        else
            lrg_vr_comfort_settings_set_locomotion_mode (g_game->comfort, LRG_VR_LOCOMOTION_SMOOTH);
    }

    if (grl_input_is_key_pressed (GRL_KEY_FOUR))
    {
        /* Toggle vignette */
        vignette = lrg_vr_comfort_settings_get_vignette_enabled (g_game->comfort);
        if (vignette)
        {
            /* Cycle intensity or disable */
            intensity = lrg_vr_comfort_settings_get_vignette_intensity (g_game->comfort);
            if (intensity >= 0.9f)
            {
                lrg_vr_comfort_settings_set_vignette_enabled (g_game->comfort, FALSE);
            }
            else
            {
                lrg_vr_comfort_settings_set_vignette_intensity (g_game->comfort, intensity + 0.2f);
            }
        }
        else
        {
            lrg_vr_comfort_settings_set_vignette_enabled (g_game->comfort, TRUE);
            lrg_vr_comfort_settings_set_vignette_intensity (g_game->comfort, 0.3f);
        }
    }
}

static void
handle_input (gfloat delta)
{
    if (g_game->vr_available)
    {
        handle_vr_input ();
    }
    else
    {
        /* Clear button state before keyboard input */
        g_game->left_buttons = 0;
        g_game->right_buttons = 0;
        handle_keyboard_input (delta);
    }

    handle_comfort_menu_input ();
}

/* =============================================================================
 * CUBE INTERACTION
 * ========================================================================== */

static gfloat
distance_to_cube (gint cube_idx, gfloat hand_x, gfloat hand_y, gfloat hand_z)
{
    gfloat dx;
    gfloat dy;
    gfloat dz;
    Cube *cube;

    cube = &g_game->cubes[cube_idx];
    dx = cube->x - hand_x;
    dy = cube->y - hand_y;
    dz = cube->z - hand_z;

    return sqrtf (dx * dx + dy * dy + dz * dz);
}

static void
update_cube_grabbing (void)
{
    gint i;
    gint nearest;
    gfloat nearest_dist;
    gfloat dist;
    gboolean left_grip;
    gboolean right_grip;
    gboolean left_grip_pressed;
    gboolean right_grip_pressed;
    Cube *cube;

    left_grip = (g_game->left_buttons & LRG_VR_CONTROLLER_BUTTON_GRIP) != 0;
    right_grip = (g_game->right_buttons & LRG_VR_CONTROLLER_BUTTON_GRIP) != 0;

    left_grip_pressed = left_grip && !g_game->left_grip_was_pressed;
    right_grip_pressed = right_grip && !g_game->right_grip_was_pressed;

    /* Check for new grabs (right hand) */
    if (right_grip_pressed)
    {
        nearest = -1;
        nearest_dist = GRAB_DISTANCE;

        for (i = 0; i < CUBE_COUNT; i++)
        {
            if (g_game->cubes[i].grabbed)
                continue;

            dist = distance_to_cube (i, g_game->right_hand_x,
                                     g_game->right_hand_y,
                                     g_game->right_hand_z);
            if (dist < nearest_dist)
            {
                nearest = i;
                nearest_dist = dist;
            }
        }

        if (nearest >= 0)
        {
            g_game->cubes[nearest].grabbed = TRUE;
            g_game->cubes[nearest].grabbed_by = LRG_VR_HAND_RIGHT;
            g_game->cubes_grabbed_total++;

            /* Trigger haptic feedback */
            lrg_vr_service_trigger_haptic (g_game->vr_service, LRG_VR_HAND_RIGHT, 0.1f, 0.8f);
        }
    }

    /* Check for new grabs (left hand) */
    if (left_grip_pressed)
    {
        nearest = -1;
        nearest_dist = GRAB_DISTANCE;

        for (i = 0; i < CUBE_COUNT; i++)
        {
            if (g_game->cubes[i].grabbed)
                continue;

            dist = distance_to_cube (i, g_game->left_hand_x,
                                     g_game->left_hand_y,
                                     g_game->left_hand_z);
            if (dist < nearest_dist)
            {
                nearest = i;
                nearest_dist = dist;
            }
        }

        if (nearest >= 0)
        {
            g_game->cubes[nearest].grabbed = TRUE;
            g_game->cubes[nearest].grabbed_by = LRG_VR_HAND_LEFT;
            g_game->cubes_grabbed_total++;

            /* Trigger haptic feedback */
            lrg_vr_service_trigger_haptic (g_game->vr_service, LRG_VR_HAND_LEFT, 0.1f, 0.8f);
        }
    }

    /* Update grabbed cube positions */
    for (i = 0; i < CUBE_COUNT; i++)
    {
        cube = &g_game->cubes[i];
        if (!cube->grabbed)
            continue;

        if (cube->grabbed_by == LRG_VR_HAND_LEFT)
        {
            if (!left_grip)
            {
                cube->grabbed = FALSE;
                /* Small haptic on release */
                lrg_vr_service_trigger_haptic (g_game->vr_service, LRG_VR_HAND_LEFT, 0.05f, 0.3f);
            }
            else
            {
                cube->x = g_game->left_hand_x;
                cube->y = g_game->left_hand_y;
                cube->z = g_game->left_hand_z;
            }
        }
        else if (cube->grabbed_by == LRG_VR_HAND_RIGHT)
        {
            if (!right_grip)
            {
                cube->grabbed = FALSE;
                /* Small haptic on release */
                lrg_vr_service_trigger_haptic (g_game->vr_service, LRG_VR_HAND_RIGHT, 0.05f, 0.3f);
            }
            else
            {
                cube->x = g_game->right_hand_x;
                cube->y = g_game->right_hand_y;
                cube->z = g_game->right_hand_z;
            }
        }
    }

    /* Store previous grip state */
    g_game->left_grip_was_pressed = left_grip;
    g_game->right_grip_was_pressed = right_grip;
}

/* =============================================================================
 * UPDATE
 * ========================================================================== */

static void
update_cubes (gfloat delta)
{
    gint i;

    for (i = 0; i < CUBE_COUNT; i++)
    {
        /* Rotate cubes that aren't grabbed */
        if (!g_game->cubes[i].grabbed)
        {
            g_game->cubes[i].rotation += 30.0f * delta;
            if (g_game->cubes[i].rotation > 360.0f)
                g_game->cubes[i].rotation -= 360.0f;
        }
    }
}

static void
update_locomotion (gfloat delta)
{
    LrgVRTurnMode turn_mode;
    gfloat snap_angle;
    gfloat sin_yaw;
    gfloat cos_yaw;
    gfloat move_x;
    gfloat move_z;

    /* Update snap turn cooldown */
    if (g_game->snap_turn_cooldown > 0)
        g_game->snap_turn_cooldown -= delta;

    /* Get comfort settings */
    turn_mode = lrg_vr_comfort_settings_get_turn_mode (g_game->comfort);
    snap_angle = lrg_vr_comfort_settings_get_snap_turn_angle (g_game->comfort);

    /* Apply turning */
    if (turn_mode == LRG_VR_TURN_MODE_SMOOTH)
    {
        g_game->player_yaw += g_game->right_thumbstick_x * TURN_SPEED * delta;
    }
    else /* SNAP */
    {
        if (g_game->snap_turn_cooldown <= 0)
        {
            if (g_game->right_thumbstick_x > 0.5f)
            {
                g_game->player_yaw += snap_angle;
                g_game->snap_turn_cooldown = SNAP_COOLDOWN;
            }
            else if (g_game->right_thumbstick_x < -0.5f)
            {
                g_game->player_yaw -= snap_angle;
                g_game->snap_turn_cooldown = SNAP_COOLDOWN;
            }
        }
    }

    /* Apply movement (only in VR mode, keyboard mode handles this differently) */
    if (g_game->vr_available)
    {
        sin_yaw = sinf (g_game->player_yaw * G_PI / 180.0f);
        cos_yaw = cosf (g_game->player_yaw * G_PI / 180.0f);

        move_x = g_game->left_thumbstick_x * cos_yaw - g_game->left_thumbstick_y * sin_yaw;
        move_z = g_game->left_thumbstick_x * sin_yaw + g_game->left_thumbstick_y * cos_yaw;

        g_game->player_x += move_x * MOVE_SPEED * delta;
        g_game->player_z += move_z * MOVE_SPEED * delta;
    }
}

static void
update_trigger_haptic (void)
{
    /* Demonstrate trigger haptic - vibrate proportional to trigger press */
    if (g_game->left_trigger > 0.1f)
    {
        lrg_vr_service_trigger_haptic (g_game->vr_service, LRG_VR_HAND_LEFT,
                                        0.016f, g_game->left_trigger * 0.5f);
    }

    if (g_game->right_trigger > 0.1f)
    {
        lrg_vr_service_trigger_haptic (g_game->vr_service, LRG_VR_HAND_RIGHT,
                                        0.016f, g_game->right_trigger * 0.5f);
    }
}

static void
update (gfloat delta)
{
    update_cubes (delta);
    update_cube_grabbing ();
    update_locomotion (delta);
    update_trigger_haptic ();
}

/* =============================================================================
 * RENDERING - 3D SCENE
 * ========================================================================== */

static void
draw_floor (void)
{
    gint x;
    gint z;
    gint floor_size;
    gint px;
    gint pz;
    gint sx;
    gint sz;

    floor_size = 10;

    /* Draw floor tiles */
    for (z = -floor_size; z < floor_size; z++)
    {
        for (x = -floor_size; x < floor_size; x++)
        {
            /* Project 3D floor position to 2D screen */
            px = WINDOW_WIDTH / 2 + (gint)((x - g_game->player_x) * 30);
            pz = WINDOW_HEIGHT / 2 + (gint)((z - g_game->player_z) * 30);

            /* Simple top-down view with perspective hint */
            sx = 28 - abs (x) / 2;
            sz = 28 - abs (z) / 2;

            if (sx > 0 && sz > 0)
            {
                grl_draw_rectangle (px - sx / 2, pz - sz / 2, sx, sz, color_floor);
                grl_draw_rectangle_lines (px - sx / 2, pz - sz / 2, sx, sz, color_grid);
            }
        }
    }
}

static void
draw_cube_at (gfloat x, gfloat y, gfloat z, gfloat rotation, GrlColor *color)
{
    gfloat dx;
    gfloat dz;
    gint px;
    gint py;
    gint size;
    gfloat dist;

    (void)rotation; /* Would be used for actual 3D rendering */

    /* Relative to player */
    dx = x - g_game->player_x;
    dz = z - g_game->player_z;

    /* Simple perspective projection */
    dist = sqrtf (dx * dx + dz * dz);
    if (dist < 0.1f)
        dist = 0.1f;

    px = WINDOW_WIDTH / 2 + (gint)(dx * 100);
    py = WINDOW_HEIGHT / 2 + (gint)(dz * 100) - (gint)((y - 1.0f) * 100);

    /* Size based on distance (crude depth) */
    size = (gint)(CUBE_SIZE * 150 / (1.0f + dist * 0.3f));
    if (size < 5)
        size = 5;
    if (size > 100)
        size = 100;

    grl_draw_rectangle (px - size / 2, py - size / 2, size, size, color);
}

static void
draw_cubes (void)
{
    gint i;
    Cube *cube;
    GrlColor *color;
    gfloat dist;

    for (i = 0; i < CUBE_COUNT; i++)
    {
        cube = &g_game->cubes[i];

        if (cube->grabbed)
        {
            color = color_cube_grabbed;
        }
        else
        {
            /* Highlight cubes within grab range */
            dist = distance_to_cube (i, g_game->right_hand_x,
                                     g_game->right_hand_y,
                                     g_game->right_hand_z);
            if (dist < GRAB_DISTANCE)
                color = color_cube_highlight;
            else
                color = color_cube_default;
        }

        draw_cube_at (cube->x, cube->y, cube->z, cube->rotation, color);
    }
}

static void
draw_controllers (void)
{
    gint lx;
    gint ly;
    gint rx;
    gint ry;
    gint size;

    /* Project hand positions to screen */
    lx = WINDOW_WIDTH / 2 + (gint)((g_game->left_hand_x - g_game->player_x) * 100);
    ly = WINDOW_HEIGHT / 2 + (gint)((g_game->left_hand_z - g_game->player_z) * 100);

    rx = WINDOW_WIDTH / 2 + (gint)((g_game->right_hand_x - g_game->player_x) * 100);
    ry = WINDOW_HEIGHT / 2 + (gint)((g_game->right_hand_z - g_game->player_z) * 100);

    size = 15;

    /* Draw controller spheres */
    grl_draw_circle (lx, ly, size, color_left_hand);
    grl_draw_circle (rx, ry, size, color_right_hand);

    /* Show grip state */
    if (g_game->left_buttons & LRG_VR_CONTROLLER_BUTTON_GRIP)
    {
        grl_draw_circle (lx, ly, size + 5, color_cube_grabbed);
    }
    if (g_game->right_buttons & LRG_VR_CONTROLLER_BUTTON_GRIP)
    {
        grl_draw_circle (rx, ry, size + 5, color_cube_grabbed);
    }
}

static void
draw_vignette (void)
{
    gboolean enabled;
    gfloat intensity;
    gint border;
    guint8 alpha;
    GrlColor *vignette;

    enabled = lrg_vr_comfort_settings_get_vignette_enabled (g_game->comfort);
    if (!enabled)
        return;

    intensity = lrg_vr_comfort_settings_get_vignette_intensity (g_game->comfort);

    /* Only show vignette when moving */
    if (fabsf (g_game->left_thumbstick_x) < 0.1f &&
        fabsf (g_game->left_thumbstick_y) < 0.1f &&
        fabsf (g_game->right_thumbstick_x) < 0.1f)
    {
        return;
    }

    border = (gint)(intensity * 100);
    alpha = (guint8)(intensity * 200);
    vignette = grl_color_new (0, 0, 0, alpha);

    /* Draw vignette borders */
    grl_draw_rectangle (0, 0, border, WINDOW_HEIGHT, vignette);
    grl_draw_rectangle (WINDOW_WIDTH - border, 0, border, WINDOW_HEIGHT, vignette);
    grl_draw_rectangle (0, 0, WINDOW_WIDTH, border, vignette);
    grl_draw_rectangle (0, WINDOW_HEIGHT - border, WINDOW_WIDTH, border, vignette);

    grl_color_free (vignette);
}

/* =============================================================================
 * RENDERING - UI
 * ========================================================================== */

static void
draw_status_panel (void)
{
    gchar *status;
    gchar *controllers;
    gchar *grabbed;
    gint y;

    y = 20;

    /* VR Status */
    if (g_game->vr_available)
    {
        status = g_game->hmd_present ?
                 "VR: Active (HMD Connected)" :
                 "VR: Active (HMD Not Present)";
        grl_draw_text (status, 20, y, 16, color_success);
    }
    else
    {
        grl_draw_text ("VR: Not Available (Using Keyboard/Mouse)", 20, y, 16, color_warning);
    }
    y += 20;

    /* Render resolution */
    status = g_strdup_printf ("Render: %ux%u per eye", g_game->render_width, g_game->render_height);
    grl_draw_text (status, 20, y, 14, color_text_dim);
    g_free (status);
    y += 20;

    /* Controller state */
    controllers = g_strdup_printf ("L:[%s%s%s] R:[%s%s%s]",
                                    (g_game->left_buttons & LRG_VR_CONTROLLER_BUTTON_GRIP) ? "G" : "-",
                                    (g_game->left_buttons & LRG_VR_CONTROLLER_BUTTON_TRIGGER) ? "T" : "-",
                                    (g_game->left_buttons & LRG_VR_CONTROLLER_BUTTON_THUMBSTICK) ? "S" : "-",
                                    (g_game->right_buttons & LRG_VR_CONTROLLER_BUTTON_GRIP) ? "G" : "-",
                                    (g_game->right_buttons & LRG_VR_CONTROLLER_BUTTON_TRIGGER) ? "T" : "-",
                                    (g_game->right_buttons & LRG_VR_CONTROLLER_BUTTON_THUMBSTICK) ? "S" : "-");
    grl_draw_text (controllers, 20, y, 14, color_text);
    g_free (controllers);
    y += 20;

    /* Grab counter */
    grabbed = g_strdup_printf ("Cubes Grabbed: %d", g_game->cubes_grabbed_total);
    grl_draw_text (grabbed, 20, y, 14, color_cube_grabbed);
    g_free (grabbed);
}

static void
draw_comfort_menu (void)
{
    gint x;
    gint y;
    gint width;
    gint height;
    LrgVRTurnMode turn_mode;
    LrgVRLocomotionMode loco_mode;
    gfloat snap_angle;
    gboolean vignette;
    gfloat vignette_intensity;
    gchar *line;

    if (!g_game->show_comfort_menu)
        return;

    x = WINDOW_WIDTH - 320;
    y = 20;
    width = 300;
    height = 180;

    /* Background */
    grl_draw_rectangle (x, y, width, height, color_ui_bg);

    /* Title */
    grl_draw_text ("Comfort Settings", x + 10, y + 10, 18, color_text);
    y += 35;

    /* Get current settings */
    turn_mode = lrg_vr_comfort_settings_get_turn_mode (g_game->comfort);
    snap_angle = lrg_vr_comfort_settings_get_snap_turn_angle (g_game->comfort);
    loco_mode = lrg_vr_comfort_settings_get_locomotion_mode (g_game->comfort);
    vignette = lrg_vr_comfort_settings_get_vignette_enabled (g_game->comfort);
    vignette_intensity = lrg_vr_comfort_settings_get_vignette_intensity (g_game->comfort);

    /* Turn mode */
    line = g_strdup_printf ("[1] Turn: %s",
                            turn_mode == LRG_VR_TURN_MODE_SMOOTH ? "Smooth" : "Snap");
    grl_draw_text (line, x + 10, y, 14, color_text);
    g_free (line);
    y += 20;

    /* Snap angle */
    line = g_strdup_printf ("[2] Snap Angle: %.0f deg", snap_angle);
    grl_draw_text (line, x + 10, y, 14,
                   turn_mode == LRG_VR_TURN_MODE_SNAP ? color_text : color_text_dim);
    g_free (line);
    y += 20;

    /* Locomotion mode */
    line = g_strdup_printf ("[3] Locomotion: %s",
                            loco_mode == LRG_VR_LOCOMOTION_SMOOTH ? "Smooth" : "Teleport");
    grl_draw_text (line, x + 10, y, 14, color_text);
    g_free (line);
    y += 20;

    /* Vignette */
    if (vignette)
    {
        line = g_strdup_printf ("[4] Vignette: ON (%.0f%%)", vignette_intensity * 100);
    }
    else
    {
        line = g_strdup_printf ("[4] Vignette: OFF");
    }
    grl_draw_text (line, x + 10, y, 14, color_text);
    g_free (line);
    y += 25;

    /* Instructions */
    grl_draw_text ("Press keys 1-4 to change", x + 10, y, 12, color_text_dim);
}

static void
draw_controls_help (void)
{
    gint y;

    y = WINDOW_HEIGHT - 100;

    grl_draw_text ("Controls:", 20, y, 14, color_text);
    y += 18;

    if (g_game->vr_available)
    {
        grl_draw_text ("Thumbsticks: Move/Turn | Grip: Grab | Trigger: Haptic Test | B: Menu",
                       20, y, 12, color_text_dim);
    }
    else
    {
        grl_draw_text ("WASD: Move | Right-Click: Mouse Look | Space: Grab | Tab: Menu | Esc: Exit",
                       20, y, 12, color_text_dim);
    }
}

/* =============================================================================
 * RENDERING - STEREO (VR)
 * ========================================================================== */

static void
render_scene_for_eye (LrgVREye eye)
{
    gfloat proj_matrix[16];
    gfloat eye_matrix[16];
    gfloat hmd_matrix[16];

    (void)eye; /* Would be used for actual stereo rendering */

    /*
     * In a real VR implementation, you would:
     * 1. Set up a render target for this eye
     * 2. Get the projection matrix from VR service
     * 3. Combine HMD pose with eye-to-head offset
     * 4. Render the scene with these matrices
     * 5. Submit the texture to the VR compositor
     *
     * Here we just demonstrate the API calls.
     */

    lrg_vr_service_get_eye_projection (g_game->vr_service, eye,
                                        0.1f, 100.0f, proj_matrix);
    lrg_vr_service_get_eye_to_head (g_game->vr_service, eye, eye_matrix);
    lrg_vr_service_get_hmd_pose (g_game->vr_service, hmd_matrix);

    /*
     * In practice:
     * view_matrix = inverse(hmd_matrix * eye_matrix)
     * mvp = proj_matrix * view_matrix * model_matrix
     */
}

static void
render_stereo (void)
{
    g_autoptr(GError) error = NULL;

    if (!g_game->vr_available)
        return;

    /* Render left eye */
    render_scene_for_eye (LRG_VR_EYE_LEFT);
    /* In real code: bind left eye texture, render scene */
    lrg_vr_service_submit_frame (g_game->vr_service, LRG_VR_EYE_LEFT, 0, &error);

    /* Render right eye */
    render_scene_for_eye (LRG_VR_EYE_RIGHT);
    /* In real code: bind right eye texture, render scene */
    lrg_vr_service_submit_frame (g_game->vr_service, LRG_VR_EYE_RIGHT, 0, &error);
}

/* =============================================================================
 * MAIN RENDER
 * ========================================================================== */

static void
render (void)
{
    /* Draw 3D scene (simplified 2D representation) */
    draw_floor ();
    draw_cubes ();
    draw_controllers ();
    draw_vignette ();

    /* Draw UI */
    draw_status_panel ();
    draw_comfort_menu ();
    draw_controls_help ();

    /* Stereo rendering (VR) */
    render_stereo ();
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

    (void)argc;
    (void)argv;

    /* Initialize window */
    window = grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT, "VR Demo - Libregnum");
    grl_window_set_target_fps (window, 90); /* VR typically runs at 90 Hz */

    /* Initialize */
    init_colors ();
    game_init ();

    /* Main game loop */
    while (!grl_window_should_close (window))
    {
        delta = grl_window_get_frame_time (window);

        /* Exit on Escape */
        if (grl_input_is_key_pressed (GRL_KEY_ESCAPE))
            break;

        /* Input */
        handle_input (delta);

        /* Update */
        update (delta);

        /* Render */
        grl_window_begin_drawing (window);
        grl_draw_clear_background (color_background);

        render ();

        grl_draw_fps (WINDOW_WIDTH - 100, 10);

        grl_window_end_drawing (window);
    }

    /* Cleanup */
    game_cleanup ();
    cleanup_colors ();
    g_object_unref (window);

    return 0;
}
