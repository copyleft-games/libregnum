/* test-ui-dock.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDockArea and LrgDockPanel: tree building, tabs, close requests,
 * dividers, drag and drop docking through the drop compass, Escape,
 * collapsing, floating frames and YAML layout persistence.
 *
 * Headless: the area sits on a canvas and is driven through
 * lrg_canvas_process_input() like the real input loop, stepped at
 * 60 Hz. lrg_dock_area_describe() gives the tree as text:
 * "H(ratio,a,b)" side by side, "V(ratio,a,b)" stacked, "[x,y*]" a tab
 * stack with its active tab starred. One GPU case renders the area
 * under a hidden window and probes pixels; it skips without a display.
 */

#include <glib.h>
#include <math.h>
#include <libregnum.h>
#include <raylib.h>

#define DT (1.0f / 60.0f)

/* Geometry the tests rely on: 800 x 600 area, 30 unit tab strips,
 * 6 unit dividers. The compass buttons are 32 units with 4 unit gaps
 * (36 between centres), centred on a stack's content area. */
#define AREA_W (800.0f)
#define AREA_H (600.0f)
#define STRIP (30.0f)
#define DIVIDER (6.0f)
#define COMPASS_STEP (36.0f)

/* ==========================================================================
 * Canvas and input helpers
 * ========================================================================== */

static LrgCanvas *
canvas_new (void)
{
	LrgCanvas *canvas = lrg_canvas_new ();

	lrg_widget_set_size (LRG_WIDGET (canvas), AREA_W, AREA_H);
	lrg_canvas_set_paint_background (canvas, FALSE);
	return canvas;
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
press_button (LrgCanvas      *canvas,
              gfloat          x,
              gfloat          y,
              GrlMouseButton  button)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (button);
	input.buttons_pressed = LRG_CANVAS_BUTTON_MASK (button);
	lrg_canvas_process_input (canvas, &input);
}

static void
release_button (LrgCanvas      *canvas,
                gfloat          x,
                gfloat          y,
                GrlMouseButton  button)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.buttons_released = LRG_CANVAS_BUTTON_MASK (button);
	lrg_canvas_process_input (canvas, &input);
}

static void
press_at (LrgCanvas *canvas,
          gfloat     x,
          gfloat     y)
{
	press_button (canvas, x, y, GRL_MOUSE_BUTTON_LEFT);
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
	release_button (canvas, x, y, GRL_MOUSE_BUTTON_LEFT);
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

/* key: one frame with a key press (pointer left where it was) */
static void
key_at (LrgCanvas *canvas,
        gfloat     x,
        gfloat     y,
        GrlKey     k)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_input_add_key (&input, k);
	lrg_canvas_process_input (canvas, &input);
}

static void
settle (LrgCanvas *canvas,
        guint      frames)
{
	guint i;

	for (i = 0; i < frames; i++)
		lrg_canvas_update (canvas, DT);
}

/* ==========================================================================
 * Fixtures
 * ========================================================================== */

/* area_new: an empty dock area filling the canvas (canvas owns it) */
static LrgDockArea *
area_new (LrgCanvas *canvas)
{
	LrgDockArea *area = lrg_dock_area_new ();

	lrg_dock_area_set_tab_height (area, STRIP);
	lrg_dock_area_set_divider_size (area, DIVIDER);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (area));
	g_object_unref (area);
	lrg_widget_allocate (LRG_WIDGET (area), 0.0f, 0.0f, AREA_W, AREA_H);
	lrg_canvas_record (canvas);
	return area;
}

/* add: a new panel @id (title = id) docked into @area */
static LrgDockPanel *
add (LrgDockArea     *area,
     const gchar     *id,
     LrgDockPanel    *relative,
     LrgDockPosition  position)
{
	LrgDockPanel *p = lrg_dock_panel_new (id, id);
	LrgWidget    *content = g_object_new (LRG_TYPE_WIDGET, NULL);

	lrg_dock_panel_set_child (p, content);
	g_object_unref (content);
	lrg_dock_area_add_panel (area, p, relative, position);
	g_object_unref (p);
	return p;
}

static void
assert_tree (LrgDockArea *area,
             const gchar *expected)
{
	g_autofree gchar *got = lrg_dock_area_describe (area);

	g_assert_cmpstr (got, ==, expected);
}

static void
assert_stack (LrgDockArea  *area,
              LrgDockPanel *panel,
              gfloat        x,
              gfloat        y,
              gfloat        w,
              gfloat        h)
{
	gfloat sx;
	gfloat sy;
	gfloat sw;
	gfloat sh;

	g_assert_true (lrg_dock_area_get_stack_rect (area, panel, &sx, &sy, &sw, &sh));
	g_assert_cmpfloat (sx, ==, x);
	g_assert_cmpfloat (sy, ==, y);
	g_assert_cmpfloat (sw, ==, w);
	g_assert_cmpfloat (sh, ==, h);
}

/* tab_center: world centre of a panel's tab */
static void
tab_center (LrgDockArea  *area,
            LrgDockPanel *panel,
            gfloat       *x,
            gfloat       *y)
{
	gfloat tx;
	gfloat ty;
	gfloat tw;
	gfloat th;

	g_assert_true (lrg_dock_area_get_tab_rect (area, panel, &tx, &ty, &tw, &th));
	/* Away from the close button (trailing end of the tab) */
	*x = tx + tw * (lrg_widget_is_rtl (LRG_WIDGET (area)) ? 0.65f : 0.35f);
	*y = ty + th * 0.5f;
}

/* compass: world centre of a compass button over @panel's stack */
static void
compass (LrgDockArea     *area,
         LrgDockPanel    *panel,
         LrgDockPosition  position,
         gfloat          *x,
         gfloat          *y)
{
	gfloat sx;
	gfloat sy;
	gfloat sw;
	gfloat sh;

	g_assert_true (lrg_dock_area_get_stack_rect (area, panel, &sx, &sy, &sw, &sh));
	*x = sx + sw * 0.5f;
	*y = sy + STRIP + (sh - STRIP) * 0.5f;
	if (position == LRG_DOCK_POSITION_LEFT)
		*x -= COMPASS_STEP;
	else if (position == LRG_DOCK_POSITION_RIGHT)
		*x += COMPASS_STEP;
	else if (position == LRG_DOCK_POSITION_TOP)
		*y -= COMPASS_STEP;
	else if (position == LRG_DOCK_POSITION_BOTTOM)
		*y += COMPASS_STEP;
}

