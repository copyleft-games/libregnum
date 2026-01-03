/* lrg-third-person-template.c - Third-person game template implementation
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "../config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TEMPLATE

#include "lrg-third-person-template.h"
#include "lrg-game-3d-template-private.h"
#include "../lrg-log.h"
#include "../graphics/lrg-window.h"

#include <graylib.h>
#include <raylib.h>
#include <math.h>

/* ==========================================================================
 * Default Constants
 * ========================================================================== */

#define DEFAULT_MOVE_SPEED          5.0f
#define DEFAULT_RUN_MULTIPLIER      1.8f
#define DEFAULT_ROTATION_SPEED      720.0f      /* degrees per second */
#define DEFAULT_JUMP_HEIGHT         1.5f
#define DEFAULT_GRAVITY             20.0f

#define DEFAULT_CAMERA_DISTANCE     5.0f
#define DEFAULT_CAMERA_HEIGHT       2.0f
#define DEFAULT_CAMERA_SMOOTHING    0.15f
#define DEFAULT_AIM_DISTANCE        2.5f

#define DEFAULT_SHOULDER_OFFSET_X   1.0f        /* right shoulder */
#define DEFAULT_SHOULDER_OFFSET_Y   0.5f

#define DEFAULT_LOCK_ON_RANGE       30.0f

#define DEFAULT_MAX_HEALTH          100.0f
#define DEFAULT_MAX_STAMINA         100.0f
#define DEFAULT_STAMINA_REGEN       15.0f

#define DEFAULT_DODGE_DISTANCE      3.0f
#define DEFAULT_DODGE_STAMINA_COST  25.0f
#define DEFAULT_DODGE_DURATION      0.4f

#define DEFAULT_CHARACTER_HEIGHT    1.8f
#define DEFAULT_CHARACTER_RADIUS    0.3f

/* ==========================================================================
 * Property IDs
 * ========================================================================== */

enum
{
    PROP_0,

    /* Movement */
    PROP_MOVE_SPEED,
    PROP_RUN_MULTIPLIER,
    PROP_ROTATION_SPEED,
    PROP_JUMP_HEIGHT,
    PROP_GRAVITY,

    /* Camera */
    PROP_CAMERA_DISTANCE,
    PROP_CAMERA_HEIGHT,
    PROP_CAMERA_SMOOTHING,
    PROP_AIM_DISTANCE,

    /* Aim Mode */
    PROP_AIM_MODE,

    /* Health / Stamina */
    PROP_HEALTH,
    PROP_MAX_HEALTH,
    PROP_STAMINA,
    PROP_MAX_STAMINA,

    /* Dodge */
    PROP_DODGE_DISTANCE,
    PROP_DODGE_STAMINA_COST,

