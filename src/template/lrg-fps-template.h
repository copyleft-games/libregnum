/* lrg-fps-template.h - First-person shooter game template
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Base template for first-person shooter games.
 *
 * This template extends LrgGame3DTemplate with FPS-specific features:
 * - First-person movement (WASD + mouse look)
 * - Sprint, crouch, and jump mechanics
 * - Weapon handling (fire, reload, switch)
 * - Health and armor system
 * - Crosshair rendering
 * - Head bob effect
 *
 * Subclass this template for FPS games, immersive sims,
 * horror games, or any first-person perspective game.
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

#define LRG_TYPE_FPS_TEMPLATE (lrg_fps_template_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgFPSTemplate, lrg_fps_template,
                          LRG, FPS_TEMPLATE, LrgGame3DTemplate)

/**
 * LrgFPSPosture:
 * @LRG_FPS_POSTURE_STANDING: Standing upright
 * @LRG_FPS_POSTURE_CROUCHING: Crouching (lower height, slower)
 * @LRG_FPS_POSTURE_PRONE: Prone (lying down, very low)
 *
 * Player posture states affecting height and movement.
 */
typedef enum
{
    LRG_FPS_POSTURE_STANDING = 0,
    LRG_FPS_POSTURE_CROUCHING,
    LRG_FPS_POSTURE_PRONE
} LrgFPSPosture;

/**
 * LrgFPSTemplateClass:
 * @parent_class: parent class
 * @on_fire: called when fire button is pressed
 * @on_reload: called when reload is triggered
 * @on_weapon_switch: called when switching weapons
 * @on_jump: called when player jumps
 * @on_land: called when player lands
 * @on_damage: called when player takes damage
 * @on_death: called when player dies
 * @on_posture_changed: called when posture changes
 * @update_movement: updates player position
 * @check_ground: checks if player is on ground
 * @draw_weapon: renders the weapon viewmodel
 * @draw_crosshair: renders the crosshair
 * @draw_hud: renders health, ammo, etc.
 *
 * Class structure for #LrgFPSTemplate.
 *
 * Subclasses should override these methods to implement
 * game-specific combat, movement, and rendering.
 */
struct _LrgFPSTemplateClass
{
    LrgGame3DTemplateClass parent_class;

    /*< public >*/

    /**
     * LrgFPSTemplateClass::on_fire:
     * @self: a #LrgFPSTemplate
     * @is_primary: %TRUE for primary fire, %FALSE for secondary
     *
     * Called when the fire button is pressed.
     *
     * Override to implement weapon firing, ammo consumption, etc.
     *
     * Returns: %TRUE if fire was successful
     */
    gboolean (*on_fire) (LrgFPSTemplate *self,
                         gboolean        is_primary);

    /**
     * LrgFPSTemplateClass::on_reload:
     * @self: a #LrgFPSTemplate
     *
     * Called when reload is triggered.
     *
     * Returns: %TRUE if reload started
     */
    gboolean (*on_reload) (LrgFPSTemplate *self);

    /**
     * LrgFPSTemplateClass::on_weapon_switch:
     * @self: a #LrgFPSTemplate
     * @weapon_index: new weapon index
     *
     * Called when switching weapons.
     */
    void (*on_weapon_switch) (LrgFPSTemplate *self,
                              gint            weapon_index);

    /**
     * LrgFPSTemplateClass::on_jump:
     * @self: a #LrgFPSTemplate
     *
     * Called when the player jumps.
     */
    void (*on_jump) (LrgFPSTemplate *self);

    /**
     * LrgFPSTemplateClass::on_land:
     * @self: a #LrgFPSTemplate
     * @fall_velocity: velocity at landing (for fall damage)
     *
     * Called when the player lands on ground.
     */
    void (*on_land) (LrgFPSTemplate *self,
                     gfloat          fall_velocity);

