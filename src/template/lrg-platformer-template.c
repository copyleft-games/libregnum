/* lrg-platformer-template.c - Platformer game template implementation
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "../config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TEMPLATE

#include "lrg-platformer-template.h"
#include "lrg-game-2d-template-private.h"
#include "../lrg-log.h"

#include <graylib.h>
#include <math.h>

/* ==========================================================================
 * Default Constants
 * ========================================================================== */

#define LRG_PLATFORMER_DEFAULT_MOVE_SPEED      200.0f
#define LRG_PLATFORMER_DEFAULT_ACCELERATION    1500.0f
#define LRG_PLATFORMER_DEFAULT_FRICTION        1200.0f
#define LRG_PLATFORMER_DEFAULT_AIR_FRICTION    400.0f
#define LRG_PLATFORMER_DEFAULT_GRAVITY         980.0f
#define LRG_PLATFORMER_DEFAULT_JUMP_HEIGHT     120.0f
#define LRG_PLATFORMER_DEFAULT_FALL_MULTIPLIER 2.5f
#define LRG_PLATFORMER_DEFAULT_MAX_FALL_SPEED  600.0f
#define LRG_PLATFORMER_DEFAULT_COYOTE_TIME     0.1f
#define LRG_PLATFORMER_DEFAULT_JUMP_BUFFER     0.1f
#define LRG_PLATFORMER_DEFAULT_WALL_SLIDE_SPEED 60.0f
#define LRG_PLATFORMER_DEFAULT_WALL_JUMP_X     250.0f
#define LRG_PLATFORMER_DEFAULT_WALL_JUMP_Y     350.0f

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct _LrgPlatformerTemplatePrivate
{
    /* Position & Velocity */
    gfloat player_x;
    gfloat player_y;
    gfloat velocity_x;
    gfloat velocity_y;

    /* Movement */
    gfloat move_speed;
    gfloat acceleration;
    gfloat friction;
    gfloat air_friction;
    gfloat move_input;

    /* Gravity & Jump */
    gfloat gravity;
    gfloat jump_height;
    gfloat jump_velocity;
    gfloat fall_multiplier;
    gfloat max_fall_speed;

    /* Coyote & Buffer */
    gfloat coyote_time;
    gfloat coyote_timer;
    gfloat jump_buffer_time;
    gfloat jump_buffer_timer;

    /* Wall mechanics */
    gboolean wall_slide_enabled;
    gfloat wall_slide_speed;
    gboolean wall_jump_enabled;
    gfloat wall_jump_x;
    gfloat wall_jump_y;

    /* State */
    gboolean is_grounded;
    gboolean was_grounded;
    gboolean is_jumping;
    gboolean is_wall_sliding;
    gboolean jump_held;
    gint facing_direction;
    gint wall_direction;

    /* Collision bounds (simple rectangle) */
    gfloat hitbox_width;
    gfloat hitbox_height;
    gfloat ground_y;

} LrgPlatformerTemplatePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgPlatformerTemplate, lrg_platformer_template,
                            LRG_TYPE_GAME_2D_TEMPLATE)

/* ==========================================================================
 * Property IDs
 * ========================================================================== */

enum
{
    PROP_0,
    PROP_PLAYER_X,
    PROP_PLAYER_Y,
    PROP_MOVE_SPEED,
    PROP_ACCELERATION,
    PROP_FRICTION,
    PROP_AIR_FRICTION,
    PROP_GRAVITY,
    PROP_JUMP_HEIGHT,
    PROP_FALL_MULTIPLIER,
    PROP_MAX_FALL_SPEED,
    PROP_COYOTE_TIME,
    PROP_JUMP_BUFFER_TIME,
    PROP_WALL_SLIDE_ENABLED,
    PROP_WALL_SLIDE_SPEED,
    PROP_WALL_JUMP_ENABLED,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL };

/* ==========================================================================
 * Signal IDs
 * ========================================================================== */

