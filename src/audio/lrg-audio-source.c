/* lrg-audio-source.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The pull-source interface (see lrg-audio-source.h).
 */

#include "config.h"

#include "lrg-audio-source.h"

G_DEFINE_INTERFACE (LrgAudioSource, lrg_audio_source, G_TYPE_OBJECT)

static void
lrg_audio_source_default_init (LrgAudioSourceInterface *iface)
{
	(void) iface;
}

gboolean
lrg_audio_source_render (LrgAudioSource *self,
                         gfloat         *out,
                         guint           frames,
                         guint           channels,
                         guint           sample_rate)
{
	LrgAudioSourceInterface *iface;

	g_return_val_if_fail (LRG_IS_AUDIO_SOURCE (self), FALSE);
	g_return_val_if_fail (out != NULL || frames == 0, FALSE);

	/* a layout no source supports ends it rather than corrupting @out */
	if (channels == 0 || channels > 2 || sample_rate < 8000 || sample_rate > 192000)
		return FALSE;
	if (frames == 0)
		return TRUE;
	iface = LRG_AUDIO_SOURCE_GET_IFACE (self);
	if (iface->render == NULL)
		return FALSE;
	return iface->render (self, out, frames, channels, sample_rate);
}

void
lrg_audio_source_reset (LrgAudioSource *self)
{
	LrgAudioSourceInterface *iface;

	g_return_if_fail (LRG_IS_AUDIO_SOURCE (self));

	iface = LRG_AUDIO_SOURCE_GET_IFACE (self);
	if (iface->reset != NULL)
		iface->reset (self);
}
