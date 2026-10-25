/* test-gui.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tests for the immediate-mode GUI toolkit: widgets are driven
 * with synthetic LrgGuiInput frames and checked through their return
 * values and the recorded draw commands.
 */

#include <glib.h>
#include <string.h>
#include <libregnum.h>

#define W (800.0f)
#define H (600.0f)

typedef struct
{
	LrgGuiContext *ctx;
	LrgGuiInput    input;
} Gui;

static void
gui_setup (Gui           *g,
           gconstpointer  data)
{
	g->ctx = lrg_gui_context_new ();
	lrg_gui_input_init (&g->input);
}

static void
gui_teardown (Gui           *g,
              gconstpointer  data)
{
	g_clear_object (&g->ctx);
}

/* Starts a frame at (x, y) with the given left-button state. */
static void
begin (Gui      *g,
       gfloat    x,
       gfloat    y,
       gboolean  down)
{
	lrg_gui_input_new_frame (&g->input, 1.0f / 60.0f);
	g->input.mouse_x = x;
	g->input.mouse_y = y;
	g->input.mouse_down[LRG_GUI_MOUSE_LEFT] = down;
	lrg_gui_context_begin_frame (g->ctx, &g->input, W, H);
}

static void
end (Gui *g)
{
	lrg_gui_context_end_frame (g->ctx);
}

static void
key (Gui             *g,
     LrgGuiKey        k,
     LrgGuiModifiers  mods)
{
	lrg_gui_input_add_key (&g->input, k, mods);
}

/* Releases held keys (the backend would do this). */
static void
release_keys (Gui *g)
{
	memset (g->input.key_down, 0, sizeof g->input.key_down);
	g->input.mods = 0;
}

static const LrgGuiRect panel = { 10.0f, 10.0f, 400.0f, 300.0f };

/* ==========================================================================
 * Buttons, labels, layout
 * ========================================================================== */

static void
test_button_click (Gui           *g,
                   gconstpointer  data)
{
	LrgGuiRect rect;
	gboolean   clicked;
	gfloat     cx, cy;

	/* Frame 1: find where the button is. */
	begin (g, -1, -1, FALSE);
	lrg_gui_begin_region (g->ctx, "p", &panel, LRG_GUI_REGION_NONE);
	g_assert_false (lrg_gui_button (g->ctx, "Save##btn"));
	lrg_gui_get_item_rect (g->ctx, &rect);
	lrg_gui_end_region (g->ctx);
	end (g);
	g_assert_nonnull (lrg_gui_draw_list_find_text (lrg_gui_context_get_draw_list (g->ctx), "Save"));
	g_assert_null (lrg_gui_draw_list_find_text (lrg_gui_context_get_draw_list (g->ctx), "Save##btn"));
	cx = rect.x + rect.w * 0.5f;
	cy = rect.y + rect.h * 0.5f;

	/* Press: no click yet. */
	begin (g, cx, cy, TRUE);
	lrg_gui_begin_region (g->ctx, "p", &panel, LRG_GUI_REGION_NONE);
	clicked = lrg_gui_button (g->ctx, "Save##btn");
	g_assert_true (lrg_gui_is_item_active (g->ctx));
	g_assert_true (lrg_gui_is_item_activated (g->ctx));
	lrg_gui_end_region (g->ctx);
	end (g);
	g_assert_false (clicked);
	g_assert_true (lrg_gui_context_wants_mouse (g->ctx));

	/* Release over it: click. */
	begin (g, cx, cy, FALSE);
	lrg_gui_begin_region (g->ctx, "p", &panel, LRG_GUI_REGION_NONE);
	clicked = lrg_gui_button (g->ctx, "Save##btn");
	g_assert_true (lrg_gui_is_item_deactivated (g->ctx));
	lrg_gui_end_region (g->ctx);
	end (g);
	g_assert_true (clicked);

	/* Press then release elsewhere: no click. */
	begin (g, cx, cy, TRUE);
	lrg_gui_begin_region (g->ctx, "p", &panel, LRG_GUI_REGION_NONE);
	lrg_gui_button (g->ctx, "Save##btn");
	lrg_gui_end_region (g->ctx);
	end (g);
	begin (g, 700, 500, FALSE);
	lrg_gui_begin_region (g->ctx, "p", &panel, LRG_GUI_REGION_NONE);
	clicked = lrg_gui_button (g->ctx, "Save##btn");
	lrg_gui_end_region (g->ctx);
	end (g);
	g_assert_false (clicked);
}

static void
test_layout_same_line_and_ids (Gui           *g,
                               gconstpointer  data)
{
	LrgGuiRect a, b, c;

	begin (g, -1, -1, FALSE);
	lrg_gui_begin_region (g->ctx, "p", &panel, LRG_GUI_REGION_NONE);
	lrg_gui_button (g->ctx, "A");
	lrg_gui_get_item_rect (g->ctx, &a);
	lrg_gui_same_line (g->ctx, -1);
	lrg_gui_button (g->ctx, "B");
	lrg_gui_get_item_rect (g->ctx, &b);
	lrg_gui_button (g->ctx, "C");
	lrg_gui_get_item_rect (g->ctx, &c);
	lrg_gui_end_region (g->ctx);
	end (g);

	g_assert_cmpfloat (b.x, >, a.x + a.w);
	g_assert_cmpfloat (b.y, ==, a.y);
	g_assert_cmpfloat (c.y, >, a.y);
	g_assert_cmpfloat (c.x, ==, a.x);

	/* "###" keys identity on the suffix only. */
	g_assert_cmpuint (lrg_gui_get_id (g->ctx, "Inspector (3)###insp"), ==,
	                  lrg_gui_get_id (g->ctx, "Inspector (9)###insp"));
	g_assert_cmpuint (lrg_gui_get_id (g->ctx, "a##1"), !=, lrg_gui_get_id (g->ctx, "a##2"));
}