/* drag_tab: press @panel's tab, drag in steps to (@x, @y), release */
static void
drag_tab (LrgCanvas    *canvas,
          LrgDockArea  *area,
          LrgDockPanel *panel,
          gfloat        x,
          gfloat        y)
{
	gfloat tx;
	gfloat ty;
	guint  i;

	tab_center (area, panel, &tx, &ty);
	move_to (canvas, tx, ty);
	press_at (canvas, tx, ty);
	for (i = 1; i <= 4; i++)
		drag_to (canvas, tx + (x - tx) * (gfloat) i / 4.0f, ty + (y - ty) * (gfloat) i / 4.0f);
	g_assert_true (lrg_dock_area_get_dragging (area));
	release_at (canvas, x, y);
	g_assert_false (lrg_dock_area_get_dragging (area));
}

typedef struct
{
	guint         closed;
	guint         activated;
	guint         changed;
	guint         requests;
	gboolean      veto;
	LrgDockPanel *last;
} Log;

static gboolean
on_close_request (LrgDockArea  *area,
                  LrgDockPanel *panel,
                  Log          *log)
{
	log->requests++;
	return log->veto;
}

static void
on_closed (LrgDockArea  *area,
           LrgDockPanel *panel,
           Log          *log)
{
	log->closed++;
	log->last = panel;
}

static void
on_activated (LrgDockArea  *area,
              LrgDockPanel *panel,
              Log          *log)
{
	log->activated++;
	log->last = panel;
}

static void
on_changed (LrgDockArea *area,
            Log         *log)
{
	log->changed++;
}

static void
connect_log (LrgDockArea *area,
             Log         *log)
{
	g_signal_connect (area, "close-request", G_CALLBACK (on_close_request), log);
	g_signal_connect (area, "panel-closed", G_CALLBACK (on_closed), log);
	g_signal_connect (area, "panel-activated", G_CALLBACK (on_activated), log);
	g_signal_connect (area, "layout-changed", G_CALLBACK (on_changed), log);
}

/* ==========================================================================
 * Building the tree
 * ========================================================================== */

static void
test_dock_add_positions (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgDockArea         *area = area_new (canvas);
	LrgDockPanel        *a;
	LrgDockPanel        *b;
	LrgDockPanel        *c;
	LrgDockPanel        *d;
	LrgDockPanel        *f;
	LrgDockPanel        *g;
	LrgDockPanel        *h;
	gfloat               bw = lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (area)),
	                                                LRG_THEME_METRIC_BORDER_WIDTH);
	g_autoptr(GList)     panels = NULL;

	assert_tree (area, "()");

	/* The first panel fills the area; its content sits under the strip */
	a = add (area, "a", NULL, LRG_DOCK_POSITION_CENTER);
	assert_tree (area, "[a*]");
	assert_stack (area, a, 0.0f, 0.0f, AREA_W, AREA_H);
	g_assert_cmpfloat (lrg_widget_get_world_y (LRG_WIDGET (a)), ==, STRIP);
	g_assert_cmpfloat (lrg_widget_get_world_x (LRG_WIDGET (a)), ==, bw);
	g_assert_cmpfloat (lrg_widget_get_height (LRG_WIDGET (a)), ==, AREA_H - STRIP - bw);
	g_assert_true (lrg_dock_area_get_active_panel (area) == a);
	g_assert_true (lrg_dock_panel_get_dock_area (a) == area);

	/* RIGHT of a: halves with the divider between */
	b = add (area, "b", a, LRG_DOCK_POSITION_RIGHT);
	assert_tree (area, "H(0.50,[a*],[b*])");
	assert_stack (area, a, 0.0f, 0.0f, 397.0f, AREA_H);
	assert_stack (area, b, 403.0f, 0.0f, 397.0f, AREA_H);

	/* BOTTOM of b splits b's stack */
	c = add (area, "c", b, LRG_DOCK_POSITION_BOTTOM);
	assert_tree (area, "H(0.50,[a*],V(0.50,[b*],[c*]))");
	assert_stack (area, b, 403.0f, 0.0f, 397.0f, 297.0f);
	assert_stack (area, c, 403.0f, 303.0f, 397.0f, 297.0f);

	/* LEFT of a and TOP of c */
	d = add (area, "d", a, LRG_DOCK_POSITION_LEFT);
	assert_tree (area, "H(0.50,H(0.50,[d*],[a*]),V(0.50,[b*],[c*]))");
	assert_stack (area, d, 0.0f, 0.0f, 196.0f, AREA_H);
	assert_stack (area, a, 202.0f, 0.0f, 195.0f, AREA_H);
	add (area, "e", c, LRG_DOCK_POSITION_TOP);
	assert_tree (area, "H(0.50,H(0.50,[d*],[a*]),V(0.50,[b*],V(0.50,[e*],[c*])))");

	/* CENTER of b: a second tab, now the visible one */
	f = add (area, "f", b, LRG_DOCK_POSITION_CENTER);
	assert_tree (area, "H(0.50,H(0.50,[d*],[a*]),V(0.50,[b,f*],V(0.50,[e*],[c*])))");
	g_assert_true (lrg_widget_get_visible (LRG_WIDGET (f)));
	g_assert_false (lrg_widget_get_visible (LRG_WIDGET (b)));

	/* No reference: RIGHT wraps the whole tree, the new panel takes a
	 * quarter; CENTER goes to the active panel's stack (a) */
	g = add (area, "g", NULL, LRG_DOCK_POSITION_RIGHT);
	assert_tree (area, "H(0.75,H(0.50,H(0.50,[d*],[a*]),V(0.50,[b,f*],V(0.50,[e*],[c*]))),[g*])");
	assert_stack (area, g, 602.0f, 0.0f, 198.0f, AREA_H);
	g_assert_cmpfloat_with_epsilon (lrg_dock_area_get_panel_share (area, g), 0.25f, 0.001f);
	h = add (area, "h", NULL, LRG_DOCK_POSITION_CENTER);
	assert_tree (area, "H(0.75,H(0.50,H(0.50,[d*],[a,h*]),V(0.50,[b,f*],V(0.50,[e*],[c*]))),[g*])");

	/* Lookups */
	panels = lrg_dock_area_get_panels (area);
	g_assert_cmpuint (g_list_length (panels), ==, 8);
	g_assert_true (panels->data == d);
	g_assert_true (lrg_dock_area_get_panel (area, "h") == h);
	g_assert_null (lrg_dock_area_get_panel (area, "nope"));
	g_assert_cmpfloat (lrg_dock_area_get_panel_share (area, h), ==, 0.5f);

	/* Shares move the split directly above a stack */
	lrg_dock_area_set_panel_share (area, g, 0.5f);
	assert_stack (area, g, 403.0f, 0.0f, 397.0f, AREA_H);

	/* A panel added with the plain container API is adopted as a tab */
	{
		LrgDockPanel *x = lrg_dock_panel_new ("x", "X");

		lrg_container_add_child (LRG_CONTAINER (area), LRG_WIDGET (x));
		g_object_unref (x);
		g_assert_true (lrg_dock_panel_get_dock_area (x) == area);
		assert_tree (area, "H(0.50,H(0.50,H(0.50,[d*],[a,h,x*]),V(0.50,[b,f*],V(0.50,[e*],[c*]))),[g*])");
	}
}

