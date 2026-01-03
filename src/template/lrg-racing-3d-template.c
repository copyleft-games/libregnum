/* lrg-racing-3d-template.c - 3D racing game template implementation
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "../config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TEMPLATE

#include "lrg-racing-3d-template.h"
#include "lrg-game-3d-template-private.h"
#include "../lrg-log.h"
#include "../graphics/lrg-window.h"

#include <graylib.h>
#include <raylib.h>
#include <rlgl.h>
#include <math.h>

/* ==========================================================================
 * Default Constants
 * ========================================================================== */

#define DEFAULT_MAX_SPEED           80.0f
#define DEFAULT_ACCELERATION        40.0f
#define DEFAULT_BRAKE_POWER         60.0f
#define DEFAULT_STEERING_SPEED      120.0f
#define DEFAULT_GRIP                0.85f
#define DEFAULT_DRAG                0.98f
#define DEFAULT_GRAVITY             30.0f

#define DEFAULT_BOOST_SPEED         1.5f
#define DEFAULT_BOOST_DRAIN         0.3f

#define DEFAULT_CHASE_DISTANCE      8.0f
#define DEFAULT_CHASE_HEIGHT        3.0f
#define DEFAULT_CHASE_LOOK_AHEAD    2.0f
#define DEFAULT_CAMERA_SMOOTHING    0.1f

#define DEFAULT_COUNTDOWN_DURATION  3.0f

#define DEFAULT_TOTAL_LAPS          3
#define DEFAULT_TOTAL_CHECKPOINTS   4
#define DEFAULT_TOTAL_RACERS        1

#define VEHICLE_LENGTH              2.0f
#define VEHICLE_WIDTH               1.0f
#define VEHICLE_HEIGHT              0.5f

/* ==========================================================================
 * Property IDs
 * ========================================================================== */

enum
{
    PROP_0,

    /* Vehicle */
    PROP_MAX_SPEED,
    PROP_ACCELERATION,
    PROP_BRAKE_POWER,
    PROP_STEERING_SPEED,
    PROP_GRIP,

    /* Boost */
    PROP_BOOST,
    PROP_BOOST_SPEED,

    /* Camera */
    PROP_CAMERA_MODE,
    PROP_CHASE_DISTANCE,
    PROP_CHASE_HEIGHT,

    /* Race */
    PROP_RACE_STATE,
    PROP_TOTAL_LAPS,
    PROP_TOTAL_CHECKPOINTS,
    PROP_TOTAL_RACERS,
    PROP_RACE_POSITION,