static void
test_checkbox_and_toggle (Gui           *g,
                          gconstpointer  data)
{
	gboolean   value = FALSE;
	LrgGuiRect rect;
	gint       i;

	begin (g, -1, -1, FALSE);
	lrg_gui_begin_region (g->ctx, "p", &panel, LRG_GUI_REGION_NONE);
	lrg_gui_checkbox (g->ctx, "Visible", &value);
	lrg_gui_get_item_rect (g->ctx, &rect);
	lrg_gui_end_region (g->ctx);
	end (g);

	for (i = 0; i < 2; i++)
	{
		begin (g, rect.x + 4, rect.y + 4, i == 0);
		lrg_gui_begin_region (g->ctx, "p", &panel, LRG_GUI_REGION_NONE);
		lrg_gui_checkbox (g->ctx, "Visible", &value);
		lrg_gui_end_region (g->ctx);
		end (g);
	}
	g_assert_true (value);
}

static void
test_region_scroll_and_clip (Gui           *g,
                             gconstpointer  data)
{
	const LrgGuiRect small = { 0, 0, 200, 100 };
	LrgGuiRect       first;
	LrgGuiDrawList  *dl;
	guint            i, n, clips = 0;
	gint             frame;

	for (frame = 0; frame < 3; frame++)
	{
		lrg_gui_input_new_frame (&g->input, 0.016f);
		g->input.mouse_x = 50;
		g->input.mouse_y = 50;
		g->input.wheel_y = (frame == 1) ? -2.0f : 0.0f;
		lrg_gui_context_begin_frame (g->ctx, &g->input, W, H);
		lrg_gui_begin_region (g->ctx, "list", &small, LRG_GUI_REGION_SCROLL_Y);
		for (i = 0; i < 40; i++)
		{
			g_autofree gchar *t = g_strdup_printf ("row %u", i);

			lrg_gui_text (g->ctx, t);
			if (i == 0)
				lrg_gui_get_item_rect (g->ctx, &first);
		}
		lrg_gui_end_region (g->ctx);
		end (g);
	}

	/* The wheel scrolled the list (row 0 moved up out of view). */
	g_assert_cmpfloat (first.y, <, 0.0f);

	dl = lrg_gui_context_get_draw_list (g->ctx);
	n = lrg_gui_draw_list_get_length (dl);
	for (i = 0; i < n; i++)
		if (lrg_gui_draw_list_get_command (dl, i)->kind == LRG_GUI_CMD_CLIP_PUSH)
			clips++;
	g_assert_cmpuint (clips, >=, 1);
}

/* ==========================================================================
 * Text input
 * ========================================================================== */

typedef struct
{
	gboolean ret;
	LrgGuiRect rect;
} FieldResult;

static FieldResult
field_frame (Gui                  *g,
             GString              *buf,
             LrgGuiInputTextFlags  flags,
             gfloat               x,
             gfloat               y,
             gboolean             down)
{
	FieldResult r;

	lrg_gui_context_begin_frame (g->ctx, &g->input, W, H);
	lrg_gui_begin_region (g->ctx, "p", &panel, LRG_GUI_REGION_NONE);
	r.ret = lrg_gui_input_text (g->ctx, "##name", "hint", buf, flags);
	lrg_gui_get_item_rect (g->ctx, &r.rect);
	lrg_gui_end_region (g->ctx);
	lrg_gui_context_end_frame (g->ctx);
	(void) x; (void) y; (void) down;
	return r;
}

static void
prep (Gui      *g,
      gfloat    x,
      gfloat    y,
      gboolean  down)
{
	release_keys (g);
	lrg_gui_input_new_frame (&g->input, 0.016f);
	g->input.mouse_x = x;
	g->input.mouse_y = y;
	g->input.mouse_down[0] = down;
}

