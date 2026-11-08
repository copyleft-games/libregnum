/* lrg-audio-source.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A pull source of interleaved float audio that an #LrgAudioMixer plays
 * as a voice: instruments, synthesizers and anything else that makes
 * samples while it plays instead of holding a finished #LrgWaveData.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_AUDIO_SOURCE (lrg_audio_source_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgAudioSource, lrg_audio_source, LRG, AUDIO_SOURCE, GObject)

/**
 * LrgAudioSourceInterface:
 * @parent_iface: the parent interface
 * @render: adds @frames frames of @channels interleaved samples at
 *   @sample_rate into @out (the buffer already holds other sources' audio,
 *   so a source adds, never overwrites); returns %FALSE once the source is
 *   finished and will make no more sound
 * @reset: stops every sound and forgets all state, as if new
 *
 * The interface of a pull source.  @render is called from the thread that
 * renders the mixer that plays it, block by block, and must not block.
 */
struct _LrgAudioSourceInterface
{
	GTypeInterface parent_iface;

	/*< public >*/
	gboolean (*render) (LrgAudioSource *self,
	                    gfloat         *out,
	                    guint           frames,
	                    guint           channels,
	                    guint           sample_rate);
	void     (*reset)  (LrgAudioSource *self);

	/*< private >*/
	gpointer _reserved[4];
};

/**
 * lrg_audio_source_render:
 * @self: an #LrgAudioSource
 * @out: (array) (inout): @frames x @channels interleaved samples to add to
 * @frames: frames to render
 * @channels: samples per frame (1 or 2)
 * @sample_rate: the rate in Hz (8000 .. 192000)
 *
 * Adds the next @frames frames of the source into @out.
 *
 * Returns: %TRUE while the source can still make sound, %FALSE once it is
 *   finished (an #LrgAudioMixer then drops the voice)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_source_render (LrgAudioSource *self,
                                  gfloat         *out,
                                  guint           frames,
                                  guint           channels,
                                  guint           sample_rate);

/**
 * lrg_audio_source_reset:
 * @self: an #LrgAudioSource
 *
 * Silences the source and forgets its state.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_source_reset (LrgAudioSource *self);

G_END_DECLS
