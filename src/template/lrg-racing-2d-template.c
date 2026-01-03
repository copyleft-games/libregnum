/* lrg-racing-2d-template.c - 2D top-down racing game template implementation
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include "lrg-racing-2d-template.h"
#include "../lrg-log.h"

#include <graylib.h>
#include <raylib.h>
#include <math.h>

/* Default values */
#define LRG_RACING_2D_DEFAULT_MAX_SPEED        (400.0f)
#define LRG_RACING_2D_DEFAULT_ACCELERATION     (300.0f)
#define LRG_RACING_2D_DEFAULT_BRAKE_POWER      (500.0f)
#define LRG_RACING_2D_DEFAULT_TURN_SPEED       (3.0f)
#define LRG_RACING_2D_DEFAULT_GRIP             (0.8f)
#define LRG_RACING_2D_DEFAULT_DRIFT_THRESHOLD  (200.0f)
#define LRG_RACING_2D_DEFAULT_FRICTION         (100.0f)
#define LRG_RACING_2D_DEFAULT_BOOST_MULTIPLIER (1.5f)
#define LRG_RACING_2D_DEFAULT_BOOST_DRAIN      (0.3f)    /* Per second */
#define LRG_RACING_2D_DEFAULT_COUNTDOWN_TIME   (3.0f)
#define LRG_RACING_2D_DEFAULT_LOOK_AHEAD       (100.0f)

typedef struct
{
    /* Race state */
    LrgRaceState race_state;
    LrgRaceState state_before_pause;
    gfloat countdown_timer;
    gint countdown_value;

    /* Vehicle position/state */
    gfloat vehicle_x;
    gfloat vehicle_y;
    gfloat vehicle_angle;
    gfloat speed;
    gfloat velocity_x;
    gfloat velocity_y;
    gboolean is_drifting;

    /* Vehicle settings */
    gfloat max_speed;
    gfloat acceleration;
    gfloat brake_power;
    gfloat turn_speed;
    gfloat grip;
    gfloat drift_threshold;
    gfloat friction;

    /* Boost system */
    gfloat boost;
    gfloat boost_multiplier;
    gfloat boost_drain;
    gboolean is_boosting;

    /* Lap tracking */
    guint current_lap;
    guint total_laps;
    guint current_checkpoint;
    guint total_checkpoints;

    /* Time tracking */
    gfloat race_time;
    gfloat lap_time;
    gfloat best_lap_time;

    /* Camera */
    gfloat look_ahead;

    /* Input state */
    gfloat throttle_input;
    gfloat steer_input;
    gboolean brake_input;
    gboolean boost_input;
} LrgRacing2DTemplatePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgRacing2DTemplate, lrg_racing_2d_template,
                            LRG_TYPE_GAME_2D_TEMPLATE)

