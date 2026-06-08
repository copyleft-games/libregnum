/* lrg-reel-video-exporter.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelVideoExporter - MP4/WebM output via an ffmpeg subprocess.
 *
 * Streams rendered frames (as a PNG image sequence) to ffmpeg's stdin and,
 * optionally, muxes a pre-mixed audio track in.  ffmpeg is discovered at
 * runtime; if it is not installed, begin() fails gracefully with
 * %LRG_REEL_EXPORTER_ERROR_FFMPEG_NOT_FOUND.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-reel-exporter.h"

G_BEGIN_DECLS

#define LRG_TYPE_REEL_VIDEO_EXPORTER (lrg_reel_video_exporter_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelVideoExporter, lrg_reel_video_exporter, LRG, REEL_VIDEO_EXPORTER, LrgReelExporter)

/**
 * lrg_reel_video_exporter_new:
 * @path: (type filename): output file path (e.g. "out.mp4" or "out.webm").
 * @codec: the video codec / container to use.
 *
 * Creates a video exporter.  Nothing is spawned until
 * lrg_reel_exporter_begin() is called.
 *
 * Returns: (transfer full): a new #LrgReelVideoExporter
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelVideoExporter *
lrg_reel_video_exporter_new (const gchar       *path,
                             LrgReelVideoCodec  codec);

/**
 * lrg_reel_video_exporter_set_audio:
 * @self: a #LrgReelVideoExporter
 * @audio: (nullable) (transfer none): a mixed audio track, or %NULL for none.
 *
 * Sets the audio to mux into the output.  Typically the result of
 * lrg_reel_audio_track_mix().  Must be set before begin().
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_video_exporter_set_audio (LrgReelVideoExporter *self,
                                   LrgWaveData          *audio);

/**
 * lrg_reel_video_exporter_set_crf:
 * @self: a #LrgReelVideoExporter
 * @crf: constant-rate-factor quality (lower = higher quality; 0..51 for H.264).
 *
 * Sets the encoder quality.  The default is 23.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_video_exporter_set_crf (LrgReelVideoExporter *self,
                                 gint                  crf);

LRG_AVAILABLE_IN_ALL
void
lrg_reel_video_exporter_set_bitrate (LrgReelVideoExporter *self,
                                     gint                  kbps);

/**
 * lrg_reel_video_exporter_set_ffmpeg_path:
 * @self: a #LrgReelVideoExporter
 * @path: (type filename) (nullable): path to the ffmpeg binary, or %NULL to
 *   auto-discover it on the PATH.
 *
 * Overrides the ffmpeg executable used.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_video_exporter_set_ffmpeg_path (LrgReelVideoExporter *self,
                                         const gchar          *path);

/**
 * lrg_reel_video_exporter_is_ffmpeg_available:
 *
 * Checks whether an ffmpeg executable can be found on the PATH.
 *
 * Returns: %TRUE if ffmpeg is available
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_video_exporter_is_ffmpeg_available (void);

G_END_DECLS