    /* Crosshair */
    PROP_CROSSHAIR_VISIBLE,

    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Signal IDs
 * ========================================================================== */

enum
{
    SIGNAL_JUMPED,
    SIGNAL_LANDED,
    SIGNAL_DODGED,
    SIGNAL_ATTACKED,
    SIGNAL_DAMAGED,
    SIGNAL_DIED,

    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct _LrgThirdPersonTemplatePrivate
{
    /* Player position and rotation */
    gfloat player_x;
    gfloat player_y;
    gfloat player_z;
    gfloat player_rotation;         /* Y rotation in degrees */

    /* Velocity */
    gfloat velocity_x;
    gfloat velocity_y;
    gfloat velocity_z;

    /* Movement settings */
    gfloat move_speed;
    gfloat run_multiplier;
    gfloat rotation_speed;
    gfloat jump_height;
    gfloat gravity;

    /* Camera settings */
    gfloat camera_distance;
    gfloat camera_height;
    gfloat camera_smoothing;
    gfloat aim_distance;

    /* Current camera position (for smoothing) */
    gfloat camera_current_x;
    gfloat camera_current_y;
    gfloat camera_current_z;

    /* Shoulder offset for aiming */
    gfloat shoulder_offset_x;
    gfloat shoulder_offset_y;

    /* Aim mode */
    LrgThirdPersonAimMode aim_mode;

    /* Lock-on */
    gboolean has_lock_on_target;
    gfloat lock_on_x;
    gfloat lock_on_y;
    gfloat lock_on_z;
    gfloat lock_on_range;

    /* Health / Stamina */
    gfloat health;
    gfloat max_health;
    gfloat stamina;
    gfloat max_stamina;
    gfloat stamina_regen;

    /* Dodge */
    gfloat dodge_distance;
    gfloat dodge_stamina_cost;
    gfloat dodge_duration;
    gfloat dodge_timer;
    gfloat dodge_direction_x;
    gfloat dodge_direction_z;

    /* State flags */
    gboolean is_running;
    gboolean is_on_ground;
    gboolean is_dodging;
    gboolean is_dead;

    /* UI */
    gboolean crosshair_visible;

} LrgThirdPersonTemplatePrivate;

/* ==========================================================================
 * Type Definition
 * ========================================================================== */

G_DEFINE_TYPE_WITH_PRIVATE (LrgThirdPersonTemplate, lrg_third_person_template,
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
calculate_jump_velocity (gfloat gravity,
                         gfloat jump_height)
{
    /* v = sqrt(2 * g * h) */
    return sqrtf (2.0f * gravity * jump_height);
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

static gfloat
angle_difference (gfloat from,
                  gfloat to)
{
    gfloat diff;

    diff = to - from;

    while (diff > 180.0f)
        diff -= 360.0f;
    while (diff < -180.0f)
        diff += 360.0f;

    return diff;
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static void
lrg_third_person_template_real_on_aim_mode_changed (LrgThirdPersonTemplate *self,
                                                    LrgThirdPersonAimMode   old_mode,
                                                    LrgThirdPersonAimMode   new_mode)
{
    /* Default: no action */
}

static void
lrg_third_person_template_real_on_lock_on_target_changed (LrgThirdPersonTemplate *self,
                                                          gpointer                old_target,
                                                          gpointer                new_target)
{
    /* Default: no action */
}

static void
lrg_third_person_template_real_on_jump (LrgThirdPersonTemplate *self)
{
    /* Default: no action */
}

static void
lrg_third_person_template_real_on_land (LrgThirdPersonTemplate *self,
                                        gfloat                  fall_velocity)
{
    /* Default: no action */
}

static void
lrg_third_person_template_real_on_dodge (LrgThirdPersonTemplate *self,
                                         gfloat                  direction_x,
                                         gfloat                  direction_z)
{
    /* Default: no action */
}

static gboolean
lrg_third_person_template_real_on_attack (LrgThirdPersonTemplate *self,
                                          gint                    attack_type)
{
    /* Default: no attack system */
    return FALSE;
}

static void
lrg_third_person_template_real_on_damage (LrgThirdPersonTemplate *self,
                                          gfloat                  amount,
                                          gfloat                  source_x,
                                          gfloat                  source_y,
                                          gfloat                  source_z)
{
    /* Default: no action */
}

static void
lrg_third_person_template_real_on_death (LrgThirdPersonTemplate *self)
{
    /* Default: no action */
}

static void
lrg_third_person_template_real_update_movement (LrgThirdPersonTemplate *self,
                                                gdouble                 delta)
{
    LrgThirdPersonTemplatePrivate *priv;
    LrgThirdPersonTemplateClass *klass;
    gfloat input_x;
    gfloat input_z;
    gfloat camera_yaw;
    gfloat camera_yaw_rad;
    gfloat move_x;
    gfloat move_z;
    gfloat move_length;
    gfloat speed;
    gfloat target_rotation;
    gfloat rotation_diff;
    gfloat rotation_step;

    priv = lrg_third_person_template_get_instance_private (self);
    klass = LRG_THIRD_PERSON_TEMPLATE_GET_CLASS (self);

    if (priv->is_dead)
        return;

    /* Handle dodge */
    if (priv->is_dodging)
    {
        priv->dodge_timer -= (gfloat)delta;

        if (priv->dodge_timer <= 0.0f)
        {
            priv->is_dodging = FALSE;
        }
        else
        {
            /* Move in dodge direction */
            gfloat dodge_speed;

            dodge_speed = priv->dodge_distance / priv->dodge_duration;
            priv->player_x += priv->dodge_direction_x * dodge_speed * (gfloat)delta;
            priv->player_z += priv->dodge_direction_z * dodge_speed * (gfloat)delta;

            return;  /* Skip normal movement during dodge */
        }
    }

    /* Get movement input */
    input_x = 0.0f;
    input_z = 0.0f;

    if (IsKeyDown (KEY_W) || IsKeyDown (KEY_UP))
        input_z -= 1.0f;
    if (IsKeyDown (KEY_S) || IsKeyDown (KEY_DOWN))
        input_z += 1.0f;
    if (IsKeyDown (KEY_A) || IsKeyDown (KEY_LEFT))
        input_x -= 1.0f;
    if (IsKeyDown (KEY_D) || IsKeyDown (KEY_RIGHT))
        input_x += 1.0f;

    /* Gamepad input */
    if (IsGamepadAvailable (0))
    {
        gfloat gp_x;
        gfloat gp_y;

        gp_x = GetGamepadAxisMovement (0, GAMEPAD_AXIS_LEFT_X);
        gp_y = GetGamepadAxisMovement (0, GAMEPAD_AXIS_LEFT_Y);

        if (fabsf (gp_x) > 0.2f)
            input_x = gp_x;
        if (fabsf (gp_y) > 0.2f)
            input_z = gp_y;
    }

    /* Check run button */
    priv->is_running = IsKeyDown (KEY_LEFT_SHIFT) ||
                       (IsGamepadAvailable (0) && IsGamepadButtonDown (0, GAMEPAD_BUTTON_LEFT_TRIGGER_1));

    /* Calculate movement relative to camera */
    camera_yaw = lrg_game_3d_template_get_yaw (LRG_GAME_3D_TEMPLATE (self));
    camera_yaw_rad = camera_yaw * (gfloat)(G_PI / 180.0);

    move_x = input_x * cosf (camera_yaw_rad) - input_z * sinf (camera_yaw_rad);
    move_z = input_x * sinf (camera_yaw_rad) + input_z * cosf (camera_yaw_rad);

    /* Normalize if needed */
    move_length = sqrtf (move_x * move_x + move_z * move_z);
    if (move_length > 1.0f)
    {
        move_x /= move_length;
        move_z /= move_length;
        move_length = 1.0f;
    }

    /* Apply movement */
    if (move_length > 0.01f)
    {
        speed = priv->move_speed;
        if (priv->is_running)
            speed *= priv->run_multiplier;

        priv->velocity_x = move_x * speed;
        priv->velocity_z = move_z * speed;

        /* Rotate character based on aim mode */
        if (priv->aim_mode == LRG_THIRD_PERSON_AIM_MODE_FREE)
        {
            /* Character faces movement direction */
            target_rotation = atan2f (move_x, move_z) * (gfloat)(180.0 / G_PI);
            target_rotation = normalize_angle (target_rotation);

            rotation_diff = angle_difference (priv->player_rotation, target_rotation);
            rotation_step = priv->rotation_speed * (gfloat)delta;

            if (fabsf (rotation_diff) <= rotation_step)
                priv->player_rotation = target_rotation;
            else if (rotation_diff > 0)
                priv->player_rotation += rotation_step;
            else
                priv->player_rotation -= rotation_step;

            priv->player_rotation = normalize_angle (priv->player_rotation);
        }
        else
        {
            /* Character faces camera direction (strafing) */
            target_rotation = camera_yaw;
            rotation_diff = angle_difference (priv->player_rotation, target_rotation);
            rotation_step = priv->rotation_speed * (gfloat)delta;

            if (fabsf (rotation_diff) <= rotation_step)
                priv->player_rotation = target_rotation;
            else if (rotation_diff > 0)
                priv->player_rotation += rotation_step;
            else
                priv->player_rotation -= rotation_step;

            priv->player_rotation = normalize_angle (priv->player_rotation);
        }
    }
    else
    {
        priv->velocity_x = 0.0f;
        priv->velocity_z = 0.0f;
    }

    /* Apply horizontal velocity */
    priv->player_x += priv->velocity_x * (gfloat)delta;
    priv->player_z += priv->velocity_z * (gfloat)delta;

    /* Apply gravity */
    priv->velocity_y -= priv->gravity * (gfloat)delta;
    priv->player_y += priv->velocity_y * (gfloat)delta;

    /* Ground check (simple floor at y=0) */
    if (priv->player_y <= 0.0f)
    {
        if (!priv->is_on_ground)
        {
            priv->is_on_ground = TRUE;

            if (klass->on_land != NULL)
                klass->on_land (self, fabsf (priv->velocity_y));

            g_signal_emit (self, signals[SIGNAL_LANDED], 0, fabsf (priv->velocity_y));
        }

        priv->player_y = 0.0f;
        priv->velocity_y = 0.0f;
    }
    else
    {
        priv->is_on_ground = FALSE;
    }

    /* Jump */
    if (priv->is_on_ground &&
        (IsKeyPressed (KEY_SPACE) ||
         (IsGamepadAvailable (0) && IsGamepadButtonPressed (0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))))
    {
        priv->velocity_y = calculate_jump_velocity (priv->gravity, priv->jump_height);
        priv->is_on_ground = FALSE;

        if (klass->on_jump != NULL)
            klass->on_jump (self);

        g_signal_emit (self, signals[SIGNAL_JUMPED], 0);
    }

    /* Dodge (roll) */
    if (priv->is_on_ground && !priv->is_dodging &&
        priv->stamina >= priv->dodge_stamina_cost &&
        (IsKeyPressed (KEY_LEFT_CONTROL) ||
         (IsGamepadAvailable (0) && IsGamepadButtonPressed (0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT))))
    {
        gfloat dodge_dir_x;
        gfloat dodge_dir_z;

        /* Dodge in movement direction, or forward if not moving */
        if (move_length > 0.01f)
        {
            dodge_dir_x = move_x;
            dodge_dir_z = move_z;
        }
        else
        {
            gfloat rot_rad;

            rot_rad = priv->player_rotation * (gfloat)(G_PI / 180.0);
            dodge_dir_x = sinf (rot_rad);
            dodge_dir_z = cosf (rot_rad);
        }

        priv->is_dodging = TRUE;
        priv->dodge_timer = priv->dodge_duration;
        priv->dodge_direction_x = dodge_dir_x;
        priv->dodge_direction_z = dodge_dir_z;
        priv->stamina -= priv->dodge_stamina_cost;

        if (klass->on_dodge != NULL)
            klass->on_dodge (self, dodge_dir_x, dodge_dir_z);

        g_signal_emit (self, signals[SIGNAL_DODGED], 0, dodge_dir_x, dodge_dir_z);
    }

    /* Attack inputs */
    if (IsKeyPressed (KEY_F) || IsMouseButtonPressed (MOUSE_BUTTON_LEFT) ||
        (IsGamepadAvailable (0) && IsGamepadButtonPressed (0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT)))
    {
        if (klass->on_attack != NULL)
            klass->on_attack (self, 0);  /* Light attack */

        g_signal_emit (self, signals[SIGNAL_ATTACKED], 0, 0);
    }

    if (IsKeyPressed (KEY_R) || IsMouseButtonPressed (MOUSE_BUTTON_RIGHT) ||
        (IsGamepadAvailable (0) && IsGamepadButtonPressed (0, GAMEPAD_BUTTON_RIGHT_FACE_UP)))
    {
        if (klass->on_attack != NULL)
            klass->on_attack (self, 1);  /* Heavy attack */

        g_signal_emit (self, signals[SIGNAL_ATTACKED], 0, 1);
    }

    /* Toggle aim mode */
    if (IsKeyPressed (KEY_TAB) ||
        (IsGamepadAvailable (0) && IsGamepadButtonPressed (0, GAMEPAD_BUTTON_LEFT_TRIGGER_2)))
    {
        LrgThirdPersonAimMode old_mode;
        LrgThirdPersonAimMode new_mode;

        old_mode = priv->aim_mode;

        if (priv->aim_mode == LRG_THIRD_PERSON_AIM_MODE_FREE)
            new_mode = LRG_THIRD_PERSON_AIM_MODE_AIM;
        else if (priv->aim_mode == LRG_THIRD_PERSON_AIM_MODE_AIM)
            new_mode = LRG_THIRD_PERSON_AIM_MODE_FREE;
        else
            new_mode = priv->aim_mode;  /* Don't toggle out of strafe/lock-on */

        if (new_mode != old_mode)
        {
            priv->aim_mode = new_mode;

            if (klass->on_aim_mode_changed != NULL)
                klass->on_aim_mode_changed (self, old_mode, new_mode);
        }
    }

    /* Regenerate stamina */
    if (!priv->is_running && !priv->is_dodging)
    {
        priv->stamina += priv->stamina_regen * (gfloat)delta;
        if (priv->stamina > priv->max_stamina)
            priv->stamina = priv->max_stamina;
    }
}

static void
lrg_third_person_template_real_update_camera_orbit (LrgThirdPersonTemplate *self,
                                                    gdouble                 delta)
{
    LrgThirdPersonTemplatePrivate *priv;
    LrgThirdPersonTemplateClass *klass;
    LrgGame3DTemplatePrivate *parent_priv;
    gfloat target_x;
    gfloat target_y;
    gfloat target_z;
    gfloat camera_yaw;
    gfloat camera_pitch;
    gfloat yaw_rad;
    gfloat pitch_rad;
    gfloat cos_pitch;
    gfloat distance;
    gfloat adjusted_x;
    gfloat adjusted_y;
    gfloat adjusted_z;
    gfloat shoulder_x;
    gfloat shoulder_y;

    priv = lrg_third_person_template_get_instance_private (self);
    klass = LRG_THIRD_PERSON_TEMPLATE_GET_CLASS (self);
    parent_priv = lrg_game_3d_template_get_private (LRG_GAME_3D_TEMPLATE (self));

    /* Get current camera yaw/pitch */
    camera_yaw = lrg_game_3d_template_get_yaw (LRG_GAME_3D_TEMPLATE (self));
    camera_pitch = lrg_game_3d_template_get_pitch (LRG_GAME_3D_TEMPLATE (self));

    /* Determine camera distance based on aim mode */
    distance = priv->camera_distance;
    shoulder_x = 0.0f;
    shoulder_y = 0.0f;

    if (priv->aim_mode == LRG_THIRD_PERSON_AIM_MODE_AIM ||
        priv->aim_mode == LRG_THIRD_PERSON_AIM_MODE_LOCK_ON)
    {
        distance = priv->aim_distance;
        shoulder_x = priv->shoulder_offset_x;
        shoulder_y = priv->shoulder_offset_y;
    }

    /* Calculate camera position behind player */
    yaw_rad = camera_yaw * (gfloat)(G_PI / 180.0);
    pitch_rad = camera_pitch * (gfloat)(G_PI / 180.0);
    cos_pitch = cosf (pitch_rad);

    /* Camera orbits behind the player */
    target_x = priv->player_x - distance * cos_pitch * sinf (yaw_rad);
    target_y = priv->player_y + priv->camera_height + distance * sinf (pitch_rad);
    target_z = priv->player_z - distance * cos_pitch * cosf (yaw_rad);

    /* Apply shoulder offset (in camera space) */
    target_x += shoulder_x * cosf (yaw_rad);
    target_y += shoulder_y;
    target_z -= shoulder_x * sinf (yaw_rad);

    /* Check for camera collision */
    if (klass->check_camera_collision != NULL)
    {
        klass->check_camera_collision (self,
                                       target_x, target_y, target_z,
                                       &adjusted_x, &adjusted_y, &adjusted_z);
        target_x = adjusted_x;
        target_y = adjusted_y;
        target_z = adjusted_z;
    }

    /* Smooth camera movement */
    priv->camera_current_x = lerp (priv->camera_current_x, target_x, priv->camera_smoothing);
    priv->camera_current_y = lerp (priv->camera_current_y, target_y, priv->camera_smoothing);
    priv->camera_current_z = lerp (priv->camera_current_z, target_z, priv->camera_smoothing);

    /* Update parent camera position */
    if (parent_priv->camera != NULL)
    {
        lrg_camera3d_set_position_xyz (parent_priv->camera,
                                       priv->camera_current_x,
                                       priv->camera_current_y,
                                       priv->camera_current_z);

        /* Camera looks at player (with offset for center of character) */
        if (priv->aim_mode == LRG_THIRD_PERSON_AIM_MODE_LOCK_ON && priv->has_lock_on_target)
        {
            /* Look at lock-on target */
            lrg_camera3d_set_target_xyz (parent_priv->camera,
                                         priv->lock_on_x,
                                         priv->lock_on_y,
                                         priv->lock_on_z);
        }
        else
        {
            /* Look ahead from camera based on yaw/pitch */
            gfloat look_x;
            gfloat look_y;
            gfloat look_z;

            look_x = priv->camera_current_x + cos_pitch * sinf (yaw_rad);
            look_y = priv->camera_current_y - sinf (pitch_rad);
            look_z = priv->camera_current_z + cos_pitch * cosf (yaw_rad);

            lrg_camera3d_set_target_xyz (parent_priv->camera, look_x, look_y, look_z);
        }
    }

    /* Store position in parent priv for consistency */
    parent_priv->position_x = priv->camera_current_x;
    parent_priv->position_y = priv->camera_current_y;
    parent_priv->position_z = priv->camera_current_z;
}

static gboolean
lrg_third_person_template_real_check_camera_collision (LrgThirdPersonTemplate *self,
                                                       gfloat                  camera_x,
                                                       gfloat                  camera_y,
                                                       gfloat                  camera_z,
                                                       gfloat                 *out_x,
                                                       gfloat                 *out_y,
                                                       gfloat                 *out_z)
{
    /* Default: no collision, return unchanged position */
    if (out_x != NULL)
        *out_x = camera_x;
    if (out_y != NULL)
        *out_y = camera_y;
    if (out_z != NULL)
        *out_z = camera_z;

    return FALSE;
}

static void
lrg_third_person_template_real_draw_character (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;
    Vector3 position;
    gfloat radius;
    gfloat height;
    Color color;

    priv = lrg_third_person_template_get_instance_private (self);

    /* Draw a capsule/cylinder to represent the character */
    position.x = priv->player_x;
    position.y = priv->player_y + DEFAULT_CHARACTER_HEIGHT * 0.5f;
    position.z = priv->player_z;

    radius = DEFAULT_CHARACTER_RADIUS;
    height = DEFAULT_CHARACTER_HEIGHT;

    /* Color based on state */
    if (priv->is_dodging)
        color = (Color){ 100, 100, 255, 200 };  /* Blue when dodging */
    else if (priv->is_running)
        color = (Color){ 100, 255, 100, 255 };  /* Green when running */
    else
        color = (Color){ 200, 200, 200, 255 };  /* Gray default */

    DrawCylinder (position, radius, radius, height, 8, color);

    /* Draw direction indicator */
    {
        gfloat rot_rad;
        Vector3 dir_start;
        Vector3 dir_end;

        rot_rad = priv->player_rotation * (gfloat)(G_PI / 180.0);
        dir_start = position;
        dir_start.y = priv->player_y + 0.1f;
        dir_end = dir_start;
        dir_end.x += sinf (rot_rad) * 0.8f;
        dir_end.z += cosf (rot_rad) * 0.8f;

        DrawLine3D (dir_start, dir_end, RED);
    }
}

static void
lrg_third_person_template_real_draw_target_indicator (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;
    Vector3 target;

    priv = lrg_third_person_template_get_instance_private (self);

    if (!priv->has_lock_on_target)
        return;

    target.x = priv->lock_on_x;
    target.y = priv->lock_on_y;
    target.z = priv->lock_on_z;

    /* Draw a ring around the target */
    DrawCircle3D (target, 0.5f, (Vector3){1, 0, 0}, 90.0f, RED);
    DrawCircle3D (target, 0.5f, (Vector3){0, 0, 1}, 90.0f, RED);
}

static void
lrg_third_person_template_real_draw_crosshair (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;
    gint screen_width;
    gint screen_height;
    gint center_x;
    gint center_y;
    gint size;
    gint gap;
    Color color;

    priv = lrg_third_person_template_get_instance_private (self);

    if (!priv->crosshair_visible)
        return;

    /* Only show crosshair in aim mode */
    if (priv->aim_mode != LRG_THIRD_PERSON_AIM_MODE_AIM &&
        priv->aim_mode != LRG_THIRD_PERSON_AIM_MODE_LOCK_ON)
        return;

    screen_width = GetScreenWidth ();
    screen_height = GetScreenHeight ();
    center_x = screen_width / 2;
    center_y = screen_height / 2;

    size = 12;
    gap = 4;
    color = WHITE;

    /* Draw crosshair lines */
    DrawLine (center_x - size, center_y, center_x - gap, center_y, color);
    DrawLine (center_x + gap, center_y, center_x + size, center_y, color);
    DrawLine (center_x, center_y - size, center_x, center_y - gap, color);
    DrawLine (center_x, center_y + gap, center_x, center_y + size, color);
}

static void
lrg_third_person_template_real_draw_hud (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;
    gint screen_width;
    gint screen_height;
    gint bar_width;
    gint bar_height;
    gint x;
    gint y;
    gint health_width;
    gint stamina_width;

    priv = lrg_third_person_template_get_instance_private (self);

    screen_width = GetScreenWidth ();
    screen_height = GetScreenHeight ();

    bar_width = 200;
    bar_height = 20;
    x = 20;
    y = screen_height - 80;

    /* Health bar */
    health_width = (gint)((priv->health / priv->max_health) * bar_width);
    DrawRectangle (x, y, bar_width, bar_height, DARKGRAY);
    DrawRectangle (x, y, health_width, bar_height, RED);
    DrawRectangleLines (x, y, bar_width, bar_height, WHITE);
    DrawText ("HP", x + 5, y + 3, 14, WHITE);

    /* Stamina bar */
    y += bar_height + 5;
    stamina_width = (gint)((priv->stamina / priv->max_stamina) * bar_width);
    DrawRectangle (x, y, bar_width, bar_height, DARKGRAY);
    DrawRectangle (x, y, stamina_width, bar_height, GREEN);
    DrawRectangleLines (x, y, bar_width, bar_height, WHITE);
    DrawText ("ST", x + 5, y + 3, 14, WHITE);

    /* Aim mode indicator */
    {
        const gchar *mode_text;

        switch (priv->aim_mode)
        {
            case LRG_THIRD_PERSON_AIM_MODE_FREE:
                mode_text = "FREE";
                break;
            case LRG_THIRD_PERSON_AIM_MODE_STRAFE:
                mode_text = "STRAFE";
                break;
            case LRG_THIRD_PERSON_AIM_MODE_AIM:
                mode_text = "AIM";
                break;
            case LRG_THIRD_PERSON_AIM_MODE_LOCK_ON:
                mode_text = "LOCK-ON";
                break;
            default:
                mode_text = "???";
                break;
        }

        DrawText (mode_text, screen_width - 100, screen_height - 40, 20, WHITE);
    }
}

/* ==========================================================================
 * Overridden Parent Virtual Methods
 * ========================================================================== */

static void
lrg_third_person_template_update_camera (LrgGame3DTemplate *self,
                                         gdouble            delta)
{
    LrgThirdPersonTemplate *tp_self;
    LrgThirdPersonTemplateClass *klass;

    tp_self = LRG_THIRD_PERSON_TEMPLATE (self);
    klass = LRG_THIRD_PERSON_TEMPLATE_GET_CLASS (tp_self);

    /* Update movement first */
    if (klass->update_movement != NULL)
        klass->update_movement (tp_self, delta);

    /* Then update camera orbit */
    if (klass->update_camera_orbit != NULL)
        klass->update_camera_orbit (tp_self, delta);
}

static void
lrg_third_person_template_draw_world (LrgGame3DTemplate *self)
{
    LrgThirdPersonTemplate *tp_self;
    LrgThirdPersonTemplateClass *klass;

    tp_self = LRG_THIRD_PERSON_TEMPLATE (self);
    klass = LRG_THIRD_PERSON_TEMPLATE_GET_CLASS (tp_self);

    /* Draw a grid */
    grl_draw_grid (20, 1.0f);

    /* Draw player character */
    if (klass->draw_character != NULL)
        klass->draw_character (tp_self);

    /* Draw target indicator */
    if (klass->draw_target_indicator != NULL)
        klass->draw_target_indicator (tp_self);
}

static void
lrg_third_person_template_draw_ui (LrgGame3DTemplate *self)
{
    LrgThirdPersonTemplate *tp_self;
    LrgThirdPersonTemplateClass *klass;

    tp_self = LRG_THIRD_PERSON_TEMPLATE (self);
    klass = LRG_THIRD_PERSON_TEMPLATE_GET_CLASS (tp_self);

    /* Draw crosshair */
    if (klass->draw_crosshair != NULL)
        klass->draw_crosshair (tp_self);

    /* Draw HUD */
    if (klass->draw_hud != NULL)
        klass->draw_hud (tp_self);
}

/* ==========================================================================
 * GObject Virtual Methods
 * ========================================================================== */

static void
lrg_third_person_template_constructed (GObject *object)
{
    LrgThirdPersonTemplate *self;
    LrgThirdPersonTemplatePrivate *priv;

    G_OBJECT_CLASS (lrg_third_person_template_parent_class)->constructed (object);

    self = LRG_THIRD_PERSON_TEMPLATE (object);
    priv = lrg_third_person_template_get_instance_private (self);

    /* Enable mouse look by default */
    lrg_game_3d_template_set_mouse_look_enabled (LRG_GAME_3D_TEMPLATE (self), TRUE);

    /* Initialize camera position */
    priv->camera_current_x = priv->player_x;
    priv->camera_current_y = priv->player_y + priv->camera_height;
    priv->camera_current_z = priv->player_z - priv->camera_distance;
}

static void
lrg_third_person_template_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
    LrgThirdPersonTemplate *self;
    LrgThirdPersonTemplatePrivate *priv;

    self = LRG_THIRD_PERSON_TEMPLATE (object);
    priv = lrg_third_person_template_get_instance_private (self);

    switch (prop_id)
    {
        case PROP_MOVE_SPEED:
            g_value_set_float (value, priv->move_speed);
            break;

        case PROP_RUN_MULTIPLIER:
            g_value_set_float (value, priv->run_multiplier);
            break;

        case PROP_ROTATION_SPEED:
            g_value_set_float (value, priv->rotation_speed);
            break;

        case PROP_JUMP_HEIGHT:
            g_value_set_float (value, priv->jump_height);
            break;

        case PROP_GRAVITY:
            g_value_set_float (value, priv->gravity);
            break;

        case PROP_CAMERA_DISTANCE:
            g_value_set_float (value, priv->camera_distance);
            break;

        case PROP_CAMERA_HEIGHT:
            g_value_set_float (value, priv->camera_height);
            break;

        case PROP_CAMERA_SMOOTHING:
            g_value_set_float (value, priv->camera_smoothing);
            break;

        case PROP_AIM_DISTANCE:
            g_value_set_float (value, priv->aim_distance);
            break;

        case PROP_AIM_MODE:
            g_value_set_int (value, (gint)priv->aim_mode);
            break;

        case PROP_HEALTH:
            g_value_set_float (value, priv->health);
            break;

        case PROP_MAX_HEALTH:
            g_value_set_float (value, priv->max_health);
            break;

        case PROP_STAMINA:
            g_value_set_float (value, priv->stamina);
            break;

        case PROP_MAX_STAMINA:
            g_value_set_float (value, priv->max_stamina);
            break;

        case PROP_DODGE_DISTANCE:
            g_value_set_float (value, priv->dodge_distance);
            break;

        case PROP_DODGE_STAMINA_COST:
            g_value_set_float (value, priv->dodge_stamina_cost);
            break;

        case PROP_CROSSHAIR_VISIBLE:
            g_value_set_boolean (value, priv->crosshair_visible);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_third_person_template_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
    LrgThirdPersonTemplate *self;
    LrgThirdPersonTemplatePrivate *priv;

    self = LRG_THIRD_PERSON_TEMPLATE (object);
    priv = lrg_third_person_template_get_instance_private (self);

    switch (prop_id)
    {
        case PROP_MOVE_SPEED:
            priv->move_speed = g_value_get_float (value);
            break;

        case PROP_RUN_MULTIPLIER:
            priv->run_multiplier = g_value_get_float (value);
            break;

        case PROP_ROTATION_SPEED:
            priv->rotation_speed = g_value_get_float (value);
            break;

        case PROP_JUMP_HEIGHT:
            priv->jump_height = g_value_get_float (value);
            break;

        case PROP_GRAVITY:
            priv->gravity = g_value_get_float (value);
            break;

        case PROP_CAMERA_DISTANCE:
            priv->camera_distance = g_value_get_float (value);
            break;

        case PROP_CAMERA_HEIGHT:
            priv->camera_height = g_value_get_float (value);
            break;

        case PROP_CAMERA_SMOOTHING:
            priv->camera_smoothing = g_value_get_float (value);
            break;

        case PROP_AIM_DISTANCE:
            priv->aim_distance = g_value_get_float (value);
            break;

        case PROP_AIM_MODE:
            lrg_third_person_template_set_aim_mode (self, (LrgThirdPersonAimMode)g_value_get_int (value));
            break;

        case PROP_HEALTH:
            lrg_third_person_template_set_health (self, g_value_get_float (value));
            break;

        case PROP_MAX_HEALTH:
            lrg_third_person_template_set_max_health (self, g_value_get_float (value));
            break;

        case PROP_STAMINA:
            lrg_third_person_template_set_stamina (self, g_value_get_float (value));
            break;

        case PROP_MAX_STAMINA:
            lrg_third_person_template_set_max_stamina (self, g_value_get_float (value));
            break;

        case PROP_DODGE_DISTANCE:
            priv->dodge_distance = g_value_get_float (value);
            break;

        case PROP_DODGE_STAMINA_COST:
            priv->dodge_stamina_cost = g_value_get_float (value);
            break;

        case PROP_CROSSHAIR_VISIBLE:
            priv->crosshair_visible = g_value_get_boolean (value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

/* ==========================================================================
 * Class Initialization
 * ========================================================================== */

static void
lrg_third_person_template_class_init (LrgThirdPersonTemplateClass *klass)
{
    GObjectClass *object_class;
    LrgGame3DTemplateClass *template_class;

    object_class = G_OBJECT_CLASS (klass);
    template_class = LRG_GAME_3D_TEMPLATE_CLASS (klass);

    object_class->constructed = lrg_third_person_template_constructed;
    object_class->get_property = lrg_third_person_template_get_property;
    object_class->set_property = lrg_third_person_template_set_property;

    /* Override parent virtuals */
    template_class->update_camera = lrg_third_person_template_update_camera;
    template_class->draw_world = lrg_third_person_template_draw_world;
    template_class->draw_ui = lrg_third_person_template_draw_ui;

    /* Set default implementations */
    klass->on_aim_mode_changed = lrg_third_person_template_real_on_aim_mode_changed;
    klass->on_lock_on_target_changed = lrg_third_person_template_real_on_lock_on_target_changed;
    klass->on_jump = lrg_third_person_template_real_on_jump;
    klass->on_land = lrg_third_person_template_real_on_land;
    klass->on_dodge = lrg_third_person_template_real_on_dodge;
    klass->on_attack = lrg_third_person_template_real_on_attack;
    klass->on_damage = lrg_third_person_template_real_on_damage;
    klass->on_death = lrg_third_person_template_real_on_death;
    klass->update_movement = lrg_third_person_template_real_update_movement;
    klass->update_camera_orbit = lrg_third_person_template_real_update_camera_orbit;
    klass->check_camera_collision = lrg_third_person_template_real_check_camera_collision;
    klass->draw_character = lrg_third_person_template_real_draw_character;
    klass->draw_target_indicator = lrg_third_person_template_real_draw_target_indicator;
    klass->draw_crosshair = lrg_third_person_template_real_draw_crosshair;
    klass->draw_hud = lrg_third_person_template_real_draw_hud;

    /* Properties */

    properties[PROP_MOVE_SPEED] =
        g_param_spec_float ("move-speed", "Move Speed",
                            "Movement speed in units per second",
                            0.1f, 100.0f, DEFAULT_MOVE_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_RUN_MULTIPLIER] =
        g_param_spec_float ("run-multiplier", "Run Multiplier",
                            "Speed multiplier when running",
                            1.0f, 5.0f, DEFAULT_RUN_MULTIPLIER,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_ROTATION_SPEED] =
        g_param_spec_float ("rotation-speed", "Rotation Speed",
                            "Character rotation speed in degrees per second",
                            1.0f, 1440.0f, DEFAULT_ROTATION_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_JUMP_HEIGHT] =
        g_param_spec_float ("jump-height", "Jump Height",
                            "Maximum jump height in world units",
                            0.1f, 20.0f, DEFAULT_JUMP_HEIGHT,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_GRAVITY] =
        g_param_spec_float ("gravity", "Gravity",
                            "Gravity acceleration",
                            0.1f, 100.0f, DEFAULT_GRAVITY,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_CAMERA_DISTANCE] =
        g_param_spec_float ("camera-distance", "Camera Distance",
                            "Distance from player to camera",
                            0.5f, 50.0f, DEFAULT_CAMERA_DISTANCE,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_CAMERA_HEIGHT] =
        g_param_spec_float ("camera-height", "Camera Height",
                            "Height offset from player to camera",
                            -10.0f, 20.0f, DEFAULT_CAMERA_HEIGHT,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_CAMERA_SMOOTHING] =
        g_param_spec_float ("camera-smoothing", "Camera Smoothing",
                            "Camera follow smoothing factor (0-1)",
                            0.01f, 1.0f, DEFAULT_CAMERA_SMOOTHING,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_AIM_DISTANCE] =
        g_param_spec_float ("aim-distance", "Aim Distance",
                            "Camera distance when aiming",
                            0.5f, 20.0f, DEFAULT_AIM_DISTANCE,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_AIM_MODE] =
        g_param_spec_int ("aim-mode", "Aim Mode",
                          "Current aim/camera mode",
                          0, 3, LRG_THIRD_PERSON_AIM_MODE_FREE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_HEALTH] =
        g_param_spec_float ("health", "Health",
                            "Current health",
                            0.0f, G_MAXFLOAT, DEFAULT_MAX_HEALTH,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_HEALTH] =
        g_param_spec_float ("max-health", "Max Health",
                            "Maximum health",
                            1.0f, G_MAXFLOAT, DEFAULT_MAX_HEALTH,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_STAMINA] =
        g_param_spec_float ("stamina", "Stamina",
                            "Current stamina",
                            0.0f, G_MAXFLOAT, DEFAULT_MAX_STAMINA,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_STAMINA] =
        g_param_spec_float ("max-stamina", "Max Stamina",
                            "Maximum stamina",
                            1.0f, G_MAXFLOAT, DEFAULT_MAX_STAMINA,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DODGE_DISTANCE] =
        g_param_spec_float ("dodge-distance", "Dodge Distance",
                            "Distance covered when dodging",
                            0.1f, 20.0f, DEFAULT_DODGE_DISTANCE,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DODGE_STAMINA_COST] =
        g_param_spec_float ("dodge-stamina-cost", "Dodge Stamina Cost",
                            "Stamina cost of dodging",
                            0.0f, 100.0f, DEFAULT_DODGE_STAMINA_COST,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_CROSSHAIR_VISIBLE] =
        g_param_spec_boolean ("crosshair-visible", "Crosshair Visible",
                              "Whether to show crosshair when aiming",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /* Signals */

    signals[SIGNAL_JUMPED] =
        g_signal_new ("jumped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    signals[SIGNAL_LANDED] =
        g_signal_new ("landed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_FLOAT);

    signals[SIGNAL_DODGED] =
        g_signal_new ("dodged",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_FLOAT, G_TYPE_FLOAT);

    signals[SIGNAL_ATTACKED] =
        g_signal_new ("attacked",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_INT);

    signals[SIGNAL_DAMAGED] =
        g_signal_new ("damaged",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_FLOAT);

    signals[SIGNAL_DIED] =
        g_signal_new ("died",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_third_person_template_init (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    priv = lrg_third_person_template_get_instance_private (self);

    /* Player position */
    priv->player_x = 0.0f;
    priv->player_y = 0.0f;
    priv->player_z = 0.0f;
    priv->player_rotation = 0.0f;

    /* Velocity */
    priv->velocity_x = 0.0f;
    priv->velocity_y = 0.0f;
    priv->velocity_z = 0.0f;

    /* Movement */
    priv->move_speed = DEFAULT_MOVE_SPEED;
    priv->run_multiplier = DEFAULT_RUN_MULTIPLIER;
    priv->rotation_speed = DEFAULT_ROTATION_SPEED;
    priv->jump_height = DEFAULT_JUMP_HEIGHT;
    priv->gravity = DEFAULT_GRAVITY;

    /* Camera */
    priv->camera_distance = DEFAULT_CAMERA_DISTANCE;
    priv->camera_height = DEFAULT_CAMERA_HEIGHT;
    priv->camera_smoothing = DEFAULT_CAMERA_SMOOTHING;
    priv->aim_distance = DEFAULT_AIM_DISTANCE;

    priv->camera_current_x = 0.0f;
    priv->camera_current_y = DEFAULT_CAMERA_HEIGHT;
    priv->camera_current_z = -DEFAULT_CAMERA_DISTANCE;

    /* Shoulder offset */
    priv->shoulder_offset_x = DEFAULT_SHOULDER_OFFSET_X;
    priv->shoulder_offset_y = DEFAULT_SHOULDER_OFFSET_Y;

    /* Aim mode */
    priv->aim_mode = LRG_THIRD_PERSON_AIM_MODE_FREE;

    /* Lock-on */
    priv->has_lock_on_target = FALSE;
    priv->lock_on_x = 0.0f;
    priv->lock_on_y = 0.0f;
    priv->lock_on_z = 0.0f;
    priv->lock_on_range = DEFAULT_LOCK_ON_RANGE;

    /* Health / Stamina */
    priv->health = DEFAULT_MAX_HEALTH;
    priv->max_health = DEFAULT_MAX_HEALTH;
    priv->stamina = DEFAULT_MAX_STAMINA;
    priv->max_stamina = DEFAULT_MAX_STAMINA;
    priv->stamina_regen = DEFAULT_STAMINA_REGEN;

    /* Dodge */
    priv->dodge_distance = DEFAULT_DODGE_DISTANCE;
    priv->dodge_stamina_cost = DEFAULT_DODGE_STAMINA_COST;
    priv->dodge_duration = DEFAULT_DODGE_DURATION;
    priv->dodge_timer = 0.0f;
    priv->dodge_direction_x = 0.0f;
    priv->dodge_direction_z = 0.0f;

    /* State */
    priv->is_running = FALSE;
    priv->is_on_ground = TRUE;
    priv->is_dodging = FALSE;
    priv->is_dead = FALSE;

    /* UI */
    priv->crosshair_visible = TRUE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgThirdPersonTemplate *
lrg_third_person_template_new (void)
{
    return g_object_new (LRG_TYPE_THIRD_PERSON_TEMPLATE, NULL);
}

void
lrg_third_person_template_get_position (LrgThirdPersonTemplate *self,
                                        gfloat                 *x,
                                        gfloat                 *y,
                                        gfloat                 *z)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));

    priv = lrg_third_person_template_get_instance_private (self);

    if (x != NULL)
        *x = priv->player_x;
    if (y != NULL)
        *y = priv->player_y;
    if (z != NULL)
        *z = priv->player_z;
}

void
lrg_third_person_template_set_position (LrgThirdPersonTemplate *self,
                                        gfloat                  x,
                                        gfloat                  y,
                                        gfloat                  z)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));

    priv = lrg_third_person_template_get_instance_private (self);

    priv->player_x = x;
    priv->player_y = y;
    priv->player_z = z;
}

gfloat
lrg_third_person_template_get_rotation (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), 0.0f);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->player_rotation;
}

void
lrg_third_person_template_set_rotation (LrgThirdPersonTemplate *self,
                                        gfloat                  rotation)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));

    priv = lrg_third_person_template_get_instance_private (self);
    priv->player_rotation = normalize_angle (rotation);
}

gfloat
lrg_third_person_template_get_move_speed (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), DEFAULT_MOVE_SPEED);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->move_speed;
}

void
lrg_third_person_template_set_move_speed (LrgThirdPersonTemplate *self,
                                          gfloat                  speed)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));
    g_return_if_fail (speed > 0.0f);

    priv = lrg_third_person_template_get_instance_private (self);
    priv->move_speed = speed;
}

