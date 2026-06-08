/* lrg-reel-caption-clip.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelCaptionClip - a reel clip that renders subtitle captions.
 *
 * Reads timed text from an #LrgVideoSubtitleTrack and draws the active caption
 * at the bottom of the frame each render tick.  Text may be drawn left-,
 * centre-, or right-aligned, optionally behind a semi-transparent box, and
 * may be populated from an SRT or WebVTT file.
 *
 * For runtime transcription from audio via an external speech-recognition
 * tool, see lrg_reel_transcribe_audio().
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

G_BEGIN_DECLS

#define LRG_TYPE_REEL_CAPTION_CLIP (lrg_reel_caption_clip_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelCaptionClip, lrg_reel_caption_clip,
                      LRG, REEL_CAPTION_CLIP, LrgReelClip)

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_reel_caption_clip_new:
 *
 * Creates a new #LrgReelCaptionClip with an empty subtitle track.
 *
 * Returns: (transfer full): a new #LrgReelCaptionClip
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelCaptionClip *
lrg_reel_caption_clip_new (void);

/**
 * lrg_reel_caption_clip_new_from_srt:
 * @path: (type filename): path to an SRT subtitle file.
 * @error: (nullable): return location for a #GError, or %NULL.
 *
 * Creates a new #LrgReelCaptionClip pre-loaded with cues from @path.
 *
 * Returns: (transfer full) (nullable): a new #LrgReelCaptionClip, or %NULL on
 *   error.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelCaptionClip *
lrg_reel_caption_clip_new_from_srt (const gchar  *path,
                                     GError      **error);

/**
 * lrg_reel_caption_clip_new_from_vtt:
 * @path: (type filename): path to a WebVTT subtitle file.
 * @error: (nullable): return location for a #GError, or %NULL.
 *
 * Creates a new #LrgReelCaptionClip pre-loaded with cues from @path.
 *
 * Returns: (transfer full) (nullable): a new #LrgReelCaptionClip, or %NULL on
 *   error.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelCaptionClip *
lrg_reel_caption_clip_new_from_vtt (const gchar  *path,
                                     GError      **error);

/* ==========================================================================
 * Track
 * ========================================================================== */

/**
 * lrg_reel_caption_clip_set_track:
 * @self: an #LrgReelCaptionClip
 * @track: (transfer none) (nullable): the #LrgVideoSubtitleTrack to use, or
 *   %NULL to clear.
 *
 * Replaces the clip's subtitle track.  The clip takes a reference on @track.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_caption_clip_set_track (LrgReelCaptionClip    *self,
                                  LrgVideoSubtitleTrack *track);

/**
 * lrg_reel_caption_clip_get_track:
 * @self: an #LrgReelCaptionClip
 *
 * Returns: (transfer none) (nullable): the current #LrgVideoSubtitleTrack, or
 *   %NULL if none is set.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVideoSubtitleTrack *
lrg_reel_caption_clip_get_track (LrgReelCaptionClip *self);

/* ==========================================================================
 * Caption style
 * ========================================================================== */

