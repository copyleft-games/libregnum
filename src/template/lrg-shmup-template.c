/* lrg-shmup-template.c - Scrolling shooter (shmup) template implementation
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "../config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TEMPLATE

#include "lrg-shmup-template.h"
#include "lrg-shooter-2d-template-private.h"
#include "../lrg-log.h"

#include <graylib.h>
#include <math.h>

/* ==========================================================================
 * Default Constants
 * ========================================================================== */

#define LRG_SHMUP_DEFAULT_SCROLL_SPEED          60.0f
#define LRG_SHMUP_DEFAULT_LIVES                 3
#define LRG_SHMUP_DEFAULT_MAX_LIVES             5
#define LRG_SHMUP_DEFAULT_CONTINUES             3
#define LRG_SHMUP_DEFAULT_BOMBS                 3
#define LRG_SHMUP_DEFAULT_MAX_BOMBS             5
#define LRG_SHMUP_DEFAULT_BOMB_DURATION         2.0f
#define LRG_SHMUP_DEFAULT_MAX_POWER_LEVEL       4
#define LRG_SHMUP_DEFAULT_POWER_PER_LEVEL       100
#define LRG_SHMUP_DEFAULT_GRAZE_RADIUS          20.0f
#define LRG_SHMUP_DEFAULT_GRAZE_POINTS          10
#define LRG_SHMUP_DEFAULT_HITBOX_RADIUS         3.0f
#define LRG_SHMUP_DEFAULT_FOCUS_SPEED           0.5f

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct
{
    /* Scrolling */
    LrgShmupScrollDirection scroll_direction;
    gfloat scroll_speed;
    gfloat scroll_position;
    gboolean scroll_paused;

    /* Lives & Continues */
    gint lives;
    gint max_lives;
    gint continues;

    /* Bombs */
    gint bombs;
    gint max_bombs;
    gfloat bomb_duration;
    gfloat bomb_timer;

    /* Power */
    gint power_level;
    gint max_power_level;
    gint power_points;
    gint power_per_level;

    /* Grazing */
    guint graze_count;
    gfloat graze_radius;
    gint64 graze_points;

    /* Hitbox */
    gfloat hitbox_radius;
    gboolean show_hitbox;

    /* Focus mode */
    gfloat focus_speed_multiplier;
    gboolean is_focused;

    /* Movement input */
    gfloat move_x;
    gfloat move_y;

    /* Invincibility after death */
    gfloat invincibility_timer;
} LrgShmupTemplatePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgShmupTemplate, lrg_shmup_template,
                            LRG_TYPE_SHOOTER_2D_TEMPLATE)

/* ==========================================================================
 * Property IDs
 * ========================================================================== */

enum
{
    PROP_0,
    PROP_SCROLL_DIRECTION,
    PROP_SCROLL_SPEED,
    PROP_SCROLL_POSITION,
    PROP_SCROLL_PAUSED,
    PROP_LIVES,
    PROP_MAX_LIVES,
    PROP_CONTINUES,
    PROP_BOMBS,
    PROP_MAX_BOMBS,
    PROP_BOMB_DURATION,
    PROP_POWER_LEVEL,
    PROP_MAX_POWER_LEVEL,
    PROP_GRAZE_RADIUS,
    PROP_GRAZE_POINTS,
    PROP_HITBOX_RADIUS,
    PROP_SHOW_HITBOX,
    PROP_FOCUS_SPEED_MULTIPLIER,
    PROP_FOCUSED,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL };

/* ==========================================================================
 * Signal IDs
 * ========================================================================== */