    /* HUD */
    PROP_SPEEDOMETER_VISIBLE,
    PROP_MINIMAP_VISIBLE,

    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Signal IDs
 * ========================================================================== */

enum
{
    SIGNAL_RACE_STATE_CHANGED,
    SIGNAL_LAP_COMPLETE,
    SIGNAL_CHECKPOINT_REACHED,
    SIGNAL_COLLISION,
    SIGNAL_BOOST_ACTIVATED,

    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct _LrgRacing3DTemplatePrivate
{
    /* Vehicle position and rotation */
    gfloat vehicle_x;
    gfloat vehicle_y;
    gfloat vehicle_z;
    gfloat vehicle_rotation;        /* Y rotation (heading) in degrees */

    /* Vehicle velocity */
    gfloat velocity_x;
    gfloat velocity_y;
    gfloat velocity_z;
    gfloat speed;                   /* Current speed (magnitude) */

    /* Vehicle settings */
    gfloat max_speed;
    gfloat acceleration;
    gfloat brake_power;
    gfloat steering_speed;
    gfloat grip;
    gfloat drag;
    gfloat gravity;

    /* Current input steering */
    gfloat steering_angle;

    /* Boost */
    gfloat boost;
    gfloat boost_speed;
    gfloat boost_drain;
    gboolean is_boosting;

    /* Camera */
    LrgRacing3DCameraMode camera_mode;
    gfloat chase_distance;
    gfloat chase_height;
    gfloat chase_look_ahead;
    gfloat camera_smoothing;

    /* Camera position (for smoothing) */
    gfloat camera_current_x;
    gfloat camera_current_y;
    gfloat camera_current_z;
    gfloat camera_yaw;

    /* Race state */
    LrgRacing3DRaceState race_state;
    gfloat countdown_timer;
    gint countdown_value;

    /* Race progress */
    gint current_lap;
    gint total_laps;
    gfloat race_time;
    gfloat lap_time;
    gfloat best_lap_time;

    /* Checkpoints */
    gint current_checkpoint;
    gint total_checkpoints;

    /* Position */
    gint race_position;
    gint total_racers;

    /* State flags */
    gboolean is_grounded;
    gboolean is_accelerating;
    gboolean is_braking;
    gboolean is_reversing;

    /* HUD */
    gboolean speedometer_visible;
    gboolean minimap_visible;

} LrgRacing3DTemplatePrivate;

/* ==========================================================================
 * Type Definition
 * ========================================================================== */

G_DEFINE_TYPE_WITH_PRIVATE (LrgRacing3DTemplate, lrg_racing_3d_template,
                            LRG_TYPE_GAME_3D_TEMPLATE)

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

static gfloat
lerp (gfloat a,
      gfloat b,
      gfloat t)
{
    return a + (b - a) * t;
}

static gfloat
normalize_angle (gfloat angle)
{
    while (angle < 0.0f)
        angle += 360.0f;
    while (angle >= 360.0f)
        angle -= 360.0f;
    return angle;
}

static gchar *
format_time (gfloat seconds)
{
    gint minutes;
    gint secs;
    gint ms;

    minutes = (gint)(seconds / 60.0f);
    secs = (gint)seconds % 60;
    ms = (gint)((seconds - floorf (seconds)) * 1000.0f);

    return g_strdup_printf ("%d:%02d.%03d", minutes, secs, ms);
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static void
lrg_racing_3d_template_real_on_race_state_changed (LrgRacing3DTemplate  *self,
                                                   LrgRacing3DRaceState  old_state,
                                                   LrgRacing3DRaceState  new_state)
{
    /* Default: no action */
}

static void
lrg_racing_3d_template_real_on_lap_complete (LrgRacing3DTemplate *self,
                                             gint                 lap,
                                             gfloat               lap_time)
{
    /* Default: no action */
}

static void
lrg_racing_3d_template_real_on_checkpoint_reached (LrgRacing3DTemplate *self,
                                                   gint                 checkpoint)
{
    /* Default: no action */
}

static void
lrg_racing_3d_template_real_on_collision (LrgRacing3DTemplate *self,
                                          gfloat               impact_force,
                                          gfloat               normal_x,
                                          gfloat               normal_y,
                                          gfloat               normal_z)
{
    /* Default: no action */
}

static void
lrg_racing_3d_template_real_on_boost_activated (LrgRacing3DTemplate *self)
{
    /* Default: no action */
}

static void
lrg_racing_3d_template_real_update_vehicle (LrgRacing3DTemplate *self,
                                            gdouble              delta)
{
    LrgRacing3DTemplatePrivate *priv;
    LrgRacing3DTemplateClass *klass;
    gfloat throttle;
    gfloat brake;
    gfloat steering;
    gfloat accel_amount;
    gfloat target_speed;
    gfloat rotation_rad;
    gfloat forward_x;
    gfloat forward_z;

    priv = lrg_racing_3d_template_get_instance_private (self);
    klass = LRG_RACING_3D_TEMPLATE_GET_CLASS (self);

    /* Don't update if not racing */
    if (priv->race_state != LRG_RACING_3D_RACE_STATE_RACING)
        return;

    /* Get input */
    throttle = 0.0f;
    brake = 0.0f;
    steering = 0.0f;

    if (IsKeyDown (KEY_W) || IsKeyDown (KEY_UP))
        throttle = 1.0f;
    if (IsKeyDown (KEY_S) || IsKeyDown (KEY_DOWN))
        brake = 1.0f;
    if (IsKeyDown (KEY_A) || IsKeyDown (KEY_LEFT))
        steering = -1.0f;
    if (IsKeyDown (KEY_D) || IsKeyDown (KEY_RIGHT))
        steering = 1.0f;

    /* Gamepad input */
    if (IsGamepadAvailable (0))
    {
        gfloat rt;
        gfloat lt;
        gfloat stick_x;

        rt = GetGamepadAxisMovement (0, GAMEPAD_AXIS_RIGHT_TRIGGER);
        lt = GetGamepadAxisMovement (0, GAMEPAD_AXIS_LEFT_TRIGGER);
        stick_x = GetGamepadAxisMovement (0, GAMEPAD_AXIS_LEFT_X);

        /* Triggers are -1 to 1, normalize to 0 to 1 */
        rt = (rt + 1.0f) * 0.5f;
        lt = (lt + 1.0f) * 0.5f;

        if (rt > throttle)
            throttle = rt;
        if (lt > brake)
            brake = lt;

        if (fabsf (stick_x) > 0.2f)
            steering = stick_x;
    }

    /* Boost input */
    if (priv->boost > 0.0f &&
        (IsKeyDown (KEY_LEFT_SHIFT) ||
         (IsGamepadAvailable (0) && IsGamepadButtonDown (0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))))
    {
        if (!priv->is_boosting && priv->boost > 0.1f)
        {
            priv->is_boosting = TRUE;

            if (klass->on_boost_activated != NULL)
                klass->on_boost_activated (self);

            g_signal_emit (self, signals[SIGNAL_BOOST_ACTIVATED], 0);
        }
    }
    else
    {
        priv->is_boosting = FALSE;
    }

    /* Update boost */
    if (priv->is_boosting)
    {
        priv->boost -= priv->boost_drain * (gfloat)delta;
        if (priv->boost < 0.0f)
        {
            priv->boost = 0.0f;
            priv->is_boosting = FALSE;
        }
    }

    priv->is_accelerating = throttle > 0.1f;
    priv->is_braking = brake > 0.1f;

    /* Calculate target speed */
    target_speed = priv->max_speed;
    if (priv->is_boosting)
        target_speed *= priv->boost_speed;

    /* Steering (only effective when moving) */
    if (fabsf (priv->speed) > 1.0f)
    {
        gfloat steering_factor;

        /* Reduce steering at high speeds */
        steering_factor = 1.0f - (fabsf (priv->speed) / target_speed) * 0.3f;
        steering_factor = CLAMP (steering_factor, 0.5f, 1.0f);

        priv->vehicle_rotation += steering * priv->steering_speed * steering_factor * (gfloat)delta;
        priv->vehicle_rotation = normalize_angle (priv->vehicle_rotation);
    }

    /* Calculate forward direction */
    rotation_rad = priv->vehicle_rotation * (gfloat)(G_PI / 180.0);
    forward_x = sinf (rotation_rad);
    forward_z = cosf (rotation_rad);

    /* Acceleration */
    if (priv->is_accelerating && priv->speed < target_speed)
    {
        accel_amount = priv->acceleration;
        if (priv->is_boosting)
            accel_amount *= 1.5f;

        priv->speed += accel_amount * (gfloat)delta;
        if (priv->speed > target_speed)
            priv->speed = target_speed;
    }

    /* Braking */
    if (priv->is_braking)
    {
        priv->speed -= priv->brake_power * (gfloat)delta;

        /* Allow reversing */
        if (priv->speed < -priv->max_speed * 0.3f)
            priv->speed = -priv->max_speed * 0.3f;

        priv->is_reversing = priv->speed < 0.0f;
    }
    else
    {
        priv->is_reversing = FALSE;
    }

    /* Apply drag when not accelerating */
    if (!priv->is_accelerating && !priv->is_braking)
    {
        priv->speed *= priv->drag;
        if (fabsf (priv->speed) < 0.1f)
            priv->speed = 0.0f;
    }

    /* Apply velocity */
    priv->velocity_x = forward_x * priv->speed;
    priv->velocity_z = forward_z * priv->speed;

    /* Apply grip (blend current velocity towards forward direction) */
    priv->vehicle_x += priv->velocity_x * (gfloat)delta;
    priv->vehicle_z += priv->velocity_z * (gfloat)delta;

    /* Apply gravity */
    if (!priv->is_grounded)
    {
        priv->velocity_y -= priv->gravity * (gfloat)delta;
        priv->vehicle_y += priv->velocity_y * (gfloat)delta;
    }

    /* Ground check (simple floor at y=0) */
    if (priv->vehicle_y <= 0.0f)
    {
        priv->vehicle_y = 0.0f;
        priv->velocity_y = 0.0f;
        priv->is_grounded = TRUE;
    }
    else
    {
        priv->is_grounded = FALSE;
    }

    /* Update race time */
    priv->race_time += (gfloat)delta;
    priv->lap_time += (gfloat)delta;
}

static void
lrg_racing_3d_template_real_update_chase_camera (LrgRacing3DTemplate *self,
                                                 gdouble              delta)
{
    LrgRacing3DTemplatePrivate *priv;
    LrgGame3DTemplatePrivate *parent_priv;
    gfloat target_x;
    gfloat target_y;
    gfloat target_z;
    gfloat rotation_rad;
    gfloat look_x;
    gfloat look_y;
    gfloat look_z;

    priv = lrg_racing_3d_template_get_instance_private (self);
    parent_priv = lrg_game_3d_template_get_private (LRG_GAME_3D_TEMPLATE (self));

    rotation_rad = priv->vehicle_rotation * (gfloat)(G_PI / 180.0);

    switch (priv->camera_mode)
    {
        case LRG_RACING_3D_CAMERA_CHASE:
            /* Camera behind the vehicle */
            target_x = priv->vehicle_x - sinf (rotation_rad) * priv->chase_distance;
            target_y = priv->vehicle_y + priv->chase_height;
            target_z = priv->vehicle_z - cosf (rotation_rad) * priv->chase_distance;
            break;

        case LRG_RACING_3D_CAMERA_HOOD:
            /* Camera on hood */
            target_x = priv->vehicle_x + sinf (rotation_rad) * 1.5f;
            target_y = priv->vehicle_y + 1.0f;
            target_z = priv->vehicle_z + cosf (rotation_rad) * 1.5f;
            break;

        case LRG_RACING_3D_CAMERA_BUMPER:
            /* Camera at front bumper */
            target_x = priv->vehicle_x + sinf (rotation_rad) * 2.0f;
            target_y = priv->vehicle_y + 0.5f;
            target_z = priv->vehicle_z + cosf (rotation_rad) * 2.0f;
            break;

        case LRG_RACING_3D_CAMERA_COCKPIT:
            /* Camera inside vehicle */
            target_x = priv->vehicle_x + sinf (rotation_rad) * 0.5f;
            target_y = priv->vehicle_y + 0.8f;
            target_z = priv->vehicle_z + cosf (rotation_rad) * 0.5f;
            break;

        case LRG_RACING_3D_CAMERA_ORBIT:
        default:
            /* Use parent mouse look for orbit camera */
            return;
    }

    /* Smooth camera movement */
    priv->camera_current_x = lerp (priv->camera_current_x, target_x, priv->camera_smoothing);
    priv->camera_current_y = lerp (priv->camera_current_y, target_y, priv->camera_smoothing);
    priv->camera_current_z = lerp (priv->camera_current_z, target_z, priv->camera_smoothing);

    /* Update parent camera */
    if (parent_priv->camera != NULL)
    {
        lrg_camera3d_set_position_xyz (parent_priv->camera,
                                       priv->camera_current_x,
                                       priv->camera_current_y,
                                       priv->camera_current_z);

        /* Look at vehicle (slightly ahead) */
        look_x = priv->vehicle_x + sinf (rotation_rad) * priv->chase_look_ahead;
        look_y = priv->vehicle_y + 0.5f;
        look_z = priv->vehicle_z + cosf (rotation_rad) * priv->chase_look_ahead;

        lrg_camera3d_set_target_xyz (parent_priv->camera, look_x, look_y, look_z);
    }

    parent_priv->position_x = priv->camera_current_x;
    parent_priv->position_y = priv->camera_current_y;
    parent_priv->position_z = priv->camera_current_z;
}

static void
lrg_racing_3d_template_real_check_checkpoints (LrgRacing3DTemplate *self)
{
    /* Default: no checkpoint checking (must be overridden for actual tracks) */
}

static void
lrg_racing_3d_template_real_draw_vehicle (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;
    Vector3 position;
    Vector3 size;
    Color color;
    gfloat rotation_rad;

    priv = lrg_racing_3d_template_get_instance_private (self);

    position.x = priv->vehicle_x;
    position.y = priv->vehicle_y + VEHICLE_HEIGHT * 0.5f;
    position.z = priv->vehicle_z;

    size.x = VEHICLE_WIDTH;
    size.y = VEHICLE_HEIGHT;
    size.z = VEHICLE_LENGTH;

    /* Color based on state */
    if (priv->is_boosting)
        color = (Color){ 255, 100, 0, 255 };    /* Orange when boosting */
    else if (priv->is_braking)
        color = (Color){ 255, 50, 50, 255 };    /* Red when braking */
    else if (priv->is_accelerating)
        color = (Color){ 50, 255, 50, 255 };    /* Green when accelerating */
    else
        color = (Color){ 100, 100, 200, 255 };  /* Blue default */

    /* Draw rotated box */
    {
        gfloat rl_rotation;

        rl_rotation = priv->vehicle_rotation;
        rotation_rad = rl_rotation * (gfloat)(G_PI / 180.0);

        /* Use raylib's rotated cube drawing */
        rlPushMatrix ();
        rlTranslatef (position.x, position.y, position.z);
        rlRotatef (rl_rotation, 0.0f, 1.0f, 0.0f);
        DrawCubeV ((Vector3){0, 0, 0}, size, color);
        DrawCubeWiresV ((Vector3){0, 0, 0}, size, WHITE);
        rlPopMatrix ();
    }

    /* Draw direction indicator on top */
    {
        Vector3 front_start;
        Vector3 front_end;

        rotation_rad = priv->vehicle_rotation * (gfloat)(G_PI / 180.0);

        front_start.x = priv->vehicle_x;
        front_start.y = priv->vehicle_y + VEHICLE_HEIGHT + 0.1f;
        front_start.z = priv->vehicle_z;

        front_end.x = front_start.x + sinf (rotation_rad) * 1.5f;
        front_end.y = front_start.y;
        front_end.z = front_start.z + cosf (rotation_rad) * 1.5f;

        DrawLine3D (front_start, front_end, YELLOW);
    }
}

static void
lrg_racing_3d_template_real_draw_track (LrgRacing3DTemplate *self)
{
    /* Draw a simple placeholder track */
    grl_draw_grid (50, 2.0f);

    /* Draw some track markers */
    DrawCube ((Vector3){-20, 0.5f, 0}, 1, 1, 1, RED);
    DrawCube ((Vector3){20, 0.5f, 0}, 1, 1, 1, GREEN);
    DrawCube ((Vector3){0, 0.5f, -20}, 1, 1, 1, BLUE);
    DrawCube ((Vector3){0, 0.5f, 20}, 1, 1, 1, YELLOW);
}

static void
lrg_racing_3d_template_real_draw_speedometer (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;
    gint screen_width;
    gint screen_height;
    gint x;
    gint y;
    gint width;
    gint height;
    gint speed_width;
    gfloat speed_percent;
    g_autofree gchar *speed_text = NULL;

    priv = lrg_racing_3d_template_get_instance_private (self);

    if (!priv->speedometer_visible)
        return;

    screen_width = GetScreenWidth ();
    screen_height = GetScreenHeight ();

    /* Speedometer bar */
    width = 200;
    height = 30;
    x = screen_width - width - 20;
    y = screen_height - height - 20;

    speed_percent = fabsf (priv->speed) / priv->max_speed;
    if (priv->is_boosting)
        speed_percent = fabsf (priv->speed) / (priv->max_speed * priv->boost_speed);
    speed_percent = CLAMP (speed_percent, 0.0f, 1.0f);
    speed_width = (gint)(speed_percent * width);

    DrawRectangle (x, y, width, height, DARKGRAY);
    DrawRectangle (x, y, speed_width, height, priv->is_boosting ? ORANGE : GREEN);
    DrawRectangleLines (x, y, width, height, WHITE);

    /* Speed text */
    speed_text = g_strdup_printf ("%d km/h", (gint)(fabsf (priv->speed) * 3.6f));
    DrawText (speed_text, x + 5, y + 7, 16, WHITE);

    /* Boost bar */
    if (priv->boost > 0.0f)
    {
        gint boost_width;

        y -= 20;
        boost_width = (gint)(priv->boost * width);

        DrawRectangle (x, y, width, 15, DARKGRAY);
        DrawRectangle (x, y, boost_width, 15, ORANGE);
        DrawRectangleLines (x, y, width, 15, WHITE);
        DrawText ("NITRO", x + 5, y + 1, 12, WHITE);
    }
}

static void
lrg_racing_3d_template_real_draw_minimap (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;
    gint screen_width;
    gint x;
    gint y;
    gint size;
    gint center_x;
    gint center_y;
    gint player_x;
    gint player_y;
    gfloat scale;

    priv = lrg_racing_3d_template_get_instance_private (self);

    if (!priv->minimap_visible)
        return;

    screen_width = GetScreenWidth ();

    /* Minimap background */
    size = 150;
    x = screen_width - size - 20;
    y = 20;

    DrawRectangle (x, y, size, size, (Color){30, 30, 30, 200});
    DrawRectangleLines (x, y, size, size, WHITE);

    /* Draw player position */
    center_x = x + size / 2;
    center_y = y + size / 2;
    scale = 1.0f;  /* World units to minimap pixels */

    player_x = center_x + (gint)(priv->vehicle_x * scale);
    player_y = center_y - (gint)(priv->vehicle_z * scale);

    /* Clamp to minimap bounds */
    player_x = CLAMP (player_x, x + 5, x + size - 5);
    player_y = CLAMP (player_y, y + 5, y + size - 5);

    DrawCircle (player_x, player_y, 4, GREEN);

    /* Draw direction indicator */
    {
        gfloat rot_rad;
        gint dir_x;
        gint dir_y;

        rot_rad = priv->vehicle_rotation * (gfloat)(G_PI / 180.0);
        dir_x = player_x + (gint)(sinf (rot_rad) * 8);
        dir_y = player_y - (gint)(cosf (rot_rad) * 8);

        DrawLine (player_x, player_y, dir_x, dir_y, YELLOW);
    }
}

static void
lrg_racing_3d_template_real_draw_race_hud (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;
    gint screen_width;
    gint screen_height;
    g_autofree gchar *lap_text = NULL;
    g_autofree gchar *pos_text = NULL;
    g_autofree gchar *time_text = NULL;
    g_autofree gchar *best_text = NULL;

    priv = lrg_racing_3d_template_get_instance_private (self);

    screen_width = GetScreenWidth ();
    screen_height = GetScreenHeight ();

    /* Lap counter */
    lap_text = g_strdup_printf ("Lap %d/%d", priv->current_lap, priv->total_laps);
    DrawText (lap_text, 20, 20, 24, WHITE);

    /* Position */
    pos_text = g_strdup_printf ("%d/%d", priv->race_position, priv->total_racers);
    DrawText (pos_text, 20, 50, 24, WHITE);

    /* Race time */
    time_text = format_time (priv->race_time);
    DrawText (time_text, 20, 80, 20, WHITE);

    /* Best lap */
    if (priv->best_lap_time >= 0.0f)
    {
        best_text = format_time (priv->best_lap_time);
        DrawText ("Best:", 20, 110, 16, GRAY);
        DrawText (best_text, 70, 110, 16, YELLOW);
    }

    /* Countdown */
    if (priv->race_state == LRG_RACING_3D_RACE_STATE_COUNTDOWN)
    {
        const gchar *countdown_str;
        gint text_width;
        Color color;

        if (priv->countdown_value > 0)
        {
            countdown_str = g_strdup_printf ("%d", priv->countdown_value);
            color = WHITE;
        }
        else
        {
            countdown_str = "GO!";
            color = GREEN;
        }

        text_width = MeasureText (countdown_str, 80);
        DrawText (countdown_str,
                  (screen_width - text_width) / 2,
                  screen_height / 2 - 40,
                  80, color);
    }

    /* Waiting message */
    if (priv->race_state == LRG_RACING_3D_RACE_STATE_WAITING)
    {
        const gchar *msg = "Press SPACE to start";
        gint text_width;

        text_width = MeasureText (msg, 30);
        DrawText (msg,
                  (screen_width - text_width) / 2,
                  screen_height / 2,
                  30, WHITE);
    }

    /* Finished message */
    if (priv->race_state == LRG_RACING_3D_RACE_STATE_FINISHED)
    {
        const gchar *msg = "FINISHED!";
        gint text_width;

        text_width = MeasureText (msg, 60);
        DrawText (msg,
                  (screen_width - text_width) / 2,
                  screen_height / 2 - 30,
                  60, YELLOW);
    }
}

/* ==========================================================================
 * Overridden Parent Virtual Methods
 * ========================================================================== */

static void
lrg_racing_3d_template_update_camera (LrgGame3DTemplate *self,
                                      gdouble            delta)
{
    LrgRacing3DTemplate *racing_self;
    LrgRacing3DTemplateClass *klass;
    LrgRacing3DTemplatePrivate *priv;

    racing_self = LRG_RACING_3D_TEMPLATE (self);
    klass = LRG_RACING_3D_TEMPLATE_GET_CLASS (racing_self);
    priv = lrg_racing_3d_template_get_instance_private (racing_self);

    /* Handle countdown */
    if (priv->race_state == LRG_RACING_3D_RACE_STATE_COUNTDOWN)
    {
        priv->countdown_timer -= (gfloat)delta;

        if (priv->countdown_timer <= 0.0f)
        {
            priv->countdown_value--;

            if (priv->countdown_value < 0)
            {
                /* Start racing */
                lrg_racing_3d_template_set_race_state (racing_self,
                                                       LRG_RACING_3D_RACE_STATE_RACING);
            }
            else
            {
                priv->countdown_timer = 1.0f;
            }
        }
    }

    /* Handle start input */
    if (priv->race_state == LRG_RACING_3D_RACE_STATE_WAITING)
    {
        if (IsKeyPressed (KEY_SPACE) ||
            (IsGamepadAvailable (0) && IsGamepadButtonPressed (0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)))
        {
            lrg_racing_3d_template_start_countdown (racing_self);
        }
    }

    /* Camera cycling */
    if (IsKeyPressed (KEY_C) ||
        (IsGamepadAvailable (0) && IsGamepadButtonPressed (0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)))
    {
        lrg_racing_3d_template_cycle_camera (racing_self);
    }

    /* Update vehicle */
    if (klass->update_vehicle != NULL)
        klass->update_vehicle (racing_self, delta);

    /* Check checkpoints */
    if (priv->race_state == LRG_RACING_3D_RACE_STATE_RACING)
    {
        if (klass->check_checkpoints != NULL)
            klass->check_checkpoints (racing_self);
    }

    /* Update chase camera */
    if (priv->camera_mode != LRG_RACING_3D_CAMERA_ORBIT)
    {
        if (klass->update_chase_camera != NULL)
            klass->update_chase_camera (racing_self, delta);
    }
}

static void
lrg_racing_3d_template_draw_world (LrgGame3DTemplate *self)
{
    LrgRacing3DTemplate *racing_self;
    LrgRacing3DTemplateClass *klass;

    racing_self = LRG_RACING_3D_TEMPLATE (self);
    klass = LRG_RACING_3D_TEMPLATE_GET_CLASS (racing_self);

    /* Draw track */
    if (klass->draw_track != NULL)
        klass->draw_track (racing_self);

    /* Draw vehicle */
    if (klass->draw_vehicle != NULL)
        klass->draw_vehicle (racing_self);
}

static void
lrg_racing_3d_template_draw_ui (LrgGame3DTemplate *self)
{
    LrgRacing3DTemplate *racing_self;
    LrgRacing3DTemplateClass *klass;

    racing_self = LRG_RACING_3D_TEMPLATE (self);
    klass = LRG_RACING_3D_TEMPLATE_GET_CLASS (racing_self);

    /* Draw speedometer */
    if (klass->draw_speedometer != NULL)
        klass->draw_speedometer (racing_self);

    /* Draw minimap */
    if (klass->draw_minimap != NULL)
        klass->draw_minimap (racing_self);

    /* Draw race HUD */
    if (klass->draw_race_hud != NULL)
        klass->draw_race_hud (racing_self);
}

/* ==========================================================================
 * GObject Virtual Methods
 * ========================================================================== */

static void
lrg_racing_3d_template_constructed (GObject *object)
{
    LrgRacing3DTemplate *self;
    LrgRacing3DTemplatePrivate *priv;

    G_OBJECT_CLASS (lrg_racing_3d_template_parent_class)->constructed (object);

    self = LRG_RACING_3D_TEMPLATE (object);
    priv = lrg_racing_3d_template_get_instance_private (self);

    /* Initialize camera position */
    priv->camera_current_x = priv->vehicle_x;
    priv->camera_current_y = priv->vehicle_y + priv->chase_height;
    priv->camera_current_z = priv->vehicle_z - priv->chase_distance;
}

static void
lrg_racing_3d_template_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
    LrgRacing3DTemplate *self;
    LrgRacing3DTemplatePrivate *priv;

    self = LRG_RACING_3D_TEMPLATE (object);
    priv = lrg_racing_3d_template_get_instance_private (self);

    switch (prop_id)
    {
        case PROP_MAX_SPEED:
            g_value_set_float (value, priv->max_speed);
            break;

        case PROP_ACCELERATION:
            g_value_set_float (value, priv->acceleration);
            break;

        case PROP_BRAKE_POWER:
            g_value_set_float (value, priv->brake_power);
            break;

        case PROP_STEERING_SPEED:
            g_value_set_float (value, priv->steering_speed);
            break;

        case PROP_GRIP:
            g_value_set_float (value, priv->grip);
            break;

        case PROP_BOOST:
            g_value_set_float (value, priv->boost);
            break;

        case PROP_BOOST_SPEED:
            g_value_set_float (value, priv->boost_speed);
            break;

        case PROP_CAMERA_MODE:
            g_value_set_int (value, (gint)priv->camera_mode);
            break;

        case PROP_CHASE_DISTANCE:
            g_value_set_float (value, priv->chase_distance);
            break;

        case PROP_CHASE_HEIGHT:
            g_value_set_float (value, priv->chase_height);
            break;

        case PROP_RACE_STATE:
            g_value_set_int (value, (gint)priv->race_state);
            break;

        case PROP_TOTAL_LAPS:
            g_value_set_int (value, priv->total_laps);
            break;

        case PROP_TOTAL_CHECKPOINTS:
            g_value_set_int (value, priv->total_checkpoints);
            break;

        case PROP_TOTAL_RACERS:
            g_value_set_int (value, priv->total_racers);
            break;

        case PROP_RACE_POSITION:
            g_value_set_int (value, priv->race_position);
            break;

        case PROP_SPEEDOMETER_VISIBLE:
            g_value_set_boolean (value, priv->speedometer_visible);
            break;

        case PROP_MINIMAP_VISIBLE:
            g_value_set_boolean (value, priv->minimap_visible);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_racing_3d_template_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
    LrgRacing3DTemplate *self;
    LrgRacing3DTemplatePrivate *priv;

    self = LRG_RACING_3D_TEMPLATE (object);
    priv = lrg_racing_3d_template_get_instance_private (self);

    switch (prop_id)
    {
        case PROP_MAX_SPEED:
            priv->max_speed = g_value_get_float (value);
            break;

        case PROP_ACCELERATION:
            priv->acceleration = g_value_get_float (value);
            break;

        case PROP_BRAKE_POWER:
            priv->brake_power = g_value_get_float (value);
            break;

        case PROP_STEERING_SPEED:
            priv->steering_speed = g_value_get_float (value);
            break;

        case PROP_GRIP:
            priv->grip = g_value_get_float (value);
            break;

        case PROP_BOOST:
            priv->boost = CLAMP (g_value_get_float (value), 0.0f, 1.0f);
            break;

        case PROP_BOOST_SPEED:
            priv->boost_speed = g_value_get_float (value);
            break;

        case PROP_CAMERA_MODE:
            lrg_racing_3d_template_set_camera_mode (self, (LrgRacing3DCameraMode)g_value_get_int (value));
            break;

        case PROP_CHASE_DISTANCE:
            priv->chase_distance = g_value_get_float (value);
            break;

        case PROP_CHASE_HEIGHT:
            priv->chase_height = g_value_get_float (value);
            break;

        case PROP_RACE_STATE:
            lrg_racing_3d_template_set_race_state (self, (LrgRacing3DRaceState)g_value_get_int (value));
            break;

        case PROP_TOTAL_LAPS:
            priv->total_laps = g_value_get_int (value);
            break;

        case PROP_TOTAL_CHECKPOINTS:
            priv->total_checkpoints = g_value_get_int (value);
            break;

        case PROP_TOTAL_RACERS:
            priv->total_racers = g_value_get_int (value);
            break;

        case PROP_RACE_POSITION:
            priv->race_position = g_value_get_int (value);
            break;

        case PROP_SPEEDOMETER_VISIBLE:
            priv->speedometer_visible = g_value_get_boolean (value);
            break;

        case PROP_MINIMAP_VISIBLE:
            priv->minimap_visible = g_value_get_boolean (value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

/* ==========================================================================
 * Class Initialization
 * ========================================================================== */

static void
lrg_racing_3d_template_class_init (LrgRacing3DTemplateClass *klass)
{
    GObjectClass *object_class;
    LrgGame3DTemplateClass *template_class;

    object_class = G_OBJECT_CLASS (klass);
    template_class = LRG_GAME_3D_TEMPLATE_CLASS (klass);

    object_class->constructed = lrg_racing_3d_template_constructed;
    object_class->get_property = lrg_racing_3d_template_get_property;
    object_class->set_property = lrg_racing_3d_template_set_property;

    /* Override parent virtuals */
    template_class->update_camera = lrg_racing_3d_template_update_camera;
    template_class->draw_world = lrg_racing_3d_template_draw_world;
    template_class->draw_ui = lrg_racing_3d_template_draw_ui;

    /* Set default implementations */
    klass->on_race_state_changed = lrg_racing_3d_template_real_on_race_state_changed;
    klass->on_lap_complete = lrg_racing_3d_template_real_on_lap_complete;
    klass->on_checkpoint_reached = lrg_racing_3d_template_real_on_checkpoint_reached;
    klass->on_collision = lrg_racing_3d_template_real_on_collision;
    klass->on_boost_activated = lrg_racing_3d_template_real_on_boost_activated;
    klass->update_vehicle = lrg_racing_3d_template_real_update_vehicle;
    klass->update_chase_camera = lrg_racing_3d_template_real_update_chase_camera;
    klass->check_checkpoints = lrg_racing_3d_template_real_check_checkpoints;
    klass->draw_vehicle = lrg_racing_3d_template_real_draw_vehicle;
    klass->draw_track = lrg_racing_3d_template_real_draw_track;
    klass->draw_speedometer = lrg_racing_3d_template_real_draw_speedometer;
    klass->draw_minimap = lrg_racing_3d_template_real_draw_minimap;
    klass->draw_race_hud = lrg_racing_3d_template_real_draw_race_hud;

    /* Properties */

    properties[PROP_MAX_SPEED] =
        g_param_spec_float ("max-speed", "Max Speed",
                            "Maximum vehicle speed",
                            1.0f, 1000.0f, DEFAULT_MAX_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_ACCELERATION] =
        g_param_spec_float ("acceleration", "Acceleration",
                            "Vehicle acceleration",
                            1.0f, 500.0f, DEFAULT_ACCELERATION,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_BRAKE_POWER] =
        g_param_spec_float ("brake-power", "Brake Power",
                            "Brake deceleration",
                            1.0f, 500.0f, DEFAULT_BRAKE_POWER,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_STEERING_SPEED] =
        g_param_spec_float ("steering-speed", "Steering Speed",
                            "Steering speed in degrees per second",
                            1.0f, 500.0f, DEFAULT_STEERING_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_GRIP] =
        g_param_spec_float ("grip", "Grip",
                            "Tire grip factor",
                            0.0f, 1.0f, DEFAULT_GRIP,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_BOOST] =
        g_param_spec_float ("boost", "Boost",
                            "Current boost amount",
                            0.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_BOOST_SPEED] =
        g_param_spec_float ("boost-speed", "Boost Speed",
                            "Speed multiplier when boosting",
                            1.0f, 3.0f, DEFAULT_BOOST_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_CAMERA_MODE] =
        g_param_spec_int ("camera-mode", "Camera Mode",
                          "Current camera view mode",
                          0, 4, LRG_RACING_3D_CAMERA_CHASE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_CHASE_DISTANCE] =
        g_param_spec_float ("chase-distance", "Chase Distance",
                            "Chase camera distance from vehicle",
                            1.0f, 50.0f, DEFAULT_CHASE_DISTANCE,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_CHASE_HEIGHT] =
        g_param_spec_float ("chase-height", "Chase Height",
                            "Chase camera height above vehicle",
                            0.0f, 20.0f, DEFAULT_CHASE_HEIGHT,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_RACE_STATE] =
        g_param_spec_int ("race-state", "Race State",
                          "Current race state",
                          0, 4, LRG_RACING_3D_RACE_STATE_WAITING,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_TOTAL_LAPS] =
        g_param_spec_int ("total-laps", "Total Laps",
                          "Total number of laps",
                          1, 100, DEFAULT_TOTAL_LAPS,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_TOTAL_CHECKPOINTS] =
        g_param_spec_int ("total-checkpoints", "Total Checkpoints",
                          "Total number of checkpoints",
                          1, 100, DEFAULT_TOTAL_CHECKPOINTS,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_TOTAL_RACERS] =
        g_param_spec_int ("total-racers", "Total Racers",
                          "Total number of racers",
                          1, 100, DEFAULT_TOTAL_RACERS,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_RACE_POSITION] =
        g_param_spec_int ("race-position", "Race Position",
                          "Current race position",
                          1, 100, 1,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SPEEDOMETER_VISIBLE] =
        g_param_spec_boolean ("speedometer-visible", "Speedometer Visible",
                              "Whether to show speedometer",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MINIMAP_VISIBLE] =
        g_param_spec_boolean ("minimap-visible", "Minimap Visible",
                              "Whether to show minimap",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /* Signals */

    signals[SIGNAL_RACE_STATE_CHANGED] =
        g_signal_new ("race-state-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

    signals[SIGNAL_LAP_COMPLETE] =
        g_signal_new ("lap-complete",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_FLOAT);

    signals[SIGNAL_CHECKPOINT_REACHED] =
        g_signal_new ("checkpoint-reached",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_INT);

    signals[SIGNAL_COLLISION] =
        g_signal_new ("collision",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 4, G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_FLOAT);

    signals[SIGNAL_BOOST_ACTIVATED] =
        g_signal_new ("boost-activated",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_racing_3d_template_init (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    priv = lrg_racing_3d_template_get_instance_private (self);

    /* Vehicle position */
    priv->vehicle_x = 0.0f;
    priv->vehicle_y = 0.0f;
    priv->vehicle_z = 0.0f;
    priv->vehicle_rotation = 0.0f;

    /* Velocity */
    priv->velocity_x = 0.0f;
    priv->velocity_y = 0.0f;
    priv->velocity_z = 0.0f;
    priv->speed = 0.0f;

    /* Vehicle settings */
    priv->max_speed = DEFAULT_MAX_SPEED;
    priv->acceleration = DEFAULT_ACCELERATION;
    priv->brake_power = DEFAULT_BRAKE_POWER;
    priv->steering_speed = DEFAULT_STEERING_SPEED;
    priv->grip = DEFAULT_GRIP;
    priv->drag = DEFAULT_DRAG;
    priv->gravity = DEFAULT_GRAVITY;

    priv->steering_angle = 0.0f;

    /* Boost */
    priv->boost = 0.0f;
    priv->boost_speed = DEFAULT_BOOST_SPEED;
    priv->boost_drain = DEFAULT_BOOST_DRAIN;
    priv->is_boosting = FALSE;

    /* Camera */
    priv->camera_mode = LRG_RACING_3D_CAMERA_CHASE;
    priv->chase_distance = DEFAULT_CHASE_DISTANCE;
    priv->chase_height = DEFAULT_CHASE_HEIGHT;
    priv->chase_look_ahead = DEFAULT_CHASE_LOOK_AHEAD;
    priv->camera_smoothing = DEFAULT_CAMERA_SMOOTHING;

    priv->camera_current_x = 0.0f;
    priv->camera_current_y = DEFAULT_CHASE_HEIGHT;
    priv->camera_current_z = -DEFAULT_CHASE_DISTANCE;
    priv->camera_yaw = 0.0f;

    /* Race state */
    priv->race_state = LRG_RACING_3D_RACE_STATE_WAITING;
    priv->countdown_timer = 0.0f;
    priv->countdown_value = 3;

    /* Race progress */
    priv->current_lap = 1;
    priv->total_laps = DEFAULT_TOTAL_LAPS;
    priv->race_time = 0.0f;
    priv->lap_time = 0.0f;
    priv->best_lap_time = -1.0f;

    /* Checkpoints */
    priv->current_checkpoint = -1;
    priv->total_checkpoints = DEFAULT_TOTAL_CHECKPOINTS;

    /* Position */
    priv->race_position = 1;
    priv->total_racers = DEFAULT_TOTAL_RACERS;

    /* State */
    priv->is_grounded = TRUE;
    priv->is_accelerating = FALSE;
    priv->is_braking = FALSE;
    priv->is_reversing = FALSE;

    /* HUD */
    priv->speedometer_visible = TRUE;
    priv->minimap_visible = TRUE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgRacing3DTemplate *
lrg_racing_3d_template_new (void)
{
    return g_object_new (LRG_TYPE_RACING_3D_TEMPLATE, NULL);
}

void
lrg_racing_3d_template_get_position (LrgRacing3DTemplate *self,
                                     gfloat              *x,
                                     gfloat              *y,
                                     gfloat              *z)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));

    priv = lrg_racing_3d_template_get_instance_private (self);

    if (x != NULL)
        *x = priv->vehicle_x;
    if (y != NULL)
        *y = priv->vehicle_y;
    if (z != NULL)
        *z = priv->vehicle_z;
}

void
lrg_racing_3d_template_set_position (LrgRacing3DTemplate *self,
                                     gfloat               x,
                                     gfloat               y,
                                     gfloat               z)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));

    priv = lrg_racing_3d_template_get_instance_private (self);

    priv->vehicle_x = x;
    priv->vehicle_y = y;
    priv->vehicle_z = z;
}

gfloat
lrg_racing_3d_template_get_rotation (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), 0.0f);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->vehicle_rotation;
}

void
lrg_racing_3d_template_set_rotation (LrgRacing3DTemplate *self,
                                     gfloat               rotation)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));

