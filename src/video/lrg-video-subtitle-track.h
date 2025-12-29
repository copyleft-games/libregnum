/* lrg-video-subtitle-track.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LrgSubtitleCue:
 *
 * Represents a single subtitle cue with timing and text.
 */
typedef struct _LrgSubtitleCue LrgSubtitleCue;

#define LRG_TYPE_SUBTITLE_CUE (lrg_subtitle_cue_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_subtitle_cue_get_type (void) G_GNUC_CONST;

/**
 * lrg_subtitle_cue_new:
 * @start_time: start time in seconds
 * @end_time: end time in seconds
 * @text: subtitle text
 *
 * Creates a new subtitle cue.
 *
 * Returns: (transfer full): A new #LrgSubtitleCue
 */
LRG_AVAILABLE_IN_ALL
LrgSubtitleCue *lrg_subtitle_cue_new (gdouble      start_time,
                                      gdouble      end_time,
                                      const gchar *text);

/**
 * lrg_subtitle_cue_copy:
 * @cue: an #LrgSubtitleCue
 *
 * Creates a copy of the subtitle cue.
 *
 * Returns: (transfer full): A copy of the cue
 */
LRG_AVAILABLE_IN_ALL
LrgSubtitleCue *lrg_subtitle_cue_copy (const LrgSubtitleCue *cue);

/**
 * lrg_subtitle_cue_free:
 * @cue: an #LrgSubtitleCue
 *
 * Frees the subtitle cue.
 */
LRG_AVAILABLE_IN_ALL
void lrg_subtitle_cue_free (LrgSubtitleCue *cue);

/**
 * lrg_subtitle_cue_get_start_time:
 * @cue: an #LrgSubtitleCue
 *
 * Returns: The start time in seconds
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_subtitle_cue_get_start_time (const LrgSubtitleCue *cue);

/**
 * lrg_subtitle_cue_get_end_time:
 * @cue: an #LrgSubtitleCue
 *
 * Returns: The end time in seconds
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_subtitle_cue_get_end_time (const LrgSubtitleCue *cue);

/**
 * lrg_subtitle_cue_get_text:
 * @cue: an #LrgSubtitleCue
 *
 * Returns: (transfer none): The subtitle text
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_subtitle_cue_get_text (const LrgSubtitleCue *cue);

/**
 * lrg_subtitle_cue_contains_time:
 * @cue: an #LrgSubtitleCue
 * @time: time in seconds
 *
 * Checks if the cue is active at the given time.
 *
 * Returns: %TRUE if the time is within the cue's range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_subtitle_cue_contains_time (const LrgSubtitleCue *cue,
                                         gdouble               time);

#define LRG_TYPE_VIDEO_SUBTITLE_TRACK (lrg_video_subtitle_track_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVideoSubtitleTrack, lrg_video_subtitle_track,
                      LRG, VIDEO_SUBTITLE_TRACK, GObject)

/**
 * lrg_video_subtitle_track_new:
 *
 * Creates a new empty subtitle track.
 *
 * Returns: (transfer full): A new #LrgVideoSubtitleTrack
 */
LRG_AVAILABLE_IN_ALL
LrgVideoSubtitleTrack *lrg_video_subtitle_track_new (void);

/**
 * lrg_video_subtitle_track_load_srt:
 * @track: an #LrgVideoSubtitleTrack
 * @path: path to SRT file
 * @error: (nullable): return location for error
 *
 * Loads subtitles from an SRT file.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_video_subtitle_track_load_srt (LrgVideoSubtitleTrack  *track,
                                            const gchar            *path,
                                            GError                **error);

/**
 * lrg_video_subtitle_track_load_vtt:
 * @track: an #LrgVideoSubtitleTrack
 * @path: path to WebVTT file
 * @error: (nullable): return location for error
 *
 * Loads subtitles from a WebVTT file.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_video_subtitle_track_load_vtt (LrgVideoSubtitleTrack  *track,
                                            const gchar            *path,
                                            GError                **error);

/**
 * lrg_video_subtitle_track_load_data:
 * @track: an #LrgVideoSubtitleTrack
 * @data: subtitle data
 * @format: format string ("srt" or "vtt")
 * @error: (nullable): return location for error
 *
 * Loads subtitles from string data.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_video_subtitle_track_load_data (LrgVideoSubtitleTrack  *track,
                                             const gchar            *data,
                                             const gchar            *format,
                                             GError                **error);

/**
 * lrg_video_subtitle_track_add_cue:
 * @track: an #LrgVideoSubtitleTrack
 * @cue: the cue to add
 *
 * Adds a cue to the track. The track takes ownership of the cue.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_subtitle_track_add_cue (LrgVideoSubtitleTrack *track,
                                       LrgSubtitleCue        *cue);

/**
 * lrg_video_subtitle_track_clear:
 * @track: an #LrgVideoSubtitleTrack
 *
 * Removes all cues from the track.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_subtitle_track_clear (LrgVideoSubtitleTrack *track);

/**
 * lrg_video_subtitle_track_get_cue_count:
 * @track: an #LrgVideoSubtitleTrack
 *
 * Returns: The number of cues in the track
 */
LRG_AVAILABLE_IN_ALL
guint lrg_video_subtitle_track_get_cue_count (LrgVideoSubtitleTrack *track);

/**
 * lrg_video_subtitle_track_get_cue:
 * @track: an #LrgVideoSubtitleTrack
 * @index: cue index
 *
 * Gets a cue by index.
 *
 * Returns: (transfer none) (nullable): The cue, or %NULL if index is invalid
 */
LRG_AVAILABLE_IN_ALL
const LrgSubtitleCue *lrg_video_subtitle_track_get_cue (LrgVideoSubtitleTrack *track,
                                                        guint                  index);

/**
 * lrg_video_subtitle_track_get_text_at:
 * @track: an #LrgVideoSubtitleTrack
 * @time: time in seconds
 *
 * Gets the subtitle text at the given time.
 *
 * Returns: (transfer full) (nullable): The subtitle text, or %NULL
 */
LRG_AVAILABLE_IN_ALL
gchar *lrg_video_subtitle_track_get_text_at (LrgVideoSubtitleTrack *track,
                                             gdouble                time);

/**
 * lrg_video_subtitle_track_get_cues_at:
 * @track: an #LrgVideoSubtitleTrack
 * @time: time in seconds
 *
 * Gets all cues active at the given time.
 *
 * Returns: (transfer container) (element-type LrgSubtitleCue): Active cues
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_video_subtitle_track_get_cues_at (LrgVideoSubtitleTrack *track,
                                                 gdouble                time);

/**
 * lrg_video_subtitle_track_get_duration:
 * @track: an #LrgVideoSubtitleTrack
 *
 * Gets the total duration based on the last cue's end time.
 *
 * Returns: The duration in seconds
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_video_subtitle_track_get_duration (LrgVideoSubtitleTrack *track);

/**
 * lrg_video_subtitle_track_get_language:
 * @track: an #LrgVideoSubtitleTrack
 *
 * Gets the language code if set.
 *
 * Returns: (transfer none) (nullable): The language code
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_video_subtitle_track_get_language (LrgVideoSubtitleTrack *track);

/**
 * lrg_video_subtitle_track_set_language:
 * @track: an #LrgVideoSubtitleTrack
 * @language: (nullable): language code (e.g., "en", "es")
 *
 * Sets the language code for this track.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_subtitle_track_set_language (LrgVideoSubtitleTrack *track,
                                            const gchar           *language);

G_END_DECLS
