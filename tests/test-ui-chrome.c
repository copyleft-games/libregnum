/* test-ui-chrome.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Window chrome: LrgPagination, LrgHeaderBar and LrgNotification.
 *
 * Headless: widgets sit on a canvas, are driven through
 * lrg_canvas_process_input() like the real input loop and stepped at
 * 60 Hz, so animations are deterministic. The header bar's window
 * operations go to a fake window through lrg_header_bar_set_window_funcs().
 * Notifications run against a private bus (GTestDBus) with a fake
 * org.freedesktop.Notifications server implemented here, never the
 * user's session bus.
 */

#include <glib.h>
#include <gio/gio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>

#define DT (1.0f / 60.0f)

/* Set in main() when a hidden window (GL context) could be opened */
static GrlWindow *test_window = NULL;
static gboolean   graphics_available = FALSE;

/* ==========================================================================
 * Canvas and input helpers
 * ========================================================================== */

static LrgCanvas *
canvas_new (void)
{
	LrgCanvas *canvas = lrg_canvas_new ();

	lrg_widget_set_size (LRG_WIDGET (canvas), 800.0f, 600.0f);
	lrg_canvas_set_paint_background (canvas, FALSE);
	return canvas;
}

/* place: add @widget to the canvas at a fixed rectangle (canvas owns it) */
static gpointer
place (LrgCanvas *canvas,
       gpointer   widget,
       gfloat     x,
       gfloat     y,
       gfloat     w,
       gfloat     h)
{
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (widget));
	g_object_unref (widget);
	lrg_widget_allocate (LRG_WIDGET (widget), x, y, w, h);
	lrg_canvas_record (canvas);
	return widget;
}

static void
frame_pointer (LrgCanvasInput *input,
               gfloat          x,
               gfloat          y)
{
	lrg_canvas_input_init (input);
	input->pointer_x = x;
	input->pointer_y = y;
	input->delta_time = DT;
}

static void
move_to (LrgCanvas *canvas,
         gfloat     x,
         gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	lrg_canvas_process_input (canvas, &input);
}

static void
press_at (LrgCanvas *canvas,
          gfloat     x,
          gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	input.buttons_pressed = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

static void
drag_to (LrgCanvas *canvas,
         gfloat     x,
         gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

static void
release_at (LrgCanvas *canvas,
            gfloat     x,
            gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.buttons_released = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

static void
click_at (LrgCanvas *canvas,
          gfloat     x,
          gfloat     y)
{
	move_to (canvas, x, y);
	press_at (canvas, x, y);
	release_at (canvas, x, y);
}

/* key: one frame with a key press */
static void
key (LrgCanvas *canvas,
     GrlKey     k)
{
	LrgCanvasInput input;

	frame_pointer (&input, -10.0f, -10.0f);
	lrg_canvas_input_add_key (&input, k);
	lrg_canvas_process_input (canvas, &input);
}

/* frames: advance the canvas clock without input */
static void
frames (LrgCanvas *canvas,
        guint      n)
{
	guint i;

	for (i = 0; i < n; i++)
		lrg_canvas_update (canvas, DT);
}

/* count_cb: generic counter for signals without arguments we care about */
static void
count_cb (gpointer  instance,
          gint     *count)
{
	(*count)++;
}

/* ==========================================================================
 * LrgPagination: pure item computation
 * ========================================================================== */

#define G (LRG_PAGINATION_GAP)
#define END (-100)

typedef struct
{
	guint n_pages;
	guint page;
	guint siblings;
	guint boundary;
	gint  expect[16];   /* terminated by END */
} ItemsCase;

static const ItemsCase items_cases[] = {
	{ 0, 0, 1, 1, { END } },
	{ 1, 0, 1, 1, { 0, END } },
	{ 5, 2, 1, 1, { 0, 1, 2, 3, 4, END } },
	{ 7, 3, 1, 1, { 0, 1, 2, 3, 4, 5, 6, END } },
	{ 8, 0, 1, 1, { 0, 1, 2, 3, 4, G, 7, END } },
	{ 8, 7, 1, 1, { 0, G, 3, 4, 5, 6, 7, END } },
	{ 20, 0, 1, 1, { 0, 1, 2, 3, 4, G, 19, END } },
	{ 20, 3, 1, 1, { 0, 1, 2, 3, 4, G, 19, END } },
	{ 20, 4, 1, 1, { 0, G, 3, 4, 5, G, 19, END } },
	{ 20, 9, 1, 1, { 0, G, 8, 9, 10, G, 19, END } },
	{ 20, 15, 1, 1, { 0, G, 14, 15, 16, G, 19, END } },
	{ 20, 16, 1, 1, { 0, G, 15, 16, 17, 18, 19, END } },
	{ 20, 19, 1, 1, { 0, G, 15, 16, 17, 18, 19, END } },
	{ 20, 10, 2, 0, { G, 8, 9, 10, 11, 12, G, END } },
	{ 20, 0, 0, 0, { 0, 1, G, END } },
	{ 20, 10, 0, 0, { G, 10, G, END } },
	{ 100, 50, 1, 2, { 0, 1, G, 49, 50, 51, G, 98, 99, END } },
	{ 5, 99, 1, 1, { 0, 1, 2, 3, 4, END } },
	{ 10, 5, 3, 1, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, END } }
};

static void
test_pagination_compute_items (void)
{
	guint c;

	for (c = 0; c < G_N_ELEMENTS (items_cases); c++)
	{
		const ItemsCase *tc = &items_cases[c];
		gint             items[32];
		guint            expect_n = 0;
		guint            n;
		guint            i;

		while (tc->expect[expect_n] != END)
			expect_n++;

		n = lrg_pagination_compute_items (tc->n_pages, tc->page, tc->siblings, tc->boundary, items,
		                                  G_N_ELEMENTS (items));
		if (n != expect_n)
			g_error ("case %u (n=%u p=%u s=%u b=%u): %u items, expected %u", c, tc->n_pages, tc->page,
			         tc->siblings, tc->boundary, n, expect_n);
		for (i = 0; i < n; i++)
			if (items[i] != tc->expect[i])
				g_error ("case %u item %u: %d, expected %d", c, i, items[i], tc->expect[i]);

		/* A NULL array only counts */
		g_assert_cmpuint (lrg_pagination_compute_items (tc->n_pages, tc->page, tc->siblings, tc->boundary,
		                                                NULL, 0), ==, expect_n);
	}
}

/* Invariants over many configurations: constant count, current page shown,
 * sorted pages, no gap ever hides a single page */
static void
test_pagination_compute_invariants (void)
{
	guint n, s, b, p;

	for (n = 1; n <= 30; n++)
		for (s = 0; s <= 3; s++)
			for (b = 0; b <= 2; b++)
			{
				guint fixed = 2 * b + 2 * s + 3;

				for (p = 0; p < n; p++)
				{
					gint     items[64];
					guint    count = lrg_pagination_compute_items (n, p, s, b, items, 64);
					gboolean found = FALSE;
					gint     prev = -1;
					guint    i;

					g_assert_cmpuint (count, ==, MIN (n, fixed));
					for (i = 0; i < count; i++)
					{
						if (items[i] == (gint) p)
							found = TRUE;
						if (items[i] == LRG_PAGINATION_GAP)
						{
							gint next = items[i + 1 < count ? i + 1 : i];

							/* The gap stands for at least two pages */
							if (i + 1 < count)
								g_assert_cmpint (next - prev - 1, >=, 2);
							else
								g_assert_cmpint ((gint) n - prev - 1, >=, 2);
						}
						else
						{
							g_assert_cmpint (items[i], >, prev);
							prev = items[i];
						}
					}
					g_assert_true (found);
				}
			}
}

/* ==========================================================================
 * LrgPagination: widget
 * ========================================================================== */

/* page_center: centre of @page's button */
static void
page_center (LrgPagination *pag,
             guint          page,
             gfloat        *cx,
             gfloat        *cy)
{
	gfloat x, y, w, h;

	g_assert_true (lrg_pagination_get_page_rect (pag, page, &x, &y, &w, &h));
	*cx = x + w * 0.5f;
	*cy = y + h * 0.5f;
}

/* find_item: centre of the first button of @kind (scanning left to right) */
static gboolean
find_item (LrgPagination     *pag,
           LrgPaginationItem  kind,
           gfloat            *cx,
           gfloat            *cy)
{
	LrgWidget *w = LRG_WIDGET (pag);
	gfloat     y = lrg_widget_get_world_y (w) + lrg_widget_get_height (w) * 0.5f;
	gfloat     x0 = lrg_widget_get_world_x (w);
	gfloat     x;
	gfloat     start = -1.0f;

	for (x = x0; x < x0 + lrg_widget_get_width (w); x += 1.0f)
	{
		gboolean hit = lrg_pagination_get_item_at (pag, x, y, NULL) == kind;

		if (hit && start < 0.0f)
			start = x;
		else if (!hit && start >= 0.0f)
		{
			*cx = (start + x) * 0.5f;
			*cy = y;
			return TRUE;
		}
	}
	return FALSE;
}

static void
on_page_changed (LrgPagination *pag,
                 guint          page,
                 gint          *last)
{
	*last = (gint) page;
}

static void
test_pagination_click (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgPagination       *pag = place (canvas, lrg_pagination_new (20), 50.0f, 100.0f, 700.0f, 40.0f);
	gint                 last = -1;
	gint                 jumped = -1;
	gfloat               cx, cy;

	g_signal_connect (pag, "page-changed", G_CALLBACK (on_page_changed), &last);

	/* Page buttons */
	page_center (pag, 2, &cx, &cy);
	click_at (canvas, cx, cy);
	g_assert_cmpuint (lrg_pagination_get_page (pag), ==, 2);
	g_assert_cmpint (last, ==, 2);

	/* Next arrow */
	g_assert_true (find_item (pag, LRG_PAGINATION_ITEM_NEXT, &cx, &cy));
	click_at (canvas, cx, cy);
	g_assert_cmpuint (lrg_pagination_get_page (pag), ==, 3);

	/* Prev arrow */
	g_assert_true (find_item (pag, LRG_PAGINATION_ITEM_PREVIOUS, &cx, &cy));
	click_at (canvas, cx, cy);
	click_at (canvas, cx, cy);
	click_at (canvas, cx, cy);
	g_assert_cmpuint (lrg_pagination_get_page (pag), ==, 0);

	/* At the first page the prev arrow is inert */
	last = -1;
	g_assert_cmpint (lrg_pagination_get_item_at (pag, cx, cy, &jumped), ==, LRG_PAGINATION_ITEM_PREVIOUS);
	g_assert_cmpint (jumped, ==, -1);
	click_at (canvas, cx, cy);
	g_assert_cmpuint (lrg_pagination_get_page (pag), ==, 0);
	g_assert_cmpint (last, ==, -1);

	/* Press on one page, release on another: no change */
	page_center (pag, 1, &cx, &cy);
	move_to (canvas, cx, cy);
	press_at (canvas, cx, cy);
	page_center (pag, 2, &cx, &cy);
	drag_to (canvas, cx, cy);
	release_at (canvas, cx, cy);
	g_assert_cmpuint (lrg_pagination_get_page (pag), ==, 0);

	/* A gap after the current page jumps siblings * 2 + 1 = 3 forward */
	g_assert_true (find_item (pag, LRG_PAGINATION_ITEM_GAP, &cx, &cy));
	g_assert_cmpint (lrg_pagination_get_item_at (pag, cx, cy, &jumped), ==, LRG_PAGINATION_ITEM_GAP);
	g_assert_cmpint (jumped, ==, 3);
	click_at (canvas, cx, cy);
	g_assert_cmpuint (lrg_pagination_get_page (pag), ==, 3);

	/* Inert gaps */
	lrg_pagination_set_page (pag, 10);
	lrg_pagination_set_jump_on_gap (pag, FALSE);
	g_assert_true (find_item (pag, LRG_PAGINATION_ITEM_GAP, &cx, &cy));
	click_at (canvas, cx, cy);
	g_assert_cmpuint (lrg_pagination_get_page (pag), ==, 10);

	/* No arrows when hidden */
	lrg_pagination_set_show_prev_next (pag, FALSE);
	lrg_canvas_record (canvas);
	g_assert_false (find_item (pag, LRG_PAGINATION_ITEM_NEXT, &cx, &cy));

	/* n_pages clamps the page */
	lrg_pagination_set_n_pages (pag, 4);
	g_assert_cmpuint (lrg_pagination_get_page (pag), ==, 3);
	g_assert_cmpint (last, ==, 3);
}

static void
test_pagination_keys (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgPagination       *pag = place (canvas, lrg_pagination_new (20), 50.0f, 100.0f, 700.0f, 40.0f);
	LrgButton           *other = place (canvas, lrg_button_new ("Other"), 50.0f, 200.0f, 100.0f, 30.0f);

	(void) other;
	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (pag));

	key (canvas, GRL_KEY_RIGHT);
	g_assert_cmpuint (lrg_pagination_get_page (pag), ==, 1);
	key (canvas, GRL_KEY_END);
	g_assert_cmpuint (lrg_pagination_get_page (pag), ==, 19);

	/* Arrows at the end are still consumed: focus stays */
	key (canvas, GRL_KEY_RIGHT);
	g_assert_cmpuint (lrg_pagination_get_page (pag), ==, 19);
	g_assert_true (lrg_canvas_get_focused_widget (canvas) == LRG_WIDGET (pag));

	key (canvas, GRL_KEY_PAGE_UP);
	g_assert_cmpuint (lrg_pagination_get_page (pag), ==, 16);
	key (canvas, GRL_KEY_LEFT);
	g_assert_cmpuint (lrg_pagination_get_page (pag), ==, 15);
	key (canvas, GRL_KEY_HOME);
	g_assert_cmpuint (lrg_pagination_get_page (pag), ==, 0);
	key (canvas, GRL_KEY_PAGE_DOWN);
	g_assert_cmpuint (lrg_pagination_get_page (pag), ==, 3);
	key (canvas, GRL_KEY_DOWN);
	g_assert_true (lrg_canvas_get_focused_widget (canvas) != LRG_WIDGET (pag));
}

static void
test_pagination_rtl (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgPagination       *pag = place (canvas, lrg_pagination_new (5), 50.0f, 100.0f, 700.0f, 40.0f);
	gfloat               x0, x1, y, w, h;
	gfloat               next_x, prev_x, cy;

	lrg_widget_set_direction (LRG_WIDGET (pag), LRG_WIDGET_DIRECTION_RTL);
	lrg_canvas_record (canvas);

	/* Page 1 sits right of page 2; next is on the left */
	g_assert_true (lrg_pagination_get_page_rect (pag, 0, &x0, &y, &w, &h));
	g_assert_true (lrg_pagination_get_page_rect (pag, 1, &x1, &y, &w, &h));
	g_assert_cmpfloat (x0, >, x1);
	g_assert_true (find_item (pag, LRG_PAGINATION_ITEM_NEXT, &next_x, &cy));
	g_assert_true (find_item (pag, LRG_PAGINATION_ITEM_PREVIOUS, &prev_x, &cy));
	g_assert_cmpfloat (next_x, <, prev_x);

	/* Mirrored about the widget's centre */
	{
		gfloat lx, ly, lw, lh;

		lrg_widget_set_direction (LRG_WIDGET (pag), LRG_WIDGET_DIRECTION_LTR);
		g_assert_true (lrg_pagination_get_page_rect (pag, 0, &lx, &ly, &lw, &lh));
		g_assert_cmpfloat (fabsf ((lx + lw * 0.5f) + (x0 + w * 0.5f) - 800.0f), <, 0.5f);
		lrg_widget_set_direction (LRG_WIDGET (pag), LRG_WIDGET_DIRECTION_RTL);
	}

	/* Click next (on the left) and keys mirror too */
	click_at (canvas, next_x, cy);
	g_assert_cmpuint (lrg_pagination_get_page (pag), ==, 1);
	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (pag));
	key (canvas, GRL_KEY_LEFT);
	g_assert_cmpuint (lrg_pagination_get_page (pag), ==, 2);
	key (canvas, GRL_KEY_RIGHT);
	g_assert_cmpuint (lrg_pagination_get_page (pag), ==, 1);
}