/* Property IDs */
enum
{
    PROP_0,
    PROP_RACE_STATE,
    PROP_MAX_SPEED,
    PROP_ACCELERATION,
    PROP_BRAKE_POWER,
    PROP_TURN_SPEED,
    PROP_GRIP,
    PROP_DRIFT_THRESHOLD,
    PROP_TOTAL_LAPS,
    PROP_BOOST,
    PROP_BOOST_MULTIPLIER,
    PROP_LOOK_AHEAD,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Signal IDs */
enum
{
    SIGNAL_RACE_STATE_CHANGED,
    SIGNAL_LAP_COMPLETE,
    SIGNAL_CHECKPOINT_PASSED,
    SIGNAL_COUNTDOWN_TICK,
    SIGNAL_COLLISION,
    SIGNAL_BOOST_STARTED,
    SIGNAL_BOOST_ENDED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

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
get_surface_grip_modifier (LrgSurfaceType surface)
{
    switch (surface)
    {
        case LRG_SURFACE_ROAD:    return 1.0f;
        case LRG_SURFACE_OFFROAD: return 0.6f;
        case LRG_SURFACE_ICE:     return 0.2f;
        case LRG_SURFACE_BOOST:   return 1.0f;
        case LRG_SURFACE_SLOW:    return 0.8f;
        case LRG_SURFACE_DAMAGE:  return 0.7f;
        default:                  return 1.0f;
    }
}

static gfloat
get_surface_speed_modifier (LrgSurfaceType surface)
{
    switch (surface)
    {
        case LRG_SURFACE_ROAD:    return 1.0f;
        case LRG_SURFACE_OFFROAD: return 0.7f;
        case LRG_SURFACE_ICE:     return 0.9f;
        case LRG_SURFACE_BOOST:   return 1.3f;
        case LRG_SURFACE_SLOW:    return 0.4f;
        case LRG_SURFACE_DAMAGE:  return 0.8f;
        default:                  return 1.0f;
    }
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static void
lrg_racing_2d_template_real_on_race_state_changed (LrgRacing2DTemplate *self,
                                                   LrgRaceState         old_state,
                                                   LrgRaceState         new_state)
{
    /* Default: emit signal only */
    g_signal_emit (self, signals[SIGNAL_RACE_STATE_CHANGED], 0, old_state, new_state);
}

static void
lrg_racing_2d_template_real_on_lap_complete (LrgRacing2DTemplate *self,
                                             guint                lap,
                                             gfloat               lap_time)
{
    g_signal_emit (self, signals[SIGNAL_LAP_COMPLETE], 0, lap, lap_time);
}

static void
lrg_racing_2d_template_real_on_checkpoint_passed (LrgRacing2DTemplate *self,
                                                  guint                checkpoint)
{
    g_signal_emit (self, signals[SIGNAL_CHECKPOINT_PASSED], 0, checkpoint);
}

static void
lrg_racing_2d_template_real_on_countdown_tick (LrgRacing2DTemplate *self,
                                               gint                 count)
{
    g_signal_emit (self, signals[SIGNAL_COUNTDOWN_TICK], 0, count);
}

static void
lrg_racing_2d_template_real_on_collision (LrgRacing2DTemplate *self,
                                          gfloat               impact_speed)
{
    g_signal_emit (self, signals[SIGNAL_COLLISION], 0, impact_speed);
}

static void
lrg_racing_2d_template_real_update_vehicle (LrgRacing2DTemplate *self,
                                            gdouble              delta)
{
    LrgRacing2DTemplateClass *klass;
    LrgRacing2DTemplatePrivate *priv;
    LrgSurfaceType surface;
    gfloat grip_mod;
    gfloat speed_mod;
    gfloat effective_max_speed;
    gfloat effective_grip;
    gfloat target_speed;
    gfloat turn_factor;
    gfloat forward_x;
    gfloat forward_y;
    gfloat lateral_vel;
    gfloat slip_angle;

    priv = lrg_racing_2d_template_get_instance_private (self);
    klass = LRG_RACING_2D_TEMPLATE_GET_CLASS (self);

    /* Get surface at vehicle position */
    surface = LRG_SURFACE_ROAD;
    if (klass->get_surface_at != NULL)
        surface = klass->get_surface_at (self, priv->vehicle_x, priv->vehicle_y);

    grip_mod = get_surface_grip_modifier (surface);
    speed_mod = get_surface_speed_modifier (surface);

    effective_max_speed = priv->max_speed * speed_mod;
    effective_grip = priv->grip * grip_mod;

    /* Apply boost */
    if (priv->is_boosting && priv->boost > 0.0f)
    {
        effective_max_speed *= priv->boost_multiplier;
        priv->boost -= priv->boost_drain * (gfloat)delta;
        if (priv->boost <= 0.0f)
        {
            priv->boost = 0.0f;
            priv->is_boosting = FALSE;
            g_signal_emit (self, signals[SIGNAL_BOOST_ENDED], 0);
        }
    }

    /* Calculate target speed based on input */
    target_speed = 0.0f;
    if (priv->throttle_input > 0.0f)
    {
        target_speed = effective_max_speed * priv->throttle_input;
    }
    else if (priv->throttle_input < 0.0f)
    {
        /* Reverse at half speed */
        target_speed = priv->max_speed * 0.5f * priv->throttle_input;
    }

    /* Apply braking */
    if (priv->brake_input && priv->speed > 0.0f)
    {
        priv->speed -= priv->brake_power * (gfloat)delta;
        if (priv->speed < 0.0f)
            priv->speed = 0.0f;
    }
    else
    {
        /* Accelerate/decelerate toward target */
        if (priv->speed < target_speed)
        {
            priv->speed += priv->acceleration * (gfloat)delta;
            if (priv->speed > target_speed)
                priv->speed = target_speed;
        }
        else if (priv->speed > target_speed)
        {
            priv->speed -= priv->friction * (gfloat)delta;
            if (priv->speed < target_speed)
                priv->speed = target_speed;
        }
    }

    /* Steering - only effective when moving */
    turn_factor = priv->speed / priv->max_speed;
    turn_factor = CLAMP (turn_factor, 0.0f, 1.0f);

    if (fabsf (priv->steer_input) > 0.1f && priv->speed > 10.0f)
    {
        gfloat turn_amount;

        turn_amount = priv->steer_input * priv->turn_speed * turn_factor * (gfloat)delta;
        priv->vehicle_angle += turn_amount;
    }

    /* Calculate forward direction */
    forward_x = cosf (priv->vehicle_angle);
    forward_y = sinf (priv->vehicle_angle);

    /* Calculate velocity with grip/drift */
    priv->velocity_x = forward_x * priv->speed;
    priv->velocity_y = forward_y * priv->speed;

    /* Calculate lateral velocity for drift detection */
    lateral_vel = -priv->velocity_x * sinf (priv->vehicle_angle) +
                   priv->velocity_y * cosf (priv->vehicle_angle);
    slip_angle = fabsf (lateral_vel);

    priv->is_drifting = slip_angle > 20.0f && priv->speed > priv->drift_threshold;

    /* Apply grip - blend between ideal velocity and current direction */
    if (priv->speed > 0.0f)
    {
        gfloat vel_mag;
        gfloat vel_angle;
        gfloat angle_diff;
        gfloat grip_factor;

        vel_mag = sqrtf (priv->velocity_x * priv->velocity_x +
                         priv->velocity_y * priv->velocity_y);

        if (vel_mag > 0.0f)
        {
            vel_angle = atan2f (priv->velocity_y, priv->velocity_x);
            angle_diff = priv->vehicle_angle - vel_angle;

            /* Normalize angle difference */
            while (angle_diff > (gfloat)G_PI)
                angle_diff -= 2.0f * (gfloat)G_PI;
            while (angle_diff < -(gfloat)G_PI)
                angle_diff += 2.0f * (gfloat)G_PI;

            /* Grip determines how quickly velocity aligns with heading */
            grip_factor = effective_grip;
            priv->velocity_x = lerp (priv->velocity_x, forward_x * vel_mag, grip_factor);
            priv->velocity_y = lerp (priv->velocity_y, forward_y * vel_mag, grip_factor);
        }
    }

    /* Update position */
    priv->vehicle_x += priv->velocity_x * (gfloat)delta;
    priv->vehicle_y += priv->velocity_y * (gfloat)delta;
}

static LrgSurfaceType
lrg_racing_2d_template_real_get_surface_at (LrgRacing2DTemplate *self,
                                            gfloat               x,
                                            gfloat               y)
{
    /* Default: all road */
    (void)self;
    (void)x;
    (void)y;
    return LRG_SURFACE_ROAD;
}

static void
lrg_racing_2d_template_real_draw_vehicle (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;
    gfloat x;
    gfloat y;
    gfloat angle_deg;
    g_autoptr(GrlColor) body_color = NULL;
    g_autoptr(GrlColor) wheel_color = NULL;

    priv = lrg_racing_2d_template_get_instance_private (self);

    x = priv->vehicle_x;
    y = priv->vehicle_y;
    angle_deg = priv->vehicle_angle * 180.0f / (gfloat)G_PI;

    /* Draw simple car shape */
    /* Car body */
    body_color = grl_color_new (200, 50, 50, 255);
    DrawRectanglePro ((Rectangle){x, y, 40.0f, 20.0f},
                      (Vector2){20.0f, 10.0f}, angle_deg, RED);

    /* Wheels */
    wheel_color = grl_color_new (40, 40, 40, 255);
    /* (Simplified - just draw the car body for placeholder) */

    /* Draw boost effect */
    if (priv->is_boosting)
    {
        gfloat back_x;
        gfloat back_y;
        g_autoptr(GrlColor) boost_color = NULL;

        back_x = x - cosf (priv->vehicle_angle) * 25.0f;
        back_y = y - sinf (priv->vehicle_angle) * 25.0f;

        boost_color = grl_color_new (255, 150, 0, 200);
        grl_draw_circle ((gint)back_x, (gint)back_y, 10.0f, boost_color);
    }

    /* Draw drift sparks */
    if (priv->is_drifting)
    {
        g_autoptr(GrlColor) spark_color = NULL;

        spark_color = grl_color_new (255, 255, 100, 200);
        grl_draw_circle ((gint)(x - cosf (priv->vehicle_angle) * 15.0f),
                         (gint)(y - sinf (priv->vehicle_angle) * 15.0f),
                         4.0f, spark_color);
    }
}

static void
lrg_racing_2d_template_real_draw_race_ui (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;
    g_autofree gchar *speed_text = NULL;
    g_autofree gchar *lap_text = NULL;
    g_autofree gchar *time_text = NULL;
    g_autoptr(GrlColor) text_color = NULL;
    gint virt_w;
    gint virt_h;
    gint minutes;
    gfloat seconds;

    priv = lrg_racing_2d_template_get_instance_private (self);

    virt_w = lrg_game_2d_template_get_virtual_width (LRG_GAME_2D_TEMPLATE (self));
    virt_h = lrg_game_2d_template_get_virtual_height (LRG_GAME_2D_TEMPLATE (self));

    text_color = grl_color_new (255, 255, 255, 255);

    /* Speed */
    speed_text = g_strdup_printf ("%.0f km/h", priv->speed * 3.6f);  /* Arbitrary units */
    grl_draw_text (speed_text, 10, virt_h - 40, 24, text_color);

    /* Lap counter */
    lap_text = g_strdup_printf ("LAP %u/%u", priv->current_lap, priv->total_laps);
    grl_draw_text (lap_text, virt_w - 120, 10, 20, text_color);

    /* Race time */
    minutes = (gint)(priv->race_time / 60.0f);
    seconds = fmodf (priv->race_time, 60.0f);
    time_text = g_strdup_printf ("%d:%05.2f", minutes, seconds);
    grl_draw_text (time_text, virt_w / 2 - 40, 10, 24, text_color);

    /* Boost bar */
    if (priv->boost > 0.0f)
    {
        g_autoptr(GrlColor) bar_bg = NULL;
        g_autoptr(GrlColor) bar_fg = NULL;

        bar_bg = grl_color_new (50, 50, 50, 200);
        bar_fg = grl_color_new (255, 150, 0, 255);

        grl_draw_rectangle (10.0f, (gfloat)(virt_h - 60), 100.0f, 10.0f, bar_bg);
        grl_draw_rectangle (10.0f, (gfloat)(virt_h - 60), 100.0f * priv->boost, 10.0f, bar_fg);
    }

    /* Countdown display */
    if (priv->race_state == LRG_RACE_STATE_COUNTDOWN)
    {
        g_autofree gchar *countdown_text = NULL;
        g_autoptr(GrlColor) countdown_color = NULL;

        if (priv->countdown_value > 0)
            countdown_text = g_strdup_printf ("%d", priv->countdown_value);
        else
            countdown_text = g_strdup ("GO!");

        countdown_color = grl_color_new (255, 255, 0, 255);
        grl_draw_text (countdown_text, virt_w / 2 - 30, virt_h / 2 - 40, 80, countdown_color);
    }
}

/* ==========================================================================
 * Overridden Parent Virtual Methods
 * ========================================================================== */

static void
lrg_racing_2d_template_pre_update (LrgGameTemplate *template,
                                   gdouble          delta)
{
    LrgRacing2DTemplate *self;
    LrgRacing2DTemplateClass *klass;
    LrgRacing2DTemplatePrivate *priv;

    self = LRG_RACING_2D_TEMPLATE (template);
    klass = LRG_RACING_2D_TEMPLATE_GET_CLASS (self);
    priv = lrg_racing_2d_template_get_instance_private (self);

    /* Handle pause */
    if (IsKeyPressed (KEY_ESCAPE) || IsKeyPressed (KEY_P))
    {
        if (priv->race_state == LRG_RACE_STATE_RACING)
            lrg_racing_2d_template_pause_race (self);
        else if (priv->race_state == LRG_RACE_STATE_PAUSED)
            lrg_racing_2d_template_resume_race (self);
    }

    /* Process based on race state */
    switch (priv->race_state)
    {
        case LRG_RACE_STATE_COUNTDOWN:
        {
            gint old_countdown;

            old_countdown = priv->countdown_value;
            priv->countdown_timer -= (gfloat)delta;

            priv->countdown_value = (gint)ceilf (priv->countdown_timer);

            if (priv->countdown_value != old_countdown && klass->on_countdown_tick != NULL)
                klass->on_countdown_tick (self, priv->countdown_value);

            if (priv->countdown_timer <= 0.0f)
                lrg_racing_2d_template_start_race (self);
            break;
        }

        case LRG_RACE_STATE_RACING:
        {
            /* Read input */
            priv->throttle_input = 0.0f;
            priv->steer_input = 0.0f;
            priv->brake_input = FALSE;
            priv->boost_input = FALSE;

            if (IsKeyDown (KEY_UP) || IsKeyDown (KEY_W))
                priv->throttle_input = 1.0f;
            if (IsKeyDown (KEY_DOWN) || IsKeyDown (KEY_S))
                priv->throttle_input = -1.0f;
            if (IsKeyDown (KEY_RIGHT) || IsKeyDown (KEY_D))
                priv->steer_input = 1.0f;
            if (IsKeyDown (KEY_LEFT) || IsKeyDown (KEY_A))
                priv->steer_input = -1.0f;
            if (IsKeyDown (KEY_SPACE))
                priv->brake_input = TRUE;
            if (IsKeyDown (KEY_LEFT_SHIFT) || IsKeyDown (KEY_RIGHT_SHIFT))
                priv->boost_input = TRUE;

            /* Gamepad input */
            if (IsGamepadAvailable (0))
            {
                gfloat trigger;

                /* Right trigger = throttle, left trigger = brake */
                trigger = GetGamepadAxisMovement (0, GAMEPAD_AXIS_RIGHT_TRIGGER);
                if (trigger > 0.1f)
                    priv->throttle_input = trigger;

                trigger = GetGamepadAxisMovement (0, GAMEPAD_AXIS_LEFT_TRIGGER);
                if (trigger > 0.1f)
                {
                    priv->brake_input = TRUE;
                    if (priv->throttle_input == 0.0f)
                        priv->throttle_input = -trigger;
                }

                priv->steer_input = GetGamepadAxisMovement (0, GAMEPAD_AXIS_LEFT_X);
                if (fabsf (priv->steer_input) < 0.15f)
                    priv->steer_input = 0.0f;

                if (IsGamepadButtonDown (0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))
                    priv->boost_input = TRUE;
            }

            /* Handle boost activation */
            if (priv->boost_input && priv->boost > 0.0f && !priv->is_boosting)
            {
                priv->is_boosting = TRUE;
                g_signal_emit (self, signals[SIGNAL_BOOST_STARTED], 0);
            }
            else if (!priv->boost_input && priv->is_boosting)
            {
                priv->is_boosting = FALSE;
                g_signal_emit (self, signals[SIGNAL_BOOST_ENDED], 0);
            }

            /* Update vehicle physics */
            if (klass->update_vehicle != NULL)
                klass->update_vehicle (self, delta);

            /* Update timers */
            priv->race_time += (gfloat)delta;
            priv->lap_time += (gfloat)delta;

            /* Update camera to follow vehicle with look-ahead */
            {
                gfloat look_x;
                gfloat look_y;

                look_x = priv->vehicle_x + cosf (priv->vehicle_angle) * priv->look_ahead;
                look_y = priv->vehicle_y + sinf (priv->vehicle_angle) * priv->look_ahead;

                lrg_game_2d_template_set_camera_target (LRG_GAME_2D_TEMPLATE (self),
                                                        look_x, look_y);
            }
            break;
        }

        case LRG_RACE_STATE_PAUSED:
        case LRG_RACE_STATE_WAITING:
        case LRG_RACE_STATE_FINISHED:
            /* No update */
            break;
    }

    /* Chain up */
    LRG_GAME_TEMPLATE_CLASS (lrg_racing_2d_template_parent_class)->pre_update (template, delta);
}

static void
lrg_racing_2d_template_draw_world (LrgGame2DTemplate *template)
{
    LrgRacing2DTemplate *self;
    LrgRacing2DTemplateClass *klass;

    self = LRG_RACING_2D_TEMPLATE (template);
    klass = LRG_RACING_2D_TEMPLATE_GET_CLASS (self);

    /* Draw vehicle */
    if (klass->draw_vehicle != NULL)
        klass->draw_vehicle (self);

    /* Chain up */
    LRG_GAME_2D_TEMPLATE_CLASS (lrg_racing_2d_template_parent_class)->draw_world (template);
}

static void
lrg_racing_2d_template_draw_ui (LrgGame2DTemplate *template)
{
    LrgRacing2DTemplate *self;
    LrgRacing2DTemplateClass *klass;

    self = LRG_RACING_2D_TEMPLATE (template);
    klass = LRG_RACING_2D_TEMPLATE_GET_CLASS (self);

    /* Draw race UI */
    if (klass->draw_race_ui != NULL)
        klass->draw_race_ui (self);

    /* Chain up */
    LRG_GAME_2D_TEMPLATE_CLASS (lrg_racing_2d_template_parent_class)->draw_ui (template);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_racing_2d_template_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
    LrgRacing2DTemplate *self = LRG_RACING_2D_TEMPLATE (object);

    switch (prop_id)
    {
        case PROP_MAX_SPEED:
            lrg_racing_2d_template_set_max_speed (self, g_value_get_float (value));
            break;
        case PROP_ACCELERATION:
            lrg_racing_2d_template_set_acceleration (self, g_value_get_float (value));
            break;
        case PROP_BRAKE_POWER:
            lrg_racing_2d_template_set_brake_power (self, g_value_get_float (value));
            break;
        case PROP_TURN_SPEED:
            lrg_racing_2d_template_set_turn_speed (self, g_value_get_float (value));
            break;
        case PROP_GRIP:
            lrg_racing_2d_template_set_grip (self, g_value_get_float (value));
            break;
        case PROP_DRIFT_THRESHOLD:
            lrg_racing_2d_template_set_drift_threshold (self, g_value_get_float (value));
            break;
        case PROP_TOTAL_LAPS:
            lrg_racing_2d_template_set_total_laps (self, g_value_get_uint (value));
            break;
        case PROP_BOOST:
            lrg_racing_2d_template_set_boost (self, g_value_get_float (value));
            break;
        case PROP_BOOST_MULTIPLIER:
            lrg_racing_2d_template_set_boost_multiplier (self, g_value_get_float (value));
            break;
        case PROP_LOOK_AHEAD:
            {
                LrgRacing2DTemplatePrivate *priv;
                priv = lrg_racing_2d_template_get_instance_private (self);
                priv->look_ahead = g_value_get_float (value);
            }
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_racing_2d_template_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
    LrgRacing2DTemplate *self = LRG_RACING_2D_TEMPLATE (object);
    LrgRacing2DTemplatePrivate *priv = lrg_racing_2d_template_get_instance_private (self);

    switch (prop_id)
    {
        case PROP_RACE_STATE:
            g_value_set_enum (value, priv->race_state);
            break;
        case PROP_MAX_SPEED:
            g_value_set_float (value, priv->max_speed);
            break;
        case PROP_ACCELERATION:
            g_value_set_float (value, priv->acceleration);
            break;
        case PROP_BRAKE_POWER:
            g_value_set_float (value, priv->brake_power);
            break;
        case PROP_TURN_SPEED:
            g_value_set_float (value, priv->turn_speed);
            break;
        case PROP_GRIP:
            g_value_set_float (value, priv->grip);
            break;
        case PROP_DRIFT_THRESHOLD:
            g_value_set_float (value, priv->drift_threshold);
            break;
        case PROP_TOTAL_LAPS:
            g_value_set_uint (value, priv->total_laps);
            break;
        case PROP_BOOST:
            g_value_set_float (value, priv->boost);
            break;
        case PROP_BOOST_MULTIPLIER:
            g_value_set_float (value, priv->boost_multiplier);
            break;
        case PROP_LOOK_AHEAD:
            g_value_set_float (value, priv->look_ahead);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_racing_2d_template_class_init (LrgRacing2DTemplateClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    LrgGame2DTemplateClass *template_2d_class = LRG_GAME_2D_TEMPLATE_CLASS (klass);

    object_class->set_property = lrg_racing_2d_template_set_property;
    object_class->get_property = lrg_racing_2d_template_get_property;

    /* Override parent virtuals */
    template_class->pre_update = lrg_racing_2d_template_pre_update;
    template_2d_class->draw_world = lrg_racing_2d_template_draw_world;
    template_2d_class->draw_ui = lrg_racing_2d_template_draw_ui;

    /* Set up class virtuals */
    klass->on_race_state_changed = lrg_racing_2d_template_real_on_race_state_changed;
    klass->on_lap_complete = lrg_racing_2d_template_real_on_lap_complete;
    klass->on_checkpoint_passed = lrg_racing_2d_template_real_on_checkpoint_passed;
    klass->on_countdown_tick = lrg_racing_2d_template_real_on_countdown_tick;
    klass->on_collision = lrg_racing_2d_template_real_on_collision;
    klass->update_vehicle = lrg_racing_2d_template_real_update_vehicle;
    klass->get_surface_at = lrg_racing_2d_template_real_get_surface_at;
    klass->draw_vehicle = lrg_racing_2d_template_real_draw_vehicle;
    klass->draw_race_ui = lrg_racing_2d_template_real_draw_race_ui;

    /* Properties */
    properties[PROP_RACE_STATE] =
        g_param_spec_int ("race-state", NULL, NULL,
                          LRG_RACE_STATE_WAITING, LRG_RACE_STATE_PAUSED,
                          LRG_RACE_STATE_WAITING,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_SPEED] =
        g_param_spec_float ("max-speed", NULL, NULL,
                            1.0f, G_MAXFLOAT, LRG_RACING_2D_DEFAULT_MAX_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_ACCELERATION] =
        g_param_spec_float ("acceleration", NULL, NULL,
                            1.0f, G_MAXFLOAT, LRG_RACING_2D_DEFAULT_ACCELERATION,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_BRAKE_POWER] =
        g_param_spec_float ("brake-power", NULL, NULL,
                            1.0f, G_MAXFLOAT, LRG_RACING_2D_DEFAULT_BRAKE_POWER,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_TURN_SPEED] =
        g_param_spec_float ("turn-speed", NULL, NULL,
                            0.1f, 10.0f, LRG_RACING_2D_DEFAULT_TURN_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_GRIP] =
        g_param_spec_float ("grip", NULL, NULL,
                            0.0f, 1.0f, LRG_RACING_2D_DEFAULT_GRIP,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DRIFT_THRESHOLD] =
        g_param_spec_float ("drift-threshold", NULL, NULL,
                            0.0f, G_MAXFLOAT, LRG_RACING_2D_DEFAULT_DRIFT_THRESHOLD,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_TOTAL_LAPS] =
        g_param_spec_uint ("total-laps", NULL, NULL,
                           1, 100, 3,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_BOOST] =
        g_param_spec_float ("boost", NULL, NULL,
                            0.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_BOOST_MULTIPLIER] =
        g_param_spec_float ("boost-multiplier", NULL, NULL,
                            1.0f, 3.0f, LRG_RACING_2D_DEFAULT_BOOST_MULTIPLIER,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_LOOK_AHEAD] =
        g_param_spec_float ("look-ahead", NULL, NULL,
                            0.0f, G_MAXFLOAT, LRG_RACING_2D_DEFAULT_LOOK_AHEAD,
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
                      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_FLOAT);

    signals[SIGNAL_CHECKPOINT_PASSED] =
        g_signal_new ("checkpoint-passed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_UINT);

    signals[SIGNAL_COUNTDOWN_TICK] =
        g_signal_new ("countdown-tick",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_INT);

    signals[SIGNAL_COLLISION] =
        g_signal_new ("collision",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_FLOAT);

    signals[SIGNAL_BOOST_STARTED] =
        g_signal_new ("boost-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    signals[SIGNAL_BOOST_ENDED] =
        g_signal_new ("boost-ended",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_racing_2d_template_init (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    priv = lrg_racing_2d_template_get_instance_private (self);

    priv->race_state = LRG_RACE_STATE_WAITING;
    priv->countdown_timer = LRG_RACING_2D_DEFAULT_COUNTDOWN_TIME;
    priv->countdown_value = 3;

    priv->vehicle_x = 0.0f;
    priv->vehicle_y = 0.0f;
    priv->vehicle_angle = 0.0f;
    priv->speed = 0.0f;
    priv->velocity_x = 0.0f;
    priv->velocity_y = 0.0f;
    priv->is_drifting = FALSE;

    priv->max_speed = LRG_RACING_2D_DEFAULT_MAX_SPEED;
    priv->acceleration = LRG_RACING_2D_DEFAULT_ACCELERATION;
    priv->brake_power = LRG_RACING_2D_DEFAULT_BRAKE_POWER;
    priv->turn_speed = LRG_RACING_2D_DEFAULT_TURN_SPEED;
    priv->grip = LRG_RACING_2D_DEFAULT_GRIP;
    priv->drift_threshold = LRG_RACING_2D_DEFAULT_DRIFT_THRESHOLD;
    priv->friction = LRG_RACING_2D_DEFAULT_FRICTION;

    priv->boost = 0.0f;
    priv->boost_multiplier = LRG_RACING_2D_DEFAULT_BOOST_MULTIPLIER;
    priv->boost_drain = LRG_RACING_2D_DEFAULT_BOOST_DRAIN;
    priv->is_boosting = FALSE;

    priv->current_lap = 1;
    priv->total_laps = 3;
    priv->current_checkpoint = 0;
    priv->total_checkpoints = 0;

    priv->race_time = 0.0f;
    priv->lap_time = 0.0f;
    priv->best_lap_time = -1.0f;

    priv->look_ahead = LRG_RACING_2D_DEFAULT_LOOK_AHEAD;
}

/* ==========================================================================
 * Public API Implementation
 * ========================================================================== */

LrgRacing2DTemplate *
lrg_racing_2d_template_new (void)
{
    return g_object_new (LRG_TYPE_RACING_2D_TEMPLATE, NULL);
}

LrgRaceState
lrg_racing_2d_template_get_race_state (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_2D_TEMPLATE (self), LRG_RACE_STATE_WAITING);

    priv = lrg_racing_2d_template_get_instance_private (self);
    return priv->race_state;
}

static void
set_race_state (LrgRacing2DTemplate *self,
                LrgRaceState         new_state)
{
    LrgRacing2DTemplateClass *klass;
    LrgRacing2DTemplatePrivate *priv;
    LrgRaceState old_state;

    priv = lrg_racing_2d_template_get_instance_private (self);
    klass = LRG_RACING_2D_TEMPLATE_GET_CLASS (self);

    if (priv->race_state == new_state)
        return;

    old_state = priv->race_state;
    priv->race_state = new_state;

    if (klass->on_race_state_changed != NULL)
        klass->on_race_state_changed (self, old_state, new_state);
}

void
lrg_racing_2d_template_start_countdown (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_2D_TEMPLATE (self));

    priv = lrg_racing_2d_template_get_instance_private (self);
    priv->countdown_timer = LRG_RACING_2D_DEFAULT_COUNTDOWN_TIME;
    priv->countdown_value = 3;

    set_race_state (self, LRG_RACE_STATE_COUNTDOWN);
}

void
lrg_racing_2d_template_start_race (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_2D_TEMPLATE (self));

    priv = lrg_racing_2d_template_get_instance_private (self);
    priv->race_time = 0.0f;
    priv->lap_time = 0.0f;
    priv->current_lap = 1;
    priv->current_checkpoint = 0;

    set_race_state (self, LRG_RACE_STATE_RACING);
}

void
lrg_racing_2d_template_finish_race (LrgRacing2DTemplate *self)
{
    g_return_if_fail (LRG_IS_RACING_2D_TEMPLATE (self));
    set_race_state (self, LRG_RACE_STATE_FINISHED);
}

void
lrg_racing_2d_template_pause_race (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_2D_TEMPLATE (self));

    priv = lrg_racing_2d_template_get_instance_private (self);

    if (priv->race_state != LRG_RACE_STATE_RACING)
        return;

    priv->state_before_pause = priv->race_state;
    set_race_state (self, LRG_RACE_STATE_PAUSED);
}

void
lrg_racing_2d_template_resume_race (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_2D_TEMPLATE (self));

    priv = lrg_racing_2d_template_get_instance_private (self);

    if (priv->race_state != LRG_RACE_STATE_PAUSED)
        return;

    set_race_state (self, priv->state_before_pause);
}

void
lrg_racing_2d_template_reset_race (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_2D_TEMPLATE (self));

    priv = lrg_racing_2d_template_get_instance_private (self);

    priv->race_time = 0.0f;
    priv->lap_time = 0.0f;
    priv->best_lap_time = -1.0f;
    priv->current_lap = 1;
    priv->current_checkpoint = 0;
    priv->speed = 0.0f;
    priv->velocity_x = 0.0f;
    priv->velocity_y = 0.0f;
    priv->boost = 0.0f;
    priv->is_boosting = FALSE;
    priv->is_drifting = FALSE;

    set_race_state (self, LRG_RACE_STATE_WAITING);
}

gfloat
lrg_racing_2d_template_get_vehicle_x (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_2D_TEMPLATE (self), 0.0f);

    priv = lrg_racing_2d_template_get_instance_private (self);
    return priv->vehicle_x;
}

gfloat
lrg_racing_2d_template_get_vehicle_y (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_2D_TEMPLATE (self), 0.0f);

    priv = lrg_racing_2d_template_get_instance_private (self);
    return priv->vehicle_y;
}

void
lrg_racing_2d_template_set_vehicle_position (LrgRacing2DTemplate *self,
                                             gfloat               x,
                                             gfloat               y)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_2D_TEMPLATE (self));