enum
{
    SIGNAL_LIFE_LOST,
    SIGNAL_GAME_OVER,
    SIGNAL_CONTINUE_USED,
    SIGNAL_BOMB_USED,
    SIGNAL_POWER_LEVEL_CHANGED,
    SIGNAL_BULLET_GRAZED,
    N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

/* ==========================================================================
 * LrgGameTemplate Overrides
 * ========================================================================== */

static void
lrg_shmup_template_pre_update (LrgGameTemplate *template,
                                gdouble          delta)
{
    LrgShmupTemplate *self;
    LrgShmupTemplatePrivate *priv;
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

    self = LRG_SHMUP_TEMPLATE (template);
    priv = lrg_shmup_template_get_instance_private (self);
    delta_f = (gfloat) delta;

    /* Update scrolling */
    if (!priv->scroll_paused && priv->scroll_direction != LRG_SHMUP_SCROLL_NONE)
    {
        priv->scroll_position += priv->scroll_speed * delta_f;
    }

    /* Update bomb timer */
    if (priv->bomb_timer > 0.0f)
    {
        priv->bomb_timer -= delta_f;
        if (priv->bomb_timer < 0.0f)
            priv->bomb_timer = 0.0f;
    }

    /* Update invincibility timer */
    if (priv->invincibility_timer > 0.0f)
    {
        priv->invincibility_timer -= delta_f;
        if (priv->invincibility_timer < 0.0f)
            priv->invincibility_timer = 0.0f;
    }

    /* Get current player position */
    lrg_shooter_2d_template_get_player_position (LRG_SHOOTER_2D_TEMPLATE (self),
                                                  &player_x, &player_y);

    /* Calculate movement speed */
    g_object_get (self, "player-speed", &player_speed, NULL);

    if (priv->is_focused)
        move_speed = player_speed * priv->focus_speed_multiplier;
    else
        move_speed = player_speed;

    /* Apply movement */
    player_x += priv->move_x * move_speed * delta_f;
    player_y += priv->move_y * move_speed * delta_f;

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
    parent_class = LRG_GAME_TEMPLATE_CLASS (lrg_shmup_template_parent_class);
    if (parent_class->pre_update != NULL)
        parent_class->pre_update (template, delta);
}

/* ==========================================================================
 * LrgGame2DTemplate Overrides
 * ========================================================================== */

static void
lrg_shmup_template_draw_world (LrgGame2DTemplate *template)
{
    LrgShmupTemplate *self;
    LrgShmupTemplatePrivate *priv;
    LrgGame2DTemplateClass *parent_class;
    gfloat player_x;
    gfloat player_y;

    self = LRG_SHMUP_TEMPLATE (template);
    priv = lrg_shmup_template_get_instance_private (self);

    /* Chain up to draw projectiles and player */
    parent_class = LRG_GAME_2D_TEMPLATE_CLASS (lrg_shmup_template_parent_class);
    if (parent_class->draw_world != NULL)
        parent_class->draw_world (template);

    /* Get player position */
    lrg_shooter_2d_template_get_player_position (LRG_SHOOTER_2D_TEMPLATE (self),
                                                  &player_x, &player_y);

    /* Draw hitbox if enabled */
    if (priv->show_hitbox)
    {
        g_autoptr(GrlColor) hitbox_color = grl_color_new (255, 255, 255, 200);
        grl_draw_circle ((gint) player_x, (gint) player_y,
                         priv->hitbox_radius, hitbox_color);
    }

    /* Draw graze radius (faint) when focused */
    if (priv->is_focused)
    {
        g_autoptr(GrlColor) graze_color = grl_color_new (100, 100, 255, 50);
        grl_draw_circle ((gint) player_x, (gint) player_y,
                         priv->graze_radius, graze_color);
    }

    /* Draw invincibility flash */
    if (priv->invincibility_timer > 0.0f)
    {
        gint flash;

        flash = (gint) (priv->invincibility_timer * 10.0f) % 2;
        if (flash)
        {
            g_autoptr(GrlColor) flash_color = grl_color_new (255, 255, 255, 100);
            grl_draw_circle ((gint) player_x, (gint) player_y, 20.0f, flash_color);
        }
    }

    /* Draw bomb effect */
    if (priv->bomb_timer > 0.0f)
    {
        gfloat bomb_radius;
        guint8 alpha;

        bomb_radius = 200.0f * (1.0f - priv->bomb_timer / priv->bomb_duration);
        alpha = (guint8) (200 * (priv->bomb_timer / priv->bomb_duration));

        {
            g_autoptr(GrlColor) bomb_color = grl_color_new (255, 200, 100, alpha);
            grl_draw_circle ((gint) player_x, (gint) player_y, bomb_radius, bomb_color);
        }
    }
}

static void
lrg_shmup_template_draw_ui (LrgGame2DTemplate *template)
{
    LrgShmupTemplate *self;
    LrgShmupTemplatePrivate *priv;
    LrgGame2DTemplateClass *parent_class;
    gint i;
    gint virtual_width;

    self = LRG_SHMUP_TEMPLATE (template);
    priv = lrg_shmup_template_get_instance_private (self);

    /* Chain up first */
    parent_class = LRG_GAME_2D_TEMPLATE_CLASS (lrg_shmup_template_parent_class);
    if (parent_class->draw_ui != NULL)
        parent_class->draw_ui (template);

    virtual_width = lrg_game_2d_template_get_virtual_width (template);

    /* Draw lives */
    for (i = 0; i < priv->lives; i++)
    {
        g_autoptr(GrlColor) life_color = grl_color_new (255, 100, 100, 255);
        grl_draw_rectangle (10 + i * 25, 10, 20, 20, life_color);
    }

    /* Draw bombs */
    for (i = 0; i < priv->bombs; i++)
    {
        g_autoptr(GrlColor) bomb_color = grl_color_new (100, 100, 255, 255);
        grl_draw_circle (20 + i * 25, 45, 8.0f, bomb_color);
    }

    /* Draw power level */
    {
        g_autoptr(GrlColor) power_bg = grl_color_new (50, 50, 50, 200);
        g_autoptr(GrlColor) power_fg = grl_color_new (100, 255, 100, 255);
        gint bar_width;

        bar_width = 100;

        grl_draw_rectangle (virtual_width - bar_width - 10, 10, bar_width, 15, power_bg);
        grl_draw_rectangle (virtual_width - bar_width - 10, 10,
                            (bar_width * priv->power_level) / priv->max_power_level,
                            15, power_fg);
    }

    /* Draw graze count */
    {
        gchar graze_text[32];
        g_autoptr(GrlColor) text_color = grl_color_new (200, 200, 255, 255);

        g_snprintf (graze_text, sizeof (graze_text), "Graze: %u", priv->graze_count);
        grl_draw_text (graze_text, virtual_width - 100, 35, 16, text_color);
    }
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_shmup_template_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgShmupTemplate *self;

    self = LRG_SHMUP_TEMPLATE (object);
    /* Note: Public API functions will get priv themselves */

    switch (prop_id)
    {
        case PROP_SCROLL_DIRECTION:
            lrg_shmup_template_set_scroll_direction (self, g_value_get_int (value));
            break;

        case PROP_SCROLL_SPEED:
            lrg_shmup_template_set_scroll_speed (self, g_value_get_float (value));
            break;

        case PROP_SCROLL_POSITION:
            lrg_shmup_template_set_scroll_position (self, g_value_get_float (value));
            break;

        case PROP_SCROLL_PAUSED:
            lrg_shmup_template_set_scroll_paused (self, g_value_get_boolean (value));
            break;

        case PROP_LIVES:
            lrg_shmup_template_set_lives (self, g_value_get_int (value));
            break;

        case PROP_MAX_LIVES:
            lrg_shmup_template_set_max_lives (self, g_value_get_int (value));
            break;

        case PROP_CONTINUES:
            lrg_shmup_template_set_continues (self, g_value_get_int (value));
            break;

        case PROP_BOMBS:
            lrg_shmup_template_set_bombs (self, g_value_get_int (value));
            break;

        case PROP_MAX_BOMBS:
            lrg_shmup_template_set_max_bombs (self, g_value_get_int (value));
            break;

        case PROP_BOMB_DURATION:
            lrg_shmup_template_set_bomb_duration (self, g_value_get_float (value));
            break;

        case PROP_POWER_LEVEL:
            lrg_shmup_template_set_power_level (self, g_value_get_int (value));
            break;

        case PROP_MAX_POWER_LEVEL:
            lrg_shmup_template_set_max_power_level (self, g_value_get_int (value));
            break;

        case PROP_GRAZE_RADIUS:
            lrg_shmup_template_set_graze_radius (self, g_value_get_float (value));
            break;

        case PROP_GRAZE_POINTS:
            lrg_shmup_template_set_graze_points (self, g_value_get_int64 (value));
            break;

        case PROP_HITBOX_RADIUS:
            lrg_shmup_template_set_hitbox_radius (self, g_value_get_float (value));
            break;

        case PROP_SHOW_HITBOX:
            lrg_shmup_template_set_show_hitbox (self, g_value_get_boolean (value));
            break;

        case PROP_FOCUS_SPEED_MULTIPLIER:
            lrg_shmup_template_set_focus_speed_multiplier (self, g_value_get_float (value));
            break;

        case PROP_FOCUSED:
            lrg_shmup_template_set_focused (self, g_value_get_boolean (value));
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
lrg_shmup_template_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgShmupTemplate *self;
    LrgShmupTemplatePrivate *priv;

    self = LRG_SHMUP_TEMPLATE (object);
    priv = lrg_shmup_template_get_instance_private (self);

    switch (prop_id)
    {
        case PROP_SCROLL_DIRECTION:
            g_value_set_int (value, priv->scroll_direction);
            break;

        case PROP_SCROLL_SPEED:
            g_value_set_float (value, priv->scroll_speed);
            break;

        case PROP_SCROLL_POSITION:
            g_value_set_float (value, priv->scroll_position);
            break;

        case PROP_SCROLL_PAUSED:
            g_value_set_boolean (value, priv->scroll_paused);
            break;

        case PROP_LIVES:
            g_value_set_int (value, priv->lives);
            break;

        case PROP_MAX_LIVES:
            g_value_set_int (value, priv->max_lives);
            break;

        case PROP_CONTINUES:
            g_value_set_int (value, priv->continues);
            break;

        case PROP_BOMBS:
            g_value_set_int (value, priv->bombs);
            break;

        case PROP_MAX_BOMBS:
            g_value_set_int (value, priv->max_bombs);
            break;

        case PROP_BOMB_DURATION:
            g_value_set_float (value, priv->bomb_duration);
            break;

        case PROP_POWER_LEVEL:
            g_value_set_int (value, priv->power_level);
            break;

        case PROP_MAX_POWER_LEVEL:
            g_value_set_int (value, priv->max_power_level);
            break;

        case PROP_GRAZE_RADIUS:
            g_value_set_float (value, priv->graze_radius);
            break;

        case PROP_GRAZE_POINTS:
            g_value_set_int64 (value, priv->graze_points);
            break;

        case PROP_HITBOX_RADIUS:
            g_value_set_float (value, priv->hitbox_radius);
            break;

        case PROP_SHOW_HITBOX:
            g_value_set_boolean (value, priv->show_hitbox);
            break;

        case PROP_FOCUS_SPEED_MULTIPLIER:
            g_value_set_float (value, priv->focus_speed_multiplier);
            break;

        case PROP_FOCUSED:
            g_value_set_boolean (value, priv->is_focused);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
lrg_shmup_template_init (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    priv = lrg_shmup_template_get_instance_private (self);

    /* Scrolling */
    priv->scroll_direction = LRG_SHMUP_SCROLL_UP;
    priv->scroll_speed = LRG_SHMUP_DEFAULT_SCROLL_SPEED;
    priv->scroll_position = 0.0f;
    priv->scroll_paused = FALSE;

    /* Lives & Continues */
    priv->lives = LRG_SHMUP_DEFAULT_LIVES;
    priv->max_lives = LRG_SHMUP_DEFAULT_MAX_LIVES;
    priv->continues = LRG_SHMUP_DEFAULT_CONTINUES;

    /* Bombs */
    priv->bombs = LRG_SHMUP_DEFAULT_BOMBS;
    priv->max_bombs = LRG_SHMUP_DEFAULT_MAX_BOMBS;
    priv->bomb_duration = LRG_SHMUP_DEFAULT_BOMB_DURATION;
    priv->bomb_timer = 0.0f;

    /* Power */
    priv->power_level = 1;
    priv->max_power_level = LRG_SHMUP_DEFAULT_MAX_POWER_LEVEL;
    priv->power_points = 0;
    priv->power_per_level = LRG_SHMUP_DEFAULT_POWER_PER_LEVEL;

    /* Grazing */
    priv->graze_count = 0;
    priv->graze_radius = LRG_SHMUP_DEFAULT_GRAZE_RADIUS;
    priv->graze_points = LRG_SHMUP_DEFAULT_GRAZE_POINTS;

    /* Hitbox */
    priv->hitbox_radius = LRG_SHMUP_DEFAULT_HITBOX_RADIUS;
    priv->show_hitbox = FALSE;

    /* Focus mode */
    priv->focus_speed_multiplier = LRG_SHMUP_DEFAULT_FOCUS_SPEED;
    priv->is_focused = FALSE;

    /* Movement */
    priv->move_x = 0.0f;
    priv->move_y = 0.0f;

    /* Invincibility */
    priv->invincibility_timer = 0.0f;

    /* Enable auto-fire for shmups */
    g_object_set (self, "auto-fire", TRUE, NULL);
}

static void
lrg_shmup_template_class_init (LrgShmupTemplateClass *klass)
{
    GObjectClass *object_class;
    LrgGameTemplateClass *template_class;
    LrgGame2DTemplateClass *template_2d_class;

    object_class = G_OBJECT_CLASS (klass);
    template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    template_2d_class = LRG_GAME_2D_TEMPLATE_CLASS (klass);

    /* GObject overrides */
    object_class->set_property = lrg_shmup_template_set_property;
    object_class->get_property = lrg_shmup_template_get_property;

    /* LrgGameTemplate overrides */
    template_class->pre_update = lrg_shmup_template_pre_update;

    /* LrgGame2DTemplate overrides */
    template_2d_class->draw_world = lrg_shmup_template_draw_world;
    template_2d_class->draw_ui = lrg_shmup_template_draw_ui;

    /* Properties */
    properties[PROP_SCROLL_DIRECTION] =
        g_param_spec_int ("scroll-direction", "Scroll Direction",
                          "Automatic scroll direction",
                          LRG_SHMUP_SCROLL_UP, LRG_SHMUP_SCROLL_NONE,
                          LRG_SHMUP_SCROLL_UP,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SCROLL_SPEED] =
        g_param_spec_float ("scroll-speed", "Scroll Speed",
                            "Scroll speed in units per second",
                            0.0f, G_MAXFLOAT,
                            LRG_SHMUP_DEFAULT_SCROLL_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SCROLL_POSITION] =
        g_param_spec_float ("scroll-position", "Scroll Position",
                            "Current scroll position",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SCROLL_PAUSED] =
        g_param_spec_boolean ("scroll-paused", "Scroll Paused",
                              "Whether scrolling is paused",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_LIVES] =
        g_param_spec_int ("lives", "Lives",
                          "Current number of lives",
                          0, G_MAXINT,
                          LRG_SHMUP_DEFAULT_LIVES,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_LIVES] =
        g_param_spec_int ("max-lives", "Max Lives",
                          "Maximum number of lives",
                          1, G_MAXINT,
                          LRG_SHMUP_DEFAULT_MAX_LIVES,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_CONTINUES] =
        g_param_spec_int ("continues", "Continues",
                          "Number of continues remaining",
                          0, G_MAXINT,
                          LRG_SHMUP_DEFAULT_CONTINUES,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_BOMBS] =
        g_param_spec_int ("bombs", "Bombs",
                          "Current number of bombs",
                          0, G_MAXINT,
                          LRG_SHMUP_DEFAULT_BOMBS,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_BOMBS] =
        g_param_spec_int ("max-bombs", "Max Bombs",
                          "Maximum number of bombs",
                          0, G_MAXINT,
                          LRG_SHMUP_DEFAULT_MAX_BOMBS,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_BOMB_DURATION] =
        g_param_spec_float ("bomb-duration", "Bomb Duration",
                            "Bomb effect duration in seconds",
                            0.0f, G_MAXFLOAT,
                            LRG_SHMUP_DEFAULT_BOMB_DURATION,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_POWER_LEVEL] =
        g_param_spec_int ("power-level", "Power Level",
                          "Current weapon power level",
                          1, G_MAXINT, 1,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_POWER_LEVEL] =
        g_param_spec_int ("max-power-level", "Max Power Level",
                          "Maximum weapon power level",
                          1, G_MAXINT,
                          LRG_SHMUP_DEFAULT_MAX_POWER_LEVEL,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_GRAZE_RADIUS] =
        g_param_spec_float ("graze-radius", "Graze Radius",
                            "Graze detection radius",
                            0.0f, G_MAXFLOAT,
                            LRG_SHMUP_DEFAULT_GRAZE_RADIUS,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_GRAZE_POINTS] =
        g_param_spec_int64 ("graze-points", "Graze Points",
                            "Score points per graze",
                            0, G_MAXINT64,
                            LRG_SHMUP_DEFAULT_GRAZE_POINTS,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_HITBOX_RADIUS] =
        g_param_spec_float ("hitbox-radius", "Hitbox Radius",
                            "Player hitbox radius",
                            0.0f, G_MAXFLOAT,
                            LRG_SHMUP_DEFAULT_HITBOX_RADIUS,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_HITBOX] =
        g_param_spec_boolean ("show-hitbox", "Show Hitbox",
                              "Whether to display the player hitbox",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_FOCUS_SPEED_MULTIPLIER] =
        g_param_spec_float ("focus-speed-multiplier", "Focus Speed Multiplier",
                            "Speed multiplier when focused",
                            0.0f, 1.0f,
                            LRG_SHMUP_DEFAULT_FOCUS_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_FOCUSED] =
        g_param_spec_boolean ("focused", "Focused",
                              "Whether the player is in focus mode",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPERTIES, properties);

    /* Signals */
    signals[SIGNAL_LIFE_LOST] =
        g_signal_new ("life-lost",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_INT);

    signals[SIGNAL_GAME_OVER] =
        g_signal_new ("game-over",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    signals[SIGNAL_CONTINUE_USED] =
        g_signal_new ("continue-used",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_INT);

    signals[SIGNAL_BOMB_USED] =
        g_signal_new ("bomb-used",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_INT);

    signals[SIGNAL_POWER_LEVEL_CHANGED] =
        g_signal_new ("power-level-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_INT);

    signals[SIGNAL_BULLET_GRAZED] =
        g_signal_new ("bullet-grazed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_UINT);
}

/* ==========================================================================
 * Public API - Constructor
 * ========================================================================== */

LrgShmupTemplate *
lrg_shmup_template_new (void)
{
    return g_object_new (LRG_TYPE_SHMUP_TEMPLATE, NULL);
}

/* ==========================================================================
 * Public API - Scrolling
 * ========================================================================== */

LrgShmupScrollDirection
lrg_shmup_template_get_scroll_direction (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), LRG_SHMUP_SCROLL_UP);

    priv = lrg_shmup_template_get_instance_private (self);
    return priv->scroll_direction;
}

void
lrg_shmup_template_set_scroll_direction (LrgShmupTemplate       *self,
                                          LrgShmupScrollDirection direction)
{
    LrgShmupTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHMUP_TEMPLATE (self));

    priv = lrg_shmup_template_get_instance_private (self);
    priv->scroll_direction = direction;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCROLL_DIRECTION]);
}

gfloat
lrg_shmup_template_get_scroll_speed (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), LRG_SHMUP_DEFAULT_SCROLL_SPEED);

