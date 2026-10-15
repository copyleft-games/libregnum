/* lrg-audio-manager.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Centralized audio management for games.
 *
 * LrgAudioManager provides a singleton interface for managing all audio
 * in a game, including:
 * - Multiple sound banks for organized sound effects
 * - Background music with crossfading support
 * - Volume channels (master, sfx, music, voice)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-sound-bank.h"
#include "lrg-music-track.h"
#include "lrg-procedural-audio.h"

G_BEGIN_DECLS

#define LRG_TYPE_AUDIO_MANAGER (lrg_audio_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAudioManager, lrg_audio_manager, LRG, AUDIO_MANAGER, GObject)

/* ==========================================================================
 * Singleton Access
 * ========================================================================== */

/**
 * lrg_audio_manager_get_default:
 *
 * Gets the default audio manager singleton.
 *
 * Returns: (transfer none): The default #LrgAudioManager
 */
LRG_AVAILABLE_IN_ALL
LrgAudioManager * lrg_audio_manager_get_default (void);

/* ==========================================================================
 * Update
 * ========================================================================== */

/**
 * lrg_audio_manager_update:
 * @self: the audio manager
 *
 * Updates all audio streams. Must be called every frame.
 *
 * This updates music streams, handles crossfading, and manages
 * other time-based audio features.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_manager_update (LrgAudioManager *self);

/* ==========================================================================
 * Sound Bank Management
 * ========================================================================== */

/**
 * lrg_audio_manager_add_bank:
 * @self: the audio manager
 * @bank: the sound bank to add
 *
 * Adds a sound bank to the manager.
 *
 * The bank is referenced by its name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_manager_add_bank (LrgAudioManager *self,
                                  LrgSoundBank    *bank);

/**
 * lrg_audio_manager_load_bank:
 * @self: the audio manager
 * @manifest_path: path to the bank manifest YAML file
 * @error: return location for a #GError
 *
 * Loads a sound bank from a manifest file and adds it.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_manager_load_bank (LrgAudioManager  *self,
                                       const gchar      *manifest_path,
                                       GError          **error);

/**
 * lrg_audio_manager_remove_bank:
 * @self: the audio manager
 * @name: the bank name to remove
 *
 * Removes a sound bank from the manager.
 *
 * Returns: %TRUE if the bank was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_manager_remove_bank (LrgAudioManager *self,
                                         const gchar     *name);

/**
 * lrg_audio_manager_get_bank:
 * @self: the audio manager
 * @name: the bank name
 *
 * Gets a sound bank by name.
 *
 * Returns: (transfer none) (nullable): the sound bank, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
LrgSoundBank * lrg_audio_manager_get_bank (LrgAudioManager *self,
                                            const gchar     *name);

/**
 * lrg_audio_manager_get_bank_names:
 * @self: the audio manager
 *
 * Gets a list of all bank names.
 *
 * Returns: (transfer container) (element-type utf8): list of bank names
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_audio_manager_get_bank_names (LrgAudioManager *self);

/* ==========================================================================
 * Sound Playback
 * ========================================================================== */

/**
 * lrg_audio_manager_play_sound:
 * @self: the audio manager
 * @bank: the bank name
 * @sound: the sound name
 *
 * Plays a sound from a bank.
 *
 * Returns: %TRUE if the sound was found and played
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_manager_play_sound (LrgAudioManager *self,
                                        const gchar     *bank,
                                        const gchar     *sound);

/**
 * lrg_audio_manager_play_sound_multi:
 * @self: the audio manager
 * @bank: the bank name
 * @sound: the sound name
 *
 * Plays a sound allowing multiple overlapping instances.
 *
 * Returns: %TRUE if the sound was found and played
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_manager_play_sound_multi (LrgAudioManager *self,
                                              const gchar     *bank,
                                              const gchar     *sound);

/**
 * lrg_audio_manager_stop_sound:
 * @self: the audio manager
 * @bank: the bank name
 * @sound: the sound name
 *
 * Stops a playing sound.
 *
 * Returns: %TRUE if the sound was found and stopped
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_manager_stop_sound (LrgAudioManager *self,
                                        const gchar     *bank,
                                        const gchar     *sound);

/**
 * lrg_audio_manager_stop_all_sounds:
 * @self: the audio manager
 *
 * Stops all playing sounds in all banks.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_manager_stop_all_sounds (LrgAudioManager *self);

/* ==========================================================================
 * Positional Sound (emitters and listener)
 * ========================================================================== */

