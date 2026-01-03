/* lrg-shooter-2d-template-private.h - Private data for 2D shooter template
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#pragma once

#include "lrg-shooter-2d-template.h"
#include <graylib.h>

G_BEGIN_DECLS

/* ==========================================================================
 * Internal Structures
 * ========================================================================== */

/**
 * LrgProjectile2D:
 *
 * Internal representation of a 2D projectile.
 */
typedef struct _LrgProjectile2D
{
    guint    id;
    gboolean active;
    gfloat   x;
    gfloat   y;
    gfloat   velocity_x;
    gfloat   velocity_y;
    gfloat   speed;
    gfloat   lifetime;
    gfloat   max_lifetime;
    guint    owner_id;
} LrgProjectile2D;

/* ==========================================================================
 * Private Data Structure
 * ========================================================================== */

/**
 * LrgShooter2DTemplatePrivate:
 *
 * Private instance data for #LrgShooter2DTemplate.
 */
typedef struct _LrgShooter2DTemplatePrivate
{
    /* Player state */
    gfloat player_x;
    gfloat player_y;
    gfloat player_speed;

    /* Fire rate */
    gfloat fire_rate;          /* Shots per second */
    gfloat fire_cooldown;      /* Remaining cooldown */

    /* Projectile settings */
    gfloat projectile_speed;
    guint  max_projectiles;
    gfloat projectile_lifetime;

    /* Projectile pool */
    LrgProjectile2D *projectiles;
    guint            projectile_count;
    guint            next_projectile_id;

    /* Weapons */
    guint current_weapon;
    guint weapon_count;

    /* Score */
    gint64 score;
    gint64 high_score;
    gfloat score_multiplier;

    /* Play area bounds */
    gfloat play_area_min_x;
    gfloat play_area_min_y;
    gfloat play_area_max_x;
    gfloat play_area_max_y;

    /* Auto-fire */
    gboolean auto_fire;

} LrgShooter2DTemplatePrivate;

/* ==========================================================================
 * Private Functions (for subclass use)
 * ========================================================================== */

/**
 * lrg_shooter_2d_template_get_private:
 * @self: a #LrgShooter2DTemplate
 *
 * Gets the private data for the template.
 *
 * For use by subclasses only.
 *
 * Returns: (transfer none): the private data
 */
LrgShooter2DTemplatePrivate *
lrg_shooter_2d_template_get_private (LrgShooter2DTemplate *self);

/**
 * lrg_shooter_2d_template_get_projectile:
 * @self: a #LrgShooter2DTemplate
 * @index: projectile index
 *
 * Gets a projectile by index.
 *
 * For use by subclasses only.
 *
 * Returns: (transfer none) (nullable): the projectile, or %NULL if invalid index
 */
LrgProjectile2D *
lrg_shooter_2d_template_get_projectile (LrgShooter2DTemplate *self,
                                         guint                 index);

/* ==========================================================================
 * Default Constants
 * ========================================================================== */

/* Default fire rate (shots per second) */
#define LRG_SHOOTER_2D_DEFAULT_FIRE_RATE        5.0f

/* Default projectile speed (units per second) */
#define LRG_SHOOTER_2D_DEFAULT_PROJECTILE_SPEED 500.0f

/* Default maximum projectiles */
#define LRG_SHOOTER_2D_DEFAULT_MAX_PROJECTILES  100

/* Default projectile lifetime (seconds) */
#define LRG_SHOOTER_2D_DEFAULT_PROJECTILE_LIFETIME 5.0f

/* Default player speed (units per second) */
#define LRG_SHOOTER_2D_DEFAULT_PLAYER_SPEED     300.0f

/* Default weapon count */
#define LRG_SHOOTER_2D_DEFAULT_WEAPON_COUNT     1

/* Minimum fire rate */
#define LRG_SHOOTER_2D_MIN_FIRE_RATE            0.1f

G_END_DECLS
