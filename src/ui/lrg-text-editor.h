/* lrg-text-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTextEditor: a multi-line, syntax highlighted text editor.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"
#include "lrg-syntax.h"

G_BEGIN_DECLS

#define LRG_TYPE_TEXT_EDITOR (lrg_text_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTextEditor, lrg_text_editor, LRG, TEXT_EDITOR, LrgContainer)

/**
 * lrg_text_editor_new:
 *
 * Creates an empty, editable editor in the "ui-mono" font with line
 * numbers off, no wrapping and no language.
 *
 * Returns: (transfer full): a new #LrgTextEditor
 */
LRG_AVAILABLE_IN_ALL
LrgTextEditor * lrg_text_editor_new (void);

/* ==========================================================================
 * Text
 * ========================================================================== */

/**
 * lrg_text_editor_get_text:
 * @self: an #LrgTextEditor
 *
 * Returns: (transfer full): the whole text, lines joined with '\n'
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_text_editor_get_text (LrgTextEditor *self);

/**
 * lrg_text_editor_set_text:
 * @self: an #LrgTextEditor
 * @text: (nullable): new UTF-8 text ("\r\n" and "\r" become '\n')
 *
 * Replaces the text, puts the caret at the start, clears the selection
 * and the undo history and scrolls to the top. Invalid UTF-8 is
 * replaced with U+FFFD.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_set_text (LrgTextEditor *self,
                               const gchar   *text);

/**
 * lrg_text_editor_get_line_count:
 * @self: an #LrgTextEditor
 *
 * Returns: the number of lines (at least 1)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_text_editor_get_line_count (LrgTextEditor *self);

/**
 * lrg_text_editor_get_line:
 * @self: an #LrgTextEditor
 * @line: a line index
 *
 * Returns: (transfer none) (nullable): the text of @line without its
 *   newline, valid until the next edit, or %NULL if out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_text_editor_get_line (LrgTextEditor *self,
                                        guint          line);

/**
 * lrg_text_editor_insert_text:
 * @self: an #LrgTextEditor
 * @text: UTF-8 text
 *
 * Inserts @text at the caret, replacing the selection, as one undo
 * step. Works in read-only mode too (it is the program editing).
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_insert_text (LrgTextEditor *self,
                                  const gchar   *text);

/**
 * lrg_text_editor_append:
 * @self: an #LrgTextEditor
 * @text: UTF-8 text
 *
 * Appends @text at the end of the document (for consoles and logs).
 * When the caret was at the end it stays there and the view follows
 * the new text; otherwise caret and view are left alone. Not recorded
 * for undo. With #LrgTextEditor:max-lines the oldest lines are dropped.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_append (LrgTextEditor *self,
                             const gchar   *text);

/**
 * lrg_text_editor_delete_selection:
 * @self: an #LrgTextEditor
 *
 * Deletes the selected text as one undo step.
 *
 * Returns: %TRUE if there was a selection
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_text_editor_delete_selection (LrgTextEditor *self);

/* ==========================================================================
 * Caret and selection
 * ========================================================================== */

/**
 * lrg_text_editor_get_cursor:
 * @self: an #LrgTextEditor
 * @line: (out) (optional): caret line
 * @column: (out) (optional): caret column, in characters
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_get_cursor (LrgTextEditor *self,
                                 guint         *line,
                                 guint         *column);

/**
 * lrg_text_editor_set_cursor:
 * @self: an #LrgTextEditor
 * @line: a line (clamped)
 * @column: a column in characters (clamped to the line)
 *
 * Moves the caret and clears the selection; the view scrolls to show it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_set_cursor (LrgTextEditor *self,
                                 guint          line,
                                 guint          column);

/**
 * lrg_text_editor_select_range:
 * @self: an #LrgTextEditor
 * @anchor_line: line of the fixed end
 * @anchor_column: column of the fixed end, in characters
 * @cursor_line: line of the caret end
 * @cursor_column: column of the caret end, in characters
 *
 * Selects from the anchor to the caret (either order).
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_select_range (LrgTextEditor *self,
                                   guint          anchor_line,
                                   guint          anchor_column,
                                   guint          cursor_line,
                                   guint          cursor_column);

/**
 * lrg_text_editor_select_all:
 * @self: an #LrgTextEditor
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_select_all (LrgTextEditor *self);

/**
 * lrg_text_editor_get_selection_bounds:
 * @self: an #LrgTextEditor
 * @start_line: (out) (optional): first selected line
 * @start_column: (out) (optional): its column
 * @end_line: (out) (optional): last line
 * @end_column: (out) (optional): column after the selection
 *
 * Returns: %TRUE if something is selected
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_text_editor_get_selection_bounds (LrgTextEditor *self,
                                               guint         *start_line,
                                               guint         *start_column,
                                               guint         *end_line,
                                               guint         *end_column);

/**
 * lrg_text_editor_get_selected_text:
 * @self: an #LrgTextEditor
 *
 * Returns: (transfer full) (nullable): the selected text, or %NULL
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_text_editor_get_selected_text (LrgTextEditor *self);

/* ==========================================================================
 * Undo and clipboard
 * ========================================================================== */