    priv = lrg_racing_3d_template_get_instance_private (self);
    priv->vehicle_rotation = normalize_angle (rotation);
}

gfloat
lrg_racing_3d_template_get_speed (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), 0.0f);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->speed;
}

gfloat
lrg_racing_3d_template_get_max_speed (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), DEFAULT_MAX_SPEED);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->max_speed;
}

void
lrg_racing_3d_template_set_max_speed (LrgRacing3DTemplate *self,
                                      gfloat               speed)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));

    priv = lrg_racing_3d_template_get_instance_private (self);
    priv->max_speed = speed;
}

gfloat
lrg_racing_3d_template_get_acceleration (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), DEFAULT_ACCELERATION);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->acceleration;
}

void
lrg_racing_3d_template_set_acceleration (LrgRacing3DTemplate *self,
                                         gfloat               accel)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));

    priv = lrg_racing_3d_template_get_instance_private (self);
    priv->acceleration = accel;
}

gfloat
lrg_racing_3d_template_get_brake_power (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), DEFAULT_BRAKE_POWER);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->brake_power;
}

void
lrg_racing_3d_template_set_brake_power (LrgRacing3DTemplate *self,
                                        gfloat               power)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));

    priv = lrg_racing_3d_template_get_instance_private (self);
    priv->brake_power = power;
}