gfloat
lrg_third_person_template_get_run_multiplier (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), DEFAULT_RUN_MULTIPLIER);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->run_multiplier;
}

void
lrg_third_person_template_set_run_multiplier (LrgThirdPersonTemplate *self,
                                              gfloat                  multiplier)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));
    g_return_if_fail (multiplier >= 1.0f);

    priv = lrg_third_person_template_get_instance_private (self);
    priv->run_multiplier = multiplier;
}

gfloat
lrg_third_person_template_get_rotation_speed (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), DEFAULT_ROTATION_SPEED);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->rotation_speed;
}

void
lrg_third_person_template_set_rotation_speed (LrgThirdPersonTemplate *self,
                                              gfloat                  speed)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));
    g_return_if_fail (speed > 0.0f);

    priv = lrg_third_person_template_get_instance_private (self);
    priv->rotation_speed = speed;
}

gfloat
lrg_third_person_template_get_jump_height (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), DEFAULT_JUMP_HEIGHT);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->jump_height;
}

void
lrg_third_person_template_set_jump_height (LrgThirdPersonTemplate *self,
                                           gfloat                  height)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));
    g_return_if_fail (height > 0.0f);

    priv = lrg_third_person_template_get_instance_private (self);
    priv->jump_height = height;
}

gfloat
lrg_third_person_template_get_gravity (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), DEFAULT_GRAVITY);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->gravity;
}