/**
 * LRG_AUDIO_HAS_POSITIONAL:
 *
 * Defined when this libregnum provides positional voices
 * (lrg_audio_manager_play_sound_at() and friends), so applications can
 * probe for the API at compile time.
 */
#define LRG_AUDIO_HAS_POSITIONAL (1)

/**
 * LRG_AUDIO_DEFAULT_MIN_DISTANCE:
 *
 * Default #LrgAudioAttenuation.min_distance: full volume inside it.
 */
#define LRG_AUDIO_DEFAULT_MIN_DISTANCE (1.0f)

/**
 * LRG_AUDIO_DEFAULT_MAX_DISTANCE:
 *
 * Default #LrgAudioAttenuation.max_distance: silent beyond it.
 */
#define LRG_AUDIO_DEFAULT_MAX_DISTANCE (50.0f)

/**
 * LRG_AUDIO_DEFAULT_ROLLOFF:
 *
 * Default #LrgAudioAttenuation.rolloff (inverse distance).
 */
#define LRG_AUDIO_DEFAULT_ROLLOFF (1.0f)

#define LRG_TYPE_AUDIO_ATTENUATION (lrg_audio_attenuation_get_type ())

/**
 * LrgAudioAttenuation:
 * @min_distance: distance within which a source plays at full volume
 * @max_distance: distance beyond which a source is silent
 * @rolloff: curve between the two: 0 is linear; a positive value is
 *   inverse distance `min / (min + rolloff * (d - min))`, rescaled so
 *   that it reaches exactly 0 at @max_distance (1.0 resembles Unity's
 *   logarithmic rolloff; larger values fall off faster)
 * @spatial_blend: 0 plays flat (2D: full volume, centred), 1 fully
 *   positional; values between mix gain and pan linearly
 *
 * How a positional voice fades and pans with its distance and direction
 * from the listener (see lrg_audio_compute_spatial()).
 */
typedef struct _LrgAudioAttenuation LrgAudioAttenuation;

struct _LrgAudioAttenuation
{
    gfloat min_distance;
    gfloat max_distance;
    gfloat rolloff;
    gfloat spatial_blend;
};

LRG_AVAILABLE_IN_ALL
GType lrg_audio_attenuation_get_type (void) G_GNUC_CONST;

/**
 * lrg_audio_attenuation_new:
 * @min_distance: full volume within this distance
 * @max_distance: silent beyond this distance
 * @rolloff: 0 linear, > 0 inverse distance
 * @spatial_blend: 0 flat .. 1 positional
 *
 * Returns: (transfer full): a new attenuation, free with
 *   lrg_audio_attenuation_free()
 */
LRG_AVAILABLE_IN_ALL
LrgAudioAttenuation * lrg_audio_attenuation_new (gfloat min_distance,
                                                 gfloat max_distance,
                                                 gfloat rolloff,
                                                 gfloat spatial_blend);