    priv = lrg_racing_2d_template_get_instance_private (self);
    priv->vehicle_x = x;
    priv->vehicle_y = y;
}

gfloat
lrg_racing_2d_template_get_vehicle_angle (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_2D_TEMPLATE (self), 0.0f);

    priv = lrg_racing_2d_template_get_instance_private (self);
    return priv->vehicle_angle;
}

void
lrg_racing_2d_template_set_vehicle_angle (LrgRacing2DTemplate *self,
                                          gfloat               angle)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_2D_TEMPLATE (self));

    priv = lrg_racing_2d_template_get_instance_private (self);
    priv->vehicle_angle = angle;
}

gfloat
lrg_racing_2d_template_get_speed (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_2D_TEMPLATE (self), 0.0f);

    priv = lrg_racing_2d_template_get_instance_private (self);
    return priv->speed;
}

gboolean
lrg_racing_2d_template_is_drifting (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_2D_TEMPLATE (self), FALSE);

    priv = lrg_racing_2d_template_get_instance_private (self);
    return priv->is_drifting;
}

gfloat
lrg_racing_2d_template_get_max_speed (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_2D_TEMPLATE (self), 400.0f);

    priv = lrg_racing_2d_template_get_instance_private (self);
    return priv->max_speed;
}

void
lrg_racing_2d_template_set_max_speed (LrgRacing2DTemplate *self,
                                      gfloat               speed)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_2D_TEMPLATE (self));

    priv = lrg_racing_2d_template_get_instance_private (self);
    priv->max_speed = MAX (speed, 1.0f);
}