gfloat
lrg_racing_3d_template_get_steering_speed (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), DEFAULT_STEERING_SPEED);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->steering_speed;
}

void
lrg_racing_3d_template_set_steering_speed (LrgRacing3DTemplate *self,
                                           gfloat               speed)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));

    priv = lrg_racing_3d_template_get_instance_private (self);
    priv->steering_speed = speed;
}

gfloat
lrg_racing_3d_template_get_grip (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), DEFAULT_GRIP);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->grip;
}

void
lrg_racing_3d_template_set_grip (LrgRacing3DTemplate *self,
                                 gfloat               grip)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));

    priv = lrg_racing_3d_template_get_instance_private (self);
    priv->grip = CLAMP (grip, 0.0f, 1.0f);
}

gboolean
lrg_racing_3d_template_is_grounded (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), FALSE);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->is_grounded;
}

gfloat
lrg_racing_3d_template_get_boost (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), 0.0f);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->boost;
}

void
lrg_racing_3d_template_set_boost (LrgRacing3DTemplate *self,
                                  gfloat               boost)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));

    priv = lrg_racing_3d_template_get_instance_private (self);
    priv->boost = CLAMP (boost, 0.0f, 1.0f);
}

gfloat
lrg_racing_3d_template_get_boost_speed (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), DEFAULT_BOOST_SPEED);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->boost_speed;
}