/**
 * lrg_audio_attenuation_init_default:
 * @self: (out caller-allocates): the attenuation to fill
 *
 * Fills @self with %LRG_AUDIO_DEFAULT_MIN_DISTANCE,
 * %LRG_AUDIO_DEFAULT_MAX_DISTANCE, %LRG_AUDIO_DEFAULT_ROLLOFF and a
 * spatial blend of 1.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_attenuation_init_default (LrgAudioAttenuation *self);

/**
 * lrg_audio_attenuation_copy:
 * @self: an #LrgAudioAttenuation
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgAudioAttenuation * lrg_audio_attenuation_copy (const LrgAudioAttenuation *self);

/**
 * lrg_audio_attenuation_free:
 * @self: (nullable): an #LrgAudioAttenuation
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_attenuation_free (LrgAudioAttenuation *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgAudioAttenuation, lrg_audio_attenuation_free)

/**
 * lrg_audio_compute_spatial:
 * @listener_position: (array fixed-size=3): the listener (ears)
 * @listener_forward: (array fixed-size=3): where the listener looks
 *   (need not be normalized; a zero vector means +Z)
 * @listener_up: (array fixed-size=3) (nullable): the listener's up,
 *   %NULL for +Y
 * @source_position: (array fixed-size=3): the sound source
 * @attenuation: (nullable): the curve, %NULL for the defaults
 * @out_gain: (out) (optional): 0 .. 1 volume factor
 * @out_pan: (out) (optional): -1 (left) .. 1 (right)
 *
 * Pure spatialisation used by positional voices.  The gain is 1 within
 * min_distance, 0 beyond max_distance and follows the rolloff curve in
 * between.  The pan is the source direction projected on the
 * listener's right vector (`forward x up`, the screen right of a
 * right-handed Y-up camera such as raylib's), shrunk inside
 * min_distance so a source at the listener's head stays centred.  Both
 * are mixed with the flat values (gain 1, pan 0) by spatial_blend.
 * Needs no audio device.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_compute_spatial (const gfloat              *listener_position,
                                const gfloat              *listener_forward,
                                const gfloat              *listener_up,
                                const gfloat              *source_position,
                                const LrgAudioAttenuation *attenuation,
                                gfloat                    *out_gain,
                                gfloat                    *out_pan);

/**
 * lrg_audio_manager_set_listener:
 * @self: the audio manager
 * @position: (array fixed-size=3): the listener position (camera)
 * @forward: (array fixed-size=3): view direction
 * @up: (array fixed-size=3) (nullable): up vector, %NULL for +Y
 *
 * Moves the ears every positional voice is heard with.  Live voices
 * pick the change up on the next lrg_audio_manager_update().  The
 * default listener sits at the origin looking along +Z with +Y up.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_manager_set_listener (LrgAudioManager *self,
                                     const gfloat    *position,
                                     const gfloat    *forward,
                                     const gfloat    *up);

/**
 * lrg_audio_manager_get_listener:
 * @self: the audio manager
 * @out_position: (out caller-allocates) (array fixed-size=3) (optional):
 *   the listener position
 * @out_forward: (out caller-allocates) (array fixed-size=3) (optional):
 *   the view direction as set
 * @out_up: (out caller-allocates) (array fixed-size=3) (optional): the
 *   up vector
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_manager_get_listener (LrgAudioManager *self,
                                     gfloat          *out_position,
                                     gfloat          *out_forward,
                                     gfloat          *out_up);

/**
 * lrg_audio_manager_play_sound_at:
 * @self: the audio manager
 * @bank_name: the bank name
 * @sound_name: the sound name in that bank
 * @position: (array fixed-size=3) (nullable): where the sound plays,
 *   %NULL for a flat (2D) voice
 * @volume: 0 .. 1 before the manager's master and SFX volumes and the
 *   attenuation
 * @attenuation: (nullable): the curve, %NULL for the defaults
 *
 * Starts a new voice of the sound: an alias of the bank's #GrlSound
 * that shares its samples, so several voices of one sound overlap, each
 * with its own volume and pan.  The voice is released by
 * lrg_audio_manager_update() once it finished.
 *
 * Returns: a voice id (> 0), or 0 when nothing played (unknown bank or
 *   sound, no audio device)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_audio_manager_play_sound_at (LrgAudioManager           *self,
                                       const gchar               *bank_name,
                                       const gchar               *sound_name,
                                       const gfloat              *position,
                                       gfloat                     volume,
                                       const LrgAudioAttenuation *attenuation);

/**
 * lrg_audio_manager_set_voice_position:
 * @self: the audio manager
 * @voice: a voice id
 * @position: (array fixed-size=3): the new source position
 *
 * Moves a positional voice (a moving emitter); gain and pan follow at
 * once.  A flat voice becomes positional.
 *
 * Returns: %TRUE when @voice is alive
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_manager_set_voice_position (LrgAudioManager *self,
                                               guint            voice,
                                               const gfloat    *position);

/**
 * lrg_audio_manager_get_voice_spatial:
 * @self: the audio manager
 * @voice: a voice id
 * @out_gain: (out) (optional): the attenuation gain last applied
 * @out_pan: (out) (optional): the pan last applied
 *
 * Returns: %TRUE when @voice is alive
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_manager_get_voice_spatial (LrgAudioManager *self,
                                              guint            voice,
                                              gfloat          *out_gain,
                                              gfloat          *out_pan);

/**
 * lrg_audio_manager_stop_voice:
 * @self: the audio manager
 * @voice: a voice id
 *
 * Stops and releases a voice.
 *
 * Returns: %TRUE when @voice was alive
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_manager_stop_voice (LrgAudioManager *self,
                                       guint            voice);

/**
 * lrg_audio_manager_is_voice_playing:
 * @self: the audio manager
 * @voice: a voice id
 *
 * Returns: %TRUE while @voice is audible (alive and not finished)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_manager_is_voice_playing (LrgAudioManager *self,
                                             guint            voice);

/**
 * lrg_audio_manager_get_voice_count:
 * @self: the audio manager
 *
 * Returns: the number of voices not yet released
 */
