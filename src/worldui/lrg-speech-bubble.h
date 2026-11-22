/* lrg-speech-bubble.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSpeechBubble: a rounded box of word-wrapped text with a tail
 * pointing at its anchor, fading in when something is said and out
 * after it has been read.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-world-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_SPEECH_BUBBLE (lrg_speech_bubble_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSpeechBubble, lrg_speech_bubble, LRG, SPEECH_BUBBLE, LrgWorldWidget)

/**
 * lrg_speech_bubble_new:
 * @text: (nullable): the text
 *
 * Creates a speech bubble: 220 px wide at most, 16 px text, 8 px
 * padding, a 10 px tail, dark text on an off-white box.
 *
 * Returns: (transfer full): a new #LrgSpeechBubble
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgSpeechBubble * lrg_speech_bubble_new (const gchar *text);

/**
 * lrg_speech_bubble_set_text:
 * @self: an #LrgSpeechBubble
 * @text: (nullable): the text; "\n" starts a new paragraph
 *
 * Replaces the text.  It is wrapped at word boundaries to the maximum
 * width; a word longer than a line is broken between characters.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_speech_bubble_set_text (LrgSpeechBubble *self,
                                 const gchar     *text);

/**
 * lrg_speech_bubble_get_text:
 * @self: an #LrgSpeechBubble
 *
 * Returns: (transfer none): the text
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_speech_bubble_get_text (LrgSpeechBubble *self);

/**
 * lrg_speech_bubble_say:
 * @self: an #LrgSpeechBubble
 * @text: the text
 * @hold_seconds: how long the bubble stays before fading out; 0 keeps it
 *
 * Sets the text, fades the bubble in (0.15 s) and arranges for it to
 * fade out (0.4 s) after @hold_seconds.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_speech_bubble_say (LrgSpeechBubble *self,
                            const gchar     *text,
                            gfloat           hold_seconds);

/**
 * lrg_speech_bubble_set_max_width:
 * @self: an #LrgSpeechBubble
 * @width: the widest the text may be, in pixels at scale 1 (> 0)
 *
 * Sets the wrap width.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_speech_bubble_set_max_width (LrgSpeechBubble *self,
                                      gfloat           width);

/**
 * lrg_speech_bubble_get_max_width:
 * @self: an #LrgSpeechBubble
 *
 * Returns: the wrap width in pixels at scale 1
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_speech_bubble_get_max_width (LrgSpeechBubble *self);

/**
 * lrg_speech_bubble_set_font_size:
 * @self: an #LrgSpeechBubble
 * @size: font size in pixels at scale 1 (> 0)
 *
 * Sets the text size.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_speech_bubble_set_font_size (LrgSpeechBubble *self,
                                      gfloat           size);

/**
 * lrg_speech_bubble_set_style:
 * @self: an #LrgSpeechBubble
 * @background: (nullable): box colour (%NULL keeps it)
 * @foreground: (nullable): text colour (%NULL keeps it)
 * @border: (nullable): outline colour (%NULL keeps it)
 * @padding: space between the text and the box edge, in pixels
 * @tail_size: height of the tail in pixels (0: no tail)
 *
 * Sets the bubble's look.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_speech_bubble_set_style (LrgSpeechBubble *self,
                                  const GrlColor  *background,
                                  const GrlColor  *foreground,
                                  const GrlColor  *border,
                                  gfloat           padding,
                                  gfloat           tail_size);

/**
 * lrg_speech_bubble_get_n_lines:
 * @self: an #LrgSpeechBubble
 *
 * Returns: the number of lines the text wraps to
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_speech_bubble_get_n_lines (LrgSpeechBubble *self);

/**
 * lrg_speech_bubble_get_line:
 * @self: an #LrgSpeechBubble
 * @index: a line index
 *
 * Returns: (transfer none) (nullable): the line's text
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_speech_bubble_get_line (LrgSpeechBubble *self,
                                          guint            index);

G_END_DECLS