void
lrg_racing_3d_template_set_boost_speed (LrgRacing3DTemplate *self,
                                        gfloat               multiplier)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));

    priv = lrg_racing_3d_template_get_instance_private (self);
    priv->boost_speed = multiplier;
}

gboolean
lrg_racing_3d_template_is_boosting (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), FALSE);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->is_boosting;
}

LrgRacing3DCameraMode
lrg_racing_3d_template_get_camera_mode (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), LRG_RACING_3D_CAMERA_CHASE);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->camera_mode;
}

void
lrg_racing_3d_template_set_camera_mode (LrgRacing3DTemplate  *self,
                                        LrgRacing3DCameraMode mode)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));

    priv = lrg_racing_3d_template_get_instance_private (self);
    priv->camera_mode = mode;
}

void
lrg_racing_3d_template_cycle_camera (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;
    gint next_mode;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));

    priv = lrg_racing_3d_template_get_instance_private (self);

    next_mode = ((gint)priv->camera_mode + 1) % 5;
    priv->camera_mode = (LrgRacing3DCameraMode)next_mode;
}

gfloat
lrg_racing_3d_template_get_chase_distance (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), DEFAULT_CHASE_DISTANCE);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->chase_distance;
}

void
lrg_racing_3d_template_set_chase_distance (LrgRacing3DTemplate *self,
                                           gfloat               distance)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));

    priv = lrg_racing_3d_template_get_instance_private (self);
    priv->chase_distance = distance;
}