LRG_AVAILABLE_IN_ALL
guint lrg_audio_manager_get_voice_count (LrgAudioManager *self);

/**
 * lrg_audio_manager_stop_all_voices:
 * @self: the audio manager
 *
 * Stops and releases every voice (lrg_audio_manager_stop_all_sounds()
 * does this too).
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_manager_stop_all_voices (LrgAudioManager *self);

/* ==========================================================================
 * Music Playback
 * ========================================================================== */

/**
 * lrg_audio_manager_play_music:
 * @self: the audio manager
 * @track: the music track to play
 *
 * Plays a music track, replacing any currently playing music.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_manager_play_music (LrgAudioManager *self,
                                    LrgMusicTrack   *track);

/**
 * lrg_audio_manager_play_music_from_file:
 * @self: the audio manager
 * @path: path to the music file
 * @error: return location for a #GError
 *
 * Loads and plays a music file.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_manager_play_music_from_file (LrgAudioManager  *self,
                                                  const gchar      *path,
                                                  GError          **error);

/**
 * lrg_audio_manager_stop_music:
 * @self: the audio manager
 *
 * Stops the currently playing music.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_manager_stop_music (LrgAudioManager *self);

/**
 * lrg_audio_manager_pause_music:
 * @self: the audio manager
 *
 * Pauses the currently playing music.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_manager_pause_music (LrgAudioManager *self);

/**
 * lrg_audio_manager_resume_music:
 * @self: the audio manager
 *
 * Resumes paused music.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_manager_resume_music (LrgAudioManager *self);

/**
 * lrg_audio_manager_get_current_music:
 * @self: the audio manager
 *
 * Gets the currently playing music track.
 *
 * Returns: (transfer none) (nullable): the current track, or %NULL if none
 */
LRG_AVAILABLE_IN_ALL
LrgMusicTrack * lrg_audio_manager_get_current_music (LrgAudioManager *self);

/**
 * lrg_audio_manager_is_music_playing:
 * @self: the audio manager
 *
 * Checks if music is currently playing.
 *
 * Returns: %TRUE if music is playing
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_manager_is_music_playing (LrgAudioManager *self);

/* ==========================================================================
 * Crossfade
 * ========================================================================== */

/**
 * lrg_audio_manager_crossfade_to:
 * @self: the audio manager
 * @track: the new music track
 * @duration: crossfade duration in seconds
 *
 * Crossfades from the current music to a new track.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_manager_crossfade_to (LrgAudioManager *self,
                                      LrgMusicTrack   *track,
                                      gfloat           duration);

/**
 * lrg_audio_manager_is_crossfading:
 * @self: the audio manager
 *
 * Checks if a crossfade is in progress.
 *
 * Returns: %TRUE if crossfading
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_manager_is_crossfading (LrgAudioManager *self);

/* ==========================================================================
 * Volume Control
 * ========================================================================== */

