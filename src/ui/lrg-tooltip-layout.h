/* lrg-tooltip-layout.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTooltipLayout - sizes an LrgTooltip (wrapping flavour text, aligning
 * left/right pairs) with a caller supplied text measure, places it (and
 * a side-by-side comparison tooltip) clamped to the screen, and draws it
 * through three callbacks, so any renderer (graylib, a skin, a test that
 * records calls) can show it.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-tooltip.h"

G_BEGIN_DECLS

/**
 * LrgTooltipMeasureFunc:
 * @text: the text to measure (single line)
 * @width: (out): its width
 * @height: (out): its height
 * @user_data: (closure): the data given with the callback
 *
 * Measures one line of text in the caller's font.
 */
typedef void (*LrgTooltipMeasureFunc) (const gchar *text,
                                       gfloat      *width,
                                       gfloat      *height,
                                       gpointer     user_data);

/**
 * LrgTooltipStyle:
 * @padding: space between the border and the content
 * @line_spacing: space between two rows
 * @separator_height: height of a separator row
 * @spacer_height: height of a spacer row
 * @column_gap: least space between the two texts of a double line
 * @icon_size: edge of an icon (and the indent of an icon line's text)
 * @min_width: the narrowest tooltip
 * @max_width: the widest tooltip; longer text wraps
 * @border: border thickness
 * @background: the fill colour as 0xRRGGBBAA
 * @border_color: the border colour as 0xRRGGBBAA
 * @separator_color: the separator colour as 0xRRGGBBAA
 *
 * Sizes and colours of the tooltip frame.  A plain value type; fill with
 * lrg_tooltip_style_init_default() and change what is needed.
 */
struct _LrgTooltipStyle
{
    gfloat  padding;
    gfloat  line_spacing;
    gfloat  separator_height;
    gfloat  spacer_height;
    gfloat  column_gap;
    gfloat  icon_size;
    gfloat  min_width;
    gfloat  max_width;
    gfloat  border;
    guint32 background;
    guint32 border_color;
    guint32 separator_color;
};

#define LRG_TYPE_TOOLTIP_STYLE (lrg_tooltip_style_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_tooltip_style_get_type (void) G_GNUC_CONST;

/**
 * lrg_tooltip_style_copy:
 * @self: a #LrgTooltipStyle
 *
 * Returns: (transfer full): a copy
 */
LRG_AVAILABLE_IN_ALL
LrgTooltipStyle *lrg_tooltip_style_copy (const LrgTooltipStyle *self);

/**
 * lrg_tooltip_style_free:
 * @self: (nullable): a #LrgTooltipStyle
 *
 * Frees a style made by lrg_tooltip_style_copy().
 */
LRG_AVAILABLE_IN_ALL
void lrg_tooltip_style_free (LrgTooltipStyle *self);

/**
 * lrg_tooltip_style_init_default:
 * @style: (out caller-allocates): the style to fill
 *
 * Fills @style with a dark, thin-bordered Warcraft-like frame: padding 4,
 * line spacing 1, separator 5, spacer 4, column gap 12, icon 12, width
 * 60 to 220, border 1, near-black background.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tooltip_style_init_default (LrgTooltipStyle *style);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgTooltipStyle, lrg_tooltip_style_free)

/**
 * LrgTooltipRow:
 * @kind: the row's kind
 * @y: the row's top, from the tooltip's top edge
 * @height: the row's height
 * @left: (nullable): the left text (one wrapped piece); owned by the layout
 * @right: (nullable): the right text; owned by the layout
 * @left_color: the left colour as 0xRRGGBBAA
 * @right_color: the right colour as 0xRRGGBBAA
 * @left_x: where the left text starts, from the tooltip's left edge
 * @right_x: where the right text starts, from the tooltip's left edge
 * @icon: (nullable): the icon name of an icon row; owned by the layout
 * @line_index: the index of the #LrgTooltip line the row came from
 *
 * One drawn row of a layout.  A wrapped line yields several rows.
 */
typedef struct
{
    LrgTooltipLineKind kind;
    gfloat             y;
    gfloat             height;
    const gchar       *left;
    const gchar       *right;
    guint32            left_color;
    guint32            right_color;
    gfloat             left_x;
    gfloat             right_x;
    const gchar       *icon;
    guint              line_index;
} LrgTooltipRow;

/**
 * LrgTooltipDrawFuncs:
 * @fill_rect: (nullable): fills a rectangle with a 0xRRGGBBAA colour
 * @draw_text: (nullable): draws text with its top-left corner at x, y
 * @draw_icon: (nullable): draws a named icon into a square of @size pixels
 *
 * The three primitives a renderer supplies; a %NULL one skips that part.
 */
typedef struct
{
    void (*fill_rect) (gfloat x, gfloat y, gfloat width, gfloat height, guint32 color, gpointer user_data);
    void (*draw_text) (const gchar *text, gfloat x, gfloat y, guint32 color, gpointer user_data);
    void (*draw_icon) (const gchar *icon, gfloat x, gfloat y, gfloat size, gpointer user_data);
} LrgTooltipDrawFuncs;

#define LRG_TYPE_TOOLTIP_LAYOUT (lrg_tooltip_layout_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_tooltip_layout_get_type (void) G_GNUC_CONST;

