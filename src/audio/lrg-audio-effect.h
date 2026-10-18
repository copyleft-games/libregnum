/* lrg-audio-effect.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * DSP effects on interleaved float buffers, and a chain of them that can
 * run on the audio device's final mix.
 *
 * #LrgAudioEffect is the abstract base of a sample processor: it gets a
 * block of interleaved 32-bit float frames and rewrites it in place.
 * #LrgReverb and #LrgLowPass are the effects libregnum ships;
 * applications subclass #LrgAudioEffect for their own.  Processing is
 * headless (the tests feed synthetic buffers); #LrgAudioEffectChain runs
 * several effects in order and, with lrg_audio_effect_chain_attach_mix(),
 * on the device's mixed output through raylib's mixed-audio processor
 * (raudio is wrapped, not modified).
 *
 * Applications can probe for this API at compile time with
 * `#ifdef LRG_AUDIO_HAS_EFFECTS`.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * LRG_AUDIO_HAS_EFFECTS:
 *
 * Defined when this libregnum provides #LrgAudioEffect, #LrgReverb,
 * #LrgLowPass, #LrgAudioEffectChain, #LrgSoundBed and the MIDI output
 * types (#LrgMidiOutput, #LrgMidiSequencer).
 */
#define LRG_AUDIO_HAS_EFFECTS (1)

/**
 * LRG_AUDIO_EFFECT_MAX_CHANNELS:
 *
 * The most interleaved channels an effect processes (more are left as
 * they are).
 */
#define LRG_AUDIO_EFFECT_MAX_CHANNELS (8)

/**
 * LRG_AUDIO_EFFECT_DEFAULT_SAMPLE_RATE:
 *
 * The rate lrg_audio_effect_chain_attach_mix() assumes when the caller
 * passes 0: raylib opens the device at the system's default rate and does
 * not report it, and 48000 Hz is that default on PipeWire and
 * PulseAudio.  A wrong guess only scales the effects' delay lengths and
 * cut-off frequencies by the ratio of the two rates.
 */
#define LRG_AUDIO_EFFECT_DEFAULT_SAMPLE_RATE (48000)

#define LRG_TYPE_AUDIO_EFFECT (lrg_audio_effect_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgAudioEffect, lrg_audio_effect, LRG, AUDIO_EFFECT, GObject)

/**
 * LrgAudioEffectClass:
 * @parent_class: the parent class
 * @process: rewrites @n_frames interleaved frames of @channels floats at
 *   @sample_rate in place; called only while the effect is enabled
 * @reset: forgets the effect's history (delay lines, filter state)
 *
 * The class of a DSP effect.  @process runs on the audio thread when the
 * effect sits in an attached #LrgAudioEffectChain: it must not block,
 * and it reads the effect's parameters as plain floats (setters on the
 * main thread store them; a block may see an old value, never a torn
 * one).
 */
struct _LrgAudioEffectClass
{
    GObjectClass parent_class;