enum
{
    SIGNAL_LANDED,
    SIGNAL_JUMPED,
    SIGNAL_WALL_SLIDE_STARTED,
    SIGNAL_WALL_JUMPED,
    N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

static gfloat
calculate_jump_velocity (gfloat gravity,
                         gfloat jump_height)
{
    return -sqrtf (2.0f * gravity * jump_height);
}

static gfloat
approach (gfloat current,
          gfloat target,
          gfloat amount)
{
    if (current < target)
        return fminf (current + amount, target);
    else
        return fmaxf (current - amount, target);
}

/* ==========================================================================
 * Default Virtual Implementations
 * ========================================================================== */

static void
lrg_platformer_template_real_on_landed (LrgPlatformerTemplate *self)
{
    g_signal_emit (self, signals[SIGNAL_LANDED], 0);
}

static void
lrg_platformer_template_real_on_jump (LrgPlatformerTemplate *self)
{
    g_signal_emit (self, signals[SIGNAL_JUMPED], 0);
}

static void
lrg_platformer_template_real_on_wall_slide (LrgPlatformerTemplate *self)
{
    g_signal_emit (self, signals[SIGNAL_WALL_SLIDE_STARTED], 0);
}

static void
lrg_platformer_template_real_on_wall_jump (LrgPlatformerTemplate *self,
                                            gint                   direction)
{
    g_signal_emit (self, signals[SIGNAL_WALL_JUMPED], 0, direction);
}

static gboolean
lrg_platformer_template_real_check_ground (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;

    priv = lrg_platformer_template_get_instance_private (self);

    /* Simple ground check: are we at or below ground level? */
    return priv->player_y >= priv->ground_y;
}

static gboolean
lrg_platformer_template_real_check_wall (LrgPlatformerTemplate *self,
                                          gint                   direction)
{
    /* Default: no walls. Override in subclass */
    return FALSE;
}

static void
lrg_platformer_template_real_update_physics (LrgPlatformerTemplate *self,
                                              gdouble                delta)
{
    LrgPlatformerTemplatePrivate *priv;
    LrgPlatformerTemplateClass *klass;
    gfloat delta_f;
    gfloat target_velocity;
    gfloat accel;
    gfloat gravity;

    priv = lrg_platformer_template_get_instance_private (self);
    klass = LRG_PLATFORMER_TEMPLATE_GET_CLASS (self);
    delta_f = (gfloat) delta;

    /* Store previous grounded state */
    priv->was_grounded = priv->is_grounded;

    /* Check ground */
    if (klass->check_ground != NULL)
        priv->is_grounded = klass->check_ground (self);
    else
        priv->is_grounded = lrg_platformer_template_real_check_ground (self);

    /* Update coyote timer */
    if (priv->is_grounded)
    {
        priv->coyote_timer = priv->coyote_time;
        priv->is_jumping = FALSE;
    }
    else
    {
        if (priv->coyote_timer > 0.0f)
            priv->coyote_timer -= delta_f;
    }

    /* Update jump buffer */
    if (priv->jump_buffer_timer > 0.0f)
    {
        priv->jump_buffer_timer -= delta_f;

        /* Try to execute buffered jump */
        if (priv->is_grounded || priv->coyote_timer > 0.0f)
        {
            priv->velocity_y = priv->jump_velocity;
            priv->is_jumping = TRUE;
            priv->coyote_timer = 0.0f;
            priv->jump_buffer_timer = 0.0f;

            if (klass->on_jump != NULL)
                klass->on_jump (self);
        }
    }

    /* Horizontal movement */
    target_velocity = priv->move_input * priv->move_speed;

    if (priv->is_grounded)
        accel = (fabsf (priv->move_input) > 0.01f) ? priv->acceleration : priv->friction;
    else
        accel = priv->air_friction;

    priv->velocity_x = approach (priv->velocity_x, target_velocity, accel * delta_f);

    /* Update facing direction */
    if (fabsf (priv->move_input) > 0.1f)
        priv->facing_direction = (priv->move_input > 0) ? 1 : -1;

    /* Gravity */
    gravity = priv->gravity;

    /* Apply fall multiplier when falling or when jump released */
    if (priv->velocity_y > 0.0f || (!priv->jump_held && priv->velocity_y < 0.0f))
        gravity *= priv->fall_multiplier;

    priv->velocity_y += gravity * delta_f;

    /* Clamp fall speed */
    if (priv->velocity_y > priv->max_fall_speed)
        priv->velocity_y = priv->max_fall_speed;

    /* Wall slide */
    priv->is_wall_sliding = FALSE;
    if (priv->wall_slide_enabled && !priv->is_grounded && priv->velocity_y > 0.0f)
    {
        gint check_dir;
        gboolean touching_wall;

        check_dir = (priv->move_input > 0.1f) ? 1 : (priv->move_input < -0.1f) ? -1 : 0;
        touching_wall = FALSE;

        if (check_dir != 0 && klass->check_wall != NULL)
            touching_wall = klass->check_wall (self, check_dir);

        if (touching_wall)
        {
            priv->is_wall_sliding = TRUE;
            priv->wall_direction = check_dir;

            if (priv->velocity_y > priv->wall_slide_speed)
                priv->velocity_y = priv->wall_slide_speed;

            if (!priv->was_grounded && klass->on_wall_slide != NULL)
                klass->on_wall_slide (self);
        }
    }

    /* Apply velocity */
    priv->player_x += priv->velocity_x * delta_f;
    priv->player_y += priv->velocity_y * delta_f;

    /* Ground collision */
    if (priv->player_y >= priv->ground_y)
    {
        priv->player_y = priv->ground_y;
        priv->velocity_y = 0.0f;

        if (!priv->was_grounded && klass->on_landed != NULL)
            klass->on_landed (self);
    }
}

/* ==========================================================================
 * LrgGameTemplate Overrides
 * ========================================================================== */

static void
lrg_platformer_template_pre_update (LrgGameTemplate *template,
                                     gdouble          delta)
{
    LrgPlatformerTemplate *self;
    LrgPlatformerTemplateClass *klass;
    LrgGameTemplateClass *parent_class;

    self = LRG_PLATFORMER_TEMPLATE (template);
    klass = LRG_PLATFORMER_TEMPLATE_GET_CLASS (self);

    /* Update physics */
    if (klass->update_physics != NULL)
        klass->update_physics (self, delta);

    /* Chain up */
    parent_class = LRG_GAME_TEMPLATE_CLASS (lrg_platformer_template_parent_class);
    if (parent_class->pre_update != NULL)
        parent_class->pre_update (template, delta);
}

/* ==========================================================================
 * LrgGame2DTemplate Overrides
 * ========================================================================== */

static void
lrg_platformer_template_draw_world (LrgGame2DTemplate *template)
{
    LrgPlatformerTemplate *self;
    LrgPlatformerTemplatePrivate *priv;
    LrgGame2DTemplateClass *parent_class;
    gint draw_x;
    gint draw_y;

    self = LRG_PLATFORMER_TEMPLATE (template);
    priv = lrg_platformer_template_get_instance_private (self);

    /* Chain up first */
    parent_class = LRG_GAME_2D_TEMPLATE_CLASS (lrg_platformer_template_parent_class);
    if (parent_class->draw_world != NULL)
        parent_class->draw_world (template);

    /* Draw player (simple rectangle) */
    draw_x = (gint) (priv->player_x - priv->hitbox_width / 2.0f);
    draw_y = (gint) (priv->player_y - priv->hitbox_height);

    {
        g_autoptr(GrlColor) player_color = NULL;

        if (priv->is_wall_sliding)
            player_color = grl_color_new (100, 100, 255, 255);
        else if (priv->is_grounded)
            player_color = grl_color_new (0, 255, 0, 255);
        else if (priv->velocity_y < 0)
            player_color = grl_color_new (255, 255, 0, 255);
        else
            player_color = grl_color_new (255, 100, 0, 255);

        grl_draw_rectangle (draw_x, draw_y,
                            (gint) priv->hitbox_width,
                            (gint) priv->hitbox_height,
                            player_color);
    }

    /* Draw ground line */
    {
        g_autoptr(GrlColor) ground_color = grl_color_new (100, 100, 100, 255);
        gint virtual_width;

        virtual_width = lrg_game_2d_template_get_virtual_width (template);
        grl_draw_line (0, (gint) priv->ground_y,
                       virtual_width, (gint) priv->ground_y,
                       ground_color);
    }
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_platformer_template_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
    LrgPlatformerTemplate *self;
    LrgPlatformerTemplatePrivate *priv;

    self = LRG_PLATFORMER_TEMPLATE (object);
    priv = lrg_platformer_template_get_instance_private (self);

    switch (prop_id)
    {
        case PROP_PLAYER_X:
            priv->player_x = g_value_get_float (value);
            break;
        case PROP_PLAYER_Y:
            priv->player_y = g_value_get_float (value);
            break;
        case PROP_MOVE_SPEED:
            lrg_platformer_template_set_move_speed (self, g_value_get_float (value));
            break;
        case PROP_ACCELERATION:
            lrg_platformer_template_set_acceleration (self, g_value_get_float (value));
            break;
        case PROP_FRICTION:
            lrg_platformer_template_set_friction (self, g_value_get_float (value));
            break;
        case PROP_AIR_FRICTION:
            lrg_platformer_template_set_air_friction (self, g_value_get_float (value));
            break;
        case PROP_GRAVITY:
            lrg_platformer_template_set_gravity (self, g_value_get_float (value));
            break;
        case PROP_JUMP_HEIGHT:
            lrg_platformer_template_set_jump_height (self, g_value_get_float (value));
            break;
        case PROP_FALL_MULTIPLIER:
            lrg_platformer_template_set_fall_multiplier (self, g_value_get_float (value));
            break;
        case PROP_MAX_FALL_SPEED:
            lrg_platformer_template_set_max_fall_speed (self, g_value_get_float (value));
            break;
        case PROP_COYOTE_TIME:
            lrg_platformer_template_set_coyote_time (self, g_value_get_float (value));
            break;
        case PROP_JUMP_BUFFER_TIME:
            lrg_platformer_template_set_jump_buffer_time (self, g_value_get_float (value));
            break;
        case PROP_WALL_SLIDE_ENABLED:
            lrg_platformer_template_set_wall_slide_enabled (self, g_value_get_boolean (value));
            break;
        case PROP_WALL_SLIDE_SPEED:
            lrg_platformer_template_set_wall_slide_speed (self, g_value_get_float (value));
            break;
        case PROP_WALL_JUMP_ENABLED:
            lrg_platformer_template_set_wall_jump_enabled (self, g_value_get_boolean (value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
lrg_platformer_template_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
    LrgPlatformerTemplate *self;
    LrgPlatformerTemplatePrivate *priv;

    self = LRG_PLATFORMER_TEMPLATE (object);
    priv = lrg_platformer_template_get_instance_private (self);

    switch (prop_id)
    {
        case PROP_PLAYER_X:
            g_value_set_float (value, priv->player_x);
            break;
        case PROP_PLAYER_Y:
            g_value_set_float (value, priv->player_y);
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
        case PROP_AIR_FRICTION:
            g_value_set_float (value, priv->air_friction);
            break;
        case PROP_GRAVITY:
            g_value_set_float (value, priv->gravity);
            break;
        case PROP_JUMP_HEIGHT:
            g_value_set_float (value, priv->jump_height);
            break;
        case PROP_FALL_MULTIPLIER:
            g_value_set_float (value, priv->fall_multiplier);
            break;
        case PROP_MAX_FALL_SPEED:
            g_value_set_float (value, priv->max_fall_speed);
            break;
        case PROP_COYOTE_TIME:
            g_value_set_float (value, priv->coyote_time);
            break;
        case PROP_JUMP_BUFFER_TIME:
            g_value_set_float (value, priv->jump_buffer_time);
            break;
        case PROP_WALL_SLIDE_ENABLED:
            g_value_set_boolean (value, priv->wall_slide_enabled);
            break;
        case PROP_WALL_SLIDE_SPEED:
            g_value_set_float (value, priv->wall_slide_speed);
            break;
        case PROP_WALL_JUMP_ENABLED:
            g_value_set_boolean (value, priv->wall_jump_enabled);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
lrg_platformer_template_init (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;
    gint virtual_width;
    gint virtual_height;

    priv = lrg_platformer_template_get_instance_private (self);

    virtual_width = lrg_game_2d_template_get_virtual_width (LRG_GAME_2D_TEMPLATE (self));
    virtual_height = lrg_game_2d_template_get_virtual_height (LRG_GAME_2D_TEMPLATE (self));

    /* Position */
    priv->player_x = virtual_width / 2.0f;
    priv->player_y = virtual_height - 100.0f;
    priv->velocity_x = 0.0f;
    priv->velocity_y = 0.0f;

    /* Movement */
    priv->move_speed = LRG_PLATFORMER_DEFAULT_MOVE_SPEED;
    priv->acceleration = LRG_PLATFORMER_DEFAULT_ACCELERATION;
    priv->friction = LRG_PLATFORMER_DEFAULT_FRICTION;
    priv->air_friction = LRG_PLATFORMER_DEFAULT_AIR_FRICTION;
    priv->move_input = 0.0f;

    /* Gravity & Jump */
    priv->gravity = LRG_PLATFORMER_DEFAULT_GRAVITY;
    priv->jump_height = LRG_PLATFORMER_DEFAULT_JUMP_HEIGHT;
    priv->jump_velocity = calculate_jump_velocity (priv->gravity, priv->jump_height);
    priv->fall_multiplier = LRG_PLATFORMER_DEFAULT_FALL_MULTIPLIER;
    priv->max_fall_speed = LRG_PLATFORMER_DEFAULT_MAX_FALL_SPEED;

    /* Coyote & Buffer */
    priv->coyote_time = LRG_PLATFORMER_DEFAULT_COYOTE_TIME;
    priv->coyote_timer = 0.0f;
    priv->jump_buffer_time = LRG_PLATFORMER_DEFAULT_JUMP_BUFFER;
    priv->jump_buffer_timer = 0.0f;

    /* Wall */
    priv->wall_slide_enabled = FALSE;
    priv->wall_slide_speed = LRG_PLATFORMER_DEFAULT_WALL_SLIDE_SPEED;
    priv->wall_jump_enabled = FALSE;
    priv->wall_jump_x = LRG_PLATFORMER_DEFAULT_WALL_JUMP_X;
    priv->wall_jump_y = LRG_PLATFORMER_DEFAULT_WALL_JUMP_Y;

    /* State */
    priv->is_grounded = TRUE;
    priv->was_grounded = TRUE;
    priv->is_jumping = FALSE;
    priv->is_wall_sliding = FALSE;
    priv->jump_held = FALSE;
    priv->facing_direction = 1;
    priv->wall_direction = 0;

    /* Hitbox */
    priv->hitbox_width = 32.0f;
    priv->hitbox_height = 48.0f;
    priv->ground_y = virtual_height - 50.0f;
}

static void
lrg_platformer_template_class_init (LrgPlatformerTemplateClass *klass)
{
    GObjectClass *object_class;
    LrgGameTemplateClass *template_class;
    LrgGame2DTemplateClass *template_2d_class;

    object_class = G_OBJECT_CLASS (klass);
    template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    template_2d_class = LRG_GAME_2D_TEMPLATE_CLASS (klass);

    object_class->set_property = lrg_platformer_template_set_property;
    object_class->get_property = lrg_platformer_template_get_property;

    template_class->pre_update = lrg_platformer_template_pre_update;
    template_2d_class->draw_world = lrg_platformer_template_draw_world;

    klass->on_landed = lrg_platformer_template_real_on_landed;
    klass->on_jump = lrg_platformer_template_real_on_jump;
    klass->on_wall_slide = lrg_platformer_template_real_on_wall_slide;
    klass->on_wall_jump = lrg_platformer_template_real_on_wall_jump;
    klass->update_physics = lrg_platformer_template_real_update_physics;
    klass->check_ground = lrg_platformer_template_real_check_ground;
    klass->check_wall = lrg_platformer_template_real_check_wall;

    /* Properties */
    properties[PROP_PLAYER_X] =
        g_param_spec_float ("player-x", "Player X", "Player X position",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_PLAYER_Y] =
        g_param_spec_float ("player-y", "Player Y", "Player Y position",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MOVE_SPEED] =
        g_param_spec_float ("move-speed", "Move Speed", "Horizontal movement speed",
                            0.0f, G_MAXFLOAT, LRG_PLATFORMER_DEFAULT_MOVE_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_ACCELERATION] =
        g_param_spec_float ("acceleration", "Acceleration", "Ground acceleration",
                            0.0f, G_MAXFLOAT, LRG_PLATFORMER_DEFAULT_ACCELERATION,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_FRICTION] =
        g_param_spec_float ("friction", "Friction", "Ground friction",
                            0.0f, G_MAXFLOAT, LRG_PLATFORMER_DEFAULT_FRICTION,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_AIR_FRICTION] =
        g_param_spec_float ("air-friction", "Air Friction", "Air friction",
                            0.0f, G_MAXFLOAT, LRG_PLATFORMER_DEFAULT_AIR_FRICTION,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_GRAVITY] =
        g_param_spec_float ("gravity", "Gravity", "Gravity acceleration",
                            0.0f, G_MAXFLOAT, LRG_PLATFORMER_DEFAULT_GRAVITY,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_JUMP_HEIGHT] =
        g_param_spec_float ("jump-height", "Jump Height", "Maximum jump height",
                            0.0f, G_MAXFLOAT, LRG_PLATFORMER_DEFAULT_JUMP_HEIGHT,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_FALL_MULTIPLIER] =
        g_param_spec_float ("fall-multiplier", "Fall Multiplier", "Gravity multiplier when falling",
                            1.0f, G_MAXFLOAT, LRG_PLATFORMER_DEFAULT_FALL_MULTIPLIER,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_FALL_SPEED] =
        g_param_spec_float ("max-fall-speed", "Max Fall Speed", "Terminal velocity",
                            0.0f, G_MAXFLOAT, LRG_PLATFORMER_DEFAULT_MAX_FALL_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_COYOTE_TIME] =
        g_param_spec_float ("coyote-time", "Coyote Time", "Jump grace period after leaving ledge",
                            0.0f, 1.0f, LRG_PLATFORMER_DEFAULT_COYOTE_TIME,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_JUMP_BUFFER_TIME] =
        g_param_spec_float ("jump-buffer-time", "Jump Buffer Time", "Pre-emptive jump input window",
                            0.0f, 1.0f, LRG_PLATFORMER_DEFAULT_JUMP_BUFFER,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_WALL_SLIDE_ENABLED] =
        g_param_spec_boolean ("wall-slide-enabled", "Wall Slide Enabled", "Enable wall sliding",
                              FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_WALL_SLIDE_SPEED] =
        g_param_spec_float ("wall-slide-speed", "Wall Slide Speed", "Wall slide speed",
                            0.0f, G_MAXFLOAT, LRG_PLATFORMER_DEFAULT_WALL_SLIDE_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_WALL_JUMP_ENABLED] =
        g_param_spec_boolean ("wall-jump-enabled", "Wall Jump Enabled", "Enable wall jumping",
                              FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPERTIES, properties);

    signals[SIGNAL_LANDED] =
        g_signal_new ("landed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgPlatformerTemplateClass, on_landed),
                      NULL, NULL, NULL, G_TYPE_NONE, 0);

    signals[SIGNAL_JUMPED] =
        g_signal_new ("jumped", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgPlatformerTemplateClass, on_jump),
                      NULL, NULL, NULL, G_TYPE_NONE, 0);

    signals[SIGNAL_WALL_SLIDE_STARTED] =
        g_signal_new ("wall-slide-started", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgPlatformerTemplateClass, on_wall_slide),
                      NULL, NULL, NULL, G_TYPE_NONE, 0);

    signals[SIGNAL_WALL_JUMPED] =
        g_signal_new ("wall-jumped", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgPlatformerTemplateClass, on_wall_jump),
                      NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgPlatformerTemplate *
lrg_platformer_template_new (void)
{
    return g_object_new (LRG_TYPE_PLATFORMER_TEMPLATE, NULL);
}

void
lrg_platformer_template_get_player_position (LrgPlatformerTemplate *self,
                                              gfloat                *x,
                                              gfloat                *y)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self));
    priv = lrg_platformer_template_get_instance_private (self);
    if (x) *x = priv->player_x;
    if (y) *y = priv->player_y;
}

void
lrg_platformer_template_set_player_position (LrgPlatformerTemplate *self,
                                              gfloat                 x,
                                              gfloat                 y)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self));
    priv = lrg_platformer_template_get_instance_private (self);
    priv->player_x = x;
    priv->player_y = y;
}

