/* lrg-twin-stick-template.c - Twin-stick shooter template implementation
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "../config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TEMPLATE

#include "lrg-twin-stick-template.h"
#include "lrg-shooter-2d-template-private.h"
#include "../lrg-log.h"

#include <graylib.h>
#include <math.h>

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct
{
    /* Aim direction (normalized) */
    gfloat aim_x;
    gfloat aim_y;

    /* Movement direction (raw input) */
    gfloat move_x;
    gfloat move_y;

    /* Input settings */
    gfloat aim_deadzone;
    gfloat move_deadzone;
    gfloat fire_threshold;

    /* Aim mode */
    LrgTwinStickAimMode aim_mode;
    LrgTwinStickAimMode last_input_mode;

    /* Dash state */
    gfloat dash_speed;
    gfloat dash_duration;
    gfloat dash_cooldown;
    gfloat dash_timer;
    gfloat dash_cooldown_timer;
    gboolean is_dashing;
    gfloat dash_dir_x;
    gfloat dash_dir_y;
} LrgTwinStickTemplatePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgTwinStickTemplate, lrg_twin_stick_template,
                            LRG_TYPE_SHOOTER_2D_TEMPLATE)

/* ==========================================================================
 * Default Constants
 * ========================================================================== */

#define LRG_TWIN_STICK_DEFAULT_AIM_DEADZONE     0.2f
#define LRG_TWIN_STICK_DEFAULT_MOVE_DEADZONE    0.15f
#define LRG_TWIN_STICK_DEFAULT_FIRE_THRESHOLD   0.5f
#define LRG_TWIN_STICK_DEFAULT_DASH_SPEED       3.0f
#define LRG_TWIN_STICK_DEFAULT_DASH_DURATION    0.15f
#define LRG_TWIN_STICK_DEFAULT_DASH_COOLDOWN    1.0f

/* ==========================================================================
 * Property IDs
 * ========================================================================== */

enum
{
    PROP_0,
    PROP_AIM_X,
    PROP_AIM_Y,
    PROP_MOVE_X,
    PROP_MOVE_Y,
    PROP_AIM_DEADZONE,
    PROP_MOVE_DEADZONE,
    PROP_FIRE_THRESHOLD,
    PROP_AIM_MODE,
    PROP_DASH_SPEED,
    PROP_DASH_DURATION,
    PROP_DASH_COOLDOWN,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL };

/* ==========================================================================
 * Signal IDs
 * ========================================================================== */

