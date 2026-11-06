/* test-ui-viewport.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCustomWidget and LrgViewport.
 *
 * Headless: widgets sit on a canvas, are driven through
 * lrg_canvas_process_input() and stepped with lrg_canvas_update() at
 * 60 Hz. Custom widget hooks (function table and signals) are checked
 * through measuring, recording, stepping and real input; the viewport's
 * fit math is checked directly and through pointer mapping, letterbox
 * bars, focus + keys and touch. GPU cases (skipped without a display)
 * render a scene into the viewport's texture, read the canvas back and
 * check the texture's placement, orientation, rounded corners and
 * resize debouncing.
 */

#include <glib.h>
#include <math.h>
#include <libregnum.h>
#include <raylib.h>

#define DT (1.0f / 60.0f)

/* ==========================================================================
 * Canvas and input helpers
 * ========================================================================== */

static LrgCanvas *
canvas_new (void)
{
	LrgCanvas *canvas = lrg_canvas_new ();

	lrg_widget_set_size (LRG_WIDGET (canvas), 400.0f, 300.0f);
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
         gfloat     y,
         gboolean   held)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	if (held)
		input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
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
scroll_at (LrgCanvas *canvas,
           gfloat     x,
           gfloat     y,
           gfloat     amount)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.wheel_y = amount;
	lrg_canvas_process_input (canvas, &input);
}

static void
key (LrgCanvas *canvas,
     GrlKey     k)
{
	LrgCanvasInput input;

	frame_pointer (&input, -10.0f, -10.0f);
	lrg_canvas_input_add_key (&input, k);
	lrg_canvas_process_input (canvas, &input);
}

/* touch_frame: one frame with one finger at (@x, @y), or none when @down is FALSE */
static void
touch_frame (LrgCanvas *canvas,
             gboolean   down,
             gfloat     x,
             gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, -100.0f, -100.0f);
	if (down)
		lrg_canvas_input_add_touch (&input, 0, x, y);
	lrg_canvas_process_input (canvas, &input);
}

/* ==========================================================================
 * Custom widget
 * ========================================================================== */

typedef struct
{
	guint    measures;
	guint    paints;
	guint    steps_left;   /* step returns TRUE while > 0 */
	guint    events;
	guint    presses;
	gboolean consume;      /* table event function consumes everything */
	guint    destroyed;
	gfloat   last_x;
	gfloat   last_y;
	gfloat   last_w;
	gfloat   last_h;
} CustomState;

static void
funcs_measure (LrgCustomWidget     *widget,
               gfloat               avail_width,
               gfloat               avail_height,
               LrgCustomWidgetSize *size,
               gpointer             user_data)
{
	CustomState *st = user_data;

	st->measures++;
	size->width = 120.0f;   /* height left at the property value */
}

static void
funcs_paint (LrgCustomWidget *widget,
             LrgPaintList    *list,
             gfloat           x,
             gfloat           y,
             gfloat           width,
             gfloat           height,
             gpointer         user_data)
{
	CustomState *st = user_data;
	GrlColor     c = { 200, 40, 40, 255 };

	st->paints++;
	st->last_x = x;
	st->last_y = y;
	st->last_w = width;
	st->last_h = height;
	lrg_paint_list_rect (list, x, y, width, height, 0.0f, &c);
}

static gboolean
funcs_step (LrgCustomWidget *widget,
            gfloat           delta_time,
            gpointer         user_data)
{
	CustomState *st = user_data;

	if (st->steps_left == 0)
		return FALSE;
	st->steps_left--;
	return TRUE;
}

static gboolean
funcs_event (LrgCustomWidget  *widget,
             const LrgUIEvent *event,
             gpointer          user_data)
{
	CustomState *st = user_data;

	st->events++;
	return st->consume;
}

static void
state_destroy (gpointer data)
{
	((CustomState *) data)->destroyed++;
}

static const LrgCustomWidgetFuncs test_funcs = {
	funcs_measure, funcs_paint, funcs_step, funcs_event, { NULL }
};

/* signal: measure doubles the height on top of the table */
static void
on_measure (LrgCustomWidget     *widget,
            gfloat               avail_width,
            gfloat               avail_height,
            LrgCustomWidgetSize *size,
            gpointer             user_data)
{
	size->height *= 2.0f;
}

static void
on_paint (LrgCustomWidget *widget,
          LrgPaintList    *list,
          gfloat           x,
          gfloat           y,
          gfloat           width,
          gfloat           height,
          gpointer         user_data)
{
	GrlColor c = { 40, 200, 40, 255 };

	lrg_paint_list_circle (list, x + width * 0.5f, y + height * 0.5f, 4.0f, &c);
}

static gboolean
on_event (LrgCustomWidget *widget,
          LrgUIEvent      *event,
          gpointer         user_data)
{
	CustomState *st = user_data;

	if (lrg_ui_event_get_event_type (event) == LRG_UI_EVENT_MOUSE_BUTTON_DOWN)
	{
		st->presses++;
		return TRUE;
	}
	return FALSE;
}

