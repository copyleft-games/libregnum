/* lrg-wave-data.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Audio wave data for loading and manipulating audio samples.
 *
 * LrgWaveData wraps graylib's GrlWave type, providing a GObject
 * wrapper with game-specific features like procedural generation
 * and integration with the audio manager.
 *
 * Unlike GrlSound which is ready for playback, LrgWaveData is meant
 * for preprocessing audio data before converting it to a sound.
 *
 * Common use cases:
 * - Loading audio files and manipulating them (crop, resample, format conversion)
 * - Generating procedural audio
 * - Exporting modified audio to files
 *
 * To play wave data, convert it to a GrlSound using lrg_wave_data_to_sound().
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

#define LRG_TYPE_WAVE_DATA (lrg_wave_data_get_type ())
#define LRG_WAVE_DATA_ERROR (lrg_wave_data_error_quark ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWaveData, lrg_wave_data, LRG, WAVE_DATA, GObject)

/**
 * LrgWaveDataError:
 * @LRG_WAVE_DATA_ERROR_FILE_NOT_FOUND: File could not be opened
 * @LRG_WAVE_DATA_ERROR_INVALID_FORMAT: Invalid audio format
 * @LRG_WAVE_DATA_ERROR_INVALID_PARAMS: Invalid parameters
 * @LRG_WAVE_DATA_ERROR_EXPORT_FAILED: Export operation failed
 *
 * Error codes for #LrgWaveData operations.
 */
typedef enum
{
    LRG_WAVE_DATA_ERROR_FILE_NOT_FOUND,
    LRG_WAVE_DATA_ERROR_INVALID_FORMAT,
    LRG_WAVE_DATA_ERROR_INVALID_PARAMS,
    LRG_WAVE_DATA_ERROR_EXPORT_FAILED
} LrgWaveDataError;

LRG_AVAILABLE_IN_ALL
GQuark lrg_wave_data_error_quark (void);

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_wave_data_new_from_file:
 * @path: (type filename): path to the audio file
 * @error: (nullable): return location for a #GError
 *
 * Loads wave data from an audio file.
 *
 * Supported formats: WAV, OGG, MP3, FLAC (depending on raylib build)
 *
 * Returns: (transfer full) (nullable): A new #LrgWaveData, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgWaveData * lrg_wave_data_new_from_file (const gchar  *path,
                                            GError      **error);

/**
 * lrg_wave_data_new_from_memory:
 * @file_type: file type extension (e.g., ".wav", ".ogg")
 * @data: (array length=data_size): audio file data in memory
 * @data_size: size of @data in bytes
 * @error: (nullable): return location for a #GError
 *
 * Loads wave data from a memory buffer containing an audio file.
 *
 * The @file_type parameter specifies the audio format. It should be
 * a file extension including the dot (e.g., ".wav", ".ogg", ".mp3").
 *
 * Returns: (transfer full) (nullable): A new #LrgWaveData, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgWaveData * lrg_wave_data_new_from_memory (const gchar  *file_type,
                                              const guint8 *data,
                                              gsize         data_size,
                                              GError      **error);

/**
 * lrg_wave_data_new_from_samples:
 * @sample_rate: sample rate in Hz (e.g., 44100)
 * @sample_size: bits per sample (8, 16, or 32)
 * @channels: number of channels (1 = mono, 2 = stereo)
 * @data: (array length=data_size): raw sample data
 * @data_size: size of @data in bytes
 *
 * Creates wave data from raw sample data.
 *
 * The data should be in the format specified by @sample_size:
 * - 8-bit: unsigned char (0-255, 128 = silence)
 * - 16-bit: signed short (-32768 to 32767)
 * - 32-bit: float (-1.0 to 1.0)
 *
 * Returns: (transfer full) (nullable): A new #LrgWaveData, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgWaveData * lrg_wave_data_new_from_samples (guint         sample_rate,
                                               guint         sample_size,
                                               guint         channels,
                                               const guint8 *data,
                                               gsize         data_size);

/**
 * lrg_wave_data_new_procedural:
 * @sample_rate: sample rate in Hz (e.g., 44100)
 * @channels: number of channels (1 = mono, 2 = stereo)
 * @duration: duration in seconds
 *
 * Creates empty wave data for procedural generation.
 *
 * Use lrg_wave_data_set_samples() to fill the buffer with generated audio.
 *
 * Returns: (transfer full) (nullable): A new #LrgWaveData
 */
