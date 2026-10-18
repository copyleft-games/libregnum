/* lrg-tooltip.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTooltip - a rich tooltip model in the style of World of Warcraft's
 * GameTooltip: an ordered list of lines (single text, left/right pairs,
 * icon lines, separators and spacers), each with colours, an optional
 * stable id so a line can be found, replaced or removed later, and a
 * wrap flag for flavour text.  The model draws nothing: see
 * LrgTooltipLayout for sizing, placement and drawing through callbacks.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * LrgTooltipLineKind:
 * @LRG_TOOLTIP_LINE_TEXT: one left-aligned text
 * @LRG_TOOLTIP_LINE_DOUBLE: a left-aligned and a right-aligned text on one row
 * @LRG_TOOLTIP_LINE_ICON: an icon followed by left-aligned text
 * @LRG_TOOLTIP_LINE_SEPARATOR: a thin horizontal rule
 * @LRG_TOOLTIP_LINE_SPACER: an empty gap of a few pixels
 *
 * What a #LrgTooltipLine shows.
 */
typedef enum
{
    LRG_TOOLTIP_LINE_TEXT,
    LRG_TOOLTIP_LINE_DOUBLE,
    LRG_TOOLTIP_LINE_ICON,
    LRG_TOOLTIP_LINE_SEPARATOR,
    LRG_TOOLTIP_LINE_SPACER
} LrgTooltipLineKind;

#define LRG_TYPE_TOOLTIP_LINE_KIND (lrg_tooltip_line_kind_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_tooltip_line_kind_get_type (void) G_GNUC_CONST;

/* --- LrgTooltipLine: one row of a tooltip (boxed) --- */

#define LRG_TYPE_TOOLTIP_LINE (lrg_tooltip_line_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_tooltip_line_get_type (void) G_GNUC_CONST;

/**
 * lrg_tooltip_line_new:
 * @kind: what the line shows
 *
 * Creates a line: no text, white, no id, not wrapped.
 *
 * Returns: (transfer full): a new #LrgTooltipLine
 */
LRG_AVAILABLE_IN_ALL
LrgTooltipLine *lrg_tooltip_line_new (LrgTooltipLineKind kind);

/**
 * lrg_tooltip_line_copy:
 * @self: a #LrgTooltipLine
 *
 * Returns: (transfer full): a deep copy
 */
LRG_AVAILABLE_IN_ALL
LrgTooltipLine *lrg_tooltip_line_copy (const LrgTooltipLine *self);

/**
 * lrg_tooltip_line_free:
 * @self: (nullable): a #LrgTooltipLine
 *
 * Frees the line.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tooltip_line_free (LrgTooltipLine *self);

/**
 * lrg_tooltip_line_get_kind:
 * @self: a #LrgTooltipLine
 *
 * Returns: the line's kind
 */
LRG_AVAILABLE_IN_ALL
LrgTooltipLineKind lrg_tooltip_line_get_kind (const LrgTooltipLine *self);

