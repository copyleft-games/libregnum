/* test-touch.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Touch input without a screen: LrgTouchTracker's gesture classification
 * from synthetic frames (tap, double tap, long press, drag, pinch,
 * two-finger tap and their thresholds), and LrgTouchControls on a fixed
 * 1000x500 screen: finger ownership, the stick (deadzone, run threshold,
 * normalisation), look and zoom, buttons (hit, hold, tap, release), the
 * pass-through of free taps, the sink, auto mode, scale and YAML layouts.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

#define SCREEN_W (1000.0f)
#define SCREEN_H (500.0f)

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static LrgTouchPoint
pt (gint   id,
    gfloat x,
    gfloat y)
{
	LrgTouchPoint p;

	p.id = id;
	p.x = x;
	p.y = y;
	return p;
}

/* one finger */
static void
feed1 (LrgTouchTracker *tracker,
       gint             id,
       gfloat           x,
       gfloat           y,
       gdouble          time)
{
	LrgTouchPoint p = pt (id, x, y);

	lrg_touch_tracker_feed (tracker, &p, 1, time);
}

/* two fingers */
static void
feed2 (LrgTouchTracker *tracker,
       gfloat           ax,
       gfloat           ay,
       gfloat           bx,
       gfloat           by,
       gdouble          time)
{
	LrgTouchPoint p[2];

	p[0] = pt (1, ax, ay);
	p[1] = pt (2, bx, by);
	lrg_touch_tracker_feed (tracker, p, 2, time);
}

static void
feed0 (LrgTouchTracker *tracker,
       gdouble          time)
{
	lrg_touch_tracker_feed (tracker, NULL, 0, time);
}

static const LrgTouchGesture *
find_gesture (LrgTouchTracker     *tracker,
              LrgTouchGestureType  type)
{
	const LrgTouchGesture *gestures;
	guint n;
	guint i;

	gestures = lrg_touch_tracker_get_gestures (tracker, &n);
	for (i = 0; i < n; i++)
		if (gestures[i].type == type)
			return &gestures[i];
	return NULL;
}

/* require_gesture: the gesture of @type, or the test fails */
static const LrgTouchGesture *
require_gesture (LrgTouchTracker     *tracker,
                 LrgTouchGestureType  type)
{
	const LrgTouchGesture *g = find_gesture (tracker, type);

	if (g == NULL)
		g_error ("no gesture of type %d this frame", (gint) type);
	return g;
}

static guint
n_gestures (LrgTouchTracker *tracker)
{
	guint n;

	lrg_touch_tracker_get_gestures (tracker, &n);
	return n;
}

/* ==========================================================================
 * LrgTouchTracker
 * ========================================================================== */

static void
test_tracker_tap (void)
{
	g_autoptr(LrgTouchTracker) t = lrg_touch_tracker_new ();
	const LrgTouchGesture *g;

	feed1 (t, 7, 100, 100, 0.0);
	g_assert_cmpuint (lrg_touch_tracker_get_finger_count (t), ==, 1);
	g_assert_cmpuint (n_gestures (t), ==, 0);
	/* jitter inside the slop is still a tap */
	feed1 (t, 7, 105, 103, 0.1);
	feed0 (t, 0.2);
	g = require_gesture (t, LRG_TOUCH_GESTURE_TAP);
	g_assert_cmpint (g->finger, ==, 7);
	g_assert_cmpfloat (g->x, ==, 105.0f);
	g_assert_cmpfloat (g->start_x, ==, 100.0f);
	g_assert_cmpfloat_with_epsilon (g->duration, 0.2, 1e-9);
	g_assert_null (find_gesture (t, LRG_TOUCH_GESTURE_DOUBLE_TAP));
	g_assert_cmpuint (lrg_touch_tracker_get_finger_count (t), ==, 0);

	/* the next frame forgets it */
	feed0 (t, 0.3);
	g_assert_cmpuint (n_gestures (t), ==, 0);
}

static void
test_tracker_tap_too_slow (void)
{
	g_autoptr(LrgTouchTracker) t = lrg_touch_tracker_new ();

	/* longer than tap-max-duration (0.3 s) but shorter than a long press */
	feed1 (t, 1, 100, 100, 0.0);
	feed1 (t, 1, 100, 100, 0.2);
	feed0 (t, 0.45);
	g_assert_false (lrg_touch_tracker_has_gesture (t, LRG_TOUCH_GESTURE_TAP));
	g_assert_false (lrg_touch_tracker_has_gesture (t, LRG_TOUCH_GESTURE_LONG_PRESS));
}

static void
test_tracker_double_tap (void)
{
	g_autoptr(LrgTouchTracker) t = lrg_touch_tracker_new ();
	const LrgTouchGesture *g;

	feed1 (t, 1, 100, 100, 0.0);
	feed0 (t, 0.1);
	g_assert_true (lrg_touch_tracker_has_gesture (t, LRG_TOUCH_GESTURE_TAP));

	/* the second tap: a tap and a double tap, at once */
	feed1 (t, 2, 110, 105, 0.2);
	feed0 (t, 0.3);
	g_assert_true (lrg_touch_tracker_has_gesture (t, LRG_TOUCH_GESTURE_TAP));
	g = require_gesture (t, LRG_TOUCH_GESTURE_DOUBLE_TAP);
	g_assert_cmpfloat (g->start_x, ==, 100.0f);

	/* a third tap starts over */
	feed1 (t, 3, 100, 100, 0.4);
	feed0 (t, 0.45);
	g_assert_true (lrg_touch_tracker_has_gesture (t, LRG_TOUCH_GESTURE_TAP));
	g_assert_false (lrg_touch_tracker_has_gesture (t, LRG_TOUCH_GESTURE_DOUBLE_TAP));
}

static void
test_tracker_double_tap_limits (void)
{
	g_autoptr(LrgTouchTracker) t = lrg_touch_tracker_new ();

	/* too far apart (slop 40 dp) */
	feed1 (t, 1, 100, 100, 0.0);
	feed0 (t, 0.1);
	feed1 (t, 2, 200, 100, 0.2);
	feed0 (t, 0.3);
	g_assert_false (lrg_touch_tracker_has_gesture (t, LRG_TOUCH_GESTURE_DOUBLE_TAP));

	/* too late (interval 0.3 s) */
	feed1 (t, 3, 200, 100, 0.9);
	feed0 (t, 1.0);
	g_assert_true (lrg_touch_tracker_has_gesture (t, LRG_TOUCH_GESTURE_TAP));
	g_assert_false (lrg_touch_tracker_has_gesture (t, LRG_TOUCH_GESTURE_DOUBLE_TAP));
}

static void
test_tracker_long_press (void)
{
	g_autoptr(LrgTouchTracker) t = lrg_touch_tracker_new ();
	const LrgTouchGesture *g;

	feed1 (t, 1, 300, 200, 0.0);
	feed1 (t, 1, 302, 201, 0.4);
	g_assert_false (lrg_touch_tracker_has_gesture (t, LRG_TOUCH_GESTURE_LONG_PRESS));
	/* reported while still down, once */
	feed1 (t, 1, 302, 201, 0.5);
	g = require_gesture (t, LRG_TOUCH_GESTURE_LONG_PRESS);
	g_assert_cmpfloat (g->x, ==, 302.0f);
	feed1 (t, 1, 302, 201, 0.8);
	g_assert_false (lrg_touch_tracker_has_gesture (t, LRG_TOUCH_GESTURE_LONG_PRESS));
	/* and its lift is not a tap */
	feed0 (t, 0.9);
	g_assert_cmpuint (n_gestures (t), ==, 0);
}

