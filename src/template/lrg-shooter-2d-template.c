/* lrg-shooter-2d-template.c - 2D shooter game template implementation
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "../config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TEMPLATE

#include "lrg-shooter-2d-template.h"
#include "lrg-shooter-2d-template-private.h"
#include "../lrg-log.h"

#include <graylib.h>
#include <math.h>
#include <string.h>

/* ==========================================================================
 * Type Definition
 * ========================================================================== */

typedef struct _LrgShooter2DTemplatePrivate LrgShooter2DTemplatePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgShooter2DTemplate, lrg_shooter_2d_template,
                            LRG_TYPE_GAME_2D_TEMPLATE)

/* ==========================================================================
 * Property IDs
 * ========================================================================== */

enum
{
    PROP_0,
    PROP_PLAYER_X,
    PROP_PLAYER_Y,
    PROP_PLAYER_SPEED,
    PROP_FIRE_RATE,
    PROP_PROJECTILE_SPEED,
    PROP_MAX_PROJECTILES,
    PROP_CURRENT_WEAPON,
    PROP_WEAPON_COUNT,
    PROP_SCORE,
    PROP_HIGH_SCORE,
    PROP_SCORE_MULTIPLIER,
    PROP_AUTO_FIRE,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL };

/* ==========================================================================
 * Signal IDs
 * ========================================================================== */

enum
{
    SIGNAL_PROJECTILE_SPAWNED,
    SIGNAL_PROJECTILE_HIT,
    SIGNAL_ENEMY_DESTROYED,
    SIGNAL_SCORE_CHANGED,
    SIGNAL_WEAPON_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

/* ==========================================================================
 * Private Function Declarations
 * ========================================================================== */

static void lrg_shooter_2d_template_pre_update (LrgGameTemplate *template,
                                                 gdouble          delta);
static void lrg_shooter_2d_template_draw_world (LrgGame2DTemplate *template);

/* ==========================================================================
 * Private Data Access
 * ========================================================================== */

LrgShooter2DTemplatePrivate *
lrg_shooter_2d_template_get_private (LrgShooter2DTemplate *self)
{
    return lrg_shooter_2d_template_get_instance_private (self);
}

LrgProjectile2D *
lrg_shooter_2d_template_get_projectile (LrgShooter2DTemplate *self,
                                         guint                 index)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self), NULL);

    priv = lrg_shooter_2d_template_get_private (self);

    if (index >= priv->max_projectiles)
        return NULL;

    return &priv->projectiles[index];
}

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static gboolean
lrg_shooter_2d_template_real_spawn_projectile (LrgShooter2DTemplate *self,
                                                gfloat                x,
                                                gfloat                y,
                                                gfloat                direction_x,
                                                gfloat                direction_y,
                                                gfloat                speed,
                                                guint                 owner_id)
{
    LrgShooter2DTemplatePrivate *priv;
    LrgProjectile2D *projectile;
    guint i;
    gfloat length;

    priv = lrg_shooter_2d_template_get_private (self);

    /* Find an inactive projectile slot */
    projectile = NULL;
    for (i = 0; i < priv->max_projectiles; i++)
    {
        if (!priv->projectiles[i].active)
        {
            projectile = &priv->projectiles[i];
            break;
        }
    }

    if (projectile == NULL)
    {
        lrg_debug (LRG_LOG_DOMAIN, "Max projectiles reached");
        return FALSE;
    }

    /* Normalize direction */
    length = sqrtf (direction_x * direction_x + direction_y * direction_y);
    if (length > 0.0001f)
    {
        direction_x /= length;
        direction_y /= length;
    }

    /* Initialize projectile */
    projectile->id = priv->next_projectile_id++;
    projectile->active = TRUE;
    projectile->x = x;
    projectile->y = y;
    projectile->velocity_x = direction_x * speed;
    projectile->velocity_y = direction_y * speed;
    projectile->speed = speed;
    projectile->lifetime = 0.0f;
    projectile->max_lifetime = priv->projectile_lifetime;
    projectile->owner_id = owner_id;

    priv->projectile_count++;

    g_signal_emit (self, signals[SIGNAL_PROJECTILE_SPAWNED], 0,
                   projectile->id, x, y, direction_x, direction_y);

    return TRUE;
}

