/* lrg-third-person-template.h - Third-person game template
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Base template for third-person action games.
 *
 * This template extends LrgGame3DTemplate with third-person camera features:
 * - Orbiting camera that follows the player
 * - Configurable camera distance and height offset
 * - Shoulder offset for over-the-shoulder aiming
 * - Multiple aim modes (free, aim, lock-on)
 * - Smooth camera collision avoidance
 * - Character rotation options (movement-based or camera-based)
 *
 * Subclass this template for action-adventure games, third-person shooters,
 * character action games, or any third-person perspective game.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-game-3d-template.h"

G_BEGIN_DECLS

#define LRG_TYPE_THIRD_PERSON_TEMPLATE (lrg_third_person_template_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgThirdPersonTemplate, lrg_third_person_template,
                          LRG, THIRD_PERSON_TEMPLATE, LrgGame3DTemplate)

/**
 * LrgThirdPersonAimMode:
 * @LRG_THIRD_PERSON_AIM_MODE_FREE: Free camera, character moves independently
 * @LRG_THIRD_PERSON_AIM_MODE_STRAFE: Character always faces camera direction
 * @LRG_THIRD_PERSON_AIM_MODE_AIM: Over-the-shoulder aiming mode
 * @LRG_THIRD_PERSON_AIM_MODE_LOCK_ON: Locked onto a target
 *
 * Camera and character orientation modes for third-person games.
 */
typedef enum
{
    LRG_THIRD_PERSON_AIM_MODE_FREE = 0,
    LRG_THIRD_PERSON_AIM_MODE_STRAFE,
    LRG_THIRD_PERSON_AIM_MODE_AIM,
    LRG_THIRD_PERSON_AIM_MODE_LOCK_ON
} LrgThirdPersonAimMode;

/**
 * LrgThirdPersonTemplateClass:
 * @parent_class: parent class
 * @on_aim_mode_changed: called when aim mode changes
 * @on_lock_on_target_changed: called when lock-on target changes
 * @on_jump: called when player jumps
 * @on_land: called when player lands
 * @on_dodge: called when player dodges/rolls
 * @on_attack: called when player attacks
 * @on_damage: called when player takes damage
 * @on_death: called when player dies
 * @update_movement: updates player position and rotation
 * @update_camera_orbit: updates camera orbit position
 * @check_camera_collision: checks for camera line-of-sight
 * @draw_character: renders the player character
 * @draw_target_indicator: renders lock-on indicator
 * @draw_crosshair: renders the aiming crosshair
 * @draw_hud: renders the HUD
 *
 * Class structure for #LrgThirdPersonTemplate.
 *
 * Subclasses should override these methods to implement
 * game-specific combat, movement, and rendering.
 */
struct _LrgThirdPersonTemplateClass
{
    LrgGame3DTemplateClass parent_class;

    /*< public >*/

    /**
     * LrgThirdPersonTemplateClass::on_aim_mode_changed:
     * @self: a #LrgThirdPersonTemplate
     * @old_mode: previous aim mode
     * @new_mode: new aim mode
     *
     * Called when the aim mode changes.
     *
     * Override to animate camera transitions or change controls.
     */
    void (*on_aim_mode_changed) (LrgThirdPersonTemplate *self,
                                 LrgThirdPersonAimMode   old_mode,
                                 LrgThirdPersonAimMode   new_mode);

    /**
     * LrgThirdPersonTemplateClass::on_lock_on_target_changed:
     * @self: a #LrgThirdPersonTemplate
     * @old_target: previous target position (or NULL)
     * @new_target: new target position (or NULL)
     *
     * Called when the lock-on target changes.
     */
    void (*on_lock_on_target_changed) (LrgThirdPersonTemplate *self,
                                       gpointer                old_target,
                                       gpointer                new_target);

    /**
     * LrgThirdPersonTemplateClass::on_jump:
     * @self: a #LrgThirdPersonTemplate
     *
     * Called when the player jumps.
     */
    void (*on_jump) (LrgThirdPersonTemplate *self);

