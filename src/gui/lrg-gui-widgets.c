/* lrg-gui-widgets.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Value editors and trees for the immediate-mode GUI: tree nodes and
 * collapsing headers, drag/slider number fields (with type-in mode), the
 * single-line text editor, combos, the colour editor, progress bars and
 * images.
 */

#include "lrg-gui-private.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==========================================================================
 * Trees
 * ========================================================================== */

gboolean
lrg_gui_tree_node (LrgGuiContext   *self,
                   const gchar     *label,
                   LrgGuiTreeFlags  flags)
{
	LrgGuiId     id;
	GuiState    *st;
	LrgGuiRect   rect, arrow;
	const gchar *end;
	gboolean     hovered, held, pressed, visible;
	gboolean     leaf = (flags & LRG_GUI_TREE_LEAF) != 0;
	gboolean     framed = (flags & LRG_GUI_TREE_FRAMED) != 0;
	gfloat       h, w, arrow_w, text_x;
	LrgGuiColor  text_col;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (label != NULL, FALSE);

	id = gui_hash_label (self, label);
	end = gui_label_end (label);
	st = gui_state (self, id);

	/* First appearance and forced state. */
	if (!st->initialized)
	{
		st->initialized = TRUE;
		st->open = (flags & LRG_GUI_TREE_DEFAULT_OPEN) != 0;
	}
	if (self->next_item_open >= 0)
	{
		st->open = (self->next_item_open == 1);
		self->next_item_open = -1;
	}

	h = framed ? gui_frame_height (self) : self->style.font_size + self->style.frame_padding_y * 2.0f;
	w = gui_item_width (self, 0.0f);
	arrow_w = self->style.font_size;

	visible = gui_item_add (self, id, w, h, &rect);
	arrow.x = rect.x;
	arrow.y = rect.y;
	arrow.w = arrow_w + self->style.frame_padding_x * 0.5f;
	arrow.h = rect.h;

	/* Rows activate on press so they can start drags. */
	pressed = gui_button_behavior (self, id, &rect,
	                               GUI_BUTTON_ON_PRESS | GUI_BUTTON_RIGHT, &hovered, &held);

	if (!leaf && pressed && self->item.clicked[LRG_GUI_MOUSE_LEFT])
	{
		gboolean on_arrow = gui_rect_contains (&arrow, self->input.mouse_x, self->input.mouse_y);

		/* OPEN_ON_ARROW rows toggle only from the arrow or a double
		 * click; other rows toggle from anywhere. */
		if (!(flags & LRG_GUI_TREE_OPEN_ON_ARROW) || on_arrow)
			st->open = !st->open;
	}
	if (!leaf && (flags & LRG_GUI_TREE_OPEN_ON_ARROW) && self->item.double_clicked &&
	    !gui_rect_contains (&arrow, self->input.mouse_x, self->input.mouse_y))
		st->open = !st->open;

	/* Keyboard: arrows on the hovered row. */
	if (!leaf && hovered && self->focus_id == 0)
	{
		if (gui_key_pressed (self, LRG_GUI_KEY_RIGHT, FALSE, NULL))
			st->open = TRUE;
		else if (gui_key_pressed (self, LRG_GUI_KEY_LEFT, FALSE, NULL))
			st->open = FALSE;
	}

	if (visible)
	{
		if (framed)
			gui_draw_frame (self, &rect,
			                gui_col (self, held ? LRG_GUI_COL_HEADER_ACTIVE
			                                    : (hovered ? LRG_GUI_COL_HEADER_HOVERED
			                                               : LRG_GUI_COL_BUTTON)), FALSE);
		else if ((flags & LRG_GUI_TREE_SELECTED) || hovered)
			lrg_gui_draw_list_rect_filled (gui_dl (self), &rect,
			                               gui_col (self, (flags & LRG_GUI_TREE_SELECTED)
			                                              ? LRG_GUI_COL_HEADER
			                                              : LRG_GUI_COL_HEADER_HOVERED),
			                               self->style.rounding);

		if (!leaf)
			gui_draw_arrow (self, rect.x + arrow_w * 0.5f + 2.0f, rect.y + rect.h * 0.5f,
			                self->style.font_size * 0.55f, st->open,
			                gui_col (self, LRG_GUI_COL_TEXT_DISABLED));

		text_col = gui_col (self, (flags & LRG_GUI_TREE_DIMMED) ? LRG_GUI_COL_TEXT_DISABLED
		                                                        : LRG_GUI_COL_TEXT);
		text_x = rect.x + arrow_w + self->style.frame_padding_x * 0.5f;
		{
			LrgGuiRect text = { text_x, rect.y, rect.x + rect.w - text_x, rect.h };

			gui_draw_text_clipped (self, &text, label, end - label, text_col, 0.0f, FALSE);
		}
	}

	if (st->open && !leaf)
	{
		gfloat indent = framed ? 0.0f : self->style.indent;

		lrg_gui_push_id_int (self, (gint64) id);
		if (indent > 0.0f)
			lrg_gui_indent (self, indent);
		g_array_append_val (self->tree_indents, indent);
		return TRUE;
	}

	return FALSE;
}

void
lrg_gui_tree_pop (LrgGuiContext *self)
{
	gfloat indent;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (self->tree_indents->len > 0);

	indent = g_array_index (self->tree_indents, gfloat, self->tree_indents->len - 1);
	g_array_set_size (self->tree_indents, self->tree_indents->len - 1);
	if (indent > 0.0f)
		lrg_gui_unindent (self, indent);
	lrg_gui_pop_id (self);
}

void
lrg_gui_set_next_item_open (LrgGuiContext *self,
                            gboolean       open)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	self->next_item_open = open ? 1 : 0;
}

