/* lrg-shooter-2d-template.h - 2D shooter game template
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * Base template for 2D shooter games.
 *
 * This template extends LrgGame2DTemplate with shooter-specific features:
 * - Projectile spawning and management
 * - Fire rate and cooldown handling
 * - Multiple weapon slots
 * - Basic collision detection for projectiles
 * - Score tracking
 *
 * Subclass this template for shoot-em-ups, twin-stick shooters,
 * bullet hell games, and similar genres.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-game-2d-template.h"

G_BEGIN_DECLS

#define LRG_TYPE_SHOOTER_2D_TEMPLATE (lrg_shooter_2d_template_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgShooter2DTemplate, lrg_shooter_2d_template,
                          LRG, SHOOTER_2D_TEMPLATE, LrgGame2DTemplate)

/**
 * LrgShooter2DTemplateClass:
 * @parent_class: parent class
 * @spawn_projectile: spawns a new projectile
 * @update_projectiles: updates all active projectiles
 * @on_projectile_hit: called when a projectile hits something
 * @fire_weapon: fires the current weapon
 * @switch_weapon: switches to a different weapon slot
 * @on_enemy_destroyed: called when an enemy is destroyed
 *
 * Class structure for #LrgShooter2DTemplate.
 *
 * Subclasses should override the virtual methods to customize
 * projectile behavior, weapon handling, and enemy destruction.
 */
struct _LrgShooter2DTemplateClass
{
    LrgGame2DTemplateClass parent_class;

    /*< public >*/

    /**
     * LrgShooter2DTemplateClass::spawn_projectile:
     * @self: a #LrgShooter2DTemplate
     * @x: spawn X position in world coordinates
     * @y: spawn Y position in world coordinates
     * @direction_x: projectile direction X component (normalized)
     * @direction_y: projectile direction Y component (normalized)
     * @speed: projectile speed in units per second
     * @owner_id: identifier for the entity that fired this projectile
     *
     * Spawns a new projectile at the specified position.
     *
     * The default implementation creates a simple projectile and
     * adds it to the projectile pool. Override for custom projectile
     * types or spawning patterns.
     *
     * Returns: %TRUE if the projectile was spawned successfully
     */
    gboolean (*spawn_projectile) (LrgShooter2DTemplate *self,
                                  gfloat                x,
                                  gfloat                y,
                                  gfloat                direction_x,
                                  gfloat                direction_y,
                                  gfloat                speed,
                                  guint                 owner_id);

    /**
     * LrgShooter2DTemplateClass::update_projectiles:
     * @self: a #LrgShooter2DTemplate
     * @delta: time since last frame in seconds
     *
     * Updates all active projectiles.
     *
     * The default implementation moves projectiles based on their
     * velocity and removes those that exit the play area.
     * Override for custom projectile behavior.
     */
    void (*update_projectiles) (LrgShooter2DTemplate *self,
                                gdouble               delta);

    /**
     * LrgShooter2DTemplateClass::on_projectile_hit:
     * @self: a #LrgShooter2DTemplate
     * @projectile_id: identifier of the projectile that hit
     * @target_id: identifier of the target that was hit
     * @x: collision X position
     * @y: collision Y position
     *
     * Called when a projectile collides with a target.
     *
     * Override to implement damage, effects, or other hit reactions.
     */
    void (*on_projectile_hit) (LrgShooter2DTemplate *self,
                               guint                 projectile_id,
                               guint                 target_id,
                               gfloat                x,
                               gfloat                y);

    /**
     * LrgShooter2DTemplateClass::fire_weapon:
     * @self: a #LrgShooter2DTemplate
     *
     * Fires the currently equipped weapon.
     *
     * The default implementation checks the fire cooldown and
     * calls spawn_projectile if ready to fire.
     *
     * Returns: %TRUE if the weapon fired, %FALSE if on cooldown
     */
    gboolean (*fire_weapon) (LrgShooter2DTemplate *self);

    /**
     * LrgShooter2DTemplateClass::switch_weapon:
     * @self: a #LrgShooter2DTemplate
     * @slot: weapon slot index (0-based)
     *
     * Switches to a different weapon slot.
     *
     * Override to add weapon switch animations or delays.
     *
     * Returns: %TRUE if the switch was successful
     */
    gboolean (*switch_weapon) (LrgShooter2DTemplate *self,
                               guint                 slot);