    /**
     * LrgThirdPersonTemplateClass::on_land:
     * @self: a #LrgThirdPersonTemplate
     * @fall_velocity: velocity at landing (for fall damage)
     *
     * Called when the player lands on ground.
     */
    void (*on_land) (LrgThirdPersonTemplate *self,
                     gfloat                  fall_velocity);

    /**
     * LrgThirdPersonTemplateClass::on_dodge:
     * @self: a #LrgThirdPersonTemplate
     * @direction_x: dodge direction X component
     * @direction_z: dodge direction Z component
     *
     * Called when the player dodges or rolls.
     */
    void (*on_dodge) (LrgThirdPersonTemplate *self,
                      gfloat                  direction_x,
                      gfloat                  direction_z);

    /**
     * LrgThirdPersonTemplateClass::on_attack:
     * @self: a #LrgThirdPersonTemplate
     * @attack_type: type of attack (0=light, 1=heavy, etc.)
     *
     * Called when the player attacks.
     *
     * Returns: %TRUE if attack was performed
     */
    gboolean (*on_attack) (LrgThirdPersonTemplate *self,
                           gint                    attack_type);

    /**
     * LrgThirdPersonTemplateClass::on_damage:
     * @self: a #LrgThirdPersonTemplate
     * @amount: damage amount
     * @source_x: damage source X position
     * @source_y: damage source Y position
     * @source_z: damage source Z position
     *
     * Called when the player takes damage.
     */
    void (*on_damage) (LrgThirdPersonTemplate *self,
                       gfloat                  amount,
                       gfloat                  source_x,
                       gfloat                  source_y,
                       gfloat                  source_z);

    /**
     * LrgThirdPersonTemplateClass::on_death:
     * @self: a #LrgThirdPersonTemplate
     *
     * Called when the player dies (health reaches 0).
     */
    void (*on_death) (LrgThirdPersonTemplate *self);

    /**
     * LrgThirdPersonTemplateClass::update_movement:
     * @self: a #LrgThirdPersonTemplate
     * @delta: time since last frame in seconds
     *
     * Updates player position and rotation based on input.
     *
     * Default handles movement relative to camera direction.
     */
    void (*update_movement) (LrgThirdPersonTemplate *self,
                             gdouble                 delta);

    /**
     * LrgThirdPersonTemplateClass::update_camera_orbit:
     * @self: a #LrgThirdPersonTemplate
     * @delta: time since last frame in seconds
     *
     * Updates the camera orbit around the player.
     *
     * Default implements smooth follow with shoulder offset in aim mode.
     */
    void (*update_camera_orbit) (LrgThirdPersonTemplate *self,
                                 gdouble                 delta);

    /**
     * LrgThirdPersonTemplateClass::check_camera_collision:
     * @self: a #LrgThirdPersonTemplate
     * @camera_x: desired camera X position
     * @camera_y: desired camera Y position
     * @camera_z: desired camera Z position
     * @out_x: (out): adjusted camera X
     * @out_y: (out): adjusted camera Y
     * @out_z: (out): adjusted camera Z
     *
     * Checks for camera collision and adjusts position.
     *
     * Override to implement collision detection against world geometry.
     * Default returns the input position unchanged.
     *
     * Returns: %TRUE if collision was detected
     */
    gboolean (*check_camera_collision) (LrgThirdPersonTemplate *self,
                                        gfloat                  camera_x,
                                        gfloat                  camera_y,
                                        gfloat                  camera_z,
                                        gfloat                 *out_x,
                                        gfloat                 *out_y,
                                        gfloat                 *out_z);

    /**
     * LrgThirdPersonTemplateClass::draw_character:
     * @self: a #LrgThirdPersonTemplate
     *
     * Renders the player character.
     *
     * Called during world rendering with camera active.
     */
    void (*draw_character) (LrgThirdPersonTemplate *self);

    /**
     * LrgThirdPersonTemplateClass::draw_target_indicator:
     * @self: a #LrgThirdPersonTemplate
     *
     * Renders the lock-on target indicator.
     *
     * Called when in lock-on mode with a valid target.
     */
    void (*draw_target_indicator) (LrgThirdPersonTemplate *self);