gboolean
lrg_gui_collapsing_header (LrgGuiContext *self,
                           const gchar   *label,
                           gboolean       default_open)
{
	gboolean open;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);

	open = lrg_gui_tree_node (self, label, LRG_GUI_TREE_FRAMED |
	                          (default_open ? LRG_GUI_TREE_DEFAULT_OPEN : 0));
	if (open)
		lrg_gui_tree_pop (self);   /* headers do not indent or scope */
	return open;
}

/* ==========================================================================
 * Text editing
 * ========================================================================== */

/* Byte offset of the UTF-8 character before/after @pos. */
static gint
prev_char (const gchar *s,
           gint         pos)
{
	const gchar *p;

	if (pos <= 0)
		return 0;
	p = g_utf8_find_prev_char (s, s + pos);
	return (p != NULL) ? (gint) (p - s) : 0;
}

static gint
next_char (const gchar *s,
           gint         len,
           gint         pos)
{
	if (pos >= len)
		return len;
	return (gint) (g_utf8_next_char (s + pos) - s);
}

static gboolean
is_word_char (gunichar c)
{
	return g_unichar_isalnum (c) || c == '_';
}

static gint
word_left (const gchar *s,
           gint         pos)
{
	/* Skip separators, then the word. */
	while (pos > 0 && !is_word_char (g_utf8_get_char (s + prev_char (s, pos))))
		pos = prev_char (s, pos);
	while (pos > 0 && is_word_char (g_utf8_get_char (s + prev_char (s, pos))))
		pos = prev_char (s, pos);
	return pos;
}

static gint
word_right (const gchar *s,
            gint         len,
            gint         pos)
{
	while (pos < len && !is_word_char (g_utf8_get_char (s + pos)))
		pos = next_char (s, len, pos);
	while (pos < len && is_word_char (g_utf8_get_char (s + pos)))
		pos = next_char (s, len, pos);
	return pos;
}

static gboolean
has_selection (LrgGuiContext *self)
{
	return self->edit_anchor != self->edit_cursor;
}

static void
delete_selection (LrgGuiContext *self)
{
	gint a = MIN (self->edit_anchor, self->edit_cursor);
	gint b = MAX (self->edit_anchor, self->edit_cursor);

	g_string_erase (self->edit_buf, a, b - a);
	self->edit_cursor = a;
	self->edit_anchor = a;
	self->edit_dirty = TRUE;
}

static void
insert_text (LrgGuiContext *self,
             const gchar   *text,
             gssize         len)
{
	if (has_selection (self))
		delete_selection (self);
	if (len < 0)
		len = strlen (text);
	g_string_insert_len (self->edit_buf, self->edit_cursor, text, len);
	self->edit_cursor += (gint) len;
	self->edit_anchor = self->edit_cursor;
	self->edit_dirty = TRUE;
}

/* Byte offset whose glyph boundary is nearest to @x within the field. */
static gint
offset_at_x (LrgGuiContext *self,
             const gchar   *text,
             gfloat         x,
             gboolean       mono)
{
	gint         len = (gint) strlen (text);
	gint         pos = 0;
	gfloat       prev_w = 0.0f;

	while (pos < len)
	{
		gint   next = next_char (text, len, pos);
		gfloat w = gui_text_width (self, text, next, mono);

		if (x < (prev_w + w) * 0.5f)
			return pos;
		prev_w = w;
		pos = next;
	}
	return len;
}

void
gui_focus_text (LrgGuiContext *self,
                LrgGuiId       id,
                const gchar   *initial,
                gboolean       select_all)
{
	self->focus_id = id;
	self->focus_seen = TRUE;
	g_string_assign (self->edit_buf, initial != NULL ? initial : "");
	g_string_assign (self->edit_initial, self->edit_buf->str);
	self->edit_cursor = (gint) self->edit_buf->len;
	self->edit_anchor = select_all ? 0 : self->edit_cursor;
	self->edit_scroll = 0.0f;
	self->edit_dirty = FALSE;
	self->caret_time = self->input.time;
}

/* Masks @text with bullets for password fields. */
static gchar *
display_text (const gchar *text,
              gboolean     password)
{
	GString *out;
	glong    n, i;

	if (!password)
		return g_strdup (text);

	n = g_utf8_strlen (text, -1);
	out = g_string_sized_new (n * 3);
	for (i = 0; i < n; i++)
		g_string_append (out, "\xe2\x80\xa2");
	return g_string_free (out, FALSE);
}

/*
 * gui_input_text_ex:
 *
 * The text field in @rect. While focused it edits an internal buffer so
 * that callers rebuilding their GString every frame (immediate mode) do not
 * fight the editor; changes are copied out on every edit, or only on
 * commit with ENTER_RETURNS_TRUE. @committed reports Enter/focus loss.
 */