/**
 * lrg_text_editor_undo:
 * @self: an #LrgTextEditor
 *
 * Reverts the last undo step (a run of typing is one step).
 *
 * Returns: %TRUE if something was undone
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_text_editor_undo (LrgTextEditor *self);

/**
 * lrg_text_editor_redo:
 * @self: an #LrgTextEditor
 *
 * Returns: %TRUE if something was redone
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_text_editor_redo (LrgTextEditor *self);

/**
 * lrg_text_editor_can_undo:
 * @self: an #LrgTextEditor
 *
 * Returns: %TRUE if there is an undo step
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_text_editor_can_undo (LrgTextEditor *self);

/**
 * lrg_text_editor_can_redo:
 * @self: an #LrgTextEditor
 *
 * Returns: %TRUE if there is a redo step
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_text_editor_can_redo (LrgTextEditor *self);

/**
 * lrg_text_editor_clear_history:
 * @self: an #LrgTextEditor
 *
 * Forgets every undo and redo step.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_clear_history (LrgTextEditor *self);

/**
 * lrg_text_editor_copy_clipboard:
 * @self: an #LrgTextEditor
 *
 * Copies the selection to the system clipboard (a process-local
 * clipboard without a window).
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_copy_clipboard (LrgTextEditor *self);

/**
 * lrg_text_editor_cut_clipboard:
 * @self: an #LrgTextEditor
 *
 * Copies and deletes the selection (copies only when read-only).
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_cut_clipboard (LrgTextEditor *self);

/**
 * lrg_text_editor_paste_clipboard:
 * @self: an #LrgTextEditor
 *
 * Inserts the clipboard at the caret (ignored when read-only).
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_paste_clipboard (LrgTextEditor *self);

/* ==========================================================================
 * Indentation
 * ========================================================================== */

/**
 * lrg_text_editor_indent:
 * @self: an #LrgTextEditor
 *
 * Indents every line touched by the selection (or the caret line) by
 * one level: a tab, or #LrgTextEditor:tab-width spaces.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_indent (LrgTextEditor *self);

/**
 * lrg_text_editor_outdent:
 * @self: an #LrgTextEditor
 *
 * Removes up to one level of leading whitespace from every line
 * touched by the selection (or the caret line).
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_outdent (LrgTextEditor *self);

/* ==========================================================================
 * Find
 * ========================================================================== */

/**
 * lrg_text_editor_find:
 * @self: an #LrgTextEditor
 * @needle: (nullable): text to find (single line), %NULL or "" to stop
 * @case_sensitive: whether letters must match case; when %FALSE, text
 *   is compared after Unicode case folding and canonical decomposition
 *   ("straße" finds "STRASSE", "äbc" finds "ÄBC")
 *
 * Highlights every match and selects the first one at or after the
 * caret (wrapping around).
 *
 * Returns: %TRUE if there is a match
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_text_editor_find (LrgTextEditor *self,
                               const gchar   *needle,
                               gboolean       case_sensitive);

/**
 * lrg_text_editor_find_next:
 * @self: an #LrgTextEditor
 *
 * Selects the next match after the caret, wrapping at the end.
 *
 * Returns: %TRUE if there is a match
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_text_editor_find_next (LrgTextEditor *self);

/**
 * lrg_text_editor_find_previous:
 * @self: an #LrgTextEditor
 *
 * Selects the match before the selection start, wrapping at the top.
 *
 * Returns: %TRUE if there is a match
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_text_editor_find_previous (LrgTextEditor *self);

/**
 * lrg_text_editor_get_match_count:
 * @self: an #LrgTextEditor
 *
 * Returns: how many matches of the current find text there are
 */
LRG_AVAILABLE_IN_ALL
guint lrg_text_editor_get_match_count (LrgTextEditor *self);

/**
 * lrg_text_editor_get_bracket_match:
 * @self: an #LrgTextEditor
 * @line: (out) (optional): line of the bracket at the caret
 * @column: (out) (optional): its column in characters
 * @match_line: (out) (optional): line of its partner
 * @match_column: (out) (optional): the partner's column in characters
 *
 * The bracket pair the editor outlines: the bracket just before the
 * caret if it has a partner, else the one just after it. With a
 * language set, brackets inside strings and comments are ignored, both
 * for nesting and at the caret (a caret on such a bracket has no
 * match). The search is bounded, so a partner very far away is not
 * found.
 *
 * Returns: %TRUE if the caret is next to a bracket with a partner
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_text_editor_get_bracket_match (LrgTextEditor *self,
                                            guint         *line,
                                            guint         *column,
                                            guint         *match_line,
                                            guint         *match_column);

/* ==========================================================================
 * View
 * ========================================================================== */