enum
{
    SIGNAL_DASH_STARTED,
    SIGNAL_DASH_ENDED,
    N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

static gfloat
apply_deadzone (gfloat value,
                gfloat deadzone)
{
    gfloat sign;
    gfloat magnitude;

    if (fabsf (value) < deadzone)
        return 0.0f;

    sign = value < 0.0f ? -1.0f : 1.0f;
    magnitude = (fabsf (value) - deadzone) / (1.0f - deadzone);

    return sign * magnitude;
}

static void
normalize_direction (gfloat  x,
                     gfloat  y,
                     gfloat *out_x,
                     gfloat *out_y)
{
    gfloat length;

    length = sqrtf (x * x + y * y);

    if (length > 0.0001f)
    {
        *out_x = x / length;
        *out_y = y / length;
    }
    else
    {
        *out_x = 0.0f;
        *out_y = -1.0f; /* Default: aim up */
    }
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

static gboolean
lrg_twin_stick_template_fire_weapon (LrgShooter2DTemplate *shooter)
{
    LrgTwinStickTemplate *self;
    LrgTwinStickTemplatePrivate *priv;
    LrgShooter2DTemplateClass *shooter_class;
    gfloat player_x;
    gfloat player_y;
    gfloat aim_magnitude;

    self = LRG_TWIN_STICK_TEMPLATE (shooter);
    priv = lrg_twin_stick_template_get_instance_private (self);

    /* Check if aiming with enough magnitude */
    aim_magnitude = sqrtf (priv->aim_x * priv->aim_x + priv->aim_y * priv->aim_y);
    if (aim_magnitude < priv->fire_threshold)
        return FALSE;

    /* Check cooldown */
    if (lrg_shooter_2d_template_get_fire_cooldown (shooter) > 0.0f)
        return FALSE;

    /* Get player position */
    lrg_shooter_2d_template_get_player_position (shooter, &player_x, &player_y);

    /* Spawn projectile in aim direction */
    shooter_class = LRG_SHOOTER_2D_TEMPLATE_GET_CLASS (shooter);
    if (shooter_class->spawn_projectile != NULL)
    {
        return shooter_class->spawn_projectile (shooter,
                                                 player_x,
                                                 player_y,
                                                 priv->aim_x,
                                                 priv->aim_y,
                                                 lrg_shooter_2d_template_get_projectile_speed (shooter),
                                                 0);
    }

    return FALSE;
}

/* ==========================================================================
 * LrgGameTemplate Overrides
 * ========================================================================== */

static void
lrg_twin_stick_template_pre_update (LrgGameTemplate *template,
                                     gdouble          delta)
{
    LrgTwinStickTemplate *self;
    LrgTwinStickTemplatePrivate *priv;
    LrgTwinStickTemplateClass *klass;
    LrgGameTemplateClass *parent_class;
    gfloat delta_f;
    gfloat player_x;
    gfloat player_y;
    gfloat player_speed;
    gfloat move_speed;
    gfloat play_min_x;
    gfloat play_min_y;
    gfloat play_max_x;
    gfloat play_max_y;

    self = LRG_TWIN_STICK_TEMPLATE (template);
    priv = lrg_twin_stick_template_get_instance_private (self);
    klass = LRG_TWIN_STICK_TEMPLATE_GET_CLASS (self);
    delta_f = (gfloat) delta;

    /* Update dash state */
    if (priv->is_dashing)
    {
        priv->dash_timer -= delta_f;
        if (priv->dash_timer <= 0.0f)
        {
            priv->is_dashing = FALSE;
            priv->dash_timer = 0.0f;
            if (klass->on_dash_ended != NULL)
                klass->on_dash_ended (self);
            g_signal_emit (self, signals[SIGNAL_DASH_ENDED], 0);
        }
    }

    /* Update dash cooldown */
    if (priv->dash_cooldown_timer > 0.0f)
    {
        priv->dash_cooldown_timer -= delta_f;
        if (priv->dash_cooldown_timer < 0.0f)
            priv->dash_cooldown_timer = 0.0f;
    }

    /* Get current player position */
    lrg_shooter_2d_template_get_player_position (LRG_SHOOTER_2D_TEMPLATE (self),
                                                  &player_x, &player_y);

    /* Calculate movement speed */
    g_object_get (self, "player-speed", &player_speed, NULL);

    if (priv->is_dashing)
        move_speed = player_speed * priv->dash_speed;
    else
        move_speed = player_speed;

    /* Apply movement */
    if (priv->is_dashing)
    {
        /* Move in dash direction */
        player_x += priv->dash_dir_x * move_speed * delta_f;
        player_y += priv->dash_dir_y * move_speed * delta_f;
    }
    else
    {
        /* Normal movement */
        gfloat move_x_adj;
        gfloat move_y_adj;
        gfloat move_length;

        move_x_adj = apply_deadzone (priv->move_x, priv->move_deadzone);
        move_y_adj = apply_deadzone (priv->move_y, priv->move_deadzone);

        /* Normalize diagonal movement */
        move_length = sqrtf (move_x_adj * move_x_adj + move_y_adj * move_y_adj);
        if (move_length > 1.0f)
        {
            move_x_adj /= move_length;
            move_y_adj /= move_length;
        }

        player_x += move_x_adj * move_speed * delta_f;
        player_y += move_y_adj * move_speed * delta_f;
    }

    /* Constrain to play area */
    lrg_shooter_2d_template_get_play_area (LRG_SHOOTER_2D_TEMPLATE (self),
                                            &play_min_x, &play_min_y,
                                            &play_max_x, &play_max_y);

    if (player_x < play_min_x) player_x = play_min_x;
    if (player_x > play_max_x) player_x = play_max_x;
    if (player_y < play_min_y) player_y = play_min_y;
    if (player_y > play_max_y) player_y = play_max_y;

    /* Update position */
    lrg_shooter_2d_template_set_player_position (LRG_SHOOTER_2D_TEMPLATE (self),
                                                  player_x, player_y);

    /* Chain up */
    parent_class = LRG_GAME_TEMPLATE_CLASS (lrg_twin_stick_template_parent_class);
    if (parent_class->pre_update != NULL)
        parent_class->pre_update (template, delta);
}

/* ==========================================================================
 * LrgGame2DTemplate Overrides
 * ========================================================================== */

static void
lrg_twin_stick_template_draw_world (LrgGame2DTemplate *template)
{
    LrgTwinStickTemplate *self;
    LrgTwinStickTemplatePrivate *priv;
    LrgGame2DTemplateClass *parent_class;
    gfloat player_x;
    gfloat player_y;
    gfloat aim_end_x;
    gfloat aim_end_y;

    self = LRG_TWIN_STICK_TEMPLATE (template);
    priv = lrg_twin_stick_template_get_instance_private (self);

    /* Chain up to draw projectiles and player */
    parent_class = LRG_GAME_2D_TEMPLATE_CLASS (lrg_twin_stick_template_parent_class);
    if (parent_class->draw_world != NULL)
        parent_class->draw_world (template);

    /* Draw aim indicator */
    lrg_shooter_2d_template_get_player_position (LRG_SHOOTER_2D_TEMPLATE (self),
                                                  &player_x, &player_y);

    aim_end_x = player_x + priv->aim_x * 50.0f;
    aim_end_y = player_y + priv->aim_y * 50.0f;

    {
        g_autoptr(GrlColor) aim_color = grl_color_new (255, 0, 0, 180);
        grl_draw_line ((gint) player_x, (gint) player_y,
                       (gint) aim_end_x, (gint) aim_end_y, aim_color);
    }

    /* Draw dash cooldown indicator */
    if (priv->dash_cooldown_timer > 0.0f)
    {
        gfloat cooldown_ratio;
        g_autoptr(GrlColor) cooldown_color = grl_color_new (100, 100, 100, 150);

        cooldown_ratio = priv->dash_cooldown_timer / priv->dash_cooldown;

        grl_draw_rectangle ((gint) (player_x - 20),
                            (gint) (player_y + 25),
                            (gint) (40 * cooldown_ratio), 4, cooldown_color);
    }
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_twin_stick_template_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
    LrgTwinStickTemplate *self;
    LrgTwinStickTemplatePrivate *priv;

    self = LRG_TWIN_STICK_TEMPLATE (object);
    priv = lrg_twin_stick_template_get_instance_private (self);

    switch (prop_id)
    {
        case PROP_AIM_X:
            priv->aim_x = g_value_get_float (value);
            normalize_direction (priv->aim_x, priv->aim_y,
                                 &priv->aim_x, &priv->aim_y);
            break;

        case PROP_AIM_Y:
            priv->aim_y = g_value_get_float (value);
            normalize_direction (priv->aim_x, priv->aim_y,
                                 &priv->aim_x, &priv->aim_y);
            break;

        case PROP_MOVE_X:
            priv->move_x = CLAMP (g_value_get_float (value), -1.0f, 1.0f);
            break;

        case PROP_MOVE_Y:
            priv->move_y = CLAMP (g_value_get_float (value), -1.0f, 1.0f);
            break;

        case PROP_AIM_DEADZONE:
            lrg_twin_stick_template_set_aim_deadzone (self, g_value_get_float (value));
            break;

        case PROP_MOVE_DEADZONE:
            lrg_twin_stick_template_set_move_deadzone (self, g_value_get_float (value));
            break;

        case PROP_FIRE_THRESHOLD:
            lrg_twin_stick_template_set_fire_threshold (self, g_value_get_float (value));
            break;

        case PROP_AIM_MODE:
            lrg_twin_stick_template_set_aim_mode (self, g_value_get_int (value));
            break;

        case PROP_DASH_SPEED:
            lrg_twin_stick_template_set_dash_speed (self, g_value_get_float (value));
            break;

        case PROP_DASH_DURATION:
            lrg_twin_stick_template_set_dash_duration (self, g_value_get_float (value));
            break;

        case PROP_DASH_COOLDOWN:
            lrg_twin_stick_template_set_dash_cooldown (self, g_value_get_float (value));
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
lrg_twin_stick_template_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
    LrgTwinStickTemplate *self;
    LrgTwinStickTemplatePrivate *priv;

    self = LRG_TWIN_STICK_TEMPLATE (object);
    priv = lrg_twin_stick_template_get_instance_private (self);

    switch (prop_id)
    {
        case PROP_AIM_X:
            g_value_set_float (value, priv->aim_x);
            break;

        case PROP_AIM_Y:
            g_value_set_float (value, priv->aim_y);
            break;

        case PROP_MOVE_X:
            g_value_set_float (value, priv->move_x);
            break;

        case PROP_MOVE_Y:
            g_value_set_float (value, priv->move_y);
            break;

        case PROP_AIM_DEADZONE:
            g_value_set_float (value, priv->aim_deadzone);
            break;

        case PROP_MOVE_DEADZONE:
            g_value_set_float (value, priv->move_deadzone);
            break;

        case PROP_FIRE_THRESHOLD:
            g_value_set_float (value, priv->fire_threshold);
            break;

        case PROP_AIM_MODE:
            g_value_set_int (value, priv->aim_mode);
            break;

        case PROP_DASH_SPEED:
            g_value_set_float (value, priv->dash_speed);
            break;

        case PROP_DASH_DURATION:
            g_value_set_float (value, priv->dash_duration);
            break;

        case PROP_DASH_COOLDOWN:
            g_value_set_float (value, priv->dash_cooldown);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
lrg_twin_stick_template_init (LrgTwinStickTemplate *self)
{
    LrgTwinStickTemplatePrivate *priv;

    priv = lrg_twin_stick_template_get_instance_private (self);

    /* Aim direction (default: up) */
    priv->aim_x = 0.0f;
    priv->aim_y = -1.0f;

    /* Movement direction */
    priv->move_x = 0.0f;
    priv->move_y = 0.0f;

    /* Input settings */
    priv->aim_deadzone = LRG_TWIN_STICK_DEFAULT_AIM_DEADZONE;
    priv->move_deadzone = LRG_TWIN_STICK_DEFAULT_MOVE_DEADZONE;
    priv->fire_threshold = LRG_TWIN_STICK_DEFAULT_FIRE_THRESHOLD;

    /* Aim mode */
    priv->aim_mode = LRG_TWIN_STICK_AIM_HYBRID;
    priv->last_input_mode = LRG_TWIN_STICK_AIM_STICK;

    /* Dash */
    priv->dash_speed = LRG_TWIN_STICK_DEFAULT_DASH_SPEED;
    priv->dash_duration = LRG_TWIN_STICK_DEFAULT_DASH_DURATION;
    priv->dash_cooldown = LRG_TWIN_STICK_DEFAULT_DASH_COOLDOWN;
    priv->dash_timer = 0.0f;
    priv->dash_cooldown_timer = 0.0f;
    priv->is_dashing = FALSE;
    priv->dash_dir_x = 0.0f;
    priv->dash_dir_y = 0.0f;

    /* Enable auto-fire for twin-stick */
    g_object_set (self, "auto-fire", TRUE, NULL);
}

static void
lrg_twin_stick_template_class_init (LrgTwinStickTemplateClass *klass)
{
    GObjectClass *object_class;
    LrgGameTemplateClass *template_class;
    LrgGame2DTemplateClass *template_2d_class;
    LrgShooter2DTemplateClass *shooter_class;

    object_class = G_OBJECT_CLASS (klass);
    template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    template_2d_class = LRG_GAME_2D_TEMPLATE_CLASS (klass);
    shooter_class = LRG_SHOOTER_2D_TEMPLATE_CLASS (klass);

    /* GObject overrides */
    object_class->set_property = lrg_twin_stick_template_set_property;
    object_class->get_property = lrg_twin_stick_template_get_property;

    /* LrgGameTemplate overrides */
    template_class->pre_update = lrg_twin_stick_template_pre_update;

    /* LrgGame2DTemplate overrides */
    template_2d_class->draw_world = lrg_twin_stick_template_draw_world;

    /* LrgShooter2DTemplate overrides */
    shooter_class->fire_weapon = lrg_twin_stick_template_fire_weapon;

    /* Properties */
    properties[PROP_AIM_X] =
        g_param_spec_float ("aim-x", "Aim X",
                            "Aim direction X component",
                            -1.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_AIM_Y] =
        g_param_spec_float ("aim-y", "Aim Y",
                            "Aim direction Y component",
                            -1.0f, 1.0f, -1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MOVE_X] =
        g_param_spec_float ("move-x", "Move X",
                            "Movement direction X component",
                            -1.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MOVE_Y] =
        g_param_spec_float ("move-y", "Move Y",
                            "Movement direction Y component",
                            -1.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_AIM_DEADZONE] =
        g_param_spec_float ("aim-deadzone", "Aim Deadzone",
                            "Gamepad stick deadzone for aiming",
                            0.0f, 1.0f,
                            LRG_TWIN_STICK_DEFAULT_AIM_DEADZONE,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MOVE_DEADZONE] =
        g_param_spec_float ("move-deadzone", "Move Deadzone",
                            "Gamepad stick deadzone for movement",
                            0.0f, 1.0f,
                            LRG_TWIN_STICK_DEFAULT_MOVE_DEADZONE,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_FIRE_THRESHOLD] =
        g_param_spec_float ("fire-threshold", "Fire Threshold",
                            "Minimum aim magnitude to trigger firing",
                            0.0f, 1.0f,
                            LRG_TWIN_STICK_DEFAULT_FIRE_THRESHOLD,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_AIM_MODE] =
        g_param_spec_int ("aim-mode", "Aim Mode",
                          "Aiming input mode",
                          LRG_TWIN_STICK_AIM_STICK,
                          LRG_TWIN_STICK_AIM_HYBRID,
                          LRG_TWIN_STICK_AIM_HYBRID,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DASH_SPEED] =
        g_param_spec_float ("dash-speed", "Dash Speed",
                            "Dash speed multiplier",
                            1.0f, G_MAXFLOAT,
                            LRG_TWIN_STICK_DEFAULT_DASH_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DASH_DURATION] =
        g_param_spec_float ("dash-duration", "Dash Duration",
                            "Dash duration in seconds",
                            0.01f, G_MAXFLOAT,
                            LRG_TWIN_STICK_DEFAULT_DASH_DURATION,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DASH_COOLDOWN] =
        g_param_spec_float ("dash-cooldown", "Dash Cooldown",
                            "Dash cooldown in seconds",
                            0.0f, G_MAXFLOAT,
                            LRG_TWIN_STICK_DEFAULT_DASH_COOLDOWN,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPERTIES, properties);

    /* Signals */
    signals[SIGNAL_DASH_STARTED] =
        g_signal_new ("dash-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    signals[SIGNAL_DASH_ENDED] =
        g_signal_new ("dash-ended",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgTwinStickTemplate *
lrg_twin_stick_template_new (void)
{
    return g_object_new (LRG_TYPE_TWIN_STICK_TEMPLATE, NULL);
}

void
lrg_twin_stick_template_get_aim_direction (LrgTwinStickTemplate *self,
                                            gfloat               *x,
                                            gfloat               *y)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self));

    priv = lrg_twin_stick_template_get_instance_private (self);

    if (x != NULL)
        *x = priv->aim_x;
    if (y != NULL)
        *y = priv->aim_y;
}

void
lrg_twin_stick_template_set_aim_direction (LrgTwinStickTemplate *self,
                                            gfloat                x,
                                            gfloat                y)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self));

    priv = lrg_twin_stick_template_get_instance_private (self);

    normalize_direction (x, y, &priv->aim_x, &priv->aim_y);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AIM_X]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AIM_Y]);
}