static void
lrg_shooter_2d_template_real_update_projectiles (LrgShooter2DTemplate *self,
                                                  gdouble               delta)
{
    LrgShooter2DTemplatePrivate *priv;
    guint i;
    gfloat delta_f;

    priv = lrg_shooter_2d_template_get_private (self);
    delta_f = (gfloat) delta;

    for (i = 0; i < priv->max_projectiles; i++)
    {
        LrgProjectile2D *p;

        p = &priv->projectiles[i];
        if (!p->active)
            continue;

        /* Update position */
        p->x += p->velocity_x * delta_f;
        p->y += p->velocity_y * delta_f;
        p->lifetime += delta_f;

        /* Check if projectile should be removed */
        if (p->lifetime >= p->max_lifetime ||
            p->x < priv->play_area_min_x ||
            p->x > priv->play_area_max_x ||
            p->y < priv->play_area_min_y ||
            p->y > priv->play_area_max_y)
        {
            p->active = FALSE;
            priv->projectile_count--;
        }
    }
}

static void
lrg_shooter_2d_template_real_on_projectile_hit (LrgShooter2DTemplate *self,
                                                 guint                 projectile_id,
                                                 guint                 target_id,
                                                 gfloat                x,
                                                 gfloat                y)
{
    /* Default: emit signal, subclasses can override */
    g_signal_emit (self, signals[SIGNAL_PROJECTILE_HIT], 0,
                   projectile_id, target_id, x, y);
}

static gboolean
lrg_shooter_2d_template_real_fire_weapon (LrgShooter2DTemplate *self)
{
    LrgShooter2DTemplatePrivate *priv;
    LrgShooter2DTemplateClass *klass;

    priv = lrg_shooter_2d_template_get_private (self);
    klass = LRG_SHOOTER_2D_TEMPLATE_GET_CLASS (self);

    /* Check cooldown */
    if (priv->fire_cooldown > 0.0f)
        return FALSE;

    /* Spawn projectile */
    if (klass->spawn_projectile != NULL)
    {
        gboolean spawned;

        /* Fire upward by default (direction: 0, -1) */
        spawned = klass->spawn_projectile (self,
                                           priv->player_x,
                                           priv->player_y,
                                           0.0f, -1.0f,
                                           priv->projectile_speed,
                                           0); /* owner_id 0 = player */

        if (spawned)
        {
            /* Reset cooldown */
            priv->fire_cooldown = 1.0f / priv->fire_rate;
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
lrg_shooter_2d_template_real_switch_weapon (LrgShooter2DTemplate *self,
                                             guint                 slot)
{
    LrgShooter2DTemplatePrivate *priv;

    priv = lrg_shooter_2d_template_get_private (self);

    if (slot >= priv->weapon_count)
        return FALSE;

    if (slot == priv->current_weapon)
        return TRUE;

    priv->current_weapon = slot;

    g_signal_emit (self, signals[SIGNAL_WEAPON_CHANGED], 0, slot);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_WEAPON]);

    return TRUE;
}

static void
lrg_shooter_2d_template_real_on_enemy_destroyed (LrgShooter2DTemplate *self,
                                                  guint                 enemy_id,
                                                  gfloat                x,
                                                  gfloat                y,
                                                  gint64                points)
{
    LrgShooter2DTemplatePrivate *priv;
    gint64 actual_points;

    priv = lrg_shooter_2d_template_get_private (self);

    /* Apply multiplier */
    actual_points = (gint64) (points * priv->score_multiplier);
    priv->score += actual_points;

    /* Update high score */
    if (priv->score > priv->high_score)
        priv->high_score = priv->score;

    g_signal_emit (self, signals[SIGNAL_ENEMY_DESTROYED], 0,
                   enemy_id, x, y, actual_points);
    g_signal_emit (self, signals[SIGNAL_SCORE_CHANGED], 0, priv->score);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCORE]);
}

