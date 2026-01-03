/* lrg-top-down-template.c - Top-down game template implementation
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include "lrg-top-down-template.h"
#include "../lrg-log.h"

#include <graylib.h>
#include <raylib.h>
#include <math.h>

/* Default values */
#define LRG_TOP_DOWN_DEFAULT_MOVE_SPEED      (200.0f)
#define LRG_TOP_DOWN_DEFAULT_ACCELERATION    (2000.0f)
#define LRG_TOP_DOWN_DEFAULT_FRICTION        (1500.0f)
#define LRG_TOP_DOWN_DEFAULT_ROTATION_SPEED  (4.0f)
#define LRG_TOP_DOWN_DEFAULT_INTERACT_RADIUS (48.0f)
#define LRG_TOP_DOWN_DEFAULT_LOOK_AHEAD      (64.0f)
#define LRG_TOP_DOWN_DEFAULT_LOOK_AHEAD_SPEED (0.1f)
#define LRG_TOP_DOWN_DEFAULT_PLAYER_WIDTH    (32.0f)
#define LRG_TOP_DOWN_DEFAULT_PLAYER_HEIGHT   (32.0f)

typedef struct
{
    /* Player state */
    gfloat player_x;
    gfloat player_y;
    gfloat velocity_x;
    gfloat velocity_y;
    gfloat player_width;
    gfloat player_height;

    /* Movement settings */
    LrgTopDownMovementMode movement_mode;
    gfloat move_speed;
    gfloat acceleration;
    gfloat friction;
    gfloat rotation_speed;

    /* Facing (discrete for 4/8-dir, angle for free/tank) */
    LrgFacingDirection facing;
    gfloat facing_angle;

    /* Interaction system */
    gfloat interact_radius;
    gpointer interact_target;

    /* Camera look-ahead */
    gfloat look_ahead;
    gfloat look_ahead_speed;
    gfloat look_ahead_x;
    gfloat look_ahead_y;

    /* Input state */
    gfloat input_x;
    gfloat input_y;
    gboolean is_moving;
} LrgTopDownTemplatePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgTopDownTemplate, lrg_top_down_template,
                            LRG_TYPE_GAME_2D_TEMPLATE)