void
lrg_platformer_template_get_velocity (LrgPlatformerTemplate *self,
                                       gfloat                *vx,
                                       gfloat                *vy)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self));
    priv = lrg_platformer_template_get_instance_private (self);
    if (vx) *vx = priv->velocity_x;
    if (vy) *vy = priv->velocity_y;
}

void
lrg_platformer_template_set_velocity (LrgPlatformerTemplate *self,
                                       gfloat                 vx,
                                       gfloat                 vy)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self));
    priv = lrg_platformer_template_get_instance_private (self);
    priv->velocity_x = vx;
    priv->velocity_y = vy;
}

gfloat lrg_platformer_template_get_move_speed (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self), 0.0f);
    priv = lrg_platformer_template_get_instance_private (self);
    return priv->move_speed;
}

void lrg_platformer_template_set_move_speed (LrgPlatformerTemplate *self, gfloat speed)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self));
    priv = lrg_platformer_template_get_instance_private (self);
    priv->move_speed = speed;
}

gfloat lrg_platformer_template_get_acceleration (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self), 0.0f);
    priv = lrg_platformer_template_get_instance_private (self);
    return priv->acceleration;
}

void lrg_platformer_template_set_acceleration (LrgPlatformerTemplate *self, gfloat accel)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self));
    priv = lrg_platformer_template_get_instance_private (self);
    priv->acceleration = accel;
}