/* ==========================================================================
 * LrgGameTemplate Overrides
 * ========================================================================== */

static void
lrg_shooter_2d_template_pre_update (LrgGameTemplate *template,
                                     gdouble          delta)
{
    LrgShooter2DTemplate *self;
    LrgShooter2DTemplatePrivate *priv;
    LrgShooter2DTemplateClass *klass;
    LrgGameTemplateClass *parent_class;

    self = LRG_SHOOTER_2D_TEMPLATE (template);
    priv = lrg_shooter_2d_template_get_private (self);
    klass = LRG_SHOOTER_2D_TEMPLATE_GET_CLASS (self);

    /* Chain up first */
    parent_class = LRG_GAME_TEMPLATE_CLASS (lrg_shooter_2d_template_parent_class);
    if (parent_class->pre_update != NULL)
        parent_class->pre_update (template, delta);

    /* Update fire cooldown */
    if (priv->fire_cooldown > 0.0f)
    {
        priv->fire_cooldown -= (gfloat) delta;
        if (priv->fire_cooldown < 0.0f)
            priv->fire_cooldown = 0.0f;
    }

    /* Update projectiles */
    if (klass->update_projectiles != NULL)
        klass->update_projectiles (self, delta);

    /* Auto-fire if enabled */
    if (priv->auto_fire && klass->fire_weapon != NULL)
        klass->fire_weapon (self);
}

/* ==========================================================================
 * LrgGame2DTemplate Overrides
 * ========================================================================== */

