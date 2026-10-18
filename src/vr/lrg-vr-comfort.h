/* lrg-vr-comfort.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * VR comfort settings for motion sickness mitigation.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_VR_COMFORT_SETTINGS (lrg_vr_comfort_settings_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVRComfortSettings, lrg_vr_comfort_settings, LRG, VR_COMFORT_SETTINGS, GObject)

/**
 * lrg_vr_comfort_settings_new:
 *
 * Creates new VR comfort settings with defaults.
 *
 * Returns: (transfer full): a new #LrgVRComfortSettings
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVRComfortSettings * lrg_vr_comfort_settings_new (void);

/**
 * lrg_vr_comfort_settings_get_turn_mode:
 * @self: a #LrgVRComfortSettings
 *
 * Gets the turning mode.
 *
 * Returns: the #LrgVRTurnMode
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVRTurnMode lrg_vr_comfort_settings_get_turn_mode (LrgVRComfortSettings *self);

/**
 * lrg_vr_comfort_settings_set_turn_mode:
 * @self: a #LrgVRComfortSettings
 * @mode: the #LrgVRTurnMode
 *
 * Sets the turning mode.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_comfort_settings_set_turn_mode (LrgVRComfortSettings *self,
                                            LrgVRTurnMode         mode);

/**
 * lrg_vr_comfort_settings_get_snap_turn_angle:
 * @self: a #LrgVRComfortSettings
 *
 * Gets the snap turn angle in degrees.
 *
 * Returns: snap turn angle
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_vr_comfort_settings_get_snap_turn_angle (LrgVRComfortSettings *self);

/**
 * lrg_vr_comfort_settings_set_snap_turn_angle:
 * @self: a #LrgVRComfortSettings
 * @angle: snap turn angle in degrees
 *
 * Sets the snap turn angle.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_comfort_settings_set_snap_turn_angle (LrgVRComfortSettings *self,
                                                  gfloat                angle);

/**
 * lrg_vr_comfort_settings_get_locomotion_mode:
 * @self: a #LrgVRComfortSettings
 *
 * Gets the locomotion mode.
 *
 * Returns: the #LrgVRLocomotionMode
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVRLocomotionMode lrg_vr_comfort_settings_get_locomotion_mode (LrgVRComfortSettings *self);

/**
 * lrg_vr_comfort_settings_set_locomotion_mode:
 * @self: a #LrgVRComfortSettings
 * @mode: the #LrgVRLocomotionMode
 *
 * Sets the locomotion mode.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_comfort_settings_set_locomotion_mode (LrgVRComfortSettings *self,
                                                  LrgVRLocomotionMode   mode);

/**
 * lrg_vr_comfort_settings_get_vignette_enabled:
 * @self: a #LrgVRComfortSettings
 *
 * Gets whether comfort vignette is enabled.
 *
 * Returns: %TRUE if vignette is enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_comfort_settings_get_vignette_enabled (LrgVRComfortSettings *self);

/**
 * lrg_vr_comfort_settings_set_vignette_enabled:
 * @self: a #LrgVRComfortSettings
 * @enabled: whether to enable vignette
 *
 * Sets whether comfort vignette is enabled.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_comfort_settings_set_vignette_enabled (LrgVRComfortSettings *self,
                                                   gboolean              enabled);

/**
 * lrg_vr_comfort_settings_get_vignette_intensity:
 * @self: a #LrgVRComfortSettings
 *
 * Gets the vignette intensity.
 *
 * Returns: vignette intensity (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_vr_comfort_settings_get_vignette_intensity (LrgVRComfortSettings *self);

/**
 * lrg_vr_comfort_settings_set_vignette_intensity:
 * @self: a #LrgVRComfortSettings
 * @intensity: vignette intensity (0.0 to 1.0)
 *
 * Sets the vignette intensity.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_comfort_settings_set_vignette_intensity (LrgVRComfortSettings *self,
                                                     gfloat                intensity);

/**
 * lrg_vr_comfort_settings_get_height_adjustment:
 * @self: a #LrgVRComfortSettings
 *
 * Gets the height adjustment offset.
 *
 * Returns: height adjustment in meters
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_vr_comfort_settings_get_height_adjustment (LrgVRComfortSettings *self);

/**
 * lrg_vr_comfort_settings_set_height_adjustment:
 * @self: a #LrgVRComfortSettings
 * @adjustment: height adjustment in meters
 *
 * Sets the height adjustment offset.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_comfort_settings_set_height_adjustment (LrgVRComfortSettings *self,
                                                    gfloat                adjustment);

/**
 * lrg_vr_comfort_settings_get_smooth_turn_speed:
 * @self: a #LrgVRComfortSettings
 *
 * Returns: the smooth turn rate at full stick, degrees per second
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_vr_comfort_settings_get_smooth_turn_speed (LrgVRComfortSettings *self);

/**
 * lrg_vr_comfort_settings_set_smooth_turn_speed:
 * @self: a #LrgVRComfortSettings
 * @speed: degrees per second at full stick (clamped to 10 .. 360)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_comfort_settings_set_smooth_turn_speed (LrgVRComfortSettings *self,
                                                    gfloat                speed);

/**
 * lrg_vr_comfort_settings_get_seated:
 * @self: a #LrgVRComfortSettings
 *
 * Returns: %TRUE in seated mode
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_comfort_settings_get_seated (LrgVRComfortSettings *self);

/**
 * lrg_vr_comfort_settings_set_seated:
 * @self: a #LrgVRComfortSettings
 * @seated: %TRUE for seated play
 *
 * Seated mode: the player's eye height comes from a calibration
 * (lrg_vr_comfort_calibrate_height()) instead of the tracked head, so a
 * seated player stands at full height in the game.  #LrgVRComfort asks
 * for that calibration on the first tracked frame
 * (lrg_vr_comfort_needs_calibration()).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_comfort_settings_set_seated (LrgVRComfortSettings *self,
                                         gboolean              seated);

/* ==========================================================================
 * LrgVRComfort: the comfort state machine
 * ========================================================================== */

