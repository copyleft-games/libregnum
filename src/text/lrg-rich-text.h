/* lrg-rich-text.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Rich text with BBCode markup support.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-text-span.h"
#include "lrg-text-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_RICH_TEXT (lrg_rich_text_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgRichText, lrg_rich_text, LRG, RICH_TEXT, GObject)

/**
 * LrgRichTextClass:
 * @parent_class: Parent class
 * @parse: Virtual method to parse markup text
 * @update: Update animation effects
 * @draw: Draw the rich text
 *
 * Class structure for #LrgRichText.
 */
struct _LrgRichTextClass
{
    GObjectClass parent_class;

    /*< public >*/
    void     (*parse)   (LrgRichText *text,
                         const gchar *markup);
    void     (*update)  (LrgRichText *text,
                         gfloat       delta_time);
    void     (*draw)    (LrgRichText *text,
                         gfloat       x,
                         gfloat       y);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_rich_text_new:
 *
 * Creates a new empty rich text object.
 *
 * Returns: (transfer full): A new #LrgRichText
 */
LRG_AVAILABLE_IN_ALL
LrgRichText *       lrg_rich_text_new                   (void);

/**
 * lrg_rich_text_new_from_markup:
 * @markup: BBCode markup text
 *
 * Creates a new rich text object from BBCode markup.
 *
 * Supported tags:
 * - [b]bold[/b]
 * - [i]italic[/i]
 * - [u]underline[/u]
 * - [s]strikethrough[/s]
 * - [color=#RRGGBB]colored[/color]
 * - [color=red]named color[/color]
 * - [size=1.5]larger text[/size]
 * - [shake]shaking text[/shake]
 * - [wave]wavy text[/wave]
 * - [rainbow]rainbow text[/rainbow]
 * - [typewriter speed=50]progressive reveal[/typewriter]
 *
 * Returns: (transfer full): A new #LrgRichText
 */
LRG_AVAILABLE_IN_ALL
LrgRichText *       lrg_rich_text_new_from_markup       (const gchar *markup);

/**
 * lrg_rich_text_set_markup:
 * @text: A #LrgRichText
 * @markup: BBCode markup text
 *
 * Sets the text content from BBCode markup.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rich_text_set_markup            (LrgRichText *text,
                                                         const gchar *markup);

/**
 * lrg_rich_text_get_plain_text:
 * @text: A #LrgRichText
 *
 * Gets the plain text content without markup.
 *
 * Returns: (transfer none): The plain text
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_rich_text_get_plain_text        (LrgRichText *text);

/**
 * lrg_rich_text_get_span_count:
 * @text: A #LrgRichText
 *
 * Gets the number of styled spans.
 *
 * Returns: The span count
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_rich_text_get_span_count        (LrgRichText *text);

/**
 * lrg_rich_text_get_span:
 * @text: A #LrgRichText
 * @index: Span index
 *
 * Gets a span by index.
 *
 * Returns: (transfer none) (nullable): The span
 */
LRG_AVAILABLE_IN_ALL
const LrgTextSpan * lrg_rich_text_get_span              (LrgRichText *text,
                                                         guint        index);

/**
 * lrg_rich_text_get_font_size:
 * @text: A #LrgRichText
 *
 * Gets the base font size.
 *
 * Returns: The font size in pixels
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_rich_text_get_font_size         (LrgRichText *text);

/**
 * lrg_rich_text_set_font_size:
 * @text: A #LrgRichText
 * @size: The font size in pixels
 *
 * Sets the base font size.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rich_text_set_font_size         (LrgRichText *text,
                                                         gfloat       size);

/**
 * lrg_rich_text_get_line_spacing:
 * @text: A #LrgRichText
 *
 * Gets the line spacing multiplier.
 *
 * Returns: The line spacing
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_rich_text_get_line_spacing      (LrgRichText *text);

/**
 * lrg_rich_text_set_line_spacing:
 * @text: A #LrgRichText
 * @spacing: The line spacing multiplier
 *
 * Sets the line spacing.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rich_text_set_line_spacing      (LrgRichText *text,
                                                         gfloat       spacing);

/**
 * lrg_rich_text_get_max_width:
 * @text: A #LrgRichText
 *
 * Gets the maximum width for word wrapping.
 *
 * Returns: The max width, or 0 for no wrapping
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_rich_text_get_max_width         (LrgRichText *text);

/**
 * lrg_rich_text_set_max_width:
 * @text: A #LrgRichText
 * @width: The max width, or 0 for no wrapping
 *
 * Sets the maximum width for word wrapping.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rich_text_set_max_width         (LrgRichText *text,
                                                         gfloat       width);

/**
 * lrg_rich_text_get_alignment:
 * @text: A #LrgRichText
 *
 * Gets the text alignment.
 *
 * Returns: The alignment
 */
LRG_AVAILABLE_IN_ALL
LrgTextAlignment    lrg_rich_text_get_alignment         (LrgRichText *text);

/**
 * lrg_rich_text_set_alignment:
 * @text: A #LrgRichText
 * @alignment: The alignment
 *
 * Sets the text alignment.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rich_text_set_alignment         (LrgRichText *text,
                                                         LrgTextAlignment alignment);

/**
 * lrg_rich_text_get_default_color:
 * @text: A #LrgRichText
 * @r: (out): Red component
 * @g: (out): Green component
 * @b: (out): Blue component
 * @a: (out): Alpha component
 *
 * Gets the default text color.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rich_text_get_default_color     (LrgRichText *text,
                                                         guint8      *r,
                                                         guint8      *g,
                                                         guint8      *b,
                                                         guint8      *a);

/**
 * lrg_rich_text_set_default_color:
 * @text: A #LrgRichText
 * @r: Red component
 * @g: Green component
 * @b: Blue component
 * @a: Alpha component
 *
 * Sets the default text color.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rich_text_set_default_color     (LrgRichText *text,
                                                         guint8       r,
                                                         guint8       g,
                                                         guint8       b,
                                                         guint8       a);

/**
 * lrg_rich_text_update:
 * @text: A #LrgRichText
 * @delta_time: Time since last update
 *
 * Updates animation effects.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rich_text_update                (LrgRichText *text,
                                                         gfloat       delta_time);

/**
 * lrg_rich_text_draw:
 * @text: A #LrgRichText
 * @x: X position
 * @y: Y position
 *
 * Draws the rich text.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rich_text_draw                  (LrgRichText *text,
                                                         gfloat       x,
                                                         gfloat       y);

/**
 * lrg_rich_text_measure:
 * @text: A #LrgRichText
 * @width: (out): Resulting width
 * @height: (out): Resulting height
 *
 * Measures the dimensions of the rendered text.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rich_text_measure               (LrgRichText *text,
                                                         gfloat      *width,
                                                         gfloat      *height);

/**
 * lrg_rich_text_reset_effects:
 * @text: A #LrgRichText
 *
 * Resets all animation effects.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rich_text_reset_effects         (LrgRichText *text);

/**
 * lrg_rich_text_effects_complete:
 * @text: A #LrgRichText
 *
 * Checks if all finite effects have completed.
 *
 * Returns: %TRUE if all effects are complete
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_rich_text_effects_complete      (LrgRichText *text);

G_END_DECLS