static void
test_dock_panel_properties (void)
{
	g_autoptr(LrgDockPanel) p = lrg_dock_panel_new ("id1", "Title");
	LrgWidget              *child = g_object_new (LRG_TYPE_WIDGET, NULL);
	g_autofree gchar       *icon = NULL;
	gboolean                closable;

	g_assert_cmpstr (lrg_dock_panel_get_id (p), ==, "id1");
	g_assert_cmpstr (lrg_dock_panel_get_title (p), ==, "Title");
	g_assert_true (lrg_dock_panel_get_closable (p));
	g_assert_null (lrg_dock_panel_get_icon (p));

	g_object_set (p, "icon", "gear", "closable", FALSE, "child", child, NULL);
	g_object_get (p, "icon", &icon, "closable", &closable, NULL);
	g_assert_cmpstr (icon, ==, "gear");
	g_assert_false (closable);
	g_assert_true (lrg_dock_panel_get_child (p) == child);
	g_object_unref (child);

	/* Replacing the child */
	lrg_dock_panel_set_child (p, NULL);
	g_assert_null (lrg_dock_panel_get_child (p));
	g_assert_null (lrg_dock_panel_get_dock_area (p));
	{
		GEnumClass *k = g_type_class_ref (LRG_TYPE_DOCK_POSITION);

		g_assert_cmpint (g_enum_get_value_by_nick (k, "center")->value, ==, LRG_DOCK_POSITION_CENTER);
		g_type_class_unref (k);
	}
}

/* ==========================================================================
 * Tabs
 * ========================================================================== */

static void
test_dock_tabs (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgDockArea         *area = area_new (canvas);
	LrgDockPanel        *a = add (area, "a", NULL, LRG_DOCK_POSITION_CENTER);
	LrgDockPanel        *b = add (area, "b", a, LRG_DOCK_POSITION_CENTER);
	LrgDockPanel        *c = add (area, "c", a, LRG_DOCK_POSITION_CENTER);
	Log                  log = { 0 };
	gfloat               x;
	gfloat               y;
	gfloat               tx;
	gfloat               ty;
	gfloat               tw;
	gfloat               th;
	gfloat               pad;
	guint                i;

	connect_log (area, &log);
	assert_tree (area, "[a,b,c*]");
	settle (canvas, 2);

	/* Click a's tab: active, visible, signals */
	tab_center (area, a, &x, &y);
	click_at (canvas, x, y);
	assert_tree (area, "[a*,b,c]");
	g_assert_true (lrg_widget_get_visible (LRG_WIDGET (a)));
	g_assert_false (lrg_widget_get_visible (LRG_WIDGET (c)));
	g_assert_true (lrg_dock_area_get_active_panel (area) == a);
	g_assert_cmpuint (log.changed, ==, 1);
	g_assert_true (log.last == a);
	g_assert_true (lrg_canvas_get_focused_widget (canvas) == LRG_WIDGET (area));

	/* Tabs run left to right without overlapping */
	{
		gfloat ax;
		gfloat bx;

		lrg_dock_area_get_tab_rect (area, a, &ax, NULL, &tw, NULL);
		lrg_dock_area_get_tab_rect (area, b, &bx, NULL, NULL, NULL);
		g_assert_cmpfloat (bx, >=, ax + tw);
	}

	/* Arrows on the focused area switch tabs */
	key_at (canvas, x, y, GRL_KEY_RIGHT);
	assert_tree (area, "[a,b*,c]");
	key_at (canvas, x, y, GRL_KEY_LEFT);
	assert_tree (area, "[a*,b,c]");

	/* The indicator springs to the new tab (paint stays deterministic) */
	for (i = 0; i < 120; i++)
		lrg_canvas_update (canvas, DT);
	g_assert_false (lrg_canvas_update (canvas, DT));

	/* Close button on b's tab, vetoed by close-request */
	pad = lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (area)), LRG_THEME_METRIC_PADDING_X);
	lrg_dock_area_get_tab_rect (area, b, &tx, &ty, &tw, &th);
	x = tx + tw - pad * 0.5f - 8.0f;
	y = ty + th * 0.5f;
	log.veto = TRUE;
	click_at (canvas, x, y);
	g_assert_cmpuint (log.requests, ==, 1);
	g_assert_cmpuint (log.closed, ==, 0);
	assert_tree (area, "[a*,b,c]");

	/* Allowed: closed, signal carries the panel, which leaves the area */
	log.veto = FALSE;
	g_object_ref (b);
	click_at (canvas, x, y);
	g_assert_cmpuint (log.requests, ==, 2);
	g_assert_cmpuint (log.closed, ==, 1);
	g_assert_true (log.last == b);
	g_assert_null (lrg_dock_panel_get_dock_area (b));
	g_assert_null (lrg_widget_get_parent (LRG_WIDGET (b)));
	g_object_unref (b);
	assert_tree (area, "[a*,c]");

	/* Middle click closes a closable tab, not a fixed one */
	lrg_dock_panel_set_closable (c, FALSE);
	tab_center (area, c, &x, &y);
	press_button (canvas, x, y, GRL_MOUSE_BUTTON_MIDDLE);
	release_button (canvas, x, y, GRL_MOUSE_BUTTON_MIDDLE);
	assert_tree (area, "[a*,c]");
	lrg_dock_panel_set_closable (c, TRUE);
	press_button (canvas, x, y, GRL_MOUSE_BUTTON_MIDDLE);
	release_button (canvas, x, y, GRL_MOUSE_BUTTON_MIDDLE);
	assert_tree (area, "[a*]");
	g_assert_cmpuint (log.closed, ==, 2);

	/* API close and present */
	g_assert_true (lrg_dock_area_close_panel (area, a));
	assert_tree (area, "()");
	g_assert_null (lrg_dock_area_get_active_panel (area));
}

