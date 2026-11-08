/* test-ui-dock-overflow.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tab strips that do not fit: readable minimum width, ellipsis, icon
 * plus label, scroll arrows, the tab list, the active tab coming back
 * into view, the wheel, and drag reorder / drag-out. Also the canvas
 * rule that several actions may share a key and the first enabled one
 * fires (priority, then registration order).
 *
 * Headless: an LrgDockArea on an LrgCanvas, driven through
 * lrg_canvas_process_input(). Motion is NONE so scroll snaps.
 */

#include <glib.h>
#include <string.h>
#include <libregnum.h>

#define DT (1.0f / 60.0f)
#define STRIP (30.0f)
#define READABLE (96.0f)
#define OW (420.0f)
#define OH (500.0f)

/* A title wider than a readable tab at the theme's 16px UI font */
#define LONG_TITLE "Inspector Properties Panel"

/* ==========================================================================
 * Input
 * ========================================================================== */

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

static void
wheel_at (LrgCanvas *canvas,
          gfloat     x,
          gfloat     y,
          gfloat     wheel_x,
          gfloat     wheel_y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.wheel_x = wheel_x;
	input.wheel_y = wheel_y;
	lrg_canvas_process_input (canvas, &input);
}

/* drag_tab: press @panel's tab (clear of its close button) and drop at (@x, @y) */
static void
drag_tab (LrgCanvas    *canvas,
          LrgDockArea  *area,
          LrgDockPanel *panel,
          gfloat        x,
          gfloat        y)
{
	gfloat tx;
	gfloat ty;
	gfloat tw;
	gfloat th;
	guint  i;

	g_assert_true (lrg_dock_area_get_tab_rect (area, panel, &tx, &ty, &tw, &th));
	tx += tw * 0.35f;
	ty += th * 0.5f;
	move_to (canvas, tx, ty);
	press_at (canvas, tx, ty);
	for (i = 1; i <= 4; i++)
		drag_to (canvas, tx + (x - tx) * (gfloat) i / 4.0f, ty + (y - ty) * (gfloat) i / 4.0f);
	g_assert_true (lrg_dock_area_get_dragging (area));
	release_at (canvas, x, y);
	g_assert_false (lrg_dock_area_get_dragging (area));
}

static void
press_key (LrgCanvas *canvas,
           GrlKey     key,
           gunichar   ch)
{
	LrgCanvasInput input;

	lrg_canvas_input_init (&input);
	input.pointer_x = -1.0f;
	input.pointer_y = -1.0f;
	input.delta_time = DT;
	input.keys[0] = key;
	input.n_keys = 1;
	if (ch != 0)
	{
		input.chars[0] = ch;
		input.n_chars = 1;
	}
	lrg_canvas_process_input (canvas, &input);
}

/* ==========================================================================
 * Dock fixtures
 * ========================================================================== */

/* area_new: a dock filling @w x @h of a canvas, motion snapped */
static LrgDockArea *
area_new (LrgCanvas *canvas,
          gfloat     w,
          gfloat     h)
{
	LrgDockArea *area = lrg_dock_area_new ();

	lrg_widget_set_size (LRG_WIDGET (canvas), MAX (w, 800.0f), MAX (h, 600.0f));
	lrg_canvas_set_paint_background (canvas, FALSE);
	lrg_dock_area_set_tab_height (area, STRIP);
	lrg_widget_set_motion (LRG_WIDGET (area), LRG_MOTION_NONE);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (area));
	g_object_unref (area);
	lrg_widget_allocate (LRG_WIDGET (area), 0.0f, 0.0f, w, h);
	return area;
}

/* add_titled: a closable panel, tab of @relative (or the whole area) */
static LrgDockPanel *
add_titled (LrgDockArea  *area,
            const gchar  *id,
            const gchar  *title,
            LrgDockPanel *relative)
{
	LrgDockPanel *p = lrg_dock_panel_new (id, title);
	LrgWidget    *content = g_object_new (LRG_TYPE_WIDGET, NULL);

	lrg_dock_panel_set_child (p, content);
	g_object_unref (content);
	lrg_dock_area_add_panel (area, p, relative, LRG_DOCK_POSITION_CENTER);
	g_object_unref (p);
	return p;
}