/**
 * lrg_text_editor_scroll_to_line:
 * @self: an #LrgTextEditor
 * @line: a line index
 * @animate: glide there (%FALSE jumps)
 *
 * Scrolls so @line is the first visible line (as far as possible).
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_scroll_to_line (LrgTextEditor *self,
                                     guint          line,
                                     gboolean       animate);

/**
 * lrg_text_editor_get_scroll:
 * @self: an #LrgTextEditor
 * @x: (out) (optional): horizontal scroll target
 * @y: (out) (optional): vertical scroll target
 *
 * The offsets the view is scrolling to (the displayed offsets follow
 * on springs).
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_get_scroll (LrgTextEditor *self,
                                 gfloat        *x,
                                 gfloat        *y);

/**
 * lrg_text_editor_get_line_height:
 * @self: an #LrgTextEditor
 *
 * Returns: the height of one text row
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_text_editor_get_line_height (LrgTextEditor *self);

/**
 * lrg_text_editor_get_row_count:
 * @self: an #LrgTextEditor
 *
 * Returns: the number of visual rows: the line count, or more when
 *   #LrgTextEditor:wrap splits long lines
 */
LRG_AVAILABLE_IN_ALL
guint lrg_text_editor_get_row_count (LrgTextEditor *self);

/**
 * lrg_text_editor_get_tokenize_count:
 * @self: an #LrgTextEditor
 *
 * Diagnostics: how many times a line was tokenized with spans (for
 * painting) since the editor was created. Lines are tokenized lazily,
 * only when painted, and again only when they or the state entering
 * them changed.
 *
 * Returns: the count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_text_editor_get_tokenize_count (LrgTextEditor *self);

/**
 * lrg_text_editor_set_token_color:
 * @self: an #LrgTextEditor
 * @token: a token kind
 * @color: (nullable): the colour, %NULL to use the language / theme
 *
 * Overrides one token colour for this editor only.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_set_token_color (LrgTextEditor  *self,
                                      LrgSyntaxToken  token,
                                      const GrlColor *color);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_text_editor_get_language:
 * @self: an #LrgTextEditor
 *
 * Returns: (transfer none) (nullable): the language name, %NULL for
 *   plain text
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_text_editor_get_language (LrgTextEditor *self);

/**
 * lrg_text_editor_set_language:
 * @self: an #LrgTextEditor
 * @language: (nullable): a name for lrg_syntax_get_for_name(), %NULL
 *   for plain text
 *
 * Unknown names fall back to plain text (with a debug message).
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_set_language (LrgTextEditor *self,
                                   const gchar   *language);

/**
 * lrg_text_editor_get_syntax:
 * @self: an #LrgTextEditor
 *
 * Returns: (transfer none) (nullable): the highlighter
 */
LRG_AVAILABLE_IN_ALL
LrgSyntax * lrg_text_editor_get_syntax (LrgTextEditor *self);

/**
 * lrg_text_editor_set_syntax:
 * @self: an #LrgTextEditor
 * @syntax: (nullable): a highlighter, %NULL for plain text
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_set_syntax (LrgTextEditor *self,
                                 LrgSyntax     *syntax);

/**
 * lrg_text_editor_get_show_line_numbers:
 * @self: an #LrgTextEditor
 *
 * Returns: whether the line-number gutter shows
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_text_editor_get_show_line_numbers (LrgTextEditor *self);

/**
 * lrg_text_editor_set_show_line_numbers:
 * @self: an #LrgTextEditor
 * @show: show the gutter
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_set_show_line_numbers (LrgTextEditor *self,
                                            gboolean       show);

/**
 * lrg_text_editor_get_wrap:
 * @self: an #LrgTextEditor
 *
 * Returns: whether long lines wrap
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_text_editor_get_wrap (LrgTextEditor *self);

/**
 * lrg_text_editor_set_wrap:
 * @self: an #LrgTextEditor
 * @wrap: wrap long lines at word boundaries (%FALSE scrolls sideways)
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_set_wrap (LrgTextEditor *self,
                               gboolean       wrap);

/**
 * lrg_text_editor_get_read_only:
 * @self: an #LrgTextEditor
 *
 * Returns: whether user editing is blocked
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_text_editor_get_read_only (LrgTextEditor *self);

/**
 * lrg_text_editor_set_read_only:
 * @self: an #LrgTextEditor
 * @read_only: block typing, paste, cut, undo (selection and copy work)
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_set_read_only (LrgTextEditor *self,
                                    gboolean       read_only);

/**
 * lrg_text_editor_get_tab_width:
 * @self: an #LrgTextEditor
 *
 * Returns: columns per tab stop
 */