void
lrg_third_person_template_set_gravity (LrgThirdPersonTemplate *self,
                                       gfloat                  gravity)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));
    g_return_if_fail (gravity > 0.0f);

    priv = lrg_third_person_template_get_instance_private (self);
    priv->gravity = gravity;
}

gboolean
lrg_third_person_template_is_running (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), FALSE);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->is_running;
}

gboolean
lrg_third_person_template_is_on_ground (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), FALSE);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->is_on_ground;
}

gfloat
lrg_third_person_template_get_camera_distance (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), DEFAULT_CAMERA_DISTANCE);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->camera_distance;
}

void
lrg_third_person_template_set_camera_distance (LrgThirdPersonTemplate *self,
                                               gfloat                  distance)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));
    g_return_if_fail (distance > 0.0f);

    priv = lrg_third_person_template_get_instance_private (self);
    priv->camera_distance = distance;
}

gfloat
lrg_third_person_template_get_camera_height (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), DEFAULT_CAMERA_HEIGHT);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->camera_height;
}

void
lrg_third_person_template_set_camera_height (LrgThirdPersonTemplate *self,
                                             gfloat                  height)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));

    priv = lrg_third_person_template_get_instance_private (self);
    priv->camera_height = height;
}

gfloat
lrg_third_person_template_get_camera_smoothing (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), DEFAULT_CAMERA_SMOOTHING);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->camera_smoothing;
}

