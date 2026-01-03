/* lrg-twin-stick-template.h - Twin-stick shooter game template
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * Template for twin-stick shooter games.
 *
 * This template extends LrgShooter2DTemplate with twin-stick controls:
 * - Left stick / WASD controls player movement
 * - Right stick / mouse controls aim direction
 * - Continuous firing in aim direction
 * - 360-degree aiming
 * - Gamepad and keyboard+mouse input support
 *
 * Use for games like Geometry Wars, Enter the Gungeon, or Binding of Isaac.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-shooter-2d-template.h"

G_BEGIN_DECLS

#define LRG_TYPE_TWIN_STICK_TEMPLATE (lrg_twin_stick_template_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgTwinStickTemplate, lrg_twin_stick_template,
                          LRG, TWIN_STICK_TEMPLATE, LrgShooter2DTemplate)

/**
 * LrgTwinStickTemplateClass:
 * @parent_class: parent class
 * @on_dash_started: called when a dash begins
 * @on_dash_ended: called when a dash ends
 * @update_aim: update the aim direction from input
 * @update_movement: update movement from input
 *
 * Class structure for #LrgTwinStickTemplate.
 *
 * Subclasses can override these virtual methods to customize
 * twin-stick behavior.
 */
struct _LrgTwinStickTemplateClass
{
    LrgShooter2DTemplateClass parent_class;

    /*< public >*/

    /**
     * LrgTwinStickTemplateClass::on_dash_started:
     * @self: a #LrgTwinStickTemplate
     * @direction_x: dash direction X component
     * @direction_y: dash direction Y component
     *
     * Called when a dash begins. Override to add dash effects.
     */
    void (*on_dash_started) (LrgTwinStickTemplate *self,
                             gfloat                direction_x,
                             gfloat                direction_y);

    /**
     * LrgTwinStickTemplateClass::on_dash_ended:
     * @self: a #LrgTwinStickTemplate
     *
     * Called when a dash ends.
     */
    void (*on_dash_ended) (LrgTwinStickTemplate *self);

    /**
     * LrgTwinStickTemplateClass::update_aim:
     * @self: a #LrgTwinStickTemplate
     * @delta: time since last frame
     *
     * Updates aim direction from input. Override for custom aim behavior.
     */
    void (*update_aim) (LrgTwinStickTemplate *self,
                        gdouble               delta);

