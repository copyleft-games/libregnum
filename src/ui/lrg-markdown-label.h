/* lrg-markdown-label.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMarkdownLabel: Renders a Markdown subset (headings, emphasis, code, lists, quotes, links).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-widget.h"
#include "../text/lrg-rich-text.h"

G_BEGIN_DECLS

/**
 * LrgMarkdownBlockKind:
 * @LRG_MARKDOWN_BLOCK_PARAGRAPH: running text, wrapped to the width
 * @LRG_MARKDOWN_BLOCK_HEADING: "#" to "######" heading (level 1..6;
 *   levels below 3 render like 3)
 * @LRG_MARKDOWN_BLOCK_BULLET_ITEM: "-", "*" or "+" list item (level =
 *   nesting depth)
 * @LRG_MARKDOWN_BLOCK_NUMBERED_ITEM: "1." or "1)" list item (level =
 *   nesting depth)
 * @LRG_MARKDOWN_BLOCK_QUOTE: block quote (lines starting with ">")
 * @LRG_MARKDOWN_BLOCK_CODE_BLOCK: fenced "```" code block (not wrapped)
 * @LRG_MARKDOWN_BLOCK_RULE: horizontal rule ("---", "***" or "___")
 *
 * The block structure of parsed Markdown, see
 * lrg_markdown_label_get_block_kind().
 */
typedef enum
{
	LRG_MARKDOWN_BLOCK_PARAGRAPH,
	LRG_MARKDOWN_BLOCK_HEADING,
	LRG_MARKDOWN_BLOCK_BULLET_ITEM,
	LRG_MARKDOWN_BLOCK_NUMBERED_ITEM,
	LRG_MARKDOWN_BLOCK_QUOTE,
	LRG_MARKDOWN_BLOCK_CODE_BLOCK,
	LRG_MARKDOWN_BLOCK_RULE
} LrgMarkdownBlockKind;

#define LRG_TYPE_MARKDOWN_BLOCK_KIND (lrg_markdown_block_kind_get_type ())

/**
 * lrg_markdown_block_kind_get_type:
 *
 * Returns: the #GType of #LrgMarkdownBlockKind
 */
LRG_AVAILABLE_IN_ALL
GType lrg_markdown_block_kind_get_type (void) G_GNUC_CONST;

/**
 * LrgMarkdownSpanStyle:
 * @LRG_MARKDOWN_SPAN_NONE: plain text
 * @LRG_MARKDOWN_SPAN_BOLD: "**strong**" or "__strong__"
 * @LRG_MARKDOWN_SPAN_ITALIC: "*emphasis*" or "_emphasis_"
 * @LRG_MARKDOWN_SPAN_CODE: "`inline code`" (and code blocks)
 * @LRG_MARKDOWN_SPAN_LINK: the text of a "[text](url)" link
 *
 * Inline styles of a span of text; they combine.
 */
typedef enum /*< flags >*/
{
	LRG_MARKDOWN_SPAN_NONE   = 0,
	LRG_MARKDOWN_SPAN_BOLD   = 1 << 0,
	LRG_MARKDOWN_SPAN_ITALIC = 1 << 1,
	LRG_MARKDOWN_SPAN_CODE   = 1 << 2,
	LRG_MARKDOWN_SPAN_LINK   = 1 << 3
} LrgMarkdownSpanStyle;

#define LRG_TYPE_MARKDOWN_SPAN_STYLE (lrg_markdown_span_style_get_type ())

/**
 * lrg_markdown_span_style_get_type:
 *
 * Returns: the #GType of #LrgMarkdownSpanStyle
 */
LRG_AVAILABLE_IN_ALL
GType lrg_markdown_span_style_get_type (void) G_GNUC_CONST;

#define LRG_TYPE_MARKDOWN_LABEL (lrg_markdown_label_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMarkdownLabel, lrg_markdown_label, LRG, MARKDOWN_LABEL, LrgWidget)

/**
 * lrg_markdown_label_new:
 * @markdown: (nullable): initial Markdown source
 *
 * Creates a Markdown label.
 *
 * Returns: (transfer full): a new #LrgMarkdownLabel
 */
LRG_AVAILABLE_IN_ALL
LrgMarkdownLabel * lrg_markdown_label_new (const gchar *markdown);

/**
 * lrg_markdown_label_get_markdown:
 * @self: an #LrgMarkdownLabel
 *
 * Returns: (nullable): the Markdown source
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_markdown_label_get_markdown (LrgMarkdownLabel *self);

/**
 * lrg_markdown_label_set_markdown:
 * @self: an #LrgMarkdownLabel
 * @markdown: (nullable): Markdown source
 *
 * Replaces the text. It is parsed at once and laid out for the
 * widget's width.
 */
LRG_AVAILABLE_IN_ALL
void lrg_markdown_label_set_markdown (LrgMarkdownLabel *self,
                                      const gchar      *markdown);

/**
 * lrg_markdown_label_get_font_size:
 * @self: an #LrgMarkdownLabel
 *
 * Returns: the explicit body font size, 0 when it follows the theme
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_markdown_label_get_font_size (LrgMarkdownLabel *self);

/**
 * lrg_markdown_label_set_font_size:
 * @self: an #LrgMarkdownLabel
 * @size: body font size, 0 for the theme's FONT_SIZE
 *
 * Headings and code scale with it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_markdown_label_set_font_size (LrgMarkdownLabel *self,
                                       gfloat            size);

/**
 * lrg_markdown_label_get_text_color:
 * @self: an #LrgMarkdownLabel
 *
 * Returns: (nullable) (transfer none): the explicit body colour, or
 *   %NULL when the theme's TEXT is used
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_markdown_label_get_text_color (LrgMarkdownLabel *self);

/**
 * lrg_markdown_label_set_text_color:
 * @self: an #LrgMarkdownLabel
 * @color: (nullable): body and heading colour, %NULL for the theme's
 *
 * Overrides the LABEL and HEADER colours; beats theme style rules.
 */