static void
test_pagination_springs (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgPagination       *pag = place (canvas, lrg_pagination_new (5), 50.0f, 100.0f, 700.0f, 40.0f);
	gfloat               start;
	gfloat               target;
	gfloat               mid;
	gfloat               y, w, h;
	guint                i;

	/* First step snaps: the pill starts on page 0 */
	frames (canvas, 1);
	g_assert_true (lrg_pagination_get_page_rect (pag, 0, &start, &y, &w, &h));
	g_assert_cmpfloat (fabsf (lrg_pagination_get_indicator_x (pag) - start), <, 0.01f);
	g_assert_false (lrg_canvas_update (canvas, DT));

	/* It glides to page 3 */
	lrg_pagination_set_page (pag, 3);
	g_assert_true (lrg_pagination_get_page_rect (pag, 3, &target, &y, &w, &h));
	g_assert_true (lrg_canvas_update (canvas, DT));
	frames (canvas, 1);
	mid = lrg_pagination_get_indicator_x (pag);
	g_assert_cmpfloat (mid, >, start + 0.5f);
	g_assert_cmpfloat (mid, <, target - 0.5f);

	for (i = 0; i < 240 && lrg_canvas_update (canvas, DT); i++)
		;
	g_assert_cmpuint (i, <, 240);
	g_assert_cmpfloat (fabsf (lrg_pagination_get_indicator_x (pag) - target), <, 0.5f);

	/* Hover springs settle too */
	{
		gfloat cx, cy;

		page_center (pag, 1, &cx, &cy);
		move_to (canvas, cx, cy);
		g_assert_true (lrg_canvas_update (canvas, DT));
		for (i = 0; i < 240 && lrg_canvas_update (canvas, DT); i++)
			;
		g_assert_cmpuint (i, <, 240);
	}
}

/* Records in every theme preset; the pill uses the accent colour */
static void
test_pagination_paint (void)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	guint                p;

	for (p = 0; presets[p] != NULL; p++)
	{
		g_autoptr(LrgCanvas) canvas = canvas_new ();
		g_autoptr(LrgTheme)  theme = lrg_theme_new_preset (presets[p]);
		LrgPagination       *pag;
		LrgPaintList        *list;
		const GrlColor      *accent;
		gboolean             found = FALSE;
		guint                i;

		lrg_widget_set_theme (LRG_WIDGET (canvas), theme);
		pag = place (canvas, lrg_pagination_new (20), 50.0f, 100.0f, 700.0f, 40.0f);
		lrg_pagination_set_page (pag, 9);
		frames (canvas, 2);
		list = lrg_canvas_record (canvas);
		g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, 10);

		accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
		for (i = 0; i < lrg_paint_list_get_n_instances (list) && !found; i++)
		{
			const LrgPaintInstance *inst = lrg_paint_list_get_instance (list, i);

			found = inst->color[0] == accent->r && inst->color[1] == accent->g &&
			        inst->color[2] == accent->b && inst->color[3] > 0;
		}
		g_assert_true (found);
	}
}

/* ==========================================================================
 * LrgHeaderBar: decoration layout
 * ========================================================================== */

#define BN LRG_HEADER_BAR_BUTTON_NONE
#define BMIN LRG_HEADER_BAR_BUTTON_MINIMIZE
#define BMAX LRG_HEADER_BAR_BUTTON_MAXIMIZE
#define BCLOSE LRG_HEADER_BAR_BUTTON_CLOSE

typedef struct
{
	const gchar        *layout;
	guint               n;
	LrgHeaderBarButton  start[3];
	LrgHeaderBarButton  end[3];
} LayoutCase;

static const LayoutCase layout_cases[] = {
	{ ":minimize,maximize,close", 3, { BN, BN, BN }, { BMIN, BMAX, BCLOSE } },
	{ "close:", 1, { BCLOSE, BN, BN }, { BN, BN, BN } },
	{ "close,minimize:maximize", 3, { BCLOSE, BMIN, BN }, { BMAX, BN, BN } },
	{ "menu:minimize,close", 2, { BN, BN, BN }, { BMIN, BCLOSE, BN } },
	{ " icon , minimize : close ", 2, { BMIN, BN, BN }, { BCLOSE, BN, BN } },
	{ "close,close:close,maximize", 2, { BCLOSE, BN, BN }, { BMAX, BN, BN } },
	{ "minimize,close", 2, { BMIN, BCLOSE, BN }, { BN, BN, BN } },
	{ "", 0, { BN, BN, BN }, { BN, BN, BN } },
	{ NULL, 0, { BN, BN, BN }, { BN, BN, BN } },
	{ "spacer:bogus", 0, { BN, BN, BN }, { BN, BN, BN } }
};

static void
test_header_bar_parse_layout (void)
{
	guint c;

	for (c = 0; c < G_N_ELEMENTS (layout_cases); c++)
	{
		const LayoutCase   *tc = &layout_cases[c];
		LrgHeaderBarButton  start[3];
		LrgHeaderBarButton  end[3];
		guint               i;

		g_assert_cmpuint (lrg_header_bar_parse_decoration_layout (tc->layout, start, end), ==, tc->n);
		for (i = 0; i < 3; i++)
		{
			g_assert_cmpint (start[i], ==, tc->start[i]);
			g_assert_cmpint (end[i], ==, tc->end[i]);
		}
	}
}

/* ==========================================================================
 * LrgHeaderBar: fake window
 * ========================================================================== */