    /**
     * LrgTwinStickTemplateClass::update_movement:
     * @self: a #LrgTwinStickTemplate
     * @delta: time since last frame
     *
     * Updates movement from input. Override for custom movement.
     */
    void (*update_movement) (LrgTwinStickTemplate *self,
                             gdouble               delta);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Constructor
 * ========================================================================== */

/**
 * lrg_twin_stick_template_new:
 *
 * Creates a new twin-stick shooter template.
 *
 * Returns: (transfer full): a new #LrgTwinStickTemplate
 */
LRG_AVAILABLE_IN_ALL
LrgTwinStickTemplate * lrg_twin_stick_template_new (void);

/* ==========================================================================
 * Aim Direction
 * ========================================================================== */

/**
 * lrg_twin_stick_template_get_aim_direction:
 * @self: a #LrgTwinStickTemplate
 * @x: (out) (optional): location for aim X component
 * @y: (out) (optional): location for aim Y component
 *
 * Gets the current aim direction (normalized).
 */
LRG_AVAILABLE_IN_ALL
void lrg_twin_stick_template_get_aim_direction (LrgTwinStickTemplate *self,
                                                 gfloat               *x,
                                                 gfloat               *y);

/**
 * lrg_twin_stick_template_set_aim_direction:
 * @self: a #LrgTwinStickTemplate
 * @x: aim X component
 * @y: aim Y component
 *
 * Sets the aim direction. Will be normalized automatically.
 */
LRG_AVAILABLE_IN_ALL
void lrg_twin_stick_template_set_aim_direction (LrgTwinStickTemplate *self,
                                                 gfloat                x,
                                                 gfloat                y);

/**
 * lrg_twin_stick_template_get_aim_angle:
 * @self: a #LrgTwinStickTemplate
 *
 * Gets the aim angle in radians (0 = right, PI/2 = down).
 *
 * Returns: aim angle in radians
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_twin_stick_template_get_aim_angle (LrgTwinStickTemplate *self);

/**
 * lrg_twin_stick_template_set_aim_angle:
 * @self: a #LrgTwinStickTemplate
 * @angle: aim angle in radians
 *
 * Sets the aim direction from an angle.
 */
LRG_AVAILABLE_IN_ALL
void lrg_twin_stick_template_set_aim_angle (LrgTwinStickTemplate *self,
                                             gfloat                angle);

/* ==========================================================================
 * Movement
 * ========================================================================== */

/**
 * lrg_twin_stick_template_get_move_direction:
 * @self: a #LrgTwinStickTemplate
 * @x: (out) (optional): location for move X component
 * @y: (out) (optional): location for move Y component
 *
 * Gets the current movement direction (may not be normalized).
 */
LRG_AVAILABLE_IN_ALL
void lrg_twin_stick_template_get_move_direction (LrgTwinStickTemplate *self,
                                                  gfloat               *x,
                                                  gfloat               *y);

/**
 * lrg_twin_stick_template_set_move_direction:
 * @self: a #LrgTwinStickTemplate
 * @x: move X component (-1 to 1)
 * @y: move Y component (-1 to 1)
 *
 * Sets the movement direction. Values are clamped to -1..1 range.
 */
LRG_AVAILABLE_IN_ALL
void lrg_twin_stick_template_set_move_direction (LrgTwinStickTemplate *self,
                                                  gfloat                x,
                                                  gfloat                y);

/* ==========================================================================
 * Input Settings
 * ========================================================================== */

/**
 * lrg_twin_stick_template_get_aim_deadzone:
 * @self: a #LrgTwinStickTemplate
 *
 * Gets the gamepad stick deadzone for aiming.
 *
 * Returns: deadzone value (0.0-1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_twin_stick_template_get_aim_deadzone (LrgTwinStickTemplate *self);

/**
 * lrg_twin_stick_template_set_aim_deadzone:
 * @self: a #LrgTwinStickTemplate
 * @deadzone: deadzone value (0.0-1.0)
 *
 * Sets the gamepad stick deadzone for aiming.
 */
LRG_AVAILABLE_IN_ALL
void lrg_twin_stick_template_set_aim_deadzone (LrgTwinStickTemplate *self,
                                                gfloat                deadzone);

/**
 * lrg_twin_stick_template_get_move_deadzone:
 * @self: a #LrgTwinStickTemplate
 *
 * Gets the gamepad stick deadzone for movement.
 *
 * Returns: deadzone value (0.0-1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_twin_stick_template_get_move_deadzone (LrgTwinStickTemplate *self);

/**
 * lrg_twin_stick_template_set_move_deadzone:
 * @self: a #LrgTwinStickTemplate
 * @deadzone: deadzone value (0.0-1.0)
 *
 * Sets the gamepad stick deadzone for movement.
 */
LRG_AVAILABLE_IN_ALL
void lrg_twin_stick_template_set_move_deadzone (LrgTwinStickTemplate *self,
                                                 gfloat                deadzone);

/**
 * lrg_twin_stick_template_get_fire_threshold:
 * @self: a #LrgTwinStickTemplate
 *
 * Gets the minimum aim magnitude to trigger firing.
 *
 * Returns: fire threshold (0.0-1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_twin_stick_template_get_fire_threshold (LrgTwinStickTemplate *self);

/**
 * lrg_twin_stick_template_set_fire_threshold:
 * @self: a #LrgTwinStickTemplate
 * @threshold: fire threshold (0.0-1.0)
 *
 * Sets the minimum aim magnitude required to fire.
 * Set to 0 to always fire if any aim input is present.
 */
LRG_AVAILABLE_IN_ALL
void lrg_twin_stick_template_set_fire_threshold (LrgTwinStickTemplate *self,
                                                  gfloat                threshold);

/* ==========================================================================
 * Input Mode
 * ========================================================================== */

/**
 * LrgTwinStickAimMode:
 * @LRG_TWIN_STICK_AIM_STICK: Aim using right stick (gamepad)
 * @LRG_TWIN_STICK_AIM_MOUSE: Aim toward mouse cursor
 * @LRG_TWIN_STICK_AIM_HYBRID: Auto-switch based on last input
 *
 * Aiming input modes.
 */
typedef enum
{
    LRG_TWIN_STICK_AIM_STICK,
    LRG_TWIN_STICK_AIM_MOUSE,
    LRG_TWIN_STICK_AIM_HYBRID
} LrgTwinStickAimMode;

/**
 * lrg_twin_stick_template_get_aim_mode:
 * @self: a #LrgTwinStickTemplate
 *
 * Gets the current aiming input mode.
 *
 * Returns: the #LrgTwinStickAimMode
 */
LRG_AVAILABLE_IN_ALL
LrgTwinStickAimMode lrg_twin_stick_template_get_aim_mode (LrgTwinStickTemplate *self);

/**
 * lrg_twin_stick_template_set_aim_mode:
 * @self: a #LrgTwinStickTemplate
 * @mode: the aiming input mode
 *
 * Sets the aiming input mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_twin_stick_template_set_aim_mode (LrgTwinStickTemplate *self,
                                            LrgTwinStickAimMode   mode);

/* ==========================================================================
 * Dash / Dodge
 * ========================================================================== */

/**
 * lrg_twin_stick_template_get_dash_speed:
 * @self: a #LrgTwinStickTemplate
 *
 * Gets the dash speed multiplier.
 *
 * Returns: dash speed multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_twin_stick_template_get_dash_speed (LrgTwinStickTemplate *self);

/**
 * lrg_twin_stick_template_set_dash_speed:
 * @self: a #LrgTwinStickTemplate
 * @speed: dash speed multiplier (1.0 = same as normal speed)
 *
 * Sets the dash speed multiplier.
 */
LRG_AVAILABLE_IN_ALL
void lrg_twin_stick_template_set_dash_speed (LrgTwinStickTemplate *self,
                                              gfloat                speed);

/**
 * lrg_twin_stick_template_get_dash_duration:
 * @self: a #LrgTwinStickTemplate
 *
 * Gets the dash duration in seconds.
 *
 * Returns: dash duration
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_twin_stick_template_get_dash_duration (LrgTwinStickTemplate *self);

/**
 * lrg_twin_stick_template_set_dash_duration:
 * @self: a #LrgTwinStickTemplate
 * @duration: dash duration in seconds
 *
 * Sets the dash duration.
 */
LRG_AVAILABLE_IN_ALL
void lrg_twin_stick_template_set_dash_duration (LrgTwinStickTemplate *self,
                                                 gfloat                duration);

/**
 * lrg_twin_stick_template_get_dash_cooldown:
 * @self: a #LrgTwinStickTemplate
 *
 * Gets the dash cooldown time.
 *
 * Returns: dash cooldown in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_twin_stick_template_get_dash_cooldown (LrgTwinStickTemplate *self);

/**
 * lrg_twin_stick_template_set_dash_cooldown:
 * @self: a #LrgTwinStickTemplate
 * @cooldown: dash cooldown in seconds
 *
 * Sets the dash cooldown time.
 */
LRG_AVAILABLE_IN_ALL
void lrg_twin_stick_template_set_dash_cooldown (LrgTwinStickTemplate *self,
                                                 gfloat                cooldown);

/**
 * lrg_twin_stick_template_can_dash:
 * @self: a #LrgTwinStickTemplate
 *
 * Checks if the player can currently dash.
 *
 * Returns: %TRUE if dash is available
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_twin_stick_template_can_dash (LrgTwinStickTemplate *self);

/**
 * lrg_twin_stick_template_is_dashing:
 * @self: a #LrgTwinStickTemplate
 *
 * Checks if the player is currently dashing.
 *
 * Returns: %TRUE if currently dashing
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_twin_stick_template_is_dashing (LrgTwinStickTemplate *self);

/**
 * lrg_twin_stick_template_dash:
 * @self: a #LrgTwinStickTemplate
 *
 * Initiates a dash in the current movement direction.
 *
 * Returns: %TRUE if dash was initiated
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_twin_stick_template_dash (LrgTwinStickTemplate *self);

G_END_DECLS