void
lrg_third_person_template_set_camera_smoothing (LrgThirdPersonTemplate *self,
                                                gfloat                  smoothing)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));

    priv = lrg_third_person_template_get_instance_private (self);
    priv->camera_smoothing = CLAMP (smoothing, 0.01f, 1.0f);
}

void
lrg_third_person_template_get_shoulder_offset (LrgThirdPersonTemplate *self,
                                               gfloat                 *x,
                                               gfloat                 *y)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));

    priv = lrg_third_person_template_get_instance_private (self);

    if (x != NULL)
        *x = priv->shoulder_offset_x;
    if (y != NULL)
        *y = priv->shoulder_offset_y;
}

void
lrg_third_person_template_set_shoulder_offset (LrgThirdPersonTemplate *self,
                                               gfloat                  x,
                                               gfloat                  y)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));

    priv = lrg_third_person_template_get_instance_private (self);

    priv->shoulder_offset_x = x;
    priv->shoulder_offset_y = y;
}

void
lrg_third_person_template_swap_shoulder (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));

    priv = lrg_third_person_template_get_instance_private (self);

    priv->shoulder_offset_x = -priv->shoulder_offset_x;
}

gfloat
lrg_third_person_template_get_aim_distance (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), DEFAULT_AIM_DISTANCE);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->aim_distance;
}