    priv = lrg_shmup_template_get_instance_private (self);
    return priv->scroll_speed;
}

void
lrg_shmup_template_set_scroll_speed (LrgShmupTemplate *self,
                                      gfloat            speed)
{
    LrgShmupTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHMUP_TEMPLATE (self));

    priv = lrg_shmup_template_get_instance_private (self);

    if (speed < 0.0f)
        speed = 0.0f;

    priv->scroll_speed = speed;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCROLL_SPEED]);
}

gfloat
lrg_shmup_template_get_scroll_position (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), 0.0f);

    priv = lrg_shmup_template_get_instance_private (self);
    return priv->scroll_position;
}

void
lrg_shmup_template_set_scroll_position (LrgShmupTemplate *self,
                                         gfloat            position)
{
    LrgShmupTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHMUP_TEMPLATE (self));

    priv = lrg_shmup_template_get_instance_private (self);
    priv->scroll_position = position;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCROLL_POSITION]);
}

gboolean
lrg_shmup_template_get_scroll_paused (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), FALSE);

    priv = lrg_shmup_template_get_instance_private (self);
    return priv->scroll_paused;
}

void
lrg_shmup_template_set_scroll_paused (LrgShmupTemplate *self,
                                       gboolean          paused)
{
    LrgShmupTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHMUP_TEMPLATE (self));

    priv = lrg_shmup_template_get_instance_private (self);
    priv->scroll_paused = paused;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCROLL_PAUSED]);
}