typedef struct
{
	gboolean can_move;
	gint     x;
	gint     y;
	gint     moves;
	gboolean maximized;
	gint     minimized;
	gint     maximize_calls;
	gint     restore_calls;
} FakeWindow;

static gboolean
fake_can_move (gpointer user_data)
{
	return ((FakeWindow *) user_data)->can_move;
}

static void
fake_get_position (gpointer  user_data,
                   gint     *x,
                   gint     *y)
{
	*x = ((FakeWindow *) user_data)->x;
	*y = ((FakeWindow *) user_data)->y;
}

static void
fake_set_position (gpointer user_data,
                   gint     x,
                   gint     y)
{
	FakeWindow *w = user_data;

	w->x = x;
	w->y = y;
	w->moves++;
}

static void
fake_minimize (gpointer user_data)
{
	((FakeWindow *) user_data)->minimized++;
}

static gboolean
fake_is_maximized (gpointer user_data)
{
	return ((FakeWindow *) user_data)->maximized;
}

static void
fake_maximize (gpointer user_data)
{
	((FakeWindow *) user_data)->maximized = TRUE;
	((FakeWindow *) user_data)->maximize_calls++;
}

static void
fake_restore (gpointer user_data)
{
	((FakeWindow *) user_data)->maximized = FALSE;
	((FakeWindow *) user_data)->restore_calls++;
}

static const LrgHeaderBarWindowFuncs fake_funcs = {
	fake_can_move,
	fake_get_position,
	fake_set_position,
	fake_minimize,
	fake_is_maximized,
	fake_maximize,
	fake_restore
};

typedef struct
{
	LrgCanvas    *canvas;
	LrgHeaderBar *bar;
	LrgButton    *back;
	LrgButton    *menu;
	LrgLabel     *tag;
	FakeWindow    win;
	gint          quits;
	gint          close_requests;
	gboolean      veto;
} BarFixture;

static gboolean
on_close_requested (LrgHeaderBar *bar,
                    BarFixture   *f)
{
	f->close_requests++;
	return f->veto;
}

static void
bar_set_up (BarFixture    *f,
            gconstpointer  data)
{
	memset (&f->win, 0, sizeof (f->win));
	f->win.can_move = TRUE;
	f->win.x = 100;
	f->win.y = 50;

	f->canvas = canvas_new ();
	f->bar = lrg_header_bar_new ();
	lrg_header_bar_set_title (f->bar, "Document.txt");
	lrg_header_bar_set_subtitle (f->bar, "~/notes");
	lrg_header_bar_set_window_funcs (f->bar, &fake_funcs, &f->win, NULL);

	f->back = lrg_button_new ("Back");
	f->tag = lrg_label_new ("Draft");
	f->menu = lrg_button_new ("Menu");
	lrg_header_bar_pack_start (f->bar, LRG_WIDGET (f->back));
	lrg_header_bar_pack_start (f->bar, LRG_WIDGET (f->tag));
	lrg_header_bar_pack_end (f->bar, LRG_WIDGET (f->menu));
	g_object_unref (f->back);
	g_object_unref (f->tag);
	g_object_unref (f->menu);

	place (f->canvas, f->bar, 0.0f, 0.0f, 800.0f, 48.0f);

	g_signal_connect (f->canvas, "quit-requested", G_CALLBACK (count_cb), &f->quits);
	g_signal_connect (f->bar, "close-requested", G_CALLBACK (on_close_requested), f);
}

static void
bar_tear_down (BarFixture    *f,
               gconstpointer  data)
{
	g_clear_object (&f->canvas);
}

static gfloat
cx_of (gpointer widget)
{
	return lrg_widget_get_world_x (LRG_WIDGET (widget)) + lrg_widget_get_width (LRG_WIDGET (widget)) * 0.5f;
}

static void
button_center (LrgHeaderBar       *bar,
               LrgHeaderBarButton  b,
               gfloat             *cx,
               gfloat             *cy)
{
	gfloat x, y, w, h;

	g_assert_true (lrg_header_bar_get_button_rect (bar, b, &x, &y, &w, &h));
	*cx = x + w * 0.5f;
	*cy = y + h * 0.5f;
}

static void
test_header_bar_layout_ltr (BarFixture    *f,
                            gconstpointer  data)
{
	gfloat min_x, max_x, close_x, cy;
	gfloat back_x = lrg_widget_get_world_x (LRG_WIDGET (f->back));
	gfloat tag_x = lrg_widget_get_world_x (LRG_WIDGET (f->tag));
	gfloat menu_r = lrg_widget_get_world_x (LRG_WIDGET (f->menu)) + lrg_widget_get_width (LRG_WIDGET (f->menu));

	/* Start children from the left, in order */
	g_assert_cmpfloat (back_x, <, 20.0f);
	g_assert_cmpfloat (tag_x, >, back_x + lrg_widget_get_width (LRG_WIDGET (f->back)) - 0.5f);

	/* Window buttons at the right in layout order, end child before them */
	button_center (f->bar, BMIN, &min_x, &cy);
	button_center (f->bar, BMAX, &max_x, &cy);
	button_center (f->bar, BCLOSE, &close_x, &cy);
	g_assert_cmpfloat (min_x, <, max_x);
	g_assert_cmpfloat (max_x, <, close_x);
	g_assert_cmpfloat (close_x, >, 760.0f);
	g_assert_cmpfloat (menu_r, <, min_x);
	g_assert_cmpfloat (fabsf (cy - 24.0f), <=, 1.0f);

	/* Children are centred vertically */
	g_assert_cmpfloat (fabsf (lrg_widget_get_world_y (LRG_WIDGET (f->back)) +
	                          lrg_widget_get_height (LRG_WIDGET (f->back)) * 0.5f - 24.0f), <, 1.0f);

	/* Hiding the buttons gives the space back */
	lrg_header_bar_set_show_window_buttons (f->bar, FALSE);
	lrg_canvas_record (f->canvas);
	g_assert_false (lrg_header_bar_get_button_rect (f->bar, BCLOSE, NULL, NULL, NULL, NULL));
	g_assert_cmpfloat (lrg_widget_get_world_x (LRG_WIDGET (f->menu)) + lrg_widget_get_width (LRG_WIDGET (f->menu)),
	                   >, 780.0f);

	/* macOS style: close at the leading edge, children after it */
	lrg_header_bar_set_show_window_buttons (f->bar, TRUE);
	lrg_header_bar_set_decoration_layout (f->bar, "close:");
	lrg_canvas_record (f->canvas);
	button_center (f->bar, BCLOSE, &close_x, &cy);
	g_assert_cmpfloat (close_x, <, 30.0f);
	g_assert_cmpfloat (lrg_widget_get_world_x (LRG_WIDGET (f->back)), >, close_x);
	g_assert_false (lrg_header_bar_get_button_rect (f->bar, BMIN, NULL, NULL, NULL, NULL));
}

static void
test_header_bar_title_widget (BarFixture    *f,
                              gconstpointer  data)
{
	LrgLabel *title = lrg_label_new ("Centred");

	lrg_header_bar_set_title_widget (f->bar, LRG_WIDGET (title));
	g_object_unref (title);
	lrg_canvas_record (f->canvas);
	g_assert_true (lrg_header_bar_get_title_widget (f->bar) == LRG_WIDGET (title));
	g_assert_cmpfloat (fabsf (cx_of (title) - 400.0f), <, 1.0f);

	/* Not a start / end child */
	g_assert_false (lrg_header_bar_get_child_packing (f->bar, LRG_WIDGET (title)));

	/* Moving a child to the end group */
	lrg_header_bar_set_child_packing (f->bar, LRG_WIDGET (f->tag), TRUE);
	lrg_canvas_record (f->canvas);
	g_assert_true (lrg_header_bar_get_child_packing (f->bar, LRG_WIDGET (f->tag)));
	g_assert_cmpfloat (cx_of (f->tag), >, 400.0f);

	/* Unsetting removes it from the bar */
	lrg_header_bar_set_title_widget (f->bar, NULL);
	g_assert_null (lrg_header_bar_get_title_widget (f->bar));
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (f->bar)), ==, 3);
}

static void
test_header_bar_layout_rtl (BarFixture    *f,
                            gconstpointer  data)
{
	gfloat min_x, max_x, close_x, cy;

	lrg_canvas_set_default_direction (f->canvas, LRG_WIDGET_DIRECTION_RTL);
	lrg_canvas_record (f->canvas);

	/* Start children from the right, end child on the left */
	g_assert_cmpfloat (cx_of (f->back), >, 700.0f);
	g_assert_cmpfloat (cx_of (f->tag), <, cx_of (f->back));
	g_assert_cmpfloat (cx_of (f->menu), <, 400.0f);

	/* Window buttons mirrored: close at the far left */
	button_center (f->bar, BMIN, &min_x, &cy);
	button_center (f->bar, BMAX, &max_x, &cy);
	button_center (f->bar, BCLOSE, &close_x, &cy);
	g_assert_cmpfloat (close_x, <, 40.0f);
	g_assert_cmpfloat (close_x, <, max_x);
	g_assert_cmpfloat (max_x, <, min_x);
	g_assert_cmpfloat (cx_of (f->menu), >, min_x);

	/* Clicking the mirrored close still closes */
	click_at (f->canvas, close_x, cy);
	g_assert_cmpint (f->close_requests, ==, 1);
	g_assert_cmpint (f->quits, ==, 1);
}

static void
test_header_bar_drag_area (BarFixture    *f,
                           gconstpointer  data)
{
	gfloat cx, cy;

	/* Empty space and the title are drag area */
	g_assert_true (lrg_header_bar_is_drag_area (f->bar, 400.0f, 24.0f));
	g_assert_true (lrg_header_bar_is_drag_area (f->bar, 550.0f, 5.0f));

	/* Inert children too, interactive ones and window buttons not */
	g_assert_true (lrg_header_bar_is_drag_area (f->bar, cx_of (f->tag), 24.0f));
	g_assert_false (lrg_header_bar_is_drag_area (f->bar, cx_of (f->back), 24.0f));
	g_assert_false (lrg_header_bar_is_drag_area (f->bar, cx_of (f->menu), 24.0f));
	button_center (f->bar, BMAX, &cx, &cy);
	g_assert_false (lrg_header_bar_is_drag_area (f->bar, cx, cy));
	g_assert_cmpint (lrg_header_bar_get_button_at (f->bar, cx, cy), ==, BMAX);

	/* Outside the bar */
	g_assert_false (lrg_header_bar_is_drag_area (f->bar, 400.0f, 60.0f));
}

