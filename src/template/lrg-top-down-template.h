/* lrg-top-down-template.h - Top-down game template
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Base template for top-down 2D games like RPGs, adventure games,
 * dungeon crawlers, and twin-stick action games.
 *
 * This template extends LrgGame2DTemplate with top-down specific features:
 * - Multiple movement modes (4-dir, 8-dir, free movement)
 * - Character facing direction tracking
 * - Interaction system for NPCs, objects, and triggers
 * - Movement physics with acceleration and friction
 * - Camera look-ahead for smoother scrolling
 *
 * Subclass this template for games with a top-down or isometric perspective.
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

#define LRG_TYPE_TOP_DOWN_TEMPLATE (lrg_top_down_template_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgTopDownTemplate, lrg_top_down_template,
                          LRG, TOP_DOWN_TEMPLATE, LrgGame2DTemplate)

/**
 * LrgTopDownMovementMode:
 * @LRG_TOP_DOWN_MOVEMENT_4_DIR: 4-directional movement (up/down/left/right)
 * @LRG_TOP_DOWN_MOVEMENT_8_DIR: 8-directional movement (includes diagonals)
 * @LRG_TOP_DOWN_MOVEMENT_FREE: Free analog movement (any angle)
 * @LRG_TOP_DOWN_MOVEMENT_TANK: Tank controls (forward/back + rotate)
 *
 * Movement modes for top-down games.
 *
 * Different games require different movement styles. 4-directional is
 * common for classic RPGs, 8-directional for action games, free movement
 * for twin-stick shooters, and tank controls for vehicle-based games.
 */
typedef enum
{
    LRG_TOP_DOWN_MOVEMENT_4_DIR = 0,
    LRG_TOP_DOWN_MOVEMENT_8_DIR,
    LRG_TOP_DOWN_MOVEMENT_FREE,
    LRG_TOP_DOWN_MOVEMENT_TANK
} LrgTopDownMovementMode;

/**
 * LrgFacingDirection:
 * @LRG_FACING_UP: Facing up (north)
 * @LRG_FACING_DOWN: Facing down (south)
 * @LRG_FACING_LEFT: Facing left (west)
 * @LRG_FACING_RIGHT: Facing right (east)
 * @LRG_FACING_UP_LEFT: Facing up-left (northwest)
 * @LRG_FACING_UP_RIGHT: Facing up-right (northeast)
 * @LRG_FACING_DOWN_LEFT: Facing down-left (southwest)
 * @LRG_FACING_DOWN_RIGHT: Facing down-right (southeast)
 *
 * Character facing directions for sprite selection and interaction.
 *
 * For 4-directional games, only the cardinal directions are used.
 * For 8-directional games, diagonal directions are also available.
 */
typedef enum
{
    LRG_FACING_DOWN = 0,
    LRG_FACING_UP,
    LRG_FACING_LEFT,
    LRG_FACING_RIGHT,
    LRG_FACING_DOWN_LEFT,
    LRG_FACING_DOWN_RIGHT,
    LRG_FACING_UP_LEFT,
    LRG_FACING_UP_RIGHT
} LrgFacingDirection;

/**
 * LrgTopDownTemplateClass:
 * @parent_class: parent class
 * @on_movement_input: processes movement input, returning desired velocity
 * @on_facing_changed: called when character facing direction changes
 * @on_interact: called when player presses interact button
 * @on_interact_target_changed: called when closest interactable changes
 * @update_movement: updates position based on velocity
 * @check_collision: collision detection callback
 * @draw_player: renders the player character
 * @draw_interact_prompt: draws interaction prompt near interactable
 *
 * Class structure for #LrgTopDownTemplate.
 *
 * Subclasses should override the virtual methods to implement
 * custom movement physics, collision detection, and rendering.
 */
struct _LrgTopDownTemplateClass
{
    LrgGame2DTemplateClass parent_class;

    /*< public >*/