/* ==========================================================================
 * Public API - Lives & Continues
 * ========================================================================== */

gint
lrg_shmup_template_get_lives (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), 0);

    priv = lrg_shmup_template_get_instance_private (self);
    return priv->lives;
}

void
lrg_shmup_template_set_lives (LrgShmupTemplate *self,
                               gint              lives)
{
    LrgShmupTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHMUP_TEMPLATE (self));

    priv = lrg_shmup_template_get_instance_private (self);

    if (lives < 0)
        lives = 0;
    if (lives > priv->max_lives)
        lives = priv->max_lives;

    priv->lives = lives;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LIVES]);
}

gint
lrg_shmup_template_get_max_lives (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), LRG_SHMUP_DEFAULT_MAX_LIVES);

    priv = lrg_shmup_template_get_instance_private (self);
    return priv->max_lives;
}

void
lrg_shmup_template_set_max_lives (LrgShmupTemplate *self,
                                   gint              max_lives)
{
    LrgShmupTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHMUP_TEMPLATE (self));

    priv = lrg_shmup_template_get_instance_private (self);

    if (max_lives < 1)
        max_lives = 1;

    priv->max_lives = max_lives;

    if (priv->lives > max_lives)
        priv->lives = max_lives;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_LIVES]);
}

gint
lrg_shmup_template_get_continues (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), 0);

    priv = lrg_shmup_template_get_instance_private (self);
    return priv->continues;
}