gfloat lrg_platformer_template_get_friction (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self), 0.0f);
    priv = lrg_platformer_template_get_instance_private (self);
    return priv->friction;
}

void lrg_platformer_template_set_friction (LrgPlatformerTemplate *self, gfloat friction)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self));
    priv = lrg_platformer_template_get_instance_private (self);
    priv->friction = friction;
}

gfloat lrg_platformer_template_get_air_friction (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self), 0.0f);
    priv = lrg_platformer_template_get_instance_private (self);
    return priv->air_friction;
}

void lrg_platformer_template_set_air_friction (LrgPlatformerTemplate *self, gfloat friction)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self));
    priv = lrg_platformer_template_get_instance_private (self);
    priv->air_friction = friction;
}

gfloat lrg_platformer_template_get_gravity (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self), 0.0f);
    priv = lrg_platformer_template_get_instance_private (self);
    return priv->gravity;
}

void lrg_platformer_template_set_gravity (LrgPlatformerTemplate *self, gfloat gravity)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self));
    priv = lrg_platformer_template_get_instance_private (self);
    priv->gravity = gravity;
    priv->jump_velocity = calculate_jump_velocity (gravity, priv->jump_height);
}

gfloat lrg_platformer_template_get_jump_height (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self), 0.0f);
    priv = lrg_platformer_template_get_instance_private (self);
    return priv->jump_height;
}