/**
 * lrg_tooltip_layout_new:
 * @tooltip: the tooltip to size
 * @style: (nullable): the frame style, %NULL for the default
 * @measure: (scope call) (closure user_data): measures text
 * @user_data: data for @measure
 * @error: return location for a #GError
 *
 * Wraps and positions every row.  The layout is a snapshot: later edits
 * to @tooltip do not change it.  Fails with %G_IO_ERROR_INVALID_ARGUMENT
 * when @measure is %NULL, when @tooltip is empty, or when the style is
 * unusable (maximum width not above twice the padding, or a negative size).
 *
 * Returns: (transfer full) (nullable): the layout, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgTooltipLayout *lrg_tooltip_layout_new (LrgTooltip            *tooltip,
                                          const LrgTooltipStyle *style,
                                          LrgTooltipMeasureFunc  measure,
                                          gpointer               user_data,
                                          GError               **error);

/**
 * lrg_tooltip_layout_ref:
 * @self: a #LrgTooltipLayout
 *
 * Returns: (transfer full): @self with one more reference
 */
LRG_AVAILABLE_IN_ALL
LrgTooltipLayout *lrg_tooltip_layout_ref (LrgTooltipLayout *self);

/**
 * lrg_tooltip_layout_unref:
 * @self: (nullable): a #LrgTooltipLayout
 *
 * Drops a reference.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tooltip_layout_unref (LrgTooltipLayout *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgTooltipLayout, lrg_tooltip_layout_unref)

/**
 * lrg_tooltip_layout_get_width:
 * @self: a #LrgTooltipLayout
 *
 * Returns: the tooltip's width including padding and border
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tooltip_layout_get_width (const LrgTooltipLayout *self);

/**
 * lrg_tooltip_layout_get_height:
 * @self: a #LrgTooltipLayout
 *
 * Returns: the tooltip's height including padding and border
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tooltip_layout_get_height (const LrgTooltipLayout *self);

/**
 * lrg_tooltip_layout_get_n_rows:
 * @self: a #LrgTooltipLayout
 *
 * Returns: the number of drawn rows
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tooltip_layout_get_n_rows (const LrgTooltipLayout *self);

/**
 * lrg_tooltip_layout_get_row:
 * @self: a #LrgTooltipLayout
 * @index: a row index
 * @out_row: (out caller-allocates): the row
 *
 * Returns: %TRUE, or %FALSE when @index is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tooltip_layout_get_row (const LrgTooltipLayout *self, guint index, LrgTooltipRow *out_row);

/**
 * lrg_tooltip_layout_draw:
 * @self: a #LrgTooltipLayout
 * @x: the left edge
 * @y: the top edge
 * @funcs: the drawing primitives
 * @user_data: data for them
 *
 * Draws the border, the background, then every row.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tooltip_layout_draw (const LrgTooltipLayout    *self,
                              gfloat                     x,
                              gfloat                     y,
                              const LrgTooltipDrawFuncs *funcs,
                              gpointer                   user_data);

/**
 * lrg_tooltip_place:
 * @width: the tooltip's width
 * @height: the tooltip's height
 * @anchor_x: the pointer (or anchor) x
 * @anchor_y: the pointer (or anchor) y
 * @offset: gap kept between the anchor and the tooltip
 * @screen_width: the screen's width
 * @screen_height: the screen's height
 * @out_x: (out): the tooltip's left edge
 * @out_y: (out): the tooltip's top edge
 *
 * Puts the tooltip right of and below the anchor; flips left of it when
 * it would leave the screen on the right, above it at the bottom, and
 * clamps what is left so the tooltip stays on screen (the top left wins
 * when it is larger than the screen).
 */
LRG_AVAILABLE_IN_ALL
void lrg_tooltip_place (gfloat  width,
                        gfloat  height,
                        gfloat  anchor_x,
                        gfloat  anchor_y,
                        gfloat  offset,
                        gfloat  screen_width,
                        gfloat  screen_height,
                        gfloat *out_x,
                        gfloat *out_y);

/**
 * lrg_tooltip_place_pair:
 * @width: the main tooltip's width
 * @height: the main tooltip's height
 * @other_width: the comparison tooltip's width
 * @other_height: the comparison tooltip's height
 * @anchor_x: the pointer (or anchor) x
 * @anchor_y: the pointer (or anchor) y
 * @offset: gap kept between the anchor and the tooltips, and between them
 * @screen_width: the screen's width
 * @screen_height: the screen's height
 * @out_x: (out): the main tooltip's left edge
 * @out_y: (out): the main tooltip's top edge
 * @out_other_x: (out): the comparison tooltip's left edge
 * @out_other_y: (out): the comparison tooltip's top edge
 *
 * Places a main tooltip and a comparison tooltip side by side, main
 * first, both tops level: right of the anchor when both fit, else left
 * of it, else (the pair is wider than the screen) the comparison goes
 * under the main one.  Both stay on screen.
 *
 * Returns: %TRUE when they sit side by side, %FALSE when stacked
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tooltip_place_pair (gfloat  width,
                                 gfloat  height,
                                 gfloat  other_width,
                                 gfloat  other_height,
                                 gfloat  anchor_x,
                                 gfloat  anchor_y,
                                 gfloat  offset,
                                 gfloat  screen_width,
                                 gfloat  screen_height,
                                 gfloat *out_x,
                                 gfloat *out_y,
                                 gfloat *out_other_x,
                                 gfloat *out_other_y);

G_END_DECLS