/**
 * lrg_reel_caption_clip_set_font_size:
 * @self: an #LrgReelCaptionClip
 * @font_size: font size in pixels (> 0).
 *
 * Sets the bitmap font size used to render caption text.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_caption_clip_set_font_size (LrgReelCaptionClip *self,
                                      gint                font_size);

/**
 * lrg_reel_caption_clip_get_font_size:
 * @self: an #LrgReelCaptionClip
 *
 * Returns: the current font size in pixels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_caption_clip_get_font_size (LrgReelCaptionClip *self);

/**
 * lrg_reel_caption_clip_set_color:
 * @self: an #LrgReelCaptionClip
 * @color: the text color.
 *
 * Sets the foreground color used to draw caption text.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_caption_clip_set_color (LrgReelCaptionClip *self,
                                  const GrlColor     *color);

/**
 * lrg_reel_caption_clip_get_color:
 * @self: an #LrgReelCaptionClip
 * @out_color: (out caller-allocates): return location for the color.
 *
 * Copies the current text color into @out_color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_caption_clip_get_color (LrgReelCaptionClip *self,
                                  GrlColor           *out_color);

/**
 * lrg_reel_caption_clip_set_align:
 * @self: an #LrgReelCaptionClip
 * @align: the horizontal text alignment.
 *
 * Sets how caption text is aligned horizontally.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_caption_clip_set_align (LrgReelCaptionClip *self,
                                  LrgReelTextAlign    align);

/**
 * lrg_reel_caption_clip_get_align:
 * @self: an #LrgReelCaptionClip
 *
 * Returns: the current #LrgReelTextAlign value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelTextAlign
lrg_reel_caption_clip_get_align (LrgReelCaptionClip *self);

/**
 * lrg_reel_caption_clip_set_margin_bottom:
 * @self: an #LrgReelCaptionClip
 * @margin_bottom: distance in pixels from the bottom edge of the frame.
 *
 * Sets the vertical position of the caption by specifying how far above the
 * bottom of the frame the text baseline sits.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_caption_clip_set_margin_bottom (LrgReelCaptionClip *self,
                                          gint                margin_bottom);

/**
 * lrg_reel_caption_clip_get_margin_bottom:
 * @self: an #LrgReelCaptionClip
 *
 * Returns: the bottom margin in pixels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_caption_clip_get_margin_bottom (LrgReelCaptionClip *self);

/**
 * lrg_reel_caption_clip_set_box:
 * @self: an #LrgReelCaptionClip
 * @box: %TRUE to draw a background box behind the caption text.
 *
 * Enables or disables the semi-transparent background rectangle drawn behind
 * caption text to improve readability against complex backgrounds.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_caption_clip_set_box (LrgReelCaptionClip *self,
                                gboolean            box);

/**
 * lrg_reel_caption_clip_get_box:
 * @self: an #LrgReelCaptionClip
 *
 * Returns: %TRUE if the background box is enabled.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_caption_clip_get_box (LrgReelCaptionClip *self);

/**
 * lrg_reel_caption_clip_set_box_color:
 * @self: an #LrgReelCaptionClip
 * @color: the background box fill color (alpha controls opacity).
 *
 * Sets the color of the background rectangle drawn behind the caption text.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_caption_clip_set_box_color (LrgReelCaptionClip *self,
                                      const GrlColor     *color);

/**
 * lrg_reel_caption_clip_get_box_color:
 * @self: an #LrgReelCaptionClip
 * @out_color: (out caller-allocates): return location for the box color.
 *
 * Copies the current box color into @out_color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_caption_clip_get_box_color (LrgReelCaptionClip *self,
                                      GrlColor           *out_color);

/* ==========================================================================
 * Whisper transcription helpers (free functions)
 * ========================================================================== */

/**
 * lrg_reel_is_whisper_available:
 *
 * Tests whether a supported speech-recognition command-line tool is installed
 * and accessible on the current %PATH.  The following executables are probed
 * in order: @whisper-cli, @whisper-cpp, @main (whisper.cpp legacy name),
 * @whisper (upstream).
 *
 * Returns: %TRUE if at least one supported tool was found.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_is_whisper_available (void);

/**
 * lrg_reel_transcribe_audio:
 * @audio: an #LrgWaveData containing the audio to transcribe.
 * @language: (nullable): BCP-47 language tag (e.g. "en", "es"), or %NULL to
 *   use automatic language detection.
 * @error: (nullable): return location for a #GError, or %NULL.
 *
 * Transcribes @audio to subtitle cues using a locally installed
 * speech-recognition tool.  The function exports @audio to a temporary WAV
 * file, invokes the tool, parses the resulting SRT output, and returns a
 * populated #LrgVideoSubtitleTrack.
 *
 * The temporary files are removed before this function returns.
 *
 * Assumptions: the speech-recognition tool is the whisper.cpp command-line
 * interface (whisper-cli / whisper-cpp / main).  The upstream Python
 * distribution (@whisper) is also supported with a different argument set.
 * The function selects argument order based on which executable was found;
 * if the installed binary uses a non-standard interface the transcription may
 * fail and @error will be set.
 *
 * Returns: (transfer full) (nullable): a new #LrgVideoSubtitleTrack, or %NULL
 *   on error.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVideoSubtitleTrack *
lrg_reel_transcribe_audio (LrgWaveData  *audio,
                            const gchar  *language,
                            GError      **error);

G_END_DECLS