void lrg_platformer_template_set_jump_height (LrgPlatformerTemplate *self, gfloat height)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self));
    priv = lrg_platformer_template_get_instance_private (self);
    priv->jump_height = height;
    priv->jump_velocity = calculate_jump_velocity (priv->gravity, height);
}

gfloat lrg_platformer_template_get_fall_multiplier (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self), 1.0f);
    priv = lrg_platformer_template_get_instance_private (self);
    return priv->fall_multiplier;
}

void lrg_platformer_template_set_fall_multiplier (LrgPlatformerTemplate *self, gfloat mult)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self));
    priv = lrg_platformer_template_get_instance_private (self);
    priv->fall_multiplier = mult;
}

gfloat lrg_platformer_template_get_max_fall_speed (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self), 0.0f);
    priv = lrg_platformer_template_get_instance_private (self);
    return priv->max_fall_speed;
}

void lrg_platformer_template_set_max_fall_speed (LrgPlatformerTemplate *self, gfloat speed)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self));
    priv = lrg_platformer_template_get_instance_private (self);
    priv->max_fall_speed = speed;
}

gfloat lrg_platformer_template_get_coyote_time (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self), 0.0f);
    priv = lrg_platformer_template_get_instance_private (self);
    return priv->coyote_time;
}

void lrg_platformer_template_set_coyote_time (LrgPlatformerTemplate *self, gfloat time)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self));
    priv = lrg_platformer_template_get_instance_private (self);
    priv->coyote_time = time;
}