/* ==========================================================================
 * Dividers
 * ========================================================================== */

static void
test_dock_divider (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgDockArea         *area = area_new (canvas);
	LrgDockPanel        *a = add (area, "a", NULL, LRG_DOCK_POSITION_CENTER);
	LrgDockPanel        *b = add (area, "b", a, LRG_DOCK_POSITION_RIGHT);
	Log                  log = { 0 };

	connect_log (area, &log);
	assert_stack (area, a, 0.0f, 0.0f, 397.0f, AREA_H);

	/* Grab the divider 3 units in and drag: live resize */
	move_to (canvas, 400.0f, 300.0f);
	press_at (canvas, 400.0f, 300.0f);
	drag_to (canvas, 500.0f, 300.0f);
	assert_stack (area, a, 0.0f, 0.0f, 497.0f, AREA_H);
	assert_stack (area, b, 503.0f, 0.0f, 297.0f, AREA_H);
	g_assert_cmpuint (log.changed, ==, 0);

	/* Minimum sizes: the area's floor, then a panel's own */
	drag_to (canvas, 5.0f, 300.0f);
	assert_stack (area, a, 0.0f, 0.0f, 48.0f, AREA_H);
	drag_to (canvas, 795.0f, 300.0f);
	assert_stack (area, b, 752.0f, 0.0f, 48.0f, AREA_H);
	lrg_widget_set_min_size (LRG_WIDGET (b), 200.0f, 0.0f);
	drag_to (canvas, 790.0f, 300.0f);
	assert_stack (area, b, 600.0f, 0.0f, 200.0f, AREA_H);
	release_at (canvas, 790.0f, 300.0f);
	g_assert_cmpuint (log.changed, ==, 1);

	/* Divider hover highlights (springs run, then settle) */
	settle (canvas, 120);

	/* Double click resets to equal halves */
	click_at (canvas, 597.0f, 300.0f);
	click_at (canvas, 597.0f, 300.0f);
	assert_stack (area, a, 0.0f, 0.0f, 397.0f, AREA_H);
	g_assert_cmpfloat (lrg_dock_area_get_panel_share (area, a), ==, 0.5f);

	/* Nested split dividers are found too */
	{
		LrgDockPanel *c = add (area, "c", b, LRG_DOCK_POSITION_BOTTOM);

		assert_stack (area, c, 403.0f, 303.0f, 397.0f, 297.0f);
		move_to (canvas, 600.0f, 300.0f);
		press_at (canvas, 600.0f, 300.0f);
		drag_to (canvas, 600.0f, 400.0f);
		release_at (canvas, 600.0f, 400.0f);
		assert_stack (area, c, 403.0f, 403.0f, 397.0f, 197.0f);
	}
}

/* ==========================================================================
 * Drag and drop
 * ========================================================================== */

/* fixture: a | [b, c] side by side; returns the panels */
static LrgDockArea *
drag_fixture (LrgCanvas     *canvas,
              LrgDockPanel **a,
              LrgDockPanel **b,
              LrgDockPanel **c)
{
	LrgDockArea *area = area_new (canvas);

	*a = add (area, "a", NULL, LRG_DOCK_POSITION_CENTER);
	*b = add (area, "b", *a, LRG_DOCK_POSITION_RIGHT);
	*c = add (area, "c", *b, LRG_DOCK_POSITION_CENTER);
	assert_tree (area, "H(0.50,[a*],[b,c*])");
	settle (canvas, 2);
	return area;
}

static void
test_dock_drag_compass (void)
{
	static const struct
	{
		LrgDockPosition  position;
		const gchar     *expected;
	} cases[] = {
		{ LRG_DOCK_POSITION_LEFT, "H(0.50,H(0.50,[c*],[a*]),[b*])" },
		{ LRG_DOCK_POSITION_RIGHT, "H(0.50,H(0.50,[a*],[c*]),[b*])" },
		{ LRG_DOCK_POSITION_TOP, "H(0.50,V(0.50,[c*],[a*]),[b*])" },
		{ LRG_DOCK_POSITION_BOTTOM, "H(0.50,V(0.50,[a*],[c*]),[b*])" },
		{ LRG_DOCK_POSITION_CENTER, "H(0.50,[a,c*],[b*])" },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		g_autoptr(LrgCanvas) canvas = canvas_new ();
		LrgDockPanel        *a;
		LrgDockPanel        *b;
		LrgDockPanel        *c;
		LrgDockArea         *area = drag_fixture (canvas, &a, &b, &c);
		Log                  log = { 0 };
		guint                before;
		gfloat               x;
		gfloat               y;
		gfloat               tx;
		gfloat               ty;

		connect_log (area, &log);
		compass (area, a, cases[i].position, &x, &y);

		/* Mid-drag: the feedback layer is in the overlay and records
		 * the preview, the compass and the ghost */
		lrg_canvas_record (canvas);
		before = lrg_paint_list_get_n_instances (lrg_canvas_get_paint_list (canvas));
		tab_center (area, c, &tx, &ty);
		move_to (canvas, tx, ty);
		press_at (canvas, tx, ty);
		drag_to (canvas, tx - 20.0f, ty + 40.0f);
		g_assert_true (lrg_dock_area_get_dragging (area));
		g_assert_cmpuint (lrg_canvas_get_n_overlays (canvas), ==, 1);
		drag_to (canvas, x, y);
		settle (canvas, 30);
		lrg_canvas_record (canvas);
		g_assert_cmpuint (lrg_paint_list_get_n_instances (lrg_canvas_get_paint_list (canvas)), >, before + 6);
		release_at (canvas, x, y);

		assert_tree (area, cases[i].expected);
		g_assert_true (lrg_dock_area_get_active_panel (area) == c);
		g_assert_cmpuint (log.changed, >=, 1);

		/* The feedback fades, then the layer leaves the overlay */
		settle (canvas, 120);
		g_assert_cmpuint (lrg_canvas_get_n_overlays (canvas), ==, 0);
	}
}