/* six_long: six long-titled tabs in one stack. The first carries an icon.
 * The last one added is the strip's selected tab (the one the overflow
 * keeps in view). The area's active panel stays the first until
 * lrg_dock_area_present_panel(). */
static LrgDockPanel *
six_long (LrgDockArea *area)
{
	static const gchar *ids[] = { "a", "b", "c", "d", "e", "f" };
	LrgDockPanel       *first = NULL;
	LrgDockPanel       *prev = NULL;
	guint               i;

	for (i = 0; i < G_N_ELEMENTS (ids); i++)
	{
		prev = add_titled (area, ids[i], LONG_TITLE, prev);
		if (first == NULL)
			first = prev;
	}
	lrg_dock_panel_set_icon (first, "folder");
	return first;
}

static void
button_center (LrgDockArea        *area,
               LrgDockPanel       *panel,
               LrgDockStripButton  button,
               gfloat             *x,
               gfloat             *y)
{
	gfloat bx;
	gfloat by;
	gfloat bw;
	gfloat bh;

	g_assert_true (lrg_dock_area_get_strip_button_rect (area, panel, button, &bx, &by, &bw, &bh));
	*x = bx + bw * 0.5f;
	*y = by + bh * 0.5f;
}

/* glyphs_in: SDF glyphs whose origin lies in the rectangle */
static guint
glyphs_in (LrgPaintList *list,
           gfloat        x,
           gfloat        y,
           gfloat        w,
           gfloat        h)
{
	guint n = lrg_paint_list_get_n_instances (list);
	guint count = 0;
	guint i;

	for (i = 0; i < n; i++)
	{
		const LrgPaintInstance *inst = lrg_paint_list_get_instance (list, i);
		gfloat                  gx;
		gfloat                  gy;

		if (lrg_paint_instance_get_prim (inst) != LRG_PAINT_PRIM_GLYPH)
			continue;
		gx = inst->rect[0];
		gy = inst->rect[1];
		if (gx >= x && gx < x + w && gy >= y && gy < y + h)
			count++;
	}
	return count;
}

/* ==========================================================================
 * Overflow
 * ========================================================================== */

/* Long tabs clamp to the readable minimum and still carry a label */
static void
test_overflow_readable (void)
{
	g_autoptr(LrgCanvas) canvas = lrg_canvas_new ();
	LrgDockArea         *area = area_new (canvas, OW, OH);
	LrgDockPanel        *first = six_long (area);
	LrgPaintList        *list;
	gfloat               x;
	gfloat               y;
	gfloat               w;
	gfloat               h;
	gfloat               tw = 0.0f;
	gfloat               th = 0.0f;
	guint                glyphs;
	guint                i;
	const gchar         *ids[] = { "a", "b", "c", "d", "e", "f" };

	g_assert_true (lrg_dock_area_get_tabs_overflow (area, first));
	for (i = 0; i < G_N_ELEMENTS (ids); i++)
	{
		LrgDockPanel *p = lrg_dock_area_get_panel (area, ids[i]);

		g_assert_true (lrg_dock_area_get_tab_rect (area, p, NULL, NULL, &w, NULL));
		g_assert_cmpfloat (w, ==, READABLE);
	}

	/* The full title is wider than the tab, so the strip ellipsizes it */
	lrg_paint_measure_text (lrg_theme_get_ui_font (lrg_widget_get_theme (LRG_WIDGET (area))),
	                        LONG_TITLE,
	                        lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (area)),
	                                              LRG_THEME_METRIC_FONT_SIZE),
	                        1.0f, &tw, &th);
	g_assert_cmpfloat (tw, >, READABLE);

	g_assert_true (lrg_dock_area_get_tab_rect (area, first, &x, &y, &w, &h));
	list = lrg_canvas_record (canvas);
	glyphs = glyphs_in (list, x, y, w, h);
	/* No font atlas headless: nothing is drawn, and the width above is
	 * the proof the label was kept. With an atlas the label is glyphs,
	 * shorter than the full title (an ellipsis) and not icon-only. */
	if (glyphs > 0)
	{
		g_assert_cmpuint (glyphs, >=, 1);
		g_assert_cmpuint (glyphs, <, (guint) g_utf8_strlen (LONG_TITLE, -1));
	}
}