void
lrg_shmup_template_set_continues (LrgShmupTemplate *self,
                                   gint              continues)
{
    LrgShmupTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHMUP_TEMPLATE (self));

    priv = lrg_shmup_template_get_instance_private (self);

    if (continues < 0)
        continues = 0;

    priv->continues = continues;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CONTINUES]);
}

gint
lrg_shmup_template_lose_life (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), -1);

    priv = lrg_shmup_template_get_instance_private (self);

    priv->lives--;
    priv->invincibility_timer = 2.0f;

    g_signal_emit (self, signals[SIGNAL_LIFE_LOST], 0, priv->lives);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LIVES]);

    if (priv->lives < 0)
        g_signal_emit (self, signals[SIGNAL_GAME_OVER], 0);

    return priv->lives;
}

gboolean
lrg_shmup_template_use_continue (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), FALSE);

    priv = lrg_shmup_template_get_instance_private (self);

    if (priv->continues <= 0)
        return FALSE;

    priv->continues--;
    priv->lives = LRG_SHMUP_DEFAULT_LIVES;

    g_signal_emit (self, signals[SIGNAL_CONTINUE_USED], 0, priv->continues);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CONTINUES]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LIVES]);

    return TRUE;
}

/* ==========================================================================
 * Public API - Bombs
 * ========================================================================== */