static void
test_header_bar_drag (BarFixture    *f,
                      gconstpointer  data)
{
	/* Drag the title: the window follows the pointer delta */
	move_to (f->canvas, 400.0f, 20.0f);
	press_at (f->canvas, 400.0f, 20.0f);
	drag_to (f->canvas, 412.0f, 27.0f);
	g_assert_cmpint (f->win.x, ==, 112);
	g_assert_cmpint (f->win.y, ==, 57);
	release_at (f->canvas, 412.0f, 27.0f);

	/* Movement after release does nothing */
	move_to (f->canvas, 450.0f, 30.0f);
	g_assert_cmpint (f->win.moves, ==, 1);

	/* Dragging from an inert child (a label) works too */
	frames (f->canvas, 60);
	move_to (f->canvas, cx_of (f->tag), 24.0f);
	press_at (f->canvas, cx_of (f->tag), 24.0f);
	drag_to (f->canvas, cx_of (f->tag) - 5.0f, 24.0f);
	release_at (f->canvas, cx_of (f->tag) - 5.0f, 24.0f);
	g_assert_cmpint (f->win.x, ==, 107);

	/* Not on a button child: it gets the click instead */
	frames (f->canvas, 60);
	move_to (f->canvas, cx_of (f->back), 24.0f);
	press_at (f->canvas, cx_of (f->back), 24.0f);
	drag_to (f->canvas, cx_of (f->back) + 30.0f, 24.0f);
	release_at (f->canvas, cx_of (f->back) + 30.0f, 24.0f);
	g_assert_cmpint (f->win.moves, ==, 2);

	/* Wayland (no client-side moves): a no-op */
	frames (f->canvas, 60);
	f->win.can_move = FALSE;
	g_assert_false (lrg_header_bar_get_can_move_window (f->bar));
	move_to (f->canvas, 400.0f, 20.0f);
	press_at (f->canvas, 400.0f, 20.0f);
	drag_to (f->canvas, 450.0f, 20.0f);
	release_at (f->canvas, 450.0f, 20.0f);
	g_assert_cmpint (f->win.moves, ==, 2);

	/* Not draggable */
	frames (f->canvas, 60);
	f->win.can_move = TRUE;
	lrg_header_bar_set_draggable (f->bar, FALSE);
	move_to (f->canvas, 400.0f, 20.0f);
	press_at (f->canvas, 400.0f, 20.0f);
	drag_to (f->canvas, 450.0f, 20.0f);
	release_at (f->canvas, 450.0f, 20.0f);
	g_assert_cmpint (f->win.moves, ==, 2);
}

static void
test_header_bar_double_click (BarFixture    *f,
                              gconstpointer  data)
{
	/* Two quick clicks on the title maximise, two more restore */
	click_at (f->canvas, 400.0f, 20.0f);
	click_at (f->canvas, 401.0f, 20.0f);
	g_assert_true (f->win.maximized);
	g_assert_cmpint (f->win.maximize_calls, ==, 1);

	click_at (f->canvas, 400.0f, 20.0f);
	click_at (f->canvas, 400.0f, 21.0f);
	g_assert_false (f->win.maximized);
	g_assert_cmpint (f->win.restore_calls, ==, 1);

	/* Too slow */
	frames (f->canvas, 60);
	click_at (f->canvas, 400.0f, 20.0f);
	frames (f->canvas, 60);
	click_at (f->canvas, 400.0f, 20.0f);
	g_assert_false (f->win.maximized);

	/* Too far apart */
	frames (f->canvas, 60);
	click_at (f->canvas, 300.0f, 20.0f);
	click_at (f->canvas, 500.0f, 20.0f);
	g_assert_false (f->win.maximized);

	/* Three clicks: only the first pair counts */
	frames (f->canvas, 60);
	click_at (f->canvas, 400.0f, 20.0f);
	click_at (f->canvas, 400.0f, 20.0f);
	click_at (f->canvas, 400.0f, 20.0f);
	g_assert_true (f->win.maximized);
	g_assert_cmpint (f->win.maximize_calls, ==, 2);
}

static void
test_header_bar_buttons (BarFixture    *f,
                         gconstpointer  data)
{
	gfloat cx, cy;

	button_center (f->bar, BMIN, &cx, &cy);
	click_at (f->canvas, cx, cy);
	g_assert_cmpint (f->win.minimized, ==, 1);

	button_center (f->bar, BMAX, &cx, &cy);
	click_at (f->canvas, cx, cy);
	g_assert_true (f->win.maximized);
	click_at (f->canvas, cx, cy);
	g_assert_false (f->win.maximized);

	/* Press on a button, release elsewhere: nothing */
	move_to (f->canvas, cx, cy);
	press_at (f->canvas, cx, cy);
	drag_to (f->canvas, 400.0f, 20.0f);
	release_at (f->canvas, 400.0f, 20.0f);
	g_assert_false (f->win.maximized);
	g_assert_cmpint (f->win.moves, ==, 0);

	/* Close: the handler can veto; otherwise app.quit runs */
	button_center (f->bar, BCLOSE, &cx, &cy);
	f->veto = TRUE;
	click_at (f->canvas, cx, cy);
	g_assert_cmpint (f->close_requests, ==, 1);
	g_assert_cmpint (f->quits, ==, 0);
	f->veto = FALSE;
	click_at (f->canvas, cx, cy);
	g_assert_cmpint (f->close_requests, ==, 2);
	g_assert_cmpint (f->quits, ==, 1);

	/* Programmatic activation goes the same way */
	lrg_header_bar_activate_button (f->bar, BCLOSE);
	g_assert_cmpint (f->quits, ==, 2);
}

/* The close button turns DANGER when hovered (after its spring runs) */
static void
test_header_bar_close_hover (BarFixture    *f,
                             gconstpointer  data)
{
	const GrlColor *danger = lrg_theme_get_color (lrg_widget_get_theme (LRG_WIDGET (f->bar)),
	                                              LRG_THEME_COLOR_DANGER);
	LrgPaintList   *list;
	gfloat          cx, cy;
	gboolean        found;
	guint           i;

	frames (f->canvas, 2);
	button_center (f->bar, BCLOSE, &cx, &cy);

	/* Count danger instances at rest */
	list = lrg_canvas_record (f->canvas);
	found = FALSE;
	for (i = 0; i < lrg_paint_list_get_n_instances (list); i++)
	{
		const LrgPaintInstance *inst = lrg_paint_list_get_instance (list, i);

		if (inst->color[0] == danger->r && inst->color[1] == danger->g && inst->color[2] == danger->b)
			found = TRUE;
	}
	g_assert_false (found);

	move_to (f->canvas, cx, cy);
	for (i = 0; i < 240 && lrg_canvas_update (f->canvas, DT); i++)
		;
	list = lrg_canvas_record (f->canvas);
	for (i = 0; i < lrg_paint_list_get_n_instances (list) && !found; i++)
	{
		const LrgPaintInstance *inst = lrg_paint_list_get_instance (list, i);

		found = inst->color[0] == danger->r && inst->color[1] == danger->g &&
		        inst->color[2] == danger->b && inst->rect[0] <= cx && inst->rect[0] + inst->rect[2] >= cx;
	}
	g_assert_true (found);
}

static void
test_header_bar_paint_presets (void)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	guint                p;

	for (p = 0; presets[p] != NULL; p++)
	{
		g_autoptr(LrgCanvas) canvas = canvas_new ();
		g_autoptr(LrgTheme)  theme = lrg_theme_new_preset (presets[p]);
		LrgHeaderBar        *bar = lrg_header_bar_new ();
		LrgPaintList        *list;

		lrg_widget_set_theme (LRG_WIDGET (canvas), theme);
		lrg_header_bar_set_title (bar, "Preset");
		lrg_header_bar_set_window_funcs (bar, NULL, NULL, NULL);
		place (canvas, bar, 0.0f, 0.0f, 800.0f, 48.0f);
		frames (canvas, 2);
		list = lrg_canvas_record (canvas);
		g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, 5);

		/* Default (raylib) functions are harmless without a window
		 * (with the GPU test's hidden window they would act on it) */
		if (!graphics_available)
		{
			g_assert_false (lrg_header_bar_get_can_move_window (bar));
			lrg_header_bar_activate_button (bar, BMAX);
			lrg_header_bar_activate_button (bar, BMIN);
		}
	}
}

/* ==========================================================================
 * LrgNotification: fake notification server on a private bus
 * ========================================================================== */

static const gchar fake_xml[] =
	"<node>"
	"  <interface name='org.freedesktop.Notifications'>"
	"    <method name='Notify'>"
	"      <arg type='s' direction='in'/>"
	"      <arg type='u' direction='in'/>"
	"      <arg type='s' direction='in'/>"
	"      <arg type='s' direction='in'/>"
	"      <arg type='s' direction='in'/>"
	"      <arg type='as' direction='in'/>"
	"      <arg type='a{sv}' direction='in'/>"
	"      <arg type='i' direction='in'/>"
	"      <arg type='u' direction='out'/>"
	"    </method>"
	"    <method name='CloseNotification'>"
	"      <arg type='u' direction='in'/>"
	"    </method>"
	"    <method name='GetCapabilities'>"
	"      <arg type='as' direction='out'/>"
	"    </method>"
	"    <signal name='ActionInvoked'><arg type='u'/><arg type='s'/></signal>"
	"    <signal name='NotificationClosed'><arg type='u'/><arg type='u'/></signal>"
	"  </interface>"
	"</node>";

typedef struct
{
	GDBusConnection *conn;
	GDBusNodeInfo   *info;
	guint            reg_id;
	gboolean         owned;
	guint            next_id;
	guint            n_notify;
	guint            n_close;
	guint            last_close_id;
	GVariant        *last_notify;
} FakeServer;

static void
fake_emit (FakeServer  *srv,
           const gchar *signal,
           GVariant    *params)
{
	g_autoptr(GError) error = NULL;

	g_dbus_connection_emit_signal (srv->conn, NULL, "/org/freedesktop/Notifications",
	                               "org.freedesktop.Notifications", signal, params, &error);
	g_assert_no_error (error);
}

static void
fake_method_call (GDBusConnection       *conn,
                  const gchar           *sender,
                  const gchar           *path,
                  const gchar           *iface,
                  const gchar           *method,
                  GVariant              *params,
                  GDBusMethodInvocation *invocation,
                  gpointer               user_data)
{
	FakeServer *srv = user_data;

	if (g_strcmp0 (method, "Notify") == 0)
	{
		guint32 replaces = 0;
		guint32 id;

		g_variant_get_child (params, 1, "u", &replaces);
		id = replaces != 0 ? replaces : ++srv->next_id;
		g_clear_pointer (&srv->last_notify, g_variant_unref);
		srv->last_notify = g_variant_ref (params);
		srv->n_notify++;
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(u)", id));
	}
	else if (g_strcmp0 (method, "CloseNotification") == 0)
	{
		guint32 id = 0;

		g_variant_get (params, "(u)", &id);
		srv->n_close++;
		srv->last_close_id = id;
		g_dbus_method_invocation_return_value (invocation, NULL);
		fake_emit (srv, "NotificationClosed", g_variant_new ("(uu)", id, 3u));
	}
	else
	{
		const gchar *caps[] = { "actions", "body" };

		g_dbus_method_invocation_return_value (invocation,
		                                       g_variant_new ("(@as)", g_variant_new_strv (caps, 2)));
	}
}