void
lrg_third_person_template_set_aim_distance (LrgThirdPersonTemplate *self,
                                            gfloat                  distance)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));
    g_return_if_fail (distance > 0.0f);

    priv = lrg_third_person_template_get_instance_private (self);
    priv->aim_distance = distance;
}

LrgThirdPersonAimMode
lrg_third_person_template_get_aim_mode (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), LRG_THIRD_PERSON_AIM_MODE_FREE);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->aim_mode;
}

void
lrg_third_person_template_set_aim_mode (LrgThirdPersonTemplate *self,
                                        LrgThirdPersonAimMode   mode)
{
    LrgThirdPersonTemplatePrivate *priv;
    LrgThirdPersonTemplateClass *klass;
    LrgThirdPersonAimMode old_mode;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));

    priv = lrg_third_person_template_get_instance_private (self);
    klass = LRG_THIRD_PERSON_TEMPLATE_GET_CLASS (self);

    if (priv->aim_mode == mode)
        return;

    old_mode = priv->aim_mode;
    priv->aim_mode = mode;

    if (klass->on_aim_mode_changed != NULL)
        klass->on_aim_mode_changed (self, old_mode, mode);
}

gboolean
lrg_third_person_template_is_aiming (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), FALSE);

    priv = lrg_third_person_template_get_instance_private (self);

    return priv->aim_mode == LRG_THIRD_PERSON_AIM_MODE_AIM ||
           priv->aim_mode == LRG_THIRD_PERSON_AIM_MODE_LOCK_ON;
}