gboolean
gui_input_text_ex (LrgGuiContext        *self,
                   LrgGuiId              id,
                   const LrgGuiRect     *rect,
                   const gchar          *hint,
                   GString              *buffer,
                   LrgGuiInputTextFlags  flags,
                   gboolean             *committed)
{
	gboolean    hovered, held;
	gboolean    focused;
	gboolean    changed = FALSE;
	gboolean    commit = FALSE;
	gboolean    mono = (flags & LRG_GUI_INPUT_TEXT_MONOSPACE) != 0;
	gboolean    readonly = (flags & LRG_GUI_INPUT_TEXT_READ_ONLY) != 0;
	gboolean    enter_mode = (flags & LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE) != 0;
	gfloat      pad = self->style.frame_padding_x;
	LrgGuiRect  inner;
	guint       i;
	gint        pass;

	if (committed != NULL)
		*committed = FALSE;

	gui_button_behavior (self, id, rect, GUI_BUTTON_ON_PRESS, &hovered, &held);
	if (hovered)
		self->cursor = LRG_GUI_CURSOR_TEXT;

	inner.x = rect->x + pad;
	inner.y = rect->y;
	inner.w = rect->w - pad * 2.0f;
	inner.h = rect->h;

	/* A focus request for a field that already has focus is satisfied. */
	if (self->focus_id == id)
		self->focus_request = FALSE;

	/* Focus by click, explicit request or Tab from the previous field. */
	if (self->focus_id != id &&
	    ((hovered && self->mouse_clicked[LRG_GUI_MOUSE_LEFT]) || self->focus_request ||
	     self->focus_request_id == id || (self->focus_next && self->focus_from != id)))
	{
		gboolean select_all = self->focus_request || self->focus_next || self->focus_request_id == id ||
		                      (flags & LRG_GUI_INPUT_TEXT_AUTO_SELECT_ALL);

		self->focus_request = FALSE;
		if (self->focus_request_id == id)
			self->focus_request_id = 0;
		self->focus_next = FALSE;
		gui_focus_text (self, id, buffer->str, select_all);

		/* A plain click places the caret under the pointer. */
		if (!select_all)
		{
			g_autofree gchar *shown = display_text (self->edit_buf->str,
			                                        flags & LRG_GUI_INPUT_TEXT_PASSWORD);

			self->edit_cursor = offset_at_x (self, shown, self->input.mouse_x - inner.x, mono);
			if (flags & LRG_GUI_INPUT_TEXT_PASSWORD)
				self->edit_cursor = (gint) (g_utf8_offset_to_pointer (self->edit_buf->str,
				                            self->edit_cursor / 3) - self->edit_buf->str);
			self->edit_anchor = self->edit_cursor;
		}
	}

	focused = (self->focus_id == id);
	if (focused)
	{
		const gchar *s;
		gint         len;

		self->focus_seen = TRUE;

		/* Click outside: lose focus (and commit). */
		if ((self->mouse_clicked[LRG_GUI_MOUSE_LEFT] || self->mouse_clicked[LRG_GUI_MOUSE_RIGHT]) &&
		    !hovered)
		{
			self->focus_id = 0;
			focused = FALSE;
			commit = self->edit_dirty;
		}
		else
		{
			/* Mouse selection: press places, drag extends, double-click
			 * selects everything. */
			if (held && !self->mouse_clicked[LRG_GUI_MOUSE_LEFT])
				self->edit_cursor = offset_at_x (self, self->edit_buf->str,
				                                 self->input.mouse_x - inner.x + self->edit_scroll, mono);
			else if (hovered && self->mouse_clicked[LRG_GUI_MOUSE_LEFT])
			{
				if (self->mouse_double[LRG_GUI_MOUSE_LEFT])
				{
					self->edit_anchor = 0;
					self->edit_cursor = (gint) self->edit_buf->len;
				}
				else
				{
					self->edit_cursor = offset_at_x (self, self->edit_buf->str,
					                                 self->input.mouse_x - inner.x + self->edit_scroll, mono);
					if (!(self->input.mods & LRG_GUI_MOD_SHIFT))
						self->edit_anchor = self->edit_cursor;
				}
			}

			/* Keys, in two passes: editing and movement keys first, then
			 * the typed text, then Enter/Tab/Escape, so "select all, type,
			 * press Enter" within one frame does what it says. */
			for (pass = 0; pass < 2 && self->focus_id == id; pass++)
			{
			if (pass == 1)
			{
				for (i = 0; i < self->input.n_chars && !readonly; i++)
				{
					gchar    utf8[8];
					gint     n;
					gunichar c = self->input.chars[i];

					if (c < 0x20 || c == 0x7f)
						continue;
					n = g_unichar_to_utf8 (c, utf8);
					insert_text (self, utf8, n);
				}
			}
			for (i = 0; i < self->input.n_keys; i++)
			{
				const LrgGuiKeyEvent *ev = &self->input.keys[i];
				gboolean              ctrl = (ev->mods & LRG_GUI_MOD_CTRL) != 0;
				gboolean              shift = (ev->mods & LRG_GUI_MOD_SHIFT) != 0;
				gint                  target = -1;
				gboolean              final_key = ev->key == LRG_GUI_KEY_ENTER ||
				                                  ev->key == LRG_GUI_KEY_TAB ||
				                                  ev->key == LRG_GUI_KEY_ESCAPE;

				if ((pass == 1) != final_key)
					continue;

				s = self->edit_buf->str;
				len = (gint) self->edit_buf->len;

				switch (ev->key)
				{
				case LRG_GUI_KEY_LEFT:
					if (has_selection (self) && !shift)
						target = MIN (self->edit_anchor, self->edit_cursor);
					else
						target = ctrl ? word_left (s, self->edit_cursor) : prev_char (s, self->edit_cursor);
					break;
				case LRG_GUI_KEY_RIGHT:
					if (has_selection (self) && !shift)
						target = MAX (self->edit_anchor, self->edit_cursor);
					else
						target = ctrl ? word_right (s, len, self->edit_cursor)
						              : next_char (s, len, self->edit_cursor);
					break;
				case LRG_GUI_KEY_HOME:
					target = 0;
					break;
				case LRG_GUI_KEY_END:
					target = len;
					break;
				case LRG_GUI_KEY_BACKSPACE:
					if (readonly)
						break;
					if (has_selection (self))
						delete_selection (self);
					else if (self->edit_cursor > 0)
					{
						gint from = ctrl ? word_left (s, self->edit_cursor) : prev_char (s, self->edit_cursor);

						g_string_erase (self->edit_buf, from, self->edit_cursor - from);
						self->edit_cursor = from;
						self->edit_anchor = from;
						self->edit_dirty = TRUE;
					}
					break;
				case LRG_GUI_KEY_DELETE:
					if (readonly)
						break;
					if (has_selection (self))
						delete_selection (self);
					else if (self->edit_cursor < len)
					{
						gint to = ctrl ? word_right (s, len, self->edit_cursor)
						               : next_char (s, len, self->edit_cursor);

						g_string_erase (self->edit_buf, self->edit_cursor, to - self->edit_cursor);
						self->edit_dirty = TRUE;
					}
					break;
				case LRG_GUI_KEY_A:
					if (ctrl)
					{
						self->edit_anchor = 0;
						self->edit_cursor = len;
					}
					break;
				case LRG_GUI_KEY_C:
				case LRG_GUI_KEY_X:
					if (ctrl && has_selection (self) && !(flags & LRG_GUI_INPUT_TEXT_PASSWORD))
					{
						gint              a = MIN (self->edit_anchor, self->edit_cursor);
						gint              b = MAX (self->edit_anchor, self->edit_cursor);
						g_autofree gchar *sel = g_strndup (s + a, b - a);

						lrg_gui_context_set_clipboard (self, sel);
						if (ev->key == LRG_GUI_KEY_X && !readonly)
							delete_selection (self);
					}
					break;
				case LRG_GUI_KEY_V:
					if (ctrl && !readonly)
					{
						g_autofree gchar *clip = lrg_gui_context_get_clipboard (self);

						if (clip != NULL)
						{
							gchar *nl;

							/* Single line: paste up to the first newline. */
							nl = strpbrk (clip, "\r\n");
							if (nl != NULL)
								*nl = '\0';
							insert_text (self, clip, -1);
						}
					}
					break;
				case LRG_GUI_KEY_ENTER:
					commit = TRUE;
					if (!(flags & LRG_GUI_INPUT_TEXT_KEEP_FOCUS))
						self->focus_id = 0;
					else
					{
						g_string_assign (self->edit_initial, self->edit_buf->str);
					}
					break;
				case LRG_GUI_KEY_TAB:
					commit = self->edit_dirty;
					self->focus_id = 0;
					self->focus_next = TRUE;
					self->focus_from = id;
					break;
				case LRG_GUI_KEY_ESCAPE:
					/* Revert to the text at focus time. */
					g_string_assign (self->edit_buf, self->edit_initial->str);
					self->edit_dirty = FALSE;
					self->focus_id = 0;
					if (!enter_mode)
					{
						g_string_assign (buffer, self->edit_initial->str);
						changed = TRUE;
					}
					break;
				default:
					break;
				}

				if (target >= 0)
				{
					self->edit_cursor = target;
					if (!shift)
						self->edit_anchor = target;
				}
			}
			}

			self->edit_cursor = CLAMP (self->edit_cursor, 0, (gint) self->edit_buf->len);
			self->edit_anchor = CLAMP (self->edit_anchor, 0, (gint) self->edit_buf->len);
			focused = (self->focus_id == id);
		}

		/* Copy out: every edit, or only on commit in enter mode. */
		if (!enter_mode && self->edit_dirty && g_strcmp0 (buffer->str, self->edit_buf->str) != 0)
		{
			g_string_assign (buffer, self->edit_buf->str);
			changed = TRUE;
		}
		if (commit)
		{
			if (g_strcmp0 (buffer->str, self->edit_buf->str) != 0)
				changed = TRUE;
			g_string_assign (buffer, self->edit_buf->str);
			self->edit_dirty = FALSE;
			if (committed != NULL)
				*committed = TRUE;
			if (enter_mode)
				changed = TRUE;
		}
	}

	/* ---- drawing ---- */
	{
		LrgGuiDrawList   *dl = gui_dl (self);
		const gchar      *text = focused ? self->edit_buf->str : buffer->str;
		g_autofree gchar *shown = display_text (text, flags & LRG_GUI_INPUT_TEXT_PASSWORD);
		gfloat            ty = rect->y + (rect->h - self->style.font_size) * 0.5f;

		gui_draw_frame (self, rect,
		                gui_col (self, focused ? LRG_GUI_COL_FRAME_BG_ACTIVE
		                                       : (hovered ? LRG_GUI_COL_FRAME_BG_HOVERED
		                                                  : LRG_GUI_COL_FRAME_BG)), FALSE);
		if (focused)
			lrg_gui_draw_list_rect_outline (dl, rect, gui_col (self, LRG_GUI_COL_ACCENT), 1.0f,
			                                self->style.rounding);

		lrg_gui_draw_list_push_clip (dl, &inner);
		if (focused)
		{
			gint   cursor_shown = self->edit_cursor;
			gint   anchor_shown = self->edit_anchor;
			gfloat cx;

			if (flags & LRG_GUI_INPUT_TEXT_PASSWORD)
			{
				cursor_shown = (gint) g_utf8_pointer_to_offset (text, text + self->edit_cursor) * 3;
				anchor_shown = (gint) g_utf8_pointer_to_offset (text, text + self->edit_anchor) * 3;
			}

			/* Keep the caret visible by scrolling the text. */
			cx = gui_text_width (self, shown, cursor_shown, mono);
			if (cx - self->edit_scroll > inner.w - 2.0f)
				self->edit_scroll = cx - inner.w + 2.0f;
			if (cx - self->edit_scroll < 0.0f)
				self->edit_scroll = cx;
			self->edit_scroll = MAX (0.0f, self->edit_scroll);

			if (anchor_shown != cursor_shown)
			{
				gint       a = MIN (anchor_shown, cursor_shown);
				gint       b = MAX (anchor_shown, cursor_shown);
				LrgGuiRect sel;

				sel.x = inner.x - self->edit_scroll + gui_text_width (self, shown, a, mono);
				sel.w = gui_text_width (self, shown + a, b - a, mono);
				sel.y = ty - 1.0f;
				sel.h = self->style.font_size + 2.0f;
				lrg_gui_draw_list_rect_filled (dl, &sel, gui_col (self, LRG_GUI_COL_TEXT_SELECTION), 0.0f);
			}

			lrg_gui_draw_list_text (dl, roundf (inner.x - self->edit_scroll), roundf (ty), shown, -1,
			                        self->style.font_size, gui_col (self, LRG_GUI_COL_TEXT), mono);

			/* Blinking caret. */
			if (fmod (self->input.time - self->caret_time, 1.0) < 0.6 || self->input.n_keys > 0 ||
			    self->input.n_chars > 0)
			{
				gfloat x = roundf (inner.x - self->edit_scroll + cx);

				lrg_gui_draw_list_line (dl, x + 0.5f, ty - 1.0f, x + 0.5f,
				                        ty + self->style.font_size + 1.0f,
				                        gui_col (self, LRG_GUI_COL_TEXT), 1.0f);
			}
			if (self->input.n_keys > 0 || self->input.n_chars > 0)
				self->caret_time = self->input.time;
		}
		else if (*text == '\0' && hint != NULL)
		{
			lrg_gui_draw_list_text (dl, roundf (inner.x), roundf (ty), hint, -1,
			                        self->style.font_size, gui_col (self, LRG_GUI_COL_TEXT_DISABLED), mono);
		}
		else
		{
			lrg_gui_draw_list_text (dl, roundf (inner.x), roundf (ty), shown, -1,
			                        self->style.font_size, gui_col (self, LRG_GUI_COL_TEXT), mono);
		}
		lrg_gui_draw_list_pop_clip (dl);
	}

	self->item.active = focused;
	return changed;
}

