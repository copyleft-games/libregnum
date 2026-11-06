/* test-ui-motion.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Layout springs, island morphing and touch gestures.
 */

#include <glib.h>
#include <string.h>
#include <libregnum.h>

#define DT (1.0f / 60.0f)

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* A fixed-size block that paints one rect and logs events */
#define TEST_TYPE_BLOCK (test_block_get_type ())
G_DECLARE_FINAL_TYPE (TestBlock, test_block, TEST, BLOCK, LrgWidget)

struct _TestBlock
{
	LrgWidget  parent_instance;
	gfloat     pref;
	GString   *log;          /* not owned */
	gboolean   claim_pan;
	gfloat     panned_y;
	gfloat     flick_vy;
	gfloat     zoom;
};

G_DEFINE_FINAL_TYPE (TestBlock, test_block, LRG_TYPE_WIDGET)

static void
test_block_measure (LrgWidget *w,
                    gfloat    *pw,
                    gfloat    *ph)
{
	*pw = TEST_BLOCK (w)->pref;
	*ph = TEST_BLOCK (w)->pref;
}

static void
test_block_paint (LrgWidget    *w,
                  LrgPaintList *list)
{
	static const GrlColor c = { 200, 0, 0, 255 };

	lrg_paint_list_rect (list, lrg_widget_get_world_x (w), lrg_widget_get_world_y (w),
	                     lrg_widget_get_width (w), lrg_widget_get_height (w), 0.0f, &c);
}

static gboolean
test_block_handle_event (LrgWidget        *w,
                         const LrgUIEvent *e)
{
	TestBlock *self = TEST_BLOCK (w);

	switch (lrg_ui_event_get_event_type (e))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (self->log)
			g_string_append_printf (self->log, "down%d ", lrg_ui_event_get_button (e));
		return TRUE;
	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (self->log)
			g_string_append_printf (self->log, "up%d ", lrg_ui_event_get_button (e));
		return TRUE;
	case LRG_UI_EVENT_MOUSE_MOVE:
		if (self->log && (lrg_widget_get_state_flags (w) & LRG_WIDGET_STATE_PRESSED))
			g_string_append (self->log, "drag ");
		return FALSE;
	case LRG_UI_EVENT_PAN:
		if (!self->claim_pan)
			return FALSE;
		self->panned_y += lrg_ui_event_get_delta_y (e);
		return TRUE;
	case LRG_UI_EVENT_PAN_END:
		self->flick_vy = lrg_ui_event_get_velocity_y (e);
		return TRUE;
	case LRG_UI_EVENT_ZOOM:
		self->zoom *= lrg_ui_event_get_scale (e);
		return TRUE;
	default:
		return FALSE;
	}
}

static void
test_block_class_init (TestBlockClass *klass)
{
	LrgWidgetClass *wc = LRG_WIDGET_CLASS (klass);

	wc->measure = test_block_measure;
	wc->paint = test_block_paint;
	wc->handle_event = test_block_handle_event;
}

static void
test_block_init (TestBlock *self)
{
	self->zoom = 1.0f;
}

static TestBlock *
block_new (gfloat pref)
{
	TestBlock *b = g_object_new (TEST_TYPE_BLOCK, NULL);

	b->pref = pref;
	return b;
}

/* touch_frame: one frame with the given fingers */
static void
touch_frame (LrgCanvas   *canvas,
             guint        n,
             const gfloat xy[])
{
	LrgCanvasInput in;
	guint          i;

	lrg_canvas_input_init (&in);
	in.pointer_x = -100.0f;
	in.pointer_y = -100.0f;
	in.delta_time = DT;
	for (i = 0; i < n; i++)
		lrg_canvas_input_add_touch (&in, (gint) i, xy[i * 2], xy[i * 2 + 1]);
	lrg_canvas_process_input (canvas, &in);
}

static LrgCanvas *
canvas_new (void)
{
	LrgCanvas *c = lrg_canvas_new ();

	lrg_widget_set_size (LRG_WIDGET (c), 800.0f, 600.0f);
	return c;
}