static const GDBusInterfaceVTable fake_vtable = { fake_method_call, NULL, NULL, { 0 } };

/* fake_start: connect to the test bus, export the object and own the name */
static void
fake_start (FakeServer  *srv,
            GTestDBus   *bus)
{
	g_autoptr(GError)   error = NULL;
	g_autoptr(GVariant) reply = NULL;
	guint32             result = 0;

	srv->conn = g_dbus_connection_new_for_address_sync (g_test_dbus_get_bus_address (bus),
	                                                    G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
	                                                    G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
	                                                    NULL, NULL, &error);
	g_assert_no_error (error);
	srv->info = g_dbus_node_info_new_for_xml (fake_xml, &error);
	g_assert_no_error (error);
	srv->reg_id = g_dbus_connection_register_object (srv->conn, "/org/freedesktop/Notifications",
	                                                 srv->info->interfaces[0], &fake_vtable, srv, NULL,
	                                                 &error);
	g_assert_no_error (error);

	/* RequestName goes to dbus-daemon (another process): sync is fine */
	reply = g_dbus_connection_call_sync (srv->conn, "org.freedesktop.DBus", "/org/freedesktop/DBus",
	                                     "org.freedesktop.DBus", "RequestName",
	                                     g_variant_new ("(su)", "org.freedesktop.Notifications", 4u),
	                                     G_VARIANT_TYPE ("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
	g_assert_no_error (error);
	g_variant_get (reply, "(u)", &result);
	g_assert_cmpuint (result, ==, 1);
	srv->owned = TRUE;
}

static void
fake_stop (FakeServer *srv)
{
	if (srv->conn == NULL)
		return;
	g_dbus_connection_unregister_object (srv->conn, srv->reg_id);
	g_dbus_connection_close_sync (srv->conn, NULL, NULL);
	g_clear_object (&srv->conn);
	g_clear_pointer (&srv->info, g_dbus_node_info_unref);
	g_clear_pointer (&srv->last_notify, g_variant_unref);
}

/* ==========================================================================
 * LrgNotification: fixture
 * ========================================================================== */

typedef struct
{
	GTestDBus          *bus;
	FakeServer          srv;
	LrgCanvas          *canvas;
	LrgWidget          *anchor;
	LrgToastQueue      *queue;
	GSimpleActionGroup *group;
	gint                opens;
	gchar              *open_target;
	gint                shows;
	gint                activated;
	gint                closed;
	gint                close_reason;
	gchar              *invoked;
} NotifyFixture;

static void
action_open (GSimpleAction *action,
             GVariant      *parameter,
             gpointer       user_data)
{
	NotifyFixture *f = user_data;

	f->opens++;
	g_free (f->open_target);
	f->open_target = g_variant_dup_string (parameter, NULL);
}

static void
action_show (GSimpleAction *action,
             GVariant      *parameter,
             gpointer       user_data)
{
	((NotifyFixture *) user_data)->shows++;
}

static void
on_activated (LrgNotification *n,
              NotifyFixture   *f)
{
	f->activated++;
}

static void
on_closed (LrgNotification            *n,
           LrgNotificationCloseReason  reason,
           NotifyFixture              *f)
{
	f->closed++;
	f->close_reason = (gint) reason;
}

static void
on_invoked (LrgNotification *n,
            const gchar     *action,
            NotifyFixture   *f)
{
	g_free (f->invoked);
	f->invoked = g_strdup (action);
}

static void
notify_set_up (NotifyFixture *f,
               gconstpointer  data)
{
	const GActionEntry entries[] = {
		{ "open", action_open, "s", NULL, NULL, { 0 } },
		{ "show", action_show, NULL, NULL, NULL, { 0 } }
	};

	memset (f, 0, sizeof (*f));
	f->bus = g_test_dbus_new (G_TEST_DBUS_NONE);
	g_test_dbus_up (f->bus);

	f->canvas = canvas_new ();
	f->group = g_simple_action_group_new ();
	g_action_map_add_action_entries (G_ACTION_MAP (f->group), entries, G_N_ELEMENTS (entries), f);
	lrg_widget_insert_action_group (LRG_WIDGET (f->canvas), "test", G_ACTION_GROUP (f->group));
	f->anchor = place (f->canvas, lrg_label_new ("anchor"), 10.0f, 10.0f, 100.0f, 20.0f);

	f->queue = lrg_toast_queue_new ();
	lrg_container_add_child (LRG_CONTAINER (f->canvas), LRG_WIDGET (f->queue));
	g_object_unref (f->queue);
	lrg_widget_allocate (LRG_WIDGET (f->queue), 0.0f, 0.0f, 800.0f, 600.0f);

	lrg_notification_set_force_in_app (FALSE);
	lrg_notification_set_default_toast_queue (NULL);
}

/* spin: run the main context until @flag is set, a timeout or a cap */
static gboolean
timeout_cb (gpointer data)
{
	*(gboolean *) data = TRUE;
	return G_SOURCE_REMOVE;
}

#define WAIT_FOR(cond) \
	G_STMT_START { \
		gboolean timed_out_ = FALSE; \
		guint    tid_ = g_timeout_add_seconds (10, timeout_cb, &timed_out_); \
		guint    iter_ = 0; \
		while (!(cond) && !timed_out_ && iter_++ < 100000) \
			g_main_context_iteration (NULL, TRUE); \
		if (!timed_out_) \
			g_source_remove (tid_); \
		g_assert_true (cond); \
	} G_STMT_END

/* drain: dispatch whatever is ready without blocking */
static void
drain (void)
{
	guint i;

	for (i = 0; i < 1000 && g_main_context_iteration (NULL, FALSE); i++)
		;
}

static void
notify_tear_down (NotifyFixture *f,
                  gconstpointer  data)
{
	g_clear_object (&f->canvas);
	g_clear_object (&f->group);
	drain ();
	fake_stop (&f->srv);
	drain ();
	g_test_dbus_down (f->bus);
	g_clear_object (&f->bus);
	g_free (f->open_target);
	g_free (f->invoked);
	lrg_notification_set_force_in_app (FALSE);
	lrg_notification_set_default_toast_queue (NULL);
}

/* show_result: completion of show_async */
typedef struct
{
	gboolean done;
	gboolean ok;
	GError  *error;
} ShowResult;

static void
on_show_done (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
	ShowResult *r = user_data;

	r->ok = lrg_notification_show_finish (LRG_NOTIFICATION (source), result, &r->error);
	r->done = TRUE;
}

static gboolean
show_and_wait (LrgNotification *n,
               LrgWidget       *anchor,
               GError         **error)
{
	ShowResult r = { FALSE, FALSE, NULL };

	lrg_notification_show_async (n, anchor, NULL, on_show_done, &r);
	WAIT_FOR (r.done);
	if (r.error != NULL)
		g_propagate_error (error, r.error);
	return r.ok;
}

static LrgNotification *
notification_for (NotifyFixture *f)
{
	LrgNotification *n = lrg_notification_new ("New mail");

	lrg_notification_set_body (n, "3 unread messages");
	lrg_notification_set_icon_name (n, "mail-unread");
	lrg_notification_set_app_name (n, "Chrome Test");
	lrg_notification_set_desktop_entry (n, "org.example.ChromeTest");
	lrg_notification_set_urgency (n, LRG_NOTIFICATION_URGENCY_CRITICAL);
	lrg_notification_set_timeout (n, 5000);
	lrg_notification_set_default_action (n, "test.show");
	lrg_notification_add_button (n, "Open inbox", "test.open::inbox");
	lrg_notification_add_button (n, "Archive", "test.open('archive')");
	g_signal_connect (n, "activated", G_CALLBACK (on_activated), f);
	g_signal_connect (n, "closed", G_CALLBACK (on_closed), f);
	g_signal_connect (n, "action-invoked", G_CALLBACK (on_invoked), f);
	return n;
}

static void
test_notify_args (NotifyFixture *f,
                  gconstpointer  data)
{
	g_autoptr(LrgNotification) n = notification_for (f);
	g_autoptr(GError)          error = NULL;
	g_autoptr(GVariant)        actions = NULL;
	g_autoptr(GVariant)        hints = NULL;
	const gchar               *app, *icon, *summary, *body;
	guint32                    replaces;
	gint32                     timeout;
	guchar                     urgency = 0;
	const gchar               *entry = NULL;
	g_autofree const gchar   **keys = NULL;

	fake_start (&f->srv, f->bus);
	g_assert_true (show_and_wait (n, f->anchor, &error));
	g_assert_no_error (error);
	g_assert_cmpint (lrg_notification_get_backend (n), ==, LRG_NOTIFICATION_BACKEND_DBUS);
	g_assert_cmpuint (lrg_notification_get_id (n), ==, 1);
	g_assert_cmpuint (f->srv.n_notify, ==, 1);

	g_variant_get (f->srv.last_notify, "(&su&s&s&s@as@a{sv}i)", &app, &replaces, &icon, &summary, &body,
	               &actions, &hints, &timeout);
	g_assert_cmpstr (app, ==, "Chrome Test");
	g_assert_cmpuint (replaces, ==, 0);
	g_assert_cmpstr (icon, ==, "mail-unread");
	g_assert_cmpstr (summary, ==, "New mail");
	g_assert_cmpstr (body, ==, "3 unread messages");
	g_assert_cmpint (timeout, ==, 5000);

	keys = g_variant_get_strv (actions, NULL);
	g_assert_cmpuint (g_strv_length ((gchar **) keys), ==, 6);
	g_assert_cmpstr (keys[0], ==, "default");
	g_assert_cmpstr (keys[2], ==, "lrg-button-0");
	g_assert_cmpstr (keys[3], ==, "Open inbox");
	g_assert_cmpstr (keys[4], ==, "lrg-button-1");
	g_assert_cmpstr (keys[5], ==, "Archive");

	g_assert_true (g_variant_lookup (hints, "urgency", "y", &urgency));
	g_assert_cmpuint (urgency, ==, 2);
	g_assert_true (g_variant_lookup (hints, "desktop-entry", "&s", &entry));
	g_assert_cmpstr (entry, ==, "org.example.ChromeTest");

	lrg_notification_withdraw (n);
	WAIT_FOR (f->srv.n_close == 1);
	g_assert_cmpuint (f->srv.last_close_id, ==, 1);
	g_assert_cmpint (f->closed, ==, 1);
	g_assert_cmpint (f->close_reason, ==, LRG_NOTIFICATION_CLOSE_REASON_CLOSED);
	g_assert_cmpint (lrg_notification_get_backend (n), ==, LRG_NOTIFICATION_BACKEND_NONE);
	g_assert_cmpuint (lrg_notification_get_id (n), ==, 0);

	/* The server's NotificationClosed for the withdrawn id is ignored */
	drain ();
	g_assert_cmpint (f->closed, ==, 1);
}

static void
test_notify_replace (NotifyFixture *f,
                     gconstpointer  data)
{
	g_autoptr(LrgNotification) n = notification_for (f);
	g_autoptr(LrgNotification) other = lrg_notification_new ("Other");
	guint32                    replaces = 0;

	fake_start (&f->srv, f->bus);
	g_assert_true (show_and_wait (n, f->anchor, NULL));
	g_assert_true (show_and_wait (other, f->anchor, NULL));
	g_assert_cmpuint (lrg_notification_get_id (other), ==, 2);

	/* Updating replaces the same id */
	lrg_notification_set_body (n, "4 unread messages");
	g_assert_true (show_and_wait (n, f->anchor, NULL));
	g_assert_cmpuint (f->srv.n_notify, ==, 3);
	g_variant_get_child (f->srv.last_notify, 1, "u", &replaces);
	g_assert_cmpuint (replaces, ==, 1);
	g_assert_cmpuint (lrg_notification_get_id (n), ==, 1);

	/* Two shows back to back: the second waits and still replaces */
	lrg_notification_set_body (n, "5");
	g_assert_true (lrg_notification_show (n, f->anchor, NULL));
	lrg_notification_set_body (n, "6");
	g_assert_true (lrg_notification_show (n, f->anchor, NULL));
	WAIT_FOR (f->srv.n_notify == 5);
	g_variant_get_child (f->srv.last_notify, 1, "u", &replaces);
	g_assert_cmpuint (replaces, ==, 1);
	g_assert_cmpuint (f->srv.next_id, ==, 2);

	lrg_notification_withdraw (n);
	lrg_notification_withdraw (other);
	WAIT_FOR (f->srv.n_close == 2);
}

static void
test_notify_actions (NotifyFixture *f,
                     gconstpointer  data)
{
	g_autoptr(LrgNotification) n = notification_for (f);
	guint                      id;

	fake_start (&f->srv, f->bus);
	g_assert_true (show_and_wait (n, f->anchor, NULL));
	id = lrg_notification_get_id (n);

	/* A button: its action runs on the anchor (resolved up to the canvas) */
	fake_emit (&f->srv, "ActionInvoked", g_variant_new ("(us)", id, "lrg-button-0"));
	WAIT_FOR (f->opens == 1);
	g_assert_cmpstr (f->open_target, ==, "inbox");
	g_assert_cmpstr (f->invoked, ==, "test.open::inbox");
	g_assert_cmpint (f->activated, ==, 0);

	fake_emit (&f->srv, "ActionInvoked", g_variant_new ("(us)", id, "lrg-button-1"));
	WAIT_FOR (f->opens == 2);
	g_assert_cmpstr (f->open_target, ==, "archive");

	/* The default action */
	fake_emit (&f->srv, "ActionInvoked", g_variant_new ("(us)", id, "default"));
	WAIT_FOR (f->activated == 1);
	g_assert_cmpint (f->shows, ==, 1);
	g_assert_cmpstr (f->invoked, ==, "test.show");

	/* Other ids and unknown keys are ignored */
	fake_emit (&f->srv, "ActionInvoked", g_variant_new ("(us)", id + 7, "default"));
	fake_emit (&f->srv, "ActionInvoked", g_variant_new ("(us)", id, "lrg-button-9"));
	fake_emit (&f->srv, "ActionInvoked", g_variant_new ("(us)", id, "default"));
	WAIT_FOR (f->activated == 2);
	g_assert_cmpint (f->shows, ==, 2);
	g_assert_cmpint (f->opens, ==, 2);

	lrg_notification_withdraw (n);
	WAIT_FOR (f->srv.n_close == 1);
}

static void
test_notify_closed_reasons (NotifyFixture *f,
                            gconstpointer  data)
{
	LrgNotification *n = notification_for (f);
	GWeakRef         weak;
	guint            reason;

	fake_start (&f->srv, f->bus);
	g_weak_ref_init (&weak, n);

	for (reason = 1; reason <= 4; reason++)
	{
		guint id;

		g_assert_true (show_and_wait (n, f->anchor, NULL));
		id = lrg_notification_get_id (n);
		g_assert_cmpuint (id, !=, 0);
		fake_emit (&f->srv, "NotificationClosed", g_variant_new ("(uu)", id, reason));
		WAIT_FOR (f->closed == (gint) reason);
		g_assert_cmpint (f->close_reason, ==, (gint) reason);
		g_assert_cmpint (lrg_notification_get_backend (n), ==, LRG_NOTIFICATION_BACKEND_NONE);
	}

	/* Out-of-range reasons are reported as undefined */
	g_assert_true (show_and_wait (n, f->anchor, NULL));
	fake_emit (&f->srv, "NotificationClosed", g_variant_new ("(uu)", lrg_notification_get_id (n), 42u));
	WAIT_FOR (f->closed == 5);
	g_assert_cmpint (f->close_reason, ==, LRG_NOTIFICATION_CLOSE_REASON_UNDEFINED);

	/* Fire and forget: the notification lives while shown, then goes */
	g_assert_true (show_and_wait (n, f->anchor, NULL));
	g_object_unref (n);
	n = g_weak_ref_get (&weak);
	g_assert_nonnull (n);
	fake_emit (&f->srv, "ActionInvoked", g_variant_new ("(us)", lrg_notification_get_id (n), "default"));
	fake_emit (&f->srv, "NotificationClosed", g_variant_new ("(uu)", lrg_notification_get_id (n), 2u));
	g_object_unref (n);
	WAIT_FOR (f->closed == 6);
	g_assert_cmpint (f->activated, ==, 1);
	n = g_weak_ref_get (&weak);
	g_assert_null (n);
	g_weak_ref_clear (&weak);
}

static void
test_notify_fallback_toast (NotifyFixture *f,
                            gconstpointer  data)
{
	g_autoptr(LrgNotification) n = notification_for (f);
	g_autoptr(GError)          error = NULL;
	LrgToast                  *toast;
	gfloat                     cx, cy;
	guint                      i;

	/* No server, no queue: an error */
	g_assert_false (show_and_wait (n, f->anchor, &error));
	g_assert_error (error, LRG_NOTIFICATION_ERROR, LRG_NOTIFICATION_ERROR_NO_BACKEND);
	g_assert_cmpint (lrg_notification_get_backend (n), ==, LRG_NOTIFICATION_BACKEND_NONE);

	/* No server, a default queue: a toast */
	lrg_notification_set_default_toast_queue (f->queue);
	g_assert_true (lrg_notification_get_default_toast_queue () == f->queue);
	g_assert_true (show_and_wait (n, NULL, NULL));
	g_assert_cmpint (lrg_notification_get_backend (n), ==, LRG_NOTIFICATION_BACKEND_TOAST);
	g_assert_cmpuint (lrg_toast_queue_get_n_visible (f->queue) + lrg_toast_queue_get_n_pending (f->queue), ==, 1);
	toast = lrg_notification_get_toast (n);
	g_assert_nonnull (toast);
	g_assert_cmpstr (lrg_toast_get_title (toast), ==, "New mail");
	g_assert_cmpint (lrg_toast_get_kind (toast), ==, LRG_TOAST_KIND_DANGER);
	g_assert_cmpfloat (lrg_toast_get_duration (toast), ==, 5.0);

	/* Updating changes the toast in place */
	lrg_notification_set_body (n, "4 unread");
	g_assert_true (show_and_wait (n, NULL, NULL));
	g_assert_true (lrg_notification_get_toast (n) == toast);
	g_assert_cmpstr (lrg_toast_get_message (toast), ==, "4 unread");

	/* Clicking the toast runs the default action (anchor: the queue) */
	frames (f->canvas, 60);
	lrg_canvas_record (f->canvas);
	cx = lrg_widget_get_world_x (LRG_WIDGET (toast)) + lrg_widget_get_width (LRG_WIDGET (toast)) * 0.5f;
	cy = lrg_widget_get_world_y (LRG_WIDGET (toast)) + lrg_widget_get_height (LRG_WIDGET (toast)) * 0.5f;
	click_at (f->canvas, cx, cy);
	g_assert_cmpint (f->shows, ==, 1);
	g_assert_cmpint (f->activated, ==, 1);
	g_assert_cmpint (f->closed, ==, 1);
	g_assert_cmpint (f->close_reason, ==, LRG_NOTIFICATION_CLOSE_REASON_DISMISSED);
	g_assert_cmpint (lrg_notification_get_backend (n), ==, LRG_NOTIFICATION_BACKEND_NONE);

	/* Expiry: a short timeout runs out on the canvas clock */
	lrg_notification_set_timeout (n, 500);
	move_to (f->canvas, -10.0f, -10.0f);
	g_assert_true (show_and_wait (n, f->anchor, NULL));
	for (i = 0; i < 120 && f->closed < 2; i++)
		lrg_canvas_update (f->canvas, DT);
	g_assert_cmpint (f->closed, ==, 2);
	g_assert_cmpint (f->close_reason, ==, LRG_NOTIFICATION_CLOSE_REASON_EXPIRED);

	/* Withdraw */
	g_assert_true (show_and_wait (n, f->anchor, NULL));
	lrg_notification_withdraw (n);
	g_assert_cmpint (f->closed, ==, 3);
	g_assert_cmpint (f->close_reason, ==, LRG_NOTIFICATION_CLOSE_REASON_CLOSED);
	g_assert_null (lrg_notification_get_toast (n));

	/* A cleared queue: undefined */
	g_assert_true (show_and_wait (n, f->anchor, NULL));
	lrg_toast_queue_clear (f->queue);
	g_assert_cmpint (f->closed, ==, 4);
	g_assert_cmpint (f->close_reason, ==, LRG_NOTIFICATION_CLOSE_REASON_UNDEFINED);
}

/* Toast buttons in the in-app fallback: each notification button shows
 * as a toast button, a click runs its action once (on the anchor) and
 * closes the notification as DISMISSED without the default action. */
static void
test_notify_fallback_buttons (NotifyFixture *f,
                              gconstpointer  data)
{
	g_autoptr(LrgNotification) n = notification_for (f);
	LrgToast                  *toast;
	gfloat                     bx, by, bw, bh;

	lrg_notification_set_toast_queue (n, f->queue);
	g_assert_true (show_and_wait (n, f->anchor, NULL));
	toast = lrg_notification_get_toast (n);
	g_assert_nonnull (toast);
	g_assert_cmpuint (lrg_toast_get_n_buttons (toast), ==, 2);
	g_assert_cmpstr (lrg_toast_get_button_label (toast, 0), ==, "Open inbox");
	g_assert_cmpstr (lrg_toast_get_button_label (toast, 1), ==, "Archive");
	g_assert_null (lrg_toast_get_button_action (toast, 0));

	/* Click "Archive" */
	frames (f->canvas, 60);
	lrg_canvas_record (f->canvas);
	g_assert_true (lrg_toast_get_button_rect (toast, 1, &bx, &by, &bw, &bh));
	g_assert_cmpfloat (bw, >, 0.0f);
	click_at (f->canvas, bx + bw * 0.5f, by + bh * 0.5f);
	g_assert_cmpint (f->opens, ==, 1);
	g_assert_cmpstr (f->open_target, ==, "archive");
	g_assert_cmpstr (f->invoked, ==, "test.open('archive')");
	g_assert_cmpint (f->activated, ==, 0);
	g_assert_cmpint (f->shows, ==, 0);
	g_assert_cmpint (f->closed, ==, 1);
	g_assert_cmpint (f->close_reason, ==, LRG_NOTIFICATION_CLOSE_REASON_DISMISSED);
}

static void
count_activate (gint *count)
{
	(*count)++;
}

static void
on_toast_button (LrgToast    *toast,
                 guint        index,
                 const gchar *action,
                 gpointer     user_data)
{
	gint *hits = user_data;

	hits[0]++;
	hits[1] = (gint) index;
}

/* LrgToast on its own: buttons sit in the bottom row, a button click
 * runs its action and reports BUTTON; a card click runs the default
 * action and reports CLICKED; dismiss() and expiry report their own
 * reasons. */
/* A breadcrumb as the title widget gets its natural width when there is
 * room, so it does not collapse to "first ... last" */
static void
test_header_title_breadcrumb (void)
{
	g_autoptr(LrgCanvas) canvas = lrg_canvas_new ();
	LrgHeaderBar        *bar = lrg_header_bar_new ();
	LrgBreadcrumb       *crumbs = lrg_breadcrumb_new ();

	lrg_breadcrumb_append (crumbs, "Showcase");
	lrg_breadcrumb_append (crumbs, "Cards");
	lrg_breadcrumb_append (crumbs, "Elevation");
	lrg_header_bar_set_title_widget (bar, LRG_WIDGET (crumbs));
	g_object_unref (crumbs);
	lrg_canvas_set_paint_background (canvas, FALSE);
	lrg_widget_set_size (LRG_WIDGET (canvas), 980.0f, 600.0f);
	place (canvas, bar, 0.0f, 0.0f, 980.0f, 48.0f);
	frames (canvas, 3);
	lrg_canvas_record (canvas);
	g_assert_false (lrg_breadcrumb_get_collapsed (crumbs));
}

static void
test_toast_buttons (void)
{
	g_autoptr(LrgCanvas)          canvas = lrg_canvas_new ();
	g_autoptr(GSimpleActionGroup) group = g_simple_action_group_new ();
	g_autoptr(GSimpleAction)      undo = g_simple_action_new ("undo", NULL);
	g_autoptr(GSimpleAction)      show = g_simple_action_new ("show", NULL);
	LrgToastQueue                *queue = lrg_toast_queue_new ();
	LrgToast                     *toast;
	gint                          undos = 0;
	gint                          shows = 0;
	gint                          hits[2] = { 0, -1 };
	gfloat                        bx, by, bw, bh, h_plain;
	g_autoptr(LrgToast)           plain = lrg_toast_new ("Saved", "level.rlevel");

	g_signal_connect_swapped (undo, "activate", G_CALLBACK (count_activate), &undos);
	g_signal_connect_swapped (show, "activate", G_CALLBACK (count_activate), &shows);
	g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (undo));
	g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (show));
	lrg_widget_insert_action_group (LRG_WIDGET (canvas), "t", G_ACTION_GROUP (group));
	lrg_widget_set_size (LRG_WIDGET (canvas), 800.0f, 600.0f);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (queue));

	/* Buttons make the card taller */
	lrg_widget_get_preferred_size (LRG_WIDGET (plain), NULL, &h_plain);
	toast = lrg_toast_new ("Deleted", "3 nodes");
	g_assert_cmpuint (lrg_toast_add_button (toast, "Undo", "t.undo"), ==, 0);
	g_assert_cmpuint (lrg_toast_add_button (toast, "Details", NULL), ==, 1);
	g_assert_cmpuint (lrg_toast_get_n_buttons (toast), ==, 2);
	g_assert_cmpstr (lrg_toast_get_button_action (toast, 0), ==, "t.undo");
	g_assert_null (lrg_toast_get_button_label (toast, 5));
	{
		gfloat h_buttons;

		lrg_widget_get_preferred_size (LRG_WIDGET (toast), NULL, &h_buttons);
		g_assert_cmpfloat (h_buttons, >, h_plain);
	}
	g_signal_connect (toast, "button-clicked", G_CALLBACK (on_toast_button), hits);
	lrg_toast_queue_show (queue, toast);
	frames (canvas, 60);
	lrg_canvas_record (canvas);

	/* Undo is left of Details, both inside the card's bottom half */
	{
		gfloat dx;

		g_assert_true (lrg_toast_get_button_rect (toast, 0, &bx, &by, &bw, &bh));
		g_assert_true (lrg_toast_get_button_rect (toast, 1, &dx, NULL, NULL, NULL));
		g_assert_cmpfloat (bx, <, dx);
		g_assert_cmpfloat (by, >, lrg_widget_get_world_y (LRG_WIDGET (toast)) +
		                   lrg_widget_get_height (LRG_WIDGET (toast)) * 0.5f);
		g_assert_false (lrg_toast_get_button_rect (toast, 2, NULL, NULL, NULL, NULL));
	}
	g_assert_cmpint (lrg_toast_get_dismiss_reason (toast), ==, LRG_TOAST_DISMISS_REASON_NONE);

	/* Pressing a button and releasing off it does nothing */
	press_at (canvas, bx + bw * 0.5f, by + bh * 0.5f);
	release_at (canvas, bx - 40.0f, by + bh * 0.5f);
	g_assert_cmpint (undos, ==, 0);
	g_assert_false (lrg_toast_get_dismissed (toast));

	/* A real click runs the action once and reports BUTTON */
	click_at (canvas, bx + bw * 0.5f, by + bh * 0.5f);
	g_assert_cmpint (undos, ==, 1);
	g_assert_cmpint (hits[0], ==, 1);
	g_assert_cmpint (hits[1], ==, 0);
	g_assert_cmpint (lrg_toast_get_dismiss_reason (toast), ==, LRG_TOAST_DISMISS_REASON_BUTTON);

	/* A click on the card runs the default action and reports CLICKED */
	lrg_toast_queue_clear (queue);
	toast = lrg_toast_new ("Hint", "Click me");
	lrg_toast_set_default_action (toast, "t.show");
	g_assert_cmpstr (lrg_toast_get_default_action (toast), ==, "t.show");
	lrg_toast_queue_show (queue, toast);
	frames (canvas, 60);
	lrg_canvas_record (canvas);
	click_at (canvas, lrg_widget_get_world_x (LRG_WIDGET (toast)) + 30.0f,
	          lrg_widget_get_world_y (LRG_WIDGET (toast)) + 10.0f);
	g_assert_cmpint (shows, ==, 1);
	g_assert_cmpint (lrg_toast_get_dismiss_reason (toast), ==, LRG_TOAST_DISMISS_REASON_CLICKED);

	/* dismiss() and expiry */
	g_clear_object (&plain);
	plain = lrg_toast_new ("A", NULL);
	lrg_toast_dismiss (plain);
	g_assert_cmpint (lrg_toast_get_dismiss_reason (plain), ==, LRG_TOAST_DISMISS_REASON_PROGRAMMATIC);
	g_clear_object (&plain);
	plain = lrg_toast_new ("B", NULL);
	lrg_toast_set_duration (plain, 1.0);
	lrg_toast_advance (plain, 2.0);
	g_assert_cmpint (lrg_toast_get_dismiss_reason (plain), ==, LRG_TOAST_DISMISS_REASON_EXPIRED);

	/* Clearing buttons shrinks the card back */
	lrg_toast_add_button (plain, "X", NULL);
	lrg_toast_clear_buttons (plain);
	g_assert_cmpuint (lrg_toast_get_n_buttons (plain), ==, 0);
}