gboolean
lrg_gui_input_text (LrgGuiContext        *self,
                    const gchar          *label,
                    const gchar          *hint,
                    GString              *buffer,
                    LrgGuiInputTextFlags  flags)
{
	LrgGuiId     id;
	LrgGuiRect   rect;
	const gchar *end;
	gfloat       w;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (label != NULL, FALSE);
	g_return_val_if_fail (buffer != NULL, FALSE);

	id = gui_hash_label (self, label);
	end = gui_label_end (label);
	w = gui_item_width (self, 0.0f);

	/* A visible label (no "##" prefix) goes to the right of the field. */
	if (end != label)
	{
		gfloat tw = gui_text_width (self, label, end - label, FALSE);

		w = MAX (20.0f, w - tw - self->style.item_spacing_x);
		gui_item_add (self, id, w, gui_frame_height (self), &rect);
		{
			gboolean changed = gui_input_text_ex (self, id, &rect, hint, buffer, flags, NULL);
			LrgGuiRect lr = { rect.x + rect.w + self->style.item_spacing_x, rect.y, tw, rect.h };

			gui_draw_text_clipped (self, &lr, label, end - label, gui_col (self, LRG_GUI_COL_TEXT),
			                       0.0f, FALSE);
			return changed;
		}
	}

	gui_item_add (self, id, w, gui_frame_height (self), &rect);
	return gui_input_text_ex (self, id, &rect, hint, buffer, flags, NULL);
}

