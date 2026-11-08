/* lrg-audio-source-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAudioSourceComponent: a positional sound emitter.  Plays a sound
 * bank entry (or a sound file) as an #LrgAudioManager voice at the
 * entity's world position and moves the voice with the entity every late
 * update.  Without an audio device nothing plays and nothing fails.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../ecs/lrg-component.h"
#include "../../audio/lrg-audio-manager.h"
#include "../../audio/lrg-audio-mixer.h"
#include "../../debug/lrg-debug-draw.h"
#include "../lrg-world3d.h"

G_BEGIN_DECLS

/**
 * LRG_AUDIO_SOURCE_FILE_BANK:
 *
 * Name of the sound bank #LrgAudioSourceComponent:sound-path files are
 * loaded into (one bank per audio manager, the file path is the sound
 * name).
 */
#define LRG_AUDIO_SOURCE_FILE_BANK "lrg-audio-source-files"

#define LRG_TYPE_AUDIO_SOURCE_COMPONENT (lrg_audio_source_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAudioSourceComponent, lrg_audio_source_component,
                      LRG, AUDIO_SOURCE_COMPONENT, LrgComponent)

/**
 * lrg_audio_source_component_new:
 *
 * Creates an emitter with no sound, volume 1, the default attenuation
 * (%LRG_AUDIO_DEFAULT_MIN_DISTANCE .. %LRG_AUDIO_DEFAULT_MAX_DISTANCE,
 * %LRG_AUDIO_DEFAULT_ROLLOFF), fully positional, not looping and not
 * autoplaying.
 *
 * Returns: (transfer full): a new #LrgAudioSourceComponent
 */
LRG_AVAILABLE_IN_ALL
LrgAudioSourceComponent * lrg_audio_source_component_new (void);

/**
 * lrg_audio_source_component_new_with_sound:
 * @bank: the sound bank name
 * @sound: the sound name in @bank
 *
 * Creates an emitter for a sound bank entry.
 *
 * Returns: (transfer full): a new #LrgAudioSourceComponent
 */
LRG_AVAILABLE_IN_ALL
LrgAudioSourceComponent * lrg_audio_source_component_new_with_sound (const gchar *bank,
                                                                     const gchar *sound);

/**
 * lrg_audio_source_component_set_sound:
 * @self: an #LrgAudioSourceComponent
 * @bank: (nullable): the sound bank name
 * @sound: (nullable): the sound name in @bank
 *
 * Selects a sound bank entry (#LrgAudioSourceComponent:bank and
 * #LrgAudioSourceComponent:sound).  Takes effect on the next play.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_source_component_set_sound (LrgAudioSourceComponent *self,
                                           const gchar             *bank,
                                           const gchar             *sound);

/**
 * lrg_audio_source_component_get_bank:
 * @self: an #LrgAudioSourceComponent
 *
 * Returns: (transfer none) (nullable): the sound bank name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_audio_source_component_get_bank (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_get_sound:
 * @self: an #LrgAudioSourceComponent
 *
 * Returns: (transfer none) (nullable): the sound name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_audio_source_component_get_sound (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_get_sound_path:
 * @self: an #LrgAudioSourceComponent
 *
 * Returns: (transfer none) (nullable): the sound file played when no bank
 *   is set
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_audio_source_component_get_sound_path (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_set_sound_path:
 * @self: an #LrgAudioSourceComponent
 * @path: (nullable): a sound file (the editor's AUDIO_EMITTER asset)
 *
 * Plays a file instead of a bank entry: on play it is loaded once into
 * the %LRG_AUDIO_SOURCE_FILE_BANK bank of the audio manager.  A bank set
 * with lrg_audio_source_component_set_sound() wins.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_source_component_set_sound_path (LrgAudioSourceComponent *self,
                                                const gchar             *path);

/**
 * lrg_audio_source_component_get_attenuation:
 * @self: an #LrgAudioSourceComponent
 * @out: (out caller-allocates): the attenuation voices are played with
 *
 * Fills @out from #LrgAudioSourceComponent:min-distance, max-distance,
 * rolloff and spatial-blend.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_source_component_get_attenuation (LrgAudioSourceComponent *self,
                                                 LrgAudioAttenuation     *out);

/**
 * lrg_audio_source_component_set_attenuation:
 * @self: an #LrgAudioSourceComponent
 * @attenuation: the curve to copy
 *
 * Sets min-distance, max-distance, rolloff and spatial-blend at once.
 * Takes effect on the next play.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_source_component_set_attenuation (LrgAudioSourceComponent   *self,
                                                 const LrgAudioAttenuation *attenuation);

/**
 * lrg_audio_source_component_play:
 * @self: an #LrgAudioSourceComponent
 *
 * Stops the current voice and starts a new one at the entity's world
 * position (flat when the component has no entity).
 *
 * Returns: the voice id, or 0 when nothing played (no sound, unknown
 *   bank or sound, no audio device)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_audio_source_component_play (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_stop:
 * @self: an #LrgAudioSourceComponent
 *
 * Stops the current voice, if any.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_source_component_stop (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_is_playing:
 * @self: an #LrgAudioSourceComponent
 *
 * Returns: %TRUE while the component's voice is audible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_source_component_is_playing (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_get_voice:
 * @self: an #LrgAudioSourceComponent
 *
 * Returns: the current voice id, 0 when none
 */
