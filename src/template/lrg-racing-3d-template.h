/* lrg-racing-3d-template.h - 3D racing game template
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Base template for 3D racing games.
 *
 * This template extends LrgGame3DTemplate with 3D racing features:
 * - 3D vehicle physics with acceleration, braking, steering
 * - Chase camera with multiple view modes
 * - Race state management (countdown, racing, finished)
 * - Lap and checkpoint tracking
 * - Speed effects and nitro/boost system
 *
 * Subclass this template for racing games, driving simulators,
 * kart racers, or any vehicle-based 3D game.
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

#define LRG_TYPE_RACING_3D_TEMPLATE (lrg_racing_3d_template_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgRacing3DTemplate, lrg_racing_3d_template,
                          LRG, RACING_3D_TEMPLATE, LrgGame3DTemplate)

/**
 * LrgRacing3DRaceState:
 * @LRG_RACING_3D_RACE_STATE_WAITING: Waiting to start
 * @LRG_RACING_3D_RACE_STATE_COUNTDOWN: Countdown in progress
 * @LRG_RACING_3D_RACE_STATE_RACING: Race in progress
 * @LRG_RACING_3D_RACE_STATE_FINISHED: Race completed
 * @LRG_RACING_3D_RACE_STATE_PAUSED: Race paused
 *
 * States for 3D race progression.
 */
typedef enum
{
    LRG_RACING_3D_RACE_STATE_WAITING = 0,
    LRG_RACING_3D_RACE_STATE_COUNTDOWN,
    LRG_RACING_3D_RACE_STATE_RACING,
    LRG_RACING_3D_RACE_STATE_FINISHED,
    LRG_RACING_3D_RACE_STATE_PAUSED
} LrgRacing3DRaceState;

/**
 * LrgRacing3DCameraMode:
 * @LRG_RACING_3D_CAMERA_CHASE: Chase camera behind vehicle
 * @LRG_RACING_3D_CAMERA_HOOD: Hood/bonnet camera
 * @LRG_RACING_3D_CAMERA_BUMPER: Bumper camera
 * @LRG_RACING_3D_CAMERA_COCKPIT: Cockpit/interior camera
 * @LRG_RACING_3D_CAMERA_ORBIT: Free orbit camera
 *
 * Camera view modes for racing games.
 */
typedef enum
{
    LRG_RACING_3D_CAMERA_CHASE = 0,
    LRG_RACING_3D_CAMERA_HOOD,
    LRG_RACING_3D_CAMERA_BUMPER,
    LRG_RACING_3D_CAMERA_COCKPIT,
    LRG_RACING_3D_CAMERA_ORBIT
} LrgRacing3DCameraMode;

/**
 * LrgRacing3DTemplateClass:
 * @parent_class: parent class
 * @on_race_state_changed: called when race state changes
 * @on_lap_complete: called when a lap is completed
 * @on_checkpoint_reached: called when a checkpoint is reached
 * @on_collision: called when the vehicle collides
 * @on_boost_activated: called when boost/nitro is activated
 * @update_vehicle: updates vehicle physics
 * @update_camera: updates chase camera
 * @check_checkpoints: checks for checkpoint crossing
 * @draw_vehicle: renders the player vehicle
 * @draw_track: renders the track/world
 * @draw_speedometer: renders the speedometer
 * @draw_minimap: renders the race minimap
 * @draw_race_hud: renders lap counter, position, etc.
 *
 * Class structure for #LrgRacing3DTemplate.
 *
 * Subclasses should override these methods to implement
 * game-specific vehicle behavior, track rendering, and HUD.
 */
struct _LrgRacing3DTemplateClass
{
    LrgGame3DTemplateClass parent_class;

    /*< public >*/

    /**
     * LrgRacing3DTemplateClass::on_race_state_changed:
     * @self: a #LrgRacing3DTemplate
     * @old_state: previous race state
     * @new_state: new race state
     *
     * Called when the race state changes.
     */
    void (*on_race_state_changed) (LrgRacing3DTemplate   *self,
                                   LrgRacing3DRaceState   old_state,
                                   LrgRacing3DRaceState   new_state);

