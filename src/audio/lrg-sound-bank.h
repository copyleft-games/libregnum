/* lrg-sound-bank.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A collection of named sound effects.
 *
 * LrgSoundBank manages a dictionary of GrlSound objects, allowing
 * sounds to be retrieved by name. This is useful for organizing
 * game sound effects into logical groups (e.g., "player", "ui", "enemy").
 *
 * Sound banks can be populated programmatically or loaded from a
 * YAML manifest file.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_SOUND_BANK (lrg_sound_bank_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSoundBank, lrg_sound_bank, LRG, SOUND_BANK, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_sound_bank_new:
 * @name: the bank name (e.g., "player", "ui")
 *
 * Creates a new empty sound bank.
 *
 * Returns: (transfer full): A new #LrgSoundBank
 */
LRG_AVAILABLE_IN_ALL
LrgSoundBank * lrg_sound_bank_new (const gchar *name);

/**
 * lrg_sound_bank_new_from_file:
 * @manifest_path: path to the YAML manifest file
 * @error: (optional): return location for a #GError
 *
 * Loads a sound bank from a YAML manifest file.
 *
 * The manifest format is:
 * ```yaml
 * name: player
 * base_path: sounds/player/
 * sounds:
 *   jump: jump.wav
 *   land: land.ogg
 *   hurt: hurt.wav
 * ```
 *
 * Paths in the sounds section are relative to base_path.
 *
 * Returns: (transfer full) (nullable): A new #LrgSoundBank, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgSoundBank * lrg_sound_bank_new_from_file (const gchar  *manifest_path,
                                              GError      **error);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_sound_bank_get_name:
 * @self: a #LrgSoundBank
 *
 * Gets the bank name.
 *
 * Returns: (transfer none): the bank name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_sound_bank_get_name (LrgSoundBank *self);

/**
 * lrg_sound_bank_get_base_path:
 * @self: a #LrgSoundBank
 *
 * Gets the base path for sound files.
 *
 * Returns: (transfer none) (nullable): the base path, or %NULL if not set
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_sound_bank_get_base_path (LrgSoundBank *self);

/**
 * lrg_sound_bank_set_base_path:
 * @self: a #LrgSoundBank
 * @path: (nullable): the base path for sound files
 *
 * Sets the base path for loading sound files.
 *
 * When loading sounds with lrg_sound_bank_load(), paths will be
 * relative to this base path.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sound_bank_set_base_path (LrgSoundBank *self,
                                   const gchar  *path);

/* ==========================================================================
 * Sound Management
 * ========================================================================== */