static void
test_notify_in_app (NotifyFixture *f,
                    gconstpointer  data)
{
	g_autoptr(LrgNotification) n = notification_for (f);
	g_autoptr(GError)          error = NULL;

	fake_start (&f->srv, f->bus);

	/* Forced in-app without a queue fails right away */
	lrg_notification_set_in_app (n, TRUE);
	g_assert_false (lrg_notification_show (n, f->anchor, &error));
	g_assert_error (error, LRG_NOTIFICATION_ERROR, LRG_NOTIFICATION_ERROR_NO_BACKEND);

	/* With a queue of its own: a toast, synchronously, server untouched */
	lrg_notification_set_toast_queue (n, f->queue);
	g_assert_true (lrg_notification_show (n, f->anchor, NULL));
	g_assert_cmpint (lrg_notification_get_backend (n), ==, LRG_NOTIFICATION_BACKEND_TOAST);
	drain ();
	g_assert_cmpuint (f->srv.n_notify, ==, 0);
	lrg_notification_withdraw (n);

	/* The global switch does the same */
	lrg_notification_set_in_app (n, FALSE);
	lrg_notification_set_force_in_app (TRUE);
	g_assert_true (lrg_notification_get_force_in_app ());
	g_assert_true (show_and_wait (n, f->anchor, NULL));
	g_assert_cmpint (lrg_notification_get_backend (n), ==, LRG_NOTIFICATION_BACKEND_TOAST);
	g_assert_cmpuint (f->srv.n_notify, ==, 0);
	lrg_notification_withdraw (n);

	/* Off again: the server is used */
	lrg_notification_set_force_in_app (FALSE);
	g_assert_true (show_and_wait (n, f->anchor, NULL));
	g_assert_cmpint (lrg_notification_get_backend (n), ==, LRG_NOTIFICATION_BACKEND_DBUS);
	lrg_notification_withdraw (n);
	WAIT_FOR (f->srv.n_close == 1);
}