static void
test_input_text_editing (Gui           *g,
                         gconstpointer  data)
{
	g_autoptr(GString) buf = g_string_new ("hello");
	FieldResult        r;
	gfloat             cx, cy;

	prep (g, -1, -1, FALSE);
	r = field_frame (g, buf, LRG_GUI_INPUT_TEXT_NONE, 0, 0, FALSE);
	g_assert_nonnull (lrg_gui_draw_list_find_text (lrg_gui_context_get_draw_list (g->ctx), "hello"));
	cx = r.rect.x + r.rect.w - 2.0f;   /* click at the end */
	cy = r.rect.y + 4.0f;

	prep (g, cx, cy, TRUE);
	field_frame (g, buf, LRG_GUI_INPUT_TEXT_NONE, 0, 0, FALSE);
	g_assert_true (lrg_gui_context_wants_keyboard (g->ctx));
	prep (g, cx, cy, FALSE);
	field_frame (g, buf, LRG_GUI_INPUT_TEXT_NONE, 0, 0, FALSE);

	/* Type at the end. */
	prep (g, cx, cy, FALSE);
	lrg_gui_input_add_text (&g->input, " wörld");
	r = field_frame (g, buf, LRG_GUI_INPUT_TEXT_NONE, 0, 0, FALSE);
	g_assert_true (r.ret);
	g_assert_cmpstr (buf->str, ==, "hello wörld");

	/* Ctrl+Backspace deletes the last word, Backspace a character. */
	prep (g, cx, cy, FALSE);
	key (g, LRG_GUI_KEY_BACKSPACE, LRG_GUI_MOD_CTRL);
	field_frame (g, buf, LRG_GUI_INPUT_TEXT_NONE, 0, 0, FALSE);
	g_assert_cmpstr (buf->str, ==, "hello ");
	prep (g, cx, cy, FALSE);
	key (g, LRG_GUI_KEY_BACKSPACE, 0);
	field_frame (g, buf, LRG_GUI_INPUT_TEXT_NONE, 0, 0, FALSE);
	g_assert_cmpstr (buf->str, ==, "hello");

	/* Home, shift+End selects all; typing replaces. */
	prep (g, cx, cy, FALSE);
	key (g, LRG_GUI_KEY_HOME, 0);
	key (g, LRG_GUI_KEY_END, LRG_GUI_MOD_SHIFT);
	lrg_gui_input_add_text (&g->input, "X");
	field_frame (g, buf, LRG_GUI_INPUT_TEXT_NONE, 0, 0, FALSE);
	g_assert_cmpstr (buf->str, ==, "X");

	/* Copy/paste through the context clipboard. */
	prep (g, cx, cy, FALSE);
	key (g, LRG_GUI_KEY_A, LRG_GUI_MOD_CTRL);
	key (g, LRG_GUI_KEY_C, LRG_GUI_MOD_CTRL);
	key (g, LRG_GUI_KEY_END, 0);
	key (g, LRG_GUI_KEY_V, LRG_GUI_MOD_CTRL);
	field_frame (g, buf, LRG_GUI_INPUT_TEXT_NONE, 0, 0, FALSE);
	g_assert_cmpstr (buf->str, ==, "XX");

	/* Escape reverts to the text at focus time. */
	prep (g, cx, cy, FALSE);
	key (g, LRG_GUI_KEY_ESCAPE, 0);
	field_frame (g, buf, LRG_GUI_INPUT_TEXT_NONE, 0, 0, FALSE);
	g_assert_cmpstr (buf->str, ==, "hello");
	g_assert_false (lrg_gui_context_wants_keyboard (g->ctx));
}

static void
test_input_text_enter_mode (Gui           *g,
                            gconstpointer  data)
{
	g_autoptr(GString) buf = g_string_new ("");
	FieldResult        r;
	gfloat             cx, cy;

	prep (g, -1, -1, FALSE);
	r = field_frame (g, buf, LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE, 0, 0, FALSE);
	g_assert_nonnull (lrg_gui_draw_list_find_text (lrg_gui_context_get_draw_list (g->ctx), "hint"));
	cx = r.rect.x + 5;
	cy = r.rect.y + 5;

	prep (g, cx, cy, TRUE);
	field_frame (g, buf, LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE, 0, 0, FALSE);
	prep (g, cx, cy, FALSE);
	lrg_gui_input_add_text (&g->input, "Crate");
	r = field_frame (g, buf, LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE, 0, 0, FALSE);

	/* Nothing is reported (or copied) until Enter. */
	g_assert_false (r.ret);
	g_assert_cmpstr (buf->str, ==, "");

	prep (g, cx, cy, FALSE);
	key (g, LRG_GUI_KEY_ENTER, 0);
	r = field_frame (g, buf, LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE, 0, 0, FALSE);
	g_assert_true (r.ret);
	g_assert_cmpstr (buf->str, ==, "Crate");
	g_assert_false (lrg_gui_context_wants_keyboard (g->ctx));
}

/* The caller rebuilds its buffer each frame (immediate mode): editing must
 * still accumulate. */
static void
test_input_text_rebuilt_buffer (Gui           *g,
                                gconstpointer  data)
{
	g_autofree gchar *model = g_strdup ("a");
	FieldResult       r;
	gint              i;
	gfloat            cx, cy;

	prep (g, -1, -1, FALSE);
	{
		g_autoptr(GString) b = g_string_new (model);

		r = field_frame (g, b, LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE, 0, 0, FALSE);
	}
	cx = r.rect.x + r.rect.w - 2;
	cy = r.rect.y + 4;

	for (i = 0; i < 5; i++)
	{
		g_autoptr(GString) b = g_string_new (model);

		prep (g, cx, cy, i == 0);
		if (i == 2)
			lrg_gui_input_add_text (&g->input, "bc");
		if (i == 4)
			key (g, LRG_GUI_KEY_ENTER, 0);
		r = field_frame (g, b, LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE, 0, 0, FALSE);
		if (r.ret)
		{
			g_free (model);
			model = g_strdup (b->str);
		}
	}
	g_assert_cmpstr (model, ==, "abc");
}

/* ==========================================================================
 * Number fields
 * ========================================================================== */