static gboolean
on_step_once (LrgCustomWidget *widget,
              gfloat           delta_time,
              gpointer         user_data)
{
	guint *n = user_data;

	if (*n == 0)
		return FALSE;
	(*n)--;
	return TRUE;
}

/* Measure: properties, then the table, then handlers */
static void
test_custom_measure (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	CustomState          st = { 0 };
	LrgCustomWidget     *cw = lrg_custom_widget_new ();
	gfloat               w;
	gfloat               h;

	lrg_custom_widget_set_preferred_width (cw, 30.0f);
	lrg_custom_widget_set_preferred_height (cw, 20.0f);
	place (canvas, cw, 10.0f, 10.0f, 50.0f, 50.0f);

	/* Properties only */
	lrg_widget_measure (LRG_WIDGET (cw), &w, &h);
	g_assert_cmpfloat_with_epsilon (w, 30.0f, 0.001f);
	g_assert_cmpfloat_with_epsilon (h, 20.0f, 0.001f);

	/* Table: width 120, height untouched; then signal doubles height */
	lrg_custom_widget_set_funcs (cw, &test_funcs, &st, state_destroy);
	g_signal_connect (cw, "measure", G_CALLBACK (on_measure), NULL);
	lrg_widget_measure (LRG_WIDGET (cw), &w, &h);
	g_assert_cmpuint (st.measures, >=, 1);
	g_assert_cmpfloat_with_epsilon (w, 120.0f, 0.001f);
	g_assert_cmpfloat_with_epsilon (h, 40.0f, 0.001f);

	/* Property notification and GValue round trip */
	g_object_set (cw, "preferred-height", 5.0f, NULL);
	g_assert_cmpfloat_with_epsilon (lrg_custom_widget_get_preferred_height (cw), 5.0f, 0.001f);
	lrg_widget_measure (LRG_WIDGET (cw), &w, &h);
	g_assert_cmpfloat_with_epsilon (h, 10.0f, 0.001f);

	/* Replacing the table frees the old data */
	lrg_custom_widget_set_funcs (cw, NULL, NULL, NULL);
	g_assert_cmpuint (st.destroyed, ==, 1);
}

/* Paint: table and signal both record at the widget's world rectangle */
static void
test_custom_paint (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	CustomState          st = { 0 };
	LrgCustomWidget     *cw = lrg_custom_widget_new ();
	LrgPaintList        *list;
	guint                n_plain;
	guint                n_box;

	lrg_custom_widget_set_funcs (cw, &test_funcs, &st, NULL);
	g_signal_connect (cw, "paint", G_CALLBACK (on_paint), NULL);
	place (canvas, cw, 40.0f, 30.0f, 100.0f, 60.0f);

	list = lrg_canvas_record (canvas);
	g_assert_cmpuint (st.paints, >=, 1);
	g_assert_cmpfloat_with_epsilon (st.last_x, 40.0f, 0.001f);
	g_assert_cmpfloat_with_epsilon (st.last_y, 30.0f, 0.001f);
	g_assert_cmpfloat_with_epsilon (st.last_w, 100.0f, 0.001f);
	g_assert_cmpfloat_with_epsilon (st.last_h, 60.0f, 0.001f);

	/* rect from the table + circle from the handler */
	n_plain = lrg_paint_list_get_n_instances (list);
	g_assert_cmpuint (n_plain, ==, 2);
	{
		const LrgPaintInstance *r = lrg_paint_list_get_instance (list, 0);

		g_assert_cmpfloat_with_epsilon (r->rect[0], 40.0f, 0.001f);
		g_assert_cmpfloat_with_epsilon (r->rect[3], 90.0f, 0.001f);
	}

	/* paint-box adds the themed surface underneath */
	lrg_custom_widget_set_paint_box (cw, TRUE);
	list = lrg_canvas_record (canvas);
	n_box = lrg_paint_list_get_n_instances (list);
	g_assert_cmpuint (n_box, >, n_plain);
}

/* Step: table and handlers OR-ed into lrg_canvas_update() */
static void
test_custom_step (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	CustomState          st = { 0 };
	LrgCustomWidget     *cw = lrg_custom_widget_new ();
	guint                signal_frames = 5;
	guint                frames = 0;

	lrg_custom_widget_set_preferred_width (cw, 10.0f);
	place (canvas, cw, 0.0f, 0.0f, 10.0f, 10.0f);
	while (lrg_canvas_update (canvas, DT) && frames < 100)
		frames++;

	st.steps_left = 3;
	lrg_custom_widget_set_funcs (cw, &test_funcs, &st, NULL);
	g_signal_connect (cw, "step", G_CALLBACK (on_step_once), &signal_frames);

	/* Busy while either is busy: the longer one (5 frames) wins */
	frames = 0;
	while (lrg_canvas_update (canvas, DT) && frames < 100)
		frames++;
	g_assert_cmpuint (frames, ==, 5);
	g_assert_cmpuint (st.steps_left, ==, 0);
	g_assert_cmpuint (signal_frames, ==, 0);
}

