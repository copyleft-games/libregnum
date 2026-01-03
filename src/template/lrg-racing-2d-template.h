/* lrg-racing-2d-template.h - 2D top-down racing game template
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Base template for top-down 2D racing games.
 *
 * This template extends LrgGame2DTemplate with racing-specific features:
 * - Vehicle physics (acceleration, braking, steering, drift)
 * - Lap and checkpoint tracking
 * - Race state management (countdown, racing, finished)
 * - Speed boost mechanics
 * - Camera following with look-ahead
 *
 * Subclass this template for arcade racers, micro machines style games,
 * rally games, or any top-down racing game.
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

#define LRG_TYPE_RACING_2D_TEMPLATE (lrg_racing_2d_template_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgRacing2DTemplate, lrg_racing_2d_template,
                          LRG, RACING_2D_TEMPLATE, LrgGame2DTemplate)

/**
 * LrgRaceState:
 * @LRG_RACE_STATE_WAITING: Waiting to start (pre-race menu, etc.)
 * @LRG_RACE_STATE_COUNTDOWN: Race countdown (3, 2, 1, GO!)
 * @LRG_RACE_STATE_RACING: Race in progress
 * @LRG_RACE_STATE_FINISHED: Race complete
 * @LRG_RACE_STATE_PAUSED: Race paused
 *
 * Race state for race flow control.
 */
typedef enum
{
    LRG_RACE_STATE_WAITING = 0,
    LRG_RACE_STATE_COUNTDOWN,
    LRG_RACE_STATE_RACING,
    LRG_RACE_STATE_FINISHED,
    LRG_RACE_STATE_PAUSED
} LrgRaceState;

/**
 * LrgSurfaceType:
 * @LRG_SURFACE_ROAD: Normal road (full grip, full speed)
 * @LRG_SURFACE_OFFROAD: Off-road (reduced grip, reduced speed)
 * @LRG_SURFACE_ICE: Ice/snow (very low grip)
 * @LRG_SURFACE_BOOST: Speed boost pad
 * @LRG_SURFACE_SLOW: Slow zone (mud, sand)
 * @LRG_SURFACE_DAMAGE: Damaging surface (spikes, lava)
 *
 * Surface types that affect vehicle handling.
 */
typedef enum
{
    LRG_SURFACE_ROAD = 0,
    LRG_SURFACE_OFFROAD,
    LRG_SURFACE_ICE,
    LRG_SURFACE_BOOST,
    LRG_SURFACE_SLOW,
    LRG_SURFACE_DAMAGE
} LrgSurfaceType;

/**
 * LrgRacing2DTemplateClass:
 * @parent_class: parent class
 * @on_race_state_changed: called when race state changes
 * @on_lap_complete: called when a lap is completed
 * @on_checkpoint_passed: called when a checkpoint is passed
 * @on_countdown_tick: called each second during countdown
 * @on_collision: called when vehicle collides with something
 * @update_vehicle: updates vehicle physics
 * @get_surface_at: returns surface type at world position
 * @draw_vehicle: renders the player vehicle
 * @draw_race_ui: draws race HUD (speedometer, lap counter, etc.)
 *
 * Class structure for #LrgRacing2DTemplate.
 *
 * Subclasses should override these methods to implement
 * game-specific physics, track systems, and rendering.
 */
struct _LrgRacing2DTemplateClass
{
    LrgGame2DTemplateClass parent_class;

    /*< public >*/

    /**
     * LrgRacing2DTemplateClass::on_race_state_changed:
     * @self: a #LrgRacing2DTemplate
     * @old_state: previous race state
     * @new_state: new race state
     *
     * Called when the race state changes.
     *
     * Override to trigger state-specific audio, UI, or logic.
     */
    void (*on_race_state_changed) (LrgRacing2DTemplate *self,
                                   LrgRaceState         old_state,
                                   LrgRaceState         new_state);

    /**
     * LrgRacing2DTemplateClass::on_lap_complete:
     * @self: a #LrgRacing2DTemplate
     * @lap: the lap number just completed
     * @lap_time: time for this lap in seconds
     *
     * Called when a lap is completed.
     */
    void (*on_lap_complete) (LrgRacing2DTemplate *self,
                             guint                lap,
                             gfloat               lap_time);

    /**
     * LrgRacing2DTemplateClass::on_checkpoint_passed:
     * @self: a #LrgRacing2DTemplate
     * @checkpoint: checkpoint index
     *
     * Called when a checkpoint is passed.
     */
    void (*on_checkpoint_passed) (LrgRacing2DTemplate *self,
                                  guint                checkpoint);

