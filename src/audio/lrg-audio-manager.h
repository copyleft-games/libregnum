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
 * @error: (optional): return location for a #GError
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
 * @error: (optional): return location for a #GError
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