gboolean
lrg_third_person_template_get_lock_on_target (LrgThirdPersonTemplate *self,
                                              gfloat                 *x,
                                              gfloat                 *y,
                                              gfloat                 *z)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), FALSE);

    priv = lrg_third_person_template_get_instance_private (self);

    if (!priv->has_lock_on_target)
        return FALSE;

    if (x != NULL)
        *x = priv->lock_on_x;
    if (y != NULL)
        *y = priv->lock_on_y;
    if (z != NULL)
        *z = priv->lock_on_z;

    return TRUE;
}

void
lrg_third_person_template_set_lock_on_target (LrgThirdPersonTemplate *self,
                                              gfloat                  x,
                                              gfloat                  y,
                                              gfloat                  z)
{
    LrgThirdPersonTemplatePrivate *priv;
    LrgThirdPersonTemplateClass *klass;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));

    priv = lrg_third_person_template_get_instance_private (self);
    klass = LRG_THIRD_PERSON_TEMPLATE_GET_CLASS (self);

    priv->has_lock_on_target = TRUE;
    priv->lock_on_x = x;
    priv->lock_on_y = y;
    priv->lock_on_z = z;

    if (priv->aim_mode != LRG_THIRD_PERSON_AIM_MODE_LOCK_ON)
    {
        LrgThirdPersonAimMode old_mode;

        old_mode = priv->aim_mode;
        priv->aim_mode = LRG_THIRD_PERSON_AIM_MODE_LOCK_ON;

        if (klass->on_aim_mode_changed != NULL)
            klass->on_aim_mode_changed (self, old_mode, LRG_THIRD_PERSON_AIM_MODE_LOCK_ON);
    }

    if (klass->on_lock_on_target_changed != NULL)
        klass->on_lock_on_target_changed (self, NULL, self);  /* Using self as non-NULL indicator */
}