/**
 * lrg_tooltip_line_get_left:
 * @self: a #LrgTooltipLine
 *
 * Returns: (transfer none) (nullable): the left (or only) text
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_tooltip_line_get_left (const LrgTooltipLine *self);

/**
 * lrg_tooltip_line_set_left:
 * @self: a #LrgTooltipLine
 * @text: (nullable): the left (or only) text
 *
 * Sets the left text.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tooltip_line_set_left (LrgTooltipLine *self, const gchar *text);

/**
 * lrg_tooltip_line_get_right:
 * @self: a #LrgTooltipLine
 *
 * Returns: (transfer none) (nullable): the right-aligned text of a double line
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_tooltip_line_get_right (const LrgTooltipLine *self);

/**
 * lrg_tooltip_line_set_right:
 * @self: a #LrgTooltipLine
 * @text: (nullable): the right-aligned text
 *
 * Sets the right text.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tooltip_line_set_right (LrgTooltipLine *self, const gchar *text);

/**
 * lrg_tooltip_line_get_left_color:
 * @self: a #LrgTooltipLine
 *
 * Returns: the left text colour as 0xRRGGBBAA
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_tooltip_line_get_left_color (const LrgTooltipLine *self);

/**
 * lrg_tooltip_line_get_right_color:
 * @self: a #LrgTooltipLine
 *
 * Returns: the right text colour as 0xRRGGBBAA
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_tooltip_line_get_right_color (const LrgTooltipLine *self);

/**
 * lrg_tooltip_line_set_colors:
 * @self: a #LrgTooltipLine
 * @left: the left text colour as 0xRRGGBBAA
 * @right: the right text colour as 0xRRGGBBAA
 *
 * Sets both text colours.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tooltip_line_set_colors (LrgTooltipLine *self, guint32 left, guint32 right);

/**
 * lrg_tooltip_line_get_wrap:
 * @self: a #LrgTooltipLine
 *
 * Returns: whether the left text wraps at the tooltip's width without
 *   widening it (flavour text)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tooltip_line_get_wrap (const LrgTooltipLine *self);

/**
 * lrg_tooltip_line_set_wrap:
 * @self: a #LrgTooltipLine
 * @wrap: whether the left text wraps without widening the tooltip
 *
 * Sets the wrap flag.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tooltip_line_set_wrap (LrgTooltipLine *self, gboolean wrap);

/**
 * lrg_tooltip_line_get_id:
 * @self: a #LrgTooltipLine
 *
 * Returns: (transfer none) (nullable): the line's id, "" or %NULL for none
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_tooltip_line_get_id (const LrgTooltipLine *self);

/**
 * lrg_tooltip_line_set_id:
 * @self: a #LrgTooltipLine
 * @id: (nullable): an id other code can find the line by
 *
 * Sets the line's id.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tooltip_line_set_id (LrgTooltipLine *self, const gchar *id);

/**
 * lrg_tooltip_line_get_icon:
 * @self: a #LrgTooltipLine
 *
 * Returns: (transfer none) (nullable): the icon name of an icon line
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_tooltip_line_get_icon (const LrgTooltipLine *self);

/**
 * lrg_tooltip_line_set_icon:
 * @self: a #LrgTooltipLine
 * @icon: (nullable): an icon name the draw callback understands
 *
 * Sets the icon name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tooltip_line_set_icon (LrgTooltipLine *self, const gchar *icon);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgTooltipLine, lrg_tooltip_line_free)

/* --- colours --- */

/**
 * lrg_tooltip_color_parse:
 * @spec: "#RRGGBB", "#RRGGBBAA" (hex digits, either case) or "RRGGBB"
 * @out_color: (out): the colour as 0xRRGGBBAA, alpha 0xFF when omitted
 *
 * Parses a colour; on failure @out_color is untouched.
 *
 * Returns: %TRUE when @spec is a valid colour
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tooltip_color_parse (const gchar *spec, guint32 *out_color);

/* --- LrgTooltip --- */

#define LRG_TYPE_TOOLTIP (lrg_tooltip_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTooltip, lrg_tooltip, LRG, TOOLTIP, GObject)

/**
 * lrg_tooltip_new:
 *
 * Creates an empty tooltip.
 *
 * Returns: (transfer full): a new #LrgTooltip
 */
LRG_AVAILABLE_IN_ALL
LrgTooltip *lrg_tooltip_new (void);

/**
 * lrg_tooltip_copy:
 * @self: a #LrgTooltip
 *
 * Returns: (transfer full): a tooltip with a deep copy of every line
 */
LRG_AVAILABLE_IN_ALL
LrgTooltip *lrg_tooltip_copy (LrgTooltip *self);

/**
 * lrg_tooltip_get_n_lines:
 * @self: a #LrgTooltip
 *
 * Returns: the number of lines
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tooltip_get_n_lines (LrgTooltip *self);

/**
 * lrg_tooltip_get_line:
 * @self: a #LrgTooltip
 * @index: a line index
 *
 * Returns: (transfer none) (nullable): the line, %NULL out of range; owned
 *   by the tooltip, edits to it show at once
 */
LRG_AVAILABLE_IN_ALL
LrgTooltipLine *lrg_tooltip_get_line (LrgTooltip *self, guint index);

/**
 * lrg_tooltip_add_line:
 * @self: a #LrgTooltip
 * @text: the text
 * @color: its colour as 0xRRGGBBAA
 *
 * Appends a single-text line (GameTooltip:AddLine).
 *
 * Returns: (transfer none): the new line, owned by the tooltip
 */
LRG_AVAILABLE_IN_ALL
LrgTooltipLine *lrg_tooltip_add_line (LrgTooltip *self, const gchar *text, guint32 color);