gfloat
lrg_twin_stick_template_get_aim_angle (LrgTwinStickTemplate *self)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self), 0.0f);

    priv = lrg_twin_stick_template_get_instance_private (self);

    return atan2f (priv->aim_y, priv->aim_x);
}

void
lrg_twin_stick_template_set_aim_angle (LrgTwinStickTemplate *self,
                                        gfloat                angle)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self));

    priv = lrg_twin_stick_template_get_instance_private (self);

    priv->aim_x = cosf (angle);
    priv->aim_y = sinf (angle);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AIM_X]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AIM_Y]);
}

void
lrg_twin_stick_template_get_move_direction (LrgTwinStickTemplate *self,
                                             gfloat               *x,
                                             gfloat               *y)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self));

    priv = lrg_twin_stick_template_get_instance_private (self);

    if (x != NULL)
        *x = priv->move_x;
    if (y != NULL)
        *y = priv->move_y;
}

void
lrg_twin_stick_template_set_move_direction (LrgTwinStickTemplate *self,
                                             gfloat                x,
                                             gfloat                y)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self));

    priv = lrg_twin_stick_template_get_instance_private (self);

    priv->move_x = CLAMP (x, -1.0f, 1.0f);
    priv->move_y = CLAMP (y, -1.0f, 1.0f);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MOVE_X]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MOVE_Y]);
}