gfloat
lrg_racing_3d_template_get_chase_height (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), DEFAULT_CHASE_HEIGHT);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->chase_height;
}

void
lrg_racing_3d_template_set_chase_height (LrgRacing3DTemplate *self,
                                         gfloat               height)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));

    priv = lrg_racing_3d_template_get_instance_private (self);
    priv->chase_height = height;
}

LrgRacing3DRaceState
lrg_racing_3d_template_get_race_state (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), LRG_RACING_3D_RACE_STATE_WAITING);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->race_state;
}

void
lrg_racing_3d_template_set_race_state (LrgRacing3DTemplate  *self,
                                       LrgRacing3DRaceState  state)
{
    LrgRacing3DTemplatePrivate *priv;
    LrgRacing3DTemplateClass *klass;
    LrgRacing3DRaceState old_state;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));

    priv = lrg_racing_3d_template_get_instance_private (self);
    klass = LRG_RACING_3D_TEMPLATE_GET_CLASS (self);

    if (priv->race_state == state)
        return;

    old_state = priv->race_state;
    priv->race_state = state;

    if (klass->on_race_state_changed != NULL)
        klass->on_race_state_changed (self, old_state, state);

    g_signal_emit (self, signals[SIGNAL_RACE_STATE_CHANGED], 0, (gint)old_state, (gint)state);
}