void
lrg_gui_request_focus (LrgGuiContext *self,
                       const gchar   *label)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (label != NULL);

	self->focus_request_id = gui_hash_label (self, label);
}

void
lrg_gui_clear_keyboard_focus (LrgGuiContext *self)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	self->focus_id = 0;
	self->drag_edit_id = 0;
}

void
lrg_gui_set_keyboard_focus_here (LrgGuiContext *self)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	self->focus_request = TRUE;
}

/* ==========================================================================
 * Number fields
 * ========================================================================== */

/*
 * drag_scalar:
 *
 * Shared drag field for floats (the int variant rounds). Drag to scrub
 * from the value at press time; double-click or Ctrl+click switches the
 * field into a text editor until Enter/Escape/focus loss.
 */
static gboolean
drag_scalar (LrgGuiContext *self,
             LrgGuiId       id,
             const LrgGuiRect *rect,
             const gchar   *prefix,   /* nullable label drawn at the left */
             gfloat        *value,
             gfloat         speed,
             gfloat         min,
             gfloat         max,
             const gchar   *format,
             LrgGuiColor    tint,
             gboolean       integer)
{
	gboolean hovered, held;
	gboolean changed = FALSE;
	gboolean clamp = min < max;
	gchar    buf[64];

	if (format == NULL)
		format = integer ? "%.0f" : "%.3f";

	/* Type-in mode. */
	if (self->drag_edit_id == id)
	{
		g_autoptr(GString) text = g_string_new (NULL);
		gboolean           committed = FALSE;

		gui_format_value (buf, sizeof buf, format, *value);
		g_string_assign (text, buf);
		if (self->focus_id != id)
			gui_focus_text (self, id, buf, TRUE);

		gui_input_text_ex (self, id, rect, NULL, text,
		                   LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE | LRG_GUI_INPUT_TEXT_MONOSPACE,
		                   &committed);
		if (committed)
		{
			gchar  *endp = NULL;
			gdouble v = g_ascii_strtod (text->str, &endp);

			if (endp != text->str)
			{
				gfloat nv = (gfloat) v;

				if (integer)
					nv = roundf (nv);
				if (clamp)
					nv = CLAMP (nv, min, max);
				changed = (nv != *value);
				*value = nv;
			}
		}
		if (self->focus_id != id)
			self->drag_edit_id = 0;
		return changed;
	}

	gui_button_behavior (self, id, rect, GUI_BUTTON_ON_RELEASE, &hovered, &held);
	if (hovered || held)
		self->cursor = LRG_GUI_CURSOR_RESIZE_EW;

	/* Enter type-in mode. */
	if (hovered && ((self->mouse_double[LRG_GUI_MOUSE_LEFT]) ||
	                (self->mouse_clicked[LRG_GUI_MOUSE_LEFT] && (self->input.mods & LRG_GUI_MOD_CTRL))))
	{
		self->drag_edit_id = id;
		gui_format_value (buf, sizeof buf, format, *value);
		gui_focus_text (self, id, buf, TRUE);
		gui_clear_active (self);
		return FALSE;
	}

	if (self->item.activated)
	{
		self->active_value0 = *value;
		self->active_accum = 0.0f;
	}

	if (held)
	{
		gfloat dx = self->input.mouse_x - self->click_x[LRG_GUI_MOUSE_LEFT];
		gfloat step = speed;
		gfloat nv;

		if (self->input.mods & LRG_GUI_MOD_SHIFT)
			step *= 10.0f;
		if (self->input.mods & LRG_GUI_MOD_ALT)
			step *= 0.1f;

		/* Recompute from the press value so modifiers apply cleanly. */
		nv = self->active_value0 + dx * step;
		if (integer)
			nv = roundf (nv);
		if (clamp)
			nv = CLAMP (nv, min, max);
		if (nv != *value)
		{
			*value = nv;
			changed = TRUE;
		}
	}

	/* Draw. */
	{
		LrgGuiStyleColor  c = held ? LRG_GUI_COL_FRAME_BG_ACTIVE
		                           : (hovered ? LRG_GUI_COL_FRAME_BG_HOVERED : LRG_GUI_COL_FRAME_BG);
		LrgGuiRect        text_rect = *rect;

		gui_draw_frame (self, rect, gui_col (self, c), FALSE);
		if (tint != 0)
		{
			LrgGuiRect bar = { rect->x, rect->y, 3.0f, rect->h };

			lrg_gui_draw_list_rect_filled (gui_dl (self), &bar, tint, 1.0f);
		}
		if (clamp && !integer)
		{
			/* Show the value's place in its range. */
			gfloat     t = (*value - min) / (max - min);
			LrgGuiRect fill = { rect->x, rect->y + rect->h - 2.0f, rect->w * CLAMP (t, 0.0f, 1.0f), 2.0f };

			lrg_gui_draw_list_rect_filled (gui_dl (self), &fill, gui_col (self, LRG_GUI_COL_SLIDER_GRAB), 0.0f);
		}

		gui_format_value (buf, sizeof buf, format, *value);
		if (prefix != NULL && *prefix != '\0')
		{
			g_autofree gchar *full = g_strdup_printf ("%s %s", prefix, buf);

			gui_draw_text_clipped (self, &text_rect, full, -1, gui_col (self, LRG_GUI_COL_TEXT), 0.5f, TRUE);
		}
		else
		{
			gui_draw_text_clipped (self, &text_rect, buf, -1, gui_col (self, LRG_GUI_COL_TEXT), 0.5f, TRUE);
		}
	}

	return changed;
}