static void
lrg_shooter_2d_template_draw_world (LrgGame2DTemplate *template)
{
    LrgShooter2DTemplate *self;
    LrgShooter2DTemplatePrivate *priv;
    LrgGame2DTemplateClass *parent_class;
    guint i;

    self = LRG_SHOOTER_2D_TEMPLATE (template);
    priv = lrg_shooter_2d_template_get_private (self);

    /* Chain up first to draw base world content */
    parent_class = LRG_GAME_2D_TEMPLATE_CLASS (lrg_shooter_2d_template_parent_class);
    if (parent_class->draw_world != NULL)
        parent_class->draw_world (template);

    /* Draw projectiles (simple circles for now, subclasses can override) */
    for (i = 0; i < priv->max_projectiles; i++)
    {
        LrgProjectile2D *p;

        p = &priv->projectiles[i];
        if (!p->active)
            continue;

        {
            g_autoptr(GrlColor) color = grl_color_new (255, 255, 0, 255);
            grl_draw_circle ((gint) p->x, (gint) p->y, 4.0f, color);
        }
    }

    /* Draw player (simple rectangle for now) */
    {
        g_autoptr(GrlColor) player_color = grl_color_new (0, 255, 0, 255);
        grl_draw_rectangle ((gint) (priv->player_x - 16),
                            (gint) (priv->player_y - 16),
                            32, 32, player_color);
    }
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_shooter_2d_template_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
    LrgShooter2DTemplate *self;
    LrgShooter2DTemplatePrivate *priv;

    self = LRG_SHOOTER_2D_TEMPLATE (object);
    priv = lrg_shooter_2d_template_get_private (self);

    switch (prop_id)
    {
        case PROP_PLAYER_X:
            priv->player_x = g_value_get_float (value);
            break;

        case PROP_PLAYER_Y:
            priv->player_y = g_value_get_float (value);
            break;

        case PROP_PLAYER_SPEED:
            priv->player_speed = g_value_get_float (value);
            break;

        case PROP_FIRE_RATE:
            lrg_shooter_2d_template_set_fire_rate (self, g_value_get_float (value));
            break;

        case PROP_PROJECTILE_SPEED:
            lrg_shooter_2d_template_set_projectile_speed (self, g_value_get_float (value));
            break;

        case PROP_MAX_PROJECTILES:
            lrg_shooter_2d_template_set_max_projectiles (self, g_value_get_uint (value));
            break;

        case PROP_CURRENT_WEAPON:
            lrg_shooter_2d_template_real_switch_weapon (self, g_value_get_uint (value));
            break;

        case PROP_WEAPON_COUNT:
            lrg_shooter_2d_template_set_weapon_count (self, g_value_get_uint (value));
            break;

        case PROP_SCORE:
            lrg_shooter_2d_template_set_score (self, g_value_get_int64 (value));
            break;

        case PROP_HIGH_SCORE:
            priv->high_score = g_value_get_int64 (value);
            break;

        case PROP_SCORE_MULTIPLIER:
            lrg_shooter_2d_template_set_score_multiplier (self, g_value_get_float (value));
            break;

        case PROP_AUTO_FIRE:
            priv->auto_fire = g_value_get_boolean (value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
lrg_shooter_2d_template_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
    LrgShooter2DTemplate *self;
    LrgShooter2DTemplatePrivate *priv;

    self = LRG_SHOOTER_2D_TEMPLATE (object);
    priv = lrg_shooter_2d_template_get_private (self);

    switch (prop_id)
    {
        case PROP_PLAYER_X:
            g_value_set_float (value, priv->player_x);
            break;

        case PROP_PLAYER_Y:
            g_value_set_float (value, priv->player_y);
            break;

        case PROP_PLAYER_SPEED:
            g_value_set_float (value, priv->player_speed);
            break;

        case PROP_FIRE_RATE:
            g_value_set_float (value, priv->fire_rate);
            break;

        case PROP_PROJECTILE_SPEED:
            g_value_set_float (value, priv->projectile_speed);
            break;

        case PROP_MAX_PROJECTILES:
            g_value_set_uint (value, priv->max_projectiles);
            break;

        case PROP_CURRENT_WEAPON:
            g_value_set_uint (value, priv->current_weapon);
            break;

        case PROP_WEAPON_COUNT:
            g_value_set_uint (value, priv->weapon_count);
            break;

        case PROP_SCORE:
            g_value_set_int64 (value, priv->score);
            break;

        case PROP_HIGH_SCORE:
            g_value_set_int64 (value, priv->high_score);
            break;

        case PROP_SCORE_MULTIPLIER:
            g_value_set_float (value, priv->score_multiplier);
            break;

        case PROP_AUTO_FIRE:
            g_value_set_boolean (value, priv->auto_fire);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
lrg_shooter_2d_template_finalize (GObject *object)
{
    LrgShooter2DTemplate *self;
    LrgShooter2DTemplatePrivate *priv;

    self = LRG_SHOOTER_2D_TEMPLATE (object);
    priv = lrg_shooter_2d_template_get_private (self);

    g_free (priv->projectiles);

    G_OBJECT_CLASS (lrg_shooter_2d_template_parent_class)->finalize (object);
}

static void
lrg_shooter_2d_template_init (LrgShooter2DTemplate *self)
{
    LrgShooter2DTemplatePrivate *priv;
    gint virtual_width;
    gint virtual_height;

    priv = lrg_shooter_2d_template_get_private (self);

    /* Player state */
    priv->player_x = 0.0f;
    priv->player_y = 0.0f;
    priv->player_speed = LRG_SHOOTER_2D_DEFAULT_PLAYER_SPEED;

    /* Fire rate */
    priv->fire_rate = LRG_SHOOTER_2D_DEFAULT_FIRE_RATE;
    priv->fire_cooldown = 0.0f;

    /* Projectile settings */
    priv->projectile_speed = LRG_SHOOTER_2D_DEFAULT_PROJECTILE_SPEED;
    priv->max_projectiles = LRG_SHOOTER_2D_DEFAULT_MAX_PROJECTILES;
    priv->projectile_lifetime = LRG_SHOOTER_2D_DEFAULT_PROJECTILE_LIFETIME;

    /* Allocate projectile pool */
    priv->projectiles = g_new0 (LrgProjectile2D, priv->max_projectiles);
    priv->projectile_count = 0;
    priv->next_projectile_id = 1;

    /* Weapons */
    priv->current_weapon = 0;
    priv->weapon_count = LRG_SHOOTER_2D_DEFAULT_WEAPON_COUNT;

    /* Score */
    priv->score = 0;
    priv->high_score = 0;
    priv->score_multiplier = 1.0f;

    /* Default play area to virtual resolution */
    virtual_width = lrg_game_2d_template_get_virtual_width (LRG_GAME_2D_TEMPLATE (self));
    virtual_height = lrg_game_2d_template_get_virtual_height (LRG_GAME_2D_TEMPLATE (self));

    priv->play_area_min_x = -100.0f;
    priv->play_area_min_y = -100.0f;
    priv->play_area_max_x = (gfloat) virtual_width + 100.0f;
    priv->play_area_max_y = (gfloat) virtual_height + 100.0f;

    /* Auto-fire */
    priv->auto_fire = FALSE;

    /* Center player */
    priv->player_x = (gfloat) virtual_width / 2.0f;
    priv->player_y = (gfloat) virtual_height * 0.8f;
}

static void
lrg_shooter_2d_template_class_init (LrgShooter2DTemplateClass *klass)
{
    GObjectClass *object_class;
    LrgGameTemplateClass *template_class;
    LrgGame2DTemplateClass *template_2d_class;

    object_class = G_OBJECT_CLASS (klass);
    template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    template_2d_class = LRG_GAME_2D_TEMPLATE_CLASS (klass);

    /* GObject overrides */
    object_class->set_property = lrg_shooter_2d_template_set_property;
    object_class->get_property = lrg_shooter_2d_template_get_property;
    object_class->finalize = lrg_shooter_2d_template_finalize;

    /* LrgGameTemplate overrides */
    template_class->pre_update = lrg_shooter_2d_template_pre_update;

    /* LrgGame2DTemplate overrides */
    template_2d_class->draw_world = lrg_shooter_2d_template_draw_world;

    /* Virtual methods */
    klass->spawn_projectile = lrg_shooter_2d_template_real_spawn_projectile;
    klass->update_projectiles = lrg_shooter_2d_template_real_update_projectiles;
    klass->on_projectile_hit = lrg_shooter_2d_template_real_on_projectile_hit;
    klass->fire_weapon = lrg_shooter_2d_template_real_fire_weapon;
    klass->switch_weapon = lrg_shooter_2d_template_real_switch_weapon;
    klass->on_enemy_destroyed = lrg_shooter_2d_template_real_on_enemy_destroyed;

    /* Properties */
    properties[PROP_PLAYER_X] =
        g_param_spec_float ("player-x",
                            "Player X",
                            "Player X position",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_PLAYER_Y] =
        g_param_spec_float ("player-y",
                            "Player Y",
                            "Player Y position",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_PLAYER_SPEED] =
        g_param_spec_float ("player-speed",
                            "Player Speed",
                            "Player movement speed",
                            0.0f, G_MAXFLOAT,
                            LRG_SHOOTER_2D_DEFAULT_PLAYER_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_FIRE_RATE] =
        g_param_spec_float ("fire-rate",
                            "Fire Rate",
                            "Fire rate in shots per second",
                            LRG_SHOOTER_2D_MIN_FIRE_RATE, G_MAXFLOAT,
                            LRG_SHOOTER_2D_DEFAULT_FIRE_RATE,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_PROJECTILE_SPEED] =
        g_param_spec_float ("projectile-speed",
                            "Projectile Speed",
                            "Default projectile speed",
                            1.0f, G_MAXFLOAT,
                            LRG_SHOOTER_2D_DEFAULT_PROJECTILE_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_PROJECTILES] =
        g_param_spec_uint ("max-projectiles",
                           "Max Projectiles",
                           "Maximum simultaneous projectiles",
                           1, G_MAXUINT,
                           LRG_SHOOTER_2D_DEFAULT_MAX_PROJECTILES,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_CURRENT_WEAPON] =
        g_param_spec_uint ("current-weapon",
                           "Current Weapon",
                           "Currently equipped weapon slot",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_WEAPON_COUNT] =
        g_param_spec_uint ("weapon-count",
                           "Weapon Count",
                           "Number of available weapon slots",
                           1, G_MAXUINT,
                           LRG_SHOOTER_2D_DEFAULT_WEAPON_COUNT,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SCORE] =
        g_param_spec_int64 ("score",
                            "Score",
                            "Current score",
                            G_MININT64, G_MAXINT64, 0,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_HIGH_SCORE] =
        g_param_spec_int64 ("high-score",
                            "High Score",
                            "High score",
                            G_MININT64, G_MAXINT64, 0,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SCORE_MULTIPLIER] =
        g_param_spec_float ("score-multiplier",
                            "Score Multiplier",
                            "Score multiplier",
                            0.0f, G_MAXFLOAT, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_AUTO_FIRE] =
        g_param_spec_boolean ("auto-fire",
                              "Auto Fire",
                              "Enable automatic firing",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPERTIES, properties);

    /* Signals */
    /**
     * LrgShooter2DTemplate::projectile-spawned:
     * @self: the template
     * @id: projectile identifier
     * @x: spawn X position
     * @y: spawn Y position
     * @dir_x: direction X component
     * @dir_y: direction Y component
     *
     * Emitted when a projectile is spawned.
     */
    signals[SIGNAL_PROJECTILE_SPAWNED] =
        g_signal_new ("projectile-spawned",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 5,
                      G_TYPE_UINT, G_TYPE_FLOAT, G_TYPE_FLOAT,
                      G_TYPE_FLOAT, G_TYPE_FLOAT);

    /**
     * LrgShooter2DTemplate::projectile-hit:
     * @self: the template
     * @projectile_id: projectile identifier
     * @target_id: target identifier
     * @x: hit X position
     * @y: hit Y position
     *
     * Emitted when a projectile hits a target.
     */
    signals[SIGNAL_PROJECTILE_HIT] =
        g_signal_new ("projectile-hit",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 4,
                      G_TYPE_UINT, G_TYPE_UINT, G_TYPE_FLOAT, G_TYPE_FLOAT);

    /**
     * LrgShooter2DTemplate::enemy-destroyed:
     * @self: the template
     * @enemy_id: enemy identifier
     * @x: enemy X position
     * @y: enemy Y position
     * @points: score points awarded
     *
     * Emitted when an enemy is destroyed.
     */
    signals[SIGNAL_ENEMY_DESTROYED] =
        g_signal_new ("enemy-destroyed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 4,
                      G_TYPE_UINT, G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_INT64);

    /**
     * LrgShooter2DTemplate::score-changed:
     * @self: the template
     * @new_score: the new score
     *
     * Emitted when the score changes.
     */
    signals[SIGNAL_SCORE_CHANGED] =
        g_signal_new ("score-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_INT64);

    /**
     * LrgShooter2DTemplate::weapon-changed:
     * @self: the template
     * @new_slot: the new weapon slot
     *
     * Emitted when the weapon slot changes.
     */
    signals[SIGNAL_WEAPON_CHANGED] =
        g_signal_new ("weapon-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_UINT);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgShooter2DTemplate *
lrg_shooter_2d_template_new (void)
{
    return g_object_new (LRG_TYPE_SHOOTER_2D_TEMPLATE, NULL);
}

void
lrg_shooter_2d_template_get_player_position (LrgShooter2DTemplate *self,
                                              gfloat               *x,
                                              gfloat               *y)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self));

    priv = lrg_shooter_2d_template_get_private (self);

    if (x != NULL)
        *x = priv->player_x;
    if (y != NULL)
        *y = priv->player_y;
}

void
lrg_shooter_2d_template_set_player_position (LrgShooter2DTemplate *self,
                                              gfloat                x,
                                              gfloat                y)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self));

    priv = lrg_shooter_2d_template_get_private (self);

    priv->player_x = x;
    priv->player_y = y;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYER_X]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYER_Y]);
}