static void
test_tracker_drag (void)
{
	g_autoptr(LrgTouchTracker) t = lrg_touch_tracker_new ();
	const LrgTouchGesture *g;

	feed1 (t, 4, 100, 100, 0.0);
	feed1 (t, 4, 108, 100, 0.016);   /* inside the 10 dp slop */
	g_assert_false (lrg_touch_tracker_is_dragging (t));
	feed1 (t, 4, 130, 110, 0.033);
	g = require_gesture (t, LRG_TOUCH_GESTURE_DRAG_BEGIN);
	g_assert_cmpfloat (g->start_x, ==, 100.0f);
	g_assert_cmpfloat (g->dx, ==, 30.0f);   /* everything since the start */
	g_assert_cmpfloat (g->dy, ==, 10.0f);
	g_assert_true (lrg_touch_tracker_is_dragging (t));

	feed1 (t, 4, 140, 105, 0.05);
	g = require_gesture (t, LRG_TOUCH_GESTURE_DRAG);
	g_assert_cmpfloat (g->dx, ==, 10.0f);
	g_assert_cmpfloat (g->dy, ==, -5.0f);

	/* no movement, no drag event (and no long press for a moved finger) */
	feed1 (t, 4, 140, 105, 1.0);
	g_assert_cmpuint (n_gestures (t), ==, 0);

	feed0 (t, 1.1);
	g_assert_nonnull (find_gesture (t, LRG_TOUCH_GESTURE_DRAG_END));
	g_assert_false (lrg_touch_tracker_has_gesture (t, LRG_TOUCH_GESTURE_TAP));
	g_assert_false (lrg_touch_tracker_is_dragging (t));
}

static void
test_tracker_dp_scale (void)
{
	g_autoptr(LrgTouchTracker) t = lrg_touch_tracker_new ();

	/* on a 3x screen 20 px is under the 10 dp (30 px) slop: still a tap */
	lrg_touch_tracker_set_dp_scale (t, 3.0f);
	g_assert_cmpfloat (lrg_touch_tracker_get_dp_scale (t), ==, 3.0f);
	feed1 (t, 1, 100, 100, 0.0);
	feed1 (t, 1, 120, 100, 0.05);
	g_assert_false (lrg_touch_tracker_is_dragging (t));
	feed0 (t, 0.1);
	g_assert_true (lrg_touch_tracker_has_gesture (t, LRG_TOUCH_GESTURE_TAP));

	/* at 1x the same move drags */
	lrg_touch_tracker_set_dp_scale (t, 1.0f);
	feed1 (t, 2, 100, 100, 1.0);
	feed1 (t, 2, 120, 100, 1.05);
	g_assert_true (lrg_touch_tracker_is_dragging (t));
}

static void
test_tracker_pinch (void)
{
	g_autoptr(LrgTouchTracker) t = lrg_touch_tracker_new ();
	const LrgTouchGesture *g;

	feed2 (t, 400, 200, 500, 200, 0.0);     /* 100 px apart, centre 450 */
	g_assert_cmpuint (n_gestures (t), ==, 0);
	feed2 (t, 375, 200, 525, 200, 0.05);    /* 150 px apart */
	g = require_gesture (t, LRG_TOUCH_GESTURE_PINCH_BEGIN);
	g_assert_cmpfloat_with_epsilon (g->scale, 1.5f, 1e-5);
	g_assert_cmpfloat_with_epsilon (g->total_scale, 1.5f, 1e-5);
	g_assert_cmpfloat (g->x, ==, 450.0f);
	g_assert_true (lrg_touch_tracker_is_pinching (t));

	feed2 (t, 400, 210, 500, 210, 0.1);     /* back to 100, centre down 10 */
	g = require_gesture (t, LRG_TOUCH_GESTURE_PINCH);
	g_assert_cmpfloat_with_epsilon (g->scale, 100.0f / 150.0f, 1e-5);
	g_assert_cmpfloat_with_epsilon (g->total_scale, 1.0f, 1e-5);
	g_assert_cmpfloat (g->dy, ==, 10.0f);

	/* one finger lifts: the pinch ends; the other does not drag */
	{
		LrgTouchPoint p = pt (2, 500, 210);

		lrg_touch_tracker_feed (t, &p, 1, 0.15);
	}
	g_assert_nonnull (find_gesture (t, LRG_TOUCH_GESTURE_PINCH_END));
	g_assert_false (lrg_touch_tracker_is_pinching (t));
	{
		LrgTouchPoint p = pt (2, 600, 300);

		lrg_touch_tracker_feed (t, &p, 1, 0.2);
	}
	g_assert_false (lrg_touch_tracker_is_dragging (t));
	feed0 (t, 0.25);
	g_assert_false (lrg_touch_tracker_has_gesture (t, LRG_TOUCH_GESTURE_TAP));
	g_assert_false (lrg_touch_tracker_has_gesture (t, LRG_TOUCH_GESTURE_TWO_FINGER_TAP));
}

static void
test_tracker_drag_becomes_pinch (void)
{
	g_autoptr(LrgTouchTracker) t = lrg_touch_tracker_new ();

	feed1 (t, 1, 100, 100, 0.0);
	feed1 (t, 1, 150, 100, 0.05);
	g_assert_true (lrg_touch_tracker_is_dragging (t));
	feed2 (t, 150, 100, 250, 100, 0.1);
	g_assert_nonnull (find_gesture (t, LRG_TOUCH_GESTURE_DRAG_END));
	g_assert_false (lrg_touch_tracker_is_dragging (t));
}

static void
test_tracker_two_finger_tap (void)
{
	g_autoptr(LrgTouchTracker) t = lrg_touch_tracker_new ();
	const LrgTouchGesture *g;

	feed1 (t, 1, 100, 100, 0.0);
	feed2 (t, 100, 100, 200, 100, 0.03);
	feed2 (t, 101, 100, 201, 101, 0.1);
	feed0 (t, 0.2);   /* both lift in the same frame */
	g = require_gesture (t, LRG_TOUCH_GESTURE_TWO_FINGER_TAP);
	g_assert_cmpfloat (g->x, ==, 150.0f);
	g_assert_false (lrg_touch_tracker_has_gesture (t, LRG_TOUCH_GESTURE_TAP));

	/* one after the other works too */
	feed2 (t, 100, 100, 200, 100, 1.0);
	{
		LrgTouchPoint p = pt (2, 200, 100);

		lrg_touch_tracker_feed (t, &p, 1, 1.1);
	}
	g_assert_cmpuint (n_gestures (t), ==, 0);
	feed0 (t, 1.15);
	g_assert_true (lrg_touch_tracker_has_gesture (t, LRG_TOUCH_GESTURE_TWO_FINGER_TAP));

	/* too slow: no */
	feed2 (t, 100, 100, 200, 100, 2.0);
	feed2 (t, 100, 100, 200, 100, 2.5);
	feed0 (t, 2.8);
	g_assert_false (lrg_touch_tracker_has_gesture (t, LRG_TOUCH_GESTURE_TWO_FINGER_TAP));

	/* a third finger: no */
	{
		LrgTouchPoint p[3];

		p[0] = pt (1, 100, 100);
		p[1] = pt (2, 200, 100);
		p[2] = pt (3, 300, 100);
		lrg_touch_tracker_feed (t, p, 2, 3.0);
		lrg_touch_tracker_feed (t, p, 3, 3.02);
	}
	feed0 (t, 3.1);
	g_assert_false (lrg_touch_tracker_has_gesture (t, LRG_TOUCH_GESTURE_TWO_FINGER_TAP));
	g_assert_false (lrg_touch_tracker_has_gesture (t, LRG_TOUCH_GESTURE_TAP));
}