static void
test_dock_drag_edges_tabs (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgDockPanel        *a;
	LrgDockPanel        *b;
	LrgDockPanel        *c;
	LrgDockArea         *area = drag_fixture (canvas, &a, &b, &c);
	LrgDockPanel        *d = add (area, "d", b, LRG_DOCK_POSITION_CENTER);
	gfloat               x;
	gfloat               y;
	gfloat               w;

	assert_tree (area, "H(0.50,[a*],[b,c,d*])");

	/* Drop on the left part of b's tab: d moves before b */
	lrg_dock_area_get_tab_rect (area, b, &x, &y, &w, NULL);
	drag_tab (canvas, area, d, x + 4.0f, y + 6.0f);
	assert_tree (area, "H(0.50,[a*],[d*,b,c])");

	/* Onto a's strip after its tab: d joins a's stack at the end */
	lrg_dock_area_get_tab_rect (area, a, &x, &y, &w, NULL);
	drag_tab (canvas, area, d, x + w + 30.0f, y + 6.0f);
	assert_tree (area, "H(0.50,[a,d*],[b*,c])");

	/* Right edge target of the whole area: a quarter for c */
	drag_tab (canvas, area, c, AREA_W - 8.0f - 16.0f, AREA_H * 0.5f);
	assert_tree (area, "H(0.75,H(0.50,[a,d*],[b*]),[c*])");

	/* Top edge target (below the strips) */
	drag_tab (canvas, area, b, AREA_W * 0.5f, STRIP + 8.0f + 16.0f);
	assert_tree (area, "V(0.25,[b*],H(0.75,[a,d*],[c*]))");

	/* Dropping a lone panel on its own compass changes nothing */
	compass (area, b, LRG_DOCK_POSITION_LEFT, &x, &y);
	lrg_dock_area_set_allow_floating (area, FALSE);
	drag_tab (canvas, area, b, x, y);
	assert_tree (area, "V(0.25,[b*],H(0.75,[a,d*],[c*]))");

	/* API move mirrors the drop */
	lrg_dock_area_move_panel (area, b, c, LRG_DOCK_POSITION_CENTER);
	assert_tree (area, "H(0.75,[a,d*],[c,b*])");
	lrg_dock_area_move_panel (area, a, NULL, LRG_DOCK_POSITION_BOTTOM);
	assert_tree (area, "V(0.75,H(0.75,[d*],[c,b*]),[a*])");
}

static void
test_dock_drag_cancel (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgDockPanel        *a;
	LrgDockPanel        *b;
	LrgDockPanel        *c;
	LrgDockArea         *area = drag_fixture (canvas, &a, &b, &c);
	gfloat               x;
	gfloat               y;
	gfloat               tx;
	gfloat               ty;

	compass (area, a, LRG_DOCK_POSITION_LEFT, &x, &y);
	tab_center (area, c, &tx, &ty);
	move_to (canvas, tx, ty);
	press_at (canvas, tx, ty);
	drag_to (canvas, tx - 30.0f, ty + 30.0f);
	drag_to (canvas, x, y);
	g_assert_true (lrg_dock_area_get_dragging (area));

	/* Escape while the button is still held */
	key_at (canvas, x, y, GRL_KEY_ESCAPE);
	g_assert_false (lrg_dock_area_get_dragging (area));
	drag_to (canvas, x + 2.0f, y);
	release_at (canvas, x + 2.0f, y);
	assert_tree (area, "H(0.50,[a*],[b,c*])");

	/* A short wiggle below the threshold is a plain click, no drag */
	tab_center (area, b, &tx, &ty);
	move_to (canvas, tx, ty);
	press_at (canvas, tx, ty);
	drag_to (canvas, tx + 3.0f, ty + 2.0f);
	g_assert_false (lrg_dock_area_get_dragging (area));
	release_at (canvas, tx + 3.0f, ty + 2.0f);
	assert_tree (area, "H(0.50,[a*],[b*,c])");

	/* Nothing left behind once settled */
	settle (canvas, 120);
	g_assert_cmpuint (lrg_canvas_get_n_overlays (canvas), ==, 0);
}

/* ==========================================================================
 * Right to left
 * ========================================================================== */

static void
test_dock_rtl (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgDockArea         *area;
	LrgDockPanel        *a;
	LrgDockPanel        *b;
	LrgDockPanel        *c;
	gfloat               ax;
	gfloat               aw;
	gfloat               bx;
	gfloat               x;
	gfloat               y;

	lrg_canvas_set_default_direction (canvas, LRG_WIDGET_DIRECTION_RTL);
	area = drag_fixture (canvas, &a, &b, &c);

	/* The tree stays physical: a is still the left stack, its content
	 * is not mirrored */
	assert_stack (area, a, 0.0f, 0.0f, 397.0f, AREA_H);
	g_assert_cmpfloat (lrg_widget_get_world_x (LRG_WIDGET (a)), <, 10.0f);

	/* Tabs start from the right: b (first) is right of c */
	lrg_dock_area_get_tab_rect (area, b, &bx, NULL, &aw, NULL);
	g_assert_cmpfloat (bx + aw, ==, AREA_W - 4.0f);
	lrg_dock_area_get_tab_rect (area, c, &ax, NULL, &aw, NULL);
	g_assert_cmpfloat (ax + aw, <=, bx);

	/* Clicking still hits the right tab; Left moves to the next tab in
	 * reading order (towards the left) */
	tab_center (area, b, &x, &y);
	click_at (canvas, x, y);
	assert_tree (area, "H(0.50,[a*],[b*,c])");
	key_at (canvas, x, y, GRL_KEY_LEFT);
	assert_tree (area, "H(0.50,[a*],[b,c*])");

	/* Dropping on the right part of b's tab inserts before b */
	lrg_dock_area_get_tab_rect (area, b, &bx, &y, &aw, NULL);
	drag_tab (canvas, area, c, bx + aw - 4.0f, y + 6.0f);
	assert_tree (area, "H(0.50,[a*],[c*,b])");
}