gint
lrg_shmup_template_get_bombs (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), 0);

    priv = lrg_shmup_template_get_instance_private (self);
    return priv->bombs;
}

void
lrg_shmup_template_set_bombs (LrgShmupTemplate *self,
                               gint              bombs)
{
    LrgShmupTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHMUP_TEMPLATE (self));

    priv = lrg_shmup_template_get_instance_private (self);

    if (bombs < 0)
        bombs = 0;
    if (bombs > priv->max_bombs)
        bombs = priv->max_bombs;

    priv->bombs = bombs;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BOMBS]);
}

gint
lrg_shmup_template_get_max_bombs (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), LRG_SHMUP_DEFAULT_MAX_BOMBS);

    priv = lrg_shmup_template_get_instance_private (self);
    return priv->max_bombs;
}

void
lrg_shmup_template_set_max_bombs (LrgShmupTemplate *self,
                                   gint              max_bombs)
{
    LrgShmupTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHMUP_TEMPLATE (self));

    priv = lrg_shmup_template_get_instance_private (self);

    if (max_bombs < 0)
        max_bombs = 0;

    priv->max_bombs = max_bombs;

    if (priv->bombs > max_bombs)
        priv->bombs = max_bombs;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_BOMBS]);
}

gboolean
lrg_shmup_template_use_bomb (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), FALSE);

    priv = lrg_shmup_template_get_instance_private (self);

    if (priv->bombs <= 0)
        return FALSE;

    if (priv->bomb_timer > 0.0f)
        return FALSE;

    priv->bombs--;
    priv->bomb_timer = priv->bomb_duration;

    g_signal_emit (self, signals[SIGNAL_BOMB_USED], 0, priv->bombs);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BOMBS]);

    return TRUE;
}