/* Property IDs */
enum
{
    PROP_0,
    PROP_PLAYER_X,
    PROP_PLAYER_Y,
    PROP_PLAYER_WIDTH,
    PROP_PLAYER_HEIGHT,
    PROP_MOVEMENT_MODE,
    PROP_MOVE_SPEED,
    PROP_ACCELERATION,
    PROP_FRICTION,
    PROP_ROTATION_SPEED,
    PROP_FACING,
    PROP_INTERACT_RADIUS,
    PROP_LOOK_AHEAD,
    PROP_LOOK_AHEAD_SPEED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Signal IDs */
enum
{
    SIGNAL_FACING_CHANGED,
    SIGNAL_INTERACT,
    SIGNAL_INTERACT_TARGET_CHANGED,
    SIGNAL_MOVEMENT_STARTED,
    SIGNAL_MOVEMENT_STOPPED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

/*
 * approach:
 * @current: current value
 * @target: target value
 * @amount: maximum change per step
 *
 * Moves current toward target by at most amount.
 */
static gfloat
approach (gfloat current,
          gfloat target,
          gfloat amount)
{
    if (current < target)
    {
        current += amount;
        if (current > target)
            current = target;
    }
    else
    {
        current -= amount;
        if (current < target)
            current = target;
    }
    return current;
}

/*
 * facing_from_angle:
 * @angle: angle in radians (0 = right, PI/2 = down)
 * @mode: movement mode (4-dir or 8-dir)
 *
 * Converts an angle to a discrete facing direction.
 */
static LrgFacingDirection
facing_from_angle (gfloat                  angle,
                   LrgTopDownMovementMode  mode)
{
    gfloat deg;

    /* Normalize angle to 0-360 range */
    while (angle < 0)
        angle += 2.0f * (gfloat)G_PI;
    while (angle >= 2.0f * (gfloat)G_PI)
        angle -= 2.0f * (gfloat)G_PI;

    deg = angle * 180.0f / (gfloat)G_PI;

    if (mode == LRG_TOP_DOWN_MOVEMENT_4_DIR)
    {
        /* 4-directional: 90-degree sectors */
        if (deg >= 315.0f || deg < 45.0f)
            return LRG_FACING_RIGHT;
        else if (deg >= 45.0f && deg < 135.0f)
            return LRG_FACING_DOWN;
        else if (deg >= 135.0f && deg < 225.0f)
            return LRG_FACING_LEFT;
        else
            return LRG_FACING_UP;
    }
    else
    {
        /* 8-directional: 45-degree sectors */
        if (deg >= 337.5f || deg < 22.5f)
            return LRG_FACING_RIGHT;
        else if (deg >= 22.5f && deg < 67.5f)
            return LRG_FACING_DOWN_RIGHT;
        else if (deg >= 67.5f && deg < 112.5f)
            return LRG_FACING_DOWN;
        else if (deg >= 112.5f && deg < 157.5f)
            return LRG_FACING_DOWN_LEFT;
        else if (deg >= 157.5f && deg < 202.5f)
            return LRG_FACING_LEFT;
        else if (deg >= 202.5f && deg < 247.5f)
            return LRG_FACING_UP_LEFT;
        else if (deg >= 247.5f && deg < 292.5f)
            return LRG_FACING_UP;
        else
            return LRG_FACING_UP_RIGHT;
    }
}

/*
 * angle_from_facing:
 * @facing: discrete facing direction
 *
 * Converts a discrete facing direction to an angle.
 */
static gfloat
angle_from_facing (LrgFacingDirection facing)
{
    switch (facing)
    {
        case LRG_FACING_RIGHT:      return 0.0f;
        case LRG_FACING_DOWN_RIGHT: return (gfloat)G_PI * 0.25f;
        case LRG_FACING_DOWN:       return (gfloat)G_PI * 0.5f;
        case LRG_FACING_DOWN_LEFT:  return (gfloat)G_PI * 0.75f;
        case LRG_FACING_LEFT:       return (gfloat)G_PI;
        case LRG_FACING_UP_LEFT:    return (gfloat)G_PI * 1.25f;
        case LRG_FACING_UP:         return (gfloat)G_PI * 1.5f;
        case LRG_FACING_UP_RIGHT:   return (gfloat)G_PI * 1.75f;
        default:                    return 0.0f;
    }
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static void
lrg_top_down_template_real_on_movement_input (LrgTopDownTemplate *self,
                                              gfloat              input_x,
                                              gfloat              input_y,
                                              gdouble             delta,
                                              gfloat             *velocity_x,
                                              gfloat             *velocity_y)
{
    LrgTopDownTemplatePrivate *priv;
    gfloat target_vx;
    gfloat target_vy;
    gfloat input_length;
    gfloat accel;
    gfloat decel;

    priv = lrg_top_down_template_get_instance_private (self);

    input_length = sqrtf (input_x * input_x + input_y * input_y);
    accel = priv->acceleration * (gfloat)delta;
    decel = priv->friction * (gfloat)delta;

    if (input_length > 0.001f)
    {
        gfloat move_angle;

        /* Normalize and scale by move speed */
        input_x /= input_length;
        input_y /= input_length;

        switch (priv->movement_mode)
        {
            case LRG_TOP_DOWN_MOVEMENT_4_DIR:
                /* Snap to cardinal direction */
                if (fabsf (input_x) > fabsf (input_y))
                {
                    input_x = (input_x > 0) ? 1.0f : -1.0f;
                    input_y = 0.0f;
                }
                else
                {
                    input_x = 0.0f;
                    input_y = (input_y > 0) ? 1.0f : -1.0f;
                }
                break;

            case LRG_TOP_DOWN_MOVEMENT_8_DIR:
                /* Snap to 8 directions */
                move_angle = atan2f (input_y, input_x);
                move_angle = roundf (move_angle / ((gfloat)G_PI * 0.25f)) * ((gfloat)G_PI * 0.25f);
                input_x = cosf (move_angle);
                input_y = sinf (move_angle);
                break;

            case LRG_TOP_DOWN_MOVEMENT_FREE:
                /* Already normalized, use as-is */
                break;

            case LRG_TOP_DOWN_MOVEMENT_TANK:
                /* Tank mode: forward/back only, rotation handled separately */
                input_y = -input_y;  /* Forward = negative in world coords */
                input_x = cosf (priv->facing_angle) * input_y;
                input_y = sinf (priv->facing_angle) * input_y;
                break;
        }

        target_vx = input_x * priv->move_speed;
        target_vy = input_y * priv->move_speed;

        *velocity_x = approach (priv->velocity_x, target_vx, accel);
        *velocity_y = approach (priv->velocity_y, target_vy, accel);
    }
    else
    {
        /* Apply friction when no input */
        *velocity_x = approach (priv->velocity_x, 0.0f, decel);
        *velocity_y = approach (priv->velocity_y, 0.0f, decel);
    }
}

static void
lrg_top_down_template_real_on_facing_changed (LrgTopDownTemplate *self,
                                              LrgFacingDirection  old_facing,
                                              LrgFacingDirection  new_facing)
{
    /* Default: no-op, subclasses can override for animation changes */
    (void)self;
    (void)old_facing;
    (void)new_facing;
}

static gboolean
lrg_top_down_template_real_on_interact (LrgTopDownTemplate *self)
{
    LrgTopDownTemplatePrivate *priv;

    priv = lrg_top_down_template_get_instance_private (self);

    if (priv->interact_target != NULL)
    {
        g_signal_emit (self, signals[SIGNAL_INTERACT], 0, priv->interact_target);
        return TRUE;
    }

    return FALSE;
}

static void
lrg_top_down_template_real_on_interact_target_changed (LrgTopDownTemplate *self,
                                                       gpointer            target)
{
    /* Default: emit signal */
    g_signal_emit (self, signals[SIGNAL_INTERACT_TARGET_CHANGED], 0, target);
}

static void
lrg_top_down_template_real_update_movement (LrgTopDownTemplate *self,
                                            gdouble             delta)
{
    LrgTopDownTemplateClass *klass;
    LrgTopDownTemplatePrivate *priv;
    gfloat new_x;
    gfloat new_y;
    gfloat resolved_x;
    gfloat resolved_y;
    gfloat velocity_mag;
    gboolean was_moving;
    LrgFacingDirection old_facing;

    priv = lrg_top_down_template_get_instance_private (self);
    klass = LRG_TOP_DOWN_TEMPLATE_GET_CLASS (self);
    was_moving = priv->is_moving;
    old_facing = priv->facing;

    /* Handle tank rotation */
    if (priv->movement_mode == LRG_TOP_DOWN_MOVEMENT_TANK)
    {
        gfloat rot_input;

        rot_input = priv->input_x;  /* Left/right = rotation */
        priv->facing_angle += rot_input * priv->rotation_speed * (gfloat)delta;

        /* Normalize angle */
        while (priv->facing_angle < 0)
            priv->facing_angle += 2.0f * (gfloat)G_PI;
        while (priv->facing_angle >= 2.0f * (gfloat)G_PI)
            priv->facing_angle -= 2.0f * (gfloat)G_PI;
    }

    /* Calculate new velocity */
    if (klass->on_movement_input != NULL)
    {
        klass->on_movement_input (self, priv->input_x, priv->input_y, delta,
                                   &priv->velocity_x, &priv->velocity_y);
    }

    /* Calculate new position */
    new_x = priv->player_x + priv->velocity_x * (gfloat)delta;
    new_y = priv->player_y + priv->velocity_y * (gfloat)delta;

    /* Check collision */
    resolved_x = new_x;
    resolved_y = new_y;
    if (klass->check_collision != NULL)
    {
        if (klass->check_collision (self, new_x, new_y, &resolved_x, &resolved_y))
        {
            /* Collision occurred, zero out velocity in collided direction */
            if (fabsf (resolved_x - new_x) > 0.001f)
                priv->velocity_x = 0.0f;
            if (fabsf (resolved_y - new_y) > 0.001f)
                priv->velocity_y = 0.0f;
        }
    }

    /* Update position */
    priv->player_x = resolved_x;
    priv->player_y = resolved_y;

    /* Update movement state */
    velocity_mag = sqrtf (priv->velocity_x * priv->velocity_x +
                          priv->velocity_y * priv->velocity_y);
    priv->is_moving = velocity_mag > 1.0f;

    /* Emit movement state signals */
    if (priv->is_moving && !was_moving)
        g_signal_emit (self, signals[SIGNAL_MOVEMENT_STARTED], 0);
    else if (!priv->is_moving && was_moving)
        g_signal_emit (self, signals[SIGNAL_MOVEMENT_STOPPED], 0);

    /* Update facing direction based on velocity */
    if (velocity_mag > 10.0f)
    {
        gfloat move_angle;
        LrgFacingDirection new_facing;

        move_angle = atan2f (priv->velocity_y, priv->velocity_x);

        if (priv->movement_mode == LRG_TOP_DOWN_MOVEMENT_FREE ||
            priv->movement_mode == LRG_TOP_DOWN_MOVEMENT_TANK)
        {
            priv->facing_angle = move_angle;
        }

        new_facing = facing_from_angle (move_angle, priv->movement_mode);

        if (new_facing != old_facing)
        {
            priv->facing = new_facing;

            if (klass->on_facing_changed != NULL)
                klass->on_facing_changed (self, old_facing, new_facing);

            g_signal_emit (self, signals[SIGNAL_FACING_CHANGED], 0, old_facing, new_facing);
        }
    }

    /* Update camera look-ahead */
    if (priv->look_ahead > 0.0f && velocity_mag > 10.0f)
    {
        gfloat target_look_x;
        gfloat target_look_y;
        gfloat lerp_factor;

        target_look_x = (priv->velocity_x / priv->move_speed) * priv->look_ahead;
        target_look_y = (priv->velocity_y / priv->move_speed) * priv->look_ahead;

        lerp_factor = priv->look_ahead_speed;
        priv->look_ahead_x += (target_look_x - priv->look_ahead_x) * lerp_factor;
        priv->look_ahead_y += (target_look_y - priv->look_ahead_y) * lerp_factor;
    }
    else if (priv->look_ahead > 0.0f)
    {
        /* Return to center when stopped */
        priv->look_ahead_x *= 0.95f;
        priv->look_ahead_y *= 0.95f;
    }
}

static gboolean
lrg_top_down_template_real_check_collision (LrgTopDownTemplate *self,
                                            gfloat              new_x,
                                            gfloat              new_y,
                                            gfloat             *resolved_x,
                                            gfloat             *resolved_y)
{
    /* Default: no collision */
    (void)self;
    *resolved_x = new_x;
    *resolved_y = new_y;
    return FALSE;
}

static void
lrg_top_down_template_real_draw_player (LrgTopDownTemplate *self)
{
    LrgTopDownTemplatePrivate *priv;
    g_autoptr(GrlColor) color = NULL;
    gfloat x;
    gfloat y;
    gfloat w;
    gfloat h;
    gfloat dir_x;
    gfloat dir_y;
    gfloat tip_x;
    gfloat tip_y;
    g_autoptr(GrlColor) line_color = NULL;

    priv = lrg_top_down_template_get_instance_private (self);

    /* Draw player body as rectangle */
    x = priv->player_x - priv->player_width * 0.5f;
    y = priv->player_y - priv->player_height * 0.5f;
    w = priv->player_width;
    h = priv->player_height;

    color = grl_color_new (100, 200, 100, 255);
    grl_draw_rectangle (x, y, w, h, color);

    /* Draw direction indicator */
    dir_x = cosf (priv->facing_angle);
    dir_y = sinf (priv->facing_angle);
    tip_x = priv->player_x + dir_x * priv->player_width * 0.6f;
    tip_y = priv->player_y + dir_y * priv->player_height * 0.6f;

    line_color = grl_color_new (255, 255, 255, 255);
    grl_draw_line ((gint)priv->player_x, (gint)priv->player_y,
                   (gint)tip_x, (gint)tip_y, line_color);
}

static void
lrg_top_down_template_real_draw_interact_prompt (LrgTopDownTemplate *self,
                                                 gfloat              target_x,
                                                 gfloat              target_y)
{
    g_autoptr(GrlColor) color = NULL;
    gfloat prompt_y;

    (void)self;

    /* Draw a simple indicator above the target */
    prompt_y = target_y - 24.0f;
    color = grl_color_new (255, 255, 100, 255);
    grl_draw_circle ((gint)target_x, (gint)prompt_y, 6.0f, color);
}

/* ==========================================================================
 * Overridden Parent Virtual Methods
 * ========================================================================== */

static void
lrg_top_down_template_pre_update (LrgGameTemplate *template,
                                  gdouble          delta)
{
    LrgTopDownTemplate *self;
    LrgTopDownTemplateClass *klass;
    LrgTopDownTemplatePrivate *priv;

    self = LRG_TOP_DOWN_TEMPLATE (template);
    klass = LRG_TOP_DOWN_TEMPLATE_GET_CLASS (self);
    priv = lrg_top_down_template_get_instance_private (self);

    /* Read input */
    priv->input_x = 0.0f;
    priv->input_y = 0.0f;

    if (IsKeyDown (KEY_RIGHT) || IsKeyDown (KEY_D))
        priv->input_x += 1.0f;
    if (IsKeyDown (KEY_LEFT) || IsKeyDown (KEY_A))
        priv->input_x -= 1.0f;
    if (IsKeyDown (KEY_DOWN) || IsKeyDown (KEY_S))
        priv->input_y += 1.0f;
    if (IsKeyDown (KEY_UP) || IsKeyDown (KEY_W))
        priv->input_y -= 1.0f;

    /* Gamepad input */
    if (IsGamepadAvailable (0))
    {
        gfloat gp_x;
        gfloat gp_y;

        gp_x = GetGamepadAxisMovement (0, GAMEPAD_AXIS_LEFT_X);
        gp_y = GetGamepadAxisMovement (0, GAMEPAD_AXIS_LEFT_Y);

        if (fabsf (gp_x) > 0.2f)
            priv->input_x = gp_x;
        if (fabsf (gp_y) > 0.2f)
            priv->input_y = gp_y;
    }

    /* Update movement */
    if (klass->update_movement != NULL)
        klass->update_movement (self, delta);

    /* Check for interact button */
    if (IsKeyPressed (KEY_E) || IsKeyPressed (KEY_SPACE))
    {
        if (klass->on_interact != NULL)
            klass->on_interact (self);
    }

    if (IsGamepadAvailable (0) && IsGamepadButtonPressed (0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))
    {
        if (klass->on_interact != NULL)
            klass->on_interact (self);
    }

    /* Update camera target with look-ahead */
    lrg_game_2d_template_set_camera_target (LRG_GAME_2D_TEMPLATE (self),
                                            priv->player_x + priv->look_ahead_x,
                                            priv->player_y + priv->look_ahead_y);

    /* Chain up */
    LRG_GAME_TEMPLATE_CLASS (lrg_top_down_template_parent_class)->pre_update (template, delta);
}

static void
lrg_top_down_template_draw_world (LrgGame2DTemplate *template)
{
    LrgTopDownTemplate *self;
    LrgTopDownTemplateClass *klass;

    self = LRG_TOP_DOWN_TEMPLATE (template);
    klass = LRG_TOP_DOWN_TEMPLATE_GET_CLASS (self);

    /* Draw player */
    if (klass->draw_player != NULL)
        klass->draw_player (self);

    /* Chain up */
    LRG_GAME_2D_TEMPLATE_CLASS (lrg_top_down_template_parent_class)->draw_world (template);
}

static void
lrg_top_down_template_draw_ui (LrgGame2DTemplate *template)
{
    LrgTopDownTemplate *self;
    LrgTopDownTemplateClass *klass;
    LrgTopDownTemplatePrivate *priv;

    self = LRG_TOP_DOWN_TEMPLATE (template);
    klass = LRG_TOP_DOWN_TEMPLATE_GET_CLASS (self);
    priv = lrg_top_down_template_get_instance_private (self);

    /* If there's an interact target, draw prompt */
    /* Note: target position would need to be stored/provided by subclass */
    (void)priv;
    (void)klass;

    /* Chain up */
    LRG_GAME_2D_TEMPLATE_CLASS (lrg_top_down_template_parent_class)->draw_ui (template);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_top_down_template_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
    LrgTopDownTemplate *self = LRG_TOP_DOWN_TEMPLATE (object);

    switch (prop_id)
    {
        case PROP_PLAYER_X:
            lrg_top_down_template_set_player_position (self, g_value_get_float (value),
                                                       lrg_top_down_template_get_player_y (self));
            break;
        case PROP_PLAYER_Y:
            lrg_top_down_template_set_player_position (self, lrg_top_down_template_get_player_x (self),
                                                       g_value_get_float (value));
            break;
        case PROP_PLAYER_WIDTH:
            lrg_top_down_template_set_player_width (self, g_value_get_float (value));
            break;
        case PROP_PLAYER_HEIGHT:
            lrg_top_down_template_set_player_height (self, g_value_get_float (value));
            break;
        case PROP_MOVEMENT_MODE:
            lrg_top_down_template_set_movement_mode (self, g_value_get_enum (value));
            break;
        case PROP_MOVE_SPEED:
            lrg_top_down_template_set_move_speed (self, g_value_get_float (value));
            break;
        case PROP_ACCELERATION:
            lrg_top_down_template_set_acceleration (self, g_value_get_float (value));
            break;
        case PROP_FRICTION:
            lrg_top_down_template_set_friction (self, g_value_get_float (value));
            break;
        case PROP_ROTATION_SPEED:
            lrg_top_down_template_set_rotation_speed (self, g_value_get_float (value));
            break;
        case PROP_FACING:
            lrg_top_down_template_set_facing (self, g_value_get_enum (value));
            break;
        case PROP_INTERACT_RADIUS:
            lrg_top_down_template_set_interact_radius (self, g_value_get_float (value));
            break;
        case PROP_LOOK_AHEAD:
            lrg_top_down_template_set_look_ahead (self, g_value_get_float (value));
            break;
        case PROP_LOOK_AHEAD_SPEED:
            lrg_top_down_template_set_look_ahead_speed (self, g_value_get_float (value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_top_down_template_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    LrgTopDownTemplate *self = LRG_TOP_DOWN_TEMPLATE (object);
    LrgTopDownTemplatePrivate *priv = lrg_top_down_template_get_instance_private (self);

    switch (prop_id)
    {
        case PROP_PLAYER_X:
            g_value_set_float (value, priv->player_x);
            break;
        case PROP_PLAYER_Y:
            g_value_set_float (value, priv->player_y);
            break;
        case PROP_PLAYER_WIDTH:
            g_value_set_float (value, priv->player_width);
            break;
        case PROP_PLAYER_HEIGHT:
            g_value_set_float (value, priv->player_height);
            break;
        case PROP_MOVEMENT_MODE:
            g_value_set_enum (value, priv->movement_mode);
            break;
        case PROP_MOVE_SPEED:
            g_value_set_float (value, priv->move_speed);
            break;
        case PROP_ACCELERATION:
            g_value_set_float (value, priv->acceleration);
            break;
        case PROP_FRICTION:
            g_value_set_float (value, priv->friction);
            break;
        case PROP_ROTATION_SPEED:
            g_value_set_float (value, priv->rotation_speed);
            break;
        case PROP_FACING:
            g_value_set_enum (value, priv->facing);
            break;
        case PROP_INTERACT_RADIUS:
            g_value_set_float (value, priv->interact_radius);
            break;
        case PROP_LOOK_AHEAD:
            g_value_set_float (value, priv->look_ahead);
            break;
        case PROP_LOOK_AHEAD_SPEED:
            g_value_set_float (value, priv->look_ahead_speed);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_top_down_template_class_init (LrgTopDownTemplateClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    LrgGame2DTemplateClass *template_2d_class = LRG_GAME_2D_TEMPLATE_CLASS (klass);

    object_class->set_property = lrg_top_down_template_set_property;
    object_class->get_property = lrg_top_down_template_get_property;

    /* Override parent virtuals */
    template_class->pre_update = lrg_top_down_template_pre_update;
    template_2d_class->draw_world = lrg_top_down_template_draw_world;
    template_2d_class->draw_ui = lrg_top_down_template_draw_ui;

    /* Set up class virtuals */
    klass->on_movement_input = lrg_top_down_template_real_on_movement_input;
    klass->on_facing_changed = lrg_top_down_template_real_on_facing_changed;
    klass->on_interact = lrg_top_down_template_real_on_interact;
    klass->on_interact_target_changed = lrg_top_down_template_real_on_interact_target_changed;
    klass->update_movement = lrg_top_down_template_real_update_movement;
    klass->check_collision = lrg_top_down_template_real_check_collision;
    klass->draw_player = lrg_top_down_template_real_draw_player;
    klass->draw_interact_prompt = lrg_top_down_template_real_draw_interact_prompt;

    /* Properties */
    properties[PROP_PLAYER_X] =
        g_param_spec_float ("player-x", NULL, NULL,
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_PLAYER_Y] =
        g_param_spec_float ("player-y", NULL, NULL,
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_PLAYER_WIDTH] =
        g_param_spec_float ("player-width", NULL, NULL,
                            1.0f, G_MAXFLOAT, LRG_TOP_DOWN_DEFAULT_PLAYER_WIDTH,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_PLAYER_HEIGHT] =
        g_param_spec_float ("player-height", NULL, NULL,
                            1.0f, G_MAXFLOAT, LRG_TOP_DOWN_DEFAULT_PLAYER_HEIGHT,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MOVEMENT_MODE] =
        g_param_spec_int ("movement-mode", NULL, NULL,
                          LRG_TOP_DOWN_MOVEMENT_4_DIR, LRG_TOP_DOWN_MOVEMENT_TANK,
                          LRG_TOP_DOWN_MOVEMENT_8_DIR,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MOVE_SPEED] =
        g_param_spec_float ("move-speed", NULL, NULL,
                            0.0f, G_MAXFLOAT, LRG_TOP_DOWN_DEFAULT_MOVE_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_ACCELERATION] =
        g_param_spec_float ("acceleration", NULL, NULL,
                            0.0f, G_MAXFLOAT, LRG_TOP_DOWN_DEFAULT_ACCELERATION,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_FRICTION] =
        g_param_spec_float ("friction", NULL, NULL,
                            0.0f, G_MAXFLOAT, LRG_TOP_DOWN_DEFAULT_FRICTION,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_ROTATION_SPEED] =
        g_param_spec_float ("rotation-speed", NULL, NULL,
                            0.0f, G_MAXFLOAT, LRG_TOP_DOWN_DEFAULT_ROTATION_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_FACING] =
        g_param_spec_int ("facing", NULL, NULL,
                          LRG_FACING_DOWN, LRG_FACING_UP_RIGHT,
                          LRG_FACING_DOWN,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_INTERACT_RADIUS] =
        g_param_spec_float ("interact-radius", NULL, NULL,
                            0.0f, G_MAXFLOAT, LRG_TOP_DOWN_DEFAULT_INTERACT_RADIUS,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_LOOK_AHEAD] =
        g_param_spec_float ("look-ahead", NULL, NULL,
                            0.0f, G_MAXFLOAT, LRG_TOP_DOWN_DEFAULT_LOOK_AHEAD,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_LOOK_AHEAD_SPEED] =
        g_param_spec_float ("look-ahead-speed", NULL, NULL,
                            0.0f, 1.0f, LRG_TOP_DOWN_DEFAULT_LOOK_AHEAD_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /* Signals */

    /**
     * LrgTopDownTemplate::facing-changed:
     * @self: the template
     * @old_facing: previous facing direction
     * @new_facing: new facing direction
     *
     * Emitted when the character facing direction changes.
     */
    signals[SIGNAL_FACING_CHANGED] =
        g_signal_new ("facing-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

    /**
     * LrgTopDownTemplate::interact:
     * @self: the template
     * @target: the interaction target
     *
     * Emitted when the player interacts with an object.
     */
    signals[SIGNAL_INTERACT] =
        g_signal_new ("interact",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);

    /**
     * LrgTopDownTemplate::interact-target-changed:
     * @self: the template
     * @target: (nullable): the new closest interactable
     *
     * Emitted when the closest interactable changes.
     */
    signals[SIGNAL_INTERACT_TARGET_CHANGED] =
        g_signal_new ("interact-target-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);

    /**
     * LrgTopDownTemplate::movement-started:
     * @self: the template
     *
     * Emitted when the player starts moving.
     */
    signals[SIGNAL_MOVEMENT_STARTED] =
        g_signal_new ("movement-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgTopDownTemplate::movement-stopped:
     * @self: the template
     *
     * Emitted when the player stops moving.
     */
    signals[SIGNAL_MOVEMENT_STOPPED] =
        g_signal_new ("movement-stopped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_top_down_template_init (LrgTopDownTemplate *self)
{
    LrgTopDownTemplatePrivate *priv;

    priv = lrg_top_down_template_get_instance_private (self);

    priv->player_x = 0.0f;
    priv->player_y = 0.0f;
    priv->velocity_x = 0.0f;
    priv->velocity_y = 0.0f;
    priv->player_width = LRG_TOP_DOWN_DEFAULT_PLAYER_WIDTH;
    priv->player_height = LRG_TOP_DOWN_DEFAULT_PLAYER_HEIGHT;

    priv->movement_mode = LRG_TOP_DOWN_MOVEMENT_8_DIR;
    priv->move_speed = LRG_TOP_DOWN_DEFAULT_MOVE_SPEED;
    priv->acceleration = LRG_TOP_DOWN_DEFAULT_ACCELERATION;
    priv->friction = LRG_TOP_DOWN_DEFAULT_FRICTION;
    priv->rotation_speed = LRG_TOP_DOWN_DEFAULT_ROTATION_SPEED;

    priv->facing = LRG_FACING_DOWN;
    priv->facing_angle = (gfloat)G_PI * 0.5f;  /* Facing down */

    priv->interact_radius = LRG_TOP_DOWN_DEFAULT_INTERACT_RADIUS;
    priv->interact_target = NULL;

    priv->look_ahead = LRG_TOP_DOWN_DEFAULT_LOOK_AHEAD;
    priv->look_ahead_speed = LRG_TOP_DOWN_DEFAULT_LOOK_AHEAD_SPEED;
    priv->look_ahead_x = 0.0f;
    priv->look_ahead_y = 0.0f;

    priv->input_x = 0.0f;
    priv->input_y = 0.0f;
    priv->is_moving = FALSE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgTopDownTemplate *
lrg_top_down_template_new (void)
{
    return g_object_new (LRG_TYPE_TOP_DOWN_TEMPLATE, NULL);
}

gfloat
lrg_top_down_template_get_player_x (LrgTopDownTemplate *self)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self), 0.0f);

    priv = lrg_top_down_template_get_instance_private (self);
    return priv->player_x;
}

gfloat
lrg_top_down_template_get_player_y (LrgTopDownTemplate *self)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self), 0.0f);

    priv = lrg_top_down_template_get_instance_private (self);
    return priv->player_y;
}

void
lrg_top_down_template_set_player_position (LrgTopDownTemplate *self,
                                           gfloat              x,
                                           gfloat              y)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self));

    priv = lrg_top_down_template_get_instance_private (self);
    priv->player_x = x;
    priv->player_y = y;
}

void
lrg_top_down_template_get_player_velocity (LrgTopDownTemplate *self,
                                           gfloat             *velocity_x,
                                           gfloat             *velocity_y)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self));

    priv = lrg_top_down_template_get_instance_private (self);

    if (velocity_x != NULL)
        *velocity_x = priv->velocity_x;
    if (velocity_y != NULL)
        *velocity_y = priv->velocity_y;
}

LrgTopDownMovementMode
lrg_top_down_template_get_movement_mode (LrgTopDownTemplate *self)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self), LRG_TOP_DOWN_MOVEMENT_8_DIR);