gfloat
lrg_twin_stick_template_get_aim_deadzone (LrgTwinStickTemplate *self)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self),
                          LRG_TWIN_STICK_DEFAULT_AIM_DEADZONE);

    priv = lrg_twin_stick_template_get_instance_private (self);

    return priv->aim_deadzone;
}

void
lrg_twin_stick_template_set_aim_deadzone (LrgTwinStickTemplate *self,
                                           gfloat                deadzone)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self));

    priv = lrg_twin_stick_template_get_instance_private (self);

    priv->aim_deadzone = CLAMP (deadzone, 0.0f, 1.0f);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AIM_DEADZONE]);
}

gfloat
lrg_twin_stick_template_get_move_deadzone (LrgTwinStickTemplate *self)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self),
                          LRG_TWIN_STICK_DEFAULT_MOVE_DEADZONE);

    priv = lrg_twin_stick_template_get_instance_private (self);

    return priv->move_deadzone;
}

void
lrg_twin_stick_template_set_move_deadzone (LrgTwinStickTemplate *self,
                                            gfloat                deadzone)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self));

    priv = lrg_twin_stick_template_get_instance_private (self);

    priv->move_deadzone = CLAMP (deadzone, 0.0f, 1.0f);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MOVE_DEADZONE]);
}

gfloat
lrg_twin_stick_template_get_fire_threshold (LrgTwinStickTemplate *self)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self),
                          LRG_TWIN_STICK_DEFAULT_FIRE_THRESHOLD);

    priv = lrg_twin_stick_template_get_instance_private (self);

    return priv->fire_threshold;
}