LRG_AVAILABLE_IN_ALL
void lrg_markdown_label_set_text_color (LrgMarkdownLabel *self,
                                        const GrlColor   *color);

/**
 * lrg_markdown_label_get_n_blocks:
 * @self: an #LrgMarkdownLabel
 *
 * Returns: the number of parsed blocks
 */
LRG_AVAILABLE_IN_ALL
guint lrg_markdown_label_get_n_blocks (LrgMarkdownLabel *self);

/**
 * lrg_markdown_label_get_block_kind:
 * @self: an #LrgMarkdownLabel
 * @index: a block index
 *
 * Returns: the kind of block @index (%LRG_MARKDOWN_BLOCK_PARAGRAPH when
 *   out of range)
 */
LRG_AVAILABLE_IN_ALL
LrgMarkdownBlockKind lrg_markdown_label_get_block_kind (LrgMarkdownLabel *self,
                                                        guint             index);

/**
 * lrg_markdown_label_get_block_level:
 * @self: an #LrgMarkdownLabel
 * @index: a block index
 *
 * Returns: the heading level (1..6), the list nesting depth (0 at the
 *   left margin), or 0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_markdown_label_get_block_level (LrgMarkdownLabel *self,
                                         guint             index);

/**
 * lrg_markdown_label_get_block_text:
 * @self: an #LrgMarkdownLabel
 * @index: a block index
 *
 * Returns: (nullable): the block's text with the markup removed
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_markdown_label_get_block_text (LrgMarkdownLabel *self,
                                                 guint             index);

/**
 * lrg_markdown_label_get_block_n_spans:
 * @self: an #LrgMarkdownLabel
 * @index: a block index
 *
 * Returns: the number of styled spans in block @index
 */
LRG_AVAILABLE_IN_ALL
guint lrg_markdown_label_get_block_n_spans (LrgMarkdownLabel *self,
                                            guint             index);

/**
 * lrg_markdown_label_get_block_span:
 * @self: an #LrgMarkdownLabel
 * @index: a block index
 * @span: a span index inside the block
 * @style: (out) (optional): the span's inline styles
 *
 * Returns: (nullable): the span's text
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_markdown_label_get_block_span (LrgMarkdownLabel     *self,
                                                 guint                 index,
                                                 guint                 span,
                                                 LrgMarkdownSpanStyle *style);

/**
 * lrg_markdown_label_get_n_links:
 * @self: an #LrgMarkdownLabel
 *
 * Returns: the number of links in the text
 */
LRG_AVAILABLE_IN_ALL
guint lrg_markdown_label_get_n_links (LrgMarkdownLabel *self);

/**
 * lrg_markdown_label_get_link_url:
 * @self: an #LrgMarkdownLabel
 * @index: a link index, in reading order
 *
 * Returns: (nullable): the link target
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_markdown_label_get_link_url (LrgMarkdownLabel *self,
                                               guint             index);

/**
 * lrg_markdown_label_get_link_bounds:
 * @self: an #LrgMarkdownLabel
 * @index: a link index
 * @bounds: (out caller-allocates): the first laid-out piece of the link
 *   in canvas coordinates
 *
 * Returns: %FALSE if the link does not exist
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_markdown_label_get_link_bounds (LrgMarkdownLabel *self,
                                             guint             index,
                                             GrlRectangle     *bounds);

/**
 * lrg_markdown_label_get_n_lines:
 * @self: an #LrgMarkdownLabel
 *
 * The number of visual text lines after wrapping to the current width
 * (code block lines included, rules and gaps not).
 *
 * Returns: the line count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_markdown_label_get_n_lines (LrgMarkdownLabel *self);

/**
 * lrg_markdown_label_get_block_rich_text:
 * @self: an #LrgMarkdownLabel
 * @index: a block index
 *
 * The #LrgRichText the block was translated into: its spans carry the
 * #LrgTextStyle, size, colour and effect of every run of text. Do not
 * change it; it is rebuilt when the Markdown changes.
 *
 * Returns: (transfer none) (nullable): the block's rich text, or %NULL
 *   if @index is out of range
 */
LRG_AVAILABLE_IN_ALL
LrgRichText * lrg_markdown_label_get_block_rich_text (LrgMarkdownLabel *self,
                                                      guint             index);

/**
 * lrg_markdown_label_get_block_markup:
 * @self: an #LrgMarkdownLabel
 * @index: a block index
 *
 * The LrgRichText (BBCode) markup generated for the block. Literal "["
 * characters appear as U+E000 (LrgRichText has no escape for them).
 *
 * Returns: (nullable): the markup, or %NULL if @index is out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_markdown_label_get_block_markup (LrgMarkdownLabel *self,
                                                   guint             index);

/**
 * lrg_markdown_label_reset_effects:
 * @self: an #LrgMarkdownLabel
 *
 * Restarts every text effect from the beginning (a typewriter types
 * again, a fade fades in again), through lrg_rich_text_reset_effects().
 */
LRG_AVAILABLE_IN_ALL
void lrg_markdown_label_reset_effects (LrgMarkdownLabel *self);

/**
 * lrg_markdown_label_get_effects_complete:
 * @self: an #LrgMarkdownLabel
 *
 * Whether every one-shot effect (typewriter, fade) has finished, as
 * lrg_rich_text_effects_complete() reports for each block. Looping
 * effects (wave, shake, rainbow, pulse) never complete.
 *
 * Returns: %TRUE when there is nothing left to reveal
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_markdown_label_get_effects_complete (LrgMarkdownLabel *self);

G_END_DECLS