static void
count_gesture (LrgTouchTracker       *tracker,
               const LrgTouchGesture *gesture,
               gpointer               user_data)
{
	guint *count = user_data;

	(*count)++;
}

static void
test_tracker_signal_and_reset (void)
{
	g_autoptr(LrgTouchTracker) t = lrg_touch_tracker_new ();
	guint count = 0;

	g_signal_connect (t, "gesture", G_CALLBACK (count_gesture), &count);
	feed1 (t, 1, 100, 100, 0.0);
	feed0 (t, 0.1);
	g_assert_cmpuint (count, ==, 1);

	/* a reset forgets the finger: its lift is nothing */
	feed1 (t, 2, 100, 100, 1.0);
	lrg_touch_tracker_reset (t);
	g_assert_cmpuint (lrg_touch_tracker_get_finger_count (t), ==, 0);
	feed0 (t, 1.1);
	g_assert_cmpuint (count, ==, 1);

	/* duplicate ids count once */
	{
		LrgTouchPoint p[2];

		p[0] = pt (5, 10, 10);
		p[1] = pt (5, 900, 900);
		lrg_touch_tracker_feed (t, p, 2, 2.0);
	}
	g_assert_cmpuint (lrg_touch_tracker_get_finger_count (t), ==, 1);
}

/* ==========================================================================
 * LrgTouchControls
 * ========================================================================== */

typedef struct
{
	LrgTouchControls *controls;
	LrgInputSoftware *sink;
	GPtrArray        *pointer;    /* LrgTouchGesture copies */
	GPtrArray        *pressed;    /* ids */
	GPtrArray        *released;   /* ids */
} Fixture;

static void
on_pointer (LrgTouchControls      *controls,
            const LrgTouchGesture *gesture,
            gpointer               user_data)
{
	Fixture *f = user_data;

	g_ptr_array_add (f->pointer, lrg_touch_gesture_copy (gesture));
}

static void
on_pressed (LrgTouchControls *controls,
            const gchar      *id,
            gpointer          user_data)
{
	Fixture *f = user_data;

	g_ptr_array_add (f->pressed, g_strdup (id));
}

static void
on_released (LrgTouchControls *controls,
             const gchar      *id,
             gpointer          user_data)
{
	Fixture *f = user_data;

	g_ptr_array_add (f->released, g_strdup (id));
}

static void
fixture_set_up (Fixture       *f,
                gconstpointer  data)
{
	f->controls = lrg_touch_controls_new ();
	f->sink = lrg_input_software_new ();
	f->pointer = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_touch_gesture_free);
	f->pressed = g_ptr_array_new_with_free_func (g_free);
	f->released = g_ptr_array_new_with_free_func (g_free);
	lrg_touch_controls_set_screen_size (f->controls, SCREEN_W, SCREEN_H);
	g_object_set (f->controls,
	              "dp-scale", 1.0f,
	              "sink", f->sink,
	              "mode", LRG_TOUCH_CONTROLS_SHOWN,
	              NULL);
	g_signal_connect (f->controls, "pointer-gesture", G_CALLBACK (on_pointer), f);
	g_signal_connect (f->controls, "button-pressed", G_CALLBACK (on_pressed), f);
	g_signal_connect (f->controls, "button-released", G_CALLBACK (on_released), f);
}

static void
fixture_tear_down (Fixture       *f,
                   gconstpointer  data)
{
	g_clear_object (&f->controls);
	g_clear_object (&f->sink);
	g_clear_pointer (&f->pointer, g_ptr_array_unref);
	g_clear_pointer (&f->pressed, g_ptr_array_unref);
	g_clear_pointer (&f->released, g_ptr_array_unref);
}

static void
cfeed (Fixture *f,
       gdouble  time,
       guint    n,
       ...)
{
	LrgTouchPoint points[8];
	va_list args;
	guint i;

	g_assert_cmpuint (n, <=, G_N_ELEMENTS (points));
	va_start (args, n);
	for (i = 0; i < n; i++)
	{
		points[i].id = va_arg (args, gint);
		points[i].x = (gfloat) va_arg (args, gdouble);
		points[i].y = (gfloat) va_arg (args, gdouble);
	}
	va_end (args);
	lrg_touch_controls_feed (f->controls, points, n, time);
}

/* the sink's axis as the input manager would read it */
static gfloat
sink_axis (Fixture        *f,
           GrlGamepadAxis  axis)
{
	return lrg_input_get_gamepad_axis (LRG_INPUT (f->sink), 0, axis);
}

static LrgTouchButton *
make_button (const gchar        *id,
             LrgTouchButtonMode  mode)
{
	LrgTouchButton *button = lrg_touch_button_new (id);

	/* centre (900, 400), 80x80 dp: x 860..940, y 360..440 */
	lrg_touch_button_set_placement (button, LRG_TOUCH_ANCHOR_BOTTOM_RIGHT, -100.0f, -100.0f, 80.0f, 80.0f);
	g_object_set (button, "mode", mode, "shape", LRG_TOUCH_BUTTON_ROUNDED, NULL);
	return button;
}

static void
test_controls_stick (Fixture       *f,
                     gconstpointer  data)
{
	gfloat x, y;

	/* default stick region: x 0..450, y 175..500; radius 60, deadzone 0.15 */
	cfeed (f, 0.0, 1, 1, 200.0, 400.0);
	g_assert_true (lrg_touch_controls_is_stick_active (f->controls));
	lrg_touch_controls_get_stick (f->controls, &x, &y);
	g_assert_cmpfloat (x, ==, 0.0f);
	g_assert_cmpfloat (y, ==, 0.0f);

	/* inside the deadzone (6 px of 60 = 0.1) */
	cfeed (f, 0.016, 1, 1, 206.0, 400.0);
	lrg_touch_controls_get_stick (f->controls, &x, &y);
	g_assert_cmpfloat (x, ==, 0.0f);

	/* half way: (0.5 - 0.15) / 0.85 */
	cfeed (f, 0.033, 1, 1, 230.0, 400.0);
	lrg_touch_controls_get_stick (f->controls, &x, &y);
	g_assert_cmpfloat_with_epsilon (x, 0.35f / 0.85f, 1e-5);
	g_assert_cmpfloat (y, ==, 0.0f);
	g_assert_false (lrg_touch_controls_is_running (f->controls));
	g_assert_cmpfloat_with_epsilon (sink_axis (f, GRL_GAMEPAD_AXIS_LEFT_X), 0.35f / 0.85f, 1e-5);

	/* beyond the radius, up: clamped to length 1, y negative, running */
	cfeed (f, 0.05, 1, 1, 200.0, 200.0);
	lrg_touch_controls_get_stick (f->controls, &x, &y);
	g_assert_cmpfloat_with_epsilon (x, 0.0f, 1e-6);
	g_assert_cmpfloat_with_epsilon (y, -1.0f, 1e-6);
	g_assert_true (lrg_touch_controls_is_running (f->controls));
	g_assert_cmpfloat_with_epsilon (sink_axis (f, GRL_GAMEPAD_AXIS_LEFT_Y), -1.0f, 1e-6);

	/* diagonal: the vector's length is at most 1 */
	cfeed (f, 0.066, 1, 1, 300.0, 500.0);
	lrg_touch_controls_get_stick (f->controls, &x, &y);
	g_assert_cmpfloat_with_epsilon (sqrtf (x * x + y * y), 1.0f, 1e-5);
	g_assert_cmpfloat_with_epsilon (x, y, 1e-5);

	/* run threshold */
	g_object_set (f->controls, "stick-run-threshold", 0.3f, NULL);
	cfeed (f, 0.08, 1, 1, 230.0, 400.0);
	g_assert_true (lrg_touch_controls_is_running (f->controls));

	/* lift: back to rest */
	cfeed (f, 0.1, 0);
	g_assert_false (lrg_touch_controls_is_stick_active (f->controls));
	lrg_touch_controls_get_stick (f->controls, &x, &y);
	g_assert_cmpfloat (x, ==, 0.0f);
	g_assert_cmpfloat (sink_axis (f, GRL_GAMEPAD_AXIS_LEFT_X), ==, 0.0f);
	g_assert_cmpuint (f->pointer->len, ==, 0);
}