void
lrg_twin_stick_template_set_fire_threshold (LrgTwinStickTemplate *self,
                                             gfloat                threshold)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self));

    priv = lrg_twin_stick_template_get_instance_private (self);

    priv->fire_threshold = CLAMP (threshold, 0.0f, 1.0f);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FIRE_THRESHOLD]);
}

LrgTwinStickAimMode
lrg_twin_stick_template_get_aim_mode (LrgTwinStickTemplate *self)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self),
                          LRG_TWIN_STICK_AIM_HYBRID);

    priv = lrg_twin_stick_template_get_instance_private (self);

    return priv->aim_mode;
}

void
lrg_twin_stick_template_set_aim_mode (LrgTwinStickTemplate *self,
                                       LrgTwinStickAimMode   mode)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self));

    priv = lrg_twin_stick_template_get_instance_private (self);

    priv->aim_mode = mode;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AIM_MODE]);
}

gfloat
lrg_twin_stick_template_get_dash_speed (LrgTwinStickTemplate *self)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self),
                          LRG_TWIN_STICK_DEFAULT_DASH_SPEED);

    priv = lrg_twin_stick_template_get_instance_private (self);

    return priv->dash_speed;
}

void
lrg_twin_stick_template_set_dash_speed (LrgTwinStickTemplate *self,
                                         gfloat                speed)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self));

    priv = lrg_twin_stick_template_get_instance_private (self);

    if (speed < 1.0f)
        speed = 1.0f;

    priv->dash_speed = speed;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DASH_SPEED]);
}