/* Events: table first (may consume), then the "event" signal */
static void
test_custom_event (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	CustomState          st = { 0 };
	LrgCustomWidget     *cw = lrg_custom_widget_new ();

	lrg_custom_widget_set_funcs (cw, &test_funcs, &st, NULL);
	g_signal_connect (cw, "event", G_CALLBACK (on_event), &st);
	place (canvas, cw, 20.0f, 20.0f, 80.0f, 40.0f);

	move_to (canvas, 50.0f, 40.0f, FALSE);
	press_at (canvas, 50.0f, 40.0f);
	release_at (canvas, 50.0f, 40.0f);
	g_assert_cmpuint (st.events, >=, 3);
	g_assert_cmpuint (st.presses, ==, 1);

	/* A consuming table function hides events from handlers */
	st.consume = TRUE;
	press_at (canvas, 50.0f, 40.0f);
	release_at (canvas, 50.0f, 40.0f);
	g_assert_cmpuint (st.presses, ==, 1);

	/* Focusable custom widgets take focus on click */
	lrg_widget_set_focusable (LRG_WIDGET (cw), TRUE);
	press_at (canvas, 50.0f, 40.0f);
	release_at (canvas, 50.0f, 40.0f);
	g_assert_true (lrg_canvas_get_focused_widget (canvas) == LRG_WIDGET (cw));
}

/* Disposing the widget releases the table's data */
static void
test_custom_destroy (void)
{
	CustomState      st = { 0 };
	LrgCustomWidget *cw = lrg_custom_widget_new ();

	lrg_custom_widget_set_funcs (cw, &test_funcs, &st, state_destroy);
	g_assert_cmpuint (st.destroyed, ==, 0);
	g_object_unref (cw);
	g_assert_cmpuint (st.destroyed, ==, 1);
}

/* ==========================================================================
 * Viewport: geometry
 * ========================================================================== */

static void
assert_rect (const GrlRectangle *r,
             gfloat              x,
             gfloat              y,
             gfloat              w,
             gfloat              h)
{
	g_assert_cmpfloat_with_epsilon (r->x, x, 0.01f);
	g_assert_cmpfloat_with_epsilon (r->y, y, 0.01f);
	g_assert_cmpfloat_with_epsilon (r->width, w, 0.01f);
	g_assert_cmpfloat_with_epsilon (r->height, h, 0.01f);
}

static void
test_viewport_fit_math (void)
{
	GrlRectangle square = { 0.0f, 0.0f, 100.0f, 100.0f };
	GrlRectangle wide = { 10.0f, 20.0f, 250.0f, 150.0f };
	GrlRectangle dest;
	GrlRectangle src;

	/* Stretch: whole texture over the whole area */
	g_assert_true (lrg_viewport_compute_fit (LRG_VIEWPORT_FIT_STRETCH, 100.0f, 50.0f, &square, &dest, &src));
	assert_rect (&dest, 0.0f, 0.0f, 100.0f, 100.0f);
	assert_rect (&src, 0.0f, 0.0f, 100.0f, 50.0f);

	/* Contain: 2:1 in a square, bars top and bottom */
	g_assert_true (lrg_viewport_compute_fit (LRG_VIEWPORT_FIT_CONTAIN, 100.0f, 50.0f, &square, &dest, &src));
	assert_rect (&dest, 0.0f, 25.0f, 100.0f, 50.0f);
	assert_rect (&src, 0.0f, 0.0f, 100.0f, 50.0f);

	/* Cover: 2:1 in a square, cropped left and right */
	g_assert_true (lrg_viewport_compute_fit (LRG_VIEWPORT_FIT_COVER, 100.0f, 50.0f, &square, &dest, &src));
	assert_rect (&dest, 0.0f, 0.0f, 100.0f, 100.0f);
	assert_rect (&src, 25.0f, 0.0f, 50.0f, 50.0f);

	/* Integer scale: 2.5x fits, 2x used, centred on whole units */
	g_assert_true (lrg_viewport_compute_fit (LRG_VIEWPORT_FIT_INTEGER_SCALE, 100.0f, 50.0f, &wide, &dest, &src));
	assert_rect (&dest, 35.0f, 45.0f, 200.0f, 100.0f);
	assert_rect (&src, 0.0f, 0.0f, 100.0f, 50.0f);

	/* Integer scale never shrinks below 1: an oversized texture crops */
	g_assert_true (lrg_viewport_compute_fit (LRG_VIEWPORT_FIT_INTEGER_SCALE, 300.0f, 300.0f, &square, &dest, &src));
	assert_rect (&dest, 0.0f, 0.0f, 100.0f, 100.0f);
	assert_rect (&src, 100.0f, 100.0f, 100.0f, 100.0f);

	/* Exact multiple: 3x */
	g_assert_true (lrg_viewport_compute_fit (LRG_VIEWPORT_FIT_INTEGER_SCALE, 50.0f, 50.0f, &(GrlRectangle){ 0, 0, 150, 160 },
	                                         &dest, &src));
	assert_rect (&dest, 0.0f, 5.0f, 150.0f, 150.0f);

	/* Nothing to show */
	g_assert_false (lrg_viewport_compute_fit (LRG_VIEWPORT_FIT_CONTAIN, 0.0f, 50.0f, &square, &dest, &src));
	g_assert_false (lrg_viewport_compute_fit (LRG_VIEWPORT_FIT_CONTAIN, 10.0f, 10.0f,
	                                          &(GrlRectangle){ 0, 0, 0, 10 }, &dest, &src));
}