static gboolean
drag_frame (Gui      *g,
            gfloat   *v,
            gfloat    x,
            gfloat    y,
            gboolean  down,
            LrgGuiRect *rect)
{
	gboolean changed;

	lrg_gui_context_begin_frame (g->ctx, &g->input, W, H);
	lrg_gui_begin_region (g->ctx, "p", &panel, LRG_GUI_REGION_NONE);
	changed = lrg_gui_drag_float (g->ctx, "##v", v, 0.5f, 0.0f, 0.0f, "%.2f");
	if (rect != NULL)
		lrg_gui_get_item_rect (g->ctx, rect);
	lrg_gui_end_region (g->ctx);
	lrg_gui_context_end_frame (g->ctx);
	(void) x; (void) y; (void) down;
	return changed;
}

static void
test_drag_float (Gui           *g,
                 gconstpointer  data)
{
	gfloat     v = 1.0f;
	LrgGuiRect rect;
	gfloat     cx, cy;

	prep (g, -1, -1, FALSE);
	drag_frame (g, &v, 0, 0, FALSE, &rect);
	g_assert_nonnull (lrg_gui_draw_list_find_text (lrg_gui_context_get_draw_list (g->ctx), "1.00"));
	cx = rect.x + rect.w * 0.5f;
	cy = rect.y + rect.h * 0.5f;

	prep (g, cx, cy, TRUE);
	drag_frame (g, &v, 0, 0, TRUE, NULL);
	prep (g, cx + 10, cy, TRUE);
	g_assert_true (drag_frame (g, &v, 0, 0, TRUE, NULL));
	g_assert_cmpfloat_with_epsilon (v, 6.0f, 1e-4);

	/* Shift multiplies the speed (recomputed from the press value). */
	prep (g, cx + 10, cy, TRUE);
	g->input.mods = LRG_GUI_MOD_SHIFT;
	drag_frame (g, &v, 0, 0, TRUE, NULL);
	g_assert_cmpfloat_with_epsilon (v, 51.0f, 1e-3);
	prep (g, cx + 10, cy, FALSE);
	drag_frame (g, &v, 0, 0, FALSE, NULL);

	/* Double-click: type a value. */
	g->input.time += 1.0;
	prep (g, cx, cy, TRUE);
	drag_frame (g, &v, 0, 0, TRUE, NULL);
	prep (g, cx, cy, FALSE);
	drag_frame (g, &v, 0, 0, FALSE, NULL);
	prep (g, cx, cy, TRUE);
	drag_frame (g, &v, 0, 0, TRUE, NULL);
	g_assert_true (lrg_gui_context_wants_keyboard (g->ctx));
	prep (g, cx, cy, FALSE);
	lrg_gui_input_add_text (&g->input, "-2.5");
	drag_frame (g, &v, 0, 0, FALSE, NULL);
	prep (g, cx, cy, FALSE);
	key (g, LRG_GUI_KEY_ENTER, 0);
	g_assert_true (drag_frame (g, &v, 0, 0, FALSE, NULL));
	g_assert_cmpfloat_with_epsilon (v, -2.5f, 1e-5);
}

static void
test_format_value (Gui           *g,
                   gconstpointer  data)
{
	gfloat           v = 12.5f;
	LrgGuiRect       r;

	prep (g, -1, -1, FALSE);
	lrg_gui_context_begin_frame (g->ctx, &g->input, W, H);
	lrg_gui_begin_region (g->ctx, "p", &panel, LRG_GUI_REGION_NONE);
	lrg_gui_drag_float (g->ctx, "##a", &v, 1.0f, 0, 0, "%.1f deg");
	lrg_gui_drag_float (g->ctx, "##b", &v, 1.0f, 0, 0, "%d%%");
	lrg_gui_get_item_rect (g->ctx, &r);
	lrg_gui_end_region (g->ctx);
	lrg_gui_context_end_frame (g->ctx);

	g_assert_nonnull (lrg_gui_draw_list_find_text (lrg_gui_context_get_draw_list (g->ctx), "12.5 deg"));
	g_assert_nonnull (lrg_gui_draw_list_find_text (lrg_gui_context_get_draw_list (g->ctx), "12%"));
}

/* ==========================================================================
 * Trees, popups, menus, modals, combos, tabs
 * ========================================================================== */

static gboolean
tree_frame (Gui             *g,
            LrgGuiTreeFlags  flags,
            LrgGuiRect      *rect,
            gboolean        *clicked)
{
	gboolean open;

	lrg_gui_context_begin_frame (g->ctx, &g->input, W, H);
	lrg_gui_begin_region (g->ctx, "p", &panel, LRG_GUI_REGION_NONE);
	open = lrg_gui_tree_node (g->ctx, "Parent", flags);
	if (rect != NULL)
		lrg_gui_get_item_rect (g->ctx, rect);
	if (clicked != NULL)
		*clicked = lrg_gui_is_item_clicked (g->ctx, LRG_GUI_MOUSE_LEFT);
	if (open)
	{
		lrg_gui_text (g->ctx, "Child");
		lrg_gui_tree_pop (g->ctx);
	}
	lrg_gui_end_region (g->ctx);
	lrg_gui_context_end_frame (g->ctx);
	return open;
}

