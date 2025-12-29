/* lrg-procedural-audio.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Procedural audio generation for synthesizers and real-time audio.
 *
 * LrgProceduralAudio wraps graylib's GrlAudioStream, providing a
 * derivable GObject with a virtual generate() method for custom
 * audio synthesis.
 *
 * To create a custom synthesizer:
 * 1. Subclass LrgProceduralAudio
 * 2. Override the generate() virtual method
 * 3. Fill the buffer with audio samples (-1.0 to 1.0)
 * 4. Call lrg_procedural_audio_update() each frame during playback
 *
 * Example subclass:
 * |[<!-- language="C" -->
 * static void
 * my_synth_generate (LrgProceduralAudio *self,
 *                    gfloat             *buffer,
 *                    gint                frame_count)
 * {
 *     MySynth *synth = MY_SYNTH (self);
 *     guint channels = lrg_procedural_audio_get_channels (self);
 *     guint sample_rate = lrg_procedural_audio_get_sample_rate (self);
 *
 *     for (gint i = 0; i < frame_count; i++)
 *     {
 *         gfloat sample = sinf (synth->phase * 2.0f * G_PI);
 *         synth->phase += synth->frequency / sample_rate;
 *         if (synth->phase >= 1.0f) synth->phase -= 1.0f;
 *
 *         for (guint c = 0; c < channels; c++)
 *             buffer[i * channels + c] = sample;
 *     }
 * }
 * ]|
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

#define LRG_TYPE_PROCEDURAL_AUDIO (lrg_procedural_audio_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgProceduralAudio, lrg_procedural_audio, LRG, PROCEDURAL_AUDIO, GObject)

/**
 * LrgProceduralAudioClass:
 * @parent_class: The parent class
 * @generate: Virtual method to generate audio samples. Subclasses
 *            must override this to produce audio data. The buffer
 *            contains interleaved samples for all channels. Values
 *            should be in the range -1.0 to 1.0.
 *
 * The class structure for #LrgProceduralAudio.
 */
struct _LrgProceduralAudioClass
{
    GObjectClass parent_class;

    /*< public >*/