/* Layout: content rect inside the border, desired size, preferred size */
static void
test_viewport_layout (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgViewport         *vp = lrg_viewport_new ();
	GrlRectangle         area;
	gint                 w;
	gint                 h;
	gfloat               pw;
	gfloat               ph;
	gfloat               bw;

	place (canvas, vp, 20.0f, 20.0f, 200.0f, 100.0f);
	lrg_viewport_get_content_rect (vp, &area);
	bw = area.x;
	g_assert_cmpfloat (bw, >=, 0.0f);
	g_assert_cmpfloat_with_epsilon (area.width, 200.0f - 2.0f * bw, 0.001f);
	g_assert_cmpfloat_with_epsilon (area.height, 100.0f - 2.0f * bw, 0.001f);

	/* Headless RENDER: content size is what the texture will be */
	lrg_viewport_get_content_size (vp, &w, &h);
	g_assert_cmpint (w, ==, (gint) roundf (area.width));
	g_assert_cmpint (h, ==, (gint) roundf (area.height));

	g_object_set (vp, "resolution-scale", 0.5f, NULL);
	lrg_viewport_get_content_size (vp, &w, &h);
	g_assert_cmpint (w, ==, (gint) roundf (area.width * 0.5f));
	g_assert_cmpint (h, ==, (gint) roundf (area.height * 0.5f));

	/* A fixed resolution wins and becomes the natural size */
	lrg_viewport_set_fixed_resolution (vp, 320, 180);
	lrg_viewport_get_content_size (vp, &w, &h);
	g_assert_cmpint (w, ==, 320);
	g_assert_cmpint (h, ==, 180);
	lrg_widget_measure (LRG_WIDGET (vp), &pw, &ph);
	g_assert_cmpfloat_with_epsilon (pw, 320.0f + 2.0f * bw, 0.001f);
	g_assert_cmpfloat_with_epsilon (ph, 180.0f + 2.0f * bw, 0.001f);
	g_assert_true (lrg_widget_get_needs_layout (LRG_WIDGET (vp)) ||
	               lrg_widget_get_needs_layout (LRG_WIDGET (canvas)));

	/* Headless: nothing rendered, nothing busy, the frame still paints
	 * (with a window, the GPU cases cover rendering) */
	if (!IsWindowReady ())
	{
		g_assert_false (lrg_viewport_render (vp));
		g_assert_null (lrg_viewport_get_render_texture (vp));
		g_assert_false (lrg_canvas_update (canvas, DT));
	}
	g_assert_cmpuint (lrg_paint_list_get_n_instances (lrg_canvas_record (canvas)), >, 0);

	/* Properties round trip */
	g_object_set (vp, "fit", LRG_VIEWPORT_FIT_INTEGER_SCALE, "filter", GRL_TEXTURE_FILTER_POINT,
	              "continuous", FALSE, NULL);
	g_assert_cmpint (lrg_viewport_get_fit (vp), ==, LRG_VIEWPORT_FIT_INTEGER_SCALE);
	g_assert_cmpint (lrg_viewport_get_filter (vp), ==, GRL_TEXTURE_FILTER_POINT);
	g_assert_false (lrg_viewport_get_continuous (vp));
}

/* ==========================================================================
 * Viewport: input
 * ========================================================================== */

typedef struct
{
	guint  moves;
	guint  presses;
	guint  releases;
	guint  scrolls;
	guint  keys;
	guint  pans;
	guint  pan_ends;
	gfloat pan_dx;
	gfloat u;
	gfloat v;
	gfloat dx;
	gfloat dy;
	gint   button;
	GrlKey key;
} VpLog;

static void
on_vp_move (LrgViewport *vp,
            gfloat       u,
            gfloat       v,
            gfloat       dx,
            gfloat       dy,
            VpLog       *log)
{
	log->moves++;
	log->u = u;
	log->v = v;
	log->dx = dx;
	log->dy = dy;
}

static void
on_vp_press (LrgViewport       *vp,
             gint               button,
             gfloat             u,
             gfloat             v,
             LrgInputModifiers  mods,
             VpLog             *log)
{
	log->presses++;
	log->button = button;
	log->u = u;
	log->v = v;
}