static void
test_tree_node (Gui           *g,
                gconstpointer  data)
{
	LrgGuiRect rect;
	gboolean   clicked;

	prep (g, -1, -1, FALSE);
	g_assert_false (tree_frame (g, LRG_GUI_TREE_OPEN_ON_ARROW, &rect, NULL));

	/* Clicking the label of an OPEN_ON_ARROW row selects, does not open. */
	prep (g, rect.x + rect.w * 0.6f, rect.y + 4, TRUE);
	g_assert_false (tree_frame (g, LRG_GUI_TREE_OPEN_ON_ARROW, NULL, &clicked));
	g_assert_true (clicked);
	prep (g, rect.x + rect.w * 0.6f, rect.y + 4, FALSE);
	tree_frame (g, LRG_GUI_TREE_OPEN_ON_ARROW, NULL, NULL);

	/* The arrow opens it; children appear indented. */
	g->input.time += 1.0;
	prep (g, rect.x + 4, rect.y + 4, TRUE);
	g_assert_true (tree_frame (g, LRG_GUI_TREE_OPEN_ON_ARROW, NULL, NULL));
	g_assert_nonnull (lrg_gui_draw_list_find_text (lrg_gui_context_get_draw_list (g->ctx), "Child"));
	prep (g, rect.x + 4, rect.y + 4, FALSE);
	g_assert_true (tree_frame (g, LRG_GUI_TREE_OPEN_ON_ARROW, NULL, NULL));

	/* Keyboard Left closes the hovered row. */
	prep (g, rect.x + 50, rect.y + 4, FALSE);
	key (g, LRG_GUI_KEY_LEFT, 0);
	g_assert_false (tree_frame (g, LRG_GUI_TREE_OPEN_ON_ARROW, NULL, NULL));
}

static gint
menu_frame (Gui        *g,
            LrgGuiRect *file_rect,
            LrgGuiRect *item_rect,
            gboolean   *menu_open)
{
	const LrgGuiRect bar = { 0, 0, W, 24 };
	gint             activated = 0;

	lrg_gui_context_begin_frame (g->ctx, &g->input, W, H);
	lrg_gui_begin_menu_bar (g->ctx, &bar);
	*menu_open = lrg_gui_begin_menu (g->ctx, "File", TRUE);
	if (file_rect != NULL)
	{
		/* The header is the item before the popup region opened. */
	}
	if (*menu_open)
	{
		if (lrg_gui_menu_item (g->ctx, "Save", "Ctrl+S", FALSE, TRUE))
			activated = 1;
		if (item_rect != NULL)
			lrg_gui_get_item_rect (g->ctx, item_rect);
		if (lrg_gui_menu_item (g->ctx, "Disabled", NULL, FALSE, FALSE))
			activated = 2;
		lrg_gui_end_menu (g->ctx);
	}
	lrg_gui_end_menu_bar (g->ctx);
	lrg_gui_context_end_frame (g->ctx);
	return activated;
}

static void
test_menu_bar (Gui           *g,
               gconstpointer  data)
{
	LrgGuiRect item = { 0 };
	gboolean   open;
	gfloat     fx = 20, fy = 12;   /* "File" is the first header */

	prep (g, -1, -1, FALSE);
	menu_frame (g, NULL, NULL, &open);
	g_assert_false (open);

	/* Press on File opens the menu (on press). */
	prep (g, fx, fy, TRUE);
	menu_frame (g, NULL, &item, &open);
	g_assert_true (open);
	prep (g, fx, fy, FALSE);
	menu_frame (g, NULL, &item, &open);
	g_assert_true (open);
	g_assert_nonnull (lrg_gui_draw_list_find_text (lrg_gui_context_get_draw_list (g->ctx), "Ctrl+S"));

	/* Click Save: activated, menu closes. */
	prep (g, item.x + 10, item.y + 5, TRUE);
	g_assert_cmpint (menu_frame (g, NULL, NULL, &open), ==, 0);
	prep (g, item.x + 10, item.y + 5, FALSE);
	g_assert_cmpint (menu_frame (g, NULL, NULL, &open), ==, 1);
	prep (g, item.x + 10, item.y + 5, FALSE);
	menu_frame (g, NULL, NULL, &open);
	g_assert_false (open);

	/* Reopen, then click the header again: closes. */
	prep (g, fx, fy, TRUE);
	menu_frame (g, NULL, NULL, &open);
	prep (g, fx, fy, FALSE);
	menu_frame (g, NULL, NULL, &open);
	g_assert_true (open);
	prep (g, fx, fy, TRUE);
	menu_frame (g, NULL, NULL, &open);
	g_assert_false (open);
	prep (g, fx, fy, FALSE);
	menu_frame (g, NULL, NULL, &open);
	g_assert_false (open);

	/* Escape closes an open menu. */
	prep (g, fx, fy, TRUE);
	menu_frame (g, NULL, NULL, &open);
	prep (g, 500, 500, FALSE);
	key (g, LRG_GUI_KEY_ESCAPE, 0);
	menu_frame (g, NULL, NULL, &open);
	prep (g, 500, 500, FALSE);
	menu_frame (g, NULL, NULL, &open);
	g_assert_false (open);
}

static gboolean
modal_frame (Gui        *g,
             gboolean    open_now,
             gboolean   *modal_open,
             LrgGuiRect *base_btn)
{
	gboolean clicked;
	gboolean keep = TRUE;

	lrg_gui_context_begin_frame (g->ctx, &g->input, W, H);
	lrg_gui_begin_region (g->ctx, "p", &panel, LRG_GUI_REGION_NONE);
	clicked = lrg_gui_button (g->ctx, "Base");
	if (base_btn != NULL)
		lrg_gui_get_item_rect (g->ctx, base_btn);
	if (open_now)
		lrg_gui_open_popup (g->ctx, "Confirm");
	*modal_open = lrg_gui_begin_popup_modal (g->ctx, "Confirm", &keep);
	if (*modal_open)
	{
		lrg_gui_text (g->ctx, "Really?");
		lrg_gui_end_popup (g->ctx);
	}
	lrg_gui_end_region (g->ctx);
	lrg_gui_context_end_frame (g->ctx);
	return clicked;
}