    /**
     * LrgRacing3DTemplateClass::on_lap_complete:
     * @self: a #LrgRacing3DTemplate
     * @lap: completed lap number (1-based)
     * @lap_time: time for the completed lap
     *
     * Called when a lap is completed.
     */
    void (*on_lap_complete) (LrgRacing3DTemplate *self,
                             gint                 lap,
                             gfloat               lap_time);

    /**
     * LrgRacing3DTemplateClass::on_checkpoint_reached:
     * @self: a #LrgRacing3DTemplate
     * @checkpoint: checkpoint index (0-based)
     *
     * Called when a checkpoint is reached.
     */
    void (*on_checkpoint_reached) (LrgRacing3DTemplate *self,
                                   gint                 checkpoint);

    /**
     * LrgRacing3DTemplateClass::on_collision:
     * @self: a #LrgRacing3DTemplate
     * @impact_force: force of the collision
     * @normal_x: collision normal X
     * @normal_y: collision normal Y
     * @normal_z: collision normal Z
     *
     * Called when the vehicle collides with something.
     */
    void (*on_collision) (LrgRacing3DTemplate *self,
                          gfloat               impact_force,
                          gfloat               normal_x,
                          gfloat               normal_y,
                          gfloat               normal_z);

    /**
     * LrgRacing3DTemplateClass::on_boost_activated:
     * @self: a #LrgRacing3DTemplate
     *
     * Called when boost/nitro is activated.
     */
    void (*on_boost_activated) (LrgRacing3DTemplate *self);

    /**
     * LrgRacing3DTemplateClass::update_vehicle:
     * @self: a #LrgRacing3DTemplate
     * @delta: time since last frame in seconds
     *
     * Updates vehicle physics.
     *
     * Default handles acceleration, braking, steering, and gravity.
     */
    void (*update_vehicle) (LrgRacing3DTemplate *self,
                            gdouble              delta);

    /**
     * LrgRacing3DTemplateClass::update_chase_camera:
     * @self: a #LrgRacing3DTemplate
     * @delta: time since last frame in seconds
     *
     * Updates the chase camera to follow the vehicle.
     */
    void (*update_chase_camera) (LrgRacing3DTemplate *self,
                                 gdouble              delta);

    /**
     * LrgRacing3DTemplateClass::check_checkpoints:
     * @self: a #LrgRacing3DTemplate
     *
     * Checks if the vehicle has crossed any checkpoints.
     *
     * Override to implement custom checkpoint detection.
     */
    void (*check_checkpoints) (LrgRacing3DTemplate *self);

    /**
     * LrgRacing3DTemplateClass::draw_vehicle:
     * @self: a #LrgRacing3DTemplate
     *
     * Renders the player's vehicle.
     */
    void (*draw_vehicle) (LrgRacing3DTemplate *self);

    /**
     * LrgRacing3DTemplateClass::draw_track:
     * @self: a #LrgRacing3DTemplate
     *
     * Renders the race track.
     */
    void (*draw_track) (LrgRacing3DTemplate *self);

    /**
     * LrgRacing3DTemplateClass::draw_speedometer:
     * @self: a #LrgRacing3DTemplate
     *
     * Renders the speedometer.
     */
    void (*draw_speedometer) (LrgRacing3DTemplate *self);

    /**
     * LrgRacing3DTemplateClass::draw_minimap:
     * @self: a #LrgRacing3DTemplate
     *
     * Renders the race minimap.
     */
    void (*draw_minimap) (LrgRacing3DTemplate *self);