gfloat
lrg_racing_2d_template_get_acceleration (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_2D_TEMPLATE (self), 300.0f);

    priv = lrg_racing_2d_template_get_instance_private (self);
    return priv->acceleration;
}

void
lrg_racing_2d_template_set_acceleration (LrgRacing2DTemplate *self,
                                         gfloat               accel)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_2D_TEMPLATE (self));

    priv = lrg_racing_2d_template_get_instance_private (self);
    priv->acceleration = MAX (accel, 1.0f);
}

gfloat
lrg_racing_2d_template_get_brake_power (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_2D_TEMPLATE (self), 500.0f);

    priv = lrg_racing_2d_template_get_instance_private (self);
    return priv->brake_power;
}

void
lrg_racing_2d_template_set_brake_power (LrgRacing2DTemplate *self,
                                        gfloat               power)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_2D_TEMPLATE (self));

    priv = lrg_racing_2d_template_get_instance_private (self);
    priv->brake_power = MAX (power, 1.0f);
}

gfloat
lrg_racing_2d_template_get_turn_speed (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_2D_TEMPLATE (self), 3.0f);

    priv = lrg_racing_2d_template_get_instance_private (self);
    return priv->turn_speed;
}

void
lrg_racing_2d_template_set_turn_speed (LrgRacing2DTemplate *self,
                                       gfloat               speed)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_2D_TEMPLATE (self));

    priv = lrg_racing_2d_template_get_instance_private (self);
    priv->turn_speed = CLAMP (speed, 0.1f, 10.0f);
}