gfloat lrg_platformer_template_get_jump_buffer_time (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self), 0.0f);
    priv = lrg_platformer_template_get_instance_private (self);
    return priv->jump_buffer_time;
}

void lrg_platformer_template_set_jump_buffer_time (LrgPlatformerTemplate *self, gfloat time)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self));
    priv = lrg_platformer_template_get_instance_private (self);
    priv->jump_buffer_time = time;
}

gboolean lrg_platformer_template_get_wall_slide_enabled (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self), FALSE);
    priv = lrg_platformer_template_get_instance_private (self);
    return priv->wall_slide_enabled;
}

void lrg_platformer_template_set_wall_slide_enabled (LrgPlatformerTemplate *self, gboolean enabled)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self));
    priv = lrg_platformer_template_get_instance_private (self);
    priv->wall_slide_enabled = enabled;
}

gfloat lrg_platformer_template_get_wall_slide_speed (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self), 0.0f);
    priv = lrg_platformer_template_get_instance_private (self);
    return priv->wall_slide_speed;
}

void lrg_platformer_template_set_wall_slide_speed (LrgPlatformerTemplate *self, gfloat speed)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self));
    priv = lrg_platformer_template_get_instance_private (self);
    priv->wall_slide_speed = speed;
}

gboolean lrg_platformer_template_get_wall_jump_enabled (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self), FALSE);
    priv = lrg_platformer_template_get_instance_private (self);
    return priv->wall_jump_enabled;
}