/**
 * lrg_audio_manager_set_master_volume:
 * @self: the audio manager
 * @volume: volume level (0.0 to 1.0)
 *
 * Sets the master volume for all audio.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_manager_set_master_volume (LrgAudioManager *self,
                                           gfloat           volume);

/**
 * lrg_audio_manager_get_master_volume:
 * @self: the audio manager
 *
 * Gets the master volume.
 *
 * Returns: volume level (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_audio_manager_get_master_volume (LrgAudioManager *self);

/**
 * lrg_audio_manager_set_sfx_volume:
 * @self: the audio manager
 * @volume: volume level (0.0 to 1.0)
 *
 * Sets the sound effects volume.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_manager_set_sfx_volume (LrgAudioManager *self,
                                        gfloat           volume);

/**
 * lrg_audio_manager_get_sfx_volume:
 * @self: the audio manager
 *
 * Gets the sound effects volume.
 *
 * Returns: volume level (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_audio_manager_get_sfx_volume (LrgAudioManager *self);

/**
 * lrg_audio_manager_set_music_volume:
 * @self: the audio manager
 * @volume: volume level (0.0 to 1.0)
 *
 * Sets the music volume.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_manager_set_music_volume (LrgAudioManager *self,
                                          gfloat           volume);

/**
 * lrg_audio_manager_get_music_volume:
 * @self: the audio manager
 *
 * Gets the music volume.
 *
 * Returns: volume level (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_audio_manager_get_music_volume (LrgAudioManager *self);

/**
 * lrg_audio_manager_set_voice_volume:
 * @self: the audio manager
 * @volume: volume level (0.0 to 1.0)
 *
 * Sets the voice/dialog volume.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_manager_set_voice_volume (LrgAudioManager *self,
                                          gfloat           volume);

/**
 * lrg_audio_manager_get_voice_volume:
 * @self: the audio manager
 *
 * Gets the voice/dialog volume.
 *
 * Returns: volume level (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_audio_manager_get_voice_volume (LrgAudioManager *self);

/**
 * lrg_audio_manager_set_muted:
 * @self: the audio manager
 * @muted: whether to mute
 *
 * Mutes or unmutes all audio.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_manager_set_muted (LrgAudioManager *self,
                                   gboolean         muted);

/**
 * lrg_audio_manager_get_muted:
 * @self: the audio manager
 *
 * Gets whether audio is muted.
 *
 * Returns: %TRUE if muted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_manager_get_muted (LrgAudioManager *self);

/* ==========================================================================
 * Procedural Audio Management
 * ========================================================================== */

/**
 * lrg_audio_manager_add_procedural:
 * @self: the audio manager
 * @name: a unique name for the procedural audio
 * @audio: the procedural audio source to add
 *
 * Registers a procedural audio source with the manager.
 *
 * Once registered, the audio manager's update() method will
 * automatically call update() on the procedural audio source
 * to keep it generating samples while playing.
 *
 * The audio source is referenced and will be unreferenced when
 * removed or when the manager is finalized.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_manager_add_procedural (LrgAudioManager    *self,
                                        const gchar        *name,
                                        LrgProceduralAudio *audio);

/**
 * lrg_audio_manager_remove_procedural:
 * @self: the audio manager
 * @name: the name of the procedural audio to remove
 *
 * Removes a procedural audio source from the manager.
 *
 * The audio is stopped if playing before being removed.
 *
 * Returns: %TRUE if the source was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_manager_remove_procedural (LrgAudioManager *self,
                                               const gchar     *name);

/**
 * lrg_audio_manager_get_procedural:
 * @self: the audio manager
 * @name: the name of the procedural audio
 *
 * Gets a registered procedural audio source by name.
 *
 * Returns: (transfer none) (nullable): the procedural audio, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
LrgProceduralAudio * lrg_audio_manager_get_procedural (LrgAudioManager *self,
                                                        const gchar     *name);

/**
 * lrg_audio_manager_get_procedural_names:
 * @self: the audio manager
 *
 * Gets a list of all registered procedural audio names.
 *
 * Returns: (transfer container) (element-type utf8): list of names
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_audio_manager_get_procedural_names (LrgAudioManager *self);

/**
 * lrg_audio_manager_play_procedural:
 * @self: the audio manager
 * @name: the name of the procedural audio to play
 *
 * Starts playing a registered procedural audio source.
 *
 * Returns: %TRUE if the source was found and started
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_manager_play_procedural (LrgAudioManager *self,
                                             const gchar     *name);

/**
 * lrg_audio_manager_stop_procedural:
 * @self: the audio manager
 * @name: the name of the procedural audio to stop
 *
 * Stops a playing procedural audio source.
 *
 * Returns: %TRUE if the source was found and stopped
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_manager_stop_procedural (LrgAudioManager *self,
                                             const gchar     *name);

/**
 * lrg_audio_manager_stop_all_procedural:
 * @self: the audio manager
 *
 * Stops all playing procedural audio sources.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_manager_stop_all_procedural (LrgAudioManager *self);

G_END_DECLS