static void
on_vp_release (LrgViewport       *vp,
               gint               button,
               gfloat             u,
               gfloat             v,
               LrgInputModifiers  mods,
               VpLog             *log)
{
	log->releases++;
	log->u = u;
	log->v = v;
}

static void
on_vp_scroll (LrgViewport *vp,
              gfloat       u,
              gfloat       v,
              gfloat       sx,
              gfloat       sy,
              VpLog       *log)
{
	log->scrolls++;
	log->dy = sy;
}

static gboolean
on_vp_key (LrgViewport       *vp,
           GrlKey             k,
           LrgInputModifiers  mods,
           VpLog             *log)
{
	log->keys++;
	log->key = k;
	return k == GRL_KEY_W;
}

static void
on_vp_pan (LrgViewport *vp,
           gfloat       u,
           gfloat       v,
           gfloat       dx,
           gfloat       dy,
           VpLog       *log)
{
	log->pans++;
	log->pan_dx += dx;
}

static void
on_vp_pan_end (LrgViewport *vp,
               gfloat       vx,
               gfloat       vy,
               VpLog       *log)
{
	log->pan_ends++;
}

static void
connect_log (LrgViewport *vp,
             VpLog       *log)
{
	g_signal_connect (vp, "pointer-move", G_CALLBACK (on_vp_move), log);
	g_signal_connect (vp, "pointer-press", G_CALLBACK (on_vp_press), log);
	g_signal_connect (vp, "pointer-release", G_CALLBACK (on_vp_release), log);
	g_signal_connect (vp, "pointer-scroll", G_CALLBACK (on_vp_scroll), log);
	g_signal_connect (vp, "key", G_CALLBACK (on_vp_key), log);
}

/*
 * A 100 x 50 fixed-resolution scene, contained in a 200 x 200 viewport
 * at (50, 50): bars above and below the content.
 */
static LrgViewport *
letterboxed (LrgCanvas    *canvas,
             GrlRectangle *area)
{
	LrgViewport *vp = lrg_viewport_new ();

	lrg_viewport_set_fixed_resolution (vp, 100, 50);
	lrg_viewport_set_fit (vp, LRG_VIEWPORT_FIT_CONTAIN);
	place (canvas, vp, 50.0f, 50.0f, 200.0f, 200.0f);
	lrg_viewport_get_content_rect (vp, area);
	return vp;
}

static void
test_viewport_mapping (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	GrlRectangle         area;
	GrlRectangle         disp;
	LrgViewport         *vp = letterboxed (canvas, &area);
	gfloat               a = area.width;
	gfloat               u;
	gfloat               v;
	gfloat               x;
	gfloat               y;

	/* Display rect: full width, half height, centred */
	g_assert_true (lrg_viewport_get_display_rect (vp, &disp));
	assert_rect (&disp, area.x, area.y + a * 0.25f, a, a * 0.5f);

	/* Centre maps to the texture centre */
	g_assert_true (lrg_viewport_widget_to_texture (vp, area.x + a * 0.5f, area.y + a * 0.5f, &u, &v));
	g_assert_cmpfloat_with_epsilon (u, 50.0f, 0.01f);
	g_assert_cmpfloat_with_epsilon (v, 25.0f, 0.01f);

	/* Top bar is not content, but still extrapolates */
	g_assert_false (lrg_viewport_widget_to_texture (vp, area.x + a * 0.5f, area.y + 2.0f, &u, &v));
	g_assert_cmpfloat (v, <, 0.0f);

	/* Round trip */
	lrg_viewport_texture_to_widget (vp, 10.0f, 40.0f, &x, &y);
	g_assert_true (lrg_viewport_widget_to_texture (vp, x, y, &u, &v));
	g_assert_cmpfloat_with_epsilon (u, 10.0f, 0.01f);
	g_assert_cmpfloat_with_epsilon (v, 40.0f, 0.01f);
}

