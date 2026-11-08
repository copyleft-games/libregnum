/* Configurable PCM audio graph. SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include "lrg-wave-data.h"
#include "lrg-audio-effect.h"
#include "lrg-audio-source.h"
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

/* ==========================================================================
 * 0.9.0: levels in decibels, graph inspection, solo, effect chains,
 * ramps (snapshots), ducking, meters and spatial voice controls
 * ========================================================================== */

/**
 * LRG_AUDIO_SILENCE_DB:
 *
 * The level lrg_audio_gain_to_db() reports for silence, and the level at
 * or below which lrg_audio_db_to_gain() returns 0.
 */
#define LRG_AUDIO_SILENCE_DB (-144.0)

/**
 * LRG_AUDIO_MIXER_MAX_GAIN_DB:
 *
 * The loudest bus, send and voice level in decibels: a linear gain of 4.
 */
#define LRG_AUDIO_MIXER_MAX_GAIN_DB (12.0412)

/**
 * LRG_AUDIO_MIXER_BLOCK_FRAMES:
 *
 * Renders are processed in blocks of this many frames: effect-parameter
 * ramps advance once per block, bus and send gain ramps every frame.
 */
#define LRG_AUDIO_MIXER_BLOCK_FRAMES (256)

/**
 * lrg_audio_db_to_gain:
 * @db: a level in decibels
 *
 * Converts decibels to a linear gain (`10^(db/20)`).
 *
 * Returns: the gain; 0 for @db at or below %LRG_AUDIO_SILENCE_DB or NaN
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_audio_db_to_gain (gdouble db);

/**
 * lrg_audio_gain_to_db:
 * @gain: a linear gain
 *
 * Converts a linear gain to decibels (`20 log10 gain`).
 *
 * Returns: the level, never below %LRG_AUDIO_SILENCE_DB
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_audio_gain_to_db (gdouble gain);

/**
 * lrg_audio_mixer_get_sample_rate:
 * @self: a mixer
 *
 * Returns: the sample rate in Hz
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_audio_mixer_get_sample_rate (LrgAudioMixer *self);

/**
 * lrg_audio_mixer_get_channels:
 * @self: a mixer
 *
 * Returns: 1 or 2
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_audio_mixer_get_channels (LrgAudioMixer *self);

/**
 * lrg_audio_mixer_get_bus_count:
 * @self: a mixer
 *
 * Returns: the number of buses, master included
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_audio_mixer_get_bus_count (LrgAudioMixer *self);

/**
 * lrg_audio_mixer_get_bus_name:
 * @self: a mixer
 * @index: 0 (master) .. lrg_audio_mixer_get_bus_count() - 1, in creation order
 *
 * Returns: (transfer none) (nullable): the bus name, %NULL out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_audio_mixer_get_bus_name (LrgAudioMixer *self,
                              guint          index);

/**
 * lrg_audio_mixer_has_bus:
 * @self: a mixer
 * @name: a bus name
 *
 * Returns: whether the bus exists
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_has_bus (LrgAudioMixer *self,
                         const gchar   *name);

/**
 * lrg_audio_mixer_get_bus_parent:
 * @self: a mixer
 * @name: a bus name
 *
 * Returns: (transfer none) (nullable): the parent bus name; %NULL for
 *   master and unknown buses
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_audio_mixer_get_bus_parent (LrgAudioMixer *self,
                                const gchar   *name);

/**
 * lrg_audio_mixer_get_bus_gain:
 * @self: a mixer
 * @name: a bus name
 *
 * The fader gain now: while a ramp runs, the value it has reached.
 *
 * Returns: the linear gain, or -1 for an unknown bus
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_audio_mixer_get_bus_gain (LrgAudioMixer *self,
                              const gchar   *name);

/**
 * lrg_audio_mixer_get_bus_muted:
 * @self: a mixer
 * @name: a bus name
 *
 * Returns: whether the bus is muted (%FALSE for an unknown bus)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_get_bus_muted (LrgAudioMixer *self,
                               const gchar   *name);

/**
 * lrg_audio_mixer_set_bus_solo:
 * @self: a mixer
 * @name: a bus name
 * @solo: whether the bus is soloed
 *
 * While any bus is soloed only the soloed buses, the buses feeding them
 * (their children, recursively) and the buses they feed (parents and
 * send targets, recursively) are heard; every other bus outputs
 * silence while its voices and effects keep running, as a mute does.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_set_bus_solo (LrgAudioMixer *self,
                              const gchar   *name,
                              gboolean       solo);

/**
 * lrg_audio_mixer_get_bus_solo:
 * @self: a mixer
 * @name: a bus name
 *
 * Returns: whether the bus is soloed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_get_bus_solo (LrgAudioMixer *self,
                              const gchar   *name);

/**
 * lrg_audio_mixer_is_bus_audible:
 * @self: a mixer
 * @name: a bus name
 *
 * Whether the bus is heard: not muted and not silenced by another bus's
 * solo (see lrg_audio_mixer_set_bus_solo()).
 *
 * Returns: %TRUE when the bus output reaches its parent and sends
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_is_bus_audible (LrgAudioMixer *self,
                                const gchar   *name);

/**
 * lrg_audio_mixer_get_send:
 * @self: a mixer
 * @source: source bus name
 * @target: destination bus name
 *
 * Returns: the send gain now (0 when there is no send)
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_audio_mixer_get_send (LrgAudioMixer *self,
                          const gchar   *source,
                          const gchar   *target);

/**
 * lrg_audio_mixer_list_sends:
 * @self: a mixer
 * @source: source bus name
 *
 * Returns: (transfer full) (array zero-terminated=1): the target names of
 *   the sends of @source with a gain above 0, in creation order
 */
