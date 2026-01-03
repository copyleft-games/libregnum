/* lrg-platformer-template.h - Platformer game template
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * Template for 2D platformer games.
 *
 * This template extends LrgGame2DTemplate with platformer-specific features:
 * - Gravity and physics-based movement
 * - Jumping with variable height (tap vs hold)
 * - Coyote time (jump grace period after leaving ledge)
 * - Jump buffering (pre-emptive jump input)
 * - Wall slide and wall jump
 * - Ground detection and landing
 *
 * Use for games like Mario, Celeste, Hollow Knight, or any 2D platformer.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-game-2d-template.h"

G_BEGIN_DECLS

#define LRG_TYPE_PLATFORMER_TEMPLATE (lrg_platformer_template_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgPlatformerTemplate, lrg_platformer_template,
                          LRG, PLATFORMER_TEMPLATE, LrgGame2DTemplate)

/**
 * LrgPlatformerTemplateClass:
 * @parent_class: parent class
 * @on_landed: called when the player lands on ground
 * @on_jump: called when the player starts a jump
 * @on_wall_slide: called when the player begins wall sliding
 * @on_wall_jump: called when the player wall jumps
 * @update_physics: updates player physics each frame
 * @check_ground: checks if player is grounded
 * @check_wall: checks if player is touching a wall
 *
 * Class structure for #LrgPlatformerTemplate.
 */
struct _LrgPlatformerTemplateClass
{
    LrgGame2DTemplateClass parent_class;

    /*< public >*/

    /**
     * LrgPlatformerTemplateClass::on_landed:
     * @self: a #LrgPlatformerTemplate
     *
     * Called when the player lands on the ground.
     *
     * Override to play landing sounds or effects.
     */
    void (*on_landed) (LrgPlatformerTemplate *self);

    /**
     * LrgPlatformerTemplateClass::on_jump:
     * @self: a #LrgPlatformerTemplate
     *
     * Called when the player starts a jump.
     *
     * Override to play jump sounds or effects.
     */
    void (*on_jump) (LrgPlatformerTemplate *self);

    /**
     * LrgPlatformerTemplateClass::on_wall_slide:
     * @self: a #LrgPlatformerTemplate
     *
     * Called when the player begins wall sliding.
     */
    void (*on_wall_slide) (LrgPlatformerTemplate *self);

    /**
     * LrgPlatformerTemplateClass::on_wall_jump:
     * @self: a #LrgPlatformerTemplate
     * @direction: 1 for right, -1 for left
     *
     * Called when the player performs a wall jump.
     */
    void (*on_wall_jump) (LrgPlatformerTemplate *self,
                          gint                   direction);

    /**
     * LrgPlatformerTemplateClass::update_physics:
     * @self: a #LrgPlatformerTemplate
     * @delta: time since last frame in seconds
     *
     * Updates player physics including gravity and movement.
     *
     * The default implementation applies gravity, velocity, and
     * handles basic collision. Override for custom physics.
     */
    void (*update_physics) (LrgPlatformerTemplate *self,
                            gdouble                delta);

    /**
     * LrgPlatformerTemplateClass::check_ground:
     * @self: a #LrgPlatformerTemplate
     *
     * Checks if the player is on the ground.
     *
     * Override to implement custom ground detection.
     *
     * Returns: %TRUE if grounded
     */
    gboolean (*check_ground) (LrgPlatformerTemplate *self);