/**
 * lrg_tooltip_add_double_line:
 * @self: a #LrgTooltip
 * @left: the left text
 * @right: the right text
 * @left_color: the left colour as 0xRRGGBBAA
 * @right_color: the right colour as 0xRRGGBBAA
 *
 * Appends a left/right pair (GameTooltip:AddDoubleLine).
 *
 * Returns: (transfer none): the new line, owned by the tooltip
 */
LRG_AVAILABLE_IN_ALL
LrgTooltipLine *lrg_tooltip_add_double_line (LrgTooltip *self, const gchar *left, const gchar *right, guint32 left_color, guint32 right_color);

/**
 * lrg_tooltip_add_wrapped_line:
 * @self: a #LrgTooltip
 * @text: the text
 * @color: its colour as 0xRRGGBBAA
 *
 * Appends flavour text: it wraps at the tooltip's width and never widens it.
 *
 * Returns: (transfer none): the new line, owned by the tooltip
 */
LRG_AVAILABLE_IN_ALL
LrgTooltipLine *lrg_tooltip_add_wrapped_line (LrgTooltip *self, const gchar *text, guint32 color);

/**
 * lrg_tooltip_add_icon_line:
 * @self: a #LrgTooltip
 * @icon: an icon name
 * @text: the text beside it
 * @color: its colour as 0xRRGGBBAA
 *
 * Appends an icon line.
 *
 * Returns: (transfer none): the new line, owned by the tooltip
 */
LRG_AVAILABLE_IN_ALL
LrgTooltipLine *lrg_tooltip_add_icon_line (LrgTooltip *self, const gchar *icon, const gchar *text, guint32 color);

/**
 * lrg_tooltip_add_separator:
 * @self: a #LrgTooltip
 *
 * Appends a horizontal rule.
 *
 * Returns: (transfer none): the new line, owned by the tooltip
 */
LRG_AVAILABLE_IN_ALL
LrgTooltipLine *lrg_tooltip_add_separator (LrgTooltip *self);

/**
 * lrg_tooltip_add_spacer:
 * @self: a #LrgTooltip
 *
 * Appends a small empty gap.
 *
 * Returns: (transfer none): the new line, owned by the tooltip
 */
LRG_AVAILABLE_IN_ALL
LrgTooltipLine *lrg_tooltip_add_spacer (LrgTooltip *self);

/**
 * lrg_tooltip_insert_line:
 * @self: a #LrgTooltip
 * @index: where the line goes; past the end appends
 * @line: (transfer none): a line, copied
 *
 * Inserts a copy of @line before @index.
 *
 * Returns: (transfer none): the stored copy, owned by the tooltip
 */
LRG_AVAILABLE_IN_ALL
LrgTooltipLine *lrg_tooltip_insert_line (LrgTooltip *self, guint index, const LrgTooltipLine *line);

/**
 * lrg_tooltip_remove_line:
 * @self: a #LrgTooltip
 * @index: a line index
 *
 * Removes a line.
 *
 * Returns: %TRUE when @index was in range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tooltip_remove_line (LrgTooltip *self, guint index);

/**
 * lrg_tooltip_find_line:
 * @self: a #LrgTooltip
 * @id: a line id
 *
 * Returns: the index of the first line with that id, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_tooltip_find_line (LrgTooltip *self, const gchar *id);

/**
 * lrg_tooltip_remove_by_id:
 * @self: a #LrgTooltip
 * @id: a line id
 *
 * Removes every line with that id.
 *
 * Returns: how many lines were removed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tooltip_remove_by_id (LrgTooltip *self, const gchar *id);

/**
 * lrg_tooltip_replace_line:
 * @self: a #LrgTooltip
 * @index: a line index
 * @line: (transfer none): the new line, copied
 *
 * Replaces a line with a copy of @line.
 *
 * Returns: %TRUE when @index was in range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tooltip_replace_line (LrgTooltip *self, guint index, const LrgTooltipLine *line);

/**
 * lrg_tooltip_clear:
 * @self: a #LrgTooltip
 *
 * Removes every line.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tooltip_clear (LrgTooltip *self);

/**
 * lrg_tooltip_to_string:
 * @self: a #LrgTooltip
 *
 * A plain text form for logs and tests: one row per line, a double line
 * as "left | right", a separator as "---", a spacer as "", an icon line
 * as "[icon] text".
 *
 * Returns: (transfer full): the text, lines separated by newlines
 */
LRG_AVAILABLE_IN_ALL
gchar *lrg_tooltip_to_string (LrgTooltip *self);

G_END_DECLS