void lrg_platformer_template_set_wall_jump_enabled (LrgPlatformerTemplate *self, gboolean enabled)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self));
    priv = lrg_platformer_template_get_instance_private (self);
    priv->wall_jump_enabled = enabled;
}

void lrg_platformer_template_get_wall_jump_force (LrgPlatformerTemplate *self, gfloat *x, gfloat *y)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self));
    priv = lrg_platformer_template_get_instance_private (self);
    if (x) *x = priv->wall_jump_x;
    if (y) *y = priv->wall_jump_y;
}

void lrg_platformer_template_set_wall_jump_force (LrgPlatformerTemplate *self, gfloat x, gfloat y)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self));
    priv = lrg_platformer_template_get_instance_private (self);
    priv->wall_jump_x = x;
    priv->wall_jump_y = y;
}

gboolean lrg_platformer_template_is_grounded (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self), FALSE);
    priv = lrg_platformer_template_get_instance_private (self);
    return priv->is_grounded;
}

gboolean lrg_platformer_template_is_jumping (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self), FALSE);
    priv = lrg_platformer_template_get_instance_private (self);
    return priv->is_jumping && priv->velocity_y < 0;
}

gboolean lrg_platformer_template_is_falling (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self), FALSE);
    priv = lrg_platformer_template_get_instance_private (self);
    return !priv->is_grounded && priv->velocity_y > 0;
}