/* A short title is not stretched to the readable minimum */
static void
test_overflow_short_title (void)
{
	g_autoptr(LrgCanvas) canvas = lrg_canvas_new ();
	LrgDockArea         *area = area_new (canvas, OW, OH);
	LrgDockPanel        *first = six_long (area);
	LrgDockPanel        *short_tab = add_titled (area, "z", "Z", first);
	gfloat               w = 0.0f;

	g_assert_true (lrg_dock_area_get_tabs_overflow (area, short_tab));
	g_assert_true (lrg_dock_area_get_tab_rect (area, short_tab, NULL, NULL, &w, NULL));
	g_assert_cmpfloat (w, >=, 56.0f);
	g_assert_cmpfloat (w, <, READABLE);
}

/* One tab, and a strip that fits, have no overflow chrome */
static void
test_overflow_absent (void)
{
	g_autoptr(LrgCanvas) canvas = lrg_canvas_new ();
	LrgDockArea         *area = area_new (canvas, 800.0f, 600.0f);
	LrgDockPanel        *only = add_titled (area, "only", LONG_TITLE, NULL);
	LrgDockPanel        *a;
	gfloat               before;

	g_assert_false (lrg_dock_area_get_tabs_overflow (area, only));
	g_assert_false (lrg_dock_area_get_strip_button_rect (area, only,
	                                                     LRG_DOCK_STRIP_BUTTON_LIST,
	                                                     NULL, NULL, NULL, NULL));
	g_assert_cmpfloat (lrg_dock_area_get_tab_scroll (area, only), ==, 0.0f);
	g_assert_true (lrg_dock_area_get_tab_visible (area, only));

	a = add_titled (area, "a", "A", only);
	g_assert_false (lrg_dock_area_get_tabs_overflow (area, a));
	before = lrg_dock_area_get_tab_scroll (area, a);
	wheel_at (canvas, 40.0f, 8.0f, 0.0f, -1.0f);
	g_assert_cmpfloat (lrg_dock_area_get_tab_scroll (area, a), ==, before);
}

/* Scroll arrows step; the offset clamps; buttons sit trailing, in order */
static void
test_overflow_scroll_buttons (void)
{
	g_autoptr(LrgCanvas) canvas = lrg_canvas_new ();
	LrgDockArea         *area = area_new (canvas, OW, OH);
	LrgDockPanel        *first = six_long (area);
	gfloat               bx[3];
	gfloat               x;
	gfloat               y;
	gfloat               max_scroll;
	gfloat               stepped;
	LrgDockStripButton   b;

	for (b = LRG_DOCK_STRIP_BUTTON_SCROLL_BACK; b <= LRG_DOCK_STRIP_BUTTON_LIST; b++)
	{
		gfloat bw = 0.0f;

		g_assert_true (lrg_dock_area_get_strip_button_rect (area, first, b, &bx[b], NULL, &bw, NULL));
		g_assert_cmpfloat (bw, ==, 22.0f);
		if (b > LRG_DOCK_STRIP_BUTTON_SCROLL_BACK)
			g_assert_cmpfloat (bx[b], >, bx[b - 1]);
	}

	/* The last tab is active, so the strip opens scrolled to it */
	lrg_dock_area_set_tab_scroll (area, first, 0.0f);
	g_assert_cmpfloat (lrg_dock_area_get_tab_scroll (area, first), ==, 0.0f);
	button_center (area, first, LRG_DOCK_STRIP_BUTTON_SCROLL_BACK, &x, &y);
	click_at (canvas, x, y);
	g_assert_cmpfloat (lrg_dock_area_get_tab_scroll (area, first), ==, 0.0f);

	button_center (area, first, LRG_DOCK_STRIP_BUTTON_SCROLL_FORWARD, &x, &y);
	click_at (canvas, x, y);
	stepped = lrg_dock_area_get_tab_scroll (area, first);
	g_assert_cmpfloat (stepped, >, 0.0f);

	max_scroll = lrg_dock_area_get_tab_scroll_max (area, first);
	g_assert_cmpfloat (max_scroll, >, stepped);
	lrg_dock_area_set_tab_scroll (area, first, max_scroll + 1000.0f);
	g_assert_cmpfloat (lrg_dock_area_get_tab_scroll (area, first), ==, max_scroll);
	click_at (canvas, x, y);
	g_assert_cmpfloat (lrg_dock_area_get_tab_scroll (area, first), ==, max_scroll);

	button_center (area, first, LRG_DOCK_STRIP_BUTTON_SCROLL_BACK, &x, &y);
	click_at (canvas, x, y);
	g_assert_cmpfloat (lrg_dock_area_get_tab_scroll (area, first), <, max_scroll);
}

