/* lrg-reel-video-source.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelVideoSource - decodes an existing video file into frames.
 *
 * Uses ffprobe for metadata and an ffmpeg subprocess (decoding to a PNG image
 * stream) for frames.  Decoded frames are demuxed and indexed; a frame is
 * rasterized to a #GrlImage on demand and the most recent one is cached, so
 * sequential access (the reel renderer's usual pattern) re-decodes nothing.
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

#define LRG_TYPE_REEL_VIDEO_SOURCE (lrg_reel_video_source_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelVideoSource, lrg_reel_video_source, LRG, REEL_VIDEO_SOURCE, GObject)

/**
 * lrg_reel_video_source_new_from_file:
 * @path: (type filename): path to a video file (mp4, webm, mov, ...).
 * @error: (nullable): return location for a #GError.
 *
 * Probes @path for metadata.  Frames are decoded lazily on first access.
 * Requires ffprobe/ffmpeg on the PATH.
 *
 * Returns: (transfer full) (nullable): a new #LrgReelVideoSource, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelVideoSource *
lrg_reel_video_source_new_from_file (const gchar  *path,
                                     GError      **error);

LRG_AVAILABLE_IN_ALL
gint     lrg_reel_video_source_get_width        (LrgReelVideoSource *self);
LRG_AVAILABLE_IN_ALL
gint     lrg_reel_video_source_get_height       (LrgReelVideoSource *self);
LRG_AVAILABLE_IN_ALL
gdouble  lrg_reel_video_source_get_fps          (LrgReelVideoSource *self);
LRG_AVAILABLE_IN_ALL
gdouble  lrg_reel_video_source_get_duration     (LrgReelVideoSource *self);
LRG_AVAILABLE_IN_ALL
gboolean lrg_reel_video_source_get_has_audio    (LrgReelVideoSource *self);

/**
 * lrg_reel_video_source_get_frame_count:
 * @self: a #LrgReelVideoSource
 *
 * Returns the number of decoded frames.  This forces a full decode on the
 * first call.
 *
 * Returns: the frame count, or 0 on decode failure
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_video_source_get_frame_count (LrgReelVideoSource *self);

/**
 * lrg_reel_video_source_get_frame:
 * @self: a #LrgReelVideoSource
 * @index: zero-based frame index (clamped to the valid range).
 * @error: (nullable): return location for a #GError.
 *
 * Returns the decoded frame at @index.  The returned image is owned by the
 * source and remains valid only until the next call.
 *
 * Returns: (transfer none) (nullable): the frame image, or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlImage *
lrg_reel_video_source_get_frame (LrgReelVideoSource *self,
                                 gint                index,
                                 GError            **error);

/**
 * lrg_reel_video_source_extract_audio:
 * @self: a #LrgReelVideoSource
 * @error: (nullable): return location for a #GError.
 *
 * Extracts the source's audio track as a new #LrgWaveData (via ffmpeg), or
 * %NULL if the source has no audio.
 *
 * Returns: (transfer full) (nullable): the audio, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgWaveData *
lrg_reel_video_source_extract_audio (LrgReelVideoSource *self,
                                     GError            **error);

/**
 * lrg_reel_video_source_is_ffmpeg_available:
 *
 * Returns: %TRUE if both ffmpeg and ffprobe are on the PATH
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_video_source_is_ffmpeg_available (void);

G_END_DECLS