static void
test_notify_properties (void)
{
	g_autoptr(LrgNotification) n = NULL;
	g_autofree gchar          *title = NULL;
	gint                       urgency = 0;
	gint                       timeout = 0;
	const gchar               *label = NULL;
	const gchar               *action = NULL;

	n = g_object_new (LRG_TYPE_NOTIFICATION, "title", "T", "urgency", LRG_NOTIFICATION_URGENCY_LOW,
	                  "timeout", 1500, "default-action", "app.quit", NULL);
	g_object_get (n, "title", &title, "urgency", &urgency, "timeout", &timeout, NULL);
	g_assert_cmpstr (title, ==, "T");
	g_assert_cmpint (urgency, ==, LRG_NOTIFICATION_URGENCY_LOW);
	g_assert_cmpint (timeout, ==, 1500);
	g_assert_cmpstr (lrg_notification_get_default_action (n), ==, "app.quit");

	lrg_notification_add_button (n, "A", "app.a");
	g_assert_cmpuint (lrg_notification_get_n_buttons (n), ==, 1);
	g_assert_true (lrg_notification_get_button (n, 0, &label, &action));
	g_assert_cmpstr (label, ==, "A");
	g_assert_cmpstr (action, ==, "app.a");
	g_assert_false (lrg_notification_get_button (n, 1, NULL, NULL));
	lrg_notification_clear_buttons (n);
	g_assert_cmpuint (lrg_notification_get_n_buttons (n), ==, 0);

	/* Withdrawing something never shown is harmless */
	lrg_notification_withdraw (n);
	g_assert_cmpint (lrg_notification_get_backend (n), ==, LRG_NOTIFICATION_BACKEND_NONE);

	/* Enum types are registered for the loader */
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_NOTIFICATION_URGENCY));
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_NOTIFICATION_CLOSE_REASON));
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_NOTIFICATION_BACKEND));
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_HEADER_BAR_BUTTON));
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_PAGINATION_ITEM));
}