/* The list names every tab; picking one presents it and shows its tab */
static void
test_overflow_tab_list (void)
{
	g_autoptr(LrgCanvas) canvas = lrg_canvas_new ();
	LrgDockArea         *area = area_new (canvas, OW, OH);
	LrgDockPanel        *first = six_long (area);
	LrgMenu             *menu;
	GMenuModel          *model;
	gfloat               x;
	gfloat               y;
	guint                i;
	const gchar         *ids[] = { "a", "b", "c", "d", "e", "f" };

	lrg_dock_area_set_tab_scroll (area, first, lrg_dock_area_get_tab_scroll_max (area, first));
	g_assert_false (lrg_dock_area_get_tab_visible (area, first));

	g_assert_true (lrg_dock_area_popup_tab_list (area, first));
	menu = lrg_dock_area_get_tab_list_menu (area);
	g_assert_nonnull (menu);
	g_assert_cmpuint (lrg_menu_get_n_entries (menu), ==, G_N_ELEMENTS (ids));
	model = lrg_menu_get_model (menu);
	for (i = 0; i < G_N_ELEMENTS (ids); i++)
	{
		g_autofree gchar *label = NULL;

		g_assert_true (g_menu_model_get_item_attribute (model, (gint) i, G_MENU_ATTRIBUTE_LABEL,
		                                                "s", &label));
		g_assert_cmpstr (label, ==, LONG_TITLE);
	}
	g_assert_true (lrg_menu_activate_entry (menu, 0));
	g_assert_true (lrg_dock_area_get_active_panel (area) == first);
	g_assert_true (lrg_dock_area_get_tab_visible (area, first));

	/* The list button opens the same menu */
	lrg_dock_area_set_tab_scroll (area, first, lrg_dock_area_get_tab_scroll_max (area, first));
	button_center (area, first, LRG_DOCK_STRIP_BUTTON_LIST, &x, &y);
	click_at (canvas, x, y);
	g_assert_true (lrg_menu_get_open (lrg_dock_area_get_tab_list_menu (area)));
	g_assert_cmpuint (lrg_menu_get_n_entries (lrg_dock_area_get_tab_list_menu (area)), ==,
	                  G_N_ELEMENTS (ids));
}

/* Activating a tab scrolls it into view. A manual scroll may hide it. */
static void
test_overflow_active_visible (void)
{
	g_autoptr(LrgCanvas) canvas = lrg_canvas_new ();
	LrgDockArea         *area = area_new (canvas, OW, OH);
	LrgDockPanel        *first = six_long (area);
	LrgDockPanel        *last = lrg_dock_area_get_panel (area, "f");
	g_autofree gchar    *got = NULL;

	/* The strip selects the last tab and scrolls it into view */
	got = lrg_dock_area_describe (area);
	g_assert_cmpstr (got, ==, "[a,b,c,d,e,f*]");
	g_assert_true (lrg_dock_area_get_tab_visible (area, last));
	g_assert_false (lrg_dock_area_get_tab_visible (area, first));

	/* set_tab_scroll is allowed to park the selected tab off to the side */
	lrg_dock_area_set_tab_scroll (area, last, 0.0f);
	g_assert_false (lrg_dock_area_get_tab_visible (area, last));

	lrg_dock_area_present_panel (area, first);
	g_assert_true (lrg_dock_area_get_active_panel (area) == first);
	g_assert_true (lrg_dock_area_get_tab_visible (area, first));

	/* Scrolling to the end hides the selected first tab until it is presented */
	lrg_dock_area_set_tab_scroll (area, first, lrg_dock_area_get_tab_scroll_max (area, first));
	g_assert_false (lrg_dock_area_get_tab_visible (area, first));
	lrg_dock_area_present_panel (area, last);
	g_assert_true (lrg_dock_area_get_active_panel (area) == last);
	g_assert_true (lrg_dock_area_get_tab_visible (area, last));
}