static void
test_viewport_pointer (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	GrlRectangle         area;
	LrgViewport         *vp = letterboxed (canvas, &area);
	VpLog                log = { 0 };
	gfloat               a = area.width;
	gfloat               cx = 50.0f + area.x + a * 0.5f;   /* canvas coords of the centre */
	gfloat               cy = 50.0f + area.y + a * 0.5f;
	gfloat               bar_y = 50.0f + area.y + 3.0f;    /* in the top letterbox bar */

	connect_log (vp, &log);

	/* Clicks on the bar are ignored */
	move_to (canvas, cx, bar_y, FALSE);
	press_at (canvas, cx, bar_y);
	release_at (canvas, cx, bar_y);
	g_assert_cmpuint (log.moves, ==, 0);
	g_assert_cmpuint (log.presses, ==, 0);
	g_assert_cmpuint (log.releases, ==, 0);

	/* Over the content: move, then a press at the centre */
	move_to (canvas, cx, cy, FALSE);
	g_assert_cmpuint (log.moves, ==, 1);
	g_assert_cmpfloat_with_epsilon (log.u, 50.0f, 0.01f);
	press_at (canvas, cx, cy);
	g_assert_cmpuint (log.presses, ==, 1);
	g_assert_cmpint (log.button, ==, 0);
	g_assert_cmpfloat_with_epsilon (log.v, 25.0f, 0.01f);
	g_assert_true (lrg_viewport_get_dragging (vp));

	/* Dragging out of the widget keeps reporting (extrapolated), with deltas */
	move_to (canvas, cx + a, cy, TRUE);
	g_assert_cmpuint (log.moves, ==, 2);
	g_assert_cmpfloat_with_epsilon (log.u, 150.0f, 0.05f);
	g_assert_cmpfloat_with_epsilon (log.dx, 100.0f, 0.05f);

	release_at (canvas, cx + a, cy);
	g_assert_cmpuint (log.releases, ==, 1);
	g_assert_false (lrg_viewport_get_dragging (vp));

	/* Moving outside after the drag is not reported */
	move_to (canvas, 5.0f, 5.0f, FALSE);
	g_assert_cmpuint (log.moves, ==, 2);

	/* Wheel over the content */
	scroll_at (canvas, cx, cy, -1.0f);
	g_assert_cmpuint (log.scrolls, ==, 1);
	g_assert_cmpfloat_with_epsilon (log.dy, -1.0f, 0.001f);
	scroll_at (canvas, cx, bar_y, -1.0f);
	g_assert_cmpuint (log.scrolls, ==, 1);
}

/* Focus and keys: clicking focuses, keys are forwarded and may be consumed */
static void
test_viewport_keys (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	GrlRectangle         area;
	LrgViewport         *vp = letterboxed (canvas, &area);
	VpLog                log = { 0 };
	gfloat               cx = 50.0f + area.x + area.width * 0.5f;
	gfloat               cy = 50.0f + area.y + area.height * 0.5f;

	connect_log (vp, &log);
	g_assert_true (lrg_widget_get_focusable (LRG_WIDGET (vp)));

	/* Not focused yet: no keys */
	key (canvas, GRL_KEY_W);
	g_assert_cmpuint (log.keys, ==, 0);

	move_to (canvas, cx, cy, FALSE);
	press_at (canvas, cx, cy);
	release_at (canvas, cx, cy);
	g_assert_true (lrg_canvas_get_focused_widget (canvas) == LRG_WIDGET (vp));
	g_assert_true (lrg_widget_get_state_flags (LRG_WIDGET (vp)) & LRG_WIDGET_STATE_FOCUSED);

	key (canvas, GRL_KEY_W);
	g_assert_cmpuint (log.keys, ==, 1);
	g_assert_cmpint (log.key, ==, GRL_KEY_W);
	key (canvas, GRL_KEY_Q);
	g_assert_cmpuint (log.keys, ==, 2);
}