/**
 * LRG_VR_COMFORT_SNAP_THRESHOLD:
 *
 * Stick deflection that fires a snap turn.
 */
#define LRG_VR_COMFORT_SNAP_THRESHOLD   (0.7f)

/**
 * LRG_VR_COMFORT_SNAP_REARM:
 *
 * Stick deflection below which the next snap turn is armed again.
 */
#define LRG_VR_COMFORT_SNAP_REARM       (0.3f)

/**
 * LRG_VR_COMFORT_STICK_DEADZONE:
 *
 * Stick deflection below which smooth turning does nothing.
 */
#define LRG_VR_COMFORT_STICK_DEADZONE   (0.15f)

/**
 * LRG_VR_COMFORT_VIGNETTE_SPEED:
 *
 * Linear speed (metres per second) that closes the vignette fully.
 */
#define LRG_VR_COMFORT_VIGNETTE_SPEED   (3.0f)

/**
 * LRG_VR_COMFORT_VIGNETTE_TURN:
 *
 * Turn rate (degrees per second) that closes the vignette fully.
 */
#define LRG_VR_COMFORT_VIGNETTE_TURN    (120.0f)

/**
 * LRG_VR_COMFORT_VIGNETTE_RISE:
 *
 * How fast the vignette closes, intensity per second.
 */
#define LRG_VR_COMFORT_VIGNETTE_RISE    (6.0f)

/**
 * LRG_VR_COMFORT_VIGNETTE_FALL:
 *
 * How fast the vignette opens again, intensity per second.
 */
#define LRG_VR_COMFORT_VIGNETTE_FALL    (3.0f)

/**
 * LRG_VR_COMFORT_SNAP_BLINK:
 *
 * Seconds the vignette stays closed after a snap turn.
 */
#define LRG_VR_COMFORT_SNAP_BLINK       (0.12f)

#define LRG_TYPE_VR_COMFORT (lrg_vr_comfort_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVRComfort, lrg_vr_comfort, LRG, VR_COMFORT, GObject)