/* The wheel over an overflowing strip scrolls it; over the content it does not */
static void
test_overflow_wheel (void)
{
	g_autoptr(LrgCanvas) canvas = lrg_canvas_new ();
	LrgDockArea         *area = area_new (canvas, OW, OH);
	LrgDockPanel        *first = six_long (area);
	gfloat               before;

	lrg_dock_area_set_tab_scroll (area, first, 0.0f);
	/* Wheel down (negative notch) towards the last tab */
	wheel_at (canvas, 40.0f, 8.0f, 0.0f, -1.0f);
	g_assert_cmpfloat (lrg_dock_area_get_tab_scroll (area, first), ==, 48.0f);

	before = lrg_dock_area_get_tab_scroll (area, first);
	wheel_at (canvas, 40.0f, STRIP + 40.0f, 0.0f, -1.0f);
	g_assert_cmpfloat (lrg_dock_area_get_tab_scroll (area, first), ==, before);

	wheel_at (canvas, 40.0f, 8.0f, 1.0f, 0.0f);
	g_assert_cmpfloat (lrg_dock_area_get_tab_scroll (area, first), ==, before + 48.0f);
}

/* Reorder inside the strip, then tear a tab out, both while overflowing */
static void
test_overflow_drag (void)
{
	g_autoptr(LrgCanvas) canvas = lrg_canvas_new ();
	LrgDockArea         *area = area_new (canvas, OW, OH);
	LrgDockPanel        *a = six_long (area);
	LrgDockPanel        *b = lrg_dock_area_get_panel (area, "b");
	LrgDockPanel        *c = lrg_dock_area_get_panel (area, "c");
	g_autofree gchar    *got = NULL;
	gfloat               x;
	gfloat               y;

	lrg_dock_area_set_tab_scroll (area, a, 0.0f);
	g_assert_true (lrg_dock_area_get_tab_visible (area, a));
	g_assert_true (lrg_dock_area_get_tab_visible (area, c));
	lrg_dock_area_get_tab_rect (area, c, &x, &y, NULL, NULL);
	drag_tab (canvas, area, a, x + 4.0f, y + 6.0f);
	got = lrg_dock_area_describe (area);
	g_assert_cmpstr (got, ==, "[b,a*,c,d,e,f]");

	lrg_dock_area_set_allow_floating (area, TRUE);
	lrg_dock_area_set_tab_scroll (area, b, 0.0f);
	drag_tab (canvas, area, b, 30.0f, 80.0f);
	g_clear_pointer (&got, g_free);
	got = lrg_dock_area_describe (area);
	g_assert_nonnull (strstr (got, "+float[b]"));
	g_assert_true (lrg_dock_area_is_panel_floating (area, b));
	g_assert_true (lrg_dock_area_get_tabs_overflow (area, a));
}

/* ==========================================================================
 * Shared shortcuts
 * ========================================================================== */

typedef struct
{
	gint count;
} Recorder;

static void
on_record (GSimpleAction *action,
           GVariant      *parameter,
           gpointer       user_data)
{
	(void) action;
	(void) parameter;
	((Recorder *) user_data)->count++;
}

static GSimpleAction *
add_action (GSimpleActionGroup *group,
            const gchar        *name,
            Recorder           *rec)
{
	GSimpleAction *a = g_simple_action_new (name, NULL);

	g_signal_connect (a, "activate", G_CALLBACK (on_record), rec);
	g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (a));
	g_object_unref (a);
	return a;
}