gfloat
lrg_racing_2d_template_get_grip (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_2D_TEMPLATE (self), 0.8f);

    priv = lrg_racing_2d_template_get_instance_private (self);
    return priv->grip;
}

void
lrg_racing_2d_template_set_grip (LrgRacing2DTemplate *self,
                                 gfloat               grip)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_2D_TEMPLATE (self));

    priv = lrg_racing_2d_template_get_instance_private (self);
    priv->grip = CLAMP (grip, 0.0f, 1.0f);
}

gfloat
lrg_racing_2d_template_get_drift_threshold (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_2D_TEMPLATE (self), 200.0f);

    priv = lrg_racing_2d_template_get_instance_private (self);
    return priv->drift_threshold;
}

void
lrg_racing_2d_template_set_drift_threshold (LrgRacing2DTemplate *self,
                                            gfloat               threshold)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_2D_TEMPLATE (self));

    priv = lrg_racing_2d_template_get_instance_private (self);
    priv->drift_threshold = MAX (threshold, 0.0f);
}

guint
lrg_racing_2d_template_get_lap (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_2D_TEMPLATE (self), 1);

    priv = lrg_racing_2d_template_get_instance_private (self);
    return priv->current_lap;
}

guint
lrg_racing_2d_template_get_total_laps (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_2D_TEMPLATE (self), 3);

    priv = lrg_racing_2d_template_get_instance_private (self);
    return priv->total_laps;
}