/* ==========================================================================
 * Layout springs
 * ========================================================================== */

static void
test_layout_glide (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgVBox             *box = lrg_vbox_new ();
	TestBlock           *a = block_new (20.0f);
	TestBlock           *b = block_new (20.0f);
	LrgPaintList        *list;
	gfloat               painted_y;
	gint                 i;

	lrg_widget_set_size (LRG_WIDGET (box), 200.0f, 300.0f);
	lrg_widget_set_animate_layout (LRG_WIDGET (b), TRUE);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (a));
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (b));
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (box));
	lrg_canvas_record (canvas);
	g_assert_true (lrg_widget_get_animate_layout (LRG_WIDGET (b)));

	/* First placement never animates */
	list = lrg_canvas_record (canvas);
	g_assert_cmpfloat (lrg_paint_list_get_instance (list, 1)->rect[1], ==, 20.0f);

	/* Growing a moves b down: b is laid out there at once but painted
	 * gliding from its old spot */
	a->pref = 120.0f;
	lrg_widget_queue_layout (LRG_WIDGET (a));
	lrg_canvas_record (canvas);
	g_assert_cmpfloat (lrg_widget_get_y (LRG_WIDGET (b)), ==, 120.0f);
	lrg_canvas_update (canvas, DT);
	list = lrg_canvas_record (canvas);
	painted_y = lrg_paint_list_get_instance (list, 1)->rect[1];
	g_assert_cmpfloat (painted_y, >, 20.0f);
	g_assert_cmpfloat (painted_y, <, 120.0f);

	/* Hit-testing already uses the new place */
	g_assert_true (lrg_canvas_widget_at_point (canvas, 10.0f, 125.0f) == LRG_WIDGET (b));

	for (i = 0; i < 120 && lrg_canvas_update (canvas, DT); i++)
		;
	list = lrg_canvas_record (canvas);
	g_assert_cmpfloat (lrg_paint_list_get_instance (list, 1)->rect[1], ==, 120.0f);
	g_assert_false (lrg_canvas_update (canvas, DT));

	g_object_unref (box);
	g_object_unref (a);
	g_object_unref (b);
}

/* ==========================================================================
 * Island
 * ========================================================================== */

static void
test_island_morph (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgIsland           *island = lrg_island_new ();
	TestBlock           *small = block_new (16.0f);
	TestBlock           *big = block_new (200.0f);
	gfloat               w0;
	gfloat               mid_w;
	gboolean             saw_both_hidden_overlap = FALSE;
	gint                 i;

	lrg_island_set_compact_child (island, LRG_WIDGET (small));
	lrg_island_set_expanded_child (island, LRG_WIDGET (big));
	lrg_widget_set_halign (LRG_WIDGET (island), LRG_ALIGN_CENTER);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (island));
	lrg_widget_set_size (LRG_WIDGET (island), 1.0f, 1.0f);

	/* First frame snaps to the compact pill (no animate-in) */
	g_assert_false (lrg_canvas_update (canvas, DT));
	lrg_widget_measure (LRG_WIDGET (island), &w0, NULL);
	g_assert_cmpfloat (w0, ==, 36.0f);
	g_assert_cmpfloat (lrg_island_get_progress (island), ==, 0.0f);
	g_assert_true (lrg_widget_get_visible (LRG_WIDGET (small)));
	g_assert_false (lrg_widget_get_visible (LRG_WIDGET (big)));

	/* Expand: size and morph spring towards the big child */
	lrg_island_toggle (island);
	g_assert_true (lrg_island_get_expanded (island));
	g_assert_true (lrg_widget_get_state_flags (LRG_WIDGET (island)) & LRG_WIDGET_STATE_OPEN);
	for (i = 0; i < 8; i++)
		g_assert_true (lrg_canvas_update (canvas, DT));
	lrg_widget_measure (LRG_WIDGET (island), &mid_w, NULL);
	g_assert_cmpfloat (mid_w, >, w0);
	g_assert_cmpfloat (mid_w, <, 220.0f + 40.0f);

	/* The two contents never show together */
	for (i = 0; i < 300 && lrg_canvas_update (canvas, DT); i++)
	{
		if (lrg_widget_get_visible (LRG_WIDGET (small)) && lrg_widget_get_visible (LRG_WIDGET (big)))
			saw_both_hidden_overlap = TRUE;
	}
	g_assert_false (saw_both_hidden_overlap);
	g_assert_cmpfloat (lrg_island_get_progress (island), ==, 1.0f);
	lrg_widget_measure (LRG_WIDGET (island), &mid_w, NULL);
	g_assert_cmpfloat (mid_w, ==, 220.0f);
	g_assert_true (lrg_widget_get_visible (LRG_WIDGET (big)));
	g_assert_false (lrg_widget_get_visible (LRG_WIDGET (small)));

	g_object_unref (island);
	g_object_unref (small);
	g_object_unref (big);
}

