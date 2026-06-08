/* lrg-reel-audio-exporter.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelAudioExporter - writes a mixed reel audio track to an audio-only file
 * (WAV directly; MP3/AAC/FLAC via an ffmpeg transcode).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_REEL_AUDIO_EXPORTER (lrg_reel_audio_exporter_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelAudioExporter, lrg_reel_audio_exporter, LRG, REEL_AUDIO_EXPORTER, GObject)

/**
 * lrg_reel_audio_exporter_new:
 * @path: (type filename): the output audio path.
 * @format: the #LrgReelAudioFormat.
 *
 * Returns: (transfer full): a new #LrgReelAudioExporter
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelAudioExporter *
lrg_reel_audio_exporter_new (const gchar        *path,
                             LrgReelAudioFormat  format);

/**
 * lrg_reel_audio_exporter_set_bitrate:
 * @self: an #LrgReelAudioExporter
 * @kbps: target bitrate in kbit/s for lossy formats (default 192).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_audio_exporter_set_bitrate (LrgReelAudioExporter *self,
                                     gint                  kbps);

/**
 * lrg_reel_audio_exporter_export:
 * @self: an #LrgReelAudioExporter
 * @wave: the mixed audio to write.
 * @error: (nullable): return location for a #GError.
 *
 * Writes @wave to the exporter's path in the configured format.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_audio_exporter_export (LrgReelAudioExporter *self,
                                LrgWaveData          *wave,
                                GError              **error);

G_END_DECLS