/**
 * lrg_sound_bank_add:
 * @self: a #LrgSoundBank
 * @name: the sound name
 * @sound: the sound to add
 *
 * Adds a sound to the bank.
 *
 * If a sound with the same name already exists, it will be replaced.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sound_bank_add (LrgSoundBank *self,
                         const gchar  *name,
                         GrlSound     *sound);

/**
 * lrg_sound_bank_load:
 * @self: a #LrgSoundBank
 * @name: the sound name
 * @filename: path to the sound file (relative to base_path)
 * @error: (optional): return location for a #GError
 *
 * Loads a sound from a file and adds it to the bank.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sound_bank_load (LrgSoundBank  *self,
                              const gchar   *name,
                              const gchar   *filename,
                              GError       **error);

/**
 * lrg_sound_bank_add_from_wave:
 * @self: a #LrgSoundBank
 * @name: the sound name
 * @wave: the wave data to convert to a sound
 *
 * Adds a sound created from wave data.
 *
 * The wave data is converted to a GrlSound and added to the bank.
 * This is useful when working with procedurally generated audio
 * or audio loaded from custom sources.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sound_bank_add_from_wave (LrgSoundBank *self,
                                        const gchar  *name,
                                        LrgWaveData  *wave);

/**
 * lrg_sound_bank_load_from_memory:
 * @self: a #LrgSoundBank
 * @name: the sound name
 * @file_type: file type extension (e.g., ".wav", ".ogg")
 * @data: (array length=data_size): audio file data in memory
 * @data_size: size of @data in bytes
 * @error: (optional): return location for a #GError
 *
 * Loads a sound from a memory buffer containing audio file data.
 *
 * The @file_type parameter specifies the audio format. It should be
 * a file extension including the dot (e.g., ".wav", ".ogg", ".mp3").
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sound_bank_load_from_memory (LrgSoundBank  *self,
                                           const gchar   *name,
                                           const gchar   *file_type,
                                           const guint8  *data,
                                           gsize          data_size,
                                           GError       **error);

/**
 * lrg_sound_bank_load_from_resource:
 * @self: a #LrgSoundBank
 * @name: the sound name
 * @pack: the resource pack to load from
 * @resource_name: the name of the resource in the pack
 * @error: (optional): return location for a #GError
 *
 * Loads a sound from a resource pack (rres file).
 *
 * Requires the resource pack to have a central directory for
 * name-based lookups.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sound_bank_load_from_resource (LrgSoundBank  *self,
                                             const gchar   *name,
                                             LrgAssetPack  *pack,
                                             const gchar   *resource_name,
                                             GError       **error);

/**
 * lrg_sound_bank_add_alias:
 * @self: a #LrgSoundBank
 * @alias: the alias name
 * @source: the source sound name to alias
 *
 * Creates an alias for an existing sound in the bank.
 *
 * The alias points to the same underlying GrlSound as the source.
 * This is useful for providing multiple names for the same sound
 * effect (e.g., "hit" and "damage" pointing to the same sound).
 *
 * Returns: %TRUE if the source sound exists and alias was created
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sound_bank_add_alias (LrgSoundBank *self,
                                    const gchar  *alias,
                                    const gchar  *source);

/**
 * lrg_sound_bank_remove:
 * @self: a #LrgSoundBank
 * @name: the sound name to remove
 *
 * Removes a sound from the bank.
 *
 * Returns: %TRUE if the sound was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sound_bank_remove (LrgSoundBank *self,
                                const gchar  *name);

/**
 * lrg_sound_bank_get:
 * @self: a #LrgSoundBank
 * @name: the sound name
 *
 * Gets a sound from the bank by name.
 *
 * Returns: (transfer none) (nullable): the sound, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
GrlSound * lrg_sound_bank_get (LrgSoundBank *self,
                               const gchar  *name);

/**
 * lrg_sound_bank_contains:
 * @self: a #LrgSoundBank
 * @name: the sound name
 *
 * Checks if the bank contains a sound with the given name.
 *
 * Returns: %TRUE if the sound exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sound_bank_contains (LrgSoundBank *self,
                                  const gchar  *name);

/**
 * lrg_sound_bank_get_count:
 * @self: a #LrgSoundBank
 *
 * Gets the number of sounds in the bank.
 *
 * Returns: the sound count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sound_bank_get_count (LrgSoundBank *self);

/**
 * lrg_sound_bank_get_names:
 * @self: a #LrgSoundBank
 *
 * Gets a list of all sound names in the bank.
 *
 * Returns: (transfer container) (element-type utf8): list of sound names
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_sound_bank_get_names (LrgSoundBank *self);

/**
 * lrg_sound_bank_clear:
 * @self: a #LrgSoundBank
 *
 * Removes all sounds from the bank.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sound_bank_clear (LrgSoundBank *self);

/* ==========================================================================
 * Playback
 * ========================================================================== */

/**
 * lrg_sound_bank_play:
 * @self: a #LrgSoundBank
 * @name: the sound name to play
 *
 * Plays a sound from the bank by name.
 *
 * Returns: %TRUE if the sound was found and played
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sound_bank_play (LrgSoundBank *self,
                              const gchar  *name);

/**
 * lrg_sound_bank_play_multi:
 * @self: a #LrgSoundBank
 * @name: the sound name to play
 *
 * Plays a sound allowing multiple overlapping instances.
 *
 * Returns: %TRUE if the sound was found and played
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sound_bank_play_multi (LrgSoundBank *self,
                                    const gchar  *name);

/**
 * lrg_sound_bank_stop:
 * @self: a #LrgSoundBank
 * @name: the sound name to stop
 *
 * Stops a playing sound.
 *
 * Returns: %TRUE if the sound was found and stopped
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sound_bank_stop (LrgSoundBank *self,
                              const gchar  *name);

/**
 * lrg_sound_bank_stop_all:
 * @self: a #LrgSoundBank
 *
 * Stops all playing sounds in the bank.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sound_bank_stop_all (LrgSoundBank *self);

/* ==========================================================================
 * Volume Control
 * ========================================================================== */

/**
 * lrg_sound_bank_set_volume:
 * @self: a #LrgSoundBank
 * @volume: volume level (0.0 to 1.0)
 *
 * Sets the volume for all sounds in the bank.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sound_bank_set_volume (LrgSoundBank *self,
                                gfloat        volume);

/**
 * lrg_sound_bank_get_volume:
 * @self: a #LrgSoundBank
 *
 * Gets the current volume level for the bank.
 *
 * Returns: the volume level (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_sound_bank_get_volume (LrgSoundBank *self);

G_END_DECLS