void
lrg_racing_2d_template_set_total_laps (LrgRacing2DTemplate *self,
                                       guint                laps)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_2D_TEMPLATE (self));
    g_return_if_fail (laps >= 1);

    priv = lrg_racing_2d_template_get_instance_private (self);
    priv->total_laps = laps;
}

guint
lrg_racing_2d_template_get_checkpoint (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_2D_TEMPLATE (self), 0);

    priv = lrg_racing_2d_template_get_instance_private (self);
    return priv->current_checkpoint;
}

void
lrg_racing_2d_template_set_total_checkpoints (LrgRacing2DTemplate *self,
                                              guint                checkpoints)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_2D_TEMPLATE (self));

    priv = lrg_racing_2d_template_get_instance_private (self);
    priv->total_checkpoints = checkpoints;
}

void
lrg_racing_2d_template_pass_checkpoint (LrgRacing2DTemplate *self,
                                        guint                checkpoint)
{
    LrgRacing2DTemplateClass *klass;
    LrgRacing2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_2D_TEMPLATE (self));

    priv = lrg_racing_2d_template_get_instance_private (self);
    klass = LRG_RACING_2D_TEMPLATE_GET_CLASS (self);

    /* Validate checkpoint order (allow passing 0 after last checkpoint for lap) */
    if (checkpoint == (priv->current_checkpoint + 1) % (priv->total_checkpoints + 1))
    {
        priv->current_checkpoint = checkpoint;

        if (klass->on_checkpoint_passed != NULL)
            klass->on_checkpoint_passed (self, checkpoint);

        /* Check for lap completion (checkpoint 0 = finish line) */
        if (checkpoint == 0 && priv->current_lap > 0)
        {
            gfloat this_lap_time;

            this_lap_time = priv->lap_time;

            if (klass->on_lap_complete != NULL)
                klass->on_lap_complete (self, priv->current_lap, this_lap_time);

            /* Update best lap */
            if (priv->best_lap_time < 0.0f || this_lap_time < priv->best_lap_time)
                priv->best_lap_time = this_lap_time;

            priv->lap_time = 0.0f;
            priv->current_lap++;

            /* Check for race finish */
            if (priv->current_lap > priv->total_laps)
                lrg_racing_2d_template_finish_race (self);
        }
    }
}