LRG_AVAILABLE_IN_ALL
gchar **
lrg_audio_mixer_list_sends (LrgAudioMixer *self,
                            const gchar   *source);

/**
 * lrg_audio_mixer_get_bus_chain:
 * @self: a mixer
 * @name: a bus name
 *
 * The bus's effect chain, created on first use.  It runs after the
 * built-in low-pass and delay of lrg_audio_mixer_set_effects() and before
 * ducking and the fader.  Add, remove and configure effects between
 * renders; they run at the mixer's rate and channel count.
 *
 * Returns: (transfer none) (nullable): the chain, %NULL for an unknown bus
 */
LRG_AVAILABLE_IN_ALL
LrgAudioEffectChain *
lrg_audio_mixer_get_bus_chain (LrgAudioMixer *self,
                               const gchar   *name);

/**
 * lrg_audio_mixer_ramp_bus_gain:
 * @self: a mixer
 * @name: a bus name
 * @gain: the target gain, 0 to 4
 * @seconds: ramp length; 0 or less sets the gain at once
 *
 * Moves the fader linearly from its current gain to @gain over @seconds
 * of rendered audio, one step per frame (snapshot blending).  A new ramp
 * or lrg_audio_mixer_set_bus() replaces a running one.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_ramp_bus_gain (LrgAudioMixer *self,
                               const gchar   *name,
                               gdouble        gain,
                               gdouble        seconds);

/**
 * lrg_audio_mixer_ramp_send:
 * @self: a mixer
 * @source: source bus name
 * @target: destination bus name
 * @gain: the target send gain, 0 to 4
 * @seconds: ramp length; 0 or less sets the gain at once
 *
 * Like lrg_audio_mixer_ramp_bus_gain() for a send.  A missing send is
 * created at gain 0 first (the same routing rules as
 * lrg_audio_mixer_set_send() apply); a send ramped to 0 stays in the
 * graph silent.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_ramp_send (LrgAudioMixer *self,
                           const gchar   *source,
                           const gchar   *target,
                           gdouble        gain,
                           gdouble        seconds);

/**
 * lrg_audio_mixer_ramp_effect_param:
 * @self: a mixer
 * @bus: a bus name
 * @index: the effect's position in the bus chain
 * @property: a numeric (float, double, int or uint) property of the effect
 * @value: the target value
 * @seconds: ramp length; 0 or less sets the value at once
 *
 * Moves an effect parameter linearly to @value; the property is set at
 * the start of every %LRG_AUDIO_MIXER_BLOCK_FRAMES block.  The effect's
 * own setter clamps the value.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_ramp_effect_param (LrgAudioMixer *self,
                                   const gchar   *bus,
                                   guint          index,
                                   const gchar   *property,
                                   gdouble        value,
                                   gdouble        seconds);

/**
 * lrg_audio_mixer_is_ramping:
 * @self: a mixer
 *
 * Returns: whether any bus, send or effect-parameter ramp is running
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_is_ramping (LrgAudioMixer *self);

/**
 * lrg_audio_mixer_set_duck:
 * @self: a mixer
 * @trigger: the side-chain bus (dialogue)
 * @target: the bus turned down (music)
 * @threshold_db: trigger level (per-frame peak of its fader output) at
 *   or above which the target ducks
 * @depth_db: the target's gain while ducked, -144 to 0 dB
 * @attack: seconds (time constant) to duck, 0 to 10
 * @release: seconds (time constant) to recover, 0 to 10
 *
 * Adds or edits a side-chain ducking rule.  Every frame the rule's gain
 * moves toward depth (trigger at or above the threshold) or 1 (below)
 * by a one-pole step `g += (1 - exp (-1 / (seconds * rate))) (goal - g)`,
 * so after a step the gap closes to 1/e in @attack or @release seconds.
 * The trigger is rendered before the target; a rule whose target feeds
 * the trigger (a cycle) or with master as trigger is rejected.  Several
 * rules on one target multiply.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_set_duck (LrgAudioMixer *self,
                          const gchar   *trigger,
                          const gchar   *target,
                          gdouble        threshold_db,
                          gdouble        depth_db,
                          gdouble        attack,
                          gdouble        release);

/**
 * lrg_audio_mixer_remove_duck:
 * @self: a mixer
 * @trigger: the side-chain bus
 * @target: the ducked bus
 *
 * Returns: %TRUE when the rule existed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_remove_duck (LrgAudioMixer *self,
                             const gchar   *trigger,
                             const gchar   *target);

/**
 * lrg_audio_mixer_clear_ducks:
 * @self: a mixer
 *
 * Removes every ducking rule.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_audio_mixer_clear_ducks (LrgAudioMixer *self);

/**
 * lrg_audio_mixer_get_duck_count:
 * @self: a mixer
 *
 * Returns: the number of ducking rules
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_audio_mixer_get_duck_count (LrgAudioMixer *self);

/**
 * lrg_audio_mixer_get_bus_duck_gain:
 * @self: a mixer
 * @name: a bus name
 *
 * The product of the ducking gains on the bus at the end of the last
 * render, for a gain-reduction meter.
 *
 * Returns: 0 .. 1 (1 when not ducked), or -1 for an unknown bus
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_audio_mixer_get_bus_duck_gain (LrgAudioMixer *self,
                                   const gchar   *name);

/**
 * lrg_audio_mixer_get_bus_meter:
 * @self: a mixer
 * @name: a bus name
 * @channel: 0 (left or mono), 1 (right), or -1 for all channels
 * @out_peak: (out) (optional): the largest absolute sample
 * @out_rms: (out) (optional): the root mean square
 *
 * Meters of the bus output (after effects, ducking, fader, mute and
 * solo: what reaches its parent) over the last lrg_audio_mixer_render()
 * or lrg_audio_mixer_pump() call.  Linear values; see
 * lrg_audio_gain_to_db().  Master's meter is before the final clip.
 *
 * Returns: %TRUE for a known bus and channel
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_get_bus_meter (LrgAudioMixer *self,
                               const gchar   *name,
                               gint           channel,
                               gdouble       *out_peak,
                               gdouble       *out_rms);

/**
 * lrg_audio_mixer_has_voice:
 * @self: a mixer
 * @id: voice ID
 *
 * Returns: whether the voice is still playing or paused
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_has_voice (LrgAudioMixer *self,
                           guint64        id);

/**
 * lrg_audio_mixer_set_voice_gain:
 * @self: a mixer
 * @id: voice ID
 * @gain: 0 to 4
 * @seconds: ramp length (per frame, linear); 0 or less jumps
 *
 * Changes a voice's gain (spatial attenuation, occlusion).  Ramping over
 * the time until the next update avoids zipper noise.
 *
 * Returns: %TRUE when the voice exists and @gain is valid
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_set_voice_gain (LrgAudioMixer *self,
                                guint64        id,
                                gdouble        gain,
                                gdouble        seconds);

/**
 * lrg_audio_mixer_set_voice_pan:
 * @self: a mixer
 * @id: voice ID
 * @pan: -1 (left) .. 1 (right)
 * @seconds: ramp length; 0 or less jumps
 *
 * Balance of a voice in a stereo mixer: the far channel is scaled by
 * `1 - |pan|`, the near one stays at unity (a centred voice is unchanged).
 * Ignored by mono mixers.
 *
 * Returns: %TRUE when the voice exists and @pan is valid
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_set_voice_pan (LrgAudioMixer *self,
                               guint64        id,
                               gdouble        pan,
                               gdouble        seconds);

/**
 * lrg_audio_mixer_set_voice_pitch:
 * @self: a mixer
 * @id: voice ID
 * @pitch: playback rate, 0.125 to 8 (1 plays the samples as they are)
 *
 * Changes how fast the voice reads its samples (doppler, variation),
 * with linear interpolation between frames.
 *
 * Returns: %TRUE when the voice exists and @pitch is valid
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_set_voice_pitch (LrgAudioMixer *self,
                                 guint64        id,
                                 gdouble        pitch);

/**
 * lrg_audio_mixer_set_voice_lowpass:
 * @self: a mixer
 * @id: voice ID
 * @cutoff: one-pole low-pass cut-off in Hz; 0 turns it off
 *
 * Filters one voice (occlusion muffling) before it reaches its bus.
 *
 * Returns: %TRUE when the voice exists and @cutoff is below Nyquist
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_set_voice_lowpass (LrgAudioMixer *self,
                                   guint64        id,
                                   gdouble        cutoff);

/**
 * lrg_audio_mixer_get_voice_bus:
 * @self: a mixer
 * @id: voice ID
 *
 * Returns: (transfer none) (nullable): the bus the voice plays into
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_audio_mixer_get_voice_bus (LrgAudioMixer *self,
                               guint64        id);

/* ==========================================================================
 * 0.10.0: pull-source voices and unclipped output
 * ========================================================================== */