    /**
     * LrgFPSTemplateClass::on_damage:
     * @self: a #LrgFPSTemplate
     * @amount: damage amount
     * @source_x: damage source X position
     * @source_y: damage source Y position
     * @source_z: damage source Z position
     *
     * Called when the player takes damage.
     */
    void (*on_damage) (LrgFPSTemplate *self,
                       gfloat          amount,
                       gfloat          source_x,
                       gfloat          source_y,
                       gfloat          source_z);

    /**
     * LrgFPSTemplateClass::on_death:
     * @self: a #LrgFPSTemplate
     *
     * Called when the player dies (health reaches 0).
     */
    void (*on_death) (LrgFPSTemplate *self);

    /**
     * LrgFPSTemplateClass::on_posture_changed:
     * @self: a #LrgFPSTemplate
     * @old_posture: previous posture
     * @new_posture: new posture
     *
     * Called when the player's posture changes.
     */
    void (*on_posture_changed) (LrgFPSTemplate *self,
                                LrgFPSPosture   old_posture,
                                LrgFPSPosture   new_posture);

    /**
     * LrgFPSTemplateClass::update_movement:
     * @self: a #LrgFPSTemplate
     * @delta: time since last frame in seconds
     *
     * Updates player position based on input.
     *
     * Default handles WASD movement, gravity, and jumping.
     */
    void (*update_movement) (LrgFPSTemplate *self,
                             gdouble         delta);

    /**
     * LrgFPSTemplateClass::check_ground:
     * @self: a #LrgFPSTemplate
     *
     * Checks if the player is on solid ground.
     *
     * Override for custom collision detection.
     * Default returns TRUE if Y position is at floor level.
     *
     * Returns: %TRUE if on ground
     */
    gboolean (*check_ground) (LrgFPSTemplate *self);

    /**
     * LrgFPSTemplateClass::draw_weapon:
     * @self: a #LrgFPSTemplate
     *
     * Renders the weapon viewmodel.
     *
     * Called in screen space (after 3D world rendering).
     * Override to draw the current weapon.
     */
    void (*draw_weapon) (LrgFPSTemplate *self);

    /**
     * LrgFPSTemplateClass::draw_crosshair:
     * @self: a #LrgFPSTemplate
     *
     * Renders the crosshair.
     *
     * Default draws a simple cross at screen center.
     */
    void (*draw_crosshair) (LrgFPSTemplate *self);