gfloat
lrg_twin_stick_template_get_dash_duration (LrgTwinStickTemplate *self)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self),
                          LRG_TWIN_STICK_DEFAULT_DASH_DURATION);

    priv = lrg_twin_stick_template_get_instance_private (self);

    return priv->dash_duration;
}

void
lrg_twin_stick_template_set_dash_duration (LrgTwinStickTemplate *self,
                                            gfloat                duration)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self));

    priv = lrg_twin_stick_template_get_instance_private (self);

    if (duration < 0.01f)
        duration = 0.01f;

    priv->dash_duration = duration;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DASH_DURATION]);
}

gfloat
lrg_twin_stick_template_get_dash_cooldown (LrgTwinStickTemplate *self)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self),
                          LRG_TWIN_STICK_DEFAULT_DASH_COOLDOWN);

    priv = lrg_twin_stick_template_get_instance_private (self);

    return priv->dash_cooldown;
}

void
lrg_twin_stick_template_set_dash_cooldown (LrgTwinStickTemplate *self,
                                            gfloat                cooldown)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self));

    priv = lrg_twin_stick_template_get_instance_private (self);

    if (cooldown < 0.0f)
        cooldown = 0.0f;

    priv->dash_cooldown = cooldown;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DASH_COOLDOWN]);
}