gboolean
lrg_gui_drag_float (LrgGuiContext *self,
                    const gchar   *label,
                    gfloat        *value,
                    gfloat         speed,
                    gfloat         min,
                    gfloat         max,
                    const gchar   *format)
{
	LrgGuiId         id;
	LrgGuiRect       rect;
	const gchar     *end;
	g_autofree gchar *prefix = NULL;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (label != NULL && value != NULL, FALSE);

	id = gui_hash_label (self, label);
	end = gui_label_end (label);
	prefix = g_strndup (label, end - label);
	gui_item_add (self, id, gui_item_width (self, 0.0f), gui_frame_height (self), &rect);
	return drag_scalar (self, id, &rect, prefix, value, speed, min, max, format, 0, FALSE);
}

gboolean
lrg_gui_drag_int (LrgGuiContext *self,
                  const gchar   *label,
                  gint          *value,
                  gfloat         speed,
                  gint           min,
                  gint           max)
{
	LrgGuiId         id;
	LrgGuiRect       rect;
	const gchar     *end;
	g_autofree gchar *prefix = NULL;
	gfloat           v;
	gboolean         changed;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (label != NULL && value != NULL, FALSE);

	id = gui_hash_label (self, label);
	end = gui_label_end (label);
	prefix = g_strndup (label, end - label);
	gui_item_add (self, id, gui_item_width (self, 0.0f), gui_frame_height (self), &rect);

	v = (gfloat) *value;
	changed = drag_scalar (self, id, &rect, prefix, &v, speed, (gfloat) min, (gfloat) max,
	                       "%.0f", 0, TRUE);
	if (changed)
		*value = (gint) v;
	return changed;
}

gboolean
lrg_gui_drag_float3 (LrgGuiContext *self,
                     const gchar   *label,
                     gfloat        *value,
                     gfloat         speed,
                     const gchar   *format)
{
	static const gchar *axes[3] = { "X", "Y", "Z" };
	gfloat              total, each;
	gboolean            changed = FALSE;
	gint                i;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (label != NULL && value != NULL, FALSE);

	total = gui_item_width (self, 0.0f);
	each = floorf ((total - self->style.item_spacing_x * 2.0f) / 3.0f);

	lrg_gui_push_id (self, label);
	for (i = 0; i < 3; i++)
	{
		LrgGuiId   id = gui_hash_label (self, axes[i]);
		LrgGuiRect rect;

		if (i > 0)
			lrg_gui_same_line (self, -1.0f);
		gui_item_add (self, id, each, gui_frame_height (self), &rect);
		if (drag_scalar (self, id, &rect, NULL, &value[i], speed, 0.0f, 0.0f, format,
		                 gui_col (self, LRG_GUI_COL_AXIS_X + i), FALSE))
			changed = TRUE;
	}
	lrg_gui_pop_id (self);

	return changed;
}