LRG_AVAILABLE_IN_ALL
LrgWaveData * lrg_wave_data_new_procedural (guint  sample_rate,
                                             guint  channels,
                                             gfloat duration);

/**
 * lrg_wave_data_new_from_grl_wave:
 * @wave: a #GrlWave
 *
 * Creates wave data from a graylib #GrlWave.
 *
 * The wave is copied internally.
 *
 * Returns: (transfer full) (nullable): A new #LrgWaveData
 */
LRG_AVAILABLE_IN_ALL
LrgWaveData * lrg_wave_data_new_from_grl_wave (GrlWave *wave);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_wave_data_get_name:
 * @self: a #LrgWaveData
 *
 * Gets the optional name identifier.
 *
 * Returns: (transfer none) (nullable): the name, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_wave_data_get_name (LrgWaveData *self);

/**
 * lrg_wave_data_set_name:
 * @self: a #LrgWaveData
 * @name: (nullable): the name identifier
 *
 * Sets an optional name identifier.
 */
LRG_AVAILABLE_IN_ALL
void lrg_wave_data_set_name (LrgWaveData *self,
                              const gchar *name);

/**
 * lrg_wave_data_get_source_path:
 * @self: a #LrgWaveData
 *
 * Gets the original file path if loaded from file.
 *
 * Returns: (transfer none) (nullable): the source path, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_wave_data_get_source_path (LrgWaveData *self);

/**
 * lrg_wave_data_get_frame_count:
 * @self: a #LrgWaveData
 *
 * Gets the total number of frames in the wave data.
 *
 * A frame is one sample per channel. For stereo audio, each frame
 * contains a left and right sample.
 *
 * Returns: the frame count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_wave_data_get_frame_count (LrgWaveData *self);

/**
 * lrg_wave_data_get_sample_rate:
 * @self: a #LrgWaveData
 *
 * Gets the sample rate (frequency) of the wave data.
 *
 * Returns: the sample rate in Hz
 */
LRG_AVAILABLE_IN_ALL
guint lrg_wave_data_get_sample_rate (LrgWaveData *self);

/**
 * lrg_wave_data_get_sample_size:
 * @self: a #LrgWaveData
 *
 * Gets the bit depth of the wave samples.
 *
 * Returns: bits per sample (8, 16, or 32)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_wave_data_get_sample_size (LrgWaveData *self);

/**
 * lrg_wave_data_get_channels:
 * @self: a #LrgWaveData
 *
 * Gets the number of audio channels.
 *
 * Returns: the channel count (1 = mono, 2 = stereo)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_wave_data_get_channels (LrgWaveData *self);

/**
 * lrg_wave_data_get_duration:
 * @self: a #LrgWaveData
 *
 * Gets the duration of the wave data in seconds.
 *
 * Returns: the duration in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_wave_data_get_duration (LrgWaveData *self);

/**
 * lrg_wave_data_is_valid:
 * @self: a #LrgWaveData
 *
 * Checks if the wave data is valid.
 *
 * Returns: %TRUE if the wave data is valid and ready to use
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_wave_data_is_valid (LrgWaveData *self);

/* ==========================================================================
 * Manipulation (non-destructive, return new instances)
 * ========================================================================== */

/**
 * lrg_wave_data_copy:
 * @self: a #LrgWaveData
 *
 * Creates a deep copy of the wave data.
 *
 * Returns: (transfer full): A new #LrgWaveData with copied data
 */
LRG_AVAILABLE_IN_ALL
LrgWaveData * lrg_wave_data_copy (LrgWaveData *self);