gboolean
lrg_shmup_template_is_bomb_active (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), FALSE);

    priv = lrg_shmup_template_get_instance_private (self);
    return priv->bomb_timer > 0.0f;
}

gfloat
lrg_shmup_template_get_bomb_duration (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), LRG_SHMUP_DEFAULT_BOMB_DURATION);

    priv = lrg_shmup_template_get_instance_private (self);
    return priv->bomb_duration;
}

void
lrg_shmup_template_set_bomb_duration (LrgShmupTemplate *self,
                                       gfloat            duration)
{
    LrgShmupTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHMUP_TEMPLATE (self));

    priv = lrg_shmup_template_get_instance_private (self);

    if (duration < 0.0f)
        duration = 0.0f;

    priv->bomb_duration = duration;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BOMB_DURATION]);
}

/* ==========================================================================
 * Public API - Power
 * ========================================================================== */

gint
lrg_shmup_template_get_power_level (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), 1);

    priv = lrg_shmup_template_get_instance_private (self);
    return priv->power_level;
}

void
lrg_shmup_template_set_power_level (LrgShmupTemplate *self,
                                     gint              level)
{
    LrgShmupTemplatePrivate *priv;
    gint old_level;

    g_return_if_fail (LRG_IS_SHMUP_TEMPLATE (self));

    priv = lrg_shmup_template_get_instance_private (self);

    if (level < 1)
        level = 1;
    if (level > priv->max_power_level)
        level = priv->max_power_level;

    old_level = priv->power_level;
    priv->power_level = level;

    if (old_level != level)
    {
        g_signal_emit (self, signals[SIGNAL_POWER_LEVEL_CHANGED], 0, level);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POWER_LEVEL]);
    }
}

gint
lrg_shmup_template_get_max_power_level (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), LRG_SHMUP_DEFAULT_MAX_POWER_LEVEL);

    priv = lrg_shmup_template_get_instance_private (self);
    return priv->max_power_level;
}

void
lrg_shmup_template_set_max_power_level (LrgShmupTemplate *self,
                                         gint              max_level)
{
    LrgShmupTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHMUP_TEMPLATE (self));

    priv = lrg_shmup_template_get_instance_private (self);

    if (max_level < 1)
        max_level = 1;

    priv->max_power_level = max_level;

    if (priv->power_level > max_level)
        lrg_shmup_template_set_power_level (self, max_level);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_POWER_LEVEL]);
}

void
lrg_shmup_template_add_power (LrgShmupTemplate *self,
                               gint              amount)
{
    LrgShmupTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHMUP_TEMPLATE (self));

    priv = lrg_shmup_template_get_instance_private (self);

    priv->power_points += amount;

    while (priv->power_points >= priv->power_per_level &&
           priv->power_level < priv->max_power_level)
    {
        priv->power_points -= priv->power_per_level;
        lrg_shmup_template_set_power_level (self, priv->power_level + 1);
    }

    /* Cap power points at max */
    if (priv->power_level >= priv->max_power_level)
        priv->power_points = 0;
}