gboolean
lrg_gui_slider_float (LrgGuiContext *self,
                      const gchar   *label,
                      gfloat        *value,
                      gfloat         min,
                      gfloat         max,
                      const gchar   *format)
{
	LrgGuiId     id;
	LrgGuiRect   rect, grab;
	gboolean     hovered, held, changed = FALSE;
	gchar        buf[64];
	gfloat       t, grab_w;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (label != NULL && value != NULL, FALSE);

	id = gui_hash_label (self, label);
	gui_item_add (self, id, gui_item_width (self, 0.0f), gui_frame_height (self), &rect);
	gui_button_behavior (self, id, &rect, GUI_BUTTON_ON_PRESS, &hovered, &held);

	grab_w = MAX (8.0f, rect.h * 0.6f);
	if (held && max > min)
	{
		gfloat nv;

		t = (self->input.mouse_x - rect.x - grab_w * 0.5f) / MAX (1.0f, rect.w - grab_w);
		nv = min + CLAMP (t, 0.0f, 1.0f) * (max - min);
		if (nv != *value)
		{
			*value = nv;
			changed = TRUE;
		}
	}

	t = (max > min) ? CLAMP ((*value - min) / (max - min), 0.0f, 1.0f) : 0.0f;
	gui_draw_frame (self, &rect,
	                gui_col (self, held ? LRG_GUI_COL_FRAME_BG_ACTIVE
	                                    : (hovered ? LRG_GUI_COL_FRAME_BG_HOVERED : LRG_GUI_COL_FRAME_BG)),
	                FALSE);
	grab.x = rect.x + t * (rect.w - grab_w);
	grab.y = rect.y + 2.0f;
	grab.w = grab_w;
	grab.h = rect.h - 4.0f;
	lrg_gui_draw_list_rect_filled (gui_dl (self), &grab, gui_col (self, LRG_GUI_COL_SLIDER_GRAB),
	                               self->style.rounding);
	gui_format_value (buf, sizeof buf, format != NULL ? format : "%.3f", *value);
	gui_draw_text_clipped (self, &rect, buf, -1, gui_col (self, LRG_GUI_COL_TEXT), 0.5f, TRUE);

	return changed;
}

/* ==========================================================================
 * Combos
 * ========================================================================== */

gboolean
lrg_gui_begin_combo (LrgGuiContext *self,
                     const gchar   *label,
                     const gchar   *preview)
{
	LrgGuiId   id;
	LrgGuiRect rect;
	gboolean   hovered, held, pressed;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (label != NULL, FALSE);

	id = gui_hash_label (self, label);
	gui_item_add (self, id, gui_item_width (self, 0.0f), gui_frame_height (self), &rect);
	pressed = gui_button_behavior (self, id, &rect, GUI_BUTTON_ON_PRESS, &hovered, &held);

	if (pressed)
	{
		if (gui_is_popup_open_id (self, id) ||
		    g_hash_table_contains (self->popups_at_start, GUINT_TO_POINTER (id)))
			gui_close_popups_from (self, self->popup_level);
		else
		{
			gui_open_popup_id (self, id, rect.x, rect.y + rect.h, FALSE, FALSE);
			lrg_gui_set_next_popup_size (self, rect.w, 0.0f);
		}
	}

	gui_draw_frame (self, &rect,
	                gui_col (self, (hovered || held) ? LRG_GUI_COL_FRAME_BG_HOVERED : LRG_GUI_COL_FRAME_BG),
	                FALSE);
	{
		LrgGuiRect text = { rect.x + self->style.frame_padding_x, rect.y,
		                    rect.w - self->style.frame_padding_x * 2.0f - self->style.font_size, rect.h };

		if (preview != NULL)
			gui_draw_text_clipped (self, &text, preview, -1, gui_col (self, LRG_GUI_COL_TEXT), 0.0f, FALSE);
	}
	gui_draw_arrow (self, rect.x + rect.w - self->style.font_size * 0.7f, rect.y + rect.h * 0.5f,
	                self->style.font_size * 0.5f, TRUE, gui_col (self, LRG_GUI_COL_TEXT_DISABLED));

	if (!gui_is_popup_open_id (self, id))
		return FALSE;

	/* Long lists scroll. */
	if (!self->has_next_popup_size)
		lrg_gui_set_next_popup_size (self, rect.w, 0.0f);
	return gui_begin_popup_ex (self, id, FALSE);
}

void
lrg_gui_end_combo (LrgGuiContext *self)
{
	lrg_gui_end_popup (self);
}

gboolean
lrg_gui_combo (LrgGuiContext       *self,
               const gchar         *label,
               gint                *current,
               const gchar * const *items,
               gint                 n_items)
{
	gboolean changed = FALSE;
	gint     i;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (current != NULL, FALSE);
	g_return_val_if_fail (items != NULL || n_items == 0, FALSE);

	if (lrg_gui_begin_combo (self, label,
	                         (*current >= 0 && *current < n_items) ? items[*current] : ""))
	{
		for (i = 0; i < n_items; i++)
		{
			lrg_gui_push_id_int (self, i);
			if (lrg_gui_selectable (self, items[i], i == *current, LRG_GUI_SELECTABLE_NONE))
			{
				changed = (*current != i);
				*current = i;
			}
			lrg_gui_pop_id (self);
		}
		lrg_gui_end_combo (self);
	}

	return changed;
}

/* ==========================================================================
 * Colour editor
 * ========================================================================== */

static LrgGuiColor
rgba_to_color (const gfloat *rgba)
{
	return LRG_GUI_RGBA ((guint) (CLAMP (rgba[0], 0.0f, 1.0f) * 255.0f + 0.5f),
	                     (guint) (CLAMP (rgba[1], 0.0f, 1.0f) * 255.0f + 0.5f),
	                     (guint) (CLAMP (rgba[2], 0.0f, 1.0f) * 255.0f + 0.5f),
	                     (guint) (CLAMP (rgba[3], 0.0f, 1.0f) * 255.0f + 0.5f));
}