/**
 * lrg_audio_mixer_play_source:
 * @self: a mixer
 * @source: an #LrgAudioSource (an instrument, a synthesizer)
 * @bus: destination bus name
 * @gain: finite voice gain from 0 to 4
 *
 * Plays @source as a voice: every block the mixer asks the source for the
 * next frames at the mixer's rate and channel count, then applies the
 * voice gain and pan (lrg_audio_mixer_set_voice_gain(),
 * lrg_audio_mixer_set_voice_pan()) like a wave voice.  Pitch and the voice
 * low-pass do not apply to source voices.  The voice ends when the source
 * reports it is finished, or with lrg_audio_mixer_stop_voice().  The mixer
 * keeps a reference to @source while the voice exists.
 *
 * Returns: a nonzero voice ID, or zero on failure
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_audio_mixer_play_source (LrgAudioMixer  *self,
                             LrgAudioSource *source,
                             const gchar    *bus,
                             gdouble         gain);

/**
 * lrg_audio_mixer_set_clip:
 * @self: a mixer
 * @clip: whether the master output is clamped to [-1, 1]
 *
 * By default lrg_audio_mixer_render() and lrg_audio_mixer_pump() clamp the
 * master output to [-1, 1].  An offline render that is measured and
 * normalised afterwards (a song mixdown) turns the clamp off so peaks
 * above full scale are kept instead of flattened.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_audio_mixer_set_clip (LrgAudioMixer *self,
                          gboolean       clip);

/**
 * lrg_audio_mixer_get_clip:
 * @self: a mixer
 *
 * Returns: whether the master output is clamped (the default)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_mixer_get_clip (LrgAudioMixer *self);

G_END_DECLS