    /**
     * LrgRacing3DTemplateClass::draw_race_hud:
     * @self: a #LrgRacing3DTemplate
     *
     * Renders the race HUD (lap counter, position, timer).
     */
    void (*draw_race_hud) (LrgRacing3DTemplate *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Constructor
 * ========================================================================== */

/**
 * lrg_racing_3d_template_new:
 *
 * Creates a new 3D racing game template with default settings.
 *
 * Returns: (transfer full): a new #LrgRacing3DTemplate
 */
LRG_AVAILABLE_IN_ALL
LrgRacing3DTemplate * lrg_racing_3d_template_new (void);

/* ==========================================================================
 * Vehicle Position
 * ========================================================================== */

/**
 * lrg_racing_3d_template_get_position:
 * @self: a #LrgRacing3DTemplate
 * @x: (out) (optional): location for X
 * @y: (out) (optional): location for Y
 * @z: (out) (optional): location for Z
 *
 * Gets the vehicle's world position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_get_position (LrgRacing3DTemplate *self,
                                          gfloat              *x,
                                          gfloat              *y,
                                          gfloat              *z);

/**
 * lrg_racing_3d_template_set_position:
 * @self: a #LrgRacing3DTemplate
 * @x: X position
 * @y: Y position
 * @z: Z position
 *
 * Sets the vehicle's world position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_set_position (LrgRacing3DTemplate *self,
                                          gfloat               x,
                                          gfloat               y,
                                          gfloat               z);

/**
 * lrg_racing_3d_template_get_rotation:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the vehicle's Y rotation (heading) in degrees.
 *
 * Returns: rotation angle in degrees
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_3d_template_get_rotation (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_set_rotation:
 * @self: a #LrgRacing3DTemplate
 * @rotation: rotation angle in degrees
 *
 * Sets the vehicle's Y rotation (heading).
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_set_rotation (LrgRacing3DTemplate *self,
                                          gfloat               rotation);

/* ==========================================================================
 * Vehicle Physics
 * ========================================================================== */

/**
 * lrg_racing_3d_template_get_speed:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the current vehicle speed.
 *
 * Returns: speed in units per second
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_3d_template_get_speed (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_get_max_speed:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the maximum vehicle speed.
 *
 * Returns: max speed in units per second
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_3d_template_get_max_speed (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_set_max_speed:
 * @self: a #LrgRacing3DTemplate
 * @speed: max speed in units per second
 *
 * Sets the maximum vehicle speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_set_max_speed (LrgRacing3DTemplate *self,
                                           gfloat               speed);

/**
 * lrg_racing_3d_template_get_acceleration:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the vehicle acceleration.
 *
 * Returns: acceleration in units per second squared
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_3d_template_get_acceleration (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_set_acceleration:
 * @self: a #LrgRacing3DTemplate
 * @accel: acceleration in units per second squared
 *
 * Sets the vehicle acceleration.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_set_acceleration (LrgRacing3DTemplate *self,
                                              gfloat               accel);

/**
 * lrg_racing_3d_template_get_brake_power:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the brake deceleration.
 *
 * Returns: brake power in units per second squared
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_3d_template_get_brake_power (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_set_brake_power:
 * @self: a #LrgRacing3DTemplate
 * @power: brake power in units per second squared
 *
 * Sets the brake deceleration.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_set_brake_power (LrgRacing3DTemplate *self,
                                             gfloat               power);

/**
 * lrg_racing_3d_template_get_steering_speed:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the steering speed.
 *
 * Returns: steering speed in degrees per second
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_3d_template_get_steering_speed (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_set_steering_speed:
 * @self: a #LrgRacing3DTemplate
 * @speed: steering speed in degrees per second
 *
 * Sets the steering speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_set_steering_speed (LrgRacing3DTemplate *self,
                                                gfloat               speed);

/**
 * lrg_racing_3d_template_get_grip:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the tire grip factor.
 *
 * Returns: grip (0.0-1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_3d_template_get_grip (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_set_grip:
 * @self: a #LrgRacing3DTemplate
 * @grip: grip factor (0.0-1.0)
 *
 * Sets the tire grip factor.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_set_grip (LrgRacing3DTemplate *self,
                                      gfloat               grip);

/**
 * lrg_racing_3d_template_is_grounded:
 * @self: a #LrgRacing3DTemplate
 *
 * Checks if the vehicle is on the ground.
 *
 * Returns: %TRUE if grounded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_racing_3d_template_is_grounded (LrgRacing3DTemplate *self);

/* ==========================================================================
 * Boost System
 * ========================================================================== */

/**
 * lrg_racing_3d_template_get_boost:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the current boost amount.
 *
 * Returns: boost (0.0-1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_3d_template_get_boost (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_set_boost:
 * @self: a #LrgRacing3DTemplate
 * @boost: boost amount (0.0-1.0)
 *
 * Sets the current boost amount.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_set_boost (LrgRacing3DTemplate *self,
                                       gfloat               boost);

/**
 * lrg_racing_3d_template_get_boost_speed:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the speed multiplier when boosting.
 *
 * Returns: boost speed multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_3d_template_get_boost_speed (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_set_boost_speed:
 * @self: a #LrgRacing3DTemplate
 * @multiplier: boost speed multiplier
 *
 * Sets the speed multiplier when boosting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_set_boost_speed (LrgRacing3DTemplate *self,
                                             gfloat               multiplier);

/**
 * lrg_racing_3d_template_is_boosting:
 * @self: a #LrgRacing3DTemplate
 *
 * Checks if boost is currently active.
 *
 * Returns: %TRUE if boosting
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_racing_3d_template_is_boosting (LrgRacing3DTemplate *self);

/* ==========================================================================
 * Camera
 * ========================================================================== */

/**
 * lrg_racing_3d_template_get_camera_mode:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the current camera mode.
 *
 * Returns: the #LrgRacing3DCameraMode
 */
LRG_AVAILABLE_IN_ALL
LrgRacing3DCameraMode lrg_racing_3d_template_get_camera_mode (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_set_camera_mode:
 * @self: a #LrgRacing3DTemplate
 * @mode: the camera mode
 *
 * Sets the camera mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_set_camera_mode (LrgRacing3DTemplate  *self,
                                             LrgRacing3DCameraMode mode);

/**
 * lrg_racing_3d_template_cycle_camera:
 * @self: a #LrgRacing3DTemplate
 *
 * Cycles to the next camera mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_cycle_camera (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_get_chase_distance:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the chase camera distance.
 *
 * Returns: distance in world units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_3d_template_get_chase_distance (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_set_chase_distance:
 * @self: a #LrgRacing3DTemplate
 * @distance: distance in world units
 *
 * Sets the chase camera distance.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_set_chase_distance (LrgRacing3DTemplate *self,
                                                gfloat               distance);

/**
 * lrg_racing_3d_template_get_chase_height:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the chase camera height.
 *
 * Returns: height in world units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_3d_template_get_chase_height (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_set_chase_height:
 * @self: a #LrgRacing3DTemplate
 * @height: height in world units
 *
 * Sets the chase camera height.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_set_chase_height (LrgRacing3DTemplate *self,
                                              gfloat               height);

/* ==========================================================================
 * Race State
 * ========================================================================== */

/**
 * lrg_racing_3d_template_get_race_state:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the current race state.
 *
 * Returns: the #LrgRacing3DRaceState
 */
LRG_AVAILABLE_IN_ALL
LrgRacing3DRaceState lrg_racing_3d_template_get_race_state (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_set_race_state:
 * @self: a #LrgRacing3DTemplate
 * @state: the race state
 *
 * Sets the race state.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_set_race_state (LrgRacing3DTemplate  *self,
                                            LrgRacing3DRaceState  state);

/**
 * lrg_racing_3d_template_start_countdown:
 * @self: a #LrgRacing3DTemplate
 *
 * Starts the race countdown.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_start_countdown (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_get_countdown:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the remaining countdown value.
 *
 * Returns: countdown value (3, 2, 1, 0 = GO)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_racing_3d_template_get_countdown (LrgRacing3DTemplate *self);

/* ==========================================================================
 * Race Progress
 * ========================================================================== */

/**
 * lrg_racing_3d_template_get_current_lap:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the current lap number (1-based).
 *
 * Returns: current lap
 */
LRG_AVAILABLE_IN_ALL
gint lrg_racing_3d_template_get_current_lap (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_get_total_laps:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the total number of laps.
 *
 * Returns: total laps
 */
LRG_AVAILABLE_IN_ALL
gint lrg_racing_3d_template_get_total_laps (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_set_total_laps:
 * @self: a #LrgRacing3DTemplate
 * @laps: total number of laps
 *
 * Sets the total number of laps.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_set_total_laps (LrgRacing3DTemplate *self,
                                            gint                 laps);

/**
 * lrg_racing_3d_template_get_race_time:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the total race time.
 *
 * Returns: race time in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_3d_template_get_race_time (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_get_lap_time:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the current lap time.
 *
 * Returns: lap time in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_3d_template_get_lap_time (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_get_best_lap_time:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the best lap time.
 *
 * Returns: best lap time in seconds (-1 if no lap completed)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_racing_3d_template_get_best_lap_time (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_get_current_checkpoint:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the last reached checkpoint index.
 *
 * Returns: checkpoint index (0-based)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_racing_3d_template_get_current_checkpoint (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_get_total_checkpoints:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the total number of checkpoints.
 *
 * Returns: total checkpoints
 */
LRG_AVAILABLE_IN_ALL
gint lrg_racing_3d_template_get_total_checkpoints (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_set_total_checkpoints:
 * @self: a #LrgRacing3DTemplate
 * @checkpoints: total number of checkpoints
 *
 * Sets the total number of checkpoints.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_set_total_checkpoints (LrgRacing3DTemplate *self,
                                                   gint                 checkpoints);

/**
 * lrg_racing_3d_template_reach_checkpoint:
 * @self: a #LrgRacing3DTemplate
 * @checkpoint: checkpoint index to mark as reached
 *
 * Marks a checkpoint as reached (for external collision systems).
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_reach_checkpoint (LrgRacing3DTemplate *self,
                                              gint                 checkpoint);

/* ==========================================================================
 * Race Position
 * ========================================================================== */

/**
 * lrg_racing_3d_template_get_race_position:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the current race position (1st, 2nd, etc.).
 *
 * Returns: race position (1-based)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_racing_3d_template_get_race_position (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_set_race_position:
 * @self: a #LrgRacing3DTemplate
 * @position: race position (1-based)
 *
 * Sets the race position (for external ranking systems).
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_set_race_position (LrgRacing3DTemplate *self,
                                               gint                 position);

/**
 * lrg_racing_3d_template_get_total_racers:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets the total number of racers.
 *
 * Returns: total racers
 */
LRG_AVAILABLE_IN_ALL
gint lrg_racing_3d_template_get_total_racers (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_set_total_racers:
 * @self: a #LrgRacing3DTemplate
 * @count: total number of racers
 *
 * Sets the total number of racers.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_set_total_racers (LrgRacing3DTemplate *self,
                                              gint                 count);

/* ==========================================================================
 * HUD Options
 * ========================================================================== */

/**
 * lrg_racing_3d_template_get_speedometer_visible:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets whether the speedometer is visible.
 *
 * Returns: %TRUE if visible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_racing_3d_template_get_speedometer_visible (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_set_speedometer_visible:
 * @self: a #LrgRacing3DTemplate
 * @visible: whether to show speedometer
 *
 * Sets speedometer visibility.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_set_speedometer_visible (LrgRacing3DTemplate *self,
                                                     gboolean             visible);

/**
 * lrg_racing_3d_template_get_minimap_visible:
 * @self: a #LrgRacing3DTemplate
 *
 * Gets whether the minimap is visible.
 *
 * Returns: %TRUE if visible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_racing_3d_template_get_minimap_visible (LrgRacing3DTemplate *self);

/**
 * lrg_racing_3d_template_set_minimap_visible:
 * @self: a #LrgRacing3DTemplate
 * @visible: whether to show minimap
 *
 * Sets minimap visibility.
 */
LRG_AVAILABLE_IN_ALL
void lrg_racing_3d_template_set_minimap_visible (LrgRacing3DTemplate *self,
                                                 gboolean             visible);

G_END_DECLS