LRG_AVAILABLE_IN_ALL
guint lrg_text_editor_get_tab_width (LrgTextEditor *self);

/**
 * lrg_text_editor_set_tab_width:
 * @self: an #LrgTextEditor
 * @tab_width: columns per tab stop (1..16)
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_set_tab_width (LrgTextEditor *self,
                                    guint          tab_width);

/**
 * lrg_text_editor_get_insert_spaces:
 * @self: an #LrgTextEditor
 *
 * Returns: whether Tab inserts spaces
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_text_editor_get_insert_spaces (LrgTextEditor *self);

/**
 * lrg_text_editor_set_insert_spaces:
 * @self: an #LrgTextEditor
 * @insert_spaces: Tab inserts spaces up to the next tab stop
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_set_insert_spaces (LrgTextEditor *self,
                                        gboolean       insert_spaces);

/**
 * lrg_text_editor_get_placeholder:
 * @self: an #LrgTextEditor
 *
 * Returns: (transfer none) (nullable): the placeholder
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_text_editor_get_placeholder (LrgTextEditor *self);

/**
 * lrg_text_editor_set_placeholder:
 * @self: an #LrgTextEditor
 * @placeholder: (nullable): hint shown while the editor is empty
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_set_placeholder (LrgTextEditor *self,
                                      const gchar   *placeholder);

/**
 * lrg_text_editor_get_submit_on_enter:
 * @self: an #LrgTextEditor
 *
 * Returns: whether Enter emits #LrgTextEditor::submit
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_text_editor_get_submit_on_enter (LrgTextEditor *self);

/**
 * lrg_text_editor_set_submit_on_enter:
 * @self: an #LrgTextEditor
 * @submit_on_enter: Enter emits ::submit and Shift+Enter inserts a
 *   newline (chat boxes, consoles)
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_set_submit_on_enter (LrgTextEditor *self,
                                          gboolean       submit_on_enter);

/**
 * lrg_text_editor_get_max_lines:
 * @self: an #LrgTextEditor
 *
 * Returns: the line limit, 0 for none
 */
LRG_AVAILABLE_IN_ALL
guint lrg_text_editor_get_max_lines (LrgTextEditor *self);

/**
 * lrg_text_editor_set_max_lines:
 * @self: an #LrgTextEditor
 * @max_lines: keep at most this many lines, dropping the oldest; 0
 *   for no limit
 *
 * Dropping lines clears the undo history.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_set_max_lines (LrgTextEditor *self,
                                    guint          max_lines);

/**
 * lrg_text_editor_get_font:
 * @self: an #LrgTextEditor
 *
 * Returns: (transfer none): the font name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_text_editor_get_font (LrgTextEditor *self);

/**
 * lrg_text_editor_set_font:
 * @self: an #LrgTextEditor
 * @font: (nullable): a name registered in the default #LrgFontManager,
 *   %NULL for "ui-mono" (falls back to the theme UI font)
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_set_font (LrgTextEditor *self,
                               const gchar   *font);

/**
 * lrg_text_editor_get_font_size:
 * @self: an #LrgTextEditor
 *
 * Returns: the font size, 0 when the theme's FONT_SIZE is used
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_text_editor_get_font_size (LrgTextEditor *self);

/**
 * lrg_text_editor_set_font_size:
 * @self: an #LrgTextEditor
 * @size: font size, 0 for the theme's FONT_SIZE
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_set_font_size (LrgTextEditor *self,
                                    gfloat         size);

/**
 * lrg_text_editor_get_highlight_current_line:
 * @self: an #LrgTextEditor
 *
 * Returns: whether the caret line is tinted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_text_editor_get_highlight_current_line (LrgTextEditor *self);

/**
 * lrg_text_editor_set_highlight_current_line:
 * @self: an #LrgTextEditor
 * @highlight: tint the caret line
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_set_highlight_current_line (LrgTextEditor *self,
                                                 gboolean       highlight);

/**
 * lrg_text_editor_get_auto_indent:
 * @self: an #LrgTextEditor
 *
 * Returns: whether Enter copies the indentation
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_text_editor_get_auto_indent (LrgTextEditor *self);

/**
 * lrg_text_editor_set_auto_indent:
 * @self: an #LrgTextEditor
 * @auto_indent: Enter copies the current line's indentation (one level
 *   more after an opening bracket or ':')
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_editor_set_auto_indent (LrgTextEditor *self,
                                      gboolean       auto_indent);

G_END_DECLS