    /**
     * LrgPlatformerTemplateClass::check_wall:
     * @self: a #LrgPlatformerTemplate
     * @direction: direction to check (1 = right, -1 = left)
     *
     * Checks if the player is touching a wall.
     *
     * Override to implement custom wall detection.
     *
     * Returns: %TRUE if touching wall
     */
    gboolean (*check_wall) (LrgPlatformerTemplate *self,
                            gint                   direction);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Constructor
 * ========================================================================== */

/**
 * lrg_platformer_template_new:
 *
 * Creates a new platformer template with default settings.
 *
 * Returns: (transfer full): a new #LrgPlatformerTemplate
 */
LRG_AVAILABLE_IN_ALL
LrgPlatformerTemplate * lrg_platformer_template_new (void);

/* ==========================================================================
 * Player Position & Velocity
 * ========================================================================== */

/**
 * lrg_platformer_template_get_player_position:
 * @self: a #LrgPlatformerTemplate
 * @x: (out) (optional): location for player X position
 * @y: (out) (optional): location for player Y position
 *
 * Gets the current player position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_platformer_template_get_player_position (LrgPlatformerTemplate *self,
                                                   gfloat                *x,
                                                   gfloat                *y);

/**
 * lrg_platformer_template_set_player_position:
 * @self: a #LrgPlatformerTemplate
 * @x: player X position
 * @y: player Y position
 *
 * Sets the player position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_platformer_template_set_player_position (LrgPlatformerTemplate *self,
                                                   gfloat                 x,
                                                   gfloat                 y);

/**
 * lrg_platformer_template_get_velocity:
 * @self: a #LrgPlatformerTemplate
 * @vx: (out) (optional): location for X velocity
 * @vy: (out) (optional): location for Y velocity
 *
 * Gets the current player velocity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_platformer_template_get_velocity (LrgPlatformerTemplate *self,
                                            gfloat                *vx,
                                            gfloat                *vy);

/**
 * lrg_platformer_template_set_velocity:
 * @self: a #LrgPlatformerTemplate
 * @vx: X velocity
 * @vy: Y velocity
 *
 * Sets the player velocity directly.
 */
LRG_AVAILABLE_IN_ALL
void lrg_platformer_template_set_velocity (LrgPlatformerTemplate *self,
                                            gfloat                 vx,
                                            gfloat                 vy);

/* ==========================================================================
 * Movement Properties
 * ========================================================================== */

/**
 * lrg_platformer_template_get_move_speed:
 * @self: a #LrgPlatformerTemplate
 *
 * Gets the horizontal movement speed.
 *
 * Returns: movement speed in units per second
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_platformer_template_get_move_speed (LrgPlatformerTemplate *self);

/**
 * lrg_platformer_template_set_move_speed:
 * @self: a #LrgPlatformerTemplate
 * @speed: movement speed in units per second
 *
 * Sets the horizontal movement speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_platformer_template_set_move_speed (LrgPlatformerTemplate *self,
                                              gfloat                 speed);

/**
 * lrg_platformer_template_get_acceleration:
 * @self: a #LrgPlatformerTemplate
 *
 * Gets the ground acceleration rate.
 *
 * Returns: acceleration in units per second squared
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_platformer_template_get_acceleration (LrgPlatformerTemplate *self);

/**
 * lrg_platformer_template_set_acceleration:
 * @self: a #LrgPlatformerTemplate
 * @accel: acceleration rate
 *
 * Sets the ground acceleration rate.
 */
LRG_AVAILABLE_IN_ALL
void lrg_platformer_template_set_acceleration (LrgPlatformerTemplate *self,
                                                gfloat                 accel);

/**
 * lrg_platformer_template_get_friction:
 * @self: a #LrgPlatformerTemplate
 *
 * Gets the ground friction (deceleration when not moving).
 *
 * Returns: friction value
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_platformer_template_get_friction (LrgPlatformerTemplate *self);

/**
 * lrg_platformer_template_set_friction:
 * @self: a #LrgPlatformerTemplate
 * @friction: friction value
 *
 * Sets the ground friction.
 */
LRG_AVAILABLE_IN_ALL
void lrg_platformer_template_set_friction (LrgPlatformerTemplate *self,
                                            gfloat                 friction);

/**
 * lrg_platformer_template_get_air_friction:
 * @self: a #LrgPlatformerTemplate
 *
 * Gets the air friction (horizontal damping while airborne).
 *
 * Returns: air friction value
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_platformer_template_get_air_friction (LrgPlatformerTemplate *self);

/**
 * lrg_platformer_template_set_air_friction:
 * @self: a #LrgPlatformerTemplate
 * @friction: air friction value
 *
 * Sets the air friction.
 */
LRG_AVAILABLE_IN_ALL
void lrg_platformer_template_set_air_friction (LrgPlatformerTemplate *self,
                                                gfloat                 friction);

/* ==========================================================================
 * Gravity & Jump Properties
 * ========================================================================== */

/**
 * lrg_platformer_template_get_gravity:
 * @self: a #LrgPlatformerTemplate
 *
 * Gets the gravity acceleration.
 *
 * Returns: gravity in units per second squared
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_platformer_template_get_gravity (LrgPlatformerTemplate *self);

/**
 * lrg_platformer_template_set_gravity:
 * @self: a #LrgPlatformerTemplate
 * @gravity: gravity in units per second squared
 *
 * Sets the gravity acceleration.
 */
LRG_AVAILABLE_IN_ALL
void lrg_platformer_template_set_gravity (LrgPlatformerTemplate *self,
                                           gfloat                 gravity);

/**
 * lrg_platformer_template_get_jump_height:
 * @self: a #LrgPlatformerTemplate
 *
 * Gets the maximum jump height.
 *
 * Returns: jump height in units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_platformer_template_get_jump_height (LrgPlatformerTemplate *self);

/**
 * lrg_platformer_template_set_jump_height:
 * @self: a #LrgPlatformerTemplate
 * @height: jump height in units
 *
 * Sets the maximum jump height. This automatically calculates
 * the required initial jump velocity based on gravity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_platformer_template_set_jump_height (LrgPlatformerTemplate *self,
                                               gfloat                 height);

/**
 * lrg_platformer_template_get_fall_multiplier:
 * @self: a #LrgPlatformerTemplate
 *
 * Gets the gravity multiplier when falling (not holding jump).
 *
 * Returns: fall multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_platformer_template_get_fall_multiplier (LrgPlatformerTemplate *self);

/**
 * lrg_platformer_template_set_fall_multiplier:
 * @self: a #LrgPlatformerTemplate
 * @multiplier: fall gravity multiplier
 *
 * Sets the gravity multiplier for faster falling.
 */
LRG_AVAILABLE_IN_ALL
void lrg_platformer_template_set_fall_multiplier (LrgPlatformerTemplate *self,
                                                   gfloat                 multiplier);

/**
 * lrg_platformer_template_get_max_fall_speed:
 * @self: a #LrgPlatformerTemplate
 *
 * Gets the terminal velocity (max fall speed).
 *
 * Returns: max fall speed
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_platformer_template_get_max_fall_speed (LrgPlatformerTemplate *self);

/**
 * lrg_platformer_template_set_max_fall_speed:
 * @self: a #LrgPlatformerTemplate
 * @speed: max fall speed
 *
 * Sets the terminal velocity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_platformer_template_set_max_fall_speed (LrgPlatformerTemplate *self,
                                                  gfloat                 speed);

/* ==========================================================================
 * Coyote Time & Jump Buffer
 * ========================================================================== */

/**
 * lrg_platformer_template_get_coyote_time:
 * @self: a #LrgPlatformerTemplate
 *
 * Gets the coyote time duration (grace period to jump after leaving ledge).
 *
 * Returns: coyote time in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_platformer_template_get_coyote_time (LrgPlatformerTemplate *self);

/**
 * lrg_platformer_template_set_coyote_time:
 * @self: a #LrgPlatformerTemplate
 * @time: coyote time in seconds
 *
 * Sets the coyote time duration.
 */
LRG_AVAILABLE_IN_ALL
void lrg_platformer_template_set_coyote_time (LrgPlatformerTemplate *self,
                                               gfloat                 time);

/**
 * lrg_platformer_template_get_jump_buffer_time:
 * @self: a #LrgPlatformerTemplate
 *
 * Gets the jump buffer duration (pre-emptive jump input).
 *
 * Returns: jump buffer time in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_platformer_template_get_jump_buffer_time (LrgPlatformerTemplate *self);

/**
 * lrg_platformer_template_set_jump_buffer_time:
 * @self: a #LrgPlatformerTemplate
 * @time: jump buffer time in seconds
 *
 * Sets the jump buffer duration.
 */
LRG_AVAILABLE_IN_ALL
void lrg_platformer_template_set_jump_buffer_time (LrgPlatformerTemplate *self,
                                                    gfloat                 time);

/* ==========================================================================
 * Wall Mechanics
 * ========================================================================== */

/**
 * lrg_platformer_template_get_wall_slide_enabled:
 * @self: a #LrgPlatformerTemplate
 *
 * Gets whether wall sliding is enabled.
 *
 * Returns: %TRUE if wall slide is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_platformer_template_get_wall_slide_enabled (LrgPlatformerTemplate *self);

/**
 * lrg_platformer_template_set_wall_slide_enabled:
 * @self: a #LrgPlatformerTemplate
 * @enabled: whether to enable wall sliding
 *
 * Enables or disables wall sliding.
 */
LRG_AVAILABLE_IN_ALL
void lrg_platformer_template_set_wall_slide_enabled (LrgPlatformerTemplate *self,
                                                      gboolean               enabled);

/**
 * lrg_platformer_template_get_wall_slide_speed:
 * @self: a #LrgPlatformerTemplate
 *
 * Gets the maximum speed when wall sliding.
 *
 * Returns: wall slide speed
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_platformer_template_get_wall_slide_speed (LrgPlatformerTemplate *self);

/**
 * lrg_platformer_template_set_wall_slide_speed:
 * @self: a #LrgPlatformerTemplate
 * @speed: wall slide speed
 *
 * Sets the maximum wall slide speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_platformer_template_set_wall_slide_speed (LrgPlatformerTemplate *self,
                                                    gfloat                 speed);

/**
 * lrg_platformer_template_get_wall_jump_enabled:
 * @self: a #LrgPlatformerTemplate
 *
 * Gets whether wall jumping is enabled.
 *
 * Returns: %TRUE if wall jump is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_platformer_template_get_wall_jump_enabled (LrgPlatformerTemplate *self);

/**
 * lrg_platformer_template_set_wall_jump_enabled:
 * @self: a #LrgPlatformerTemplate
 * @enabled: whether to enable wall jumping
 *
 * Enables or disables wall jumping.
 */
LRG_AVAILABLE_IN_ALL
void lrg_platformer_template_set_wall_jump_enabled (LrgPlatformerTemplate *self,
                                                     gboolean               enabled);

/**
 * lrg_platformer_template_get_wall_jump_force:
 * @self: a #LrgPlatformerTemplate
 * @x: (out) (optional): location for horizontal force
 * @y: (out) (optional): location for vertical force
 *
 * Gets the wall jump force components.
 */
LRG_AVAILABLE_IN_ALL
void lrg_platformer_template_get_wall_jump_force (LrgPlatformerTemplate *self,
                                                   gfloat                *x,
                                                   gfloat                *y);

/**
 * lrg_platformer_template_set_wall_jump_force:
 * @self: a #LrgPlatformerTemplate
 * @x: horizontal force (away from wall)
 * @y: vertical force (upward)
 *
 * Sets the wall jump force.
 */
LRG_AVAILABLE_IN_ALL
void lrg_platformer_template_set_wall_jump_force (LrgPlatformerTemplate *self,
                                                   gfloat                 x,
                                                   gfloat                 y);

/* ==========================================================================
 * State Queries
 * ========================================================================== */

/**
 * lrg_platformer_template_is_grounded:
 * @self: a #LrgPlatformerTemplate
 *
 * Checks if the player is on the ground.
 *
 * Returns: %TRUE if grounded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_platformer_template_is_grounded (LrgPlatformerTemplate *self);

/**
 * lrg_platformer_template_is_jumping:
 * @self: a #LrgPlatformerTemplate
 *
 * Checks if the player is currently jumping (rising).
 *
 * Returns: %TRUE if jumping
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_platformer_template_is_jumping (LrgPlatformerTemplate *self);

/**
 * lrg_platformer_template_is_falling:
 * @self: a #LrgPlatformerTemplate
 *
 * Checks if the player is falling.
 *
 * Returns: %TRUE if falling
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_platformer_template_is_falling (LrgPlatformerTemplate *self);

/**
 * lrg_platformer_template_is_wall_sliding:
 * @self: a #LrgPlatformerTemplate
 *
 * Checks if the player is wall sliding.
 *
 * Returns: %TRUE if wall sliding
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_platformer_template_is_wall_sliding (LrgPlatformerTemplate *self);

/**
 * lrg_platformer_template_get_facing_direction:
 * @self: a #LrgPlatformerTemplate
 *
 * Gets the direction the player is facing.
 *
 * Returns: 1 for right, -1 for left
 */
LRG_AVAILABLE_IN_ALL
gint lrg_platformer_template_get_facing_direction (LrgPlatformerTemplate *self);

/* ==========================================================================
 * Input Methods
 * ========================================================================== */

/**
 * lrg_platformer_template_set_move_input:
 * @self: a #LrgPlatformerTemplate
 * @input: horizontal input (-1 to 1)
 *
 * Sets the horizontal movement input.
 */
LRG_AVAILABLE_IN_ALL
void lrg_platformer_template_set_move_input (LrgPlatformerTemplate *self,
                                              gfloat                 input);

/**
 * lrg_platformer_template_jump:
 * @self: a #LrgPlatformerTemplate
 *
 * Requests a jump. Will buffer if not grounded.
 *
 * Returns: %TRUE if jump initiated or buffered
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_platformer_template_jump (LrgPlatformerTemplate *self);

/**
 * lrg_platformer_template_release_jump:
 * @self: a #LrgPlatformerTemplate
 *
 * Called when jump button is released (for variable height jumps).
 */
LRG_AVAILABLE_IN_ALL
void lrg_platformer_template_release_jump (LrgPlatformerTemplate *self);

G_END_DECLS