    priv = lrg_top_down_template_get_instance_private (self);
    return priv->movement_mode;
}

void
lrg_top_down_template_set_movement_mode (LrgTopDownTemplate     *self,
                                         LrgTopDownMovementMode  mode)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self));

    priv = lrg_top_down_template_get_instance_private (self);
    priv->movement_mode = mode;
}

gfloat
lrg_top_down_template_get_move_speed (LrgTopDownTemplate *self)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self), 0.0f);

    priv = lrg_top_down_template_get_instance_private (self);
    return priv->move_speed;
}

void
lrg_top_down_template_set_move_speed (LrgTopDownTemplate *self,
                                      gfloat              speed)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self));

    priv = lrg_top_down_template_get_instance_private (self);
    priv->move_speed = speed;
}

gfloat
lrg_top_down_template_get_acceleration (LrgTopDownTemplate *self)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self), 0.0f);

    priv = lrg_top_down_template_get_instance_private (self);
    return priv->acceleration;
}

void
lrg_top_down_template_set_acceleration (LrgTopDownTemplate *self,
                                        gfloat              acceleration)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self));

    priv = lrg_top_down_template_get_instance_private (self);
    priv->acceleration = acceleration;
}

gfloat
lrg_top_down_template_get_friction (LrgTopDownTemplate *self)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self), 0.0f);

    priv = lrg_top_down_template_get_instance_private (self);
    return priv->friction;
}