LRG_AVAILABLE_IN_ALL
guint lrg_audio_source_component_get_voice (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_get_audio_manager:
 * @self: an #LrgAudioSourceComponent
 *
 * Returns: (transfer none) (nullable): the explicit audio manager, %NULL
 *   for the default one
 */
LRG_AVAILABLE_IN_ALL
LrgAudioManager * lrg_audio_source_component_get_audio_manager (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_set_audio_manager:
 * @self: an #LrgAudioSourceComponent
 * @manager: (nullable): an audio manager, %NULL for the default one
 *
 * Sets the audio manager voices play through.  Stops the current voice.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_source_component_set_audio_manager (LrgAudioSourceComponent *self,
                                                   LrgAudioManager         *manager);

/* ==========================================================================
 * 0.9.0: attenuation curves, cones, doppler, occlusion, bus routing
 * ========================================================================== */

/**
 * lrg_audio_source_component_set_rolloff_curve:
 * @self: an #LrgAudioSourceComponent
 * @curve: (nullable): gain over normalised distance 0 (min-distance) ..
 *   1 (max-distance); the component keeps a reference
 *
 * Sets the custom rolloff curve (also readable and writable as text,
 * #LrgAudioSourceComponent:rolloff-curve-keys) and switches
 * #LrgAudioSourceComponent:rolloff-mode to %LRG_AUDIO_ROLLOFF_CUSTOM (or
 * back to %LRG_AUDIO_ROLLOFF_INVERSE for %NULL).  The curve is not an
 * object property, so data files and level params store the keys text.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_source_component_set_rolloff_curve (LrgAudioSourceComponent *self,
                                                   LrgKeyframeCurve        *curve);

/**
 * lrg_audio_source_component_get_rolloff_curve:
 * @self: an #LrgAudioSourceComponent
 *
 * Returns: (transfer none) (nullable): the custom rolloff curve
 */
LRG_AVAILABLE_IN_ALL
LrgKeyframeCurve * lrg_audio_source_component_get_rolloff_curve (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_get_distance_gain:
 * @self: an #LrgAudioSourceComponent
 * @distance: a distance from the source
 *
 * The distance attenuation alone (lrg_audio_rolloff_evaluate() with the
 * component's settings): what an attenuation gizmo plots.
 *
 * Returns: 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_audio_source_component_get_distance_gain (LrgAudioSourceComponent *self,
                                                     gfloat                   distance);

/**
 * lrg_audio_source_component_evaluate:
 * @self: an #LrgAudioSourceComponent
 * @listener_position: (array fixed-size=3): the listener
 * @listener_forward: (array fixed-size=3): its view direction
 * @listener_up: (array fixed-size=3) (nullable): its up, %NULL for +Y
 * @listener_velocity: (array fixed-size=3) (nullable): its velocity
 * @out_gain: (out) (optional): distance x cone x occlusion, blended with
 *   1 by #LrgAudioSourceComponent:spatial-blend (volume not included)
 * @out_pan: (out) (optional): -1 .. 1, scaled by the spatial blend
 * @out_pitch: (out) (optional): #LrgAudioSourceComponent:pitch x doppler
 *
 * The spatialisation the component applies to its voice, from the
 * entity's world position, forward (local -Z) and measured velocity and
 * the current occlusion.  Without an entity the source is flat: gain 1,
 * pan 0, pitch #LrgAudioSourceComponent:pitch.  Needs no audio device.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_source_component_evaluate (LrgAudioSourceComponent *self,
                                          const gfloat            *listener_position,
                                          const gfloat            *listener_forward,
                                          const gfloat            *listener_up,
                                          const gfloat            *listener_velocity,
                                          gfloat                  *out_gain,
                                          gfloat                  *out_pan,
                                          gfloat                  *out_pitch);

/**
 * lrg_audio_source_component_get_velocity:
 * @self: an #LrgAudioSourceComponent
 * @out_velocity: (out caller-allocates) (array fixed-size=3): units per
 *   second, measured from the entity's movement between late updates
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_source_component_get_velocity (LrgAudioSourceComponent *self,
                                              gfloat                  *out_velocity);

/**
 * lrg_audio_source_component_get_occlusion:
 * @self: an #LrgAudioSourceComponent
 *
 * The occlusion gain applied now: 1 when the path to the listener is
 * clear, #LrgAudioSourceComponent:occlusion-gain to the power of the
 * number of obstacles (at most 4) when blocked, faded over
 * #LrgAudioSourceComponent:occlusion-fade.
 *
 * Returns: 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_audio_source_component_get_occlusion (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_get_occluders:
 * @self: an #LrgAudioSourceComponent
 *
 * Returns: the number of bodies the last occlusion ray hit
 */
LRG_AVAILABLE_IN_ALL
guint lrg_audio_source_component_get_occluders (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_get_mixer:
 * @self: an #LrgAudioSourceComponent
 *
 * Returns: (transfer none) (nullable): the explicit mixer
 */
LRG_AVAILABLE_IN_ALL
LrgAudioMixer * lrg_audio_source_component_get_mixer (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_set_mixer:
 * @self: an #LrgAudioSourceComponent
 * @mixer: (nullable): a mixer graph, %NULL to use the audio manager's
 *   (lrg_audio_manager_get_mixer()) when #LrgAudioSourceComponent:bus is set
 *
 * Plays the source through @mixer (on #LrgAudioSourceComponent:bus, or
 * master) instead of a device voice.  Stops the current voice.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_source_component_set_mixer (LrgAudioSourceComponent *self,
                                           LrgAudioMixer           *mixer);

/**
 * lrg_audio_source_component_get_bus:
 * @self: an #LrgAudioSourceComponent
 *
 * Returns: (transfer none) (nullable): the mixer bus the source plays into
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_audio_source_component_get_bus (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_set_bus:
 * @self: an #LrgAudioSourceComponent
 * @bus: (nullable): a bus of the mixer, %NULL for master (and, without an
 *   explicit mixer, for a device voice)
 *
 * Routes the source into a mixer bus (#LrgAudioSourceComponent:bus).
 * Takes effect on the next play.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_source_component_set_bus (LrgAudioSourceComponent *self,
                                         const gchar             *bus);

/**
 * lrg_audio_source_component_get_wave:
 * @self: an #LrgAudioSourceComponent
 *
 * Returns: (transfer none) (nullable): the explicit wave
 */
LRG_AVAILABLE_IN_ALL
LrgWaveData * lrg_audio_source_component_get_wave (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_set_wave:
 * @self: an #LrgAudioSourceComponent
 * @wave: (nullable): samples to play through the mixer (wins over
 *   #LrgAudioSourceComponent:sound-path and the bank entry there)
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_source_component_set_wave (LrgAudioSourceComponent *self,
                                          LrgWaveData             *wave);

/**
 * lrg_audio_source_component_get_mixer_voice:
 * @self: an #LrgAudioSourceComponent
 *
 * Returns: the voice id in the mixer (lrg_audio_mixer_play()), 0 when
 *   the source plays a device voice or nothing
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_audio_source_component_get_mixer_voice (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_update_spatial:
 * @self: an #LrgAudioSourceComponent
 * @delta: seconds since the last update (velocity and ramps), 0 for none
 *
 * What the late update does: measures velocity, casts the occlusion ray,
 * evaluates the spatialisation against the audio manager's listener and
 * pushes gain, pan, pitch and occlusion filter to the voice.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_source_component_update_spatial (LrgAudioSourceComponent *self,
                                                gfloat                   delta);

/**
 * lrg_audio_source_component_debug_draw:
 * @self: an #LrgAudioSourceComponent
 * @draw: (nullable): a debug draw, %NULL for lrg_debug_draw_get_default()
 *
 * The attenuation gizmo: spheres at the minimum and maximum distance and,
 * for a directional source, the inner and outer cones out to the
 * maximum distance, on %LRG_DEBUG_CATEGORY_AUDIO for one frame.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_source_component_debug_draw (LrgAudioSourceComponent *self,
                                            LrgDebugDraw            *draw);

G_END_DECLS