    /**
     * LrgFPSTemplateClass::draw_hud:
     * @self: a #LrgFPSTemplate
     *
     * Renders the HUD (health, ammo, etc.).
     */
    void (*draw_hud) (LrgFPSTemplate *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Constructor
 * ========================================================================== */

/**
 * lrg_fps_template_new:
 *
 * Creates a new FPS game template with default settings.
 *
 * Returns: (transfer full): a new #LrgFPSTemplate
 */
LRG_AVAILABLE_IN_ALL
LrgFPSTemplate * lrg_fps_template_new (void);

/* ==========================================================================
 * Player Position
 * ========================================================================== */

/**
 * lrg_fps_template_get_position:
 * @self: a #LrgFPSTemplate
 * @x: (out) (optional): location for X
 * @y: (out) (optional): location for Y
 * @z: (out) (optional): location for Z
 *
 * Gets the player's world position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_fps_template_get_position (LrgFPSTemplate *self,
                                    gfloat         *x,
                                    gfloat         *y,
                                    gfloat         *z);

/**
 * lrg_fps_template_set_position:
 * @self: a #LrgFPSTemplate
 * @x: X position
 * @y: Y position
 * @z: Z position
 *
 * Sets the player's world position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_fps_template_set_position (LrgFPSTemplate *self,
                                    gfloat          x,
                                    gfloat          y,
                                    gfloat          z);

/* ==========================================================================
 * Movement Settings
 * ========================================================================== */

/**
 * lrg_fps_template_get_walk_speed:
 * @self: a #LrgFPSTemplate
 *
 * Gets the walking speed.
 *
 * Returns: walk speed in units per second
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_fps_template_get_walk_speed (LrgFPSTemplate *self);

/**
 * lrg_fps_template_set_walk_speed:
 * @self: a #LrgFPSTemplate
 * @speed: walk speed in units per second
 *
 * Sets the walking speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_fps_template_set_walk_speed (LrgFPSTemplate *self,
                                      gfloat          speed);

/**
 * lrg_fps_template_get_sprint_multiplier:
 * @self: a #LrgFPSTemplate
 *
 * Gets the sprint speed multiplier.
 *
 * Returns: sprint multiplier (e.g., 1.5 = 50% faster)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_fps_template_get_sprint_multiplier (LrgFPSTemplate *self);

/**
 * lrg_fps_template_set_sprint_multiplier:
 * @self: a #LrgFPSTemplate
 * @multiplier: sprint speed multiplier
 *
 * Sets the sprint speed multiplier.
 */
LRG_AVAILABLE_IN_ALL
void lrg_fps_template_set_sprint_multiplier (LrgFPSTemplate *self,
                                             gfloat          multiplier);

/**
 * lrg_fps_template_get_crouch_multiplier:
 * @self: a #LrgFPSTemplate
 *
 * Gets the crouch speed multiplier.
 *
 * Returns: crouch multiplier (e.g., 0.5 = 50% slower)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_fps_template_get_crouch_multiplier (LrgFPSTemplate *self);

/**
 * lrg_fps_template_set_crouch_multiplier:
 * @self: a #LrgFPSTemplate
 * @multiplier: crouch speed multiplier
 *
 * Sets the crouch speed multiplier.
 */
LRG_AVAILABLE_IN_ALL
void lrg_fps_template_set_crouch_multiplier (LrgFPSTemplate *self,
                                             gfloat          multiplier);

/**
 * lrg_fps_template_get_jump_height:
 * @self: a #LrgFPSTemplate
 *
 * Gets the jump height.
 *
 * Returns: jump height in world units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_fps_template_get_jump_height (LrgFPSTemplate *self);

/**
 * lrg_fps_template_set_jump_height:
 * @self: a #LrgFPSTemplate
 * @height: jump height in world units
 *
 * Sets the jump height.
 */
LRG_AVAILABLE_IN_ALL
void lrg_fps_template_set_jump_height (LrgFPSTemplate *self,
                                       gfloat          height);

/**
 * lrg_fps_template_get_gravity:
 * @self: a #LrgFPSTemplate
 *
 * Gets the gravity acceleration.
 *
 * Returns: gravity in units per second squared
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_fps_template_get_gravity (LrgFPSTemplate *self);

/**
 * lrg_fps_template_set_gravity:
 * @self: a #LrgFPSTemplate
 * @gravity: gravity acceleration
 *
 * Sets the gravity acceleration.
 */
LRG_AVAILABLE_IN_ALL
void lrg_fps_template_set_gravity (LrgFPSTemplate *self,
                                   gfloat          gravity);

/* ==========================================================================
 * Posture
 * ========================================================================== */

/**
 * lrg_fps_template_get_posture:
 * @self: a #LrgFPSTemplate
 *
 * Gets the current posture.
 *
 * Returns: the #LrgFPSPosture
 */
LRG_AVAILABLE_IN_ALL
LrgFPSPosture lrg_fps_template_get_posture (LrgFPSTemplate *self);

/**
 * lrg_fps_template_set_posture:
 * @self: a #LrgFPSTemplate
 * @posture: the new posture
 *
 * Sets the player posture.
 */
LRG_AVAILABLE_IN_ALL
void lrg_fps_template_set_posture (LrgFPSTemplate *self,
                                   LrgFPSPosture   posture);

/**
 * lrg_fps_template_is_sprinting:
 * @self: a #LrgFPSTemplate
 *
 * Checks if the player is currently sprinting.
 *
 * Returns: %TRUE if sprinting
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fps_template_is_sprinting (LrgFPSTemplate *self);

/**
 * lrg_fps_template_is_on_ground:
 * @self: a #LrgFPSTemplate
 *
 * Checks if the player is on the ground.
 *
 * Returns: %TRUE if on ground
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fps_template_is_on_ground (LrgFPSTemplate *self);

/* ==========================================================================
 * Player Height
 * ========================================================================== */

/**
 * lrg_fps_template_get_standing_height:
 * @self: a #LrgFPSTemplate
 *
 * Gets the eye height when standing.
 *
 * Returns: standing eye height
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_fps_template_get_standing_height (LrgFPSTemplate *self);

/**
 * lrg_fps_template_set_standing_height:
 * @self: a #LrgFPSTemplate
 * @height: standing eye height
 *
 * Sets the eye height when standing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_fps_template_set_standing_height (LrgFPSTemplate *self,
                                           gfloat          height);

/**
 * lrg_fps_template_get_crouch_height:
 * @self: a #LrgFPSTemplate
 *
 * Gets the eye height when crouching.
 *
 * Returns: crouching eye height
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_fps_template_get_crouch_height (LrgFPSTemplate *self);

/**
 * lrg_fps_template_set_crouch_height:
 * @self: a #LrgFPSTemplate
 * @height: crouching eye height
 *
 * Sets the eye height when crouching.
 */
LRG_AVAILABLE_IN_ALL
void lrg_fps_template_set_crouch_height (LrgFPSTemplate *self,
                                         gfloat          height);

/* ==========================================================================
 * Health System
 * ========================================================================== */

/**
 * lrg_fps_template_get_health:
 * @self: a #LrgFPSTemplate
 *
 * Gets the current health.
 *
 * Returns: current health
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_fps_template_get_health (LrgFPSTemplate *self);

/**
 * lrg_fps_template_set_health:
 * @self: a #LrgFPSTemplate
 * @health: health value (clamped to 0-max)
 *
 * Sets the current health.
 */
LRG_AVAILABLE_IN_ALL
void lrg_fps_template_set_health (LrgFPSTemplate *self,
                                  gfloat          health);

/**
 * lrg_fps_template_get_max_health:
 * @self: a #LrgFPSTemplate
 *
 * Gets the maximum health.
 *
 * Returns: max health
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_fps_template_get_max_health (LrgFPSTemplate *self);

/**
 * lrg_fps_template_set_max_health:
 * @self: a #LrgFPSTemplate
 * @max: maximum health
 *
 * Sets the maximum health.
 */
LRG_AVAILABLE_IN_ALL
void lrg_fps_template_set_max_health (LrgFPSTemplate *self,
                                      gfloat          max);

/**
 * lrg_fps_template_get_armor:
 * @self: a #LrgFPSTemplate
 *
 * Gets the current armor.
 *
 * Returns: current armor
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_fps_template_get_armor (LrgFPSTemplate *self);

/**
 * lrg_fps_template_set_armor:
 * @self: a #LrgFPSTemplate
 * @armor: armor value (clamped to 0-max)
 *
 * Sets the current armor.
 */
LRG_AVAILABLE_IN_ALL
void lrg_fps_template_set_armor (LrgFPSTemplate *self,
                                 gfloat          armor);

/**
 * lrg_fps_template_apply_damage:
 * @self: a #LrgFPSTemplate
 * @damage: damage amount
 * @source_x: damage source X
 * @source_y: damage source Y
 * @source_z: damage source Z
 *
 * Applies damage to the player.
 *
 * Damage is first absorbed by armor (if any), then applied to health.
 */
LRG_AVAILABLE_IN_ALL
void lrg_fps_template_apply_damage (LrgFPSTemplate *self,
                                    gfloat          damage,
                                    gfloat          source_x,
                                    gfloat          source_y,
                                    gfloat          source_z);

/**
 * lrg_fps_template_is_dead:
 * @self: a #LrgFPSTemplate
 *
 * Checks if the player is dead.
 *
 * Returns: %TRUE if health is 0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fps_template_is_dead (LrgFPSTemplate *self);

/* ==========================================================================
 * Weapon System
 * ========================================================================== */

/**
 * lrg_fps_template_get_current_weapon:
 * @self: a #LrgFPSTemplate
 *
 * Gets the current weapon index.
 *
 * Returns: current weapon index
 */
LRG_AVAILABLE_IN_ALL
gint lrg_fps_template_get_current_weapon (LrgFPSTemplate *self);

/**
 * lrg_fps_template_set_current_weapon:
 * @self: a #LrgFPSTemplate
 * @weapon_index: weapon index to switch to
 *
 * Switches to a weapon.
 */
LRG_AVAILABLE_IN_ALL
void lrg_fps_template_set_current_weapon (LrgFPSTemplate *self,
                                          gint            weapon_index);

/**
 * lrg_fps_template_get_ammo:
 * @self: a #LrgFPSTemplate
 *
 * Gets the current weapon's ammo count.
 *
 * Returns: ammo count
 */
LRG_AVAILABLE_IN_ALL
gint lrg_fps_template_get_ammo (LrgFPSTemplate *self);

/**
 * lrg_fps_template_set_ammo:
 * @self: a #LrgFPSTemplate
 * @ammo: ammo count
 *
 * Sets the current weapon's ammo count.
 */
LRG_AVAILABLE_IN_ALL
void lrg_fps_template_set_ammo (LrgFPSTemplate *self,
                                gint            ammo);

/**
 * lrg_fps_template_is_reloading:
 * @self: a #LrgFPSTemplate
 *
 * Checks if currently reloading.
 *
 * Returns: %TRUE if reloading
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fps_template_is_reloading (LrgFPSTemplate *self);

/* ==========================================================================
 * Head Bob
 * ========================================================================== */

/**
 * lrg_fps_template_get_head_bob_enabled:
 * @self: a #LrgFPSTemplate
 *
 * Gets whether head bob is enabled.
 *
 * Returns: %TRUE if head bob is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fps_template_get_head_bob_enabled (LrgFPSTemplate *self);

/**
 * lrg_fps_template_set_head_bob_enabled:
 * @self: a #LrgFPSTemplate
 * @enabled: whether to enable head bob
 *
 * Enables or disables head bob effect.
 */
LRG_AVAILABLE_IN_ALL
void lrg_fps_template_set_head_bob_enabled (LrgFPSTemplate *self,
                                            gboolean        enabled);

/**
 * lrg_fps_template_get_head_bob_intensity:
 * @self: a #LrgFPSTemplate
 *
 * Gets the head bob intensity.
 *
 * Returns: head bob intensity
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_fps_template_get_head_bob_intensity (LrgFPSTemplate *self);

/**
 * lrg_fps_template_set_head_bob_intensity:
 * @self: a #LrgFPSTemplate
 * @intensity: head bob intensity (0.0-1.0)
 *
 * Sets the head bob intensity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_fps_template_set_head_bob_intensity (LrgFPSTemplate *self,
                                              gfloat          intensity);

/* ==========================================================================
 * Crosshair
 * ========================================================================== */

/**
 * lrg_fps_template_get_crosshair_visible:
 * @self: a #LrgFPSTemplate
 *
 * Gets whether the crosshair is visible.
 *
 * Returns: %TRUE if crosshair is visible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_fps_template_get_crosshair_visible (LrgFPSTemplate *self);

/**
 * lrg_fps_template_set_crosshair_visible:
 * @self: a #LrgFPSTemplate
 * @visible: whether to show crosshair
 *
 * Sets crosshair visibility.
 */
LRG_AVAILABLE_IN_ALL
void lrg_fps_template_set_crosshair_visible (LrgFPSTemplate *self,
                                             gboolean        visible);

G_END_DECLS