gfloat
lrg_shooter_2d_template_get_fire_rate (LrgShooter2DTemplate *self)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self),
                          LRG_SHOOTER_2D_DEFAULT_FIRE_RATE);

    priv = lrg_shooter_2d_template_get_private (self);
    return priv->fire_rate;
}

void
lrg_shooter_2d_template_set_fire_rate (LrgShooter2DTemplate *self,
                                        gfloat                rate)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self));

    priv = lrg_shooter_2d_template_get_private (self);

    if (rate < LRG_SHOOTER_2D_MIN_FIRE_RATE)
        rate = LRG_SHOOTER_2D_MIN_FIRE_RATE;

    priv->fire_rate = rate;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FIRE_RATE]);
}

gfloat
lrg_shooter_2d_template_get_fire_cooldown (LrgShooter2DTemplate *self)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self), 0.0f);

    priv = lrg_shooter_2d_template_get_private (self);
    return priv->fire_cooldown;
}

gboolean
lrg_shooter_2d_template_can_fire (LrgShooter2DTemplate *self)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self), FALSE);

    priv = lrg_shooter_2d_template_get_private (self);
    return priv->fire_cooldown <= 0.0f;
}

gfloat
lrg_shooter_2d_template_get_projectile_speed (LrgShooter2DTemplate *self)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self),
                          LRG_SHOOTER_2D_DEFAULT_PROJECTILE_SPEED);

    priv = lrg_shooter_2d_template_get_private (self);
    return priv->projectile_speed;
}

