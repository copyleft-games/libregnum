/* lrg-vehicle-audio.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgVehicleAudio - Vehicle sound management.
 *
 * Handles engine, tire, horn, and other vehicle-related sounds.
 * Automatically adjusts pitch and volume based on vehicle state.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-vehicle.h"

G_BEGIN_DECLS

#define LRG_TYPE_VEHICLE_AUDIO (lrg_vehicle_audio_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVehicleAudio, lrg_vehicle_audio,
                      LRG, VEHICLE_AUDIO, GObject)

/**
 * lrg_vehicle_audio_new:
 *
 * Creates a new vehicle audio manager.
 *
 * Returns: (transfer full): A new #LrgVehicleAudio
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVehicleAudio *
lrg_vehicle_audio_new (void);

/* Vehicle binding */

/**
 * lrg_vehicle_audio_set_vehicle:
 * @self: an #LrgVehicleAudio
 * @vehicle: (nullable): Vehicle to sync with
 *
 * Sets the vehicle to monitor for audio.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_audio_set_vehicle (LrgVehicleAudio *self,
                               LrgVehicle      *vehicle);

/**
 * lrg_vehicle_audio_get_vehicle:
 * @self: an #LrgVehicleAudio
 *
 * Gets the monitored vehicle.
 *
 * Returns: (transfer none) (nullable): The vehicle, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVehicle *
lrg_vehicle_audio_get_vehicle (LrgVehicleAudio *self);

/* Sound configuration */

/**
 * lrg_vehicle_audio_set_engine_sound:
 * @self: an #LrgVehicleAudio
 * @sound_id: Sound asset ID for engine loop
 *
 * Sets the engine loop sound.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_audio_set_engine_sound (LrgVehicleAudio *self,
                                    const gchar     *sound_id);

/**
 * lrg_vehicle_audio_set_tire_screech_sound:
 * @self: an #LrgVehicleAudio
 * @sound_id: Sound asset ID for tire screech
 *
 * Sets the tire screech sound.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_audio_set_tire_screech_sound (LrgVehicleAudio *self,
                                          const gchar     *sound_id);

/**
 * lrg_vehicle_audio_set_horn_sound:
 * @self: an #LrgVehicleAudio
 * @sound_id: Sound asset ID for horn
 *
 * Sets the horn sound.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_audio_set_horn_sound (LrgVehicleAudio *self,
                                  const gchar     *sound_id);

/**
 * lrg_vehicle_audio_set_collision_sound:
 * @self: an #LrgVehicleAudio
 * @sound_id: Sound asset ID for collision
 *
 * Sets the collision impact sound.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_audio_set_collision_sound (LrgVehicleAudio *self,
                                       const gchar     *sound_id);

/* Engine tuning */

/**
 * lrg_vehicle_audio_set_engine_pitch_range:
 * @self: an #LrgVehicleAudio
 * @min_pitch: Pitch at idle RPM
 * @max_pitch: Pitch at max RPM
 *
 * Sets the engine sound pitch range.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_audio_set_engine_pitch_range (LrgVehicleAudio *self,
                                          gfloat           min_pitch,
                                          gfloat           max_pitch);

/**
 * lrg_vehicle_audio_set_engine_rpm_range:
 * @self: an #LrgVehicleAudio
 * @idle_rpm: Idle RPM value
 * @max_rpm: Maximum RPM value
 *
 * Sets the RPM range for pitch calculation.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_audio_set_engine_rpm_range (LrgVehicleAudio *self,
                                        gfloat           idle_rpm,
                                        gfloat           max_rpm);

/* Volume */

/**
 * lrg_vehicle_audio_set_master_volume:
 * @self: an #LrgVehicleAudio
 * @volume: Master volume (0-1)
 *
 * Sets the master volume for all vehicle sounds.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_audio_set_master_volume (LrgVehicleAudio *self,
                                     gfloat           volume);

/**
 * lrg_vehicle_audio_get_master_volume:
 * @self: an #LrgVehicleAudio
 *
 * Gets the master volume.
 *
 * Returns: Master volume
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_vehicle_audio_get_master_volume (LrgVehicleAudio *self);

/**
 * lrg_vehicle_audio_set_engine_volume:
 * @self: an #LrgVehicleAudio
 * @volume: Engine volume (0-1)
 *
 * Sets the engine sound volume.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_audio_set_engine_volume (LrgVehicleAudio *self,
                                     gfloat           volume);

/**
 * lrg_vehicle_audio_set_effects_volume:
 * @self: an #LrgVehicleAudio
 * @volume: Effects volume (0-1)
 *
 * Sets the sound effects volume (screech, collision, horn).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_audio_set_effects_volume (LrgVehicleAudio *self,
                                      gfloat           volume);

/* Playback control */

/**
 * lrg_vehicle_audio_start:
 * @self: an #LrgVehicleAudio
 *
 * Starts playing vehicle audio (engine, etc.).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_audio_start (LrgVehicleAudio *self);

/**
 * lrg_vehicle_audio_stop:
 * @self: an #LrgVehicleAudio
 *
 * Stops all vehicle audio.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_audio_stop (LrgVehicleAudio *self);

/**
 * lrg_vehicle_audio_is_playing:
 * @self: an #LrgVehicleAudio
 *
 * Checks if vehicle audio is playing.
 *
 * Returns: %TRUE if playing
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_vehicle_audio_is_playing (LrgVehicleAudio *self);

/**
 * lrg_vehicle_audio_play_horn:
 * @self: an #LrgVehicleAudio
 *
 * Plays the horn sound.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_audio_play_horn (LrgVehicleAudio *self);

/**
 * lrg_vehicle_audio_stop_horn:
 * @self: an #LrgVehicleAudio
 *
 * Stops the horn sound.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_audio_stop_horn (LrgVehicleAudio *self);

/**
 * lrg_vehicle_audio_play_collision:
 * @self: an #LrgVehicleAudio
 * @intensity: Collision intensity (0-1)
 *
 * Plays a collision sound with given intensity.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_audio_play_collision (LrgVehicleAudio *self,
                                  gfloat           intensity);

/* Update */

/**
 * lrg_vehicle_audio_update:
 * @self: an #LrgVehicleAudio
 * @delta: Time step in seconds
 *
 * Updates audio based on vehicle state.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_audio_update (LrgVehicleAudio *self,
                          gfloat           delta);

G_END_DECLS
