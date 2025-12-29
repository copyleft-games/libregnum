/* lrg-text-span.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Styled text span for rich text rendering.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_TEXT_SPAN (lrg_text_span_get_type ())

/**
 * LrgTextSpan:
 *
 * A styled run of text within a rich text block.
 * Contains the text content along with styling information
 * including font size, color, style flags, and optional effects.
 */
typedef struct _LrgTextSpan LrgTextSpan;

LRG_AVAILABLE_IN_ALL
GType           lrg_text_span_get_type          (void) G_GNUC_CONST;

/**
 * lrg_text_span_new:
 * @text: The text content
 *
 * Creates a new text span with default styling.
 *
 * Returns: (transfer full): A new #LrgTextSpan
 */
LRG_AVAILABLE_IN_ALL
LrgTextSpan *   lrg_text_span_new               (const gchar    *text);

/**
 * lrg_text_span_copy:
 * @span: A #LrgTextSpan
 *
 * Creates a copy of a text span.
 *
 * Returns: (transfer full): A copy of @span
 */
LRG_AVAILABLE_IN_ALL
LrgTextSpan *   lrg_text_span_copy              (const LrgTextSpan *span);

/**
 * lrg_text_span_free:
 * @span: A #LrgTextSpan
 *
 * Frees a text span.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_text_span_free              (LrgTextSpan    *span);

/**
 * lrg_text_span_get_text:
 * @span: A #LrgTextSpan
 *
 * Gets the text content.
 *
 * Returns: (transfer none): The text content
 */
LRG_AVAILABLE_IN_ALL
const gchar *   lrg_text_span_get_text          (const LrgTextSpan *span);

/**
 * lrg_text_span_set_text:
 * @span: A #LrgTextSpan
 * @text: The new text content
 *
 * Sets the text content.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_text_span_set_text          (LrgTextSpan    *span,
                                                 const gchar    *text);

/**
 * lrg_text_span_get_style:
 * @span: A #LrgTextSpan
 *
 * Gets the text style flags.
 *
 * Returns: The style flags
 */
LRG_AVAILABLE_IN_ALL
LrgTextStyle    lrg_text_span_get_style         (const LrgTextSpan *span);

/**
 * lrg_text_span_set_style:
 * @span: A #LrgTextSpan
 * @style: The style flags
 *
 * Sets the text style flags.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_text_span_set_style         (LrgTextSpan    *span,
                                                 LrgTextStyle    style);

/**
 * lrg_text_span_get_font_size:
 * @span: A #LrgTextSpan
 *
 * Gets the font size multiplier (1.0 = default).
 *
 * Returns: The font size multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_text_span_get_font_size     (const LrgTextSpan *span);

/**
 * lrg_text_span_set_font_size:
 * @span: A #LrgTextSpan
 * @size: The font size multiplier
 *
 * Sets the font size multiplier.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_text_span_set_font_size     (LrgTextSpan    *span,
                                                 gfloat          size);

/**
 * lrg_text_span_get_color:
 * @span: A #LrgTextSpan
 * @r: (out): Red component (0-255)
 * @g: (out): Green component (0-255)
 * @b: (out): Blue component (0-255)
 * @a: (out): Alpha component (0-255)
 *
 * Gets the text color.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_text_span_get_color         (const LrgTextSpan *span,
                                                 guint8         *r,
                                                 guint8         *g,
                                                 guint8         *b,
                                                 guint8         *a);

/**
 * lrg_text_span_set_color:
 * @span: A #LrgTextSpan
 * @r: Red component (0-255)
 * @g: Green component (0-255)
 * @b: Blue component (0-255)
 * @a: Alpha component (0-255)
 *
 * Sets the text color.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_text_span_set_color         (LrgTextSpan    *span,
                                                 guint8          r,
                                                 guint8          g,
                                                 guint8          b,
                                                 guint8          a);

/**
 * lrg_text_span_set_color_hex:
 * @span: A #LrgTextSpan
 * @hex: Hex color string (e.g., "#FF0000" or "FF0000")
 *
 * Sets the text color from a hex string.
 *
 * Returns: %TRUE if the color was parsed successfully
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_text_span_set_color_hex     (LrgTextSpan    *span,
                                                 const gchar    *hex);

/**
 * lrg_text_span_get_effect_type:
 * @span: A #LrgTextSpan
 *
 * Gets the text effect type.
 *
 * Returns: The effect type
 */
LRG_AVAILABLE_IN_ALL
LrgTextEffectType lrg_text_span_get_effect_type (const LrgTextSpan *span);

/**
 * lrg_text_span_set_effect_type:
 * @span: A #LrgTextSpan
 * @effect: The effect type
 *
 * Sets the text effect type.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_text_span_set_effect_type   (LrgTextSpan      *span,
                                                 LrgTextEffectType effect);

/**
 * lrg_text_span_get_effect_speed:
 * @span: A #LrgTextSpan
 *
 * Gets the effect animation speed.
 *
 * Returns: The effect speed
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_text_span_get_effect_speed  (const LrgTextSpan *span);

/**
 * lrg_text_span_set_effect_speed:
 * @span: A #LrgTextSpan
 * @speed: The effect speed
 *
 * Sets the effect animation speed.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_text_span_set_effect_speed  (LrgTextSpan    *span,
                                                 gfloat          speed);

/**
 * lrg_text_span_get_custom_font:
 * @span: A #LrgTextSpan
 *
 * Gets the custom font name, if any.
 *
 * Returns: (transfer none) (nullable): The font name or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar *   lrg_text_span_get_custom_font   (const LrgTextSpan *span);

/**
 * lrg_text_span_set_custom_font:
 * @span: A #LrgTextSpan
 * @font_name: (nullable): The font name or %NULL for default
 *
 * Sets a custom font for this span.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_text_span_set_custom_font   (LrgTextSpan    *span,
                                                 const gchar    *font_name);

G_END_DECLS