static void
test_modal_blocks_base (Gui           *g,
                        gconstpointer  data)
{
	LrgGuiRect btn;
	gboolean   open;

	prep (g, -1, -1, FALSE);
	modal_frame (g, TRUE, &open, &btn);
	g_assert_true (open);

	/* Clicking the base button under the modal does nothing and does not
	 * close the modal. */
	prep (g, btn.x + 3, btn.y + 3, TRUE);
	g_assert_false (modal_frame (g, FALSE, &open, NULL));
	prep (g, btn.x + 3, btn.y + 3, FALSE);
	g_assert_false (modal_frame (g, FALSE, &open, NULL));
	g_assert_true (open);

	/* Escape closes it. */
	prep (g, -1, -1, FALSE);
	key (g, LRG_GUI_KEY_ESCAPE, 0);
	modal_frame (g, FALSE, &open, NULL);
	prep (g, -1, -1, FALSE);
	modal_frame (g, FALSE, &open, NULL);
	g_assert_false (open);
}

static gboolean
combo_frame (Gui        *g,
             gint       *current,
             LrgGuiRect *rect)
{
	static const gchar *items[] = { "Cube", "Sphere", "Plane" };
	gboolean            changed;

	lrg_gui_context_begin_frame (g->ctx, &g->input, W, H);
	lrg_gui_begin_region (g->ctx, "p", &panel, LRG_GUI_REGION_NONE);
	lrg_gui_set_next_item_width (g->ctx, 150);
	changed = lrg_gui_combo (g->ctx, "Primitive", current, items, 3);
	if (rect != NULL)
		lrg_gui_get_item_rect (g->ctx, rect);
	lrg_gui_end_region (g->ctx);
	lrg_gui_context_end_frame (g->ctx);
	return changed;
}

static void
test_combo (Gui           *g,
            gconstpointer  data)
{
	gint       current = 0;
	LrgGuiRect rect;
	gfloat     row_h;

	prep (g, -1, -1, FALSE);
	combo_frame (g, &current, &rect);
	row_h = lrg_gui_get_frame_height (g->ctx);

	prep (g, rect.x + 5, rect.y + 5, TRUE);
	combo_frame (g, &current, NULL);
	prep (g, rect.x + 5, rect.y + 5, FALSE);
	combo_frame (g, &current, NULL);
	g_assert_nonnull (lrg_gui_draw_list_find_text (lrg_gui_context_get_draw_list (g->ctx), "Plane"));

	/* Third row of the list (below the combo). */
	{
		gfloat y = rect.y + rect.h + lrg_gui_context_get_style (g->ctx)->region_padding +
		           (row_h + lrg_gui_context_get_style (g->ctx)->item_spacing_y) * 2 + row_h * 0.5f;

		prep (g, rect.x + 20, y, TRUE);
		combo_frame (g, &current, NULL);
		prep (g, rect.x + 20, y, FALSE);
		g_assert_true (combo_frame (g, &current, NULL));
	}
	g_assert_cmpint (current, ==, 2);

	/* The list closed. */
	prep (g, -1, -1, FALSE);
	combo_frame (g, &current, NULL);
	g_assert_null (lrg_gui_draw_list_find_text (lrg_gui_context_get_draw_list (g->ctx), "Sphere"));
}

static gint
tabs_frame (Gui        *g,
            LrgGuiRect *second)
{
	gint shown = -1;

	lrg_gui_context_begin_frame (g->ctx, &g->input, W, H);
	lrg_gui_begin_region (g->ctx, "p", &panel, LRG_GUI_REGION_NONE);
	lrg_gui_begin_tab_bar (g->ctx, "tabs");
	if (lrg_gui_tab_item (g->ctx, "General", NULL))
		shown = 0;
	if (lrg_gui_tab_item (g->ctx, "Advanced", NULL))
		shown = 1;
	if (second != NULL)
		lrg_gui_get_item_rect (g->ctx, second);
	lrg_gui_end_tab_bar (g->ctx);
	lrg_gui_end_region (g->ctx);
	lrg_gui_context_end_frame (g->ctx);
	return shown;
}

static void
test_tabs (Gui           *g,
           gconstpointer  data)
{
	LrgGuiRect second;

	prep (g, -1, -1, FALSE);
	g_assert_cmpint (tabs_frame (g, &second), ==, 0);
	prep (g, second.x + 5, second.y + 5, TRUE);
	g_assert_cmpint (tabs_frame (g, NULL), ==, 1);
	prep (g, -1, -1, FALSE);
	g_assert_cmpint (tabs_frame (g, NULL), ==, 1);
}

/* ==========================================================================
 * Drag and drop
 * ========================================================================== */