/**
 * lrg_vr_comfort_new:
 * @settings: (nullable): the settings to follow, %NULL for new defaults
 *
 * The run-time half of the comfort options: turning (snap with re-arm
 * hysteresis, or smooth with a dead zone), the movement vignette (closes
 * with speed and turn rate, blinks shut on a snap turn, fades with
 * separate rise and fall rates) and the height calibration.  Pure state:
 * it reads no device, the caller feeds it the stick, the speeds and the
 * tracked eye height each frame.
 *
 * Returns: (transfer full): a new #LrgVRComfort
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVRComfort * lrg_vr_comfort_new (LrgVRComfortSettings *settings);

/**
 * lrg_vr_comfort_get_settings:
 * @self: a #LrgVRComfort
 *
 * Returns: (transfer none): the settings it follows
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVRComfortSettings * lrg_vr_comfort_get_settings (LrgVRComfort *self);

/**
 * lrg_vr_comfort_turn:
 * @self: a #LrgVRComfort
 * @stick_x: the turn stick, -1 (left) .. 1 (right)
 * @delta: seconds since the last call
 *
 * One frame of turning.  Snap mode turns by the snap angle once when the
 * stick passes %LRG_VR_COMFORT_SNAP_THRESHOLD and not again until it
 * comes back under %LRG_VR_COMFORT_SNAP_REARM; smooth mode turns by the
 * smooth turn speed scaled by the deflection past
 * %LRG_VR_COMFORT_STICK_DEADZONE.
 *
 * Returns: degrees to turn this frame, positive to the right (clockwise
 *   seen from above)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_vr_comfort_turn (LrgVRComfort *self,
                            gfloat        stick_x,
                            gfloat        delta);

/**
 * lrg_vr_comfort_get_snap_armed:
 * @self: a #LrgVRComfort
 *
 * Returns: %TRUE when the next snap turn may fire
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_comfort_get_snap_armed (LrgVRComfort *self);

/**
 * lrg_vr_comfort_update_vignette:
 * @self: a #LrgVRComfort
 * @linear_speed: how fast the player moves, metres per second
 * @turn_rate: how fast the player turns by the stick, degrees per second
 *   (not the head: real head motion never vignettes)
 * @delta: seconds since the last call
 *
 * Moves the vignette toward its target: zero when the vignette is off,
 * else the intensity times the larger of @linear_speed over
 * %LRG_VR_COMFORT_VIGNETTE_SPEED and @turn_rate over
 * %LRG_VR_COMFORT_VIGNETTE_TURN (clamped to 1), or the full intensity for
 * %LRG_VR_COMFORT_SNAP_BLINK after a snap turn.
 *
 * Returns: the vignette now, 0 (open) .. 1 (closed)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_vr_comfort_update_vignette (LrgVRComfort *self,
                                       gfloat        linear_speed,
                                       gfloat        turn_rate,
                                       gfloat        delta);

/**
 * lrg_vr_comfort_get_vignette:
 * @self: a #LrgVRComfort
 *
 * Returns: the vignette, 0 (open) .. 1 (closed)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_vr_comfort_get_vignette (LrgVRComfort *self);

/**
 * lrg_vr_comfort_calibrate_height:
 * @self: a #LrgVRComfort
 * @tracked_eye_height: the head's height above the tracking floor now,
 *   metres
 * @target_eye_height: the eye height the player should have, metres
 *
 * Sets the settings' height adjustment so the tracked head is at
 * @target_eye_height (clamped to the adjustment's -2 .. 2 m).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_comfort_calibrate_height (LrgVRComfort *self,
                                      gfloat        tracked_eye_height,
                                      gfloat        target_eye_height);

/**
 * lrg_vr_comfort_needs_calibration:
 * @self: a #LrgVRComfort
 *
 * Returns: %TRUE in seated mode until lrg_vr_comfort_calibrate_height()
 *   ran (again after lrg_vr_comfort_reset())
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_comfort_needs_calibration (LrgVRComfort *self);

/**
 * lrg_vr_comfort_get_eye_height:
 * @self: a #LrgVRComfort
 * @tracked_eye_height: the head's height above the tracking floor, metres
 *
 * Returns: the eye height to use: the tracked height plus the
 *   adjustment, never below 0.1 m
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_vr_comfort_get_eye_height (LrgVRComfort *self,
                                      gfloat        tracked_eye_height);

/**
 * lrg_vr_comfort_reset:
 * @self: a #LrgVRComfort
 *
 * Opens the vignette, re-arms the snap turn and forgets the seated
 * calibration (a recentre).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_comfort_reset (LrgVRComfort *self);

G_END_DECLS