void
lrg_shooter_2d_template_set_projectile_speed (LrgShooter2DTemplate *self,
                                               gfloat                speed)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self));

    priv = lrg_shooter_2d_template_get_private (self);

    if (speed < 1.0f)
        speed = 1.0f;

    priv->projectile_speed = speed;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROJECTILE_SPEED]);
}

guint
lrg_shooter_2d_template_get_max_projectiles (LrgShooter2DTemplate *self)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self),
                          LRG_SHOOTER_2D_DEFAULT_MAX_PROJECTILES);

    priv = lrg_shooter_2d_template_get_private (self);
    return priv->max_projectiles;
}

void
lrg_shooter_2d_template_set_max_projectiles (LrgShooter2DTemplate *self,
                                              guint                 max)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self));

    if (max < 1)
        max = 1;

    priv = lrg_shooter_2d_template_get_private (self);

    if (max == priv->max_projectiles)
        return;

    /* Reallocate projectile pool */
    g_free (priv->projectiles);
    priv->max_projectiles = max;
    priv->projectiles = g_new0 (LrgProjectile2D, max);
    priv->projectile_count = 0;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_PROJECTILES]);
}

guint
lrg_shooter_2d_template_get_active_projectile_count (LrgShooter2DTemplate *self)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self), 0);

    priv = lrg_shooter_2d_template_get_private (self);
    return priv->projectile_count;
}