void
lrg_racing_3d_template_start_countdown (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));

    priv = lrg_racing_3d_template_get_instance_private (self);

    priv->countdown_value = 3;
    priv->countdown_timer = 1.0f;

    lrg_racing_3d_template_set_race_state (self, LRG_RACING_3D_RACE_STATE_COUNTDOWN);
}

gint
lrg_racing_3d_template_get_countdown (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), 0);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->countdown_value;
}

gint
lrg_racing_3d_template_get_current_lap (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), 1);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->current_lap;
}

gint
lrg_racing_3d_template_get_total_laps (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), DEFAULT_TOTAL_LAPS);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->total_laps;
}

void
lrg_racing_3d_template_set_total_laps (LrgRacing3DTemplate *self,
                                       gint                 laps)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));
    g_return_if_fail (laps >= 1);

    priv = lrg_racing_3d_template_get_instance_private (self);
    priv->total_laps = laps;
}

gfloat
lrg_racing_3d_template_get_race_time (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), 0.0f);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->race_time;
}

gfloat
lrg_racing_3d_template_get_lap_time (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), 0.0f);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->lap_time;
}

gfloat
lrg_racing_3d_template_get_best_lap_time (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), -1.0f);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->best_lap_time;
}

gint
lrg_racing_3d_template_get_current_checkpoint (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), -1);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->current_checkpoint;
}