/* Touch: drags are claimed as pans only with a "pan" handler */
static void
test_viewport_touch (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	GrlRectangle         area;
	LrgViewport         *vp = letterboxed (canvas, &area);
	VpLog                log = { 0 };
	gfloat               cx = 50.0f + area.x + area.width * 0.5f;
	gfloat               cy = 50.0f + area.y + area.height * 0.5f;
	gint                 i;

	connect_log (vp, &log);

	/* No pan handler: replayed as a mouse drag */
	touch_frame (canvas, TRUE, cx, cy);
	for (i = 1; i <= 4; i++)
		touch_frame (canvas, TRUE, cx + 10.0f * (gfloat) i, cy);
	touch_frame (canvas, FALSE, 0.0f, 0.0f);
	g_assert_cmpuint (log.presses, ==, 1);
	g_assert_cmpuint (log.releases, ==, 1);
	g_assert_cmpuint (log.pans, ==, 0);

	/* With one: PAN events in content pixels, then PAN_END */
	g_signal_connect (vp, "pan", G_CALLBACK (on_vp_pan), &log);
	g_signal_connect (vp, "pan-end", G_CALLBACK (on_vp_pan_end), &log);
	touch_frame (canvas, TRUE, cx, cy);
	for (i = 1; i <= 4; i++)
		touch_frame (canvas, TRUE, cx + 10.0f * (gfloat) i, cy);
	touch_frame (canvas, FALSE, 0.0f, 0.0f);
	g_assert_cmpuint (log.presses, ==, 1);
	g_assert_cmpuint (log.pans, >=, 1);
	g_assert_cmpuint (log.pan_ends, ==, 1);
	/* 40 canvas units = 40 * 100 / area.width content pixels */
	g_assert_cmpfloat_with_epsilon (log.pan_dx, 40.0f * 100.0f / area.width, 0.1f);
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

#define GPU_W (256)
#define GPU_H (160)

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
	test_window = grl_window_new (8, 8, "lrg-ui-viewport-test");
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
pixel_near (Image *image,
            gint   x,
            gint   y,
            guint8 r,
            guint8 g,
            guint8 b,
            gint   tol)
{
	Color p = GetImageColor (*image, x, y);

	return ABS ((gint) p.r - (gint) r) <= tol &&
	       ABS ((gint) p.g - (gint) g) <= tol &&
	       ABS ((gint) p.b - (gint) b) <= tol;
}

typedef struct
{
	guint renders;
	guint resized;
	gint  last_w;
	gint  last_h;
} GpuLog;

/* Scene: red background (clear colour) with a green top-left quadrant */
static void
on_render (LrgViewport      *vp,
           GrlRenderTexture *target,
           gfloat            width,
           gfloat            height,
           GpuLog           *log)
{
	log->renders++;
	DrawRectangle (0, 0, (gint) (width * 0.5f), (gint) (height * 0.5f), (Color){ 0, 255, 0, 255 });
}

static void
on_resized (LrgViewport *vp,
            gint         width,
            gint         height,
            GpuLog      *log)
{
	log->resized++;
	log->last_w = width;
	log->last_h = height;
}

static void
test_gpu_render (void)
{
	g_autoptr(LrgCanvas) canvas = NULL;
	g_autoptr(LrgTheme)  theme = NULL;
	LrgViewport         *vp;
	GpuLog               log = { 0 };
	GrlRectangle         area;
	gfloat               radius;
	gfloat               ox;
	gfloat               oy;
	Image                img;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}

	canvas = canvas_new ();
	theme = lrg_theme_new_preset ("midnight");
	lrg_widget_set_theme (LRG_WIDGET (canvas), theme);
	radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);

	vp = lrg_viewport_new ();
	lrg_viewport_set_clear_color (vp, &(GrlColor){ 255, 0, 0, 255 });
	g_signal_connect (vp, "render", G_CALLBACK (on_render), &log);
	g_signal_connect (vp, "resized", G_CALLBACK (on_resized), &log);
	place (canvas, vp, 16.0f, 16.0f, 160.0f, 96.0f);
	lrg_viewport_get_content_rect (vp, &area);
	ox = 16.0f + area.x;
	oy = 16.0f + area.y;

	/* The step renders (outside any paint pass) */
	g_assert_true (lrg_canvas_update (canvas, DT));
	g_assert_cmpuint (log.renders, ==, 1);
	g_assert_cmpuint (log.resized, ==, 1);
	g_assert_nonnull (lrg_viewport_get_render_texture (vp));
	g_assert_cmpint (log.last_w, ==, (gint) roundf (area.width));
	g_assert_cmpint (log.last_h, ==, (gint) roundf (area.height));

	img = render_canvas (canvas);

	/* Upright: green top-left quadrant, red elsewhere */
	g_assert_true (pixel_near (&img, (gint) (ox + area.width * 0.25f), (gint) (oy + area.height * 0.25f),
	                           0, 255, 0, 8));
	g_assert_true (pixel_near (&img, (gint) (ox + area.width * 0.75f), (gint) (oy + area.height * 0.75f),
	                           255, 0, 0, 8));
	g_assert_true (pixel_near (&img, (gint) (ox + area.width * 0.75f), (gint) (oy + area.height * 0.25f),
	                           255, 0, 0, 8));

	/* Nothing outside the widget */
	g_assert_true (pixel_near (&img, 240, 155, 0, 0, 0, 24));

	/* Rounded corners clip the content */
	if (radius > 3.0f)
		g_assert_false (pixel_near (&img, (gint) ox, (gint) oy, 0, 255, 0, 40));
	UnloadImage (img);
}