    /*< public >*/
    void (*process) (LrgAudioEffect *self,
                     gfloat         *frames,
                     guint           n_frames,
                     guint           channels,
                     guint           sample_rate);
    void (*reset)   (LrgAudioEffect *self);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_audio_effect_process:
 * @self: an #LrgAudioEffect
 * @frames: (array) (inout): @n_frames x @channels interleaved samples
 * @n_frames: frames in @frames
 * @channels: samples per frame (1 .. %LRG_AUDIO_EFFECT_MAX_CHANNELS)
 * @sample_rate: the rate of @frames in Hz (8000 .. 192000)
 *
 * Runs the effect over the block in place.  A disabled effect, an empty
 * block or an unsupported layout leaves the samples untouched.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_effect_process (LrgAudioEffect *self,
                               gfloat         *frames,
                               guint           n_frames,
                               guint           channels,
                               guint           sample_rate);

/**
 * lrg_audio_effect_reset:
 * @self: an #LrgAudioEffect
 *
 * Clears the effect's history so the next block starts from silence
 * (no tail of what came before).
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_effect_reset (LrgAudioEffect *self);

/**
 * lrg_audio_effect_set_enabled:
 * @self: an #LrgAudioEffect
 * @enabled: whether lrg_audio_effect_process() changes the samples
 *
 * Disabling resets the effect, so enabling it again starts clean.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_effect_set_enabled (LrgAudioEffect *self,
                                   gboolean        enabled);

/**
 * lrg_audio_effect_get_enabled:
 * @self: an #LrgAudioEffect
 *
 * Returns: whether the effect processes (default %TRUE)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_effect_get_enabled (LrgAudioEffect *self);

/* ==========================================================================
 * Chains
 * ========================================================================== */

#define LRG_TYPE_AUDIO_EFFECT_CHAIN (lrg_audio_effect_chain_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAudioEffectChain, lrg_audio_effect_chain, LRG, AUDIO_EFFECT_CHAIN, GObject)

/**
 * lrg_audio_effect_chain_new:
 *
 * Returns: (transfer full): an empty chain
 */
LRG_AVAILABLE_IN_ALL
LrgAudioEffectChain * lrg_audio_effect_chain_new (void);

/**
 * lrg_audio_effect_chain_add:
 * @self: an #LrgAudioEffectChain
 * @effect: (transfer none): the effect to run after the ones added before
 *
 * The chain keeps a reference.  Adding an effect twice does nothing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_effect_chain_add (LrgAudioEffectChain *self,
                                 LrgAudioEffect      *effect);

/**
 * lrg_audio_effect_chain_remove:
 * @self: an #LrgAudioEffectChain
 * @effect: an effect of the chain
 *
 * Returns: %TRUE when @effect was in the chain
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_effect_chain_remove (LrgAudioEffectChain *self,
                                        LrgAudioEffect      *effect);

/**
 * lrg_audio_effect_chain_get_count:
 * @self: an #LrgAudioEffectChain
 *
 * Returns: the number of effects
 */
LRG_AVAILABLE_IN_ALL
guint lrg_audio_effect_chain_get_count (LrgAudioEffectChain *self);

/**
 * lrg_audio_effect_chain_get_effect:
 * @self: an #LrgAudioEffectChain
 * @index: 0 .. lrg_audio_effect_chain_get_count() - 1
 *
 * Returns: (transfer none) (nullable): the effect, %NULL out of range
 */
LRG_AVAILABLE_IN_ALL
LrgAudioEffect * lrg_audio_effect_chain_get_effect (LrgAudioEffectChain *self,
                                                    guint                index);

/**
 * lrg_audio_effect_chain_process:
 * @self: an #LrgAudioEffectChain
 * @frames: (array) (inout): interleaved samples
 * @n_frames: frames in @frames
 * @channels: samples per frame
 * @sample_rate: rate in Hz
 *
 * Runs every effect in order (lrg_audio_effect_process()).  Safe to call
 * from the audio thread while the main thread adds or removes effects.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_effect_chain_process (LrgAudioEffectChain *self,
                                     gfloat              *frames,
                                     guint                n_frames,
                                     guint                channels,
                                     guint                sample_rate);

/**
 * lrg_audio_effect_chain_reset:
 * @self: an #LrgAudioEffectChain
 *
 * Resets every effect of the chain.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_effect_chain_reset (LrgAudioEffectChain *self);

/**
 * lrg_audio_effect_chain_attach_mix:
 * @self: an #LrgAudioEffectChain
 * @sample_rate: the device's rate in Hz, 0 for
 *   %LRG_AUDIO_EFFECT_DEFAULT_SAMPLE_RATE
 *
 * Runs the chain on the audio device's final stereo mix (every sound,
 * music stream and voice) from now on, through raylib's mixed-audio
 * processor.  One chain can be attached at a time: attaching another
 * detaches the previous one.  The device must be open; without one
 * nothing is attached and %FALSE is returned.
 *
 * Returns: %TRUE when the chain now processes the mix
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_effect_chain_attach_mix (LrgAudioEffectChain *self,
                                            guint                sample_rate);

/**
 * lrg_audio_effect_chain_detach_mix:
 * @self: an #LrgAudioEffectChain
 *
 * Stops processing the device mix (no-op when not attached).  Disposing
 * an attached chain detaches it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_effect_chain_detach_mix (LrgAudioEffectChain *self);

/**
 * lrg_audio_effect_chain_is_attached:
 * @self: an #LrgAudioEffectChain
 *
 * Returns: %TRUE while the chain processes the device mix
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_effect_chain_is_attached (LrgAudioEffectChain *self);

G_END_DECLS