gfloat
lrg_racing_2d_template_get_race_time (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_2D_TEMPLATE (self), 0.0f);

    priv = lrg_racing_2d_template_get_instance_private (self);
    return priv->race_time;
}

gfloat
lrg_racing_2d_template_get_lap_time (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_2D_TEMPLATE (self), 0.0f);

    priv = lrg_racing_2d_template_get_instance_private (self);
    return priv->lap_time;
}

gfloat
lrg_racing_2d_template_get_best_lap_time (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_2D_TEMPLATE (self), -1.0f);

    priv = lrg_racing_2d_template_get_instance_private (self);
    return priv->best_lap_time;
}

gfloat
lrg_racing_2d_template_get_boost (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_2D_TEMPLATE (self), 0.0f);

    priv = lrg_racing_2d_template_get_instance_private (self);
    return priv->boost;
}

void
lrg_racing_2d_template_set_boost (LrgRacing2DTemplate *self,
                                  gfloat               boost)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_2D_TEMPLATE (self));

    priv = lrg_racing_2d_template_get_instance_private (self);
    priv->boost = CLAMP (boost, 0.0f, 1.0f);
}

void
lrg_racing_2d_template_add_boost (LrgRacing2DTemplate *self,
                                  gfloat               amount)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_2D_TEMPLATE (self));

    priv = lrg_racing_2d_template_get_instance_private (self);
    priv->boost = CLAMP (priv->boost + amount, 0.0f, 1.0f);
}

gboolean
lrg_racing_2d_template_is_boosting (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_2D_TEMPLATE (self), FALSE);

    priv = lrg_racing_2d_template_get_instance_private (self);
    return priv->is_boosting;
}

gfloat
lrg_racing_2d_template_get_boost_multiplier (LrgRacing2DTemplate *self)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_RACING_2D_TEMPLATE (self), 1.5f);

    priv = lrg_racing_2d_template_get_instance_private (self);
    return priv->boost_multiplier;
}

void
lrg_racing_2d_template_set_boost_multiplier (LrgRacing2DTemplate *self,
                                             gfloat               multiplier)
{
    LrgRacing2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_RACING_2D_TEMPLATE (self));

    priv = lrg_racing_2d_template_get_instance_private (self);
    priv->boost_multiplier = CLAMP (multiplier, 1.0f, 3.0f);
}
