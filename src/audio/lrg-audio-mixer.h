/* Configurable PCM audio graph. SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include "lrg-wave-data.h"
G_BEGIN_DECLS
#define LRG_TYPE_AUDIO_MIXER (lrg_audio_mixer_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAudioMixer, lrg_audio_mixer, LRG, AUDIO_MIXER, GObject)

/**
 * lrg_audio_mixer_new:
 * @sample_rate: sample rate in Hz, 8000 to 192000
 * @channels: 1 for mono or 2 for stereo
 *
 * Creates a headless mixer with a master bus. All operations run on one thread.
 *
 * Returns: (transfer full) (nullable): a mixer, or NULL for invalid format
 */
LRG_AVAILABLE_IN_ALL
LrgAudioMixer *
lrg_audio_mixer_new (guint sample_rate,
                     guint channels);

/**
 * lrg_audio_mixer_add_bus:
 * @self: a mixer
 * @name: unique nonempty name
 * @parent: existing output bus name
 *
 * Adds a bus with unity gain and an output to its parent. Master always exists.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_add_bus (LrgAudioMixer *self,
                         const gchar *name,
                         const gchar *parent);

/**
 * lrg_audio_mixer_set_bus:
 * @self: a mixer
 * @name: bus name
 * @gain: finite linear gain from 0 to 4
 * @muted: whether to silence the bus and its sends
 *
 * Sets post-effect bus gain and mute. Muted voices and effect histories still advance.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_set_bus (LrgAudioMixer *self,
                         const gchar *name,
                         gdouble gain,
                         gboolean muted);

/**
 * lrg_audio_mixer_set_send:
 * @self: a mixer
 * @source: source bus name
 * @target: destination bus name
 * @gain: finite post-fader send gain from 0 to 4; zero removes a send
 *
 * Adds or edits a send. Cycles, self-sends, master sends, and sends to the
 * primary parent are rejected without changing the graph.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_set_send (LrgAudioMixer *self,
                          const gchar *source,
                          const gchar *target,
                          gdouble gain);

/**
 * lrg_audio_mixer_set_effects:
 * @self: a mixer
 * @name: bus name
 * @cutoff: low-pass cutoff Hz; zero bypasses, otherwise below Nyquist
 * @delay_seconds: delay length from 0 to 2 seconds; zero bypasses
 * @feedback: delay feedback from 0 to less than 1
 * @wet: delay wet blend from 0 to 1
 *
 * Configures a low-pass followed by a feedback delay. Changing settings resets
 * effect histories. Invalid settings leave the existing effects unchanged.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_set_effects (LrgAudioMixer *self,
                             const gchar *name,
                             gdouble cutoff,
                             gdouble delay_seconds,
                             gdouble feedback,
                             gdouble wet);

/**
 * lrg_audio_mixer_play:
 * @self: a mixer
 * @wave: source wave
 * @bus: destination bus name
 * @gain: finite voice gain from 0 to 4
 * @loop: whether to loop
 *
 * Copies and converts wave samples to the mixer format. No audio device is
 * needed. Each call creates an independent voice; zero-length waves are rejected.
 *
 * Returns: a nonzero voice ID, or zero on failure
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_audio_mixer_play (LrgAudioMixer *self,
                      LrgWaveData *wave,
                      const gchar *bus,
                      gdouble gain,
                      gboolean loop);

/**
 * lrg_audio_mixer_stop_voice:
 * @self: a mixer
 * @id: voice ID
 *
 * Removes a voice. Existing effect tails continue to decay.
 *
 * Returns: %TRUE if the voice existed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_stop_voice (LrgAudioMixer *self,
                            guint64 id);

/**
 * lrg_audio_mixer_pause_voice:
 * @self: a mixer
 * @id: voice ID
 * @paused: whether to freeze the voice cursor
 *
 * Pauses or resumes one voice without affecting other voices or effect tails.
 *
 * Returns: %TRUE if the voice existed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_pause_voice (LrgAudioMixer *self,
                             guint64 id,
                             gboolean paused);

/**
 * lrg_audio_mixer_get_voice_count:
 * @self: a mixer
 *
 * Counts active and paused voices.
 *
 * Returns: the voice count
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_audio_mixer_get_voice_count (LrgAudioMixer *self);

/**
 * lrg_audio_mixer_render:
 * @self: a mixer
 * @frames: frames to render, 1 to 65536
 * @n_samples: (out): number of interleaved samples
 *
 * Advances voices and effects and returns master output, clamped to [-1,1].
 * Intermediate buses are not clipped. Does not access an audio device.
 *
 * Returns: (transfer full) (array length=n_samples) (nullable): PCM float samples, or NULL for
 * invalid frame count
 */
LRG_AVAILABLE_IN_ALL
gfloat *
lrg_audio_mixer_render (LrgAudioMixer *self,
                        guint frames,
                        gsize *n_samples);

/**
 * lrg_audio_mixer_pump:
 * @self: a mixer
 * @stream: a valid 32-bit stream matching mixer rate and channels
 * @frames: refill frame count, 1 to 65536; must fit the stream buffer
 * @error: (nullable): error return location
 *
 * Renders and feeds one block only when the stream requests data. The caller
 * owns the stream, initializes the audio device, and controls stream playback.
 * Call on the same thread as graph edits. Errors use G_IO_ERROR.
 *
 * Returns: %TRUE on success (including when no refill is needed)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_pump (LrgAudioMixer *self,
                      GrlAudioStream *stream,
                      guint frames,
                      GError **error);

G_END_DECLS