    /**
     * LrgTopDownTemplateClass::on_movement_input:
     * @self: a #LrgTopDownTemplate
     * @input_x: horizontal input (-1 to 1)
     * @input_y: vertical input (-1 to 1)
     * @delta: time since last frame in seconds
     * @velocity_x: (out): output horizontal velocity
     * @velocity_y: (out): output vertical velocity
     *
     * Processes movement input and returns desired velocity.
     *
     * The default implementation applies acceleration, friction,
     * and the current movement mode constraints.
     */
    void (*on_movement_input) (LrgTopDownTemplate *self,
                               gfloat              input_x,
                               gfloat              input_y,
                               gdouble             delta,
                               gfloat             *velocity_x,
                               gfloat             *velocity_y);

    /**
     * LrgTopDownTemplateClass::on_facing_changed:
     * @self: a #LrgTopDownTemplate
     * @old_facing: previous facing direction
     * @new_facing: new facing direction
     *
     * Called when the character facing direction changes.
     *
     * Override to trigger animation changes or sound effects.
     */
    void (*on_facing_changed) (LrgTopDownTemplate *self,
                               LrgFacingDirection  old_facing,
                               LrgFacingDirection  new_facing);

    /**
     * LrgTopDownTemplateClass::on_interact:
     * @self: a #LrgTopDownTemplate
     *
     * Called when the player presses the interact button.
     *
     * Override to implement custom interaction logic. The default
     * implementation emits the "interact" signal if there is
     * a valid interaction target.
     *
     * Returns: %TRUE if an interaction occurred
     */
    gboolean (*on_interact) (LrgTopDownTemplate *self);

    /**
     * LrgTopDownTemplateClass::on_interact_target_changed:
     * @self: a #LrgTopDownTemplate
     * @target: (nullable): the new closest interactable, or %NULL if none
     *
     * Called when the closest interactable target changes.
     *
     * Override to update UI prompts or highlight the target.
     */
    void (*on_interact_target_changed) (LrgTopDownTemplate *self,
                                        gpointer            target);

    /**
     * LrgTopDownTemplateClass::update_movement:
     * @self: a #LrgTopDownTemplate
     * @delta: time since last frame in seconds
     *
     * Updates the player position based on current velocity.
     *
     * The default implementation applies velocity, checks collision,
     * and updates the facing direction based on movement.
     */
    void (*update_movement) (LrgTopDownTemplate *self,
                             gdouble             delta);

    /**
     * LrgTopDownTemplateClass::check_collision:
     * @self: a #LrgTopDownTemplate
     * @new_x: proposed new X position
     * @new_y: proposed new Y position
     * @resolved_x: (out): resolved X position after collision
     * @resolved_y: (out): resolved Y position after collision
     *
     * Checks for collision and resolves the position.
     *
     * Override to implement tilemap collision, entity collision,
     * or physics-based collision response.
     *
     * The default implementation does no collision (all moves allowed).
     *
     * Returns: %TRUE if collision occurred
     */
    gboolean (*check_collision) (LrgTopDownTemplate *self,
                                 gfloat              new_x,
                                 gfloat              new_y,
                                 gfloat             *resolved_x,
                                 gfloat             *resolved_y);

    /**
     * LrgTopDownTemplateClass::draw_player:
     * @self: a #LrgTopDownTemplate
     *
     * Renders the player character.
     *
     * The default implementation draws a simple placeholder.
     * Override to draw sprites based on facing direction and state.
     */
    void (*draw_player) (LrgTopDownTemplate *self);