static void
test_gpu_resize (void)
{
	g_autoptr(LrgCanvas) canvas = NULL;
	LrgViewport         *vp;
	GpuLog               log = { 0 };
	GrlRectangle         area;
	guint                frames;
	Image                img;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}

	canvas = canvas_new ();
	vp = lrg_viewport_new ();
	lrg_viewport_set_clear_color (vp, &(GrlColor){ 255, 0, 0, 255 });
	g_signal_connect (vp, "render", G_CALLBACK (on_render), &log);
	g_signal_connect (vp, "resized", G_CALLBACK (on_resized), &log);
	place (canvas, vp, 0.0f, 0.0f, 100.0f, 60.0f);
	lrg_canvas_update (canvas, DT);
	g_assert_cmpuint (log.resized, ==, 1);

	/* Size keeps changing: no new texture yet, old one stretched */
	lrg_widget_allocate (LRG_WIDGET (vp), 0.0f, 0.0f, 140.0f, 90.0f);
	lrg_canvas_update (canvas, DT);
	lrg_widget_allocate (LRG_WIDGET (vp), 0.0f, 0.0f, 150.0f, 100.0f);
	lrg_canvas_update (canvas, DT);
	g_assert_cmpuint (log.resized, ==, 1);
	img = render_canvas (canvas);
	lrg_viewport_get_content_rect (vp, &area);
	g_assert_true (pixel_near (&img, (gint) (area.x + area.width * 0.25f), (gint) (area.y + area.height * 0.25f),
	                           0, 255, 0, 8));
	g_assert_true (pixel_near (&img, (gint) (area.x + area.width * 0.75f), (gint) (area.y + area.height * 0.75f),
	                           255, 0, 0, 8));
	UnloadImage (img);

	/* Settled for resize-delay: recreated once at the new size */
	for (frames = 0; frames < 30 && log.resized == 1; frames++)
		lrg_canvas_update (canvas, DT);
	g_assert_cmpuint (log.resized, ==, 2);
	g_assert_cmpuint (frames, >=, 4);
	g_assert_cmpint (log.last_w, ==, (gint) roundf (area.width));
	g_assert_cmpint (log.last_h, ==, (gint) roundf (area.height));

	/* On-demand rendering: nothing until queued */
	lrg_viewport_set_continuous (vp, FALSE);
	log.renders = 0;
	lrg_canvas_update (canvas, DT);
	lrg_canvas_update (canvas, DT);
	g_assert_cmpuint (log.renders, ==, 0);
	lrg_viewport_queue_render (vp);
	lrg_canvas_update (canvas, DT);
	lrg_canvas_update (canvas, DT);
	g_assert_cmpuint (log.renders, ==, 1);

	/* Inside another texture mode the render is deferred, not nested */
	{
		RenderTexture2D other = LoadRenderTexture (8, 8);

		lrg_viewport_queue_render (vp);
		BeginTextureMode (other);
		g_test_expect_message ("Libregnum-UI", G_LOG_LEVEL_WARNING, "*render skipped*");
		lrg_canvas_update (canvas, DT);
		EndTextureMode ();
		g_test_assert_expected_messages ();
		g_assert_cmpuint (log.renders, ==, 1);
		lrg_canvas_update (canvas, DT);
		g_assert_cmpuint (log.renders, ==, 2);
		UnloadRenderTexture (other);
	}
}

/* Texture mode: an application render texture, flipped upright */
static void
test_gpu_texture_mode (void)
{
	g_autoptr(LrgCanvas)        canvas = NULL;
	g_autoptr(GrlRenderTexture) app = NULL;
	LrgViewport                *vp;
	GrlRectangle                area;
	Image                       img;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}

	app = grl_render_texture_new (64, 64);
	grl_render_texture_begin (app);
	ClearBackground ((Color){ 0, 0, 255, 255 });
	DrawRectangle (0, 0, 64, 16, (Color){ 255, 255, 0, 255 });   /* yellow top strip */
	grl_render_texture_end (app);

	canvas = canvas_new ();
	vp = lrg_viewport_new ();
	lrg_viewport_set_source_render_texture (vp, app);
	lrg_viewport_set_fit (vp, LRG_VIEWPORT_FIT_CONTAIN);
	g_assert_cmpint (lrg_viewport_get_mode (vp), ==, LRG_VIEWPORT_MODE_TEXTURE);
	g_assert_true (lrg_viewport_get_flip_y (vp));
	place (canvas, vp, 0.0f, 0.0f, 200.0f, 100.0f);
	lrg_canvas_update (canvas, DT);
	g_assert_null (lrg_viewport_get_render_texture (vp));

	img = render_canvas (canvas);
	lrg_viewport_get_content_rect (vp, &area);
	/* Square content centred horizontally; strip at the top */
	g_assert_true (pixel_near (&img, 100, (gint) (area.y + area.height * 0.1f), 255, 255, 0, 8));
	g_assert_true (pixel_near (&img, 100, (gint) (area.y + area.height * 0.7f), 0, 0, 255, 8));
	/* Letterbox bars show the BOX background, not content */
	g_assert_false (pixel_near (&img, (gint) (area.x + 4.0f), 50, 0, 0, 255, 40));
	UnloadImage (img);
}

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	graphics_available = init_graphics_context ();

	g_test_add_func ("/ui/custom/measure", test_custom_measure);
	g_test_add_func ("/ui/custom/paint", test_custom_paint);
	g_test_add_func ("/ui/custom/step", test_custom_step);
	g_test_add_func ("/ui/custom/event", test_custom_event);
	g_test_add_func ("/ui/custom/destroy", test_custom_destroy);
	g_test_add_func ("/ui/viewport/fit-math", test_viewport_fit_math);
	g_test_add_func ("/ui/viewport/layout", test_viewport_layout);
	g_test_add_func ("/ui/viewport/mapping", test_viewport_mapping);
	g_test_add_func ("/ui/viewport/pointer", test_viewport_pointer);
	g_test_add_func ("/ui/viewport/keys", test_viewport_keys);
	g_test_add_func ("/ui/viewport/touch", test_viewport_touch);
	g_test_add_func ("/ui/gpu/viewport-render", test_gpu_render);
	g_test_add_func ("/ui/gpu/viewport-resize", test_gpu_resize);
	g_test_add_func ("/ui/gpu/viewport-texture", test_gpu_texture_mode);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