gint
lrg_racing_3d_template_get_total_checkpoints (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), DEFAULT_TOTAL_CHECKPOINTS);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->total_checkpoints;
}

void
lrg_racing_3d_template_set_total_checkpoints (LrgRacing3DTemplate *self,
                                              gint                 checkpoints)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));
    g_return_if_fail (checkpoints >= 1);

    priv = lrg_racing_3d_template_get_instance_private (self);
    priv->total_checkpoints = checkpoints;
}

void
lrg_racing_3d_template_reach_checkpoint (LrgRacing3DTemplate *self,
                                         gint                 checkpoint)
{
    LrgRacing3DTemplatePrivate *priv;
    LrgRacing3DTemplateClass *klass;
    gint expected_checkpoint;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));

    priv = lrg_racing_3d_template_get_instance_private (self);
    klass = LRG_RACING_3D_TEMPLATE_GET_CLASS (self);

    /* Check if this is the expected next checkpoint */
    expected_checkpoint = (priv->current_checkpoint + 1) % priv->total_checkpoints;

    if (checkpoint != expected_checkpoint)
        return;  /* Wrong checkpoint */

    priv->current_checkpoint = checkpoint;

    if (klass->on_checkpoint_reached != NULL)
        klass->on_checkpoint_reached (self, checkpoint);

    g_signal_emit (self, signals[SIGNAL_CHECKPOINT_REACHED], 0, checkpoint);

    /* Check for lap completion */
    if (checkpoint == 0 && priv->current_lap > 0)
    {
        /* Completed a lap */
        gfloat lap_time;

        lap_time = priv->lap_time;

        /* Update best lap */
        if (priv->best_lap_time < 0.0f || lap_time < priv->best_lap_time)
            priv->best_lap_time = lap_time;

        if (klass->on_lap_complete != NULL)
            klass->on_lap_complete (self, priv->current_lap, lap_time);

        g_signal_emit (self, signals[SIGNAL_LAP_COMPLETE], 0, priv->current_lap, lap_time);

        /* Start next lap or finish */
        priv->current_lap++;
        priv->lap_time = 0.0f;

        if (priv->current_lap > priv->total_laps)
        {
            lrg_racing_3d_template_set_race_state (self, LRG_RACING_3D_RACE_STATE_FINISHED);
        }
    }
}

gint
lrg_racing_3d_template_get_race_position (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), 1);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->race_position;
}

void
lrg_racing_3d_template_set_race_position (LrgRacing3DTemplate *self,
                                          gint                 position)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));
    g_return_if_fail (position >= 1);

    priv = lrg_racing_3d_template_get_instance_private (self);
    priv->race_position = position;
}

gint
lrg_racing_3d_template_get_total_racers (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), DEFAULT_TOTAL_RACERS);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->total_racers;
}

void
lrg_racing_3d_template_set_total_racers (LrgRacing3DTemplate *self,
                                         gint                 count)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));
    g_return_if_fail (count >= 1);

    priv = lrg_racing_3d_template_get_instance_private (self);
    priv->total_racers = count;
}

gboolean
lrg_racing_3d_template_get_speedometer_visible (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), TRUE);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->speedometer_visible;
}

void
lrg_racing_3d_template_set_speedometer_visible (LrgRacing3DTemplate *self,
                                                gboolean             visible)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));

    priv = lrg_racing_3d_template_get_instance_private (self);
    priv->speedometer_visible = visible;
}

gboolean
lrg_racing_3d_template_get_minimap_visible (LrgRacing3DTemplate *self)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_3D_TEMPLATE (self), TRUE);

    priv = lrg_racing_3d_template_get_instance_private (self);
    return priv->minimap_visible;
}

void
lrg_racing_3d_template_set_minimap_visible (LrgRacing3DTemplate *self,
                                            gboolean             visible)
{
    LrgRacing3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_3D_TEMPLATE (self));

    priv = lrg_racing_3d_template_get_instance_private (self);
    priv->minimap_visible = visible;
}