static void
test_controls_stick_fixed_and_side (Fixture       *f,
                                    gconstpointer  data)
{
	g_autoptr(GrlRectangle) region = grl_rectangle_new (0.0f, 0.5f, 0.4f, 0.5f);
	gfloat x;

	/* fixed: centred on the region's centre (200, 375), not the finger */
	g_object_set (f->controls,
	              "stick-region", region,
	              "stick-floating", FALSE,
	              "stick-side", LRG_TOUCH_STICK_RIGHT,
	              NULL);
	cfeed (f, 0.0, 1, 1, 230.0, 375.0);
	lrg_touch_controls_get_stick (f->controls, &x, NULL);
	g_assert_cmpfloat_with_epsilon (x, 0.35f / 0.85f, 1e-5);
	g_assert_cmpfloat_with_epsilon (sink_axis (f, GRL_GAMEPAD_AXIS_RIGHT_X), 0.35f / 0.85f, 1e-5);
	g_assert_cmpfloat (sink_axis (f, GRL_GAMEPAD_AXIS_LEFT_X), ==, 0.0f);

	/* outside the region: not the stick */
	cfeed (f, 0.1, 0);
	cfeed (f, 0.2, 1, 2, 200.0, 100.0);
	g_assert_false (lrg_touch_controls_is_stick_active (f->controls));
}

static void
test_controls_look (Fixture       *f,
                    gconstpointer  data)
{
	gfloat dx, dy;

	g_object_set (f->controls, "look-sensitivity", 2.0f, "look-axis-gain", 0.01f, NULL);
	/* look region: x 450..1000 */
	cfeed (f, 0.0, 1, 1, 700.0, 250.0);
	lrg_touch_controls_get_look_delta (f->controls, &dx, &dy);
	g_assert_cmpfloat (dx, ==, 0.0f);
	cfeed (f, 0.016, 1, 1, 730.0, 240.0);   /* past the slop: drag begins */
	lrg_touch_controls_get_look_delta (f->controls, &dx, &dy);
	g_assert_cmpfloat (dx, ==, 60.0f);
	g_assert_cmpfloat (dy, ==, -20.0f);
	g_assert_cmpfloat_with_epsilon (sink_axis (f, GRL_GAMEPAD_AXIS_RIGHT_X), 0.6f, 1e-5);
	cfeed (f, 0.033, 1, 1, 735.0, 240.0);
	lrg_touch_controls_get_look_delta (f->controls, &dx, &dy);
	g_assert_cmpfloat (dx, ==, 10.0f);
	g_assert_cmpfloat (dy, ==, 0.0f);
	/* still: no delta, axis back to 0 */
	cfeed (f, 0.05, 1, 1, 735.0, 240.0);
	lrg_touch_controls_get_look_delta (f->controls, &dx, &dy);
	g_assert_cmpfloat (dx, ==, 0.0f);
	g_assert_cmpfloat (sink_axis (f, GRL_GAMEPAD_AXIS_RIGHT_X), ==, 0.0f);
	cfeed (f, 0.066, 0);
	/* a drag in the look region is the camera's, not the game's */
	g_assert_cmpuint (f->pointer->len, ==, 0);

	/* pinch: zoom factor per frame */
	cfeed (f, 1.0, 2, 1, 600.0, 250.0, 2, 700.0, 250.0);
	g_assert_cmpfloat (lrg_touch_controls_get_zoom_delta (f->controls), ==, 1.0f);
	cfeed (f, 1.02, 2, 1, 575.0, 250.0, 2, 725.0, 250.0);
	g_assert_cmpfloat_with_epsilon (lrg_touch_controls_get_zoom_delta (f->controls), 1.5f, 1e-5);
	cfeed (f, 1.04, 2, 1, 575.0, 250.0, 2, 725.0, 250.0);
	g_assert_cmpfloat (lrg_touch_controls_get_zoom_delta (f->controls), ==, 1.0f);
	cfeed (f, 1.06, 0);
	g_assert_cmpuint (f->pointer->len, ==, 0);
}

static void
test_controls_pointer_passthrough (Fixture       *f,
                                   gconstpointer  data)
{
	LrgTouchGesture *g;
	gfloat mx, my;

	/* a tap in the look region (no movement): the game's click */
	cfeed (f, 0.0, 1, 1, 700.0, 100.0);
	cfeed (f, 0.1, 0);
	g_assert_cmpuint (f->pointer->len, ==, 1);
	g = g_ptr_array_index (f->pointer, 0);
	g_assert_cmpint (g->type, ==, LRG_TOUCH_GESTURE_TAP);
	g_assert_cmpfloat (g->x, ==, 700.0f);
	/* emulated: the sink's mouse there, left button pressed until the next feed */
	lrg_input_get_mouse_position (LRG_INPUT (f->sink), &mx, &my);
	g_assert_cmpfloat (mx, ==, 700.0f);
	g_assert_cmpfloat (my, ==, 100.0f);
	g_assert_true (lrg_input_is_mouse_button_down (LRG_INPUT (f->sink), GRL_MOUSE_BUTTON_LEFT));
	lrg_input_poll (LRG_INPUT (f->sink));
	g_assert_true (lrg_input_is_mouse_button_pressed (LRG_INPUT (f->sink), GRL_MOUSE_BUTTON_LEFT));
	cfeed (f, 0.2, 0);
	g_assert_false (lrg_input_is_mouse_button_down (LRG_INPUT (f->sink), GRL_MOUSE_BUTTON_LEFT));

	/* a long press outside every widget (top-left): right click */
	cfeed (f, 1.0, 1, 2, 50.0, 50.0);
	cfeed (f, 1.6, 1, 2, 50.0, 50.0);
	g_assert_cmpuint (f->pointer->len, ==, 2);
	g = g_ptr_array_index (f->pointer, 1);
	g_assert_cmpint (g->type, ==, LRG_TOUCH_GESTURE_LONG_PRESS);
	g_assert_true (lrg_input_is_mouse_button_down (LRG_INPUT (f->sink), GRL_MOUSE_BUTTON_RIGHT));
	cfeed (f, 1.7, 0);
	g_assert_false (lrg_input_is_mouse_button_down (LRG_INPUT (f->sink), GRL_MOUSE_BUTTON_RIGHT));

	/* a drag outside the look region is the game's too */
	cfeed (f, 2.0, 1, 3, 50.0, 50.0);
	cfeed (f, 2.02, 1, 3, 100.0, 50.0);
	g = g_ptr_array_index (f->pointer, f->pointer->len - 1);
	g_assert_cmpint (g->type, ==, LRG_TOUCH_GESTURE_DRAG_BEGIN);

	/* without emulation: the signal only */
	cfeed (f, 2.1, 0);
	g_object_set (f->controls, "pointer-emulation", FALSE, NULL);
	cfeed (f, 3.0, 1, 4, 700.0, 100.0);
	cfeed (f, 3.1, 0);
	g_assert_false (lrg_input_is_mouse_button_down (LRG_INPUT (f->sink), GRL_MOUSE_BUTTON_LEFT));
	g = g_ptr_array_index (f->pointer, f->pointer->len - 1);
	g_assert_cmpint (g->type, ==, LRG_TOUCH_GESTURE_TAP);
}