    /**
     * LrgRacing2DTemplateClass::on_countdown_tick:
     * @self: a #LrgRacing2DTemplate
     * @count: current countdown value (3, 2, 1, 0=GO)
     *
     * Called each second during the pre-race countdown.
     */
    void (*on_countdown_tick) (LrgRacing2DTemplate *self,
                               gint                 count);

    /**
     * LrgRacing2DTemplateClass::on_collision:
     * @self: a #LrgRacing2DTemplate
     * @impact_speed: speed at impact
     *
     * Called when the vehicle collides with an obstacle.
     */
    void (*on_collision) (LrgRacing2DTemplate *self,
                          gfloat               impact_speed);

    /**
     * LrgRacing2DTemplateClass::update_vehicle:
     * @self: a #LrgRacing2DTemplate
     * @delta: time since last frame in seconds
     *
     * Updates vehicle physics.
     *
     * The default implementation handles acceleration, braking,
     * steering, and drift based on input and surface type.
     */
    void (*update_vehicle) (LrgRacing2DTemplate *self,
                            gdouble              delta);

    /**
     * LrgRacing2DTemplateClass::get_surface_at:
     * @self: a #LrgRacing2DTemplate
     * @x: world X coordinate
     * @y: world Y coordinate
     *
     * Returns the surface type at a world position.
     *
     * Override to implement track-specific surface detection.
     * The default returns LRG_SURFACE_ROAD.
     *
     * Returns: the #LrgSurfaceType at the position
     */
    LrgSurfaceType (*get_surface_at) (LrgRacing2DTemplate *self,
                                      gfloat               x,
                                      gfloat               y);

    /**
     * LrgRacing2DTemplateClass::draw_vehicle:
     * @self: a #LrgRacing2DTemplate
     *
     * Renders the player vehicle.
     *
     * The default draws a simple placeholder car.
     */
    void (*draw_vehicle) (LrgRacing2DTemplate *self);