/* ==========================================================================
 * GPU: pixels of the header bar and pagination in every preset
 * ========================================================================== */

#define GPU_W (800)
#define GPU_H (200)


static gboolean
init_graphics_context (void)
{
	const gchar *display = g_getenv ("DISPLAY");
	const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

	if ((display == NULL || display[0] == '\0') && (wayland == NULL || wayland[0] == '\0'))
		return FALSE;

	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (8, 8, "lrg-ui-chrome-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

static Image
render_canvas (LrgCanvas *canvas)
{
	RenderTexture2D target = LoadRenderTexture (GPU_W, GPU_H);
	Image           image;

	BeginTextureMode (target);
	ClearBackground ((Color){ 0, 0, 0, 255 });
	lrg_canvas_render (canvas);
	EndTextureMode ();

	image = LoadImageFromTexture (target.texture);
	ImageFlipVertical (&image);
	UnloadRenderTexture (target);
	return image;
}

static gboolean
pixel_near (Image          *image,
            gint            x,
            gint            y,
            const GrlColor *c,
            gint            tol)
{
	Color p = GetImageColor (*image, x, y);

	return ABS ((gint) p.r - (gint) c->r) <= tol &&
	       ABS ((gint) p.g - (gint) c->g) <= tol &&
	       ABS ((gint) p.b - (gint) c->b) <= tol;
}

/*
 * The bar fills with PANEL, the hovered close button turns DANGER and
 * the pagination pill is ACCENT. Set CHROME_SNAPSHOT_DIR to also write
 * one PNG per preset for a visual check.
 */
static void
test_gpu_chrome (void)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	const gchar         *snap_dir = g_getenv ("CHROME_SNAPSHOT_DIR");
	guint                p;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}

	for (p = 0; presets[p] != NULL; p++)
	{
		g_autoptr(LrgCanvas) canvas = canvas_new ();
		g_autoptr(LrgTheme)  theme = lrg_theme_new_preset (presets[p]);
		LrgHeaderBar        *bar = lrg_header_bar_new ();
		LrgPagination       *pag;
		Image                image;
		FakeWindow           win;
		gfloat               cx, cy;
		gfloat               px, py, pw, ph;
		guint                i;

		memset (&win, 0, sizeof (win));
		lrg_widget_set_size (LRG_WIDGET (canvas), (gfloat) GPU_W, (gfloat) GPU_H);
		lrg_canvas_set_paint_background (canvas, TRUE);
		lrg_widget_set_theme (LRG_WIDGET (canvas), theme);
		lrg_header_bar_set_title (bar, "Document.txt");
		lrg_header_bar_set_subtitle (bar, "~/notes");
		lrg_header_bar_set_window_funcs (bar, &fake_funcs, &win, NULL);
		lrg_header_bar_pack_start (bar, LRG_WIDGET (lrg_button_new ("Back")));
		g_object_unref (lrg_container_get_child (LRG_CONTAINER (bar), 0));
		place (canvas, bar, 0.0f, 0.0f, (gfloat) GPU_W, 48.0f);
		pag = place (canvas, lrg_pagination_new (20), 0.0f, 100.0f, (gfloat) GPU_W, 40.0f);
		lrg_pagination_set_page (pag, 9);

		/* Hover the close button and let everything settle */
		button_center (bar, BCLOSE, &cx, &cy);
		move_to (canvas, cx, cy);
		for (i = 0; i < 240 && lrg_canvas_update (canvas, DT); i++)
			;
		g_assert_true (lrg_pagination_get_page_rect (pag, 9, &px, &py, &pw, &ph));

		image = render_canvas (canvas);
		if (snap_dir != NULL)
		{
			g_autofree gchar *name = g_strdup_printf ("chrome-%s.png", presets[p]);
			g_autofree gchar *path = g_build_filename (snap_dir, name, NULL);

			ExportImage (image, path);
		}

		/* Empty bar space (between the title and the buttons) */
		g_assert_true (pixel_near (&image, 600, 6, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL), 12));
		/* Close button disc, off the glyph */
		g_assert_true (pixel_near (&image, (gint) cx, (gint) (cy - 8.0f),
		                           lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER), 24));
		/* The pill under page 10, off the label */
		g_assert_true (pixel_near (&image, (gint) (px + 3.0f), (gint) (py + ph * 0.5f),
		                           lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 24));
		UnloadImage (image);
	}
}

/* ==========================================================================
 * main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	/* The notification tests talk to a fake org.freedesktop.Notifications
	 * on a private GTestDBus bus. Nothing may reach the user's session bus
	 * first: GIO's gvfs module would open it (and GLib caches that
	 * connection as the session bus), sending real desktop notifications
	 * and failing the id checks. So: local VFS, and a session bus address
	 * that leads nowhere until a fixture's GTestDBus replaces it
	 * (g_test_dbus_unset () would also drop XDG_RUNTIME_DIR and DISPLAY,
	 * which the window needs). */
	g_setenv ("GIO_USE_VFS", "local", TRUE);
	g_setenv ("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/lrg-test-no-session-bus", TRUE);
	g_unsetenv ("DBUS_STARTER_ADDRESS");
	g_unsetenv ("DBUS_STARTER_BUS_TYPE");
	g_test_init (&argc, &argv, NULL);

	/* GTestDBus forks its watcher process on the first g_test_dbus_up ().
	 * Fork it now, before the GL context starts Mesa's worker threads: a
	 * watcher forked after that inherits Mesa's atexit handler, and when
	 * it exits with the test it deadlocks joining threads that do not
	 * exist in it, leaving one stuck process behind per run. */
	{
		static const gchar *keep[] = { "DISPLAY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR" };
		g_autoptr(GTestDBus) warm = g_test_dbus_new (G_TEST_DBUS_NONE);
		gchar               *saved[G_N_ELEMENTS (keep)];
		guint                i;

		/* g_test_dbus_down () unsets these too; the window needs them */
		for (i = 0; i < G_N_ELEMENTS (keep); i++)
			saved[i] = g_strdup (g_getenv (keep[i]));
		g_test_dbus_up (warm);
		g_test_dbus_down (warm);
		for (i = 0; i < G_N_ELEMENTS (keep); i++)
		{
			if (saved[i] != NULL)
				g_setenv (keep[i], saved[i], TRUE);
			g_free (saved[i]);
		}
		g_setenv ("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/lrg-test-no-session-bus", TRUE);
	}
	graphics_available = init_graphics_context ();

	g_test_add_func ("/ui/pagination/compute-items", test_pagination_compute_items);
	g_test_add_func ("/ui/pagination/compute-invariants", test_pagination_compute_invariants);
	g_test_add_func ("/ui/pagination/click", test_pagination_click);
	g_test_add_func ("/ui/pagination/keys", test_pagination_keys);
	g_test_add_func ("/ui/pagination/rtl", test_pagination_rtl);
	g_test_add_func ("/ui/pagination/springs", test_pagination_springs);
	g_test_add_func ("/ui/pagination/paint", test_pagination_paint);

	g_test_add_func ("/ui/header-bar/parse-layout", test_header_bar_parse_layout);
	g_test_add ("/ui/header-bar/layout-ltr", BarFixture, NULL, bar_set_up, test_header_bar_layout_ltr, bar_tear_down);
	g_test_add ("/ui/header-bar/title-widget", BarFixture, NULL, bar_set_up, test_header_bar_title_widget,
	            bar_tear_down);
	g_test_add ("/ui/header-bar/layout-rtl", BarFixture, NULL, bar_set_up, test_header_bar_layout_rtl, bar_tear_down);
	g_test_add ("/ui/header-bar/drag-area", BarFixture, NULL, bar_set_up, test_header_bar_drag_area, bar_tear_down);
	g_test_add ("/ui/header-bar/drag", BarFixture, NULL, bar_set_up, test_header_bar_drag, bar_tear_down);
	g_test_add ("/ui/header-bar/double-click", BarFixture, NULL, bar_set_up, test_header_bar_double_click,
	            bar_tear_down);
	g_test_add ("/ui/header-bar/buttons", BarFixture, NULL, bar_set_up, test_header_bar_buttons, bar_tear_down);
	g_test_add ("/ui/header-bar/close-hover", BarFixture, NULL, bar_set_up, test_header_bar_close_hover,
	            bar_tear_down);
	g_test_add_func ("/ui/header-bar/paint-presets", test_header_bar_paint_presets);

	g_test_add_func ("/ui/notification/properties", test_notify_properties);
	g_test_add ("/ui/notification/args", NotifyFixture, NULL, notify_set_up, test_notify_args, notify_tear_down);
	g_test_add ("/ui/notification/replace", NotifyFixture, NULL, notify_set_up, test_notify_replace,
	            notify_tear_down);
	g_test_add ("/ui/notification/actions", NotifyFixture, NULL, notify_set_up, test_notify_actions,
	            notify_tear_down);
	g_test_add ("/ui/notification/closed-reasons", NotifyFixture, NULL, notify_set_up, test_notify_closed_reasons,
	            notify_tear_down);
	g_test_add ("/ui/notification/fallback-toast", NotifyFixture, NULL, notify_set_up, test_notify_fallback_toast,
	            notify_tear_down);
	g_test_add ("/ui/notification/fallback-buttons", NotifyFixture, NULL, notify_set_up, test_notify_fallback_buttons,
	            notify_tear_down);
	g_test_add_func ("/ui/toast/buttons", test_toast_buttons);
	g_test_add_func ("/ui/header-bar/title-breadcrumb", test_header_title_breadcrumb);
	g_test_add ("/ui/notification/in-app", NotifyFixture, NULL, notify_set_up, test_notify_in_app,
	            notify_tear_down);

	g_test_add_func ("/ui/chrome/gpu", test_gpu_chrome);

	{
		gint result = g_test_run ();

		g_clear_object (&test_window);
		return result;
	}
}