static void
test_controls_hold_button (Fixture       *f,
                           gconstpointer  data)
{
	g_autoptr(LrgTouchButton) jump = make_button ("jump", LRG_TOUCH_BUTTON_HOLD);

	g_object_set (jump, "key", GRL_KEY_SPACE, NULL);
	lrg_touch_controls_add_button (f->controls, jump);
	g_assert_cmpuint (lrg_touch_controls_get_n_buttons (f->controls), ==, 1);
	g_assert_true (lrg_touch_controls_get_button (f->controls, "jump") == jump);
	g_assert_true (lrg_touch_controls_get_nth_button (f->controls, 0) == jump);

	/* a miss (just outside the rect): the game's tap */
	cfeed (f, 0.0, 1, 1, 855.0, 400.0);
	g_assert_false (lrg_touch_controls_is_button_down (f->controls, "jump"));
	cfeed (f, 0.05, 0);
	g_assert_cmpuint (f->pointer->len, ==, 1);

	/* a hit: pressed at once, held, the key down in the sink */
	cfeed (f, 1.0, 1, 2, 900.0, 400.0);
	g_assert_true (lrg_touch_controls_is_button_pressed (f->controls, "jump"));
	g_assert_true (lrg_touch_controls_is_button_down (f->controls, "jump"));
	g_assert_cmpuint (f->pressed->len, ==, 1);
	g_assert_true (lrg_input_is_key_down (LRG_INPUT (f->sink), GRL_KEY_SPACE));

	/* moving off the button keeps it (the finger is the button's) */
	cfeed (f, 1.1, 1, 2, 500.0, 100.0);
	g_assert_false (lrg_touch_controls_is_button_pressed (f->controls, "jump"));
	g_assert_true (lrg_touch_controls_is_button_down (f->controls, "jump"));
	g_assert_cmpuint (f->pointer->len, ==, 1);

	/* lift: released */
	cfeed (f, 1.2, 0);
	g_assert_true (lrg_touch_controls_is_button_released (f->controls, "jump"));
	g_assert_false (lrg_touch_controls_is_button_down (f->controls, "jump"));
	g_assert_cmpuint (f->released->len, ==, 1);
	g_assert_false (lrg_input_is_key_down (LRG_INPUT (f->sink), GRL_KEY_SPACE));
}

static void
test_controls_tap_button (Fixture       *f,
                          gconstpointer  data)
{
	g_autoptr(LrgTouchButton) menu = make_button ("menu", LRG_TOUCH_BUTTON_TAP);

	g_object_set (menu, "gamepad-button", GRL_GAMEPAD_BUTTON_MIDDLE_RIGHT, NULL);
	lrg_touch_controls_add_button (f->controls, menu);

	/* down: shown down, nothing pressed yet */
	cfeed (f, 0.0, 1, 1, 900.0, 400.0);
	g_assert_true (lrg_touch_controls_is_button_down (f->controls, "menu"));
	g_assert_false (lrg_touch_controls_is_button_pressed (f->controls, "menu"));
	g_assert_cmpuint (f->pressed->len, ==, 0);

	/* lift inside: pressed and released at once, a click in the sink */
	cfeed (f, 0.1, 0);
	g_assert_true (lrg_touch_controls_is_button_pressed (f->controls, "menu"));
	g_assert_true (lrg_touch_controls_is_button_released (f->controls, "menu"));
	g_assert_cmpuint (f->pressed->len, ==, 1);
	g_assert_true (lrg_input_is_gamepad_button_down (LRG_INPUT (f->sink), 0, GRL_GAMEPAD_BUTTON_MIDDLE_RIGHT));
	cfeed (f, 0.2, 0);
	g_assert_false (lrg_input_is_gamepad_button_down (LRG_INPUT (f->sink), 0, GRL_GAMEPAD_BUTTON_MIDDLE_RIGHT));
	g_assert_false (lrg_touch_controls_is_button_pressed (f->controls, "menu"));

	/* slide off and lift outside: cancelled */
	cfeed (f, 1.0, 1, 2, 900.0, 400.0);
	cfeed (f, 1.1, 1, 2, 600.0, 100.0);
	cfeed (f, 1.2, 0);
	g_assert_false (lrg_touch_controls_is_button_pressed (f->controls, "menu"));
	g_assert_cmpuint (f->pressed->len, ==, 1);
}