    /**
     * LrgRacing2DTemplateClass::draw_race_ui:
     * @self: a #LrgRacing2DTemplate
     *
     * Draws the race HUD.
     *
     * The default draws speed, lap counter, and race time.
     */
    void (*draw_race_ui) (LrgRacing2DTemplate *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Constructor
 * ========================================================================== */

/**
 * lrg_racing_2d_template_new:
 *
 * Creates a new 2D racing game template with default settings.
 *
 * Returns: (transfer full): a new #LrgRacing2DTemplate
 */
LRG_AVAILABLE_IN_ALL
LrgRacing2DTemplate * lrg_racing_2d_template_new (void);

/* ==========================================================================
 * Race Control
 * ========================================================================== */

/**
 * lrg_racing_2d_template_get_race_state:
 * @self: a #LrgRacing2DTemplate
 *
 * Gets the current race state.
 *
 * Returns: the #LrgRaceState
 */
LRG_AVAILABLE_IN_ALL
LrgRaceState lrg_racing_2d_template_get_race_state (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_start_countdown:
 * @self: a #LrgRacing2DTemplate
 *
 * Starts the pre-race countdown.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_2d_template_start_countdown (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_start_race:
 * @self: a #LrgRacing2DTemplate
 *
 * Starts the race immediately (skips countdown).
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_2d_template_start_race (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_finish_race:
 * @self: a #LrgRacing2DTemplate
 *
 * Ends the race.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_2d_template_finish_race (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_pause_race:
 * @self: a #LrgRacing2DTemplate
 *
 * Pauses the race.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_2d_template_pause_race (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_resume_race:
 * @self: a #LrgRacing2DTemplate
 *
 * Resumes a paused race.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_2d_template_resume_race (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_reset_race:
 * @self: a #LrgRacing2DTemplate
 *
 * Resets the race to the waiting state.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_2d_template_reset_race (LrgRacing2DTemplate *self);

/* ==========================================================================
 * Vehicle Position/State
 * ========================================================================== */

/**
 * lrg_racing_2d_template_get_vehicle_x:
 * @self: a #LrgRacing2DTemplate
 *
 * Gets the vehicle X position.
 *
 * Returns: vehicle X coordinate
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_2d_template_get_vehicle_x (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_get_vehicle_y:
 * @self: a #LrgRacing2DTemplate
 *
 * Gets the vehicle Y position.
 *
 * Returns: vehicle Y coordinate
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_2d_template_get_vehicle_y (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_set_vehicle_position:
 * @self: a #LrgRacing2DTemplate
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Sets the vehicle position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_2d_template_set_vehicle_position (LrgRacing2DTemplate *self,
                                                  gfloat               x,
                                                  gfloat               y);

/**
 * lrg_racing_2d_template_get_vehicle_angle:
 * @self: a #LrgRacing2DTemplate
 *
 * Gets the vehicle heading angle in radians.
 *
 * Returns: heading angle (0 = right, PI/2 = down)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_2d_template_get_vehicle_angle (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_set_vehicle_angle:
 * @self: a #LrgRacing2DTemplate
 * @angle: heading angle in radians
 *
 * Sets the vehicle heading angle.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_2d_template_set_vehicle_angle (LrgRacing2DTemplate *self,
                                               gfloat               angle);

/**
 * lrg_racing_2d_template_get_speed:
 * @self: a #LrgRacing2DTemplate
 *
 * Gets the current vehicle speed.
 *
 * Returns: speed in units per second
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_2d_template_get_speed (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_is_drifting:
 * @self: a #LrgRacing2DTemplate
 *
 * Checks if the vehicle is currently drifting.
 *
 * Returns: %TRUE if drifting
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_racing_2d_template_is_drifting (LrgRacing2DTemplate *self);

/* ==========================================================================
 * Vehicle Settings
 * ========================================================================== */

/**
 * lrg_racing_2d_template_get_max_speed:
 * @self: a #LrgRacing2DTemplate
 *
 * Gets the maximum forward speed.
 *
 * Returns: max speed in units per second
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_2d_template_get_max_speed (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_set_max_speed:
 * @self: a #LrgRacing2DTemplate
 * @speed: max speed in units per second
 *
 * Sets the maximum forward speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_2d_template_set_max_speed (LrgRacing2DTemplate *self,
                                           gfloat               speed);

/**
 * lrg_racing_2d_template_get_acceleration:
 * @self: a #LrgRacing2DTemplate
 *
 * Gets the acceleration rate.
 *
 * Returns: acceleration in units per second squared
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_2d_template_get_acceleration (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_set_acceleration:
 * @self: a #LrgRacing2DTemplate
 * @accel: acceleration rate
 *
 * Sets the acceleration rate.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_2d_template_set_acceleration (LrgRacing2DTemplate *self,
                                              gfloat               accel);

/**
 * lrg_racing_2d_template_get_brake_power:
 * @self: a #LrgRacing2DTemplate
 *
 * Gets the braking deceleration rate.
 *
 * Returns: brake power
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_2d_template_get_brake_power (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_set_brake_power:
 * @self: a #LrgRacing2DTemplate
 * @power: braking power
 *
 * Sets the braking deceleration rate.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_2d_template_set_brake_power (LrgRacing2DTemplate *self,
                                             gfloat               power);

/**
 * lrg_racing_2d_template_get_turn_speed:
 * @self: a #LrgRacing2DTemplate
 *
 * Gets the steering turn rate.
 *
 * Returns: turn speed in radians per second
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_2d_template_get_turn_speed (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_set_turn_speed:
 * @self: a #LrgRacing2DTemplate
 * @speed: turn speed in radians per second
 *
 * Sets the steering turn rate.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_2d_template_set_turn_speed (LrgRacing2DTemplate *self,
                                            gfloat               speed);

/**
 * lrg_racing_2d_template_get_grip:
 * @self: a #LrgRacing2DTemplate
 *
 * Gets the base tire grip.
 *
 * Returns: grip value (0.0-1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_2d_template_get_grip (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_set_grip:
 * @self: a #LrgRacing2DTemplate
 * @grip: grip value (0.0-1.0)
 *
 * Sets the base tire grip.
 *
 * Lower grip means more sliding/drifting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_2d_template_set_grip (LrgRacing2DTemplate *self,
                                      gfloat               grip);

/**
 * lrg_racing_2d_template_get_drift_threshold:
 * @self: a #LrgRacing2DTemplate
 *
 * Gets the speed threshold for drifting.
 *
 * Returns: drift threshold speed
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_2d_template_get_drift_threshold (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_set_drift_threshold:
 * @self: a #LrgRacing2DTemplate
 * @threshold: speed threshold for drifting
 *
 * Sets the speed threshold above which drifting can occur.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_2d_template_set_drift_threshold (LrgRacing2DTemplate *self,
                                                 gfloat               threshold);

/* ==========================================================================
 * Lap/Checkpoint Tracking
 * ========================================================================== */

/**
 * lrg_racing_2d_template_get_lap:
 * @self: a #LrgRacing2DTemplate
 *
 * Gets the current lap number.
 *
 * Returns: current lap (1-based)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_racing_2d_template_get_lap (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_get_total_laps:
 * @self: a #LrgRacing2DTemplate
 *
 * Gets the total number of laps in the race.
 *
 * Returns: total laps
 */
LRG_AVAILABLE_IN_ALL
guint lrg_racing_2d_template_get_total_laps (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_set_total_laps:
 * @self: a #LrgRacing2DTemplate
 * @laps: number of laps
 *
 * Sets the total number of laps.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_2d_template_set_total_laps (LrgRacing2DTemplate *self,
                                            guint                laps);

/**
 * lrg_racing_2d_template_get_checkpoint:
 * @self: a #LrgRacing2DTemplate
 *
 * Gets the last passed checkpoint index.
 *
 * Returns: checkpoint index
 */
LRG_AVAILABLE_IN_ALL
guint lrg_racing_2d_template_get_checkpoint (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_set_total_checkpoints:
 * @self: a #LrgRacing2DTemplate
 * @checkpoints: number of checkpoints
 *
 * Sets the total number of checkpoints per lap.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_2d_template_set_total_checkpoints (LrgRacing2DTemplate *self,
                                                   guint                checkpoints);

/**
 * lrg_racing_2d_template_pass_checkpoint:
 * @self: a #LrgRacing2DTemplate
 * @checkpoint: checkpoint index
 *
 * Registers that a checkpoint was passed.
 *
 * The template validates that checkpoints are passed in order.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_2d_template_pass_checkpoint (LrgRacing2DTemplate *self,
                                             guint                checkpoint);

/* ==========================================================================
 * Time Tracking
 * ========================================================================== */

/**
 * lrg_racing_2d_template_get_race_time:
 * @self: a #LrgRacing2DTemplate
 *
 * Gets the total race time in seconds.
 *
 * Returns: race time
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_2d_template_get_race_time (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_get_lap_time:
 * @self: a #LrgRacing2DTemplate
 *
 * Gets the current lap time in seconds.
 *
 * Returns: current lap time
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_2d_template_get_lap_time (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_get_best_lap_time:
 * @self: a #LrgRacing2DTemplate
 *
 * Gets the best lap time in the current race.
 *
 * Returns: best lap time, or -1 if no lap completed
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_2d_template_get_best_lap_time (LrgRacing2DTemplate *self);

/* ==========================================================================
 * Boost System
 * ========================================================================== */

/**
 * lrg_racing_2d_template_get_boost:
 * @self: a #LrgRacing2DTemplate
 *
 * Gets the current boost amount (0.0-1.0).
 *
 * Returns: boost amount
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_2d_template_get_boost (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_set_boost:
 * @self: a #LrgRacing2DTemplate
 * @boost: boost amount (0.0-1.0)
 *
 * Sets the boost amount.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_2d_template_set_boost (LrgRacing2DTemplate *self,
                                       gfloat               boost);

/**
 * lrg_racing_2d_template_add_boost:
 * @self: a #LrgRacing2DTemplate
 * @amount: amount to add
 *
 * Adds to the boost meter (clamped to 0-1).
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_2d_template_add_boost (LrgRacing2DTemplate *self,
                                       gfloat               amount);

/**
 * lrg_racing_2d_template_is_boosting:
 * @self: a #LrgRacing2DTemplate
 *
 * Checks if boost is currently active.
 *
 * Returns: %TRUE if boosting
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_racing_2d_template_is_boosting (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_get_boost_multiplier:
 * @self: a #LrgRacing2DTemplate
 *
 * Gets the speed multiplier when boosting.
 *
 * Returns: boost multiplier (e.g., 1.5 = 50% faster)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_2d_template_get_boost_multiplier (LrgRacing2DTemplate *self);

/**
 * lrg_racing_2d_template_set_boost_multiplier:
 * @self: a #LrgRacing2DTemplate
 * @multiplier: speed multiplier when boosting
 *
 * Sets the boost speed multiplier.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_2d_template_set_boost_multiplier (LrgRacing2DTemplate *self,
                                                  gfloat               multiplier);

G_END_DECLS