void
lrg_top_down_template_set_friction (LrgTopDownTemplate *self,
                                    gfloat              friction)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self));

    priv = lrg_top_down_template_get_instance_private (self);
    priv->friction = friction;
}

LrgFacingDirection
lrg_top_down_template_get_facing (LrgTopDownTemplate *self)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self), LRG_FACING_DOWN);

    priv = lrg_top_down_template_get_instance_private (self);
    return priv->facing;
}

void
lrg_top_down_template_set_facing (LrgTopDownTemplate *self,
                                  LrgFacingDirection  facing)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self));

    priv = lrg_top_down_template_get_instance_private (self);
    priv->facing = facing;
    priv->facing_angle = angle_from_facing (facing);
}

gfloat
lrg_top_down_template_get_facing_angle (LrgTopDownTemplate *self)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self), 0.0f);

    priv = lrg_top_down_template_get_instance_private (self);
    return priv->facing_angle;
}

gfloat
lrg_top_down_template_get_rotation_speed (LrgTopDownTemplate *self)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self), 0.0f);

    priv = lrg_top_down_template_get_instance_private (self);
    return priv->rotation_speed;
}

void
lrg_top_down_template_set_rotation_speed (LrgTopDownTemplate *self,
                                          gfloat              speed)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self));

    priv = lrg_top_down_template_get_instance_private (self);
    priv->rotation_speed = speed;
}