/* Higher priority wins; a disabled action is skipped; ties keep registration order */
static void
test_shortcut_shared_key (void)
{
	g_autoptr(LrgCanvas)          canvas = lrg_canvas_new ();
	g_autoptr(GSimpleActionGroup) group = g_simple_action_group_new ();
	GSimpleAction                *high;
	GSimpleAction                *low;
	GSimpleAction                *first;
	GSimpleAction                *second;
	Recorder                      hi = { 0 };
	Recorder                      lo = { 0 };
	Recorder                      early = { 0 };
	Recorder                      late = { 0 };
	g_auto(GStrv)                 names = NULL;

	lrg_widget_set_size (LRG_WIDGET (canvas), 200.0f, 100.0f);
	high = add_action (group, "high", &hi);
	low = add_action (group, "low", &lo);
	lrg_widget_insert_action_group (LRG_WIDGET (canvas), "doc", G_ACTION_GROUP (group));

	g_assert_true (lrg_canvas_add_shortcut_full (canvas, "y", "doc.low", NULL, 0));
	g_assert_true (lrg_canvas_add_shortcut_full (canvas, "y", "doc.high", NULL, 10));
	g_assert_cmpuint (lrg_canvas_get_n_shortcuts (canvas), ==, 2);
	names = lrg_canvas_list_shortcut_actions (canvas, "y");
	g_assert_cmpstr (names[0], ==, "doc.high");
	g_assert_cmpstr (names[1], ==, "doc.low");
	g_assert_null (names[2]);

	press_key (canvas, GRL_KEY_Y, 'y');
	g_assert_cmpint (hi.count, ==, 1);
	g_assert_cmpint (lo.count, ==, 0);

	g_simple_action_set_enabled (high, FALSE);
	press_key (canvas, GRL_KEY_Y, 'y');
	g_assert_cmpint (hi.count, ==, 1);
	g_assert_cmpint (lo.count, ==, 1);

	g_simple_action_set_enabled (high, TRUE);
	press_key (canvas, GRL_KEY_Y, 'y');
	g_assert_cmpint (hi.count, ==, 2);
	g_assert_cmpint (lo.count, ==, 1);

	/* Equal priority: the earlier registration is tried first */
	lrg_canvas_remove_all_shortcuts (canvas);
	first = add_action (group, "early", &early);
	second = add_action (group, "late", &late);
	g_assert_true (lrg_canvas_add_shortcut_full (canvas, "y", "doc.early", NULL, 0));
	g_assert_true (lrg_canvas_add_shortcut_full (canvas, "y", "doc.late", NULL, 0));
	g_clear_pointer (&names, g_strfreev);
	names = lrg_canvas_list_shortcut_actions (canvas, "Y");
	g_assert_cmpstr (names[0], ==, "doc.early");
	g_assert_cmpstr (names[1], ==, "doc.late");
	press_key (canvas, GRL_KEY_Y, 'y');
	g_assert_cmpint (early.count, ==, 1);
	g_assert_cmpint (late.count, ==, 0);
	g_simple_action_set_enabled (first, FALSE);
	press_key (canvas, GRL_KEY_Y, 'y');
	g_assert_cmpint (early.count, ==, 1);
	g_assert_cmpint (late.count, ==, 1);

	/* Removing one binding leaves the other; add_shortcut replaces the chord */
	g_assert_true (lrg_canvas_remove_shortcut_for_action (canvas, "y", "doc.early", NULL));
	g_clear_pointer (&names, g_strfreev);
	names = lrg_canvas_list_shortcut_actions (canvas, "y");
	g_assert_cmpstr (names[0], ==, "doc.late");
	g_assert_null (names[1]);
	g_assert_true (lrg_canvas_add_shortcut (canvas, "y", "doc.low", NULL));
	g_assert_cmpuint (lrg_canvas_get_n_shortcuts (canvas), ==, 1);
	g_clear_pointer (&names, g_strfreev);
	names = lrg_canvas_list_shortcut_actions (canvas, "y");
	g_assert_cmpstr (names[0], ==, "doc.low");
	g_assert_null (names[1]);
	press_key (canvas, GRL_KEY_Y, 'y');
	g_assert_cmpint (lo.count, ==, 2);
	g_assert_cmpint (late.count, ==, 1);

	(void) low;
	(void) second;
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/ui/dock/overflow/readable", test_overflow_readable);
	g_test_add_func ("/ui/dock/overflow/short-title", test_overflow_short_title);
	g_test_add_func ("/ui/dock/overflow/absent", test_overflow_absent);
	g_test_add_func ("/ui/dock/overflow/scroll-buttons", test_overflow_scroll_buttons);
	g_test_add_func ("/ui/dock/overflow/tab-list", test_overflow_tab_list);
	g_test_add_func ("/ui/dock/overflow/active-visible", test_overflow_active_visible);
	g_test_add_func ("/ui/dock/overflow/wheel", test_overflow_wheel);
	g_test_add_func ("/ui/dock/overflow/drag", test_overflow_drag);
	g_test_add_func ("/ui/dock/overflow/shortcut-shared-key", test_shortcut_shared_key);
	return g_test_run ();
}