guint
lrg_shooter_2d_template_get_current_weapon (LrgShooter2DTemplate *self)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self), 0);

    priv = lrg_shooter_2d_template_get_private (self);
    return priv->current_weapon;
}

guint
lrg_shooter_2d_template_get_weapon_count (LrgShooter2DTemplate *self)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self),
                          LRG_SHOOTER_2D_DEFAULT_WEAPON_COUNT);

    priv = lrg_shooter_2d_template_get_private (self);
    return priv->weapon_count;
}

void
lrg_shooter_2d_template_set_weapon_count (LrgShooter2DTemplate *self,
                                           guint                 count)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self));

    if (count < 1)
        count = 1;

    priv = lrg_shooter_2d_template_get_private (self);
    priv->weapon_count = count;

    /* Clamp current weapon if needed */
    if (priv->current_weapon >= count)
        priv->current_weapon = count - 1;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WEAPON_COUNT]);
}

gint64
lrg_shooter_2d_template_get_score (LrgShooter2DTemplate *self)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self), 0);

    priv = lrg_shooter_2d_template_get_private (self);
    return priv->score;
}

void
lrg_shooter_2d_template_set_score (LrgShooter2DTemplate *self,
                                    gint64                score)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self));

    priv = lrg_shooter_2d_template_get_private (self);
    priv->score = score;

    if (score > priv->high_score)
        priv->high_score = score;

    g_signal_emit (self, signals[SIGNAL_SCORE_CHANGED], 0, score);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCORE]);
}