/* ==========================================================================
 * Public API - Grazing
 * ========================================================================== */

guint
lrg_shmup_template_get_graze_count (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), 0);

    priv = lrg_shmup_template_get_instance_private (self);
    return priv->graze_count;
}

void
lrg_shmup_template_add_graze (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHMUP_TEMPLATE (self));

    priv = lrg_shmup_template_get_instance_private (self);

    priv->graze_count++;

    lrg_shooter_2d_template_add_score (LRG_SHOOTER_2D_TEMPLATE (self),
                                        priv->graze_points);

    g_signal_emit (self, signals[SIGNAL_BULLET_GRAZED], 0, priv->graze_count);
}

gfloat
lrg_shmup_template_get_graze_radius (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), LRG_SHMUP_DEFAULT_GRAZE_RADIUS);

    priv = lrg_shmup_template_get_instance_private (self);
    return priv->graze_radius;
}

void
lrg_shmup_template_set_graze_radius (LrgShmupTemplate *self,
                                      gfloat            radius)
{
    LrgShmupTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHMUP_TEMPLATE (self));

    priv = lrg_shmup_template_get_instance_private (self);

    if (radius < 0.0f)
        radius = 0.0f;

    priv->graze_radius = radius;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GRAZE_RADIUS]);
}

gint64
lrg_shmup_template_get_graze_points (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), LRG_SHMUP_DEFAULT_GRAZE_POINTS);

    priv = lrg_shmup_template_get_instance_private (self);
    return priv->graze_points;
}

void
lrg_shmup_template_set_graze_points (LrgShmupTemplate *self,
                                      gint64            points)
{
    LrgShmupTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHMUP_TEMPLATE (self));

    priv = lrg_shmup_template_get_instance_private (self);
    priv->graze_points = points;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GRAZE_POINTS]);
}

/* ==========================================================================
 * Public API - Hitbox
 * ========================================================================== */

gfloat
lrg_shmup_template_get_hitbox_radius (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), LRG_SHMUP_DEFAULT_HITBOX_RADIUS);

    priv = lrg_shmup_template_get_instance_private (self);
    return priv->hitbox_radius;
}

void
lrg_shmup_template_set_hitbox_radius (LrgShmupTemplate *self,
                                       gfloat            radius)
{
    LrgShmupTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHMUP_TEMPLATE (self));

    priv = lrg_shmup_template_get_instance_private (self);

    if (radius < 0.0f)
        radius = 0.0f;

    priv->hitbox_radius = radius;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HITBOX_RADIUS]);
}

gboolean
lrg_shmup_template_get_show_hitbox (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), FALSE);

    priv = lrg_shmup_template_get_instance_private (self);
    return priv->show_hitbox;
}

void
lrg_shmup_template_set_show_hitbox (LrgShmupTemplate *self,
                                     gboolean          show)
{
    LrgShmupTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHMUP_TEMPLATE (self));

    priv = lrg_shmup_template_get_instance_private (self);
    priv->show_hitbox = show;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_HITBOX]);
}

/* ==========================================================================
 * Public API - Focus Mode
 * ========================================================================== */

gfloat
lrg_shmup_template_get_focus_speed_multiplier (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), LRG_SHMUP_DEFAULT_FOCUS_SPEED);

    priv = lrg_shmup_template_get_instance_private (self);
    return priv->focus_speed_multiplier;
}

void
lrg_shmup_template_set_focus_speed_multiplier (LrgShmupTemplate *self,
                                                gfloat            multiplier)
{
    LrgShmupTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHMUP_TEMPLATE (self));

    priv = lrg_shmup_template_get_instance_private (self);
    priv->focus_speed_multiplier = CLAMP (multiplier, 0.0f, 1.0f);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOCUS_SPEED_MULTIPLIER]);
}

gboolean
lrg_shmup_template_is_focused (LrgShmupTemplate *self)
{
    LrgShmupTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHMUP_TEMPLATE (self), FALSE);

    priv = lrg_shmup_template_get_instance_private (self);
    return priv->is_focused;
}

void
lrg_shmup_template_set_focused (LrgShmupTemplate *self,
                                 gboolean          focused)
{
    LrgShmupTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHMUP_TEMPLATE (self));

    priv = lrg_shmup_template_get_instance_private (self);

    priv->is_focused = focused;
    priv->show_hitbox = focused;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOCUSED]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_HITBOX]);
}