static void
test_island_dark_twin (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	g_autoptr(LrgTheme)  light = lrg_theme_new_preset ("daylight");
	LrgIsland           *island = lrg_island_new ();

	lrg_widget_set_theme (LRG_WIDGET (canvas), light);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (island));
	lrg_canvas_update (canvas, DT);

	/* On a light theme the island's subtree gets a dark twin with the
	 * same accent */
	g_assert_true (lrg_theme_get_dark (lrg_widget_get_theme (LRG_WIDGET (island))));
	g_assert_true (memcmp (lrg_theme_get_color (lrg_widget_get_theme (LRG_WIDGET (island)),
	                                            LRG_THEME_COLOR_ACCENT),
	                       lrg_theme_get_color (light, LRG_THEME_COLOR_ACCENT),
	                       sizeof (GrlColor)) == 0);

	g_object_unref (island);
}

/* ==========================================================================
 * Touch
 * ========================================================================== */

static void
test_touch_tap_is_click (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	g_autoptr(GString)   log = g_string_new (NULL);
	TestBlock           *b = block_new (100.0f);
	gfloat               p[2] = { 50.0f, 50.0f };

	b->log = log;
	lrg_widget_set_size (LRG_WIDGET (b), 100.0f, 100.0f);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (b));

	touch_frame (canvas, 1, p);
	touch_frame (canvas, 1, p);
	touch_frame (canvas, 0, NULL);
	g_assert_cmpstr (log->str, ==, "down0 up0 ");

	g_object_unref (b);
}

static void
test_touch_long_press_is_right_click (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	g_autoptr(GString)   log = g_string_new (NULL);
	TestBlock           *b = block_new (100.0f);
	gfloat               p[2] = { 50.0f, 50.0f };
	gint                 i;

	b->log = log;
	lrg_widget_set_size (LRG_WIDGET (b), 100.0f, 100.0f);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (b));

	for (i = 0; i < 40; i++)
		touch_frame (canvas, 1, p);
	touch_frame (canvas, 0, NULL);
	g_assert_cmpstr (log->str, ==, "down1 up1 ");

	g_object_unref (b);
}

static void
test_touch_pan_and_flick (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	TestBlock           *b = block_new (300.0f);
	gfloat               p[2] = { 50.0f, 200.0f };
	gint                 i;

	b->claim_pan = TRUE;
	lrg_widget_set_size (LRG_WIDGET (b), 300.0f, 300.0f);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (b));

	touch_frame (canvas, 1, p);
	for (i = 0; i < 10; i++)
	{
		p[1] -= 12.0f;
		touch_frame (canvas, 1, p);
	}
	touch_frame (canvas, 0, NULL);

	/* All movement arrived as PAN deltas; the release carries the
	 * upward finger velocity (~12 units per 1/60 s) */
	g_assert_cmpfloat_with_epsilon (b->panned_y, -120.0f, 0.01f);
	g_assert_cmpfloat (b->flick_vy, <, -500.0f);

	g_object_unref (b);
}