static void
test_controls_button_from_input_map (Fixture       *f,
                                     gconstpointer  data)
{
	g_autoptr(LrgInputMap) map = lrg_input_map_new ();
	g_autoptr(LrgInputAction) attack = lrg_input_action_new ("attack");
	g_autoptr(LrgInputBinding) pad = lrg_input_binding_new_gamepad_button (1, GRL_GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
	g_autoptr(LrgTouchButton) button = make_button ("attack-button", LRG_TOUCH_BUTTON_HOLD);

	lrg_input_action_add_binding (attack, pad);
	lrg_input_map_add_action (map, attack);
	g_object_set (button, "action", "attack", NULL);
	g_object_set (f->controls, "input-map", map, NULL);
	lrg_touch_controls_add_button (f->controls, button);

	cfeed (f, 0.0, 1, 1, 900.0, 400.0);
	g_assert_true (lrg_input_is_gamepad_button_down (LRG_INPUT (f->sink), 1, GRL_GAMEPAD_BUTTON_RIGHT_FACE_LEFT));
	cfeed (f, 0.1, 0);
	g_assert_false (lrg_input_is_gamepad_button_down (LRG_INPUT (f->sink), 1, GRL_GAMEPAD_BUTTON_RIGHT_FACE_LEFT));
}

static void
test_controls_ownership (Fixture       *f,
                         gconstpointer  data)
{
	g_autoptr(LrgTouchButton) jump = make_button ("jump", LRG_TOUCH_BUTTON_HOLD);
	gfloat x, dx;

	lrg_touch_controls_add_button (f->controls, jump);

	/* three fingers at once: stick, look, button */
	cfeed (f, 0.0, 3, 1, 200.0, 400.0, 2, 700.0, 200.0, 3, 900.0, 400.0);
	g_assert_true (lrg_touch_controls_is_stick_active (f->controls));
	g_assert_true (lrg_touch_controls_is_button_down (f->controls, "jump"));

	/* each moves; only its owner sees it */
	cfeed (f, 0.02, 3, 1, 260.0, 400.0, 2, 760.0, 200.0, 3, 900.0, 400.0);
	lrg_touch_controls_get_stick (f->controls, &x, NULL);
	g_assert_cmpfloat_with_epsilon (x, 1.0f, 1e-5);
	lrg_touch_controls_get_look_delta (f->controls, &dx, NULL);
	g_assert_cmpfloat (dx, ==, 60.0f);

	/* a second finger in the stick region while the stick is held is the
	 * game's (x 300 is outside the look region): its tap passes through
	 * although the look finger is still dragging */
	cfeed (f, 0.04, 4, 1, 260.0, 400.0, 2, 760.0, 200.0, 3, 900.0, 400.0, 4, 300.0, 300.0);
	cfeed (f, 0.06, 3, 1, 260.0, 400.0, 2, 760.0, 200.0, 3, 900.0, 400.0);
	g_assert_cmpuint (f->pointer->len, ==, 1);
	g_assert_cmpint (((LrgTouchGesture *) g_ptr_array_index (f->pointer, 0))->type, ==, LRG_TOUCH_GESTURE_TAP);
	g_assert_true (lrg_touch_controls_is_stick_active (f->controls));

	/* the stick's finger lifts; the button stays */
	cfeed (f, 0.08, 2, 2, 760.0, 200.0, 3, 900.0, 400.0);
	g_assert_false (lrg_touch_controls_is_stick_active (f->controls));
	g_assert_true (lrg_touch_controls_is_button_down (f->controls, "jump"));

	/* removing a held button releases it; its finger claims nothing else */
	g_assert_true (lrg_touch_controls_remove_button (f->controls, "jump"));
	g_assert_false (lrg_touch_controls_remove_button (f->controls, "jump"));
	g_assert_cmpuint (f->released->len, ==, 1);
	cfeed (f, 0.1, 2, 2, 760.0, 200.0, 3, 200.0, 400.0);
	g_assert_false (lrg_touch_controls_is_stick_active (f->controls));
	cfeed (f, 0.12, 0);
	g_assert_cmpuint (f->pointer->len, ==, 1);
}

static void
test_controls_scale (Fixture       *f,
                     gconstpointer  data)
{
	g_autoptr(LrgTouchButton) jump = make_button ("jump", LRG_TOUCH_BUTTON_HOLD);
	gfloat x, y, w, h;

	lrg_touch_controls_add_button (f->controls, jump);

	/* scale 1.5: centre (1000 - 150, 500 - 150), 120x120 */
	g_object_set (f->controls, "scale", 1.5f, NULL);
	lrg_touch_button_get_rect (jump, SCREEN_W, SCREEN_H, 1.5f, &x, &y, &w, &h);
	g_assert_cmpfloat (x, ==, 790.0f);
	g_assert_cmpfloat (y, ==, 290.0f);
	g_assert_cmpfloat (w, ==, 120.0f);
	cfeed (f, 0.0, 1, 1, 800.0, 300.0);   /* outside at 1x, inside at 1.5x */
	g_assert_true (lrg_touch_controls_is_button_down (f->controls, "jump"));
	cfeed (f, 0.1, 0);

	/* dp scale works the same way, and reaches the stick radius */
	g_object_set (f->controls, "scale", 1.0f, "dp-scale", 2.0f, NULL);
	g_assert_cmpfloat (lrg_touch_tracker_get_dp_scale (lrg_touch_controls_get_tracker (f->controls)), ==, 2.0f);
	cfeed (f, 1.0, 1, 2, 100.0, 400.0);
	cfeed (f, 1.1, 1, 2, 160.0, 400.0);   /* 60 px of a 120 px radius */
	lrg_touch_controls_get_stick (f->controls, &x, NULL);
	g_assert_cmpfloat_with_epsilon (x, 0.35f / 0.85f, 1e-5);

	/* circle buttons hit inside the ellipse only */
	g_object_set (jump, "shape", LRG_TOUCH_BUTTON_CIRCLE, NULL);
	g_assert_true (lrg_touch_button_hit (jump, SCREEN_W, SCREEN_H, 1.0f, 900.0f, 400.0f));
	g_assert_false (lrg_touch_button_hit (jump, SCREEN_W, SCREEN_H, 1.0f, 862.0f, 362.0f));
	g_object_set (jump, "shape", LRG_TOUCH_BUTTON_ROUNDED, NULL);
	g_assert_true (lrg_touch_button_hit (jump, SCREEN_W, SCREEN_H, 1.0f, 862.0f, 362.0f));
}

static void
test_controls_modes (Fixture       *f,
                     gconstpointer  data)
{
	g_autoptr(LrgTouchButton) jump = make_button ("jump", LRG_TOUCH_BUTTON_HOLD);

	g_object_set (jump, "key", GRL_KEY_SPACE, NULL);
	lrg_touch_controls_add_button (f->controls, jump);

	/* auto: hidden until a touch */
	g_object_set (f->controls, "mode", LRG_TOUCH_CONTROLS_AUTO, "auto-hide-delay", 5.0, NULL);
	g_assert_true (lrg_touch_controls_is_visible (f->controls));   /* was shown */
	g_object_set (f->controls, "mode", LRG_TOUCH_CONTROLS_HIDDEN, NULL);
	g_assert_false (lrg_touch_controls_is_visible (f->controls));
	g_object_set (f->controls, "mode", LRG_TOUCH_CONTROLS_AUTO, NULL);
	cfeed (f, 0.0, 0);
	g_assert_false (lrg_touch_controls_is_visible (f->controls));

	/* the first touch shows them and is used at once */
	cfeed (f, 1.0, 1, 1, 900.0, 400.0);
	g_assert_true (lrg_touch_controls_is_visible (f->controls));
	g_assert_true (lrg_touch_controls_is_button_down (f->controls, "jump"));

	/* keyboard only for less than the delay: still shown */
	lrg_touch_controls_note_other_input (f->controls, 2.0);
	cfeed (f, 2.0, 1, 1, 900.0, 400.0);   /* the finger still down resets it */
	lrg_touch_controls_note_other_input (f->controls, 3.0);
	cfeed (f, 7.9, 0);
	g_assert_true (lrg_touch_controls_is_visible (f->controls));
	/* then hidden: everything released */
	cfeed (f, 8.0, 0);
	g_assert_false (lrg_touch_controls_is_visible (f->controls));
	g_assert_false (lrg_input_is_key_down (LRG_INPUT (f->sink), GRL_KEY_SPACE));

	/* hidden controls claim nothing */
	g_object_set (f->controls, "mode", LRG_TOUCH_CONTROLS_HIDDEN, NULL);
	cfeed (f, 9.0, 1, 2, 900.0, 400.0);
	g_assert_false (lrg_touch_controls_is_visible (f->controls));
	g_assert_false (lrg_touch_controls_is_button_down (f->controls, "jump"));
	cfeed (f, 9.1, 0);
	g_assert_cmpuint (f->pointer->len, ==, 0);
}

static void
test_controls_mouse_source (Fixture       *f,
                            gconstpointer  data)
{
	g_autoptr(LrgInputManager) manager = g_object_new (LRG_TYPE_INPUT_MANAGER, NULL);
	g_autoptr(LrgInput) mouse = (LrgInput *) lrg_input_mock_new ();
	LrgInputSoftware *sink;

	g_object_set (mouse, "name", "mouse", NULL);
	lrg_input_manager_add_source (manager, mouse);
	lrg_touch_controls_attach (f->controls, manager);
	g_object_get (f->controls, "sink", &sink, NULL);
	g_assert_nonnull (sink);
	g_assert_true (lrg_input_manager_get_source (manager, "touch-controls") == LRG_INPUT (sink));

	/* shown with pointer emulation: the raw mouse is off */
	g_assert_false (lrg_input_get_enabled (mouse));
	g_object_set (f->controls, "pointer-emulation", FALSE, NULL);
	g_assert_true (lrg_input_get_enabled (mouse));
	g_object_set (f->controls, "pointer-emulation", TRUE, NULL);
	g_assert_false (lrg_input_get_enabled (mouse));
	g_object_set (f->controls, "mode", LRG_TOUCH_CONTROLS_HIDDEN, NULL);
	g_assert_true (lrg_input_get_enabled (mouse));

	/* the stick reaches the manager's gamepad axis through the sink */
	g_object_set (f->controls, "mode", LRG_TOUCH_CONTROLS_SHOWN, NULL);
	cfeed (f, 0.0, 1, 1, 200.0, 400.0);
	cfeed (f, 0.02, 1, 1, 260.0, 400.0);
	g_assert_cmpfloat_with_epsilon (lrg_input_manager_get_gamepad_axis (manager, 0, GRL_GAMEPAD_AXIS_LEFT_X),
	                                1.0f, 1e-5);

	/* re-attaching replaces the sink */
	lrg_touch_controls_attach (f->controls, manager);
	g_assert_cmpuint (lrg_input_manager_get_sources (manager)->len, ==, 2);
	g_object_unref (sink);

	/* disposing the controls gives the mouse back */
	g_clear_object (&f->controls);
	g_assert_true (lrg_input_get_enabled (mouse));
}

/* ==========================================================================
 * Layouts
 * ========================================================================== */

static const gchar *GOOD_LAYOUT =
	"scale: 1.25\n"
	"opacity: 0.4\n"
	"stick:\n"
	"  region: [0.0, 0.5, 0.4, 0.5]\n"
	"  radius: 50\n"
	"  deadzone: 0.2\n"
	"  run-threshold: 0.7\n"
	"  floating: false\n"
	"  side: left\n"
	"look:\n"
	"  region: [0.5, 0.0, 0.5, 1.0]\n"
	"  sensitivity: 1.5\n"
	"  axis-gain: 0.02\n"
	"buttons:\n"
	"  - id: jump\n"
	"    label: Jump\n"
	"    icon: arrow-up\n"
	"    anchor: bottom-right\n"
	"    offset: [-60, -60]\n"
	"    size: [72, 72]\n"
	"    mode: hold\n"
	"    shape: circle\n"
	"    key: space\n"
	"  - id: bag\n"
	"    action: open-bags\n"
	"    anchor: top-right\n"
	"    offset: [-40, 40]\n"
	"    size: [56, 40]\n"
	"    mode: tap\n"
	"    shape: rounded\n"
	"    gamepad-button: middle-right\n";

static void
test_layout_load (Fixture       *f,
                  gconstpointer  data)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GrlRectangle) region = NULL;
	LrgTouchButton *jump, *bag;
	gfloat scale = 0.0f, opacity = 0.0f, radius = 0.0f;
	gboolean floating = TRUE;
	GrlKey key = GRL_KEY_NULL;
	LrgTouchAnchor anchor = LRG_TOUCH_ANCHOR_CENTER;

	g_assert_true (lrg_touch_controls_load_from_data (f->controls, GOOD_LAYOUT, -1, &error));
	g_assert_no_error (error);

	g_object_get (f->controls, "scale", &scale, "opacity", &opacity, "stick-radius", &radius,
	              "stick-floating", &floating, "stick-region", &region, NULL);
	g_assert_cmpfloat (scale, ==, 1.25f);
	g_assert_cmpfloat (opacity, ==, 0.4f);
	g_assert_cmpfloat (radius, ==, 50.0f);
	g_assert_false (floating);
	g_assert_cmpfloat (region->y, ==, 0.5f);
	g_assert_cmpfloat (region->width, ==, 0.4f);

	g_assert_cmpuint (lrg_touch_controls_get_n_buttons (f->controls), ==, 2);
	jump = lrg_touch_controls_get_button (f->controls, "jump");
	bag = lrg_touch_controls_get_button (f->controls, "bag");
	g_assert_nonnull (jump);
	g_assert_nonnull (bag);
	g_assert_cmpstr (lrg_touch_button_get_label (jump), ==, "Jump");
	g_assert_cmpstr (lrg_touch_button_get_icon (jump), ==, "arrow-up");
	g_assert_cmpstr (lrg_touch_button_get_action (jump), ==, "jump");
	g_assert_cmpstr (lrg_touch_button_get_action (bag), ==, "open-bags");
	g_assert_cmpint (lrg_touch_button_get_mode (bag), ==, LRG_TOUCH_BUTTON_TAP);
	g_object_get (jump, "key", &key, NULL);
	g_assert_cmpint (key, ==, GRL_KEY_SPACE);
	g_object_get (bag, "anchor", &anchor, NULL);
	g_assert_cmpint (anchor, ==, LRG_TOUCH_ANCHOR_TOP_RIGHT);

	/* the loaded jump works: centre (1000 - 75, 500 - 75) at scale 1.25 */
	cfeed (f, 0.0, 1, 1, 925.0, 425.0);
	g_assert_true (lrg_input_is_key_down (LRG_INPUT (f->sink), GRL_KEY_SPACE));
	cfeed (f, 0.1, 0);

	/* a layout without a stick or look section has neither */
	g_assert_true (lrg_touch_controls_load_from_data (f->controls, "buttons: []\n", -1, &error));
	g_assert_no_error (error);
	g_object_get (f->controls, "stick-enabled", &floating, NULL);
	g_assert_false (floating);
	g_assert_cmpuint (lrg_touch_controls_get_n_buttons (f->controls), ==, 0);
	/* scale and opacity were not given: unchanged */
	g_object_get (f->controls, "scale", &scale, NULL);
	g_assert_cmpfloat (scale, ==, 1.25f);
}