    /**
     * LrgShooter2DTemplateClass::on_enemy_destroyed:
     * @self: a #LrgShooter2DTemplate
     * @enemy_id: identifier of the destroyed enemy
     * @x: enemy X position at destruction
     * @y: enemy Y position at destruction
     * @points: score points awarded
     *
     * Called when an enemy is destroyed.
     *
     * Override to spawn pickups, play effects, or add multipliers.
     */
    void (*on_enemy_destroyed) (LrgShooter2DTemplate *self,
                                guint                 enemy_id,
                                gfloat                x,
                                gfloat                y,
                                gint64                points);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Constructor
 * ========================================================================== */

/**
 * lrg_shooter_2d_template_new:
 *
 * Creates a new 2D shooter template with default settings.
 *
 * Returns: (transfer full): a new #LrgShooter2DTemplate
 */
LRG_AVAILABLE_IN_ALL
LrgShooter2DTemplate * lrg_shooter_2d_template_new (void);

/* ==========================================================================
 * Player Position
 * ========================================================================== */

/**
 * lrg_shooter_2d_template_get_player_position:
 * @self: a #LrgShooter2DTemplate
 * @x: (out) (optional): location for player X position
 * @y: (out) (optional): location for player Y position
 *
 * Gets the current player position in world coordinates.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shooter_2d_template_get_player_position (LrgShooter2DTemplate *self,
                                                   gfloat               *x,
                                                   gfloat               *y);

/**
 * lrg_shooter_2d_template_set_player_position:
 * @self: a #LrgShooter2DTemplate
 * @x: player X position in world coordinates
 * @y: player Y position in world coordinates
 *
 * Sets the player position in world coordinates.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shooter_2d_template_set_player_position (LrgShooter2DTemplate *self,
                                                   gfloat                x,
                                                   gfloat                y);

/* ==========================================================================
 * Fire Rate & Cooldown
 * ========================================================================== */

/**
 * lrg_shooter_2d_template_get_fire_rate:
 * @self: a #LrgShooter2DTemplate
 *
 * Gets the fire rate in shots per second.
 *
 * Returns: the fire rate
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shooter_2d_template_get_fire_rate (LrgShooter2DTemplate *self);

/**
 * lrg_shooter_2d_template_set_fire_rate:
 * @self: a #LrgShooter2DTemplate
 * @rate: fire rate in shots per second (minimum 0.1)
 *
 * Sets the fire rate in shots per second.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shooter_2d_template_set_fire_rate (LrgShooter2DTemplate *self,
                                             gfloat                rate);

/**
 * lrg_shooter_2d_template_get_fire_cooldown:
 * @self: a #LrgShooter2DTemplate
 *
 * Gets the remaining fire cooldown time.
 *
 * Returns: remaining cooldown in seconds (0.0 if ready to fire)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shooter_2d_template_get_fire_cooldown (LrgShooter2DTemplate *self);

/**
 * lrg_shooter_2d_template_can_fire:
 * @self: a #LrgShooter2DTemplate
 *
 * Checks if the weapon is ready to fire.
 *
 * Returns: %TRUE if the weapon can fire
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shooter_2d_template_can_fire (LrgShooter2DTemplate *self);

/* ==========================================================================
 * Projectile Settings
 * ========================================================================== */

/**
 * lrg_shooter_2d_template_get_projectile_speed:
 * @self: a #LrgShooter2DTemplate
 *
 * Gets the default projectile speed.
 *
 * Returns: projectile speed in units per second
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shooter_2d_template_get_projectile_speed (LrgShooter2DTemplate *self);

/**
 * lrg_shooter_2d_template_set_projectile_speed:
 * @self: a #LrgShooter2DTemplate
 * @speed: projectile speed in units per second (minimum 1.0)
 *
 * Sets the default projectile speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shooter_2d_template_set_projectile_speed (LrgShooter2DTemplate *self,
                                                    gfloat                speed);

/**
 * lrg_shooter_2d_template_get_max_projectiles:
 * @self: a #LrgShooter2DTemplate
 *
 * Gets the maximum number of simultaneous projectiles.
 *
 * Returns: maximum projectile count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_shooter_2d_template_get_max_projectiles (LrgShooter2DTemplate *self);

/**
 * lrg_shooter_2d_template_set_max_projectiles:
 * @self: a #LrgShooter2DTemplate
 * @max: maximum projectile count (minimum 1)
 *
 * Sets the maximum number of simultaneous projectiles.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shooter_2d_template_set_max_projectiles (LrgShooter2DTemplate *self,
                                                   guint                 max);

/**
 * lrg_shooter_2d_template_get_active_projectile_count:
 * @self: a #LrgShooter2DTemplate
 *
 * Gets the current number of active projectiles.
 *
 * Returns: number of active projectiles
 */
LRG_AVAILABLE_IN_ALL
guint lrg_shooter_2d_template_get_active_projectile_count (LrgShooter2DTemplate *self);

/* ==========================================================================
 * Weapons
 * ========================================================================== */

/**
 * lrg_shooter_2d_template_get_current_weapon:
 * @self: a #LrgShooter2DTemplate
 *
 * Gets the currently equipped weapon slot index.
 *
 * Returns: current weapon slot (0-based)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_shooter_2d_template_get_current_weapon (LrgShooter2DTemplate *self);

/**
 * lrg_shooter_2d_template_get_weapon_count:
 * @self: a #LrgShooter2DTemplate
 *
 * Gets the number of available weapon slots.
 *
 * Returns: number of weapon slots
 */
LRG_AVAILABLE_IN_ALL
guint lrg_shooter_2d_template_get_weapon_count (LrgShooter2DTemplate *self);

/**
 * lrg_shooter_2d_template_set_weapon_count:
 * @self: a #LrgShooter2DTemplate
 * @count: number of weapon slots (minimum 1)
 *
 * Sets the number of available weapon slots.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shooter_2d_template_set_weapon_count (LrgShooter2DTemplate *self,
                                                guint                 count);

/* ==========================================================================
 * Score
 * ========================================================================== */

/**
 * lrg_shooter_2d_template_get_score:
 * @self: a #LrgShooter2DTemplate
 *
 * Gets the current score.
 *
 * Returns: the current score
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_shooter_2d_template_get_score (LrgShooter2DTemplate *self);

/**
 * lrg_shooter_2d_template_set_score:
 * @self: a #LrgShooter2DTemplate
 * @score: the score to set
 *
 * Sets the current score.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shooter_2d_template_set_score (LrgShooter2DTemplate *self,
                                         gint64                score);

/**
 * lrg_shooter_2d_template_add_score:
 * @self: a #LrgShooter2DTemplate
 * @points: points to add (can be negative)
 *
 * Adds points to the current score.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shooter_2d_template_add_score (LrgShooter2DTemplate *self,
                                         gint64                points);

/**
 * lrg_shooter_2d_template_get_high_score:
 * @self: a #LrgShooter2DTemplate
 *
 * Gets the high score.
 *
 * Returns: the high score
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_shooter_2d_template_get_high_score (LrgShooter2DTemplate *self);

/* ==========================================================================
 * Multiplier
 * ========================================================================== */

/**
 * lrg_shooter_2d_template_get_score_multiplier:
 * @self: a #LrgShooter2DTemplate
 *
 * Gets the current score multiplier.
 *
 * Returns: the score multiplier (1.0 = normal)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shooter_2d_template_get_score_multiplier (LrgShooter2DTemplate *self);

/**
 * lrg_shooter_2d_template_set_score_multiplier:
 * @self: a #LrgShooter2DTemplate
 * @multiplier: the score multiplier (minimum 0.0)
 *
 * Sets the score multiplier applied to all points earned.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shooter_2d_template_set_score_multiplier (LrgShooter2DTemplate *self,
                                                    gfloat                multiplier);

/* ==========================================================================
 * Play Area Bounds
 * ========================================================================== */

/**
 * lrg_shooter_2d_template_set_play_area:
 * @self: a #LrgShooter2DTemplate
 * @min_x: minimum X coordinate
 * @min_y: minimum Y coordinate
 * @max_x: maximum X coordinate
 * @max_y: maximum Y coordinate
 *
 * Sets the play area bounds.
 *
 * Projectiles outside these bounds are automatically removed.
 * Player movement is constrained to these bounds.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shooter_2d_template_set_play_area (LrgShooter2DTemplate *self,
                                             gfloat                min_x,
                                             gfloat                min_y,
                                             gfloat                max_x,
                                             gfloat                max_y);

/**
 * lrg_shooter_2d_template_get_play_area:
 * @self: a #LrgShooter2DTemplate
 * @min_x: (out) (optional): location for minimum X
 * @min_y: (out) (optional): location for minimum Y
 * @max_x: (out) (optional): location for maximum X
 * @max_y: (out) (optional): location for maximum Y
 *
 * Gets the play area bounds.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shooter_2d_template_get_play_area (LrgShooter2DTemplate *self,
                                             gfloat               *min_x,
                                             gfloat               *min_y,
                                             gfloat               *max_x,
                                             gfloat               *max_y);

/* ==========================================================================
 * Utility Functions
 * ========================================================================== */

/**
 * lrg_shooter_2d_template_fire:
 * @self: a #LrgShooter2DTemplate
 *
 * Attempts to fire the current weapon.
 *
 * This is a convenience function that calls the fire_weapon virtual.
 *
 * Returns: %TRUE if the weapon fired
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shooter_2d_template_fire (LrgShooter2DTemplate *self);

/**
 * lrg_shooter_2d_template_clear_projectiles:
 * @self: a #LrgShooter2DTemplate
 *
 * Removes all active projectiles.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shooter_2d_template_clear_projectiles (LrgShooter2DTemplate *self);

G_END_DECLS