    /**
     * LrgProceduralAudioClass::generate:
     * @self: the procedural audio instance
     * @buffer: (array length=frame_count): output buffer for audio samples
     * @frame_count: number of frames to generate (samples = frames * channels)
     *
     * Virtual method to generate audio samples.
     *
     * Subclasses must override this method to produce audio data.
     * Fill the buffer with normalized float samples in the range
     * -1.0 to 1.0. The buffer is interleaved for multi-channel
     * audio (left, right, left, right, ...).
     */
    void (*generate) (LrgProceduralAudio *self,
                      gfloat             *buffer,
                      gint                frame_count);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_procedural_audio_new:
 * @sample_rate: sample rate in Hz (e.g., 44100, 48000)
 * @channels: number of channels (1 = mono, 2 = stereo)
 *
 * Creates a new procedural audio source.
 *
 * Note: This creates an instance of the base class which produces
 * silence. For useful audio, subclass LrgProceduralAudio and override
 * the generate() virtual method.
 *
 * Returns: (transfer full) (nullable): A new #LrgProceduralAudio
 */
LRG_AVAILABLE_IN_ALL
LrgProceduralAudio * lrg_procedural_audio_new (guint sample_rate,
                                                guint channels);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_procedural_audio_get_name:
 * @self: a #LrgProceduralAudio
 *
 * Gets the optional name identifier.
 *
 * Returns: (transfer none) (nullable): the name, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_procedural_audio_get_name (LrgProceduralAudio *self);

/**
 * lrg_procedural_audio_set_name:
 * @self: a #LrgProceduralAudio
 * @name: (nullable): the name identifier
 *
 * Sets an optional name identifier for debugging.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procedural_audio_set_name (LrgProceduralAudio *self,
                                     const gchar        *name);

/**
 * lrg_procedural_audio_get_sample_rate:
 * @self: a #LrgProceduralAudio
 *
 * Gets the sample rate.
 *
 * Returns: the sample rate in Hz
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procedural_audio_get_sample_rate (LrgProceduralAudio *self);

/**
 * lrg_procedural_audio_get_channels:
 * @self: a #LrgProceduralAudio
 *
 * Gets the number of audio channels.
 *
 * Returns: the channel count (1 = mono, 2 = stereo)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procedural_audio_get_channels (LrgProceduralAudio *self);

/**
 * lrg_procedural_audio_is_valid:
 * @self: a #LrgProceduralAudio
 *
 * Checks if the audio stream is valid and ready for playback.
 *
 * Returns: %TRUE if valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procedural_audio_is_valid (LrgProceduralAudio *self);

/* ==========================================================================
 * Playback Control
 * ========================================================================== */

/**
 * lrg_procedural_audio_play:
 * @self: a #LrgProceduralAudio
 *
 * Starts playing the procedural audio.
 *
 * After calling this, you must call lrg_procedural_audio_update()
 * each frame to generate and feed audio data to the stream.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procedural_audio_play (LrgProceduralAudio *self);

/**
 * lrg_procedural_audio_stop:
 * @self: a #LrgProceduralAudio
 *
 * Stops the procedural audio playback.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procedural_audio_stop (LrgProceduralAudio *self);

/**
 * lrg_procedural_audio_pause:
 * @self: a #LrgProceduralAudio
 *
 * Pauses the procedural audio playback.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procedural_audio_pause (LrgProceduralAudio *self);

/**
 * lrg_procedural_audio_resume:
 * @self: a #LrgProceduralAudio
 *
 * Resumes paused procedural audio.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procedural_audio_resume (LrgProceduralAudio *self);

/**
 * lrg_procedural_audio_is_playing:
 * @self: a #LrgProceduralAudio
 *
 * Checks if the audio is currently playing.
 *
 * Returns: %TRUE if playing
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procedural_audio_is_playing (LrgProceduralAudio *self);

/**
 * lrg_procedural_audio_update:
 * @self: a #LrgProceduralAudio
 *
 * Updates the audio stream by generating new samples if needed.
 *
 * This should be called every frame while the audio is playing.
 * It checks if the audio buffer needs more data, and if so,
 * calls the generate() virtual method to produce samples.
 *
 * If subclasses do not override generate(), silence is produced.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procedural_audio_update (LrgProceduralAudio *self);

/* ==========================================================================
 * Audio Parameters
 * ========================================================================== */

/**
 * lrg_procedural_audio_set_volume:
 * @self: a #LrgProceduralAudio
 * @volume: volume level (0.0 to 1.0)
 *
 * Sets the playback volume.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procedural_audio_set_volume (LrgProceduralAudio *self,
                                       gfloat              volume);

/**
 * lrg_procedural_audio_get_volume:
 * @self: a #LrgProceduralAudio
 *
 * Gets the current volume level.
 *
 * Returns: the volume (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_procedural_audio_get_volume (LrgProceduralAudio *self);

/**
 * lrg_procedural_audio_set_pitch:
 * @self: a #LrgProceduralAudio
 * @pitch: pitch multiplier (1.0 = normal)
 *
 * Sets the playback pitch.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procedural_audio_set_pitch (LrgProceduralAudio *self,
                                      gfloat              pitch);

/**
 * lrg_procedural_audio_get_pitch:
 * @self: a #LrgProceduralAudio
 *
 * Gets the current pitch multiplier.
 *
 * Returns: the pitch multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_procedural_audio_get_pitch (LrgProceduralAudio *self);

/**
 * lrg_procedural_audio_set_pan:
 * @self: a #LrgProceduralAudio
 * @pan: pan position (-1.0 = left, 0.0 = center, 1.0 = right)
 *
 * Sets the stereo pan position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procedural_audio_set_pan (LrgProceduralAudio *self,
                                    gfloat              pan);

/**
 * lrg_procedural_audio_get_pan:
 * @self: a #LrgProceduralAudio
 *
 * Gets the current pan position.
 *
 * Returns: the pan position (-1.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_procedural_audio_get_pan (LrgProceduralAudio *self);

/* ==========================================================================
 * Access Underlying
 * ========================================================================== */

/**
 * lrg_procedural_audio_get_audio_stream:
 * @self: a #LrgProceduralAudio
 *
 * Gets the underlying #GrlAudioStream.
 *
 * Returns: (transfer none) (nullable): the underlying #GrlAudioStream
 */
LRG_AVAILABLE_IN_ALL
GrlAudioStream * lrg_procedural_audio_get_audio_stream (LrgProceduralAudio *self);

G_END_DECLS