void
lrg_shooter_2d_template_add_score (LrgShooter2DTemplate *self,
                                    gint64                points)
{
    LrgShooter2DTemplatePrivate *priv;
    gint64 actual_points;

    g_return_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self));

    priv = lrg_shooter_2d_template_get_private (self);

    actual_points = (gint64) (points * priv->score_multiplier);
    lrg_shooter_2d_template_set_score (self, priv->score + actual_points);
}

gint64
lrg_shooter_2d_template_get_high_score (LrgShooter2DTemplate *self)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self), 0);

    priv = lrg_shooter_2d_template_get_private (self);
    return priv->high_score;
}

gfloat
lrg_shooter_2d_template_get_score_multiplier (LrgShooter2DTemplate *self)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self), 1.0f);

    priv = lrg_shooter_2d_template_get_private (self);
    return priv->score_multiplier;
}

void
lrg_shooter_2d_template_set_score_multiplier (LrgShooter2DTemplate *self,
                                               gfloat                multiplier)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self));

    if (multiplier < 0.0f)
        multiplier = 0.0f;

    priv = lrg_shooter_2d_template_get_private (self);
    priv->score_multiplier = multiplier;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCORE_MULTIPLIER]);
}

void
lrg_shooter_2d_template_set_play_area (LrgShooter2DTemplate *self,
                                        gfloat                min_x,
                                        gfloat                min_y,
                                        gfloat                max_x,
                                        gfloat                max_y)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self));

    priv = lrg_shooter_2d_template_get_private (self);

    priv->play_area_min_x = min_x;
    priv->play_area_min_y = min_y;
    priv->play_area_max_x = max_x;
    priv->play_area_max_y = max_y;
}

void
lrg_shooter_2d_template_get_play_area (LrgShooter2DTemplate *self,
                                        gfloat               *min_x,
                                        gfloat               *min_y,
                                        gfloat               *max_x,
                                        gfloat               *max_y)
{
    LrgShooter2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self));

    priv = lrg_shooter_2d_template_get_private (self);

    if (min_x != NULL)
        *min_x = priv->play_area_min_x;
    if (min_y != NULL)
        *min_y = priv->play_area_min_y;
    if (max_x != NULL)
        *max_x = priv->play_area_max_x;
    if (max_y != NULL)
        *max_y = priv->play_area_max_y;
}

gboolean
lrg_shooter_2d_template_fire (LrgShooter2DTemplate *self)
{
    LrgShooter2DTemplateClass *klass;

    g_return_val_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self), FALSE);

    klass = LRG_SHOOTER_2D_TEMPLATE_GET_CLASS (self);

    if (klass->fire_weapon != NULL)
        return klass->fire_weapon (self);

    return FALSE;
}

void
lrg_shooter_2d_template_clear_projectiles (LrgShooter2DTemplate *self)
{
    LrgShooter2DTemplatePrivate *priv;
    guint i;

    g_return_if_fail (LRG_IS_SHOOTER_2D_TEMPLATE (self));

    priv = lrg_shooter_2d_template_get_private (self);

    for (i = 0; i < priv->max_projectiles; i++)
        priv->projectiles[i].active = FALSE;

    priv->projectile_count = 0;
}