gfloat
lrg_top_down_template_get_interact_radius (LrgTopDownTemplate *self)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self), 0.0f);

    priv = lrg_top_down_template_get_instance_private (self);
    return priv->interact_radius;
}

void
lrg_top_down_template_set_interact_radius (LrgTopDownTemplate *self,
                                           gfloat              radius)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self));

    priv = lrg_top_down_template_get_instance_private (self);
    priv->interact_radius = radius;
}

gpointer
lrg_top_down_template_get_interact_target (LrgTopDownTemplate *self)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self), NULL);

    priv = lrg_top_down_template_get_instance_private (self);
    return priv->interact_target;
}

void
lrg_top_down_template_set_interact_target (LrgTopDownTemplate *self,
                                           gpointer            target)
{
    LrgTopDownTemplateClass *klass;
    LrgTopDownTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self));

    priv = lrg_top_down_template_get_instance_private (self);
    klass = LRG_TOP_DOWN_TEMPLATE_GET_CLASS (self);

    if (priv->interact_target != target)
    {
        priv->interact_target = target;

        if (klass->on_interact_target_changed != NULL)
            klass->on_interact_target_changed (self, target);
    }
}

gboolean
lrg_top_down_template_trigger_interact (LrgTopDownTemplate *self)
{
    LrgTopDownTemplateClass *klass;

    g_return_val_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self), FALSE);

    klass = LRG_TOP_DOWN_TEMPLATE_GET_CLASS (self);

    if (klass->on_interact != NULL)
        return klass->on_interact (self);

    return FALSE;
}