/* ==========================================================================
 * Collapsing
 * ========================================================================== */

static void
test_dock_collapse (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgDockArea         *area = area_new (canvas);
	LrgDockPanel        *a = add (area, "a", NULL, LRG_DOCK_POSITION_CENTER);
	LrgDockPanel        *b = add (area, "b", a, LRG_DOCK_POSITION_RIGHT);
	LrgDockPanel        *c = add (area, "c", b, LRG_DOCK_POSITION_BOTTOM);
	LrgDockPanel        *d = add (area, "d", c, LRG_DOCK_POSITION_LEFT);
	Log                  log = { 0 };

	connect_log (area, &log);
	assert_tree (area, "H(0.50,[a*],V(0.50,[b*],H(0.50,[d*],[c*])))");

	/* Emptied stacks collapse into their sibling */
	lrg_dock_area_remove_panel (area, c);
	assert_tree (area, "H(0.50,[a*],V(0.50,[b*],[d*]))");
	assert_stack (area, d, 403.0f, 303.0f, 397.0f, 297.0f);
	lrg_dock_area_remove_panel (area, b);
	assert_tree (area, "H(0.50,[a*],[d*])");
	g_assert_cmpuint (log.closed, ==, 0);
	g_assert_cmpuint (log.requests, ==, 0);
	g_assert_cmpuint (log.changed, ==, 2);

	/* Dragging the last tab out of a stack collapses it too */
	lrg_dock_area_move_panel (area, d, a, LRG_DOCK_POSITION_CENTER);
	assert_tree (area, "[a,d*]");
	assert_stack (area, a, 0.0f, 0.0f, AREA_W, AREA_H);

	lrg_dock_area_remove_panel (area, a);
	lrg_dock_area_remove_panel (area, d);
	assert_tree (area, "()");
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (area)), ==, 0);
	lrg_canvas_record (canvas);
}

/* ==========================================================================
 * Floating
 * ========================================================================== */

static void
test_dock_floating (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgDockPanel        *a;
	LrgDockPanel        *b;
	LrgDockPanel        *c;
	LrgDockArea         *area = drag_fixture (canvas, &a, &b, &c);
	LrgWidget           *frame;
	gfloat               fx;
	gfloat               fy;
	gfloat               fw;
	gfloat               fh;
	gfloat               x;
	gfloat               y;

	/* Dropped away from every target (inside a's content, off the
	 * compass): c tears off into a floating frame */
	drag_tab (canvas, area, c, 120.0f, 500.0f);
	assert_tree (area, "H(0.50,[a*],[b*])+float[c]");
	g_assert_true (lrg_dock_area_is_panel_floating (area, c));
	frame = LRG_WIDGET (lrg_widget_get_parent (LRG_WIDGET (c)));
	g_assert_nonnull (frame);
	g_assert_false (frame == LRG_WIDGET (area));
	settle (canvas, 120);
	g_assert_cmpuint (lrg_canvas_get_n_overlays (canvas), ==, 1);
	g_assert_true (lrg_canvas_widget_at_point (canvas, 120.0f, 500.0f) == frame);
	g_assert_true (lrg_widget_get_visible (LRG_WIDGET (c)));

	/* The frame is centred under the drop point by its title bar */
	fx = lrg_widget_get_world_x (frame);
	fy = lrg_widget_get_world_y (frame);
	fw = lrg_widget_get_width (frame);
	fh = lrg_widget_get_height (frame);
	g_assert_cmpfloat_with_epsilon (fx + fw * 0.5f, 120.0f, 1.0f);
	g_assert_cmpfloat (fy, <, 500.0f);
	g_assert_cmpfloat (fy + STRIP, >, 500.0f);
	g_assert_cmpfloat (lrg_widget_get_world_y (LRG_WIDGET (c)), ==, fy + STRIP);

	/* Resize with the corner grip */
	move_to (canvas, fx + fw - 3.0f, fy + fh - 3.0f);
	press_at (canvas, fx + fw - 3.0f, fy + fh - 3.0f);
	drag_to (canvas, fx + fw + 47.0f, fy + fh + 27.0f);
	release_at (canvas, fx + fw + 47.0f, fy + fh + 27.0f);
	g_assert_cmpfloat (lrg_widget_get_width (frame), ==, fw + 50.0f);
	g_assert_cmpfloat (lrg_widget_get_height (frame), ==, fh + 30.0f);
	fw += 50.0f;

	/* Move by the title bar, then Escape returns it */
	x = fx + 30.0f;
	y = fy + STRIP * 0.5f;
	move_to (canvas, x, y);
	press_at (canvas, x, y);
	drag_to (canvas, x + 40.0f, y + 40.0f);
	g_assert_true (lrg_dock_area_get_dragging (area));
	g_assert_cmpfloat (lrg_widget_get_world_x (frame), ==, fx + 40.0f);
	key_at (canvas, x + 40.0f, y + 40.0f, GRL_KEY_ESCAPE);
	g_assert_false (lrg_dock_area_get_dragging (area));
	g_assert_cmpfloat (lrg_widget_get_world_x (frame), ==, fx);
	release_at (canvas, x + 40.0f, y + 40.0f);

	/* A plain move keeps it floating where it was dropped */
	press_at (canvas, x, y);
	drag_to (canvas, x + 100.0f, y - 10.0f);
	release_at (canvas, x + 100.0f, y - 10.0f);
	g_assert_true (lrg_dock_area_is_panel_floating (area, c));
	g_assert_cmpfloat (lrg_widget_get_world_x (frame), ==, fx + 100.0f);
	fx += 100.0f;
	fy -= 10.0f;

	/* Dragged by its title onto b's compass: docked back */
	compass (area, b, LRG_DOCK_POSITION_BOTTOM, &x, &y);
	move_to (canvas, fx + 30.0f, fy + STRIP * 0.5f);
	press_at (canvas, fx + 30.0f, fy + STRIP * 0.5f);
	drag_to (canvas, fx + 60.0f, fy + 40.0f);
	drag_to (canvas, x, y);
	release_at (canvas, x, y);
	assert_tree (area, "H(0.50,[a*],V(0.50,[b*],[c*]))");
	g_assert_false (lrg_dock_area_is_panel_floating (area, c));
	g_assert_true (lrg_widget_get_parent (LRG_WIDGET (c)) == LRG_CONTAINER (area));
	settle (canvas, 120);
	g_assert_cmpuint (lrg_canvas_get_n_overlays (canvas), ==, 0);

	/* API float; double click on the title docks it again */
	g_assert_true (lrg_dock_area_float_panel (area, a, 200.0f, 150.0f, 300.0f, 200.0f));
	assert_tree (area, "V(0.50,[b*],[c*])+float[a]");
	click_at (canvas, 260.0f, 160.0f);
	click_at (canvas, 260.0f, 160.0f);
	g_assert_false (lrg_dock_area_is_panel_floating (area, a));
	assert_tree (area, "V(0.50,[b,a*],[c*])");

	/* Removing a floating panel drops its frame */
	g_assert_true (lrg_dock_area_float_panel (area, b, 10.0f, 10.0f, 200.0f, 150.0f));
	lrg_dock_area_remove_panel (area, b);
	assert_tree (area, "V(0.50,[a*],[c*])");
	g_assert_cmpuint (lrg_canvas_get_n_overlays (canvas), ==, 0);

	/* Floating can be switched off: a drop off target cancels */
	lrg_dock_area_set_allow_floating (area, FALSE);
	g_assert_false (lrg_dock_area_float_panel (area, a, 10.0f, 10.0f, 200.0f, 150.0f));
}