gboolean
lrg_twin_stick_template_can_dash (LrgTwinStickTemplate *self)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self), FALSE);

    priv = lrg_twin_stick_template_get_instance_private (self);

    return !priv->is_dashing && priv->dash_cooldown_timer <= 0.0f;
}

gboolean
lrg_twin_stick_template_is_dashing (LrgTwinStickTemplate *self)
{
    LrgTwinStickTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self), FALSE);

    priv = lrg_twin_stick_template_get_instance_private (self);

    return priv->is_dashing;
}

gboolean
lrg_twin_stick_template_dash (LrgTwinStickTemplate *self)
{
    LrgTwinStickTemplatePrivate *priv;
    LrgTwinStickTemplateClass *klass;
    gfloat move_length;

    g_return_val_if_fail (LRG_IS_TWIN_STICK_TEMPLATE (self), FALSE);

    priv = lrg_twin_stick_template_get_instance_private (self);

    if (!lrg_twin_stick_template_can_dash (self))
        return FALSE;

    /* Dash in movement direction, or aim direction if not moving */
    move_length = sqrtf (priv->move_x * priv->move_x +
                         priv->move_y * priv->move_y);

    if (move_length > priv->move_deadzone)
    {
        priv->dash_dir_x = priv->move_x / move_length;
        priv->dash_dir_y = priv->move_y / move_length;
    }
    else
    {
        /* Use aim direction */
        priv->dash_dir_x = priv->aim_x;
        priv->dash_dir_y = priv->aim_y;
    }

    priv->is_dashing = TRUE;
    priv->dash_timer = priv->dash_duration;
    priv->dash_cooldown_timer = priv->dash_cooldown;

    /* Call virtual method */
    klass = LRG_TWIN_STICK_TEMPLATE_GET_CLASS (self);
    if (klass->on_dash_started != NULL)
        klass->on_dash_started (self, priv->dash_dir_x, priv->dash_dir_y);

    g_signal_emit (self, signals[SIGNAL_DASH_STARTED], 0);

    return TRUE;
}