gboolean
lrg_gui_color_edit (LrgGuiContext *self,
                    const gchar   *label,
                    gfloat        *rgba,
                    gboolean       alpha)
{
	static const gchar *names[4] = { "R", "G", "B", "A" };
	static const guint32 palette[16] = {
		0xffffff, 0xbfbfbf, 0x7f7f7f, 0x000000, 0xf38ba8, 0xfab387, 0xf9e2af, 0xa6e3a1,
		0x94e2d5, 0x89dceb, 0x89b4fa, 0xb4befe, 0xcba6f7, 0xf5c2e7, 0x8b5a2b, 0x2e7d32
	};
	LrgGuiId   id;
	LrgGuiRect swatch;
	gfloat     total, sw, each;
	gint       n = alpha ? 4 : 3;
	gint       i;
	gboolean   changed = FALSE;
	gboolean   hovered, held;
	g_autofree gchar *popup_label = NULL;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (label != NULL && rgba != NULL, FALSE);

	id = gui_hash_label (self, label);
	total = gui_item_width (self, 0.0f);
	sw = gui_frame_height (self);
	each = floorf ((total - sw - self->style.item_spacing_x * n) / n);

	lrg_gui_push_id (self, label);

	/* Swatch: opens the palette popup. */
	gui_item_add (self, id, sw, sw, &swatch);
	if (gui_button_behavior (self, id, &swatch, GUI_BUTTON_ON_RELEASE, &hovered, &held))
		gui_open_popup_id (self, gui_hash_label (self, "##palette"), swatch.x, swatch.y + swatch.h,
		                   FALSE, FALSE);
	{
		gfloat     opaque[4] = { rgba[0], rgba[1], rgba[2], 1.0f };
		LrgGuiRect half = swatch;

		lrg_gui_draw_list_rect_filled (gui_dl (self), &swatch, rgba_to_color (opaque), self->style.rounding);
		if (alpha && rgba[3] < 1.0f)
		{
			half.x += half.w * 0.5f;
			half.w *= 0.5f;
			lrg_gui_draw_list_rect_filled (gui_dl (self), &half, rgba_to_color (rgba), 0.0f);
		}
		lrg_gui_draw_list_rect_outline (gui_dl (self), &swatch,
		                                gui_col (self, hovered ? LRG_GUI_COL_ACCENT : LRG_GUI_COL_BORDER),
		                                1.0f, self->style.rounding);
	}

	/* Component fields in 0..255. */
	for (i = 0; i < n; i++)
	{
		gfloat     v = rgba[i] * 255.0f;
		LrgGuiId   cid = gui_hash_label (self, names[i]);
		LrgGuiRect rect;

		lrg_gui_same_line (self, -1.0f);
		gui_item_add (self, cid, each, sw, &rect);
		if (drag_scalar (self, cid, &rect, names[i], &v, 1.0f, 0.0f, 255.0f, "%.0f", 0, TRUE))
		{
			rgba[i] = v / 255.0f;
			changed = TRUE;
		}
	}

	/* Palette + hex popup. */
	popup_label = g_strdup ("##palette");
	if (gui_begin_popup_ex (self, gui_hash_label (self, popup_label), FALSE))
	{
		g_autoptr(GString) hex = g_string_new (NULL);
		gfloat             cell = sw;

		for (i = 0; i < 16; i++)
		{
			LrgGuiRect c;
			LrgGuiId   pid = gui_hash_label (self, "##swatch") + (LrgGuiId) i;
			gboolean   ph;

			if (i % 8 != 0)
				lrg_gui_same_line (self, 2.0f);
			gui_item_add (self, pid, cell, cell, &c);
			if (gui_button_behavior (self, pid, &c, GUI_BUTTON_ON_RELEASE, &ph, NULL))
			{
				rgba[0] = ((palette[i] >> 16) & 0xff) / 255.0f;
				rgba[1] = ((palette[i] >> 8) & 0xff) / 255.0f;
				rgba[2] = (palette[i] & 0xff) / 255.0f;
				changed = TRUE;
			}
			lrg_gui_draw_list_rect_filled (gui_dl (self), &c, LRG_GUI_HEX (palette[i]), 2.0f);
			if (ph)
				lrg_gui_draw_list_rect_outline (gui_dl (self), &c, gui_col (self, LRG_GUI_COL_ACCENT),
				                                1.0f, 2.0f);
		}

		g_string_printf (hex, "#%02x%02x%02x", (guint) (rgba[0] * 255.0f + 0.5f),
		                 (guint) (rgba[1] * 255.0f + 0.5f), (guint) (rgba[2] * 255.0f + 0.5f));
		lrg_gui_set_next_item_width (self, cell * 8.0f + 14.0f);
		if (lrg_gui_input_text (self, "##hex", "#rrggbb", hex,
		                        LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE | LRG_GUI_INPUT_TEXT_MONOSPACE))
		{
			const gchar *h = hex->str[0] == '#' ? hex->str + 1 : hex->str;
			gchar       *endp = NULL;
			gulong       v = strtoul (h, &endp, 16);

			if (endp != h && strlen (h) == 6)
			{
				rgba[0] = ((v >> 16) & 0xff) / 255.0f;
				rgba[1] = ((v >> 8) & 0xff) / 255.0f;
				rgba[2] = (v & 0xff) / 255.0f;
				changed = TRUE;
			}
		}
		lrg_gui_end_popup (self);
	}

	lrg_gui_pop_id (self);
	self->item.id = id;
	return changed;
}

/* ==========================================================================
 * Progress and images
 * ========================================================================== */

void
lrg_gui_progress_bar (LrgGuiContext *self,
                      gfloat         fraction,
                      const gchar   *overlay)
{
	LrgGuiRect rect, fill;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	if (!gui_item_add (self, 0, gui_item_width (self, 0.0f), gui_frame_height (self), &rect))
		return;

	gui_draw_frame (self, &rect, gui_col (self, LRG_GUI_COL_FRAME_BG), FALSE);
	fill = rect;
	fill.w = rect.w * CLAMP (fraction, 0.0f, 1.0f);
	lrg_gui_draw_list_rect_filled (gui_dl (self), &fill, gui_col (self, LRG_GUI_COL_SLIDER_GRAB),
	                               self->style.rounding);
	if (overlay != NULL)
		gui_draw_text_clipped (self, &rect, overlay, -1, gui_col (self, LRG_GUI_COL_TEXT), 0.5f, FALSE);
}

void
lrg_gui_image (LrgGuiContext *self,
               GObject       *texture,
               gfloat         width,
               gfloat         height)
{
	LrgGuiRect rect;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (G_IS_OBJECT (texture));

	if (gui_item_add (self, 0, width, height, &rect))
		lrg_gui_draw_list_image (gui_dl (self), &rect, texture, LRG_GUI_HEX (0xffffff));
	gui_finish_item (self, 0, &rect);
}
