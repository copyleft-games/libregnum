/* lrg-reel-video-clip.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelVideoClip - places a decoded #LrgReelVideoSource on the timeline.
 *
 * Maps the clip-relative frame to a source time (honouring trim-start and
 * playback-rate, with optional looping) and draws the decoded frame fitted into
 * the composition frame.
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
#include "lrg-reel-clip.h"
#include "lrg-reel-video-source.h"

G_BEGIN_DECLS

#define LRG_TYPE_REEL_VIDEO_CLIP (lrg_reel_video_clip_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelVideoClip, lrg_reel_video_clip, LRG, REEL_VIDEO_CLIP, LrgReelClip)

/**
 * lrg_reel_video_clip_new_from_source:
 * @source: (transfer none): a #LrgReelVideoSource.
 *
 * Returns: (transfer full): a new #LrgReelVideoClip
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelVideoClip *
lrg_reel_video_clip_new_from_source (LrgReelVideoSource *source);

/**
 * lrg_reel_video_clip_new_from_file:
 * @path: (type filename): path to a video file.
 * @error: (nullable): return location for a #GError.
 *
 * Returns: (transfer full) (nullable): a new #LrgReelVideoClip, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelVideoClip *
lrg_reel_video_clip_new_from_file (const gchar  *path,
                                   GError      **error);

/**
 * lrg_reel_video_clip_get_source:
 * @self: a #LrgReelVideoClip
 *
 * Returns: (transfer none): the underlying #LrgReelVideoSource
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelVideoSource *
lrg_reel_video_clip_get_source (LrgReelVideoClip *self);

LRG_AVAILABLE_IN_ALL
LrgReelFit lrg_reel_video_clip_get_fit (LrgReelVideoClip *self);
LRG_AVAILABLE_IN_ALL
void       lrg_reel_video_clip_set_fit (LrgReelVideoClip *self, LrgReelFit fit);
LRG_AVAILABLE_IN_ALL
gdouble    lrg_reel_video_clip_get_trim_start (LrgReelVideoClip *self);
LRG_AVAILABLE_IN_ALL
void       lrg_reel_video_clip_set_trim_start (LrgReelVideoClip *self, gdouble seconds);
LRG_AVAILABLE_IN_ALL
gdouble    lrg_reel_video_clip_get_playback_rate (LrgReelVideoClip *self);
LRG_AVAILABLE_IN_ALL
void       lrg_reel_video_clip_set_playback_rate (LrgReelVideoClip *self, gdouble rate);
LRG_AVAILABLE_IN_ALL
gboolean   lrg_reel_video_clip_get_loop (LrgReelVideoClip *self);
LRG_AVAILABLE_IN_ALL
void       lrg_reel_video_clip_set_loop (LrgReelVideoClip *self, gboolean loop);

G_END_DECLS