void
lrg_third_person_template_clear_lock_on (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;
    LrgThirdPersonTemplateClass *klass;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));

    priv = lrg_third_person_template_get_instance_private (self);
    klass = LRG_THIRD_PERSON_TEMPLATE_GET_CLASS (self);

    if (!priv->has_lock_on_target)
        return;

    priv->has_lock_on_target = FALSE;

    if (priv->aim_mode == LRG_THIRD_PERSON_AIM_MODE_LOCK_ON)
    {
        LrgThirdPersonAimMode old_mode;

        old_mode = priv->aim_mode;
        priv->aim_mode = LRG_THIRD_PERSON_AIM_MODE_FREE;

        if (klass->on_aim_mode_changed != NULL)
            klass->on_aim_mode_changed (self, old_mode, LRG_THIRD_PERSON_AIM_MODE_FREE);
    }

    if (klass->on_lock_on_target_changed != NULL)
        klass->on_lock_on_target_changed (self, self, NULL);
}

gfloat
lrg_third_person_template_get_lock_on_range (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), DEFAULT_LOCK_ON_RANGE);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->lock_on_range;
}

void
lrg_third_person_template_set_lock_on_range (LrgThirdPersonTemplate *self,
                                             gfloat                  range)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));
    g_return_if_fail (range > 0.0f);

    priv = lrg_third_person_template_get_instance_private (self);
    priv->lock_on_range = range;
}

gfloat
lrg_third_person_template_get_health (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), 0.0f);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->health;
}

void
lrg_third_person_template_set_health (LrgThirdPersonTemplate *self,
                                      gfloat                  health)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));

    priv = lrg_third_person_template_get_instance_private (self);
    priv->health = CLAMP (health, 0.0f, priv->max_health);

    if (priv->health <= 0.0f && !priv->is_dead)
    {
        LrgThirdPersonTemplateClass *klass;

        priv->is_dead = TRUE;
        klass = LRG_THIRD_PERSON_TEMPLATE_GET_CLASS (self);

        if (klass->on_death != NULL)
            klass->on_death (self);

        g_signal_emit (self, signals[SIGNAL_DIED], 0);
    }
}

gfloat
lrg_third_person_template_get_max_health (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), DEFAULT_MAX_HEALTH);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->max_health;
}

void
lrg_third_person_template_set_max_health (LrgThirdPersonTemplate *self,
                                          gfloat                  max)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));
    g_return_if_fail (max > 0.0f);

    priv = lrg_third_person_template_get_instance_private (self);

    priv->max_health = max;
    if (priv->health > max)
        priv->health = max;
}

gfloat
lrg_third_person_template_get_stamina (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), 0.0f);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->stamina;
}

void
lrg_third_person_template_set_stamina (LrgThirdPersonTemplate *self,
                                       gfloat                  stamina)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));

    priv = lrg_third_person_template_get_instance_private (self);
    priv->stamina = CLAMP (stamina, 0.0f, priv->max_stamina);
}

gfloat
lrg_third_person_template_get_max_stamina (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), DEFAULT_MAX_STAMINA);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->max_stamina;
}

void
lrg_third_person_template_set_max_stamina (LrgThirdPersonTemplate *self,
                                           gfloat                  max)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));
    g_return_if_fail (max > 0.0f);

    priv = lrg_third_person_template_get_instance_private (self);

    priv->max_stamina = max;
    if (priv->stamina > max)
        priv->stamina = max;
}

void
lrg_third_person_template_apply_damage (LrgThirdPersonTemplate *self,
                                        gfloat                  damage,
                                        gfloat                  source_x,
                                        gfloat                  source_y,
                                        gfloat                  source_z)
{
    LrgThirdPersonTemplatePrivate *priv;
    LrgThirdPersonTemplateClass *klass;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));

    priv = lrg_third_person_template_get_instance_private (self);
    klass = LRG_THIRD_PERSON_TEMPLATE_GET_CLASS (self);

    if (priv->is_dead)
        return;

    /* Apply damage */
    priv->health -= damage;

    if (klass->on_damage != NULL)
        klass->on_damage (self, damage, source_x, source_y, source_z);

    g_signal_emit (self, signals[SIGNAL_DAMAGED], 0, damage);

    if (priv->health <= 0.0f)
    {
        priv->health = 0.0f;
        priv->is_dead = TRUE;

        if (klass->on_death != NULL)
            klass->on_death (self);

        g_signal_emit (self, signals[SIGNAL_DIED], 0);
    }
}

gboolean
lrg_third_person_template_is_dead (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), FALSE);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->is_dead;
}

gfloat
lrg_third_person_template_get_dodge_distance (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), DEFAULT_DODGE_DISTANCE);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->dodge_distance;
}

void
lrg_third_person_template_set_dodge_distance (LrgThirdPersonTemplate *self,
                                              gfloat                  distance)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));
    g_return_if_fail (distance > 0.0f);

    priv = lrg_third_person_template_get_instance_private (self);
    priv->dodge_distance = distance;
}

gfloat
lrg_third_person_template_get_dodge_stamina_cost (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), DEFAULT_DODGE_STAMINA_COST);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->dodge_stamina_cost;
}

void
lrg_third_person_template_set_dodge_stamina_cost (LrgThirdPersonTemplate *self,
                                                  gfloat                  cost)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));

    priv = lrg_third_person_template_get_instance_private (self);
    priv->dodge_stamina_cost = cost;
}

gboolean
lrg_third_person_template_can_dodge (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), FALSE);

    priv = lrg_third_person_template_get_instance_private (self);

    return priv->is_on_ground &&
           !priv->is_dodging &&
           priv->stamina >= priv->dodge_stamina_cost;
}

gboolean
lrg_third_person_template_is_dodging (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), FALSE);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->is_dodging;
}

gboolean
lrg_third_person_template_get_crosshair_visible (LrgThirdPersonTemplate *self)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self), TRUE);

    priv = lrg_third_person_template_get_instance_private (self);
    return priv->crosshair_visible;
}

void
lrg_third_person_template_set_crosshair_visible (LrgThirdPersonTemplate *self,
                                                 gboolean                visible)
{
    LrgThirdPersonTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_THIRD_PERSON_TEMPLATE (self));

    priv = lrg_third_person_template_get_instance_private (self);
    priv->crosshair_visible = visible;
}