    /**
     * LrgTopDownTemplateClass::draw_interact_prompt:
     * @self: a #LrgTopDownTemplate
     * @target_x: world X position of the interact target
     * @target_y: world Y position of the interact target
     *
     * Draws the interaction prompt near an interactable object.
     *
     * Override to customize the prompt appearance.
     */
    void (*draw_interact_prompt) (LrgTopDownTemplate *self,
                                  gfloat              target_x,
                                  gfloat              target_y);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Constructor
 * ========================================================================== */

/**
 * lrg_top_down_template_new:
 *
 * Creates a new top-down game template with default settings.
 *
 * Returns: (transfer full): a new #LrgTopDownTemplate
 */
LRG_AVAILABLE_IN_ALL
LrgTopDownTemplate * lrg_top_down_template_new (void);

/* ==========================================================================
 * Player Position
 * ========================================================================== */

/**
 * lrg_top_down_template_get_player_x:
 * @self: a #LrgTopDownTemplate
 *
 * Gets the player X position in world coordinates.
 *
 * Returns: the player X position
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_top_down_template_get_player_x (LrgTopDownTemplate *self);

/**
 * lrg_top_down_template_get_player_y:
 * @self: a #LrgTopDownTemplate
 *
 * Gets the player Y position in world coordinates.
 *
 * Returns: the player Y position
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_top_down_template_get_player_y (LrgTopDownTemplate *self);

/**
 * lrg_top_down_template_set_player_position:
 * @self: a #LrgTopDownTemplate
 * @x: the X position in world coordinates
 * @y: the Y position in world coordinates
 *
 * Sets the player position in world coordinates.
 *
 * This directly sets the position without collision checking.
 * Use for teleportation, spawning, or loading saved positions.
 */
LRG_AVAILABLE_IN_ALL
void lrg_top_down_template_set_player_position (LrgTopDownTemplate *self,
                                                gfloat              x,
                                                gfloat              y);

/**
 * lrg_top_down_template_get_player_velocity:
 * @self: a #LrgTopDownTemplate
 * @velocity_x: (out) (optional): location for X velocity
 * @velocity_y: (out) (optional): location for Y velocity
 *
 * Gets the current player velocity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_top_down_template_get_player_velocity (LrgTopDownTemplate *self,
                                                gfloat             *velocity_x,
                                                gfloat             *velocity_y);

/* ==========================================================================
 * Movement Settings
 * ========================================================================== */

/**
 * lrg_top_down_template_get_movement_mode:
 * @self: a #LrgTopDownTemplate
 *
 * Gets the current movement mode.
 *
 * Returns: the #LrgTopDownMovementMode
 */
LRG_AVAILABLE_IN_ALL
LrgTopDownMovementMode lrg_top_down_template_get_movement_mode (LrgTopDownTemplate *self);

/**
 * lrg_top_down_template_set_movement_mode:
 * @self: a #LrgTopDownTemplate
 * @mode: the movement mode to use
 *
 * Sets the movement mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_top_down_template_set_movement_mode (LrgTopDownTemplate     *self,
                                              LrgTopDownMovementMode  mode);

/**
 * lrg_top_down_template_get_move_speed:
 * @self: a #LrgTopDownTemplate
 *
 * Gets the maximum movement speed in pixels per second.
 *
 * Returns: the movement speed
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_top_down_template_get_move_speed (LrgTopDownTemplate *self);

/**
 * lrg_top_down_template_set_move_speed:
 * @self: a #LrgTopDownTemplate
 * @speed: the movement speed in pixels per second
 *
 * Sets the maximum movement speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_top_down_template_set_move_speed (LrgTopDownTemplate *self,
                                           gfloat              speed);

/**
 * lrg_top_down_template_get_acceleration:
 * @self: a #LrgTopDownTemplate
 *
 * Gets the movement acceleration rate.
 *
 * Returns: acceleration in pixels per second squared
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_top_down_template_get_acceleration (LrgTopDownTemplate *self);

/**
 * lrg_top_down_template_set_acceleration:
 * @self: a #LrgTopDownTemplate
 * @acceleration: acceleration in pixels per second squared
 *
 * Sets the movement acceleration rate.
 *
 * Higher values make movement feel more responsive.
 * Set very high for instant acceleration.
 */
LRG_AVAILABLE_IN_ALL
void lrg_top_down_template_set_acceleration (LrgTopDownTemplate *self,
                                             gfloat              acceleration);

/**
 * lrg_top_down_template_get_friction:
 * @self: a #LrgTopDownTemplate
 *
 * Gets the movement friction/deceleration rate.
 *
 * Returns: friction in pixels per second squared
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_top_down_template_get_friction (LrgTopDownTemplate *self);

/**
 * lrg_top_down_template_set_friction:
 * @self: a #LrgTopDownTemplate
 * @friction: friction in pixels per second squared
 *
 * Sets the movement friction/deceleration rate.
 *
 * Higher values make the character stop faster when input stops.
 * Set very high for instant stopping.
 */
LRG_AVAILABLE_IN_ALL
void lrg_top_down_template_set_friction (LrgTopDownTemplate *self,
                                         gfloat              friction);

/* ==========================================================================
 * Facing Direction
 * ========================================================================== */

/**
 * lrg_top_down_template_get_facing:
 * @self: a #LrgTopDownTemplate
 *
 * Gets the current facing direction.
 *
 * Returns: the #LrgFacingDirection
 */
LRG_AVAILABLE_IN_ALL
LrgFacingDirection lrg_top_down_template_get_facing (LrgTopDownTemplate *self);

/**
 * lrg_top_down_template_set_facing:
 * @self: a #LrgTopDownTemplate
 * @facing: the facing direction
 *
 * Sets the facing direction.
 *
 * This is normally updated automatically based on movement,
 * but can be set manually for cutscenes or dialogue.
 */
LRG_AVAILABLE_IN_ALL
void lrg_top_down_template_set_facing (LrgTopDownTemplate *self,
                                       LrgFacingDirection  facing);

/**
 * lrg_top_down_template_get_facing_angle:
 * @self: a #LrgTopDownTemplate
 *
 * Gets the current facing as an angle in radians.
 *
 * For free movement mode, this returns the actual angle.
 * For other modes, it returns the angle of the discrete direction.
 *
 * Returns: facing angle in radians (0 = right, PI/2 = down)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_top_down_template_get_facing_angle (LrgTopDownTemplate *self);

/* ==========================================================================
 * Tank Controls (for TANK movement mode)
 * ========================================================================== */

/**
 * lrg_top_down_template_get_rotation_speed:
 * @self: a #LrgTopDownTemplate
 *
 * Gets the rotation speed for tank controls.
 *
 * Returns: rotation speed in radians per second
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_top_down_template_get_rotation_speed (LrgTopDownTemplate *self);

/**
 * lrg_top_down_template_set_rotation_speed:
 * @self: a #LrgTopDownTemplate
 * @speed: rotation speed in radians per second
 *
 * Sets the rotation speed for tank controls.
 */
LRG_AVAILABLE_IN_ALL
void lrg_top_down_template_set_rotation_speed (LrgTopDownTemplate *self,
                                               gfloat              speed);

/* ==========================================================================
 * Interaction System
 * ========================================================================== */

/**
 * lrg_top_down_template_get_interact_radius:
 * @self: a #LrgTopDownTemplate
 *
 * Gets the interaction detection radius.
 *
 * Returns: the radius in world units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_top_down_template_get_interact_radius (LrgTopDownTemplate *self);

/**
 * lrg_top_down_template_set_interact_radius:
 * @self: a #LrgTopDownTemplate
 * @radius: the interaction radius in world units
 *
 * Sets the interaction detection radius.
 *
 * Objects within this radius of the player can be interacted with.
 */
LRG_AVAILABLE_IN_ALL
void lrg_top_down_template_set_interact_radius (LrgTopDownTemplate *self,
                                                gfloat              radius);

/**
 * lrg_top_down_template_get_interact_target:
 * @self: a #LrgTopDownTemplate
 *
 * Gets the current closest interactable target.
 *
 * Returns: (transfer none) (nullable): the current target or %NULL
 */
LRG_AVAILABLE_IN_ALL
gpointer lrg_top_down_template_get_interact_target (LrgTopDownTemplate *self);

/**
 * lrg_top_down_template_set_interact_target:
 * @self: a #LrgTopDownTemplate
 * @target: (nullable): the interact target to set
 *
 * Sets the current interact target manually.
 *
 * Normally the template finds the closest interactable automatically,
 * but this can be used to override the selection.
 */
LRG_AVAILABLE_IN_ALL
void lrg_top_down_template_set_interact_target (LrgTopDownTemplate *self,
                                                gpointer            target);

/**
 * lrg_top_down_template_trigger_interact:
 * @self: a #LrgTopDownTemplate
 *
 * Triggers an interaction with the current target.
 *
 * This is called when the player presses the interact button.
 *
 * Returns: %TRUE if an interaction occurred
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_top_down_template_trigger_interact (LrgTopDownTemplate *self);

/* ==========================================================================
 * Camera Look-Ahead
 * ========================================================================== */

/**
 * lrg_top_down_template_get_look_ahead:
 * @self: a #LrgTopDownTemplate
 *
 * Gets the camera look-ahead distance.
 *
 * Returns: look-ahead distance in world units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_top_down_template_get_look_ahead (LrgTopDownTemplate *self);

/**
 * lrg_top_down_template_set_look_ahead:
 * @self: a #LrgTopDownTemplate
 * @distance: look-ahead distance in world units
 *
 * Sets the camera look-ahead distance.
 *
 * The camera will offset in the direction of movement,
 * showing more of what's ahead of the player.
 */
LRG_AVAILABLE_IN_ALL
void lrg_top_down_template_set_look_ahead (LrgTopDownTemplate *self,
                                           gfloat              distance);

/**
 * lrg_top_down_template_get_look_ahead_speed:
 * @self: a #LrgTopDownTemplate
 *
 * Gets the camera look-ahead interpolation speed.
 *
 * Returns: interpolation speed (0.0-1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_top_down_template_get_look_ahead_speed (LrgTopDownTemplate *self);

/**
 * lrg_top_down_template_set_look_ahead_speed:
 * @self: a #LrgTopDownTemplate
 * @speed: interpolation speed (0.0-1.0)
 *
 * Sets the camera look-ahead interpolation speed.
 *
 * Lower values create smoother but slower look-ahead.
 */
LRG_AVAILABLE_IN_ALL
void lrg_top_down_template_set_look_ahead_speed (LrgTopDownTemplate *self,
                                                 gfloat              speed);

/* ==========================================================================
 * Player Size (for collision and rendering)
 * ========================================================================== */

/**
 * lrg_top_down_template_get_player_width:
 * @self: a #LrgTopDownTemplate
 *
 * Gets the player collision/render width.
 *
 * Returns: the width in world units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_top_down_template_get_player_width (LrgTopDownTemplate *self);

/**
 * lrg_top_down_template_set_player_width:
 * @self: a #LrgTopDownTemplate
 * @width: the width in world units
 *
 * Sets the player collision/render width.
 */
LRG_AVAILABLE_IN_ALL
void lrg_top_down_template_set_player_width (LrgTopDownTemplate *self,
                                             gfloat              width);

/**
 * lrg_top_down_template_get_player_height:
 * @self: a #LrgTopDownTemplate
 *
 * Gets the player collision/render height.
 *
 * Returns: the height in world units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_top_down_template_get_player_height (LrgTopDownTemplate *self);

/**
 * lrg_top_down_template_set_player_height:
 * @self: a #LrgTopDownTemplate
 * @height: the height in world units
 *
 * Sets the player collision/render height.
 */
LRG_AVAILABLE_IN_ALL
void lrg_top_down_template_set_player_height (LrgTopDownTemplate *self,
                                              gfloat              height);

G_END_DECLS