static void
test_layout_file (Fixture       *f,
                  gconstpointer  data)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = g_dir_make_tmp ("lrg-touch-XXXXXX", &error);
	g_autofree gchar *path = NULL;

	g_assert_no_error (error);
	path = g_build_filename (dir, "layout.yaml", NULL);
	g_assert_true (g_file_set_contents (path, GOOD_LAYOUT, -1, NULL));
	g_assert_true (lrg_touch_controls_load_from_file (f->controls, path, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_touch_controls_get_n_buttons (f->controls), ==, 2);
	g_assert_cmpint (g_unlink (path), ==, 0);
	g_assert_cmpint (g_rmdir (dir), ==, 0);

	g_assert_false (lrg_touch_controls_load_from_file (f->controls, path, &error));
	g_assert_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
	g_assert_cmpuint (lrg_touch_controls_get_n_buttons (f->controls), ==, 2);
}

static void
test_layout_bad_unchanged (Fixture       *f,
                           gconstpointer  data)
{
	static const struct
	{
		const gchar *yaml;
		gint         code;
	} bad[] = {
		{ "stick: [unclosed\n", LRG_TOUCH_CONTROLS_ERROR_PARSE },
		{ "- a list\n", LRG_TOUCH_CONTROLS_ERROR_INVALID },
		{ "colour: red\n", LRG_TOUCH_CONTROLS_ERROR_INVALID },
		{ "scale: 9\n", LRG_TOUCH_CONTROLS_ERROR_INVALID },
		{ "scale: big\n", LRG_TOUCH_CONTROLS_ERROR_INVALID },
		{ "opacity: 0.5x\n", LRG_TOUCH_CONTROLS_ERROR_INVALID },
		{ "stick: 3\n", LRG_TOUCH_CONTROLS_ERROR_INVALID },
		{ "stick:\n  region: [0, 0, 1]\n", LRG_TOUCH_CONTROLS_ERROR_INVALID },
		{ "stick:\n  region: [0.5, 0, 0.6, 1]\n", LRG_TOUCH_CONTROLS_ERROR_INVALID },
		{ "stick:\n  deadzone: 1.0\n", LRG_TOUCH_CONTROLS_ERROR_INVALID },
		{ "stick:\n  floating: maybe\n", LRG_TOUCH_CONTROLS_ERROR_INVALID },
		{ "stick:\n  side: middle\n", LRG_TOUCH_CONTROLS_ERROR_INVALID },
		{ "look:\n  speed: 2\n", LRG_TOUCH_CONTROLS_ERROR_INVALID },
		{ "buttons: {}\n", LRG_TOUCH_CONTROLS_ERROR_INVALID },
		{ "buttons:\n  - label: no id\n", LRG_TOUCH_CONTROLS_ERROR_INVALID },
		{ "buttons:\n  - id: a\n  - id: a\n", LRG_TOUCH_CONTROLS_ERROR_INVALID },
		{ "buttons:\n  - id: a\n    anchor: middle\n", LRG_TOUCH_CONTROLS_ERROR_INVALID },
		{ "buttons:\n  - id: a\n    size: [0, 10]\n", LRG_TOUCH_CONTROLS_ERROR_INVALID },
		{ "buttons:\n  - id: a\n    key: not-a-key\n", LRG_TOUCH_CONTROLS_ERROR_INVALID },
		{ "buttons:\n  - id: a\n    colour: red\n", LRG_TOUCH_CONTROLS_ERROR_INVALID },
		{ "buttons:\n  - id: ok\n  - id: b\n    mode: toggle\n", LRG_TOUCH_CONTROLS_ERROR_INVALID },
	};
	gfloat scale = 0.0f;
	guint i;

	g_assert_true (lrg_touch_controls_load_from_data (f->controls, GOOD_LAYOUT, -1, NULL));
	for (i = 0; i < G_N_ELEMENTS (bad); i++)
	{
		g_autoptr(GError) error = NULL;
		gboolean stick = FALSE;

		g_test_message ("bad layout %u: %s", i, bad[i].yaml);
		g_assert_false (lrg_touch_controls_load_from_data (f->controls, bad[i].yaml, -1, &error));
		g_assert_error (error, LRG_TOUCH_CONTROLS_ERROR, bad[i].code);

		/* nothing changed */
		g_assert_cmpuint (lrg_touch_controls_get_n_buttons (f->controls), ==, 2);
		g_assert_nonnull (lrg_touch_controls_get_button (f->controls, "jump"));
		g_assert_null (lrg_touch_controls_get_button (f->controls, "ok"));
		g_object_get (f->controls, "scale", &scale, "stick-enabled", &stick, NULL);
		g_assert_cmpfloat (scale, ==, 1.25f);
		g_assert_true (stick);
	}
}