gfloat
lrg_top_down_template_get_look_ahead (LrgTopDownTemplate *self)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self), 0.0f);

    priv = lrg_top_down_template_get_instance_private (self);
    return priv->look_ahead;
}

void
lrg_top_down_template_set_look_ahead (LrgTopDownTemplate *self,
                                      gfloat              distance)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self));

    priv = lrg_top_down_template_get_instance_private (self);
    priv->look_ahead = distance;
}

gfloat
lrg_top_down_template_get_look_ahead_speed (LrgTopDownTemplate *self)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self), 0.0f);

    priv = lrg_top_down_template_get_instance_private (self);
    return priv->look_ahead_speed;
}

void
lrg_top_down_template_set_look_ahead_speed (LrgTopDownTemplate *self,
                                            gfloat              speed)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self));

    priv = lrg_top_down_template_get_instance_private (self);
    priv->look_ahead_speed = CLAMP (speed, 0.0f, 1.0f);
}

gfloat
lrg_top_down_template_get_player_width (LrgTopDownTemplate *self)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self), 0.0f);

    priv = lrg_top_down_template_get_instance_private (self);
    return priv->player_width;
}

void
lrg_top_down_template_set_player_width (LrgTopDownTemplate *self,
                                        gfloat              width)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self));

    priv = lrg_top_down_template_get_instance_private (self);
    priv->player_width = MAX (width, 1.0f);
}

gfloat
lrg_top_down_template_get_player_height (LrgTopDownTemplate *self)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self), 0.0f);

    priv = lrg_top_down_template_get_instance_private (self);
    return priv->player_height;
}

void
lrg_top_down_template_set_player_height (LrgTopDownTemplate *self,
                                         gfloat              height)
{
    LrgTopDownTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TOP_DOWN_TEMPLATE (self));

    priv = lrg_top_down_template_get_instance_private (self);
    priv->player_height = MAX (height, 1.0f);
}
