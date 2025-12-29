/* lrg-video-subtitles.h
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
#include "lrg-video-subtitle-track.h"
#include "../lrg-enums.h"
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_VIDEO_SUBTITLES (lrg_video_subtitles_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVideoSubtitles, lrg_video_subtitles, LRG, VIDEO_SUBTITLES, GObject)

/**
 * lrg_video_subtitles_new:
 *
 * Creates a new subtitle renderer.
 *
 * Returns: (transfer full): A new #LrgVideoSubtitles
 */
LRG_AVAILABLE_IN_ALL
LrgVideoSubtitles *lrg_video_subtitles_new (void);

/**
 * lrg_video_subtitles_set_track:
 * @subtitles: an #LrgVideoSubtitles
 * @track: (nullable): the subtitle track
 *
 * Sets the subtitle track to render.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_subtitles_set_track (LrgVideoSubtitles     *subtitles,
                                    LrgVideoSubtitleTrack *track);

/**
 * lrg_video_subtitles_get_track:
 * @subtitles: an #LrgVideoSubtitles
 *
 * Gets the current subtitle track.
 *
 * Returns: (transfer none) (nullable): The current track
 */
LRG_AVAILABLE_IN_ALL
LrgVideoSubtitleTrack *lrg_video_subtitles_get_track (LrgVideoSubtitles *subtitles);

/**
 * lrg_video_subtitles_set_visible:
 * @subtitles: an #LrgVideoSubtitles
 * @visible: whether subtitles should be visible
 *
 * Sets subtitle visibility.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_subtitles_set_visible (LrgVideoSubtitles *subtitles,
                                      gboolean           visible);

/**
 * lrg_video_subtitles_get_visible:
 * @subtitles: an #LrgVideoSubtitles
 *
 * Gets subtitle visibility.
 *
 * Returns: %TRUE if subtitles are visible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_video_subtitles_get_visible (LrgVideoSubtitles *subtitles);

/**
 * lrg_video_subtitles_set_position:
 * @subtitles: an #LrgVideoSubtitles
 * @position: subtitle position
 *
 * Sets where subtitles are displayed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_subtitles_set_position (LrgVideoSubtitles   *subtitles,
                                       LrgSubtitlePosition  position);

/**
 * lrg_video_subtitles_get_position:
 * @subtitles: an #LrgVideoSubtitles
 *
 * Gets the subtitle position.
 *
 * Returns: The subtitle position
 */
LRG_AVAILABLE_IN_ALL
LrgSubtitlePosition lrg_video_subtitles_get_position (LrgVideoSubtitles *subtitles);

/**
 * lrg_video_subtitles_set_font_size:
 * @subtitles: an #LrgVideoSubtitles
 * @size: font size in pixels
 *
 * Sets the subtitle font size.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_subtitles_set_font_size (LrgVideoSubtitles *subtitles,
                                        gfloat             size);

/**
 * lrg_video_subtitles_get_font_size:
 * @subtitles: an #LrgVideoSubtitles
 *
 * Gets the subtitle font size.
 *
 * Returns: The font size in pixels
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_video_subtitles_get_font_size (LrgVideoSubtitles *subtitles);

/**
 * lrg_video_subtitles_set_color:
 * @subtitles: an #LrgVideoSubtitles
 * @r: red component (0-255)
 * @g: green component (0-255)
 * @b: blue component (0-255)
 * @a: alpha component (0-255)
 *
 * Sets the subtitle text color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_subtitles_set_color (LrgVideoSubtitles *subtitles,
                                    guint8             r,
                                    guint8             g,
                                    guint8             b,
                                    guint8             a);

/**
 * lrg_video_subtitles_get_color:
 * @subtitles: an #LrgVideoSubtitles
 * @r: (out) (optional): red component
 * @g: (out) (optional): green component
 * @b: (out) (optional): blue component
 * @a: (out) (optional): alpha component
 *
 * Gets the subtitle text color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_subtitles_get_color (LrgVideoSubtitles *subtitles,
                                    guint8            *r,
                                    guint8            *g,
                                    guint8            *b,
                                    guint8            *a);

/**
 * lrg_video_subtitles_set_background:
 * @subtitles: an #LrgVideoSubtitles
 * @enabled: whether to show background
 *
 * Enables or disables the subtitle background box.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_subtitles_set_background (LrgVideoSubtitles *subtitles,
                                         gboolean           enabled);

/**
 * lrg_video_subtitles_get_background:
 * @subtitles: an #LrgVideoSubtitles
 *
 * Gets whether background is enabled.
 *
 * Returns: %TRUE if background is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_video_subtitles_get_background (LrgVideoSubtitles *subtitles);

/**
 * lrg_video_subtitles_set_margin:
 * @subtitles: an #LrgVideoSubtitles
 * @margin: margin from edge in pixels
 *
 * Sets the margin from the screen edge.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_subtitles_set_margin (LrgVideoSubtitles *subtitles,
                                     gfloat             margin);

/**
 * lrg_video_subtitles_get_margin:
 * @subtitles: an #LrgVideoSubtitles
 *
 * Gets the edge margin.
 *
 * Returns: The margin in pixels
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_video_subtitles_get_margin (LrgVideoSubtitles *subtitles);

/**
 * lrg_video_subtitles_update:
 * @subtitles: an #LrgVideoSubtitles
 * @time: current playback time in seconds
 *
 * Updates the current subtitle based on playback time.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_subtitles_update (LrgVideoSubtitles *subtitles,
                                 gdouble            time);

/**
 * lrg_video_subtitles_get_current_text:
 * @subtitles: an #LrgVideoSubtitles
 *
 * Gets the current subtitle text.
 *
 * Returns: (transfer none) (nullable): The current text
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_video_subtitles_get_current_text (LrgVideoSubtitles *subtitles);

/**
 * lrg_video_subtitles_draw:
 * @subtitles: an #LrgVideoSubtitles
 * @screen_width: screen width in pixels
 * @screen_height: screen height in pixels
 *
 * Draws the current subtitle if visible.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_subtitles_draw (LrgVideoSubtitles *subtitles,
                               gint               screen_width,
                               gint               screen_height);

G_END_DECLS
