/* lrg-audio-mix-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The device-mix hub shared by LrgAudioEffectChain (lrg-audio-effect.c,
 * which owns raylib's mixed-audio processor) and LrgAudioBus
 * (lrg-audio-bus.c).  Not installed and not introspected.
 */

#pragma once

#include <glib.h>

#include "lrg-audio-bus.h"

G_BEGIN_DECLS

/* puts @bus on the device mix (FALSE without a device); @sample_rate as
 * lrg_audio_bus_attach_mix() */
gboolean
lrg_audio_mix_add_bus_private (LrgAudioBus *bus,
                               guint        sample_rate);

/* takes @bus off the device mix; once it returns the device thread no
 * longer mixes it */
void
lrg_audio_mix_remove_bus_private (LrgAudioBus *bus);

/* the rate the device mix runs at (0 when nothing is attached) */
guint
lrg_audio_mix_get_rate_private (gboolean *out_measured);

/* one device block of @bus into @out (called by the hub with its lock
 * held; takes the bus's lock) */
void
lrg_audio_bus_mix_device_private (LrgAudioBus *bus,
                                  gfloat      *out,
                                  guint        n_frames,
                                  guint        sample_rate);

G_END_DECLS