static void
test_dock_floating_without_canvas (void)
{
	g_autoptr(LrgDockArea) area = lrg_dock_area_new ();
	LrgDockPanel          *a = lrg_dock_panel_new ("a", "A");

	lrg_dock_area_add_panel (area, a, NULL, LRG_DOCK_POSITION_CENTER);
	g_object_unref (a);
	g_assert_false (lrg_dock_area_float_panel (area, a, 0.0f, 0.0f, 100.0f, 100.0f));
	assert_tree (area, "[a*]");
}

/* ==========================================================================
 * Persistence
 * ========================================================================== */

static void
test_dock_save_load (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgDockArea         *area = area_new (canvas);
	LrgDockPanel        *a = add (area, "a", NULL, LRG_DOCK_POSITION_CENTER);
	LrgDockPanel        *b = add (area, "b", a, LRG_DOCK_POSITION_RIGHT);
	LrgDockPanel        *c = add (area, "c", b, LRG_DOCK_POSITION_BOTTOM);
	LrgDockPanel        *d = add (area, "d", b, LRG_DOCK_POSITION_CENTER);
	LrgDockPanel        *e = add (area, "e", a, LRG_DOCK_POSITION_CENTER);
	g_autofree gchar    *saved = NULL;
	g_autofree gchar    *shape = NULL;
	g_autofree gchar    *again = NULL;
	g_autoptr(GError)    error = NULL;
	Log                  log = { 0 };

	/* A non-trivial arrangement: ratios, active tabs, a floating panel */
	lrg_dock_area_set_panel_share (area, a, 0.3f);
	lrg_dock_area_set_panel_share (area, c, 0.4f);
	lrg_dock_area_present_panel (area, b);
	g_assert_true (lrg_dock_area_float_panel (area, e, 50.0f, 60.0f, 250.0f, 180.0f));
	lrg_dock_area_present_panel (area, d);
	shape = lrg_dock_area_describe (area);
	g_assert_cmpstr (shape, ==, "H(0.30,[a*],V(0.60,[b,d*],[c*]))+float[e]");
	saved = lrg_dock_area_save_layout (area);
	g_assert_nonnull (strstr (saved, "split: horizontal"));
	g_assert_nonnull (strstr (saved, "floating"));

	/* Scramble everything, then load: the same shape comes back */
	lrg_dock_area_move_panel (area, e, NULL, LRG_DOCK_POSITION_LEFT);
	lrg_dock_area_move_panel (area, a, c, LRG_DOCK_POSITION_CENTER);
	lrg_dock_area_move_panel (area, d, NULL, LRG_DOCK_POSITION_TOP);
	assert_tree (area, "V(0.25,[d*],H(0.25,[e*],V(0.60,[b*],[c,a*])))");
	connect_log (area, &log);
	g_assert_true (lrg_dock_area_load_layout (area, saved, &error));
	g_assert_no_error (error);
	assert_tree (area, shape);
	g_assert_cmpuint (log.changed, >=, 1);
	g_assert_true (lrg_dock_area_get_active_panel (area) == d);
	{
		LrgWidget *frame = LRG_WIDGET (lrg_widget_get_parent (LRG_WIDGET (e)));

		g_assert_cmpfloat (lrg_widget_get_world_x (frame), ==, 50.0f);
		g_assert_cmpfloat (lrg_widget_get_width (frame), ==, 250.0f);
	}

	/* Saving again gives the same document */
	again = lrg_dock_area_save_layout (area);
	g_assert_cmpstr (again, ==, saved);

	/* Missing ids are skipped (their stack collapses), unmentioned
	 * panels join the first stack */
	g_assert_true (lrg_dock_area_load_layout (area,
	                                          "version: 1\n"
	                                          "root:\n"
	                                          "  split: vertical\n"
	                                          "  ratio: 0.7\n"
	                                          "  first:\n"
	                                          "    tabs: [ghost, phantom]\n"
	                                          "    active: ghost\n"
	                                          "  second:\n"
	                                          "    split: horizontal\n"
	                                          "    ratio: 0.4\n"
	                                          "    first: { tabs: [c, nope, a], active: a }\n"
	                                          "    second: { tabs: [b] }\n",
	                                          &error));
	g_assert_no_error (error);
	assert_tree (area, "H(0.40,[c,a*,d,e],[b*])");

	/* Malformed layouts fail without changing anything */
	g_assert_false (lrg_dock_area_load_layout (area, "root:\n  split: diagonal\n", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	g_clear_error (&error);
	g_assert_false (lrg_dock_area_load_layout (area, "root:\n  what: 1\n", &error));
	g_clear_error (&error);
	g_assert_false (lrg_dock_area_load_layout (area, "- a\n- b\n", &error));
	g_clear_error (&error);
	assert_tree (area, "H(0.40,[c,a*,d,e],[b*])");

	/* An empty layout puts every panel in one stack, in tree order */
	g_assert_true (lrg_dock_area_load_layout (area, "version: 1\nroot: null\n", &error));
	g_assert_no_error (error);
	assert_tree (area, "[c*,a,d,e,b]");
}

/* ==========================================================================
 * Looks
 * ========================================================================== */

/* Every preset records the area, a drag in progress and a frame */
static void
test_dock_presets (void)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	g_autoptr(LrgTheme)  theme = lrg_theme_new_preset ("midnight");
	LrgDockPanel        *a;
	LrgDockPanel        *b;
	LrgDockPanel        *c;
	LrgDockArea         *area;
	gfloat               x;
	gfloat               y;
	guint                i;

	lrg_widget_set_theme (LRG_WIDGET (canvas), theme);
	area = drag_fixture (canvas, &a, &b, &c);
	lrg_dock_panel_set_icon (a, "folder");
	lrg_dock_area_set_tab_height (area, 0.0f);
	lrg_dock_area_float_panel (area, b, 100.0f, 100.0f, 250.0f, 200.0f);
	tab_center (area, a, &x, &y);
	move_to (canvas, x, y);
	press_at (canvas, x, y);
	drag_to (canvas, x + 200.0f, y + 200.0f);

	for (i = 0; presets[i] != NULL; i++)
	{
		LrgPaintList *list;

		lrg_theme_apply_preset (theme, presets[i]);
		settle (canvas, 10);
		list = lrg_canvas_record (canvas);
		g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, 20);
	}
	release_at (canvas, x + 200.0f, y + 200.0f);
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

#define GPU_W (400)
#define GPU_H (300)

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

static gboolean
init_graphics_context (void)
{
	const gchar *display = g_getenv ("DISPLAY");
	const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

	if ((display == NULL || display[0] == '\0') && (wayland == NULL || wayland[0] == '\0'))
		return FALSE;

	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (8, 8, "lrg-ui-dock-test");
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
 * Two stacks side by side in every preset: the content is PANEL, the
 * strips SURFACE, the divider gap shows the black backdrop; a drag
 * over the left stack's centre tints the preview region.
 */
static void
test_gpu_dock (void)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	g_autoptr(LrgCanvas) canvas = NULL;
	g_autoptr(LrgTheme)  theme = NULL;
	LrgDockArea         *area;
	LrgDockPanel        *a;
	LrgDockPanel        *b;
	guint                i;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}

	canvas = lrg_canvas_new ();
	lrg_widget_set_size (LRG_WIDGET (canvas), GPU_W, GPU_H);
	lrg_canvas_set_paint_background (canvas, FALSE);
	theme = lrg_theme_new_preset ("midnight");
	lrg_widget_set_theme (LRG_WIDGET (canvas), theme);
	area = lrg_dock_area_new ();
	lrg_dock_area_set_tab_height (area, STRIP);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (area));
	g_object_unref (area);
	lrg_widget_allocate (LRG_WIDGET (area), 0.0f, 0.0f, GPU_W, GPU_H);
	a = add (area, "a", NULL, LRG_DOCK_POSITION_CENTER);
	b = add (area, "b", a, LRG_DOCK_POSITION_RIGHT);
	(void) b;
	settle (canvas, 5);

	for (i = 0; presets[i] != NULL; i++)
	{
		Image img;

		lrg_theme_apply_preset (theme, presets[i]);
		settle (canvas, 5);
		img = render_canvas (canvas);

		/* Left stack: x 0..197; divider 197..203; right stack 203..400 */
		g_assert_true (pixel_near (&img, 100, 200, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL), 3));
		g_assert_true (pixel_near (&img, 300, 200, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL), 3));
		g_assert_true (pixel_near (&img, 190, 8, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE), 3));
		g_assert_true (pixel_near (&img, 200, 200, &(GrlColor){ 0, 0, 0, 255 }, 0));
		UnloadImage (img);
	}

	/* Drag b's tab over a's compass centre: the preview covers a */
	{
		gfloat x;
		gfloat y;
		gfloat tx;
		gfloat ty;
		Image  img;

		lrg_theme_apply_preset (theme, "midnight");
		compass (area, a, LRG_DOCK_POSITION_CENTER, &x, &y);
		tab_center (area, b, &tx, &ty);
		move_to (canvas, tx, ty);
		press_at (canvas, tx, ty);
		drag_to (canvas, tx - 40.0f, ty + 40.0f);
		drag_to (canvas, x, y);
		settle (canvas, 60);
		img = render_canvas (canvas);
		g_assert_false (pixel_near (&img, 40, 260, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL), 3));
		g_assert_true (pixel_near (&img, 300, 200, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL), 3));
		UnloadImage (img);
		release_at (canvas, x, y);
		assert_tree (area, "[a,b*]");
	}
}

int
main (int    argc,
      char **argv)
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	graphics_available = init_graphics_context ();

	g_test_add_func ("/ui/dock/add-positions", test_dock_add_positions);
	g_test_add_func ("/ui/dock/panel-properties", test_dock_panel_properties);
	g_test_add_func ("/ui/dock/tabs", test_dock_tabs);
	g_test_add_func ("/ui/dock/divider", test_dock_divider);
	g_test_add_func ("/ui/dock/drag-compass", test_dock_drag_compass);
	g_test_add_func ("/ui/dock/drag-edges-tabs", test_dock_drag_edges_tabs);
	g_test_add_func ("/ui/dock/drag-cancel", test_dock_drag_cancel);
	g_test_add_func ("/ui/dock/rtl", test_dock_rtl);
	g_test_add_func ("/ui/dock/collapse", test_dock_collapse);
	g_test_add_func ("/ui/dock/floating", test_dock_floating);
	g_test_add_func ("/ui/dock/floating-no-canvas", test_dock_floating_without_canvas);
	g_test_add_func ("/ui/dock/save-load", test_dock_save_load);
	g_test_add_func ("/ui/dock/presets", test_dock_presets);
	g_test_add_func ("/ui/gpu/dock", test_gpu_dock);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