/**
 * lrg_wave_data_crop:
 * @self: a #LrgWaveData
 * @start_time: start time in seconds
 * @end_time: end time in seconds
 *
 * Creates a new wave containing only the specified time range.
 *
 * This is non-destructive - the original wave is not modified.
 *
 * Returns: (transfer full): A new cropped #LrgWaveData
 */
LRG_AVAILABLE_IN_ALL
LrgWaveData * lrg_wave_data_crop (LrgWaveData *self,
                                   gfloat       start_time,
                                   gfloat       end_time);

/**
 * lrg_wave_data_resample:
 * @self: a #LrgWaveData
 * @new_sample_rate: new sample rate in Hz
 *
 * Creates a new wave with the specified sample rate.
 *
 * This is non-destructive - the original wave is not modified.
 *
 * Returns: (transfer full): A new resampled #LrgWaveData
 */
LRG_AVAILABLE_IN_ALL
LrgWaveData * lrg_wave_data_resample (LrgWaveData *self,
                                       guint        new_sample_rate);

/**
 * lrg_wave_data_convert:
 * @self: a #LrgWaveData
 * @sample_rate: new sample rate in Hz
 * @sample_size: new bits per sample (8, 16, or 32)
 * @channels: new channel count (1 or 2)
 *
 * Creates a new wave with converted format settings.
 *
 * This performs resampling and format conversion as needed.
 * This is non-destructive - the original wave is not modified.
 *
 * Returns: (transfer full): A new converted #LrgWaveData
 */
LRG_AVAILABLE_IN_ALL
LrgWaveData * lrg_wave_data_convert (LrgWaveData *self,
                                      guint        sample_rate,
                                      guint        sample_size,
                                      guint        channels);

/* ==========================================================================
 * Sample Access
 * ========================================================================== */

/**
 * lrg_wave_data_get_samples:
 * @self: a #LrgWaveData
 * @out_count: (out): return location for the number of samples
 *
 * Gets all samples as normalized 32-bit floats.
 *
 * The returned array contains all samples across all channels,
 * interleaved for multi-channel audio. Values range from -1.0 to 1.0.
 *
 * The returned array should be freed with g_free() when no longer needed.
 *
 * Returns: (transfer full) (array length=out_count): float sample array
 */
LRG_AVAILABLE_IN_ALL
gfloat * lrg_wave_data_get_samples (LrgWaveData *self,
                                     gsize       *out_count);

/**
 * lrg_wave_data_set_samples:
 * @self: a #LrgWaveData
 * @samples: (array length=count): normalized float samples (-1.0 to 1.0)
 * @count: number of samples
 *
 * Sets the sample data from normalized 32-bit floats.
 *
 * The sample count must match the wave's frame_count * channels.
 * Use lrg_wave_data_new_procedural() to create a wave with the
 * appropriate size for procedural generation.
 */
LRG_AVAILABLE_IN_ALL
void lrg_wave_data_set_samples (LrgWaveData  *self,
                                 const gfloat *samples,
                                 gsize         count);

/* ==========================================================================
 * Export
 * ========================================================================== */

/**
 * lrg_wave_data_export_wav:
 * @self: a #LrgWaveData
 * @path: (type filename): output file path
 * @error: (nullable): return location for a #GError
 *
 * Exports the wave data to a WAV file.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_wave_data_export_wav (LrgWaveData  *self,
                                    const gchar  *path,
                                    GError      **error);

/* ==========================================================================
 * Conversion
 * ========================================================================== */

/**
 * lrg_wave_data_to_sound:
 * @self: a #LrgWaveData
 *
 * Converts the wave data to a playable #GrlSound.
 *
 * Returns: (transfer full) (nullable): A new #GrlSound
 */
LRG_AVAILABLE_IN_ALL
GrlSound * lrg_wave_data_to_sound (LrgWaveData *self);

/**
 * lrg_wave_data_get_grl_wave:
 * @self: a #LrgWaveData
 *
 * Gets the underlying #GrlWave.
 *
 * Returns: (transfer none) (nullable): the underlying #GrlWave
 */
LRG_AVAILABLE_IN_ALL
GrlWave * lrg_wave_data_get_grl_wave (LrgWaveData *self);

G_END_DECLS