    /**
     * LrgThirdPersonTemplateClass::draw_crosshair:
     * @self: a #LrgThirdPersonTemplate
     *
     * Renders the aiming crosshair.
     *
     * Called in aim mode (screen space).
     */
    void (*draw_crosshair) (LrgThirdPersonTemplate *self);

    /**
     * LrgThirdPersonTemplateClass::draw_hud:
     * @self: a #LrgThirdPersonTemplate
     *
     * Renders the HUD (health, stamina, etc.).
     */
    void (*draw_hud) (LrgThirdPersonTemplate *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Constructor
 * ========================================================================== */

/**
 * lrg_third_person_template_new:
 *
 * Creates a new third-person game template with default settings.
 *
 * Returns: (transfer full): a new #LrgThirdPersonTemplate
 */
LRG_AVAILABLE_IN_ALL
LrgThirdPersonTemplate * lrg_third_person_template_new (void);

/* ==========================================================================
 * Player Position
 * ========================================================================== */

/**
 * lrg_third_person_template_get_position:
 * @self: a #LrgThirdPersonTemplate
 * @x: (out) (optional): location for X
 * @y: (out) (optional): location for Y
 * @z: (out) (optional): location for Z
 *
 * Gets the player's world position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_get_position (LrgThirdPersonTemplate *self,
                                             gfloat                 *x,
                                             gfloat                 *y,
                                             gfloat                 *z);

/**
 * lrg_third_person_template_set_position:
 * @self: a #LrgThirdPersonTemplate
 * @x: X position
 * @y: Y position
 * @z: Z position
 *
 * Sets the player's world position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_position (LrgThirdPersonTemplate *self,
                                             gfloat                  x,
                                             gfloat                  y,
                                             gfloat                  z);

/**
 * lrg_third_person_template_get_rotation:
 * @self: a #LrgThirdPersonTemplate
 *
 * Gets the player's Y rotation (facing direction) in degrees.
 *
 * Returns: rotation angle in degrees
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_third_person_template_get_rotation (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_set_rotation:
 * @self: a #LrgThirdPersonTemplate
 * @rotation: rotation angle in degrees
 *
 * Sets the player's Y rotation (facing direction).
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_rotation (LrgThirdPersonTemplate *self,
                                             gfloat                  rotation);

/* ==========================================================================
 * Movement Settings
 * ========================================================================== */

/**
 * lrg_third_person_template_get_move_speed:
 * @self: a #LrgThirdPersonTemplate
 *
 * Gets the movement speed.
 *
 * Returns: move speed in units per second
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_third_person_template_get_move_speed (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_set_move_speed:
 * @self: a #LrgThirdPersonTemplate
 * @speed: move speed in units per second
 *
 * Sets the movement speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_move_speed (LrgThirdPersonTemplate *self,
                                               gfloat                  speed);

/**
 * lrg_third_person_template_get_run_multiplier:
 * @self: a #LrgThirdPersonTemplate
 *
 * Gets the run speed multiplier.
 *
 * Returns: run multiplier (e.g., 1.5 = 50% faster)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_third_person_template_get_run_multiplier (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_set_run_multiplier:
 * @self: a #LrgThirdPersonTemplate
 * @multiplier: run speed multiplier
 *
 * Sets the run speed multiplier.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_run_multiplier (LrgThirdPersonTemplate *self,
                                                   gfloat                  multiplier);

/**
 * lrg_third_person_template_get_rotation_speed:
 * @self: a #LrgThirdPersonTemplate
 *
 * Gets the character rotation speed.
 *
 * Returns: rotation speed in degrees per second
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_third_person_template_get_rotation_speed (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_set_rotation_speed:
 * @self: a #LrgThirdPersonTemplate
 * @speed: rotation speed in degrees per second
 *
 * Sets the character rotation speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_rotation_speed (LrgThirdPersonTemplate *self,
                                                   gfloat                  speed);

/**
 * lrg_third_person_template_get_jump_height:
 * @self: a #LrgThirdPersonTemplate
 *
 * Gets the jump height.
 *
 * Returns: jump height in world units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_third_person_template_get_jump_height (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_set_jump_height:
 * @self: a #LrgThirdPersonTemplate
 * @height: jump height in world units
 *
 * Sets the jump height.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_jump_height (LrgThirdPersonTemplate *self,
                                                gfloat                  height);

/**
 * lrg_third_person_template_get_gravity:
 * @self: a #LrgThirdPersonTemplate
 *
 * Gets the gravity acceleration.
 *
 * Returns: gravity in units per second squared
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_third_person_template_get_gravity (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_set_gravity:
 * @self: a #LrgThirdPersonTemplate
 * @gravity: gravity acceleration
 *
 * Sets the gravity acceleration.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_gravity (LrgThirdPersonTemplate *self,
                                            gfloat                  gravity);

/**
 * lrg_third_person_template_is_running:
 * @self: a #LrgThirdPersonTemplate
 *
 * Checks if the player is currently running.
 *
 * Returns: %TRUE if running
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_third_person_template_is_running (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_is_on_ground:
 * @self: a #LrgThirdPersonTemplate
 *
 * Checks if the player is on the ground.
 *
 * Returns: %TRUE if on ground
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_third_person_template_is_on_ground (LrgThirdPersonTemplate *self);

/* ==========================================================================
 * Camera Settings
 * ========================================================================== */

/**
 * lrg_third_person_template_get_camera_distance:
 * @self: a #LrgThirdPersonTemplate
 *
 * Gets the camera distance from the player.
 *
 * Returns: camera distance in world units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_third_person_template_get_camera_distance (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_set_camera_distance:
 * @self: a #LrgThirdPersonTemplate
 * @distance: camera distance in world units
 *
 * Sets the camera distance from the player.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_camera_distance (LrgThirdPersonTemplate *self,
                                                    gfloat                  distance);

/**
 * lrg_third_person_template_get_camera_height:
 * @self: a #LrgThirdPersonTemplate
 *
 * Gets the camera height offset from player center.
 *
 * Returns: camera height offset
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_third_person_template_get_camera_height (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_set_camera_height:
 * @self: a #LrgThirdPersonTemplate
 * @height: camera height offset
 *
 * Sets the camera height offset from player center.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_camera_height (LrgThirdPersonTemplate *self,
                                                  gfloat                  height);

/**
 * lrg_third_person_template_get_camera_smoothing:
 * @self: a #LrgThirdPersonTemplate
 *
 * Gets the camera follow smoothing factor.
 *
 * Returns: smoothing factor (0.0-1.0, 1.0 = instant)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_third_person_template_get_camera_smoothing (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_set_camera_smoothing:
 * @self: a #LrgThirdPersonTemplate
 * @smoothing: smoothing factor (0.0-1.0)
 *
 * Sets the camera follow smoothing factor.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_camera_smoothing (LrgThirdPersonTemplate *self,
                                                     gfloat                  smoothing);

/* ==========================================================================
 * Shoulder Offset (for aiming)
 * ========================================================================== */

/**
 * lrg_third_person_template_get_shoulder_offset:
 * @self: a #LrgThirdPersonTemplate
 * @x: (out) (optional): location for X offset (left/right)
 * @y: (out) (optional): location for Y offset (up/down)
 *
 * Gets the shoulder offset for over-the-shoulder aiming.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_get_shoulder_offset (LrgThirdPersonTemplate *self,
                                                    gfloat                 *x,
                                                    gfloat                 *y);

/**
 * lrg_third_person_template_set_shoulder_offset:
 * @self: a #LrgThirdPersonTemplate
 * @x: X offset (positive = right shoulder)
 * @y: Y offset (positive = higher)
 *
 * Sets the shoulder offset for over-the-shoulder aiming.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_shoulder_offset (LrgThirdPersonTemplate *self,
                                                    gfloat                  x,
                                                    gfloat                  y);

/**
 * lrg_third_person_template_swap_shoulder:
 * @self: a #LrgThirdPersonTemplate
 *
 * Swaps the shoulder offset side (left/right).
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_swap_shoulder (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_get_aim_distance:
 * @self: a #LrgThirdPersonTemplate
 *
 * Gets the camera distance when aiming.
 *
 * Returns: aim mode camera distance
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_third_person_template_get_aim_distance (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_set_aim_distance:
 * @self: a #LrgThirdPersonTemplate
 * @distance: camera distance when aiming
 *
 * Sets the camera distance when aiming.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_aim_distance (LrgThirdPersonTemplate *self,
                                                 gfloat                  distance);

/* ==========================================================================
 * Aim Mode
 * ========================================================================== */

/**
 * lrg_third_person_template_get_aim_mode:
 * @self: a #LrgThirdPersonTemplate
 *
 * Gets the current aim mode.
 *
 * Returns: the #LrgThirdPersonAimMode
 */
LRG_AVAILABLE_IN_ALL
LrgThirdPersonAimMode lrg_third_person_template_get_aim_mode (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_set_aim_mode:
 * @self: a #LrgThirdPersonTemplate
 * @mode: the aim mode
 *
 * Sets the aim mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_aim_mode (LrgThirdPersonTemplate *self,
                                             LrgThirdPersonAimMode   mode);

/**
 * lrg_third_person_template_is_aiming:
 * @self: a #LrgThirdPersonTemplate
 *
 * Checks if the player is in aiming mode.
 *
 * Returns: %TRUE if aiming (AIM or LOCK_ON mode)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_third_person_template_is_aiming (LrgThirdPersonTemplate *self);

/* ==========================================================================
 * Lock-On System
 * ========================================================================== */

/**
 * lrg_third_person_template_get_lock_on_target:
 * @self: a #LrgThirdPersonTemplate
 * @x: (out) (optional): location for target X
 * @y: (out) (optional): location for target Y
 * @z: (out) (optional): location for target Z
 *
 * Gets the current lock-on target position.
 *
 * Returns: %TRUE if there is a target
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_third_person_template_get_lock_on_target (LrgThirdPersonTemplate *self,
                                                       gfloat                 *x,
                                                       gfloat                 *y,
                                                       gfloat                 *z);

/**
 * lrg_third_person_template_set_lock_on_target:
 * @self: a #LrgThirdPersonTemplate
 * @x: target X position
 * @y: target Y position
 * @z: target Z position
 *
 * Sets the lock-on target position and enables lock-on mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_lock_on_target (LrgThirdPersonTemplate *self,
                                                   gfloat                  x,
                                                   gfloat                  y,
                                                   gfloat                  z);

/**
 * lrg_third_person_template_clear_lock_on:
 * @self: a #LrgThirdPersonTemplate
 *
 * Clears the lock-on target and returns to free mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_clear_lock_on (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_get_lock_on_range:
 * @self: a #LrgThirdPersonTemplate
 *
 * Gets the maximum lock-on range.
 *
 * Returns: lock-on range in world units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_third_person_template_get_lock_on_range (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_set_lock_on_range:
 * @self: a #LrgThirdPersonTemplate
 * @range: lock-on range in world units
 *
 * Sets the maximum lock-on range.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_lock_on_range (LrgThirdPersonTemplate *self,
                                                  gfloat                  range);

/* ==========================================================================
 * Health and Stamina
 * ========================================================================== */

/**
 * lrg_third_person_template_get_health:
 * @self: a #LrgThirdPersonTemplate
 *
 * Gets the current health.
 *
 * Returns: current health
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_third_person_template_get_health (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_set_health:
 * @self: a #LrgThirdPersonTemplate
 * @health: health value (clamped to 0-max)
 *
 * Sets the current health.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_health (LrgThirdPersonTemplate *self,
                                           gfloat                  health);

/**
 * lrg_third_person_template_get_max_health:
 * @self: a #LrgThirdPersonTemplate
 *
 * Gets the maximum health.
 *
 * Returns: max health
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_third_person_template_get_max_health (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_set_max_health:
 * @self: a #LrgThirdPersonTemplate
 * @max: maximum health
 *
 * Sets the maximum health.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_max_health (LrgThirdPersonTemplate *self,
                                               gfloat                  max);

/**
 * lrg_third_person_template_get_stamina:
 * @self: a #LrgThirdPersonTemplate
 *
 * Gets the current stamina.
 *
 * Returns: current stamina
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_third_person_template_get_stamina (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_set_stamina:
 * @self: a #LrgThirdPersonTemplate
 * @stamina: stamina value (clamped to 0-max)
 *
 * Sets the current stamina.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_stamina (LrgThirdPersonTemplate *self,
                                            gfloat                  stamina);

/**
 * lrg_third_person_template_get_max_stamina:
 * @self: a #LrgThirdPersonTemplate
 *
 * Gets the maximum stamina.
 *
 * Returns: max stamina
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_third_person_template_get_max_stamina (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_set_max_stamina:
 * @self: a #LrgThirdPersonTemplate
 * @max: maximum stamina
 *
 * Sets the maximum stamina.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_max_stamina (LrgThirdPersonTemplate *self,
                                                gfloat                  max);

/**
 * lrg_third_person_template_apply_damage:
 * @self: a #LrgThirdPersonTemplate
 * @damage: damage amount
 * @source_x: damage source X
 * @source_y: damage source Y
 * @source_z: damage source Z
 *
 * Applies damage to the player.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_apply_damage (LrgThirdPersonTemplate *self,
                                             gfloat                  damage,
                                             gfloat                  source_x,
                                             gfloat                  source_y,
                                             gfloat                  source_z);

/**
 * lrg_third_person_template_is_dead:
 * @self: a #LrgThirdPersonTemplate
 *
 * Checks if the player is dead.
 *
 * Returns: %TRUE if health is 0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_third_person_template_is_dead (LrgThirdPersonTemplate *self);

/* ==========================================================================
 * Dodge System
 * ========================================================================== */

/**
 * lrg_third_person_template_get_dodge_distance:
 * @self: a #LrgThirdPersonTemplate
 *
 * Gets the dodge distance.
 *
 * Returns: dodge distance in world units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_third_person_template_get_dodge_distance (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_set_dodge_distance:
 * @self: a #LrgThirdPersonTemplate
 * @distance: dodge distance in world units
 *
 * Sets the dodge distance.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_dodge_distance (LrgThirdPersonTemplate *self,
                                                   gfloat                  distance);

/**
 * lrg_third_person_template_get_dodge_stamina_cost:
 * @self: a #LrgThirdPersonTemplate
 *
 * Gets the stamina cost of dodging.
 *
 * Returns: stamina cost
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_third_person_template_get_dodge_stamina_cost (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_set_dodge_stamina_cost:
 * @self: a #LrgThirdPersonTemplate
 * @cost: stamina cost
 *
 * Sets the stamina cost of dodging.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_dodge_stamina_cost (LrgThirdPersonTemplate *self,
                                                       gfloat                  cost);

/**
 * lrg_third_person_template_can_dodge:
 * @self: a #LrgThirdPersonTemplate
 *
 * Checks if the player can dodge (has enough stamina).
 *
 * Returns: %TRUE if can dodge
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_third_person_template_can_dodge (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_is_dodging:
 * @self: a #LrgThirdPersonTemplate
 *
 * Checks if the player is currently dodging.
 *
 * Returns: %TRUE if dodging
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_third_person_template_is_dodging (LrgThirdPersonTemplate *self);

/* ==========================================================================
 * Crosshair
 * ========================================================================== */

/**
 * lrg_third_person_template_get_crosshair_visible:
 * @self: a #LrgThirdPersonTemplate
 *
 * Gets whether the crosshair is visible.
 *
 * Returns: %TRUE if crosshair is visible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_third_person_template_get_crosshair_visible (LrgThirdPersonTemplate *self);

/**
 * lrg_third_person_template_set_crosshair_visible:
 * @self: a #LrgThirdPersonTemplate
 * @visible: whether to show crosshair
 *
 * Sets crosshair visibility.
 */
LRG_AVAILABLE_IN_ALL
void lrg_third_person_template_set_crosshair_visible (LrgThirdPersonTemplate *self,
                                                      gboolean                visible);

G_END_DECLS