static void
test_controls_draw_hidden (Fixture       *f,
                           gconstpointer  data)
{
	/* hidden controls draw nothing (and need no window for it) */
	g_object_set (f->controls, "mode", LRG_TOUCH_CONTROLS_HIDDEN, NULL);
	lrg_touch_controls_draw (f->controls);
	g_assert_false (lrg_touch_controls_is_visible (f->controls));
}

int
main (int    argc,
      char **argv)
{
	g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);

	g_test_add_func ("/touch/tracker/tap", test_tracker_tap);
	g_test_add_func ("/touch/tracker/tap-too-slow", test_tracker_tap_too_slow);
	g_test_add_func ("/touch/tracker/double-tap", test_tracker_double_tap);
	g_test_add_func ("/touch/tracker/double-tap-limits", test_tracker_double_tap_limits);
	g_test_add_func ("/touch/tracker/long-press", test_tracker_long_press);
	g_test_add_func ("/touch/tracker/drag", test_tracker_drag);
	g_test_add_func ("/touch/tracker/dp-scale", test_tracker_dp_scale);
	g_test_add_func ("/touch/tracker/pinch", test_tracker_pinch);
	g_test_add_func ("/touch/tracker/drag-becomes-pinch", test_tracker_drag_becomes_pinch);
	g_test_add_func ("/touch/tracker/two-finger-tap", test_tracker_two_finger_tap);
	g_test_add_func ("/touch/tracker/signal-and-reset", test_tracker_signal_and_reset);

	g_test_add ("/touch/controls/stick", Fixture, NULL, fixture_set_up, test_controls_stick, fixture_tear_down);
	g_test_add ("/touch/controls/stick-fixed-and-side", Fixture, NULL, fixture_set_up, test_controls_stick_fixed_and_side, fixture_tear_down);
	g_test_add ("/touch/controls/look", Fixture, NULL, fixture_set_up, test_controls_look, fixture_tear_down);
	g_test_add ("/touch/controls/pointer-passthrough", Fixture, NULL, fixture_set_up, test_controls_pointer_passthrough, fixture_tear_down);
	g_test_add ("/touch/controls/hold-button", Fixture, NULL, fixture_set_up, test_controls_hold_button, fixture_tear_down);
	g_test_add ("/touch/controls/tap-button", Fixture, NULL, fixture_set_up, test_controls_tap_button, fixture_tear_down);
	g_test_add ("/touch/controls/button-from-input-map", Fixture, NULL, fixture_set_up, test_controls_button_from_input_map, fixture_tear_down);
	g_test_add ("/touch/controls/ownership", Fixture, NULL, fixture_set_up, test_controls_ownership, fixture_tear_down);
	g_test_add ("/touch/controls/scale", Fixture, NULL, fixture_set_up, test_controls_scale, fixture_tear_down);
	g_test_add ("/touch/controls/modes", Fixture, NULL, fixture_set_up, test_controls_modes, fixture_tear_down);
	g_test_add ("/touch/controls/mouse-source", Fixture, NULL, fixture_set_up, test_controls_mouse_source, fixture_tear_down);
	g_test_add ("/touch/controls/draw-hidden", Fixture, NULL, fixture_set_up, test_controls_draw_hidden, fixture_tear_down);
	g_test_add ("/touch/layout/load", Fixture, NULL, fixture_set_up, test_layout_load, fixture_tear_down);
	g_test_add ("/touch/layout/file", Fixture, NULL, fixture_set_up, test_layout_file, fixture_tear_down);
	g_test_add ("/touch/layout/bad-unchanged", Fixture, NULL, fixture_set_up, test_layout_bad_unchanged, fixture_tear_down);

	return g_test_run ();
}