static const gchar *
dnd_frame (Gui        *g,
           LrgGuiRect *src,
           LrgGuiRect *dst)
{
	const gchar *dropped = NULL;

	lrg_gui_context_begin_frame (g->ctx, &g->input, W, H);
	lrg_gui_begin_region (g->ctx, "p", &panel, LRG_GUI_REGION_NONE);
	lrg_gui_selectable (g->ctx, "Source", FALSE, 0);
	if (src != NULL)
		lrg_gui_get_item_rect (g->ctx, src);
	if (lrg_gui_begin_drag_source (g->ctx))
	{
		lrg_gui_set_drag_payload (g->ctx, "node", "guid-123", "Source");
		lrg_gui_end_drag_source (g->ctx);
	}
	lrg_gui_selectable (g->ctx, "Target", FALSE, 0);
	if (dst != NULL)
		lrg_gui_get_item_rect (g->ctx, dst);
	dropped = lrg_gui_accept_drop (g->ctx, "node");
	lrg_gui_end_region (g->ctx);
	lrg_gui_context_end_frame (g->ctx);
	return dropped;
}

static void
test_drag_and_drop (Gui           *g,
                    gconstpointer  data)
{
	LrgGuiRect src, dst;

	prep (g, -1, -1, FALSE);
	dnd_frame (g, &src, &dst);

	prep (g, src.x + 5, src.y + 5, TRUE);
	g_assert_null (dnd_frame (g, NULL, NULL));
	prep (g, src.x + 5, dst.y + 5, TRUE);
	g_assert_null (dnd_frame (g, NULL, NULL));
	g_assert_nonnull (lrg_gui_get_drag_payload (g->ctx, NULL));
	/* The preview is drawn. */
	g_assert_nonnull (lrg_gui_draw_list_find_text (lrg_gui_context_get_draw_list (g->ctx), "Source"));

	prep (g, src.x + 5, dst.y + 5, FALSE);
	g_assert_cmpstr (dnd_frame (g, NULL, NULL), ==, "guid-123");
	prep (g, -1, -1, FALSE);
	dnd_frame (g, NULL, NULL);
	g_assert_null (lrg_gui_get_drag_payload (g->ctx, NULL));
}

/* ==========================================================================
 * Dock
 * ========================================================================== */

static void
dock_frame (Gui        *g,
            LrgGuiDock *dock,
            gboolean   *visible)
{
	static const gchar *panels[] = { "hierarchy", "viewport", "inspector", "console" };
	const LrgGuiRect    area = { 0, 0, W, H };
	gint                i;

	lrg_gui_context_begin_frame (g->ctx, &g->input, W, H);
	lrg_gui_dock_begin (dock, g->ctx, &area);
	for (i = 0; i < 4; i++)
	{
		visible[i] = lrg_gui_dock_begin_panel (dock, g->ctx, panels[i], panels[i]);
		if (visible[i])
		{
			lrg_gui_text (g->ctx, panels[i]);
			lrg_gui_dock_end_panel (dock, g->ctx);
		}
	}
	lrg_gui_dock_end (dock, g->ctx);
	lrg_gui_context_end_frame (g->ctx);
}

static void
test_dock_layout (Gui           *g,
                  gconstpointer  data)
{
	g_autoptr(LrgGuiDock) dock = lrg_gui_dock_new ();
	g_autoptr(LrgGuiDock) other = lrg_gui_dock_new ();
	g_autoptr(GError)     error = NULL;
	g_autofree gchar     *s = NULL;
	g_autofree gchar     *s2 = NULL;
	gboolean              vis[4];
	LrgGuiRect            hr, vr;

	lrg_gui_dock_dock_panel (dock, "viewport", NULL, LRG_GUI_DOCK_CENTER, 0);
	lrg_gui_dock_dock_panel (dock, "hierarchy", "viewport", LRG_GUI_DOCK_LEFT, 0.25f);
	lrg_gui_dock_dock_panel (dock, "inspector", "viewport", LRG_GUI_DOCK_RIGHT, 0.3f);
	lrg_gui_dock_dock_panel (dock, "console", "viewport", LRG_GUI_DOCK_CENTER, 0);

	s = lrg_gui_dock_to_string (dock);
	g_assert_true (lrg_gui_dock_load_string (other, s, &error));
	g_assert_no_error (error);
	s2 = lrg_gui_dock_to_string (other);
	g_assert_cmpstr (s, ==, s2);

	/* console joined the viewport stack and is active. */
	prep (g, -1, -1, FALSE);
	dock_frame (g, dock, vis);
	g_assert_true (vis[0]);
	g_assert_false (vis[1]);
	g_assert_true (vis[2]);
	g_assert_true (vis[3]);
	g_assert_true (lrg_gui_dock_get_panel_rect (dock, "hierarchy", &hr));
	g_assert_cmpfloat_with_epsilon (hr.w, (W - 8) * 0.25f, 2.0f);

	lrg_gui_dock_open_panel (dock, "viewport");
	prep (g, -1, -1, FALSE);
	dock_frame (g, dock, vis);
	g_assert_true (vis[1]);
	g_assert_false (vis[3]);
	g_assert_true (lrg_gui_dock_get_panel_rect (dock, "viewport", &vr));
	g_assert_cmpfloat (vr.x, >, hr.x + hr.w);

	/* Closing the hierarchy collapses its pane. */
	lrg_gui_dock_close_panel (dock, "hierarchy");
	g_assert_false (lrg_gui_dock_has_panel (dock, "hierarchy"));
	prep (g, -1, -1, FALSE);
	dock_frame (g, dock, vis);
	g_assert_true (lrg_gui_dock_get_panel_rect (dock, "viewport", &vr));
	g_assert_cmpfloat (vr.x, ==, 0.0f);

	/* Bad strings are rejected without touching the layout. */
	g_assert_false (lrg_gui_dock_load_string (other, "split(x,0.5,tabs(0),tabs(0))", &error));
	g_clear_error (&error);
	g_assert_false (lrg_gui_dock_load_string (other, "tabs(0,a", &error));
	g_clear_error (&error);
	g_free (s2);
	s2 = lrg_gui_dock_to_string (other);
	g_assert_cmpstr (s, ==, s2);
}