gboolean lrg_platformer_template_is_wall_sliding (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self), FALSE);
    priv = lrg_platformer_template_get_instance_private (self);
    return priv->is_wall_sliding;
}

gint lrg_platformer_template_get_facing_direction (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self), 1);
    priv = lrg_platformer_template_get_instance_private (self);
    return priv->facing_direction;
}

void lrg_platformer_template_set_move_input (LrgPlatformerTemplate *self, gfloat input)
{
    LrgPlatformerTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self));
    priv = lrg_platformer_template_get_instance_private (self);
    priv->move_input = CLAMP (input, -1.0f, 1.0f);
}

gboolean lrg_platformer_template_jump (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;
    LrgPlatformerTemplateClass *klass;

    g_return_val_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self), FALSE);

    priv = lrg_platformer_template_get_instance_private (self);
    klass = LRG_PLATFORMER_TEMPLATE_GET_CLASS (self);

    priv->jump_held = TRUE;

    /* Wall jump */
    if (priv->wall_jump_enabled && priv->is_wall_sliding)
    {
        priv->velocity_x = -priv->wall_direction * priv->wall_jump_x;
        priv->velocity_y = -priv->wall_jump_y;
        priv->is_wall_sliding = FALSE;

        if (klass->on_wall_jump != NULL)
            klass->on_wall_jump (self, -priv->wall_direction);

        return TRUE;
    }

    /* Normal jump or coyote jump */
    if (priv->is_grounded || priv->coyote_timer > 0.0f)
    {
        priv->velocity_y = priv->jump_velocity;
        priv->is_jumping = TRUE;
        priv->coyote_timer = 0.0f;

        if (klass->on_jump != NULL)
            klass->on_jump (self);

        return TRUE;
    }

    /* Buffer the jump */
    priv->jump_buffer_timer = priv->jump_buffer_time;
    return TRUE;
}

void lrg_platformer_template_release_jump (LrgPlatformerTemplate *self)
{
    LrgPlatformerTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_PLATFORMER_TEMPLATE (self));

    priv = lrg_platformer_template_get_instance_private (self);
    priv->jump_held = FALSE;
}