static void
test_touch_unclaimed_drag_is_mouse (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	g_autoptr(GString)   log = g_string_new (NULL);
	TestBlock           *b = block_new (300.0f);
	gfloat               p[2] = { 50.0f, 50.0f };
	gint                 i;

	b->log = log;
	lrg_widget_set_size (LRG_WIDGET (b), 300.0f, 300.0f);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (b));

	touch_frame (canvas, 1, p);
	for (i = 0; i < 4; i++)
	{
		p[0] += 10.0f;
		touch_frame (canvas, 1, p);
	}
	touch_frame (canvas, 0, NULL);

	/* No PAN claimer: replayed as press, drags, release */
	g_assert_true (g_str_has_prefix (log->str, "down0 "));
	g_assert_nonnull (strstr (log->str, "drag "));
	g_assert_true (g_str_has_suffix (log->str, "up0 "));

	g_object_unref (b);
}

static void
test_touch_pinch_zoom (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	TestBlock           *b = block_new (400.0f);
	gfloat               p[4] = { 150.0f, 200.0f, 250.0f, 200.0f };
	gint                 i;

	lrg_widget_set_size (LRG_WIDGET (b), 400.0f, 400.0f);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (b));

	/* Fingers 100 apart, spread to 200: total scale 2 */
	touch_frame (canvas, 2, p);
	for (i = 0; i < 10; i++)
	{
		p[0] -= 5.0f;
		p[2] += 5.0f;
		touch_frame (canvas, 2, p);
	}
	touch_frame (canvas, 0, NULL);

	g_assert_cmpfloat_with_epsilon (b->zoom, 2.0f, 0.001f);

	g_object_unref (b);
}

static void
test_touch_scroll_box_pans (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgScrollBox        *scroll = lrg_scroll_box_new ();
	TestBlock           *tall = block_new (2000.0f);
	gfloat               p[2] = { 100.0f, 250.0f };
	gfloat               after_drag;
	gint                 i;

	lrg_widget_set_size (LRG_WIDGET (scroll), 300.0f, 300.0f);
	lrg_scroll_box_set_child (scroll, LRG_WIDGET (tall));
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (scroll));
	lrg_canvas_record (canvas);

	/* Drag up 100: content follows the finger */
	touch_frame (canvas, 1, p);
	for (i = 0; i < 10; i++)
	{
		p[1] -= 10.0f;
		touch_frame (canvas, 1, p);
	}
	after_drag = lrg_scroll_box_get_scroll_y (scroll);
	g_assert_cmpfloat (after_drag, >, 80.0f);

	/* Release with speed: the content coasts further */
	touch_frame (canvas, 0, NULL);
	for (i = 0; i < 120 && lrg_canvas_update (canvas, DT); i++)
		lrg_canvas_record (canvas);
	g_assert_cmpfloat (lrg_scroll_box_get_scroll_y (scroll), >, after_drag + 50.0f);

	g_object_unref (scroll);
	g_object_unref (tall);
}

static void
test_touch_poll_flag (void)
{
	g_assert_false (lrg_canvas_input_get_poll_touch ());
	lrg_canvas_input_set_poll_touch (TRUE);
	g_assert_true (lrg_canvas_input_get_poll_touch ());
	lrg_canvas_input_set_poll_touch (FALSE);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/ui/motion/layout-glide", test_layout_glide);
	g_test_add_func ("/ui/motion/island-morph", test_island_morph);
	g_test_add_func ("/ui/motion/island-dark-twin", test_island_dark_twin);
	g_test_add_func ("/ui/touch/tap", test_touch_tap_is_click);
	g_test_add_func ("/ui/touch/long-press", test_touch_long_press_is_right_click);
	g_test_add_func ("/ui/touch/pan-flick", test_touch_pan_and_flick);
	g_test_add_func ("/ui/touch/mouse-replay", test_touch_unclaimed_drag_is_mouse);
	g_test_add_func ("/ui/touch/pinch", test_touch_pinch_zoom);
	g_test_add_func ("/ui/touch/scroll-box", test_touch_scroll_box_pans);
	g_test_add_func ("/ui/touch/poll-flag", test_touch_poll_flag);

	return g_test_run ();
}