static void
test_dock_splitter_drag (Gui           *g,
                         gconstpointer  data)
{
	g_autoptr(LrgGuiDock) dock = lrg_gui_dock_new ();
	gboolean              vis[4];
	LrgGuiRect            hr, hr2;
	gfloat                sx;

	lrg_gui_dock_dock_panel (dock, "viewport", NULL, LRG_GUI_DOCK_CENTER, 0);
	lrg_gui_dock_dock_panel (dock, "hierarchy", "viewport", LRG_GUI_DOCK_LEFT, 0.25f);
	prep (g, -1, -1, FALSE);
	dock_frame (g, dock, vis);
	lrg_gui_dock_get_panel_rect (dock, "hierarchy", &hr);
	sx = hr.x + hr.w + 2.0f;

	prep (g, sx, 300, TRUE);
	dock_frame (g, dock, vis);
	g_assert_cmpint (lrg_gui_context_get_cursor (g->ctx), ==, LRG_GUI_CURSOR_RESIZE_EW);
	prep (g, sx + 100, 300, TRUE);
	dock_frame (g, dock, vis);
	prep (g, sx + 100, 300, FALSE);
	dock_frame (g, dock, vis);
	lrg_gui_dock_get_panel_rect (dock, "hierarchy", &hr2);
	g_assert_cmpfloat_with_epsilon (hr2.w, hr.w + 100.0f, 2.0f);
}

/* 10 px per character, so the wrap width below is exactly 10 characters. */
static gfloat
ten_px_measure (const gchar *text,
                gssize       length,
                gfloat       font_size,
                gboolean     monospace,
                gpointer     user_data)
{
	return 10.0f * (gfloat) ((length < 0) ? g_utf8_strlen (text, -1) : g_utf8_strlen (text, length));
}

static void
test_text_wrapped (Gui           *g,
                   gconstpointer  data)
{
	const LrgGuiRect  narrow = { 0.0f, 0.0f, 100.0f, 400.0f };
	LrgGuiDrawList   *dl;
	const gchar      *expected[] = { "aaaa bbbb", "cccc", "dd", "eeeeeeeeee", "eeee", "", "f" };
	guint             i, n_text = 0;

	/* Breaks at the last space before the first character that does not
	 * fit, splits words longer than a line, and keeps explicit newlines
	 * (including an empty line). */
	lrg_gui_context_set_measure_func (g->ctx, ten_px_measure, NULL, NULL);
	begin (g, -1, -1, FALSE);
	lrg_gui_begin_region (g->ctx, "p", &narrow, LRG_GUI_REGION_NO_PADDING);
	lrg_gui_text_wrapped (g->ctx, "aaaa bbbb cccc\ndd eeeeeeeeeeeeee\n\nf");
	lrg_gui_end_region (g->ctx);
	end (g);

	dl = lrg_gui_context_get_draw_list (g->ctx);
	for (i = 0; i < lrg_gui_draw_list_get_length (dl); i++)
	{
		const LrgGuiCommand *cmd = lrg_gui_draw_list_get_command (dl, i);

		if (cmd->kind != LRG_GUI_CMD_TEXT)
			continue;
		/* Empty chunks record no text command. */
		while (n_text < G_N_ELEMENTS (expected) && *expected[n_text] == '\0')
			n_text++;
		g_assert_cmpuint (n_text, <, G_N_ELEMENTS (expected));
		g_assert_cmpstr (cmd->text, ==, expected[n_text]);
		n_text++;
	}
	g_assert_cmpuint (n_text, ==, G_N_ELEMENTS (expected));
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

#define ADD(path, fn) g_test_add (path, Gui, NULL, gui_setup, fn, gui_teardown)
	ADD ("/gui/button/click", test_button_click);
	ADD ("/gui/layout/same-line-ids", test_layout_same_line_and_ids);
	ADD ("/gui/checkbox", test_checkbox_and_toggle);
	ADD ("/gui/region/scroll-clip", test_region_scroll_and_clip);
	ADD ("/gui/input-text/editing", test_input_text_editing);
	ADD ("/gui/input-text/enter-mode", test_input_text_enter_mode);
	ADD ("/gui/input-text/rebuilt-buffer", test_input_text_rebuilt_buffer);
	ADD ("/gui/drag-float", test_drag_float);
	ADD ("/gui/format-value", test_format_value);
	ADD ("/gui/tree-node", test_tree_node);
	ADD ("/gui/menu-bar", test_menu_bar);
	ADD ("/gui/modal", test_modal_blocks_base);
	ADD ("/gui/combo", test_combo);
	ADD ("/gui/tabs", test_tabs);
	ADD ("/gui/drag-and-drop", test_drag_and_drop);
	ADD ("/gui/dock/layout", test_dock_layout);
	ADD ("/gui/dock/splitter", test_dock_splitter_drag);
	ADD ("/gui/text/wrapped", test_text_wrapped);
#undef ADD

	return g_test_run ();
}
